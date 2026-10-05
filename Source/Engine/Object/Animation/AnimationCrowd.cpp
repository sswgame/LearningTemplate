#include "pch.h"

#include "Engine/Object/Animation/AnimationCrowd.h"

#include "Core/Delegate/Delegate.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Animation/AnimClip.h"
#include "Engine/Animation/AnimJsonUtil.h"
#include "Engine/Animation/Skeleton.h"
#include "Engine/Common/EngineParallel.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshVertexAnimation.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Utility/Json/JsonDocument.h"
#include "Engine/Utility/Profiling/FrameProfiler.h"

namespace sw
{
    SW_LOG_CALLER( "AnimationCrowd" );

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
                        _ppBucket[index]->evaluate( _clock );
                }
            };

            /** @brief 묶음 하나가 본 수십 개의 샘플 · 팔레트라 이 수 이상이면 나눈다(`AnimationSystem::kParallelUnitCount` 와 같은 값). */
            static constexpr uint32 kParallelBucketCount = 8;

            /** @brief 0 이상 @p period 미만으로 감습니다. */
            static float64 wrap( float64 value, float64 period ) { return value - MathUtil::floor( value / period ) * period; }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool AnimationCrowdSettings::parseJson( string_view json, string_view sourceLabel )
    {
        *this = AnimationCrowdSettings{};
        JsonDocument document;
        if ( document.parse( json, sourceLabel ) == false )
        {
            SW_LOG_ERROR( "Animation crowd settings '%#': malformed JSON", sourceLabel );
            return false;
        }
        if ( parseRoot( document.getRoot(), sourceLabel ) )
            return true;
        *this = AnimationCrowdSettings{};
        return false;
    }

    bool AnimationCrowdSettings::loadFromResource( string_view path )
    {
        string text;
        if ( ResourceUtil::readTextResource( path, text ) == false )
        {
            SW_LOG_ERROR( "Animation crowd settings '%#' could not be read", path );
            *this = AnimationCrowdSettings{};
            return false;
        }
        return parseJson( text, path );
    }

    bool AnimationCrowdSettings::parseRoot( const JsonValue& root, string_view sourceLabel )
    {
        if ( AnimJsonUtil::hasOnlyKnownKeys( root, { "variations_per_clip", "bucket_keep_seconds", "vertex_animation_frames_per_second" }, sourceLabel ) == false )
            return false;
        const JsonValue variations = root.get( "variations_per_clip" );
        const JsonValue keep       = root.get( "bucket_keep_seconds" );
        const JsonValue frameRate  = root.get( "vertex_animation_frames_per_second" );
        if ( variations.isNumber() == false || keep.isNumber() == false || frameRate.isNumber() == false || variations.asInt( 0 ) < 1 || frameRate.asFloat() <= 0.0 )
        {
            SW_LOG_ERROR( "Animation crowd settings '%#': variations_per_clip (>= 1), bucket_keep_seconds and vertex_animation_frames_per_second (> 0) are required",
                          sourceLabel );
            return false;
        }
        _variationsPerClip              = static_cast<uint32>( variations.asInt( 1 ) );
        _bucketKeepSeconds              = MathUtil::max( static_cast<float32>( keep.asFloat() ), 0.0f );
        _vertexAnimationFramesPerSecond = static_cast<float32>( frameRate.asFloat() );
        return true;
    }

    AnimationCrowdBucket::AnimationCrowdBucket( const AnimationCrowdBucketKey& key, shared_ptr<const Skeleton> skeleton, shared_ptr<const AnimClip> clip,
                                                shared_ptr<Mesh> mesh )
        : _key{ key }
        , _skeleton{ std::move( skeleton ) }
        , _clip{ std::move( clip ) }
        , _mesh{ std::move( mesh ) }
        , _listTrackToBone{}
        , _localPose{}
        , _scratchTrackPose{}
        , _listModelSpace{}
        , _listSkinPalette{}
        , _idleSince{ 0.0 }
        , _variationSpan{ 0.0 }
        , _memberCount{ 0 }
        , _visibleMemberCount{ 0 }
        , _referenceCount{ 0 }
        , _evaluationCount{ 0 }
    {
        _clip->makeTrackToBoneMap( *_skeleton, _listTrackToBone );
        _localPose.setToReference( *_skeleton );
        _localPose.computeModelSpace( _skeleton->getParentIndices(), _listModelSpace );
        Pose::computeSkinPalette( *_skeleton, _listModelSpace, _listSkinPalette );
    }

    float32 AnimationCrowdBucket::computeTime( float64 clock ) const
    {
        const float64 duration = static_cast<float64>( _clip->getDuration() );
        if ( duration <= 0.0 )
            return 0.0f;
        return static_cast<float32>( AnimationCrowdInternal::wrap( clock * static_cast<float64>( _key._playRate ) + static_cast<float64>( _key._variation ) * _variationSpan,
                                                                   duration ) );
    }

    void AnimationCrowdBucket::evaluate( float64 clock )
    {
        _localPose.setToReference( *_skeleton );
        (void)_clip->samplePose( computeTime( clock ), _listTrackToBone, _localPose, _scratchTrackPose, _key._bAnchorRootMotion == SW_TRUE );
        _localPose.computeModelSpace( _skeleton->getParentIndices(), _listModelSpace );
        Pose::computeSkinPalette( *_skeleton, _listModelSpace, _listSkinPalette );
        ++_evaluationCount;
    }

    AnimationCrowd::AnimationCrowd()
        : _settings{}
        , _listBucket{}
        , _mapBucket{}
        , _listScratchEvaluate{}
        , _mapFreeSoloMesh{}
        , _listVertexAnimation{}
        , _clock{ 0.0 }
        , _evaluatedBucketCount{ 0 }
        , _cookedVertexAnimationCount{ 0 }
    {
    }

    AnimationCrowd::~AnimationCrowd() = default;

    void AnimationCrowd::beginFrame( float32 deltaSeconds )
    {
        _clock += static_cast<float64>( MathUtil::max( deltaSeconds, 0.0f ) );
        for ( const unique_ptr<AnimationCrowdBucket>& bucket : _listBucket )
        {
            bucket->_memberCount        = 0;
            bucket->_visibleMemberCount = 0;
        }
    }

    uint32 AnimationCrowd::selectVariation( const AnimSharedPoseRequest& request, float32 duration ) const
    {
        // 묶음 v 의 시각 = 시계 × 속도 + v × (길이 / 칸 수). 유닛 시각과의 차이를 칸 폭으로 나눠 반올림하면 가장 가까운 칸이다.
        const uint32  variationCount = MathUtil::max( _settings._variationsPerClip, 1u );
        const float64 span           = static_cast<float64>( duration ) / static_cast<float64>( variationCount );
        const float64 offset         = AnimationCrowdInternal::wrap( static_cast<float64>( request._time ) - _clock * static_cast<float64>( request._playRate ),
                                                                     static_cast<float64>( duration ) );
        return static_cast<uint32>( MathUtil::round( offset / span ) ) % variationCount;
    }

    AnimationCrowdBucket* AnimationCrowd::joinBucket( AnimationCrowdBucket* pCurrent, const AnimSharedPoseRequest& request, const shared_ptr<const Skeleton>& skeleton,
                                                      const shared_ptr<Mesh>& sourceMesh, const shared_ptr<const AnimClip>& clip )
    {
        if ( request._pClip == nullptr || skeleton == nullptr || sourceMesh == nullptr || clip == nullptr || clip.get() != request._pClip ||
             clip->getDuration() <= 0.0f )
            return nullptr;
        AnimationCrowdBucketKey key{};
        key._pSkeleton         = skeleton.get();
        key._skinDataId        = sourceMesh->getSkinDataId();
        key._pClip             = request._pClip;
        key._playRate          = request._playRate;
        key._bAnchorRootMotion = request._bAnchorRootMotion;
        // 같은 상태면 칸을 바꾸지 않는다 — 유닛 시각과 묶음 시각은 같은 dt 로 흘러 어긋나지 않는다(바꾸면 메시가 바뀌어 배치가 다시 짜인다).
        if ( pCurrent != nullptr && pCurrent->_key.isSameState( key ) )
            return pCurrent;
        key._variation = selectVariation( request, clip->getDuration() );

        const auto found = _mapBucket.find( key );
        if ( found != _mapBucket.end() )
            return found->second;
        unique_ptr<AnimationCrowdBucket> bucket = sw::make_unique<AnimationCrowdBucket>( key, skeleton, clip, Mesh::createSkinInstance( *sourceMesh ) );
        bucket->_variationSpan                  = static_cast<float64>( clip->getDuration() ) / static_cast<float64>( MathUtil::max( _settings._variationsPerClip, 1u ) );
        bucket->_idleSince                      = _clock;
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
                bucket._idleSince = _clock;
                ++bucketIndex;
                continue;
            }
            if ( _clock - bucket._idleSince < static_cast<float64>( _settings._bucketKeepSeconds ) )
            {
                ++bucketIndex;
                continue;
            }
            _mapBucket.erase( bucket._key );
            _listBucket[bucketIndex] = std::move( _listBucket.back() );
            _listBucket.pop_back();
        }
    }

    shared_ptr<Mesh> AnimationCrowd::acquireSoloMesh( const shared_ptr<Mesh>& sourceMesh )
    {
        if ( sourceMesh == nullptr )
            return nullptr;
        vector<shared_ptr<Mesh>>& listFree = _mapFreeSoloMesh[sourceMesh->getSkinDataId()];
        if ( listFree.empty() == false )
        {
            shared_ptr<Mesh> mesh = std::move( listFree.back() );
            listFree.pop_back();
            return mesh;
        }
        return Mesh::createSkinInstance( *sourceMesh );
    }

    void AnimationCrowd::releaseSoloMesh( shared_ptr<Mesh> mesh )
    {
        if ( mesh == nullptr )
            return;
        _mapFreeSoloMesh[mesh->getSkinDataId()].push_back( std::move( mesh ) );
    }

    shared_ptr<Mesh> AnimationCrowd::findVertexAnimationMesh( const shared_ptr<Mesh>& sourceMesh, const Skeleton& skeleton, const AnimClip& clip, bool bAnchorRootMotion,
                                                              string_view meshPath )
    {
        if ( sourceMesh == nullptr )
            return nullptr;
        const uint64 skinDataId = sourceMesh->getSkinDataId();
        const uint8  anchor     = bAnchorRootMotion ? SW_TRUE : SW_FALSE;
        for ( const VertexAnimationEntry& entry : _listVertexAnimation )
        {
            if ( entry._skinDataId == skinDataId && entry._pSkeleton == &skeleton && entry._pClip == &clip && entry._bAnchorRootMotion == anchor )
                return entry._mesh;
        }
        // 쿠킹본이 있으면(배포본의 팩) 읽고, 없으면 처음 쓸 때 굽는다 — 같은 원본 · 클립의 먼 캐릭터는 이 메시 하나를 나눈다.
        // 굽지 못한 조합도 적어 두어 매 프레임 다시 굽지 않는다.
        SW_PROFILE_SCOPE( "GT.Animation.bakeVertexAnimation" );
        shared_ptr<MeshVertexAnimation> animation = make_shared<MeshVertexAnimation>();
        VertexAnimationEntry            entry{};
        entry._skinDataId        = skinDataId;
        entry._pSkeleton         = &skeleton;
        entry._pClip             = &clip;
        entry._bAnchorRootMotion = anchor;
        const string cookedPath  = MeshVertexAnimation::makeCookedPath( meshPath, clip.getName() );
        bool         bReady      = false;
        if ( cookedPath.empty() == false && ResourceUtil::hasResource( cookedPath ) && animation->loadFromResource( cookedPath ) )
        {
            // 쿠킹본은 같은 조건(정점 수 · 루트 묶기)일 때만 쓴다 — 아니면 굽는다.
            bReady = animation->_vertexCount == sourceMesh->getVertexCount() && animation->_bAnchorRootMotion == anchor;
            if ( bReady )
                ++_cookedVertexAnimationCount;
        }
        if ( bReady == false )
            bReady = MeshVertexAnimationBaker::bake( *sourceMesh, skeleton, clip, _settings._vertexAnimationFramesPerSecond, bAnchorRootMotion, *animation );
        if ( bReady )
        {
            entry._mesh = Mesh::create();
            entry._mesh->setVertices( sourceMesh->getVertices() );
            entry._mesh->setVertexAnimation( std::move( animation ) );
            SW_LOG_INFO( "Vertex animation '%#' ready (%# frames x %# vertices)", clip.getName().c_str(), entry._mesh->findVertexAnimation()->_frameCount,
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
        _mapFreeSoloMesh.clear();
        _listVertexAnimation.clear();
        _evaluatedBucketCount = 0;
    }
} // namespace sw
