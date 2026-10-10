#include "pch.h"

#include "Engine/Object/Component/3D/SkeletalMeshComponent.h"

#include "Core/Common/HashUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Animation/AnimationAssetCache.h"
#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Animation/Skeletal/SkeletonBoneLOD.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
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
                uint64 mixed = id * HashUtil::kGoldenRatio64;
                mixed ^= mixed >> 29;
                return static_cast<uint32>( mixed & 0xFFFFu );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SkeletalMeshLODClient::SkeletalMeshLODClient( SkeletalMeshComponent& owner )
        : _owner{ owner }
    {
    }

    bool SkeletalMeshLODClient::findAnimationLODBounds( float3& outCenter, float32& outRadius ) const
    {
        return _owner.getWorldBounds( outCenter, outRadius );
    }

    const SkeletonBoneLOD* SkeletalMeshLODClient::findBoneLOD() const
    {
        return _owner.findBoneLOD();
    }

    void SkeletalMeshLODClient::applyAnimationLOD( const AnimationLODState& state )
    {
        _owner.applyAnimationLOD( state );
    }

    SkeletalMeshComponent::SkeletalMeshComponent()
        : _skeletonPath{}
        , _updateRateDivisor{ 1 }
        , _skeleton{ SkeletalMeshComponentInternal::getImplicitSkeleton() }
        , _skinSourceMesh{}
        , _skinSourceContentID{ 0 }
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
        , _boneLOD{}
        , _listBoneLODMask{}
        , _boneLODRevision{ 0 }
        , _pBoneLODMaskSkeleton{ nullptr }
        , _interpolationFrom{}
        , _interpolationTarget{}
        , _updatePhase{ 0 }
        , _framesSinceEvaluation{ 0 }
        , _effectiveDivisor{ 1 }
        , _listMorphWeight{}
        , _pCrowdBucket{ nullptr }
        , _soloMesh{}
        , _pVertexAnimationClip{ nullptr }
        , _crowdMode{ AnimationCrowdMode::Own }
        , _bFollowParentPose{ SW_FALSE }
        , _bAnimateWhenOffscreen{ SW_FALSE }
        , _bVisibleHint{ SW_TRUE }
        , _bPoseDirty{ SW_TRUE }
        , _bInterpolateFrame{ SW_FALSE }
        , _bInterpolationReady{ SW_FALSE }
        , _bShareCrowdPose{ SW_FALSE }
        , _bHasDependents{ SW_FALSE }
        , _bRuntimeSkeleton{ SW_FALSE }
        , _reserved{ 0 }
    {
        resetPoseBuffers();
    }

    SkeletalMeshComponent::~SkeletalMeshComponent()
    {
        leaveCrowd( nullptr );
        // 등록된 일에 알린다 — 일이 이 유닛 포인터를 들고 있으면 놓게 한다.
        for ( IAnimationPhaseTask* pTask : _listTask )
        {
            pTask->onAnimationUnitDetached( *this );
        }
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
        _pAnimationSystem->registerLODClient( &_lodClient );
        _updatePhase = SkeletalMeshComponentInternal::makeUpdatePhase( getHandle().componentID() );
    }

    void SkeletalMeshComponent::onUnregister( GameObjectManager& manager )
    {
        leaveCrowd( &manager.getAnimationSystem().getCrowd() );
        manager.getAnimationSystem().unregisterLODClient( &_lodClient );
        manager.getAnimationSystem().unregisterUnit( this );
        _pAnimationSystem = nullptr;
        MeshComponent::onUnregister( manager );
    }

    void SkeletalMeshComponent::onPropertyChanged( hashed_string propertyName )
    {
        MeshComponent::onPropertyChanged( propertyName );
        static const hashed_string s_skeletonPathName( "_skeletonPath" );
        static const hashed_string s_divisorName( "_updateRateDivisor" );
        static const hashed_string s_shareCrowdName( "_bShareCrowdPose" );
        if ( propertyName == s_skeletonPathName )
            resolveSkeleton();
        else if ( propertyName == s_divisorName )
            setUpdateRateDivisor( _updateRateDivisor );
        else if ( propertyName == s_shareCrowdName )
            setShareCrowdPose( _bShareCrowdPose == SW_TRUE );
    }

    void SkeletalMeshComponent::resolveRenderAssets()
    {
        MeshComponent::resolveRenderAssets();
        resolveSkinInstanceMesh();
        resolveSkeleton();
    }

    void SkeletalMeshComponent::setSkeletonPath( string_view path )
    {
        _skeletonPath     = string{ path };
        _bRuntimeSkeleton = SW_FALSE;
        resolveSkeleton();
    }

    void SkeletalMeshComponent::setSkeleton( shared_ptr<const Skeleton> skeleton )
    {
        // 코드가 정한 스켈레톤은 경로가 빈 동안 렌더 에셋을 다시 풀어도(시작 · 메시 교체) 암묵 스켈레톤으로 돌아가지 않는다.
        _bRuntimeSkeleton = ( skeleton != nullptr ) ? SW_TRUE : SW_FALSE;
        assignSkeleton( std::move( skeleton ) );
    }

    void SkeletalMeshComponent::applyExternalPose()
    {
        _localPose.computeModelSpace( _skeleton->getParentIndices(), _listModelSpace );
        Pose::computeSkinPalette( *_skeleton, _listModelSpace, _listSkinPalette );
        ++_poseEvaluationCount;
        _bPoseDirty = SW_FALSE;
    }

    void SkeletalMeshComponent::assignSkeleton( shared_ptr<const Skeleton> skeleton )
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
        // 경로가 비었고 런타임에 정한 스켈레톤이면 그대로 둔다 — 렌더 에셋을 다시 풀어도(resolveRenderAssets) 암묵 스켈레톤으로 덮지 않는다.
        if ( _skeletonPath.empty() && _bRuntimeSkeleton == SW_TRUE )
        {
            resolveBoneLOD();
            return;
        }
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
            assignSkeleton( std::move( skeleton ) );
        // 본 LOD 는 이 스켈레톤에 맞춰 마스크를 짓는다 — 스켈레톤을 정한 뒤다.
        resolveBoneLOD();
    }

    void SkeletalMeshComponent::resolveBoneLOD()
    {
        // 본 LOD 는 스켈레톤 곁의 선택 파일이다 — 없으면 조용히 본 LOD 없이 간다.
        const string path = SkeletonBoneLOD::makePathForSkeleton( _skeletonPath );
        if ( path.empty() || ResourceUtil::hasResource( path ) == false )
        {
            setBoneLOD( nullptr );
            return;
        }
        shared_ptr<const SkeletonBoneLOD> boneLOD = SkeletonBoneLODCache::acquire( path );
        if ( boneLOD != _boneLOD )
            setBoneLOD( std::move( boneLOD ) );
    }

    void SkeletalMeshComponent::setBoneLOD( shared_ptr<const SkeletonBoneLOD> boneLOD )
    {
        _boneLOD         = std::move( boneLOD );
        _boneLODRevision = 0;
        _listBoneLODMask.clear();
        refreshBoneLODMasks();
    }

    void SkeletalMeshComponent::refreshBoneLODMasks()
    {
        if ( _boneLOD == nullptr )
        {
            _listBoneLODMask.clear();
            return;
        }
        if ( _boneLODRevision == _boneLOD->getRevision() && _pBoneLODMaskSkeleton == _skeleton.get() )
            return;
        _boneLODRevision      = _boneLOD->getRevision();
        _pBoneLODMaskSkeleton = _skeleton.get();
        // 스켈레톤에 없는 본 이름은 데이터 오류다 — 알리고 본 LOD 없이 간다.
        if ( _boneLOD->computeMasks( *_skeleton, _listBoneLODMask, _skeletonPath ) == false )
            _listBoneLODMask.clear();
    }

    const uint8* SkeletalMeshComponent::findBoneLODMask() const
    {
        const uint32 level = _lodState._boneLODLevel;
        if ( level == 0 || level > _listBoneLODMask.size() )
            return nullptr;
        const vector<uint8>& mask = _listBoneLODMask[level - 1];
        return mask.size() == _skeleton->getBoneCount() ? mask.data() : nullptr;
    }

    void SkeletalMeshComponent::applyAnimationLOD( const AnimationLODState& state )
    {
        _lodState = state;
        setVisibleHint( state._bVisible == SW_TRUE );
    }

    void SkeletalMeshComponent::resolveSkinInstanceMesh()
    {
        const shared_ptr<Mesh>& current = getMesh();
        if ( current == nullptr || current->hasSkin() == false )
            return;
        if ( _bShareCrowdPose == SW_TRUE )
        {
            // 군중 공유 — 사본을 두지 않는다. 지금 메시가 원본이거나 군중이 준 것(묶음 · 사본)이고 원본이 그대로면 둔다.
            const bool bCrowdMesh   = ( _pCrowdBucket != nullptr && current == _pCrowdBucket->getMesh() ) || ( _soloMesh != nullptr && current == _soloMesh );
            const bool bSourceFresh = _skinSourceMesh != nullptr && _skinSourceMesh->getContentID() == _skinSourceContentID;
            if ( bSourceFresh && ( current == _skinSourceMesh || bCrowdMesh ) )
                return;
            // 원본이 바뀌었다(핫 리로드 · 다른 id) — 묶음을 놓고 새 원본을 그린다. 다음 평가가 다시 묶는다.
            shared_ptr<Mesh> source = bCrowdMesh ? _skinSourceMesh : current;
            leaveCrowd( _pAnimationSystem != nullptr ? &_pAnimationSystem->getCrowd() : nullptr );
            _skinSourceMesh      = source;
            _skinSourceContentID = source->getContentID();
            setBoundsRadius( source->getBoundingRadius() * SkeletalMeshComponentInternal::kSkinnedBoundsScale );
            setMesh( std::move( source ) );
            return;
        }
        // 지금 메시가 이미 이 컴포넌트의 복사본이고 원본이 그대로면 둔다. 원본이 바뀌었으면(핫 리로드 · 다른 id) 새 복사본을 만든다 —
        // 옛 복사본은 스냅샷이 쥔 동안 산다(정점 버퍼를 제자리에서 바꾸지 않는다).
        const bool bOwnCopy = _skinSourceMesh != nullptr && current != _skinSourceMesh && _skinSourceMesh->getContentID() == _skinSourceContentID;
        if ( bOwnCopy )
            return;

        shared_ptr<Mesh> source   = ( _skinSourceMesh != nullptr && current != _skinSourceMesh ) ? _skinSourceMesh : current;
        shared_ptr<Mesh> instance = Mesh::createSkinInstance( *source );
        _skinSourceMesh           = source;
        _skinSourceContentID      = source->getContentID();
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
        _pBoneLODMaskSkeleton  = nullptr;
        refreshBoneLODMasks();
        const shared_ptr<Mesh>& mesh = getMesh();
        if ( mesh != nullptr && mesh->hasSkin() && mesh->getSkinBoneCount() != _skeleton->getBoneCount() )
            SW_LOG_ERROR( "Skinned mesh '%#' expects %# bones but skeleton '%#' has %#", getMeshID().c_str(), mesh->getSkinBoneCount(), _skeletonPath.c_str(),
                          _skeleton->getBoneCount() );
    }

    bool SkeletalMeshComponent::findBoneModelTransform( const hashed_string& boneName, float4x4& outTransform ) const
    {
        const int32             boneIndex      = _skeleton->findBoneIndex( boneName );
        const vector<float4x4>& listModelSpace = getModelSpaceTransforms();
        if ( boneIndex < 0 || static_cast<size_t>( boneIndex ) >= listModelSpace.size() )
            return false;
        outTransform = listModelSpace[static_cast<size_t>( boneIndex )];
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

    void SkeletalMeshComponent::setShareCrowdPose( bool bShare )
    {
        const uint8 value = bShare ? SW_TRUE : SW_FALSE;
        if ( _bShareCrowdPose == value && ( bShare || _crowdMode == AnimationCrowdMode::Own ) )
            return;
        _bShareCrowdPose = value;
        if ( bShare )
        {
            // 자기 사본을 원본으로 되돌린다 — 다음 평가가 묶음 · 사본 · VAT 를 정한다.
            if ( _skinSourceMesh != nullptr && getMesh() != _skinSourceMesh )
                setMesh( _skinSourceMesh );
            _bPoseDirty = SW_TRUE;
            return;
        }
        leaveCrowd( _pAnimationSystem != nullptr ? &_pAnimationSystem->getCrowd() : nullptr );
        // 공유를 끄면 예전처럼 자기 사본을 둔다.
        if ( _skinSourceMesh != nullptr )
        {
            setMesh( _skinSourceMesh );
            resolveSkinInstanceMesh();
        }
        _bPoseDirty = SW_TRUE;
    }

    uint32 SkeletalMeshComponent::getMorphTargetCount() const
    {
        const Mesh* pMesh = getRawMesh();
        return ( pMesh != nullptr ) ? pMesh->getMorphTargetCount() : 0u;
    }

    int32 SkeletalMeshComponent::findMorphTargetIndex( const hashed_string& targetName ) const
    {
        const Mesh* pMesh = getRawMesh();
        return ( pMesh != nullptr ) ? pMesh->findMorphTargetIndex( targetName ) : -1;
    }

    void SkeletalMeshComponent::setMorphWeight( uint32 targetIndex, float32 weight )
    {
        if ( targetIndex < _listMorphWeight.size() )
            _listMorphWeight[targetIndex] = weight;
    }

    void SkeletalMeshComponent::addMorphWeight( uint32 targetIndex, float32 weight )
    {
        if ( targetIndex < _listMorphWeight.size() )
            _listMorphWeight[targetIndex] += weight;
    }

    bool SkeletalMeshComponent::hasActiveMorphWeights() const
    {
        return std::any_of( _listMorphWeight.begin(), _listMorphWeight.end(), []( float32 weight )
        { return weight != 0.0f; } );
    }

    bool SkeletalMeshComponent::describeSharedPose( AnimSharedPoseRequest& outRequest ) const
    {
        // 리더를 따르거나 후처리 일이 있으면 유닛마다 포즈가 다르다 — 나눌 수 없다. 모프 가중치(얼굴 · 커브)도 묶음이 나누지 않는다.
        if ( _listTask.size() != 1 || _leader.isValid() || hasActiveMorphWeights() )
            return false;
        return _listTask[0]->describeSharedPose( outRequest );
    }

    void SkeletalMeshComponent::leaveCrowd( AnimationCrowd* pCrowd )
    {
        if ( _pCrowdBucket != nullptr )
        {
            AnimationCrowd::releaseReference( _pCrowdBucket );
            _pCrowdBucket = nullptr;
        }
        if ( _soloMesh != nullptr )
        {
            if ( pCrowd != nullptr )
                pCrowd->releaseSoloMesh( std::move( _soloMesh ) );
            _soloMesh = nullptr;
        }
        _pVertexAnimationClip = nullptr;
        _crowdMode            = AnimationCrowdMode::Own;
    }

    void SkeletalMeshComponent::updateCrowdMembership( AnimationCrowd& crowd )
    {
        if ( _bShareCrowdPose == SW_FALSE || _skinSourceMesh == nullptr )
        {
            if ( _crowdMode != AnimationCrowdMode::Own )
                setShareCrowdPose( false );
            return;
        }
        const bool                 bVisible = _bVisibleHint == SW_TRUE || _bAnimateWhenOffscreen == SW_TRUE;
        AnimSharedPoseRequest      request{};
        shared_ptr<const AnimClip> clip;
        if ( describeSharedPose( request ) )
            clip = _listTask[0]->findSharedPoseClip( request._pClip );

        if ( clip != nullptr )
        {
            // 아주 멀면 VAT — 따르는 유닛이 없고 재생 속도가 1 일 때(셰이더는 VAT 시계를 그대로 흘린다).
            if ( _lodState._bVertexAnimation == SW_TRUE && _bHasDependents == SW_FALSE && request._playRate == 1.0f )
            {
                shared_ptr<Mesh> vertexMesh =
                    crowd.findVertexAnimationMesh( _skinSourceMesh, *_skeleton, *clip, request._bAnchorRootMotion == SW_TRUE, getMeshID() );
                if ( vertexMesh != nullptr )
                {
                    const bool bEntering = _crowdMode != AnimationCrowdMode::VertexAnimation || _pVertexAnimationClip != clip.get();
                    leaveCrowd( &crowd );
                    _crowdMode            = AnimationCrowdMode::VertexAnimation;
                    _pVertexAnimationClip = clip.get();
                    if ( getMesh() != vertexMesh )
                        setMesh( std::move( vertexMesh ) );
                    // 들어갈 때 한 번 — VAT 시계 + 오프셋 = 이 유닛의 클립 시각. 그 뒤로 둘이 같은 dt 로 흘러 다시 적지 않는다(인스턴스를 다시 올리지 않는다).
                    if ( bEntering )
                        setVertexAnimationPhase( static_cast<float32>( static_cast<float64>( request._time ) - crowd.getClock() ) );
                    _frameContext._bPoseNeeded = SW_FALSE;
                    _bInterpolateFrame         = SW_FALSE;
                    return;
                }
            }
            AnimationCrowdBucket* pBucket = crowd.joinBucket( _pCrowdBucket, request, _skeleton, _skinSourceMesh, clip );
            if ( pBucket != nullptr )
            {
                if ( pBucket != _pCrowdBucket )
                {
                    leaveCrowd( &crowd );
                    AnimationCrowd::addReference( pBucket );
                    _pCrowdBucket = pBucket;
                }
                _crowdMode = AnimationCrowdMode::Shared;
                if ( getMesh() != pBucket->getMesh() )
                    setMesh( pBucket->getMesh() );
                AnimationCrowd::countMember( *pBucket, bVisible );
                // 묶음이 포즈를 만든다 — 이 유닛은 포즈 단계를 돌지 않는다.
                _frameContext._bPoseNeeded = SW_FALSE;
                _bInterpolateFrame         = SW_FALSE;
                return;
            }
        }

        // 혼자 — 사본을 빌려 평가한다. 묶음 · VAT 에서 막 넘어왔으면 이번 프레임 포즈를 만든다(가진 포즈가 없다).
        const bool bWasShared = _crowdMode != AnimationCrowdMode::Solo;
        if ( _pCrowdBucket != nullptr )
        {
            // 묶음의 마지막 포즈에서 이어 간다(섞기 시작이 튀지 않게).
            _localPose = _pCrowdBucket->getLocalPose();
            AnimationCrowd::releaseReference( _pCrowdBucket );
            _pCrowdBucket = nullptr;
        }
        _pVertexAnimationClip = nullptr;
        if ( _soloMesh == nullptr )
            _soloMesh = crowd.acquireSoloMesh( _skinSourceMesh );
        if ( _soloMesh != nullptr && getMesh() != _soloMesh )
            setMesh( _soloMesh );
        _crowdMode = AnimationCrowdMode::Solo;
        if ( bWasShared )
        {
            _frameContext._bPoseNeeded = SW_TRUE;
            _bInterpolationReady       = SW_FALSE;
        }
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
        // 그리는 메시의 모프 타깃 수에 가중치 칸을 맞춘다(메시가 바뀌었을 수 있다 — 핫 리로드 · 군중 묶음 · 사본).
        const uint32 morphTargetCount = getMorphTargetCount();
        if ( _listMorphWeight.size() != morphTargetCount )
            _listMorphWeight.assign( morphTargetCount, 0.0f );
        // 본 LOD 표가 제자리에서 다시 읽혔으면(핫 리로드) 마스크를 다시 짓는다.
        if ( _boneLOD != nullptr && _boneLODRevision != _boneLOD->getRevision() )
            refreshBoneLODMasks();
        bool bActive = _bPoseDirty == SW_TRUE || _leader.isValid();
        for ( const IAnimationPhaseTask* pTask : _listTask )
        {
            bActive = bActive || pTask->isAnimationActive();
        }
        if ( bActive == false )
            return false;

        // 갱신 주기: 시간 단계는 매 프레임 돌고(알림 · 루트 모션이 늦지 않게), 포즈는 주기마다 만든다. 화면 밖이면 포즈를 건너뛴다.
        // 주기는 PROPERTY(하한)와 LOD 판정 중 큰 쪽이고, 위상을 더해 같은 주기의 유닛이 한 프레임에 몰리지 않게 한다.
        _effectiveDivisor           = MathUtil::max( _updateRateDivisor, MathUtil::max( _lodState._updateRateDivisor, 1u ) );
        const bool bOnRate          = AnimationLODUtil::isOnUpdateFrame( frameIndex, _updatePhase, _effectiveDivisor );
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
        for ( IAnimationPhaseTask* pTask : _listTask )
        {
            pTask->prepareAnimationFrame( *this, _frameContext );
        }
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
        {
            pTask->runAnimationPhase( phase, *this, _frameContext );
        }
    }

    void SkeletalMeshComponent::copyLeaderPose( const SkeletalMeshComponent& leader )
    {
        const Skeleton& leaderSkeleton = leader.getSkeleton();
        if ( _pLeaderSkeletonForMap != &leaderSkeleton )
        {
            // 리더 스켈레톤이 바뀌었을 때만 이름 표를 다시 짓는다(본 수십 개의 이름 찾기).
            _listLeaderBone.resize( _skeleton->getBoneCount() );
            for ( uint32 boneIndex = 0; boneIndex < _skeleton->getBoneCount(); ++boneIndex )
            {
                _listLeaderBone[boneIndex] = leaderSkeleton.findBoneIndex( _skeleton->getBone( boneIndex )._name );
            }
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
                // 모프 가중치도 포즈와 같이 새로 만든다 — 일들(커브 · 얼굴)이 이 단계와 후처리 단계에서 더한다.
                std::fill( _listMorphWeight.begin(), _listMorphWeight.end(), 0.0f );
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

    bool SkeletalMeshComponent::applyRewindPose( const Pose& pose )
    {
        if ( _pCrowdBucket != nullptr || pose.getBoneCount() != _skeleton->getBoneCount() )
            return false;
        _localPose = pose;
        _localPose.computeModelSpace( _skeleton->getParentIndices(), _listModelSpace );
        Pose::computeSkinPalette( *_skeleton, _listModelSpace, _listSkinPalette );
        return true;
    }

    void SkeletalMeshComponent::collectDebugState( AnimationDebugState& inoutState ) const
    {
        for ( const IAnimationPhaseTask* pTask : _listTask )
        {
            pTask->collectDebugState( inoutState );
        }
    }

    void SkeletalMeshComponent::finishAnimationFrame()
    {
        for ( IAnimationPhaseTask* pTask : _listTask )
        {
            pTask->finishAnimationFrame( *this );
        }
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
