#include "pch.h"

#include "Engine/Animation/Component/SkeletalMeshComponent.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Foundation/Constants.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Animation/AnimationAssetCache.h"
#include "Engine/Animation/Scene/AnimationSystem.h"
#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Animation/Skeletal/SkeletonBoneLOD.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Visual/Component/MeshSectionComponent.h"
#include "Engine/Visual/MeshLODSettings.h"

namespace sw
{
    namespace
    {
        struct SkeletalMeshComponentInternal
        {
            /** @brief 본 하나("root", 단위 변환)짜리 스켈레톤을 만듭니다. */
            static shared_ptr<const Skeleton> createImplicitSkeleton()
            {
                shared_ptr<Skeleton> skeleton = make_shared<Skeleton>();
                (void)skeleton->addBone( hashed_string( "root" ), constant::invalid_index::kInt32, BoneTransform{} );
                skeleton->setPartial( true ); // 외형을 입기 전 · 전용 서버의 자리다 — 클립과 리더의 본이 없는 것이 정상이다
                return skeleton;
            }

            /** @brief 스켈레톤이 없는 부품이 받는 본 하나짜리 스켈레톤입니다(프로세스에 하나, 읽기만). */
            static const shared_ptr<const Skeleton>& getImplicitSkeleton()
            {
                static const shared_ptr<const Skeleton> s_skeleton = createImplicitSkeleton();
                return s_skeleton;
            }

            /** @brief 핸들에서 주기 위상을 냅니다(정수 해시 — 이웃한 id 가 이웃한 위상이 되지 않게 섞는다). */
            static uint32 makeUpdatePhase( uint64 id )
            {
                uint64 mixed = id * HashUtil::kGoldenRatio64;
                mixed ^= mixed >> 29;
                return static_cast<uint32>( mixed & 0xFFFFu );
            }

#if defined( SW_DEBUG )
            /** @brief 로컬 포즈와 모프 가중치의 바이트 지문입니다 — `ReadsOnly` 를 선언한 작업이 단계 뒤에도 이 값을 그대로 두었는지 `runTasks` 가 본다. */
            static uint64 fingerprintPose( const Pose& pose, const vector<float32>& listMorphWeight )
            {
                uint64 hash = HashUtil::kFNVOffset64;
                hash        = HashUtil::appendBytes64( hash, pose.getTranslations().data(), pose.getTranslations().size() * sizeof( float3 ) );
                hash        = HashUtil::appendBytes64( hash, pose.getRotations().data(), pose.getRotations().size() * sizeof( quaternion ) );
                hash        = HashUtil::appendBytes64( hash, pose.getScales().data(), pose.getScales().size() * sizeof( float3 ) );
                return HashUtil::appendBytes64( hash, listMorphWeight.data(), listMorphWeight.size() * sizeof( float32 ) );
            }
#endif
        };
    } // namespace
} // namespace sw

namespace sw
{
    /**
     * @brief `-gv_animationBoneLOD=0` — 본 LOD 마스크(손 표 · 메시 LOD 자동)를 끕니다. 시나리오가 켜고 끈 두 장 · 풀어낸 본 수(`Animation.EvaluatedBones`)를 견줍니다.
     */
    SW_GLOBAL_VARIABLE( int32, gv_animationBoneLOD, 1, "Bone LOD masks: skip bones the drawn mesh LOD does not use (0 = evaluate every bone)" );
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

    bool SkeletalMeshLODClient::isRendered() const
    {
        return _owner.isVisible();
    }

    const SkeletonBoneLOD* SkeletalMeshLODClient::findBoneLOD() const
    {
        return _owner.findBoneLOD();
    }

    int32 SkeletalMeshLODClient::findMeshLODIndex() const
    {
        const shared_ptr<Mesh>& mesh = _owner.getMesh();
        if ( mesh == nullptr || mesh->getLODCount() <= 1 )
            return constant::invalid_index::kInt32;
        return static_cast<int32>( _owner.getLODIndex() );
    }

    uint32 SkeletalMeshLODClient::getBoneLODMaskCount() const
    {
        return _owner.getBoneLODMaskCount();
    }

    void SkeletalMeshLODClient::applyAnimationLOD( const AnimationLODState& state )
    {
        _owner.applyAnimationLOD( state );
    }

