#include "pch.h"

#include "Engine/Animation/Scene/AnimationCrowd.h"

#include "Core/Delegate/Delegate.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Animation/AnimationClip.h"
#include "Engine/Animation/Scene/MeshVertexAnimationBaker.h"
#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Content/AssetID.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Foundation/EngineParallel.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshVertexAnimation.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLFields.h"
#include "Engine/Serialization/XML/XMLNameCheck.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

namespace sw
{
    namespace
    {
        struct AnimationCrowdInternal
        {
            /** @brief 묶음 평가를 워커에 나누는 잡입니다. */
            struct EvaluateJob
            {
                AnimationCrowdBucket* const* _ppBucket{ nullptr };
                float64                      _clock{ 0.0 };

                void runRange( uint32 start, uint32 end )
                {
                    for ( uint32 index = start; index < end; ++index )
                    {
                        _ppBucket[index]->evaluate( _clock );
                    }
                }
            };

            /** @brief 묶음 하나가 본 수십 개의 샘플 · 팔레트라 이 수 이상이면 나눈다. */
            static constexpr uint32 kParallelBucketCount = 8;

            /** @brief 0 이상 @p period 미만으로 감습니다. */
            static float64 wrap( float64 value, float64 period ) { return value - MathUtil::floor( value / period ) * period; }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool AnimationCrowdSettings::loadFromXMLText( string_view xml, string_view sourceLabel )
    {
        XMLDocument document;
        XMLNode     root;
        return loadFromRoot( XMLReadUtil::parseRoot( document, xml, sourceLabel, "AnimationCrowdSettings", root ), root, sourceLabel );
    }

    bool AnimationCrowdSettings::loadFromRoot( XMLRootStatus status, const XMLNode& root, string_view sourceLabel )
    {
        *this = AnimationCrowdSettings{};
        if ( status != XMLRootStatus::Found )
        {
            SW_LOG_ERROR_ID( "AnimationCrowd.AnimationCrowdSettingsMalformed", "Animation crowd settings '%#': malformed XML (expected an <AnimationCrowdSettings> root)", sourceLabel );
            return false;
        }
        if ( parseRoot( root, sourceLabel ) )
            return true;
        *this = AnimationCrowdSettings{};
        return false;
    }

    bool AnimationCrowdSettings::loadFromAsset( const AssetID& assetID )
    {
        const string_view path = assetID.view();
        string            text;
        if ( AssetIO::readTextAsset( assetID, text ) == false )
        {
            SW_LOG_ERROR_ID( "AnimationCrowd.AnimationCrowdSettingsCould", "Animation crowd settings '%#' could not be read", path );
            *this = AnimationCrowdSettings{};
            return false;
        }
        XMLDocument document;
        XMLNode     root;
        return loadFromRoot( XMLReadUtil::parseResourceRoot( document, text, assetID, "AnimationCrowdSettings", root ), root, path );
    }

    bool AnimationCrowdSettings::parseRoot( const XMLNode& root, string_view sourceLabel )
    {
        // 셋 다 필수다. 범위 밖 값은 자르지 않고 거절한다(칸 · 범위는 PROPERTY 가 정본이다).
        AnimationCrowdSettings loaded;
        if ( XMLFields::readProperties( root, loaded, sourceLabel ) == false || XMLNameCheck::reportUnknownChildren( root, nullptr, 0, sourceLabel ) == false )
            return false;
        *this = loaded;
        return true;
    }

