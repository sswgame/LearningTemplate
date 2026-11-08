#include "pch.h"

#include "Engine/Animation/Component/PoseModifierComponent.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Animation/AnimationAssetCache.h"
#include "Engine/Animation/Component/SkeletalAnimatorComponent.h"
#include "Engine/Animation/Component/SkeletalMeshComponent.h"
#include "Engine/Animation/Rig/RigAsset.h"
#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Character/CharacterDataCache.h"
#include "Engine/Character/Socket/ResolvedSocketTable.h"
#include "Engine/Character/Socket/SocketSet.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Physics/Asset/PhysicsAsset.h"
#include "Engine/Physics/IPhysicsScene.h"
#include "Engine/Physics/PhysicsSystem.h"
#include "Engine/Physics/Scene/ScenePhysics.h"

namespace sw
{
    namespace
    {
        struct PoseModifierComponentInternal
        {
            /** @brief 행렬의 회전과 이동을 구합니다(스케일은 버립니다). */
            static void decomposeRigid( const float4x4& matrix, float3& outPosition, quaternion& outRotation )
            {
                float3 scale{};
                (void)matrix.decompose( scale, outRotation, outPosition );
            }

            /** @brief 씬 물리의 광선 질의를 리그의 땅 질의로 보입니다. 3D 씬, 평면 리그면 2D 씬을 씁니다(만들지 않고 있을 때만). */
            class SceneGroundQuery final : public IRigGroundQuery
            {
            public:
                SceneGroundQuery( GameObjectManager& manager, bool bPlanar )
                    : _manager{ manager }
                    , _bPlanar{ bPlanar }
                {
                }

                bool raycastGround( const float3& origin, const float3& direction, float32 maxDistance, RigGroundHit& outHit ) const override
                {
                    PhysicsQueryFilter  filter{};
                    const ScenePhysics& physics = _manager.getSubsystem<sw::ScenePhysics>();
                    if ( _bPlanar )
                    {
                        const IPhysicsQueryScene2D* pScene = physics.findScene2D();
                        PhysicsCastHit2D            hit{};
                        if ( pScene == nullptr || pScene->raycast( float2{ origin._x, origin._y }, float2{ direction._x, direction._y }, maxDistance, filter, hit ) == false )
                            return false;
                        outHit._position = float3{ hit._point._x, hit._point._y, origin._z };
                        outHit._normal   = float3{ hit._normal._x, hit._normal._y, 0.0f };
                        return true;
                    }
                    const IPhysicsQueryScene3D* pScene = physics.findScene3D();
                    PhysicsCastHit3D            hit{};
                    if ( pScene == nullptr || pScene->raycast( origin, direction, maxDistance, filter, hit ) == false )
                        return false;
                    outHit._position = hit._point;
                    outHit._normal   = hit._normal;
                    return true;
                }

            private:
                GameObjectManager& _manager;
                uint8              _bPlanar : 1;
            };
        };
    } // namespace
} // namespace sw

namespace sw
{
    PoseModifierBinding::PoseModifierBinding( PoseModifierComponent& owner )
        : _owner{ owner }
    {
    }

    bool PoseModifierBinding::isAnimationActive() const
    {
        return _owner._bRigEnabled == SW_TRUE && _owner._instance.isInitialized();
    }

    AnimationPoseAccess PoseModifierBinding::getPoseAccess() const
    {
        const bool bWritesPose = isAnimationActive() && _owner._instance.canWritePose();
        return bWritesPose ? AnimationPoseAccess::Writes : AnimationPoseAccess::ReadsOnly;
    }

    void PoseModifierBinding::prepareAnimationFrame( SkeletalMeshComponent& skeletalMesh, const AnimationFrameContext& context )
    {
        _owner.prepareFrame( skeletalMesh, context );
    }

    void PoseModifierBinding::runAnimationPhase( AnimationPhase phase, SkeletalMeshComponent& skeletalMesh, const AnimationFrameContext& context )
    {
        (void)context;
        if ( phase == AnimationPhase::PostProcess )
            _owner.evaluateFrame( skeletalMesh );
    }

    void PoseModifierBinding::onAnimationMeshDetached( SkeletalMeshComponent& skeletalMesh )
    {
        if ( _owner._pSkeletalMesh == &skeletalMesh )
            _owner._pSkeletalMesh = nullptr;
    }

    PoseModifierCurveSource::PoseModifierCurveSource( const PoseModifierComponent& owner )
        : _owner{ owner }
    {
    }

    float32 PoseModifierCurveSource::getCurveValue( const hashed_string& curveName ) const
    {
        // 같은 유닛의 애니메이터다. 시간 단계(같은 유닛, 앞 단계)가 이번 프레임 커브를 기록했다.
        return ( _owner._pFrameAnimator != nullptr ) ? _owner._pFrameAnimator->getCurveValue( curveName ) : 0.0f;
    }

    PoseModifierSocketResolver::PoseModifierSocketResolver( const PoseModifierComponent& owner )
        : _owner{ owner }
    {
    }

