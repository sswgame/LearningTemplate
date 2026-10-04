#include "pch.h"

#include "Engine/Object/Component/3D/SkeletalMeshComponent.h"

#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Animation/Skeleton.h"
#include "Engine/Animation/SkeletonBoneLod.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Resource/AnimationAssetCache.h"
#include "Engine/Resource/ResourceUtil.h"

namespace sw
{
    SW_LOG_CALLER( "SkeletalMeshComponent" );

    namespace
    {
        struct SkeletalMeshComponentInternal
        {
            /** @brief 본 하나("root", 단위 변환)짜리 스켈레톤을 만듭니다. */
            static shared_ptr<const Skeleton> createImplicitSkeleton()
            {
                shared_ptr<Skeleton> skeleton = make_shared<Skeleton>();
                (void)skeleton->addBone( hashed_string( "root" ), -1, BoneTransform{}, float4x4::Identity );
                return skeleton;
            }

            /** @brief 스켈레톤이 없는 부품이 받는 본 하나짜리 스켈레톤입니다(프로세스에 하나, 읽기만). */
            static const shared_ptr<const Skeleton>& getImplicitSkeleton()
            {
                static const shared_ptr<const Skeleton> s_skeleton = createImplicitSkeleton();
                return s_skeleton;
            }

            /** @brief 스킨드 메시의 경계 반지름 여유입니다 — 포즈가 바인드 포즈 밖으로 정점을 옮겨도 GPU 컬링에 잘리지 않게 합니다. */
            static constexpr float32 kSkinnedBoundsScale = 1.5f;