    AnimationCrowdBucket::AnimationCrowdBucket( const AnimationCrowdBucketKey& key, shared_ptr<const Skeleton> skeleton, shared_ptr<const AnimationClip> clip,
                                                shared_ptr<const AnimationClip> nextClip, shared_ptr<Mesh> mesh )
        : _key{ key }
        , _skeleton{ std::move( skeleton ) }
        , _clip{ std::move( clip ) }
        , _nextClip{ std::move( nextClip ) }
        , _mesh{ std::move( mesh ) }
        , _listTrackToBone{}
        , _listNextTrackToBone{}
        , _localPose{}
        , _nextPose{}
        , _scratchTrackPose{}
        , _listModelSpace{}
        , _listSkinPalette{}
        , _idleStartTime{ 0.0 }
        , _variationSpan{ 0.0 }
        , _blendStartClock{ 0.0 }
        , _nextStartTime{ 0.0f }
        , _blendAlpha{ 0.0f }
        , _memberCount{ 0 }
        , _visibleMemberCount{ 0 }
        , _referenceCount{ 0 }
        , _evaluationCount{ 0 }
    {
        // 오류는 함수가 남겼고 있는 본은 재생한다.
        (void)_clip->makeTrackToBoneMap( *_skeleton, _listTrackToBone );
        if ( _nextClip != nullptr )
            (void)_nextClip->makeTrackToBoneMap( *_skeleton, _listNextTrackToBone );
        _localPose.setToReference( *_skeleton );
        _localPose.computeModelSpace( _skeleton->getParentIndices(), _listModelSpace );
        refreshSkinPalette();
    }

    void AnimationCrowdBucket::refreshSkinPalette()
    {
        if ( _mesh != nullptr )
            Pose::computeSkinPalette( _mesh->getSkinBinding()._listInverseBind, _listModelSpace, _listSkinPalette );
    }

    float32 AnimationCrowdBucket::computeTime( float64 clock ) const
    {
        const float64 duration = static_cast<float64>( _clip->getDuration() );
        if ( duration <= 0.0 )
            return 0.0f;
        return static_cast<float32>( AnimationCrowdInternal::wrap( clock * static_cast<float64>( _key._playRate ) + static_cast<float64>( _key._variation ) * _variationSpan,
                                                                   duration ) );
    }

    float32 AnimationCrowdBucket::computeNextTime( float64 clock ) const
    {
        const float64 duration = _nextClip != nullptr ? static_cast<float64>( _nextClip->getDuration() ) : 0.0;
        if ( duration <= 0.0 )
            return 0.0f;
        const float64 elapsed = ( clock - _blendStartClock ) * static_cast<float64>( _key._playRate );
        return static_cast<float32>( AnimationCrowdInternal::wrap( static_cast<float64>( _nextStartTime ) + elapsed, duration ) );
    }

    void AnimationCrowdBucket::evaluate( float64 clock )
    {
        _localPose.setToReference( *_skeleton );
        (void)_clip->samplePose( *_skeleton, computeTime( clock ), _listTrackToBone, _localPose, _scratchTrackPose, _key._bAnchorRootMotion == SW_TRUE );
        if ( _nextClip != nullptr )
        {
            // 섞기 묶음 — 애니메이터의 페이드와 같은 식(나가는 포즈와 들어가는 포즈를 가중치로 섞는다).
            _nextPose.setToReference( *_skeleton );
            (void)_nextClip->samplePose( *_skeleton, computeNextTime( clock ), _listNextTrackToBone, _nextPose, _scratchTrackPose, _key._bAnchorRootMotion == SW_TRUE );
            Pose::blend( _localPose, _nextPose, _blendAlpha, _localPose );
        }
        _localPose.computeModelSpace( _skeleton->getParentIndices(), _listModelSpace );
        refreshSkinPalette();
        ++_evaluationCount;
    }

    AnimationCrowd::AnimationCrowd()
        : _settings{}
        , _listBucket{}
        , _mapBucket{}
        , _listScratchEvaluate{}
        , _listVertexAnimation{}
        , _clock{ 0.0 }
        , _frameIndex{ 0 }
        , _evaluatedBucketCount{ 0 }
    {
    }

    AnimationCrowd::~AnimationCrowd() = default;

    void AnimationCrowd::beginFrame( float32 deltaSeconds )
    {
        _clock += static_cast<float64>( MathUtil::max( deltaSeconds, 0.0f ) );
        ++_frameIndex;
        for ( const unique_ptr<AnimationCrowdBucket>& bucket : _listBucket )
        {
            bucket->_memberCount        = 0;
            bucket->_visibleMemberCount = 0;
        }
    }