    RigSocketLookup PoseModifierSocketResolver::findSocket( const hashed_string& socketName, hashed_string& outParentBone, BoneTransform& outLocal ) const
    {
        const ResolvedSocketTable* pTable = _owner._pSocketTable;
        if ( pTable != nullptr )
        {
            const SocketID   socketID = pTable->resolveTarget( pTable->findSocket( socketName ) );
            const SocketDef* pDef     = ( socketID != kInvalidSocketID ) ? pTable->findSocketDef( socketID ) : nullptr;
            if ( pDef != nullptr )
            {
                const uint32 skeletalMeshIndex = pTable->getSocketUnit( socketID );
                const bool   bOwnUnit          = skeletalMeshIndex < _owner._listTableUnit.size() && _owner._pSkeletalMesh != nullptr &&
                                      _owner._listTableUnit[skeletalMeshIndex] == _owner._pSkeletalMesh->getHandle();
                if ( bOwnUnit == false )
                    return RigSocketLookup::OtherUnit;
                // 표면 기준 소켓은 체형 보정을 더한 로컬이다(테이블이 계산해 둔다).
                float4x4 local = pDef->makeLocalTransform();
                (void)pTable->computeLocalTransform( socketID, local ); // 켜진 소켓이라 실패하지 않는다. 실패해도 보정 없는 로컬이 남는다
                outParentBone = pDef->_parent;
                outLocal      = BoneTransform::makeFromMatrix( local );
                return RigSocketLookup::OwnUnit;
            }
        }
        const SocketDef* pDef = ( _owner._ownSockets != nullptr ) ? _owner._ownSockets->findSocket( socketName ) : nullptr;
        if ( pDef == nullptr )
            return RigSocketLookup::Missing;
        outParentBone = pDef->_parent;
        outLocal      = BoneTransform::makeFromMatrix( pDef->makeLocalTransform() );
        return RigSocketLookup::OwnUnit;
    }

    PoseModifierComponent::PoseModifierComponent()
        : _rigPath{}
        , _socketSetPath{}
        , _physicsAssetPath{}
        , _binding{ *this }
        , _curveSource{ *this }
        , _socketResolver{ *this }
        , _instance{}
        , _asset{}
        , _ownSockets{}
        , _listExternal{}
        , _listUnitBinding{}
        , _listObjectBinding{}
        , _listDependency{}
        , _listTableUnit{}
        , _groundQuery{}
        , _worldFromModel{ float4x4::Identity }
        , _pSocketTable{ nullptr }
        , _pGroundQueryOverride{ nullptr }
        , _pFrameAnimator{ nullptr }
        , _pSkeletalMesh{ nullptr }
        , _pBoundSkeleton{ nullptr }
        , _boundContentID{}
        , _bRigEnabled{ SW_TRUE }
        , _reserved{ 0 }
    {
        _instance.setCurveSource( &_curveSource );
    }

    PoseModifierComponent::~PoseModifierComponent()
    {
        if ( _pSkeletalMesh != nullptr )
            _bindingRegistration.reset();
    }