            /** @brief 핸들에서 주기 위상을 냅니다(정수 해시 — 이웃한 id 가 이웃한 위상이 되지 않게 섞는다). */
            static uint32 makeUpdatePhase( uint64 id )
            {
                uint64 mixed = id * 0x9E3779B97F4A7C15ull;
                mixed ^= mixed >> 29;
                return static_cast<uint32>( mixed & 0xFFFFu );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SkeletalMeshLodClient::SkeletalMeshLodClient( SkeletalMeshComponent& owner )
        : _owner{ owner }
    {
    }

    bool SkeletalMeshLodClient::findAnimationLodBounds( float3& outCenter, float32& outRadius ) const
    {
        return _owner.getWorldBounds( outCenter, outRadius );
    }

    const SkeletonBoneLod* SkeletalMeshLodClient::findBoneLod() const
    {
        return _owner.findBoneLod();
    }

    void SkeletalMeshLodClient::applyAnimationLod( const AnimationLodState& state )
    {
        _owner.applyAnimationLod( state );
    }

    SkeletalMeshComponent::SkeletalMeshComponent()
        : _skeletonPath{}
        , _updateRateDivisor{ 1 }
        , _skeleton{ SkeletalMeshComponentInternal::getImplicitSkeleton() }
        , _skinSourceMesh{}
        , _skinSourceContentId{ 0 }
        , _localPose{}
        , _listModelSpace{}
        , _listSkinPalette{}
        , _listDependency{}
        , _listTask{}
        , _listLeaderBone{}
        , _leader{}
        , _pLeaderSkeletonForMap{ nullptr }
        , _pAnimationSystem{ nullptr }
        , _frameContext{}
        , _poseEvaluationCount{ 0 }
        , _lodClient{ *this }
        , _lodState{}
        , _boneLod{}
        , _listBoneLodMask{}
        , _boneLodRevision{ 0 }
        , _pBoneLodMaskSkeleton{ nullptr }
        , _interpolationFrom{}
        , _interpolationTarget{}
        , _updatePhase{ 0 }
        , _framesSinceEvaluation{ 0 }
        , _effectiveDivisor{ 1 }
        , _bFollowParentPose{ SW_FALSE }
        , _bAnimateWhenOffscreen{ SW_FALSE }
        , _bVisibleHint{ SW_TRUE }
        , _bPoseDirty{ SW_TRUE }
        , _bInterpolateFrame{ SW_FALSE }
        , _bInterpolationReady{ SW_FALSE }
        , _reserved{ 0 }
    {
        resetPoseBuffers();
    }

    SkeletalMeshComponent::~SkeletalMeshComponent()
    {
        // 등록된 일에 알린다 — 일이 이 유닛 포인터를 들고 있으면 놓게 한다.
        for ( IAnimationPhaseTask* pTask : _listTask )
            pTask->onAnimationUnitDetached( *this );
        _listTask.clear();
    }

    void SkeletalMeshComponent::onBeginPlay()
    {
        MeshComponent::onBeginPlay();
        if ( _bFollowParentPose == SW_TRUE && getOwner() != nullptr )
        {
            GameObject* pParent = getOwner()->getParent();
            if ( pParent != nullptr )
                setLeaderPose( pParent->getComponent<SkeletalMeshComponent>() );
        }
    }

    void SkeletalMeshComponent::onRegister( GameObjectManager& manager )
    {
        MeshComponent::onRegister( manager );
        _pAnimationSystem = &manager.getAnimationSystem();
        _pAnimationSystem->registerUnit( this );
        _pAnimationSystem->registerLodClient( &_lodClient );
        _updatePhase = SkeletalMeshComponentInternal::makeUpdatePhase( getHandle().componentId() );
    }

    void SkeletalMeshComponent::onUnregister( GameObjectManager& manager )
    {
        manager.getAnimationSystem().unregisterLodClient( &_lodClient );
        manager.getAnimationSystem().unregisterUnit( this );
        _pAnimationSystem = nullptr;
        MeshComponent::onUnregister( manager );
    }

    void SkeletalMeshComponent::onPropertyChanged( hashed_string propertyName )
    {
        MeshComponent::onPropertyChanged( propertyName );
        static const hashed_string s_skeletonPathName( "_skeletonPath" );
        static const hashed_string s_divisorName( "_updateRateDivisor" );
        if ( propertyName == s_skeletonPathName )
            resolveSkeleton();
        else if ( propertyName == s_divisorName )
            setUpdateRateDivisor( _updateRateDivisor );
    }

    void SkeletalMeshComponent::resolveRenderAssets()
    {
        MeshComponent::resolveRenderAssets();
        resolveSkinInstanceMesh();
        resolveSkeleton();
    }

    void SkeletalMeshComponent::setSkeletonPath( string_view path )
    {
        _skeletonPath = string{ path };
        resolveSkeleton();
    }

    void SkeletalMeshComponent::setSkeleton( shared_ptr<const Skeleton> skeleton )
    {
        _skeleton = ( skeleton != nullptr ) ? std::move( skeleton ) : SkeletalMeshComponentInternal::getImplicitSkeleton();
        resetPoseBuffers();
    }

    const Skeleton& SkeletalMeshComponent::getSkeleton() const
    {
        return *_skeleton;
    }

    void SkeletalMeshComponent::resolveSkeleton()
    {
        shared_ptr<const Skeleton> skeleton;
        if ( _skeletonPath.empty() == false )
        {
            skeleton = SkeletonCache::acquire( _skeletonPath );
            if ( skeleton == nullptr )
                SW_LOG_ERROR( "Skeleton '%#' could not be loaded - using a one-bone implicit skeleton", _skeletonPath.c_str() );
        }
        if ( skeleton == nullptr )
            skeleton = SkeletalMeshComponentInternal::getImplicitSkeleton();
        if ( skeleton != _skeleton )
            setSkeleton( std::move( skeleton ) );
        // 본 LOD 는 이 스켈레톤에 맞춰 마스크를 짓는다 — 스켈레톤을 정한 뒤다.
        resolveBoneLod();
    }

    void SkeletalMeshComponent::resolveBoneLod()
    {
        // 본 LOD 는 스켈레톤 곁의 선택 파일이다 — 없으면 조용히 본 LOD 없이 간다.
        const string path = SkeletonBoneLod::makePathForSkeleton( _skeletonPath );
        if ( path.empty() || ResourceUtil::hasResource( path ) == false )
        {
            setBoneLod( nullptr );
            return;
        }
        shared_ptr<const SkeletonBoneLod> boneLod = SkeletonBoneLodCache::acquire( path );
        if ( boneLod != _boneLod )
            setBoneLod( std::move( boneLod ) );
    }

    void SkeletalMeshComponent::setBoneLod( shared_ptr<const SkeletonBoneLod> boneLod )
    {
        _boneLod         = std::move( boneLod );
        _boneLodRevision = 0;
        _listBoneLodMask.clear();
        refreshBoneLodMasks();
    }

    void SkeletalMeshComponent::refreshBoneLodMasks()
    {
        if ( _boneLod == nullptr )
        {
            _listBoneLodMask.clear();
            return;
        }
        if ( _boneLodRevision == _boneLod->getRevision() && _pBoneLodMaskSkeleton == _skeleton.get() )
            return;
        _boneLodRevision      = _boneLod->getRevision();
        _pBoneLodMaskSkeleton = _skeleton.get();
        // 스켈레톤에 없는 본 이름은 데이터 오류다 — 알리고 본 LOD 없이 간다.
        if ( _boneLod->buildMasks( *_skeleton, _listBoneLodMask, _skeletonPath ) == false )
            _listBoneLodMask.clear();
    }

    const uint8* SkeletalMeshComponent::findBoneLodMask() const
    {
        const uint32 level = _lodState._boneLodLevel;
        if ( level == 0 || level > _listBoneLodMask.size() )
            return nullptr;
        const vector<uint8>& mask = _listBoneLodMask[level - 1];
        return mask.size() == _skeleton->getBoneCount() ? mask.data() : nullptr;
    }

    void SkeletalMeshComponent::applyAnimationLod( const AnimationLodState& state )
    {
        _lodState = state;
        setVisibleHint( state._bVisible == SW_TRUE );
    }

    void SkeletalMeshComponent::resolveSkinInstanceMesh()
    {
        const shared_ptr<Mesh>& current = getMesh();
        if ( current == nullptr || current->hasSkin() == false )
            return;
        // 지금 메시가 이미 이 컴포넌트의 복사본이고 원본이 그대로면 둔다. 원본이 바뀌었으면(핫 리로드 · 다른 id) 새 복사본을 만든다 —
        // 옛 복사본은 스냅샷이 쥔 동안 산다(정점 버퍼를 제자리에서 바꾸지 않는다).
        const bool bOwnCopy = _skinSourceMesh != nullptr && current != _skinSourceMesh && _skinSourceMesh->getContentId() == _skinSourceContentId;
        if ( bOwnCopy )
            return;

        shared_ptr<Mesh> source   = ( _skinSourceMesh != nullptr && current != _skinSourceMesh ) ? _skinSourceMesh : current;
        shared_ptr<Mesh> instance = Mesh::create();
        instance->setVertices( source->getVertices() );
        instance->setSkin( source->getSkinVertices(), source->getSkinBoneCount() );
        _skinSourceMesh      = source;
        _skinSourceContentId = source->getContentId();
        setBoundsRadius( source->getBoundingRadius() * SkeletalMeshComponentInternal::kSkinnedBoundsScale );
        setMesh( std::move( instance ) );
    }

    void SkeletalMeshComponent::resetPoseBuffers()
    {
        _localPose.setToReference( *_skeleton );
        _localPose.computeModelSpace( _skeleton->getParentIndices(), _listModelSpace );
        Pose::computeSkinPalette( *_skeleton, _listModelSpace, _listSkinPalette );
        _pLeaderSkeletonForMap = nullptr;
        _bPoseDirty            = SW_TRUE;
        _bInterpolationReady   = SW_FALSE;
        _pBoneLodMaskSkeleton  = nullptr;
        refreshBoneLodMasks();
        const shared_ptr<Mesh>& mesh = getMesh();
        if ( mesh != nullptr && mesh->hasSkin() && mesh->getSkinBoneCount() != _skeleton->getBoneCount() )
            SW_LOG_ERROR( "Skinned mesh '%#' expects %# bones but skeleton '%#' has %#", getMeshId().c_str(), mesh->getSkinBoneCount(), _skeletonPath.c_str(),
                          _skeleton->getBoneCount() );
    }

    bool SkeletalMeshComponent::findBoneModelTransform( const hashed_string& boneName, float4x4& outTransform ) const
    {
        const int32 boneIndex = _skeleton->findBoneIndex( boneName );
        if ( boneIndex < 0 || static_cast<size_t>( boneIndex ) >= _listModelSpace.size() )
            return false;
        outTransform = _listModelSpace[static_cast<size_t>( boneIndex )];
        return true;
    }

    void SkeletalMeshComponent::setLeaderPose( SkeletalMeshComponent* pLeader )
    {
        SkeletalMeshComponent* pOld = findLeaderPose();
        if ( pOld == pLeader )
            return;
        if ( pOld != nullptr )
            removeAnimationDependency( pOld );
        _leader                = ( pLeader != nullptr && pLeader != this ) ? pLeader->getHandle() : ComponentHandle{};
        _pLeaderSkeletonForMap = nullptr;
        if ( pLeader != nullptr && pLeader != this )
            addAnimationDependency( pLeader );
        _bPoseDirty = SW_TRUE;
        notifyOrderChanged();
    }

    SkeletalMeshComponent* SkeletalMeshComponent::findLeaderPose() const
    {
        if ( _leader.isValid() == false || getOwner() == nullptr || getOwner()->getManager() == nullptr )
            return nullptr;
        return castTo<SkeletalMeshComponent>( getOwner()->getManager()->resolveComponent( _leader ) );
    }

    void SkeletalMeshComponent::addAnimationDependency( SkeletalMeshComponent* pUpstream )
    {
        if ( pUpstream == nullptr || pUpstream == this )
            return;
        const ComponentHandle handle = pUpstream->getHandle();
        if ( std::find( _listDependency.begin(), _listDependency.end(), handle ) != _listDependency.end() )
            return;
        _listDependency.push_back( handle );
        notifyOrderChanged();
    }

    void SkeletalMeshComponent::removeAnimationDependency( SkeletalMeshComponent* pUpstream )
    {
        if ( pUpstream == nullptr )
            return;
        const auto it = std::find( _listDependency.begin(), _listDependency.end(), pUpstream->getHandle() );
        if ( it == _listDependency.end() )
            return;
        _listDependency.erase( it );
        notifyOrderChanged();
    }

    void SkeletalMeshComponent::addAnimationPhaseTask( IAnimationPhaseTask* pTask )
    {
        if ( pTask == nullptr || std::find( _listTask.begin(), _listTask.end(), pTask ) != _listTask.end() )
            return;
        _listTask.push_back( pTask );
        _bPoseDirty = SW_TRUE;
    }

    void SkeletalMeshComponent::removeAnimationPhaseTask( IAnimationPhaseTask* pTask )
    {
        const auto it = std::find( _listTask.begin(), _listTask.end(), pTask );
        if ( it == _listTask.end() )
            return;
        _listTask.erase( it );
        pTask->onAnimationUnitDetached( *this );
        _bPoseDirty = SW_TRUE;
    }

    void SkeletalMeshComponent::setUpdateRateDivisor( uint32 divisor )
    {
        _updateRateDivisor = MathUtil::max( divisor, 1u );
    }

    void SkeletalMeshComponent::notifyOrderChanged()
    {
        if ( _pAnimationSystem != nullptr )
            _pAnimationSystem->markOrderDirty();
    }

    bool SkeletalMeshComponent::beginAnimationFrame( float32 deltaSeconds, uint64 frameIndex )
    {
        // 스켈레톤이 제자리에서 다시 읽혔으면(에셋 핫 리로드) 본 수가 달라질 수 있다 — 포즈 버퍼를 다시 맞춘다.
        if ( _localPose.getBoneCount() != _skeleton->getBoneCount() )
            resetPoseBuffers();
        // 본 LOD 표가 제자리에서 다시 읽혔으면(핫 리로드) 마스크를 다시 짓는다.
        if ( _boneLod != nullptr && _boneLodRevision != _boneLod->getRevision() )
            refreshBoneLodMasks();
        bool bActive = _bPoseDirty == SW_TRUE || _leader.isValid();
        for ( const IAnimationPhaseTask* pTask : _listTask )
            bActive = bActive || pTask->isAnimationActive();
        if ( bActive == false )
            return false;

        // 갱신 주기: 시간 단계는 매 프레임 돌고(알림 · 루트 모션이 늦지 않게), 포즈는 주기마다 만든다. 화면 밖이면 포즈를 건너뛴다.
        // 주기는 PROPERTY(하한)와 LOD 판정 중 큰 쪽이고, 위상을 더해 같은 주기의 유닛이 한 프레임에 몰리지 않게 한다.
        _effectiveDivisor           = MathUtil::max( _updateRateDivisor, MathUtil::max( _lodState._updateRateDivisor, 1u ) );
        const bool bOnRate          = AnimationLodUtil::isOnUpdateFrame( frameIndex, _updatePhase, _effectiveDivisor );
        const bool bOnScreen        = _bVisibleHint == SW_TRUE || _bAnimateWhenOffscreen == SW_TRUE;
        const bool bPoseDirty       = _bPoseDirty == SW_TRUE;
        _frameContext._deltaSeconds = deltaSeconds;
        _frameContext._frameIndex   = frameIndex;
        _frameContext._bPoseNeeded  = ( bPoseDirty || ( bOnRate && bOnScreen ) ) ? SW_TRUE : SW_FALSE;
        // 건너뛰는 프레임 — 보이고 보간이 켜졌으면 직전 두 포즈 사이를 채운다(평가는 하지 않는다).
        const bool bInterpolationOn = _lodState._bInterpolate == SW_TRUE && _effectiveDivisor > 1 && bOnScreen;
        if ( bInterpolationOn == false )
            _bInterpolationReady = SW_FALSE;
        _bInterpolateFrame = ( _frameContext._bPoseNeeded == SW_FALSE && bInterpolationOn && _bInterpolationReady == SW_TRUE ) ? SW_TRUE : SW_FALSE;
        return true;
    }

    void SkeletalMeshComponent::applySkippedFrameInterpolation( bool bEvaluatedThisFrame )
    {
        // 언리얼 URO 보간과 같은 모양: 평가한 프레임에 "지금 보이던 포즈 → 새 포즈" 를 잡고, 주기 동안 그 사이를 1/주기씩 간다(한 주기 늦다).
        const bool bInterpolationOn = _lodState._bInterpolate == SW_TRUE && _effectiveDivisor > 1;
        if ( bEvaluatedThisFrame )
        {
            _framesSinceEvaluation = 0;
            if ( bInterpolationOn == false || _interpolationFrom.getBoneCount() != _localPose.getBoneCount() )
            {
                _bInterpolationReady = SW_FALSE;
                return;
            }
            _interpolationTarget = _localPose;
            _bInterpolationReady = SW_TRUE;
        }
        else
        {
            if ( _bInterpolateFrame == SW_FALSE )
                return;
            ++_framesSinceEvaluation;
        }
        const float32 alpha = MathUtil::min( static_cast<float32>( _framesSinceEvaluation + 1 ) / static_cast<float32>( _effectiveDivisor ), 1.0f );
        Pose::blend( _interpolationFrom, _interpolationTarget, alpha, _localPose );
        _localPose.computeModelSpace( _skeleton->getParentIndices(), _listModelSpace );
    }

    void SkeletalMeshComponent::runTasks( AnimationPhase phase )
    {
        for ( IAnimationPhaseTask* pTask : _listTask )
            pTask->runAnimationPhase( phase, *this, _frameContext );
    }

    void SkeletalMeshComponent::copyLeaderPose( const SkeletalMeshComponent& leader )
    {
        const Skeleton& leaderSkeleton = leader.getSkeleton();
        if ( _pLeaderSkeletonForMap != &leaderSkeleton )
        {
            // 리더 스켈레톤이 바뀌었을 때만 이름 표를 다시 짓는다(본 수십 개의 이름 찾기).
            _listLeaderBone.resize( _skeleton->getBoneCount() );
            for ( uint32 boneIndex = 0; boneIndex < _skeleton->getBoneCount(); ++boneIndex )
                _listLeaderBone[boneIndex] = leaderSkeleton.findBoneIndex( _skeleton->getBone( boneIndex )._name );
            _pLeaderSkeletonForMap = &leaderSkeleton;
        }
        const Pose& leaderPose = leader.getLocalPose();
        for ( uint32 boneIndex = 0; boneIndex < static_cast<uint32>( _listLeaderBone.size() ); ++boneIndex )
        {
            const int32 leaderBone = _listLeaderBone[boneIndex];
            if ( leaderBone >= 0 && static_cast<uint32>( leaderBone ) < leaderPose.getBoneCount() )
                _localPose.setBoneTransform( boneIndex, leaderPose.getBoneTransform( static_cast<uint32>( leaderBone ) ) );
        }
    }

    void SkeletalMeshComponent::runAnimationPhase( AnimationPhase phase )
    {
        const bool bPoseNeeded = _frameContext._bPoseNeeded == SW_TRUE;
        switch ( phase )
        {
            case AnimationPhase::Time:
            {
                runTasks( phase );
                break;
            }
            case AnimationPhase::BasePose:
            {
                if ( bPoseNeeded == false )
                    break;
                // 보간의 출발점 — 지금 보이던 포즈(직전 프레임의 결과)다.
                if ( _lodState._bInterpolate == SW_TRUE && _effectiveDivisor > 1 )
                    _interpolationFrom = _localPose;
                _localPose.setToReference( *_skeleton );
                // 리더는 의존이라 이 레벨보다 먼저 평가됐다 — 그 로컬 포즈를 읽는다.
                const SkeletalMeshComponent* pLeader = findLeaderPose();
                if ( pLeader != nullptr )
                    copyLeaderPose( *pLeader );
                runTasks( phase );
                _localPose.computeModelSpace( _skeleton->getParentIndices(), _listModelSpace );
                break;
            }
            case AnimationPhase::Attachment:
            {
                if ( bPoseNeeded )
                    runTasks( phase );
                break;
            }
            case AnimationPhase::PostProcess:
            {
                if ( bPoseNeeded == false || _listTask.empty() )
                    break;
                runTasks( phase );
                _localPose.computeModelSpace( _skeleton->getParentIndices(), _listModelSpace );
                break;
            }
            case AnimationPhase::SkinPalette:
            {
                if ( bPoseNeeded == false && _bInterpolateFrame == SW_FALSE )
                    break;
                applySkippedFrameInterpolation( bPoseNeeded );
                Pose::computeSkinPalette( *_skeleton, _listModelSpace, _listSkinPalette );
                if ( bPoseNeeded )
                {
                    ++_poseEvaluationCount;
                    _bPoseDirty = SW_FALSE;
                }
                break;
            }
            case AnimationPhase::Count:
            {
                break;
            }
        }
    }

    void SkeletalMeshComponent::finishAnimationFrame()
    {
        for ( IAnimationPhaseTask* pTask : _listTask )
            pTask->finishAnimationFrame( *this );
    }

    void SkeletalMeshComponent::collectSyncPlayers( vector<AnimPlayer*>& inoutListPlayer, vector<float32>& inoutListWeight, vector<hashed_string>& inoutListGroup )
    {
        for ( IAnimationPhaseTask* pTask : _listTask )
        {
            hashed_string groupName{};
            float32       weight  = 0.0f;
            AnimPlayer*   pPlayer = pTask->findSyncPlayer( groupName, weight );
            if ( pPlayer == nullptr || groupName.empty() )
                continue;
            inoutListPlayer.push_back( pPlayer );
            inoutListWeight.push_back( weight );
            inoutListGroup.push_back( groupName );
        }
    }
} // namespace sw