    uint32 AnimationCrowd::selectVariation( const AnimationSharedPoseRequest& request, float32 duration ) const
    {
        // 묶음 v 의 시각 = 시계 × 속도 + v × (길이 / 칸 수). 유닛 시각과의 차이를 칸 폭으로 나눠 반올림하면 가장 가까운 칸이다.
        const uint32  variationCount = MathUtil::max( _settings._variationsPerClip, 1u );
        const float64 span           = static_cast<float64>( duration ) / static_cast<float64>( variationCount );
        const float64 offset         = AnimationCrowdInternal::wrap( static_cast<float64>( request._time ) - _clock * static_cast<float64>( request._playRate ),
                                                                     static_cast<float64>( duration ) );
        return static_cast<uint32>( MathUtil::round( offset / span ) ) % variationCount;
    }

    AnimationCrowdBucket* AnimationCrowd::joinBucket( AnimationCrowdBucket* pCurrent, const AnimationSharedPoseRequest& request, const shared_ptr<const Skeleton>& skeleton,
                                                      const shared_ptr<Mesh>& sourceMesh, const shared_ptr<const AnimationClip>& clip )
    {
        const bool bInvalidBucketRequest = request._pClip == nullptr || skeleton == nullptr || sourceMesh == nullptr || clip == nullptr || clip.get() != request._pClip ||
                                           clip->getDuration() <= 0.0f;
        if ( bInvalidBucketRequest )
            return nullptr;
        AnimationCrowdBucketKey key{};
        key._pSkeleton         = skeleton.get();
        key._skinDataID        = sourceMesh->getSkinDataID();
        key._pClip             = request._pClip;
        key._playRate          = request._playRate;
        key._bAnchorRootMotion = request._bAnchorRootMotion;
        key._skinningMethod    = request._skinningMethod;
        // 같은 상태면 칸을 바꾸지 않는다 — 유닛 시각과 묶음 시각은 같은 dt 로 흘러 어긋나지 않는다(바꾸면 메시가 바뀌어 배치가 다시 짜인다).
        if ( pCurrent != nullptr && pCurrent->_key.isSameState( key ) )
            return pCurrent;
        key._variation = selectVariation( request, clip->getDuration() );
        return findOrCreateBucket( key, skeleton, clip, nullptr, sourceMesh );
    }