    void PoseModifierComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        if ( _asset == nullptr && _rigPath.empty() == false )
            _asset = SharedAssetCache<RigAsset>::acquire( _rigPath );
        if ( _ownSockets == nullptr && _socketSetPath.empty() == false )
            setSocketSetPath( _socketSetPath );
        bindRig();
    }

    void PoseModifierComponent::onEndPlay()
    {
        releaseDependencies();
        if ( _pSkeletalMesh != nullptr )
            _bindingRegistration.reset();
        _pSkeletalMesh = nullptr;
        _instance.shutdown();
        Component::onEndPlay();
    }

    void PoseModifierComponent::onPropertyChanged( hashed_string propertyName )
    {
        Component::onPropertyChanged( propertyName );
        static const hashed_string s_rigPathName( "_rigPath" );
        static const hashed_string s_socketPathName( "_socketSetPath" );
        static const hashed_string s_physicsPathName( "_physicsAssetPath" );
        if ( propertyName == s_rigPathName )
            setRigPath( _rigPath );
        else if ( propertyName == s_socketPathName )
            setSocketSetPath( _socketSetPath );
        else if ( propertyName == s_physicsPathName )
            setPhysicsAssetPath( _physicsAssetPath );
    }

    void PoseModifierComponent::setRigPath( string_view path )
    {
        _rigPath = string{ path };
        _asset   = _rigPath.empty() ? nullptr : SharedAssetCache<RigAsset>::acquire( _rigPath );
        if ( _rigPath.empty() == false && _asset == nullptr )
            SW_LOG_ERROR_ID( "PoseModifier.RigCouldNotLoaded", "Rig '%#' could not be loaded", _rigPath.c_str() );
        bindRig();
    }

    void PoseModifierComponent::setRigAsset( shared_ptr<const RigAsset> asset )
    {
        _asset = std::move( asset );
        bindRig();
    }

    void PoseModifierComponent::setSocketSetPath( string_view path )
    {
        _socketSetPath = path;
        // 소켓 세트는 공유 캐시 한 길로 받는다 — `SocketSetComponent` 와 같은 객체다. 예전에는 여기서 파일과 기본 소켓 종류 표를 직접 다시 읽었다(같은 일 두 벌).
        _ownSockets = _socketSetPath.acquire();
        if ( _socketSetPath.empty() == false && _ownSockets == nullptr )
            SW_LOG_ERROR_ID( "PoseModifier.SocketsCouldNotLoaded", "Sockets '%#' could not be loaded for the rig", _socketSetPath.c_str() );
        bindRig();
    }

    void PoseModifierComponent::setOwnSockets( shared_ptr<const SocketSet> sockets )
    {
        _ownSockets = std::move( sockets );
        bindRig();
    }

    void PoseModifierComponent::setPhysicsAssetPath( string_view path )
    {
        _physicsAssetPath = path;
        loadSharedColliders();
    }

    void PoseModifierComponent::bindUnit( const hashed_string& skeletalMeshName, SkeletalMeshComponent* pSkeletalMesh, shared_ptr<const SocketSet> sockets )
    {
        SkeletalMeshBinding binding{};
        binding._name         = skeletalMeshName;
        binding._sockets      = std::move( sockets );
        binding._skeletalMesh = TComponentHandle<SkeletalMeshComponent>{ pSkeletalMesh };
        for ( SkeletalMeshBinding& existing : _listUnitBinding )
        {
            if ( existing._name == skeletalMeshName )
            {
                existing = binding;
                bindExternalTargets();
                return;
            }
        }
        _listUnitBinding.push_back( binding );
        bindExternalTargets();
    }

    void PoseModifierComponent::bindRigidUnit( const hashed_string& skeletalMeshName, GameObject* pObject, shared_ptr<const SocketSet> sockets )
    {
        SkeletalMeshBinding binding{};
        binding._name        = skeletalMeshName;
        binding._sockets     = std::move( sockets );
        binding._rigidObject = ( pObject != nullptr ) ? pObject->getHandle() : GameObjectHandle{};
        for ( SkeletalMeshBinding& existing : _listUnitBinding )
        {
            if ( existing._name == skeletalMeshName )
            {
                existing = binding;
                bindExternalTargets();
                return;
            }
        }
        _listUnitBinding.push_back( binding );
        bindExternalTargets();
    }

    void PoseModifierComponent::bindRigidComponentUnit( const hashed_string& skeletalMeshName, SceneComponent* pFrame, shared_ptr<const SocketSet> sockets )
    {
        SkeletalMeshBinding binding{};
        binding._name                  = skeletalMeshName;
        binding._sockets               = std::move( sockets );
        binding._rigidFrame            = TComponentHandle<SceneComponent>{ pFrame };
        SkeletalMeshBinding* pExisting = VectorUtil::findIf( _listUnitBinding, [&skeletalMeshName]( const SkeletalMeshBinding& existing )
        { return existing._name == skeletalMeshName; } );
        if ( pExisting != nullptr )
            *pExisting = binding;
        else
            _listUnitBinding.push_back( binding );
        bindExternalTargets();
    }

    void PoseModifierComponent::bindObject( const hashed_string& objectName, GameObject* pObject )
    {
        const GameObjectHandle handle = ( pObject != nullptr ) ? pObject->getHandle() : GameObjectHandle{};
        for ( ObjectBinding& existing : _listObjectBinding )
        {
            if ( existing._name == objectName )
            {
                existing._object = handle;
                bindExternalTargets();
                return;
            }
        }
        _listObjectBinding.push_back( ObjectBinding{ objectName, handle } );
        bindExternalTargets();
    }

    void PoseModifierComponent::setSocketTable( const ResolvedSocketTable* pTable, const vector<SkeletalMeshComponent*>& listUnitByIndex )
    {
        _pSocketTable = pTable;
        _listTableUnit.clear();
        for ( SkeletalMeshComponent* pSkeletalMesh : listUnitByIndex )
        {
            _listTableUnit.push_back( TComponentHandle<SkeletalMeshComponent>{ pSkeletalMesh } );
        }
        bindRig();
    }

    void PoseModifierComponent::setRigEnabled( bool bEnabled )
    {
        _bRigEnabled = bEnabled ? SW_TRUE : SW_FALSE;
        if ( bEnabled )
            _instance.reset();
        if ( _pSkeletalMesh != nullptr )
            _pSkeletalMesh->markPoseDirty(); // 끈 프레임에 리그 없는 포즈를 한 번 다시 만든다.
    }

    bool PoseModifierComponent::isTargetBound( const hashed_string& targetName ) const
    {
        const optional<uint32> targetIndex = _instance.isInitialized() ? _instance.findTargetIndex( targetName ) : nullopt;
        if ( targetIndex.has_value() == false )
            return false;
        if ( _instance.isTargetExternal( *targetIndex ) == false )
            return true;
        return *targetIndex < _listExternal.size() && _listExternal[*targetIndex]._bBound == SW_TRUE;
    }

    void PoseModifierComponent::releaseDependencies()
    {
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = ( pOwner != nullptr ) ? pOwner->getManager() : nullptr;
        if ( _pSkeletalMesh != nullptr && pManager != nullptr )
        {
            for ( const TComponentHandle<SkeletalMeshComponent> handle : _listDependency )
            {
                _pSkeletalMesh->removeAnimationDependency( pManager->resolveComponent( handle ) );
            }
        }
        _listDependency.clear();
    }

    void PoseModifierComponent::bindRig()
    {
        // 시작 전의 세터(리그, 소켓, 유닛 등록)는 값만 적는다. 바인딩은 onBeginPlay 가 한 번에 한다(순서에 따른 가짜 오류가 없게 하려는 것이다).
        if ( hasBegunPlay() == false )
            return;
        releaseDependencies();
        _instance.shutdown();
        _listExternal.clear();
        _boneRequirement.reset();
        _boundContentID.reset();
        GameObject*            pOwner        = getOwner();
        SkeletalMeshComponent* pSkeletalMesh = ( pOwner != nullptr ) ? pOwner->getComponent<SkeletalMeshComponent>() : nullptr;
        if ( pSkeletalMesh != _pSkeletalMesh )
        {
            if ( _pSkeletalMesh != nullptr )
                _bindingRegistration.reset();
            _pSkeletalMesh = pSkeletalMesh;
            if ( _pSkeletalMesh != nullptr )
                _bindingRegistration = _pSkeletalMesh->registerAnimationPhaseTask( &_binding );
        }
        _pBoundSkeleton = ( _pSkeletalMesh != nullptr ) ? &_pSkeletalMesh->getSkeleton() : nullptr;
        if ( _pSkeletalMesh == nullptr || _asset == nullptr )
            return;
        // 데이터 스켈레톤을 아직 받지 않은 유닛(전용 서버는 외형을 입히지 않고, 외형은 시작 뒤에 입혀질 수 있다)에는 묶지 않는다 — 리그는 그 뼈를
        // 이름으로 찾으므로 암묵 스켈레톤에는 늘 실패한다. 스켈레톤이 바뀌면 프레임 준비(prepareFrame)가 다시 묶는다.
        if ( _pSkeletalMesh->hasDataSkeleton() == false )
            return;
        // 소켓을 이름으로 찾는 대상이 있는데 소켓 출처(외형이 풀어 준 소켓 표 · 자기 소켓 세트)가 아직 없으면 묶지 않는다 — 스켈레톤과 같은 까닭이다.
        // 외형이 시작 뒤에 입혀지면 그 사이에 묶어 "unknown socket" 을 냈다(출처가 오면 `setSocketTable` · `setOwnSockets` 가 다시 묶는다). 출처가 있는데 이름이 없으면 그때가 진짜 오류다.
        if ( needsSockets() && _pSocketTable == nullptr && _ownSockets == nullptr )
            return;

        const string label = _rigPath.empty() ? string{ pOwner->getName().c_str() } : _rigPath.path();
        if ( _instance.initialize( _asset, _pSkeletalMesh->getSkeleton(), &_socketResolver, label ) == false )
        {
            SW_LOG_ERROR_ID( "PoseModifier.RigCouldNotBound", "Rig '%#' could not be bound to '%#'", label.c_str(), pOwner->getName().c_str() );
            return;
        }
        _boundContentID = _asset->getContentID();
        _groundQuery.reset();
        if ( pOwner->getManager() != nullptr )
            _groundQuery = make_unique<PoseModifierComponentInternal::SceneGroundQuery>( *pOwner->getManager(), _instance.getSolveSpace()._bPlanar == SW_TRUE );
        loadSharedColliders();
        bindExternalTargets();
        // 리그가 읽고 쓰는 본(대상 · 노드 사슬 · 바깥 대상의 공간 본)은 본 LOD 가 빼지 않는다 — 빠지면 IK 대상이 레퍼런스 포즈로 굳는다.
        vector<uint32> listBone;
        _instance.collectBoundBones( listBone );
        for ( const ExternalTarget& external : _listExternal )
        {
            if ( external._spaceBone >= 0 )
                listBone.push_back( static_cast<uint32>( external._spaceBone ) );
        }
        vector<hashed_string> listBoneName;
        for ( const uint32 bone : listBone )
        {
            listBoneName.push_back( _pSkeletalMesh->getSkeleton().getBone( bone )._name );
        }
        _boneRequirement = _pSkeletalMesh->requireBones( std::move( listBoneName ) );
        _pSkeletalMesh->markPoseDirty();
    }

    bool PoseModifierComponent::needsSockets() const
    {
        if ( _asset == nullptr )
            return false;
        for ( const RigTargetDef& target : _asset->getTargets() )
        {
            if ( target._kind == RigTargetKind::Socket && target.isExternal() == false )
                return true;
        }
        return false;
    }

    const PoseModifierComponent::SkeletalMeshBinding* PoseModifierComponent::findUnitBinding( const hashed_string& skeletalMeshName ) const
    {
        for ( const SkeletalMeshBinding& binding : _listUnitBinding )
        {
            if ( binding._name == skeletalMeshName )
                return &binding;
        }
        return nullptr;
    }

    SkeletalMeshComponent* PoseModifierComponent::findUnitByName( const hashed_string& skeletalMeshName, shared_ptr<const SocketSet>* pOutSockets ) const
    {
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = ( pOwner != nullptr ) ? pOwner->getManager() : nullptr;
        if ( pManager == nullptr )
            return nullptr;
        const SkeletalMeshBinding* pBinding = findUnitBinding( skeletalMeshName );
        if ( pBinding != nullptr )
        {
            if ( pOutSockets != nullptr )
                *pOutSockets = pBinding->_sockets;
            return pManager->resolveComponent( pBinding->_skeletalMesh ); // 강체 연결이면 비어 있다
        }
        // 이름으로 찾는다. 자식(장비 부품)부터 찾고, 없으면 매니저 전체에서 찾는다.
        vector<GameObject*> listChild;
        pOwner->getChildren( listChild );
        for ( GameObject* pChild : listChild )
        {
            if ( pChild->getName() == skeletalMeshName )
                return pChild->getComponent<SkeletalMeshComponent>();
        }
        GameObject* pFound = pManager->findGameObjectByName( skeletalMeshName );
        return ( pFound != nullptr ) ? pFound->getComponent<SkeletalMeshComponent>() : nullptr;
    }

    GameObject* PoseModifierComponent::findObjectByName( const hashed_string& objectName ) const
    {
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = ( pOwner != nullptr ) ? pOwner->getManager() : nullptr;
        if ( pManager == nullptr )
            return nullptr;
        for ( const ObjectBinding& binding : _listObjectBinding )
        {
            if ( binding._name == objectName )
                return pManager->resolveGameObject( binding._object );
        }
        return pManager->findGameObjectByName( objectName );
    }

    bool PoseModifierComponent::wouldCreateCycle( const SkeletalMeshComponent* pUpstream ) const
    {
        GameObjectManager* pManager = ( getOwner() != nullptr ) ? getOwner()->getManager() : nullptr;
        if ( pManager == nullptr || pUpstream == nullptr )
            return false;
        // 위 유닛에서 의존을 따라 올라가다 내 유닛을 만나면 고리다.
        vector<const SkeletalMeshComponent*> listPending{ pUpstream };
        vector<const SkeletalMeshComponent*> listVisited;
        while ( listPending.empty() == false )
        {
            const SkeletalMeshComponent* pCurrent = listPending.back();
            listPending.pop_back();
            if ( pCurrent == _pSkeletalMesh )
                return true;
            if ( VectorUtil::contains( listVisited, pCurrent ) )
                continue;
            listVisited.push_back( pCurrent );
            for ( const TComponentHandle<SkeletalMeshComponent> handle : pCurrent->getAnimationDependencies() )
            {
                const SkeletalMeshComponent* pNext = pManager->resolveComponent( handle );
                if ( pNext != nullptr )
                    listPending.push_back( pNext );
            }
        }
        return false;
    }

    bool PoseModifierComponent::resolveSocketTarget( const RigTargetDef& def, string_view label, SocketTargetResolution& outResolved ) const
    {
        SkeletalMeshComponent*      pTargetUnit = nullptr;
        shared_ptr<const SocketSet> sockets;
        hashed_string               boneName = def._bone;
        BoneTransform               socketLocal{};
        bool                        bSocketRoot  = false;
        const SkeletalMeshBinding*  pBinding     = ( def._skeletalMesh.empty() == false ) ? findUnitBinding( def._skeletalMesh ) : nullptr;
        GameObjectManager*          pManager     = getOwner()->getManager();
        GameObject*                 pRigidObject = ( pBinding != nullptr && pManager != nullptr ) ? pManager->resolveGameObject( pBinding->_rigidObject ) : nullptr;
        SceneComponent*             pRigidFrame  = ( pBinding != nullptr && pManager != nullptr ) ? pManager->resolveComponent( pBinding->_rigidFrame ) : nullptr;
        if ( pRigidObject != nullptr || pRigidFrame != nullptr )
        {
            // 강체 유닛(본 없는 장비 부품)이다. 소켓만 대상이 될 수 있고 소켓 로컬은 오브젝트 원점 기준이다.
            const SocketDef* pDef = ( def._kind == RigTargetKind::Socket && pBinding->_sockets != nullptr ) ? pBinding->_sockets->findSocket( def._socket ) : nullptr;
            if ( pDef == nullptr )
            {
                SW_LOG_ERROR_ID( "PoseModifier.RigTargetRigidUnit", "Rig '%#': target '%#' on rigid skeletalMesh '%#' must name one of its sockets (it has no bones) - '%#' not found", label, def._name.c_str(),
                                 def._skeletalMesh.c_str(), def._socket.c_str() );
                return false;
            }
            outResolved._pRigidObject = pRigidObject;
            outResolved._pRigidFrame  = pRigidFrame;
            outResolved._socketLocal  = BoneTransform::makeFromMatrix( pDef->makeLocalTransform() );
            return true;
        }
        if ( def._skeletalMesh.empty() == false )
        {
            pTargetUnit = findUnitByName( def._skeletalMesh, &sockets );
            if ( pTargetUnit != nullptr && def._kind == RigTargetKind::Socket )
            {
                const SocketDef* pDef = ( sockets != nullptr ) ? sockets->findSocket( def._socket ) : nullptr;
                if ( pDef == nullptr )
                {
                    SW_LOG_ERROR_ID( "PoseModifier.RigTargetNamesSocket", "Rig '%#': target '%#' names socket '%#' but skeletalMesh '%#' has no such socket (bindUnit with its sockets)", label, def._name.c_str(),
                                     def._socket.c_str(), def._skeletalMesh.c_str() );
                    return false;
                }
                boneName    = pDef->_parent;
                socketLocal = BoneTransform::makeFromMatrix( pDef->makeLocalTransform() );
                bSocketRoot = boneName.empty();
            }
        }
        else if ( _pSocketTable != nullptr )
        {
            const SocketID   socketID          = _pSocketTable->resolveTarget( _pSocketTable->findSocket( def._socket ) );
            const SocketDef* pDef              = ( socketID != kInvalidSocketID ) ? _pSocketTable->findSocketDef( socketID ) : nullptr;
            const uint32     skeletalMeshIndex = ( pDef != nullptr ) ? _pSocketTable->getSocketUnit( socketID ) : MathUtil::kMaxUInt32;
            if ( skeletalMeshIndex < _listTableUnit.size() && getOwner()->getManager() != nullptr )
                pTargetUnit = getOwner()->getManager()->resolveComponent( _listTableUnit[skeletalMeshIndex] );
            if ( pDef != nullptr )
            {
                float4x4 local = pDef->makeLocalTransform();
                (void)_pSocketTable->computeLocalTransform( socketID, local ); // 체형 보정을 더한다. 켜진 소켓이라 실패하지 않는다
                boneName    = pDef->_parent;
                socketLocal = BoneTransform::makeFromMatrix( local );
                bSocketRoot = boneName.empty();
            }
        }
        outResolved._pTargetUnit = pTargetUnit;
        outResolved._boneName    = boneName;
        outResolved._socketLocal = socketLocal;
        outResolved._bSocketRoot = bSocketRoot;
        return true;
    }

    void PoseModifierComponent::bindExternalTargets()
    {
        if ( hasBegunPlay() == false )
            return;
        releaseDependencies();
        if ( _instance.isInitialized() == false || _pSkeletalMesh == nullptr )
            return;
        const string_view label = _rigPath.empty() ? string_view{ "(runtime rig)" } : string_view{ _rigPath };
        _listExternal.assign( _instance.getTargetCount(), ExternalTarget{} );
        for ( uint32 targetIndex = 0; targetIndex < _instance.getTargetCount(); ++targetIndex )
        {
            if ( _instance.isTargetExternal( targetIndex ) == false )
                continue;
            // 지난 연결의 값을 비운다 — 연결이 풀린 대상(무기를 내려놓음)은 프레임 준비가 값을 넣지 않으므로 옛 값이 남는다.
            _instance.setExternalTarget( targetIndex, RigTargetValue{} );
            const RigTargetDef& def      = _instance.getTargetDef( targetIndex );
            ExternalTarget&     external = _listExternal[targetIndex];
            external._socketLocal        = def._offset;
            if ( def._space.empty() == false )
            {
                external._spaceBone = Skeleton::toBoneSlot( _pSkeletalMesh->getSkeleton().findBoneIndex( def._space ) );
                if ( external._spaceBone < 0 )
                {
                    SW_LOG_ERROR_ID( "PoseModifier.RigTargetUnknownSpace", "Rig '%#': target '%#' has unknown space bone '%#'", label, def._name.c_str(), def._space.c_str() );
                    continue;
                }
            }

            if ( def._kind == RigTargetKind::Object )
            {
                GameObject* pObject = findObjectByName( def._object );
                if ( pObject == nullptr )
                    continue; // 아직 없다. bindObject 가 다시 바인딩한다.
                external._object = pObject->getHandle();
                external._bBound = SW_TRUE;
                continue;
            }

            // 다른 유닛의 본이나 소켓이다. 유닛 이름이 없으면 해석된 소켓 테이블의 다른 유닛 소켓이다.
            SocketTargetResolution resolved;
            if ( resolveSocketTarget( def, label, resolved ) == false )
                continue;
            if ( resolved._pRigidObject != nullptr || resolved._pRigidFrame != nullptr )
            {
                // 오브젝트 대상과 같은 길로 프레임 시작에 기록한다(오프셋 × 소켓 로컬 × 오브젝트 · 컴포넌트 월드).
                external._object      = resolved._pRigidObject != nullptr ? resolved._pRigidObject->getHandle() : GameObjectHandle{};
                external._frame       = TComponentHandle<SceneComponent>{ resolved._pRigidFrame };
                external._socketLocal = BoneTransform::makeFromMatrix( def._offset.toMatrix() * resolved._socketLocal.toMatrix() );
                external._bBound      = SW_TRUE;
                continue;
            }
            SkeletalMeshComponent* pTargetUnit = resolved._pTargetUnit;
            const hashed_string&   boneName    = resolved._boneName;
            const BoneTransform&   socketLocal = resolved._socketLocal;
            const bool             bSocketRoot = resolved._bSocketRoot;
            if ( pTargetUnit == nullptr || pTargetUnit == _pSkeletalMesh )
                continue; // 아직 없다. bindUnit 이 다시 바인딩한다.
            external._skeletalMeshBone = bSocketRoot ? constant::invalid_index::kInt32 : Skeleton::toBoneSlot( pTargetUnit->getSkeleton().findBoneIndex( boneName ) );
            if ( bSocketRoot == false && external._skeletalMeshBone < 0 )
            {
                SW_LOG_ERROR_ID( "PoseModifier.RigTargetNamesBone", "Rig '%#': target '%#' names bone '%#' that skeletalMesh '%#' does not have", label, def._name.c_str(), boneName.c_str(),
                                 pTargetUnit->getOwner() != nullptr ? pTargetUnit->getOwner()->getName().c_str() : "" );
                continue;
            }
            // 데이터 오프셋은 소켓 공간에서 먼저 적용된다(행 벡터: 오프셋 × 소켓 로컬).
            external._socketLocal  = BoneTransform::makeFromMatrix( def._offset.toMatrix() * socketLocal.toMatrix() );
            external._skeletalMesh = TComponentHandle<SkeletalMeshComponent>{ pTargetUnit };
            if ( external._spaceBone >= 0 )
            {
                external._bBound = SW_TRUE; // 프레임 시작에 기록해 자기 본에 적용한다. 의존은 등록하지 않는다.
                continue;
            }
            if ( wouldCreateCycle( pTargetUnit ) )
            {
                SW_LOG_ERROR_ID( "PoseModifier.RigTargetUnitWould", "Rig '%#': target '%#' on skeletalMesh '%#' would create an animation dependency cycle - the target is disabled", label, def._name.c_str(),
                                 pTargetUnit->getOwner() != nullptr ? pTargetUnit->getOwner()->getName().c_str() : "" );
                continue;
            }
            _pSkeletalMesh->addAnimationDependency( pTargetUnit );
            _listDependency.push_back( TComponentHandle<SkeletalMeshComponent>{ pTargetUnit } );
            external._bLive  = SW_TRUE;
            external._bBound = SW_TRUE;
        }
    }

    void PoseModifierComponent::loadSharedColliders()
    {
        vector<RigSpringCollider> listCollider;
        // 물리 에셋은 래그돌과 같은 공유 캐시의 한 벌이다(사본을 읽지 않는다).
        const bool                           bWanted = _physicsAssetPath.empty() == false && _pSkeletalMesh != nullptr;
        const shared_ptr<const PhysicsAsset> pAsset  = bWanted ? SharedAssetCache<PhysicsAsset>::acquire( _physicsAssetPath ) : nullptr;
        if ( bWanted && pAsset == nullptr )
            SW_LOG_ERROR_ID( "PoseModifier.PhysicsAssetCouldNot", "Physics asset '%#' could not be loaded for spring colliders", _physicsAssetPath.c_str() );
        if ( pAsset != nullptr )
        {
            for ( const PhysicsAssetBodyDef& body : pAsset->_listBody )
            {
                const optional<uint32> bone = _pSkeletalMesh->getSkeleton().findBoneIndex( body._bone );
                if ( bone.has_value() == false )
                    continue;
                for ( const PhysicsShapeDesc3D& shape : body._listShape )
                {
                    RigSpringCollider collider{};
                    collider._bone   = *bone;
                    collider._radius = shape._radius;
                    if ( shape._type == PhysicsShapeType3D::Sphere )
                    {
                        collider._shape  = RigSpringColliderShape::Sphere;
                        collider._pointA = shape._localPosition;
                    }
                    else if ( shape._type == PhysicsShapeType3D::Capsule )
                    {
                        const float3 axis = float3::transform( float3{ 0.0f, shape._halfHeight, 0.0f }, quaternion::makeFromYawPitchRoll( shape._localRotation ) );
                        collider._shape   = RigSpringColliderShape::Capsule;
                        collider._pointA  = shape._localPosition + axis;
                        collider._pointB  = shape._localPosition - axis;
                    }
                    else
                    {
                        continue; // 상자, 볼록, 메시는 스프링 충돌체가 아니다.
                    }
                    listCollider.push_back( collider );
                }
            }
        }
        _instance.setSharedColliders( listCollider );
    }

    void PoseModifierComponent::prepareFrame( SkeletalMeshComponent& skeletalMesh, const AnimationFrameContext& context )
    {
        if ( _bRigEnabled == SW_FALSE )
            return;
        // 리그가 핫 리로드됐거나(내용 번호가 바뀜) 유닛이 다른 스켈레톤을 받았으면(외형을 입음) 다시 묶는다.
        const bool bRigChanged      = _asset != nullptr && _asset->getContentID() != _boundContentID && skeletalMesh.hasDataSkeleton();
        const bool bSkeletonChanged = &skeletalMesh.getSkeleton() != _pBoundSkeleton;
        if ( bRigChanged || bSkeletonChanged )
            bindRig();
        if ( _instance.isInitialized() == false )
            return;

        _pFrameAnimator                        = ( getOwner() != nullptr ) ? getOwner()->getComponent<SkeletalAnimatorComponent>() : nullptr;
        _worldFromModel                        = skeletalMesh.getWorldMatrix();
        const float4x4          modelFromWorld = _worldFromModel.invert();
        GameObjectManager*      pManager       = ( getOwner() != nullptr ) ? getOwner()->getManager() : nullptr;
        const vector<float4x4>& listOwnModel   = skeletalMesh.getModelSpaceTransforms(); // 지난 프레임의 최종 포즈
        for ( uint32 targetIndex = 0; targetIndex < static_cast<uint32>( _listExternal.size() ); ++targetIndex )
        {
            ExternalTarget& external = _listExternal[targetIndex];
            if ( external._bBound == SW_FALSE || pManager == nullptr )
                continue;
            float4x4 targetInModel{};
            if ( external._object.isValid() || external._frame.isValid() )
            {
                const GameObject*     pObject = pManager->resolveGameObject( external._object );
                const SceneComponent* pScene  = ( pObject != nullptr ) ? pObject->getPrimarySceneComponent() : pManager->resolveComponent( external._frame );
                if ( pScene == nullptr )
                {
                    _instance.setExternalTarget( targetIndex, RigTargetValue{} );
                    continue;
                }
                targetInModel = external._socketLocal.toMatrix() * pScene->getWorldMatrix() * modelFromWorld;
            }
            else
            {
                const SkeletalMeshComponent* pTargetUnit = pManager->resolveComponent( external._skeletalMesh );
                external._pFrameUnit                     = pTargetUnit;
                if ( pTargetUnit == nullptr )
                {
                    _instance.setExternalTarget( targetIndex, RigTargetValue{} );
                    continue;
                }
                external._skeletalMeshToModel = pTargetUnit->getWorldMatrix() * modelFromWorld;
                if ( external._bLive == SW_TRUE )
                    continue; // 평가 중에 그 유닛의 이번 프레임 포즈를 읽는다
                const vector<float4x4>& listModel = pTargetUnit->getModelSpaceTransforms();
                const float4x4          boneModel = ( 0 <= external._skeletalMeshBone && static_cast<size_t>( external._skeletalMeshBone ) < listModel.size() )
                                                      ? listModel[static_cast<size_t>( external._skeletalMeshBone )]
                                                      : float4x4::Identity;
                targetInModel                     = external._socketLocal.toMatrix() * boneModel * external._skeletalMeshToModel;
            }

            RigTargetValue value{};
            value._bValid = SW_TRUE;
            PoseModifierComponentInternal::decomposeRigid( targetInModel, value._position, value._rotation );
            if ( 0 <= external._spaceBone && static_cast<size_t>( external._spaceBone ) < listOwnModel.size() )
            {
                // 지난 프레임의 그 본 기준으로 바꿔 두면 평가할 때 이번 프레임의 그 본에 다시 적용된다.
                float3     spacePosition{};
                quaternion spaceRotation{};
                PoseModifierComponentInternal::decomposeRigid( listOwnModel[static_cast<size_t>( external._spaceBone )], spacePosition, spaceRotation );
                const quaternion inverse = RigIKSolver::makeInverse( spaceRotation );
                value._position          = float3::transform( value._position - spacePosition, inverse );
                value._rotation          = ( inverse * value._rotation ).normalize();
                value._relativeBone      = external._spaceBone;
            }
            _instance.setExternalTarget( targetIndex, value );
        }

        RigPrepareContext prepare{};
        prepare._worldFromModel = _worldFromModel;
        prepare._deltaSeconds   = context._deltaSeconds;
        prepare._worldGravity   = PhysicsSystem::getConfiguredGravity();
        prepare._pGroundQuery   = ( _pGroundQueryOverride != nullptr ) ? _pGroundQueryOverride : _groundQuery.get();
        // 등급은 지난 LOD 판정의 결과다. 뷰를 받지 않은 시스템(테스트, 서버)에서는 기본값(등급 0)이라 모든 노드가 켜진다.
        prepare._lodState = skeletalMesh.getAnimationLODState();
        _instance.prepare( prepare );
    }

    void PoseModifierComponent::evaluateFrame( SkeletalMeshComponent& skeletalMesh )
    {
        // 꺼졌어도 유닛은 다른 일(애니메이터)로 돌 수 있다. 그때 리그는 아무것도 하지 않는다.
        if ( _bRigEnabled == SW_FALSE || _instance.isInitialized() == false )
            return;
        // 의존을 등록한 유닛은 이 유닛보다 먼저 모든 단계를 마쳤다(최종 포즈). 그 본을 읽어 대상 값을 채운다(워커에서 하며, 그 유닛은 읽기만 한다).
        for ( uint32 targetIndex = 0; targetIndex < static_cast<uint32>( _listExternal.size() ); ++targetIndex )
        {
            const ExternalTarget&        external    = _listExternal[targetIndex];
            const SkeletalMeshComponent* pTargetUnit = external._pFrameUnit;
            if ( external._bLive == SW_FALSE || pTargetUnit == nullptr )
                continue;
            const vector<float4x4>& listModel = pTargetUnit->getModelSpaceTransforms();
            const float4x4          boneModel = ( 0 <= external._skeletalMeshBone && static_cast<size_t>( external._skeletalMeshBone ) < listModel.size() ) ? listModel[static_cast<size_t>( external._skeletalMeshBone )]
                                                                                                                                                            : float4x4::Identity;
            RigTargetValue          value{};
            value._bValid = SW_TRUE;
            PoseModifierComponentInternal::decomposeRigid( external._socketLocal.toMatrix() * boneModel * external._skeletalMeshToModel, value._position, value._rotation );
            _instance.setExternalTarget( targetIndex, value );
        }
        _instance.evaluate( skeletalMesh.getLocalPose(), skeletalMesh.getSkeleton().getParentIndices(), _worldFromModel );
        // 포즈 구동의 보정 모프는 이름이 그리는 메시의 모프 타깃과 같으면 그 가중치에 더한다(GPU 모프 풀이 스키닝 앞에서 적용한다). 기본 포즈 단계가 비운 뒤다.
        for ( const RigMorphWeight& morph : _instance.getMorphWeights() )
        {
            const optional<uint32> targetIndex = skeletalMesh.findMorphTargetIndex( morph._name );
            if ( targetIndex.has_value() )
                skeletalMesh.addMorphWeight( *targetIndex, morph._weight );
        }
    }
} // namespace sw