    SkeletalMeshComponent::SkeletalMeshComponent()
        : _skeletonPath{}
        , _updateRateDivisor{ 1 }
        , _skinningMethod{ SkinningMethod::Linear }
        , _skeleton{ SkeletalMeshComponentInternal::getImplicitSkeleton() }
        , _skinSourceMesh{}
        , _skinSourceContentID{}
        , _localPose{}
        , _listModelSpace{}
        , _listSkinPalette{}
        , _listDependency{}
        , _listTask{}
        , _phaseTaskSubscriptions{ ItemSubscriptionList<IAnimationPhaseTask>::ReleaseFunction::create<&SkeletalMeshComponent::detachAnimationPhaseTask>( this ) }
        , _listLeaderBone{}
        , _leader{}
        , _pLeaderSkeletonForMap{ nullptr }
        , _skinBindingContentID{}
        , _pSkinBindingSkeleton{ nullptr }
        , _pAnimationSystem{ nullptr }
        , _frameContext{}
        , _lodClient{ *this }
        , _lodState{}
        , _boneLOD{}
        , _listBoneLODMask{}
        , _boneLODRevision{}
        , _pBoneLODMaskSkeleton{ nullptr }
        , _boneLODMeshContentID{}
        , _listBoneLODMaskCount{}
        , _listBoneRequirement{}
        , _boneRequirementSubscriptions{ SubscriptionList::ReleaseFunction::create<&SkeletalMeshComponent::releaseBoneRequirement>( this ) }
        , _nextBoneRequirementID{ 1 }
        , _interpolationFrom{}
        , _interpolationTarget{}
        , _updatePhase{ 0 }
        , _framesSinceEvaluation{ 0 }
        , _effectiveDivisor{ 1 }
        , _listMorphWeight{}
        , _pCrowdBucket{ nullptr }
        , _pVertexAnimationClip{ nullptr }
        , _crowdMode{ AnimationCrowdMode::Own }
        , _drawnSkinSlot{ AnimationSkinSlot::kNone }
        , _bFollowParentPose{ SW_FALSE }
        , _bAnimateWhenOffscreen{ SW_FALSE }
        , _bVisibleHint{ SW_TRUE }
        , _bPoseDirty{ SW_TRUE }
        , _bInterpolateFrame{ SW_FALSE }
        , _bInterpolationReady{ SW_FALSE }
        , _bShareCrowdPose{ SW_FALSE }
        , _bHasDependents{ SW_FALSE }
        , _bRuntimeSkeleton{ SW_FALSE }
        , _bBoneLODDirty{ SW_TRUE }
        , _bBoneLODAuto{ SW_FALSE }
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
            pTask->onAnimationMeshDetached( *this );
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
        _pAnimationSystem = &manager.getSubsystem<AnimationSystem>();
        _pAnimationSystem->registerMesh( this );
        _lodRegistration = _pAnimationSystem->registerLODClient( &_lodClient );
        _updatePhase     = SkeletalMeshComponentInternal::makeUpdatePhase( getHandle().componentID() );
    }

    void SkeletalMeshComponent::onUnregister( GameObjectManager& manager )
    {
        leaveCrowd( &manager.getSubsystem<AnimationSystem>().getCrowd() );
        _lodRegistration.reset();
        manager.getSubsystem<AnimationSystem>().unregisterMesh( this );
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

    bool SkeletalMeshComponent::pollReadyToAppear()
    {
        // 모든 작업에 묻는다(하나가 거짓이어도 나머지가 마무리할 기회를 갖게 끝까지 돈다).
        bool bReady = true;
        for ( IAnimationPhaseTask* pTask : _listTask )
        {
            bReady = pTask->pollAnimationAssetsReady() && bReady;
        }
        return bReady;
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
        refreshSkinPalette();
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

    bool SkeletalMeshComponent::hasDataSkeleton() const
    {
        return _skeleton != SkeletalMeshComponentInternal::getImplicitSkeleton();
    }

    void SkeletalMeshComponent::setSkinSourceMesh( shared_ptr<Mesh> source )
    {
        leaveCrowd( _pAnimationSystem != nullptr ? &_pAnimationSystem->getCrowd() : nullptr );
        _skinSourceMesh = nullptr;
        _skinSourceContentID.reset();
        setMesh( std::move( source ) );
        resolveSkinInstanceMesh();
        _bPoseDirty = SW_TRUE;
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
            skeleton = SharedAssetCache<Skeleton>::acquire( _skeletonPath );
            if ( skeleton == nullptr )
                SW_LOG_ERROR_ID( "SkeletalMeshComponent.SkeletonCouldNotLoaded", "Skeleton '%#' could not be loaded - using a one-bone implicit skeleton", _skeletonPath.c_str() );
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
        // 본 LOD 는 스켈레톤 곁의 선택 파일이다 — 없으면 조용히 본 LOD 없이 간다. 변형은 곁에 없으면 부모의 것을 쓴다(이름으로 찾으므로 그대로 맞는다).
        string path = SkeletonBoneLOD::makePathForSkeleton( _skeletonPath );
        for ( const string& basePath : _skeleton->getBasePaths() )
        {
            if ( path.empty() == false && AssetIO::hasAsset( AssetID{ path } ) )
                break;
            path = SkeletonBoneLOD::makePathForSkeleton( basePath );
        }
        if ( path.empty() || AssetIO::hasAsset( AssetID{ path } ) == false )
        {
            setBoneLOD( nullptr );
            return;
        }
        shared_ptr<const SkeletonBoneLOD> boneLOD = SharedAssetCache<SkeletonBoneLOD>::acquire( path );
        if ( boneLOD != _boneLOD )
            setBoneLOD( std::move( boneLOD ) );
    }

    void SkeletalMeshComponent::setBoneLOD( shared_ptr<const SkeletonBoneLOD> boneLOD )
    {
        _boneLOD       = std::move( boneLOD );
        _bBoneLODDirty = SW_TRUE;
        refreshBoneLODMasks();
    }

    void SkeletalMeshComponent::refreshBoneLODMasks()
    {
        const IdentityToken revision      = _boneLOD != nullptr ? _boneLOD->getRevision() : IdentityToken{};
        const IdentityToken meshContentID = _skinSourceMesh != nullptr ? _skinSourceMesh->getContentID() : IdentityToken{};
        const bool          bFresh        = _bBoneLODDirty == SW_FALSE && _boneLODRevision == revision && _pBoneLODMaskSkeleton == _skeleton.get() && _boneLODMeshContentID == meshContentID;
        if ( bFresh )
            return;
        _boneLODRevision      = revision;
        _pBoneLODMaskSkeleton = _skeleton.get();
        _boneLODMeshContentID = meshContentID;
        _bBoneLODDirty        = SW_FALSE;
        _bBoneLODAuto         = SW_FALSE;
        _listBoneLODMask.clear();
        _listBoneLODMaskCount.clear();
        // 암묵 스켈레톤(외형을 입기 전 · 전용 서버)은 본이 하나라 뺄 것이 없다.
        if ( hasDataSkeleton() == false )
            return;
        vector<hashed_string> listUserBone;
        for ( const BoneRequirement& requirement : _listBoneRequirement )
        {
            listUserBone.insert( listUserBone.end(), requirement._listBone.begin(), requirement._listBone.end() );
        }
        // 핵심 본 선언이 스켈레톤에 없거나 손 파일이 핵심 본을 빼면 데이터 오류다 — 알리고 본 LOD 없이 간다(모든 본을 푼다).
        vector<uint8> requiredMask;
        if ( SkeletonBoneLOD::computeRequiredMask( *_skeleton, _boneLOD.get(), listUserBone, requiredMask, _skeletonPath ) == false )
            return;
        if ( _boneLOD != nullptr && _boneLOD->getLevelCount() > 0 )
        {
            if ( _boneLOD->computeMasks( *_skeleton, requiredMask, _listBoneLODMask, _skeletonPath ) == false )
                _listBoneLODMask.clear();
        }
        else if ( _skinSourceMesh != nullptr && _skinSourceMesh->hasSkin() )
        {
            // 손 표가 없으면 그리는 메시의 LOD 에서 만든다 — 조립 캐릭터는 합친 메시라 부품 합집합이 기준이다.
            SkeletonBoneLOD::computeMeshMasks( *_skeleton, *_skinSourceMesh, requiredMask, _listBoneLODMask );
            _bBoneLODAuto = SW_TRUE;
        }
        for ( const vector<uint8>& mask : _listBoneLODMask )
        {
            _listBoneLODMaskCount.push_back( static_cast<uint32>( std::count( mask.begin(), mask.end(), static_cast<uint8>( SW_TRUE ) ) ) );
        }
    }

    const uint8* SkeletalMeshComponent::findBoneLODMask() const
    {
        const uint32 level = _lodState._boneLODLevel;
        // 자동 마스크는 이 메시만 보고 지었다 — 이 포즈를 읽는 유닛(리더를 따르는 부품 · 리그 대상)이 있으면 그 유닛이 쓰는 본을 모른다.
        const bool bOff = gv_animationBoneLOD == 0 || level == 0 || level > _listBoneLODMask.size() || ( _bBoneLODAuto == SW_TRUE && _bHasDependents == SW_TRUE );
        if ( bOff )
            return nullptr;
        const vector<uint8>& mask = _listBoneLODMask[level - 1];
        return mask.size() == _skeleton->getBoneCount() ? mask.data() : nullptr;
    }

    uint32 SkeletalMeshComponent::getEvaluatedBoneCount() const
    {
        return findBoneLODMask() != nullptr ? _listBoneLODMaskCount[_lodState._boneLODLevel - 1u] : _skeleton->getBoneCount();
    }

    Subscription SkeletalMeshComponent::requireBones( vector<hashed_string> listBone )
    {
        const uint64 id = _nextBoneRequirementID++;
        _listBoneRequirement.push_back( BoneRequirement{ id, std::move( listBone ) } );
        _bBoneLODDirty = SW_TRUE;
        return _boneRequirementSubscriptions.issue( id );
    }

    void SkeletalMeshComponent::releaseBoneRequirement( uint64 id )
    {
        if ( VectorUtil::removeIf( _listBoneRequirement, [id]( const BoneRequirement& requirement )
        { return requirement._id == id; } ) > 0 )
            _bBoneLODDirty = SW_TRUE;
    }

    void SkeletalMeshComponent::applyAnimationLOD( const AnimationLODState& state )
    {
        _lodState = state;
        setVisibleHint( state._bVisible == SW_TRUE );
    }

    uint32 SkeletalMeshComponent::getSkinSlot() const
    {
        if ( _crowdMode == AnimationCrowdMode::VertexAnimation || getRawMesh() == nullptr || getRawMesh()->hasSkin() == false )
            return AnimationSkinSlot::kNone;
        const bool bShared = _crowdMode == AnimationCrowdMode::Shared && _pCrowdBucket != nullptr;
        return bShared ? _pCrowdBucket->getSkinSlot() : _skinSlot.get();
    }

    void SkeletalMeshComponent::resolveSkinInstanceMesh()
    {
        if ( getMesh() == nullptr )
            return;
        // 스킨 없는 모프 메시 — 본 하나의 스킨 원본으로 바꿔 GPU 스키닝 컴퓨트(모프 가중치를 거는 곳)에 들게 한다. 모프도 스킨도 없으면 고정 메시라 둔다.
        if ( getMesh()->hasSkin() == false )
        {
            if ( getMesh()->getMorphTargetCount() == 0 )
                return;
            setMesh( Mesh::acquireRigidSkinSource( *getMesh() ) );
        }
        // 사본을 두지 않는다 — 스킨 결과는 칸마다라(AnimationSkinSlot) 원본 메시를 그대로 그린다(군중 묶음도 원본을 그린다).
        // 지금 메시가 원본이거나 VAT 메시이고 원본이 그대로면 둔다. 원본이 바뀌었으면(핫 리로드 · 다른 id) 군중을 놓고 새 원본을 그린다 — 다음 평가가 다시 묶는다.
        const shared_ptr<Mesh>& current      = getMesh();
        const bool              bSourceFresh = _skinSourceMesh != nullptr && _skinSourceMesh->getContentID() == _skinSourceContentID;
        const bool              bCrowdMesh   = _crowdMode == AnimationCrowdMode::VertexAnimation && current != _skinSourceMesh;
        if ( bSourceFresh && ( current == _skinSourceMesh || bCrowdMesh ) )
            return;
        shared_ptr<Mesh> source = bCrowdMesh ? _skinSourceMesh : current;
        leaveCrowd( _pAnimationSystem != nullptr ? &_pAnimationSystem->getCrowd() : nullptr );
        _skinSourceMesh      = source;
        _skinSourceContentID = source->getContentID();
        setBoundsRadius( source->getBoundingRadius() * MeshLODSettings::getDefault()._skinnedBoundsScale ); // 포즈가 바인드 포즈 밖으로 정점을 옮겨도 컬링에 잘리지 않게
        if ( current != source )
            setMesh( std::move( source ) );
        refreshDrawnSkinSlot();
    }

    void SkeletalMeshComponent::refreshDrawnSkinSlot()
    {
        const uint32 skinSlot = getSkinSlot();
        if ( skinSlot == _drawnSkinSlot )
            return;
        _drawnSkinSlot = skinSlot;
        markRenderStateDirty();
        if ( getOwner() != nullptr )
            getOwner()->forEachComponentOfType<MeshSectionComponent>( []( MeshSectionComponent* pSection )
            { pSection->markRenderStateDirty(); } );
    }

    void SkeletalMeshComponent::resetPoseBuffers()
    {
        _localPose.setToReference( *_skeleton );
        _localPose.computeModelSpace( _skeleton->getParentIndices(), _listModelSpace );
        refreshSkinPalette();
        _pLeaderSkeletonForMap = nullptr;
        _bPoseDirty            = SW_TRUE;
        _bInterpolationReady   = SW_FALSE;
        _pBoneLODMaskSkeleton  = nullptr;
        refreshBoneLODMasks();
    }

    void SkeletalMeshComponent::refreshSkinPalette()
    {
        const shared_ptr<Mesh>& mesh = getMesh();
        if ( mesh != nullptr && mesh->hasSkin() )
            Pose::computeSkinPalette( mesh->getSkinBinding()._listInverseBind, _listModelSpace, _listSkinPalette );
    }

    void SkeletalMeshComponent::refreshSkinBinding()
    {
        const shared_ptr<Mesh>& mesh = getMesh();
        if ( mesh == nullptr || mesh->hasSkin() == false )
            return; // 군중 VAT 메시처럼 스킨이 없으면 팔레트를 쓰지 않는다 — 스킨 원본으로 돌아오면 지난 값 그대로다
        const bool bSameBinding = _skinBindingContentID == mesh->getContentID() && _pSkinBindingSkeleton == _skeleton.get();
        if ( bSameBinding )
            return;
        _skinBindingContentID = mesh->getContentID();
        _pSkinBindingSkeleton = _skeleton.get();
        _bPoseDirty           = SW_TRUE;
        // 암묵 스켈레톤은 외형을 입기 전의 자리다 — 본이 하나뿐이라 비교할 것이 없다.
        if ( hasDataSkeleton() && _skeleton->canDrawSkinBinding( mesh->getSkinBinding() ) == false )
            SW_LOG_ERROR_ID( "SkeletalMeshComponent.SkinnedMeshExpectsBones", "Skinned mesh '%#' (%# bones) does not fit skeleton '%#' (%# bones): a bone is missing or the bone layout differs - re-import the mesh against this skeleton",
                             getMeshID().c_str(), mesh->getSkinBoneCount(), _skeletonPath.c_str(), _skeleton->getBoneCount() );
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

    bool SkeletalMeshComponent::findSkeletonSocketTransform( const hashed_string& socketName, float4x4& outTransform ) const
    {
        const SkeletonSocket* pSocket = _skeleton->findSocket( socketName );
        float4x4              bone;
        if ( pSocket == nullptr || findBoneModelTransform( pSocket->_parentBone, bone ) == false )
            return false;
        outTransform = pSocket->_localTransform.toMatrix() * bone;
        return true;
    }

    void SkeletalMeshComponent::setLeaderPose( SkeletalMeshComponent* pLeader )
    {
        SkeletalMeshComponent* pOld = findLeaderPose();
        if ( pOld == pLeader )
            return;
        if ( pOld != nullptr )
            removeAnimationDependency( pOld );
        _leader                = ( pLeader != nullptr && pLeader != this ) ? TComponentHandle<SkeletalMeshComponent>{ pLeader } : TComponentHandle<SkeletalMeshComponent>{};
        _pLeaderSkeletonForMap = nullptr;
        if ( pLeader != nullptr && pLeader != this )
            addAnimationDependency( pLeader );
        _bPoseDirty = SW_TRUE;
        notifyOrderChanged();
    }

    SkeletalMeshComponent* SkeletalMeshComponent::findLeaderPose() const
    {
        const bool bNoLeader = _leader.isValid() == false || getOwner() == nullptr || getOwner()->getManager() == nullptr;
        if ( bNoLeader )
            return nullptr;
        return getOwner()->getManager()->resolveComponent( _leader );
    }

    void SkeletalMeshComponent::addAnimationDependency( SkeletalMeshComponent* pUpstream )
    {
        if ( pUpstream == nullptr || pUpstream == this )
            return;
        const TComponentHandle<SkeletalMeshComponent> handle{ pUpstream };
        if ( VectorUtil::contains( _listDependency, handle ) )
            return;
        _listDependency.push_back( handle );
        notifyOrderChanged();
    }

    void SkeletalMeshComponent::removeAnimationDependency( SkeletalMeshComponent* pUpstream )
    {
        if ( pUpstream == nullptr )
            return;
        if ( VectorUtil::removeFirst( _listDependency, TComponentHandle<SkeletalMeshComponent>{ pUpstream } ) == 0 )
            return;
        notifyOrderChanged();
    }

    Subscription SkeletalMeshComponent::registerAnimationPhaseTask( IAnimationPhaseTask* pTask )
    {
        // 등록 순서가 단계 안의 실행 순서라 vector 를 둔다. 구독 하나가 등록 하나다 — 같은 작업을 두 번 올리면 두 번 돈다(쥔 쪽이 하나만 든다).
        if ( pTask == nullptr )
            return {};
        _listTask.push_back( pTask );
        _bPoseDirty = SW_TRUE;
        return _phaseTaskSubscriptions.issue( pTask );
    }

    void SkeletalMeshComponent::detachAnimationPhaseTask( IAnimationPhaseTask* pTask )
    {
        if ( VectorUtil::removeFirst( _listTask, pTask ) == 0 )
            return;
        pTask->onAnimationMeshDetached( *this );
        _bPoseDirty = SW_TRUE;
    }

    void SkeletalMeshComponent::setUpdateRateDivisor( uint32 divisor )
    {
        _updateRateDivisor = MathUtil::max( divisor, 1u );
    }

    void SkeletalMeshComponent::setShareCrowdPose( bool bShare )
    {
        const uint8 shareFlag = bShare ? SW_TRUE : SW_FALSE;
        if ( _bShareCrowdPose == shareFlag && ( bShare || _crowdMode == AnimationCrowdMode::Own ) )
            return;
        _bShareCrowdPose = shareFlag;
        if ( bShare )
        {
            // 다음 평가가 묶음 · 혼자 · VAT 를 정한다.
            _bPoseDirty = SW_TRUE;
            return;
        }
        leaveCrowd( _pAnimationSystem != nullptr ? &_pAnimationSystem->getCrowd() : nullptr );
        // 공유를 끄면 원본을 자기 칸으로 그린다(VAT 메시였으면 원본으로 되돌린다).
        if ( _skinSourceMesh != nullptr && getMesh() != _skinSourceMesh )
            setMesh( _skinSourceMesh );
        refreshDrawnSkinSlot();
        _bPoseDirty = SW_TRUE;
    }

    uint32 SkeletalMeshComponent::getMorphTargetCount() const
    {
        const Mesh* pMesh = getRawMesh();
        return ( pMesh != nullptr ) ? pMesh->getMorphTargetCount() : 0u;
    }

    optional<uint32> SkeletalMeshComponent::findMorphTargetIndex( const hashed_string& targetName ) const
    {
        const Mesh* pMesh = getRawMesh();
        return ( pMesh != nullptr ) ? pMesh->findMorphTargetIndex( targetName ) : nullopt;
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
        return VectorUtil::containsIf( _listMorphWeight, []( float32 weight )
        { return weight != 0.0f; } );
    }

    IAnimationPhaseTask* SkeletalMeshComponent::findSolePoseWriter() const
    {
        IAnimationPhaseTask* pWriter = nullptr;
        for ( IAnimationPhaseTask* pTask : _listTask )
        {
            if ( pTask->getPoseAccess() != AnimationPoseAccess::Writes )
                continue;
            if ( pWriter != nullptr )
                return nullptr;
            pWriter = pTask;
        }
        return pWriter;
    }

    bool SkeletalMeshComponent::hasPoseWriter() const
    {
        return VectorUtil::containsIf( _listTask, []( const IAnimationPhaseTask* pTask )
        { return pTask->getPoseAccess() == AnimationPoseAccess::Writes; } );
    }

    bool SkeletalMeshComponent::describeSharedPose( AnimationSharedPoseRequest& outRequest ) const
    {
        // 리더를 따르거나 포즈를 쓰는 작업이 둘 이상(후처리 IK 등)이면 유닛마다 포즈가 다르다 — 나눌 수 없다. 모프 가중치(얼굴 · 커브)도 묶음이 나누지 않는다.
        // 읽기 작업(외형 소켓)은 세지 않는다 — 묶음을 나누면 묶음의 포즈를 읽는다(`getModelSpaceTransforms`).
        const bool bPoseNotShareable = _leader.isValid() || hasActiveMorphWeights();
        if ( bPoseNotShareable )
            return false;
        const IAnimationPhaseTask* pWriter = findSolePoseWriter();
        return pWriter != nullptr && pWriter->describeSharedPose( outRequest );
    }

    bool SkeletalMeshComponent::joinLeaderCrowdBucket( AnimationCrowd& crowd, bool bVisible )
    {
        // 리더를 따르는 부품은 리더가 묶음을 나눌 때만 나눈다 — 리더가 혼자 평가하면 그 포즈를 옮겨야 하므로 부품도 혼자다.
        // 자기 포즈를 쓰는 작업이 있거나 모프 가중치가 있으면 리더와 포즈가 달라 나눌 수 없다.
        const SkeletalMeshComponent* pLeader         = findLeaderPose();
        const bool                   bLeaderShared   = pLeader != nullptr && pLeader->_crowdMode == AnimationCrowdMode::Shared && pLeader->_pCrowdBucket != nullptr;
        const bool                   bOwnPoseDiffers = hasPoseWriter() || hasActiveMorphWeights();
        if ( bLeaderShared == false || bOwnPoseDiffers )
            return false;
        AnimationCrowdBucket* pBucket = crowd.joinFollowerBucket( _pCrowdBucket, *pLeader->_pCrowdBucket, _skeleton, _skinSourceMesh, _skinningMethod );
        if ( pBucket == nullptr )
            return false;
        enterCrowdBucket( crowd, *pBucket, bVisible );
        return true;
    }

    void SkeletalMeshComponent::enterCrowdBucket( AnimationCrowd& crowd, AnimationCrowdBucket& bucket, bool bVisible )
    {
        if ( &bucket != _pCrowdBucket )
        {
            leaveCrowd( &crowd );
            AnimationCrowd::addReference( &bucket );
            _pCrowdBucket = &bucket;
        }
        _crowdMode = AnimationCrowdMode::Shared;
        // 묶음 메시는 원본이다 — VAT 에서 넘어왔을 때만 바뀐다.
        if ( getMesh() != bucket.getMesh() )
            setMesh( bucket.getMesh() );
        AnimationCrowd::countMember( bucket, bVisible );
        // 묶음이 포즈를 만든다 — 이 유닛은 포즈 단계를 돌지 않는다.
        _frameContext._bPoseNeeded = SW_FALSE;
        _bInterpolateFrame         = SW_FALSE;
    }

    void SkeletalMeshComponent::leaveCrowd( AnimationCrowd* pCrowd )
    {
        if ( _pCrowdBucket != nullptr )
        {
            AnimationCrowd::releaseReference( _pCrowdBucket );
            _pCrowdBucket = nullptr;
        }
        (void)pCrowd; // 묶음의 참조만 놓는다 — 군중이 빌려준 메시는 없다(묶음 · 혼자 모두 원본을 그린다)
        _pVertexAnimationClip = nullptr;
        _crowdMode            = AnimationCrowdMode::Own;
    }

    void SkeletalMeshComponent::updateCrowdMembership( AnimationCrowd& crowd )
    {
        updateCrowdState( crowd );
        refreshDrawnSkinSlot();
    }

    void SkeletalMeshComponent::updateCrowdState( AnimationCrowd& crowd )
    {
        if ( _bShareCrowdPose == SW_FALSE || _skinSourceMesh == nullptr )
        {
            if ( _crowdMode != AnimationCrowdMode::Own )
                setShareCrowdPose( false );
            return;
        }
        const bool bVisible = _bVisibleHint == SW_TRUE || _bAnimateWhenOffscreen == SW_TRUE;
        if ( _leader.isValid() && joinLeaderCrowdBucket( crowd, bVisible ) )
            return;
        AnimationSharedPoseRequest      request{};
        shared_ptr<const AnimationClip> clip;
        shared_ptr<const AnimationClip> nextClip;
        if ( describeSharedPose( request ) )
        {
            request._skinningMethod            = _skinningMethod; // 묶음은 같은 방식의 유닛끼리만 나눈다
            const IAnimationPhaseTask* pWriter = findSolePoseWriter();
            clip                               = pWriter->findSharedPoseClip( request._pClip );
            if ( request._pNextClip != nullptr )
                nextClip = pWriter->findSharedPoseClip( request._pNextClip );
        }

        const bool bBlending = request._pNextClip != nullptr;
        if ( clip != nullptr && bBlending )
        {
            // 반복 클립 둘 사이의 페이드 — 같은 프레임에 같은 전이를 시작한 유닛과 섞기 묶음을 나눈다(VAT 로 가지 않는다).
            AnimationCrowdBucket* pBlendBucket = nextClip != nullptr ? crowd.joinBlendBucket( _pCrowdBucket, request, _skeleton, _skinSourceMesh, clip, nextClip ) : nullptr;
            if ( pBlendBucket != nullptr )
            {
                enterCrowdBucket( crowd, *pBlendBucket, bVisible );
                return;
            }
        }
        else if ( clip != nullptr )
        {
            // 아주 멀면 VAT — 따르는 유닛이 없고 재생 속도가 1 일 때(셰이더는 VAT 시계를 그대로 흘린다).
            // VAT 는 선형으로 굽는다(`MeshVertexAnimationBaker`) — 듀얼 쿼터니언 유닛은 묶음으로 남는다.
            const bool bUseVertexAnimation = _lodState._bVertexAnimation == SW_TRUE && _bHasDependents == SW_FALSE && request._playRate == 1.0f && _skinningMethod == SkinningMethod::Linear;
            if ( bUseVertexAnimation )
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
                enterCrowdBucket( crowd, *pBucket, bVisible );
                return;
            }
        }

        // 혼자 — 원본을 자기 칸으로 평가한다. 묶음 · VAT 에서 막 넘어왔으면 이번 프레임 포즈를 만든다(가진 포즈가 없다).
        const bool bWasShared = _crowdMode != AnimationCrowdMode::Solo;
        if ( _pCrowdBucket != nullptr )
        {
            // 묶음의 마지막 포즈에서 이어 간다(섞기 시작이 튀지 않게).
            _localPose = _pCrowdBucket->getLocalPose();
            AnimationCrowd::releaseReference( _pCrowdBucket );
            _pCrowdBucket = nullptr;
        }
        _pVertexAnimationClip = nullptr;
        if ( getMesh() != _skinSourceMesh )
            setMesh( _skinSourceMesh );
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
        refreshSkinBinding();
        // 그리는 메시의 모프 타깃 수에 가중치 칸을 맞춘다(메시가 바뀌었을 수 있다 — 핫 리로드 · 군중 묶음 · 사본).
        const uint32 morphTargetCount = getMorphTargetCount();
        if ( _listMorphWeight.size() != morphTargetCount )
            _listMorphWeight.assign( morphTargetCount, 0.0f );
        // 본 LOD 표가 제자리에서 다시 읽혔거나(핫 리로드) 스킨 원본 · 핵심 본이 바뀌었으면 마스크를 다시 짓는다(열쇠가 같으면 비교만 한다).
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
        const bool bOnRate          = AnimationLODState::isOnUpdateFrame( frameIndex, _updatePhase, _effectiveDivisor );
        const bool bOnScreen        = _bVisibleHint == SW_TRUE || _bAnimateWhenOffscreen == SW_TRUE;
        const bool bPoseDirty       = _bPoseDirty == SW_TRUE;
        _frameContext._deltaSeconds = deltaSeconds;
        _frameContext._frameIndex   = frameIndex;
        _frameContext._bPoseNeeded  = ( bPoseDirty || ( bOnRate && bOnScreen ) ) ? SW_TRUE : SW_FALSE;
        _frameContext._bOnScreen    = bOnScreen ? SW_TRUE : SW_FALSE;
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
#if defined( SW_DEBUG )
            // 포즈 접근 선언은 군중 공유가 믿는 계약이다 — 읽기만 한다고 선언한 작업이 포즈나 모프 가중치를 쓰면 묶음의 포즈를 덮거나(공유 유닛은 이 단계를 돌지 않아
            // 쓰기가 사라지거나) 한다. 반대 방향(쓴다고 선언하고 안 쓴다)은 값이 같은 쓰기가 있을 수 있어 단언할 수 없으므로 작업마다 선언과 쓰기가 같은 판정을 쓰게 하고 테스트로 막는다.
            const bool   bDeclaredReadsOnly = pTask->getPoseAccess() == AnimationPoseAccess::ReadsOnly;
            const uint64 before             = bDeclaredReadsOnly ? SkeletalMeshComponentInternal::fingerprintPose( _localPose, _listMorphWeight ) : 0ull;
#endif
            pTask->runAnimationPhase( phase, *this, _frameContext );
#if defined( SW_DEBUG )
            if ( bDeclaredReadsOnly )
                SW_LOG_ASSERT( before == SkeletalMeshComponentInternal::fingerprintPose( _localPose, _listMorphWeight ),
                               "An animation phase task declared AnimationPoseAccess::ReadsOnly changed the pose or morph weights (phase %#)", static_cast<uint32>( phase ) );
#endif
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
                // 변형이 뒤에 붙인 본(망토 · 머리카락)과 부품 스켈레톤만 리더에 없어도 된다 — 나머지는 레퍼런스에 얼어 있는 틀린 조합이다.
                const bool bMissing = _listLeaderBone[boneIndex] < 0 && _skeleton->isAddedBone( boneIndex ) == false && _skeleton->isPartial() == false && leaderSkeleton.isPartial() == false;
                if ( bMissing )
                    SW_LOG_ERROR_ID( "SkeletalMeshComponent.LeaderLacksBone", "Skeleton '%#' follows a leader without bone '%#' - the bone stays at its reference pose; add it as a variant bone or declare the skeleton partial", _skeletonPath.c_str(),
                                     _skeleton->getBone( boneIndex )._name.c_str() );
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
                // 리더는 의존이라 이 유닛보다 먼저 모든 단계를 마쳤다(`AnimationDependencyEdge`) — 부착 · 후처리(체형 비율 · IK)까지 적용된 최종 로컬 포즈를 읽는다.
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
                refreshSkinPalette();
                if ( bPoseNeeded )
                {
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
        refreshSkinPalette();
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

    void SkeletalMeshComponent::collectSyncPlayers( vector<AnimationPlayer*>& inoutListPlayer, vector<float32>& inoutListWeight, vector<hashed_string>& inoutListGroup )
    {
        for ( IAnimationPhaseTask* pTask : _listTask )
        {
            hashed_string    groupName{};
            float32          weight  = 0.0f;
            AnimationPlayer* pPlayer = pTask->findSyncPlayer( groupName, weight );
            if ( pPlayer == nullptr || groupName.empty() )
                continue;
            inoutListPlayer.push_back( pPlayer );
            inoutListWeight.push_back( weight );
            inoutListGroup.push_back( groupName );
        }
    }
} // namespace sw