    AnimationCrowdBucket* AnimationCrowd::joinBlendBucket( AnimationCrowdBucket* pCurrent, const AnimationSharedPoseRequest& request, const shared_ptr<const Skeleton>& skeleton,
                                                           const shared_ptr<Mesh>& sourceMesh, const shared_ptr<const AnimationClip>& clip,
                                                           const shared_ptr<const AnimationClip>& nextClip )
    {
        const bool bMissingInput        = request._pClip == nullptr || request._pNextClip == nullptr || skeleton == nullptr || sourceMesh == nullptr || clip == nullptr || nextClip == nullptr;
        const bool bInvalidBlendRequest = bMissingInput || clip.get() != request._pClip || nextClip.get() != request._pNextClip || clip->getDuration() <= 0.0f ||
                                          nextClip->getDuration() <= 0.0f;
        if ( bInvalidBlendRequest )
            return nullptr;
        AnimationCrowdBucketKey key{};
        key._pSkeleton         = skeleton.get();
        key._skinDataID        = sourceMesh->getSkinDataID();
        key._pClip             = request._pClip;
        key._playRate          = request._playRate;
        key._bAnchorRootMotion = request._bAnchorRootMotion;
        key._pNextClip         = request._pNextClip;
        key._fadeSeconds       = request._fadeSeconds;
        key._skinningMethod    = request._skinningMethod;
        // 이미 같은 전이의 섞기 묶음이면 그대로 — 시작 프레임은 처음 든 프레임이다.
        const bool bSameBlend = pCurrent != nullptr && pCurrent->isBlend() && pCurrent->_key._pSkeleton == key._pSkeleton && pCurrent->_key._skinDataID == key._skinDataID &&
                                pCurrent->_key._pClip == key._pClip && pCurrent->_key._pNextClip == key._pNextClip && pCurrent->_key._playRate == key._playRate &&
                                pCurrent->_key._fadeSeconds == key._fadeSeconds && pCurrent->_key._bAnchorRootMotion == key._bAnchorRootMotion &&
                                pCurrent->_key._skinningMethod == key._skinningMethod;
        if ( bSameBlend )
        {
            pCurrent->_blendAlpha = request._blendAlpha;
            return pCurrent;
        }
        // 출발 칸 — 같은 출발 상태의 묶음에서 왔으면 그 칸(보이던 포즈), 아니면 유닛 시각에 가까운 칸.
        AnimationCrowdBucketKey fromKey = key;
        fromKey._pNextClip              = nullptr;
        fromKey._fadeSeconds            = 0.0f;
        const bool bFromSameState       = pCurrent != nullptr && pCurrent->isBlend() == false && pCurrent->_key.isSameState( fromKey );
        key._variation                  = bFromSameState ? pCurrent->_key._variation : selectVariation( request, clip->getDuration() );
        key._blendStartFrame            = _frameIndex;
        const bool            bCreated  = _mapBucket.find( key ) == _mapBucket.end();
        AnimationCrowdBucket* pBucket   = findOrCreateBucket( key, skeleton, clip, nextClip, sourceMesh );
        if ( bCreated )
        {
            pBucket->_blendStartClock = _clock;
            pBucket->_nextStartTime   = request._nextTime;
        }
        pBucket->_blendAlpha = request._blendAlpha;
        return pBucket;
    }

    AnimationCrowdBucket* AnimationCrowd::joinFollowerBucket( AnimationCrowdBucket* pCurrent, const AnimationCrowdBucket& leaderBucket,
                                                              const shared_ptr<const Skeleton>& skeleton, const shared_ptr<Mesh>& sourceMesh, SkinningMethod skinningMethod )
    {
        if ( skeleton == nullptr || sourceMesh == nullptr )
            return nullptr;
        // 클립 · 속도 · 루트 고정 · 변형 칸까지 리더 묶음과 같다 — 같은 시각을 재생한다. 스켈레톤과 스킨 원본만 따르는 유닛의 것이다.
        // 리더가 칸을 옮기면 따라 옮긴다(`isSameState` 로 칸을 붙들지 않는다 — 붙들면 부품이 몸과 다른 위상으로 돈다).
        AnimationCrowdBucketKey key   = leaderBucket._key;
        key._pSkeleton                = skeleton.get();
        key._skinDataID               = sourceMesh->getSkinDataID();
        key._skinningMethod           = skinningMethod; // 부품은 제 방식으로 그린다 — 몸과 다르면 다른 묶음이다
        AnimationCrowdBucket* pBucket = ( pCurrent != nullptr && pCurrent->_key == key ) ? pCurrent : nullptr;
        if ( pBucket == nullptr )
        {
            const bool bCreated = _mapBucket.find( key ) == _mapBucket.end();
            pBucket             = findOrCreateBucket( key, skeleton, leaderBucket._clip, leaderBucket._nextClip, sourceMesh );
            if ( bCreated )
            {
                pBucket->_blendStartClock = leaderBucket._blendStartClock;
                pBucket->_nextStartTime   = leaderBucket._nextStartTime;
            }
        }
        // 섞기 묶음이면 가중치도 리더 묶음을 따른다(리더가 이번 프레임에 먼저 정했다).
        pBucket->_blendAlpha = leaderBucket._blendAlpha;
        return pBucket;
    }

    AnimationCrowdBucket* AnimationCrowd::findOrCreateBucket( const AnimationCrowdBucketKey& key, const shared_ptr<const Skeleton>& skeleton,
                                                              const shared_ptr<const AnimationClip>& clip, const shared_ptr<const AnimationClip>& nextClip,
                                                              const shared_ptr<Mesh>& sourceMesh )
    {
        const auto found = _mapBucket.find( key );
        if ( found != _mapBucket.end() )
            return found->second;
        // 묶음은 원본 메시를 그린다 — 결과는 묶음의 스킨 칸 하나라 사본이 필요 없다(AnimationSkinSlot).
        unique_ptr<AnimationCrowdBucket> bucket = sw::make_unique<AnimationCrowdBucket>( key, skeleton, clip, nextClip, sourceMesh );
        bucket->_variationSpan                  = static_cast<float64>( clip->getDuration() ) / static_cast<float64>( MathUtil::max( _settings._variationsPerClip, 1u ) );
        bucket->_idleStartTime                  = _clock;
        AnimationCrowdBucket* pBucket           = bucket.get();
        _listBucket.push_back( std::move( bucket ) );
        _mapBucket.emplace( key, pBucket );
        return pBucket;
    }

    void AnimationCrowd::addReference( AnimationCrowdBucket* pBucket )
    {
        if ( pBucket != nullptr )
            ++pBucket->_referenceCount;
    }

    void AnimationCrowd::releaseReference( AnimationCrowdBucket* pBucket )
    {
        if ( pBucket != nullptr && pBucket->_referenceCount > 0 )
            --pBucket->_referenceCount;
    }

    void AnimationCrowd::countMember( AnimationCrowdBucket& bucket, bool bVisible )
    {
        ++bucket._memberCount;
        if ( bVisible )
            ++bucket._visibleMemberCount;
    }

    void AnimationCrowd::evaluateBuckets()
    {
        _listScratchEvaluate.clear();
        for ( const unique_ptr<AnimationCrowdBucket>& bucket : _listBucket )
        {
            // 보이는 멤버가 없는 묶음은 쉰다 — 멤버가 모두 화면 밖이면 포즈를 만들 이유가 없다(유닛의 화면 밖 규칙과 같다).
            if ( bucket->_visibleMemberCount > 0 )
                _listScratchEvaluate.push_back( bucket.get() );
        }
        _evaluatedBucketCount = static_cast<uint32>( _listScratchEvaluate.size() );
        if ( _listScratchEvaluate.empty() )
            return;
        SW_PROFILE_SCOPE( "GT.Animation.crowdBuckets" );
        AnimationCrowdInternal::EvaluateJob job{};
        job._ppBucket = _listScratchEvaluate.data();
        job._clock    = _clock;
        engine::runParallel( static_cast<uint32>( _listScratchEvaluate.size() ), AnimationCrowdInternal::kParallelBucketCount,
                             SW_DELEGATE_METHOD( ParallelBlockDelegate, &AnimationCrowdInternal::EvaluateJob::runRange, &job ) );
    }

    void AnimationCrowd::endFrame()
    {
        // 멤버 · 참조가 없는 묶음은 쉰 지 오래면 지운다. 가리키는 유닛이 있으면(참조 수) 지우지 않는다.
        for ( size_t bucketIndex = 0; bucketIndex < _listBucket.size(); )
        {
            AnimationCrowdBucket& bucket = *_listBucket[bucketIndex];
            if ( bucket._memberCount > 0 || bucket._referenceCount > 0 )
            {
                bucket._idleStartTime = _clock;
                ++bucketIndex;
                continue;
            }
            if ( _clock - bucket._idleStartTime < static_cast<float64>( _settings._bucketKeepSeconds ) )
            {
                ++bucketIndex;
                continue;
            }
            _mapBucket.erase( bucket._key );
            _listBucket[bucketIndex] = std::move( _listBucket.back() );
            _listBucket.pop_back();
        }
    }

    shared_ptr<Mesh> AnimationCrowd::findVertexAnimationMesh( const shared_ptr<Mesh>& sourceMesh, const Skeleton& skeleton, const AnimationClip& clip, bool bAnchorRootMotion,
                                                              string_view meshPath )
    {
        if ( sourceMesh == nullptr )
            return nullptr;
        const IdentityToken& skinDataID = sourceMesh->getSkinDataID();
        const uint8          anchor     = bAnchorRootMotion ? SW_TRUE : SW_FALSE;
        for ( const VertexAnimationEntry& entry : _listVertexAnimation )
        {
            const bool bCachedVertexAnimation = entry._skinDataID == skinDataID && entry._pSkeleton == &skeleton && entry._pClip == &clip && entry._bAnchorRootMotion == anchor;
            if ( bCachedVertexAnimation )
                return entry._mesh;
        }
        // 쿠킹본이 있으면(배포본의 팩) 읽고, 없으면 처음 쓸 때 베이크한다 — 같은 원본 · 클립의 먼 캐릭터는 이 메시 하나를 나눈다.
        // 베이크하지 못한 조합도 적어 두어 매 프레임 다시 베이크하지 않는다.
        SW_PROFILE_SCOPE( "GT.Animation.bakeVertexAnimation" );
        shared_ptr<MeshVertexAnimation> animation = make_shared<MeshVertexAnimation>();
        VertexAnimationEntry            entry{};
        entry._skinDataID               = skinDataID;
        entry._pSkeleton                = &skeleton;
        entry._pClip                    = &clip;
        entry._bAnchorRootMotion        = anchor;
        const string cookedPath         = MeshVertexAnimation::makeCookedPath( meshPath, clip.getName() );
        bool         bReady             = false;
        const bool   bCookedBinaryFound = cookedPath.empty() == false && AssetIO::hasAsset( AssetID{ cookedPath } ) && animation->loadFromAsset( AssetID{ cookedPath } );
        if ( bCookedBinaryFound )
        {
            // 쿠킹본은 같은 조건(정점 수 · 루트 묶기)일 때만 쓴다 — 아니면 베이크한다.
            bReady = animation->_vertexCount == sourceMesh->getVertexCount() && animation->_bAnchorRootMotion == anchor;
        }
        if ( bReady == false )
            bReady = MeshVertexAnimationBaker::bake( *sourceMesh, skeleton, clip, _settings._vertexAnimationFramesPerSecond, bAnchorRootMotion, *animation );
        if ( bReady )
        {
            entry._mesh = Mesh::create();
            // 원본에서 이미 검사한 정점 · 인덱스다. 정점 순서가 원본과 같아 VAT 표(정점 번호)와 구간(인덱스 범위)도 그대로다.
            (void)entry._mesh->setGeometryWithTangents( vector<RHIVertex>( sourceMesh->getVertices() ), vector<uint32>( sourceMesh->getIndices() ) );
            (void)entry._mesh->setSections( sourceMesh->getSections() );
            entry._mesh->setVertexAnimation( std::move( animation ) );
            SW_LOG_INFO_ID( "AnimationCrowd.VertexAnimationReadyFrames", "Vertex animation '%#' ready (%# frames x %# vertices)", clip.getName().c_str(), entry._mesh->findVertexAnimation()->_frameCount,
                            entry._mesh->getVertexCount() );
        }
        _listVertexAnimation.push_back( entry );
        return entry._mesh;
    }

    void AnimationCrowd::clear()
    {
        _mapBucket.clear();
        _listBucket.clear();
        _listScratchEvaluate.clear();
        _listVertexAnimation.clear();
        _evaluatedBucketCount = 0;
    }

    /** @brief 군중 공유 표 데이터 파일 종류입니다(스키마 테스트와 카탈로그 편집기가 이 줄로 파일을 읽는다). */
    SW_ASSET_TYPE( AnimationCrowd, AssetTypeDef::makeDataFile( "animationcrowd", AssetPathRule{ {}, {}, "/animation/animationcrowd.xml" }, AssetTypeLoader::of<AnimationCrowdSettings>() ) );
} // namespace sw
