#include "pch.h"

#include "Engine/Character/PoseModifier/PoseModifierComponent.h"

#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Animation/AnimationAssetCache.h"
#include "Engine/Animation/Rig/RigAsset.h"
#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Character/Socket/ResolvedSocketTable.h"
#include "Engine/Character/Socket/SocketSet.h"
#include "Engine/Object/Component/3D/SkeletalAnimatorComponent.h"
#include "Engine/Object/Component/3D/SkeletalMeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/ScenePhysics.h"
#include "Engine/Physics/Asset/PhysicsAsset.h"
#include "Engine/Physics/IPhysicsScene.h"
#include "Engine/Physics/PhysicsSystem.h"

namespace sw
{
    SW_LOG_CALLER( "PoseModifier" );

    namespace
    {
        struct PoseModifierComponentInternal
        {
            /** @brief 행렬의 회전 · 이동(스케일은 버림)입니다. */
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
                    const ScenePhysics& physics = _manager.getScenePhysics();
                    if ( _bPlanar )
                    {
                        const IPhysicsScene2D* pScene = physics.findScene2D();
                        PhysicsCastHit2D       hit{};
                        if ( pScene == nullptr || pScene->raycast( float2{ origin._x, origin._y }, float2{ direction._x, direction._y }, maxDistance, filter, hit ) == false )
                            return false;
                        outHit._position = float3{ hit._point._x, hit._point._y, origin._z };
                        outHit._normal   = float3{ hit._normal._x, hit._normal._y, 0.0f };
                        return true;
                    }
                    const IPhysicsScene3D* pScene = physics.findScene3D();
                    PhysicsCastHit3D       hit{};
                    if ( pScene == nullptr || pScene->raycast( origin, direction, maxDistance, filter, hit ) == false )
                        return false;
                    outHit._position = hit._point;
                    outHit._normal   = hit._normal;
                    return true;
                }

            private:
                GameObjectManager& _manager;
                bool               _bPlanar;
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

    void PoseModifierBinding::prepareAnimationFrame( SkeletalMeshComponent& unit, const AnimationFrameContext& context )
    {
        _owner.prepareFrame( unit, context );
    }

    void PoseModifierBinding::runAnimationPhase( AnimationPhase phase, SkeletalMeshComponent& unit, const AnimationFrameContext& context )
    {
        (void)context;
        if ( phase == AnimationPhase::PostProcess )
            _owner.evaluateFrame( unit );
    }

    void PoseModifierBinding::onAnimationUnitDetached( SkeletalMeshComponent& unit )
    {
        if ( _owner._pUnit == &unit )
            _owner._pUnit = nullptr;
    }

    float32 PoseModifierBinding::getCurveValue( const hashed_string& curveName ) const
    {
        // 같은 유닛의 애니메이터 — 시간 단계(같은 유닛, 앞 단계)가 이번 프레임 커브를 적었다.
        return ( _owner._pFrameAnimator != nullptr ) ? _owner._pFrameAnimator->getCurveValue( curveName ) : 0.0f;
    }

    RigSocketLookup PoseModifierBinding::findSocket( const hashed_string& socketName, hashed_string& outParentBone, BoneTransform& outLocal ) const
    {
        const ResolvedSocketTable* pTable = _owner._pSocketTable;
        if ( pTable != nullptr )
        {
            const SocketId   socketId = pTable->resolveTarget( pTable->findSocket( socketName ) );
            const SocketDef* pDef     = ( socketId != kInvalidSocketId ) ? pTable->findSocketDef( socketId ) : nullptr;
            if ( pDef != nullptr )
            {
                const uint32 unitIndex = pTable->getSocketUnit( socketId );
                const bool   bOwnUnit  = unitIndex < _owner._listTableUnit.size() && _owner._pUnit != nullptr &&
                                      _owner._listTableUnit[unitIndex] == _owner._pUnit->getHandle();
                if ( bOwnUnit == false )
                    return RigSocketLookup::OtherUnit;
                outParentBone = pDef->_parent;
                outLocal      = BoneTransform::makeFromMatrix( pDef->makeLocalTransform() );
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
        , _pUnit{ nullptr }
        , _boundContentId{ 0 }
        , _bRigEnabled{ SW_TRUE }
        , _reserved{ 0 }
    {
        _instance.setCurveSource( &_binding );
    }

    PoseModifierComponent::~PoseModifierComponent()
    {
        if ( _pUnit != nullptr )
            _pUnit->removeAnimationPhaseTask( &_binding );
    }

    void PoseModifierComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        if ( _asset == nullptr && _rigPath.empty() == false )
            _asset = RigAssetCache::acquire( _rigPath );
        if ( _ownSockets == nullptr && _socketSetPath.empty() == false )
            setSocketSetPath( _socketSetPath );
        bindRig();
    }

    void PoseModifierComponent::onEndPlay()
    {
        releaseDependencies();
        if ( _pUnit != nullptr )
            _pUnit->removeAnimationPhaseTask( &_binding );
        _pUnit = nullptr;
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
        _asset   = _rigPath.empty() ? nullptr : RigAssetCache::acquire( _rigPath );
        if ( _rigPath.empty() == false && _asset == nullptr )
            SW_LOG_ERROR( "Rig '%#' could not be loaded", _rigPath.c_str() );
        bindRig();
    }

    void PoseModifierComponent::setRigAsset( shared_ptr<const RigAsset> asset )
    {
        _asset = std::move( asset );
        bindRig();
    }

    void PoseModifierComponent::setSocketSetPath( string_view path )
    {
        _socketSetPath = string{ path };
        _ownSockets.reset();
        if ( _socketSetPath.empty() == false )
        {
            SocketKindTable       kinds;
            shared_ptr<SocketSet> sockets = make_shared<SocketSet>();
            if ( kinds.loadFromResource( SocketKindTable::kDefaultPath ) && sockets->loadFromResource( _socketSetPath, kinds ) )
                _ownSockets = std::move( sockets );
            else
                SW_LOG_ERROR( "Sockets '%#' could not be loaded for the rig", _socketSetPath.c_str() );
        }
        bindRig();
    }

    void PoseModifierComponent::setOwnSockets( shared_ptr<const SocketSet> sockets )
    {
        _ownSockets = std::move( sockets );
        bindRig();
    }

    void PoseModifierComponent::setPhysicsAssetPath( string_view path )
    {
        _physicsAssetPath = string{ path };
        loadSharedColliders();
    }

    void PoseModifierComponent::bindUnit( const hashed_string& unitName, SkeletalMeshComponent* pUnit, shared_ptr<const SocketSet> sockets )
    {
        UnitBinding binding{};
        binding._name    = unitName;
        binding._sockets = std::move( sockets );
        binding._unit    = ( pUnit != nullptr ) ? pUnit->getHandle() : ComponentHandle{};
        for ( UnitBinding& existing : _listUnitBinding )
        {
            if ( existing._name == unitName )
            {
                existing = binding;
                bindExternalTargets();
                return;
            }
        }
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
        for ( SkeletalMeshComponent* pUnit : listUnitByIndex )
        {
            _listTableUnit.push_back( pUnit != nullptr ? pUnit->getHandle() : ComponentHandle{} );
        }
        bindRig();
    }

    void PoseModifierComponent::setRigEnabled( bool bEnabled )
    {
        _bRigEnabled = bEnabled ? SW_TRUE : SW_FALSE;
        if ( bEnabled )
            _instance.reset();
        if ( _pUnit != nullptr )
            _pUnit->markPoseDirty(); // 끈 프레임에 리그 없는 포즈를 한 번 다시 만든다
    }

    bool PoseModifierComponent::isTargetBound( const hashed_string& targetName ) const
    {
        const int32 targetIndex = _instance.isInitialized() ? _instance.findTargetIndex( targetName ) : -1;
        if ( targetIndex < 0 )
            return false;
        if ( _instance.isTargetExternal( static_cast<uint32>( targetIndex ) ) == false )
            return true;
        return static_cast<size_t>( targetIndex ) < _listExternal.size() && _listExternal[static_cast<size_t>( targetIndex )]._bBound == SW_TRUE;
    }

    void PoseModifierComponent::releaseDependencies()
    {
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = ( pOwner != nullptr ) ? pOwner->getManager() : nullptr;
        if ( _pUnit != nullptr && pManager != nullptr )
        {
            for ( const ComponentHandle handle : _listDependency )
            {
                _pUnit->removeAnimationDependency( castTo<SkeletalMeshComponent>( pManager->resolveComponent( handle ) ) );
            }
        }
        _listDependency.clear();
    }

    void PoseModifierComponent::bindRig()
    {
        // 시작 전의 세터(리그 · 소켓 · 유닛 걸기)는 값만 적는다 — 묶기는 onBeginPlay 가 한 번에 한다(순서에 따른 가짜 오류가 없게).
        if ( hasBegunPlay() == false )
            return;
        releaseDependencies();
        _instance.shutdown();
        _listExternal.clear();
        _boundContentId               = 0;
        GameObject*            pOwner = getOwner();
        SkeletalMeshComponent* pUnit  = ( pOwner != nullptr ) ? pOwner->getComponent<SkeletalMeshComponent>() : nullptr;
        if ( pUnit != _pUnit )
        {
            if ( _pUnit != nullptr )
                _pUnit->removeAnimationPhaseTask( &_binding );
            _pUnit = pUnit;
            if ( _pUnit != nullptr )
                _pUnit->addAnimationPhaseTask( &_binding );
        }
        if ( _pUnit == nullptr || _asset == nullptr )
            return;

        const string label = _rigPath.empty() ? string{ pOwner->getName().c_str() } : _rigPath;
        if ( _instance.initialize( _asset, _pUnit->getSkeleton(), &_binding, label ) == false )
        {
            SW_LOG_ERROR( "Rig '%#' could not be bound to '%#'", label.c_str(), pOwner->getName().c_str() );
            return;
        }
        _boundContentId = _asset->getContentId();
        _groundQuery.reset();
        if ( pOwner->getManager() != nullptr )
            _groundQuery = make_unique<PoseModifierComponentInternal::SceneGroundQuery>( *pOwner->getManager(), _instance.getSolveSpace()._bPlanar == SW_TRUE );
        loadSharedColliders();
        bindExternalTargets();
        _pUnit->markPoseDirty();
    }

    SkeletalMeshComponent* PoseModifierComponent::findUnitByName( const hashed_string& unitName, shared_ptr<const SocketSet>* pOutSockets ) const
    {
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = ( pOwner != nullptr ) ? pOwner->getManager() : nullptr;
        if ( pManager == nullptr )
            return nullptr;
        for ( const UnitBinding& binding : _listUnitBinding )
        {
            if ( binding._name == unitName )
            {
                if ( pOutSockets != nullptr )
                    *pOutSockets = binding._sockets;
                return castTo<SkeletalMeshComponent>( pManager->resolveComponent( binding._unit ) );
            }
        }
        // 이름으로 찾기 — 자식(장비 부품)부터, 없으면 매니저 전체.
        vector<GameObject*> listChild;
        pOwner->getChildren( listChild );
        for ( GameObject* pChild : listChild )
        {
            if ( pChild->getName() == unitName )
                return pChild->getComponent<SkeletalMeshComponent>();
        }
        GameObject* pFound = pManager->findGameObjectByName( unitName );
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
            if ( pCurrent == _pUnit )
                return true;
            if ( std::find( listVisited.begin(), listVisited.end(), pCurrent ) != listVisited.end() )
                continue;
            listVisited.push_back( pCurrent );
            for ( const ComponentHandle handle : pCurrent->getAnimationDependencies() )
            {
                const SkeletalMeshComponent* pNext = castTo<SkeletalMeshComponent>( pManager->resolveComponent( handle ) );
                if ( pNext != nullptr )
                    listPending.push_back( pNext );
            }
        }
        return false;
    }

    void PoseModifierComponent::bindExternalTargets()
    {
        if ( hasBegunPlay() == false )
            return;
        releaseDependencies();
        if ( _instance.isInitialized() == false || _pUnit == nullptr )
            return;
        const string_view label = _rigPath.empty() ? string_view{ "(runtime rig)" } : string_view{ _rigPath };
        _listExternal.assign( _instance.getTargetCount(), ExternalTarget{} );
        for ( uint32 targetIndex = 0; targetIndex < _instance.getTargetCount(); ++targetIndex )
        {
            if ( _instance.isTargetExternal( targetIndex ) == false )
                continue;
            const RigTargetDef& def      = _instance.getTargetDef( targetIndex );
            ExternalTarget&     external = _listExternal[targetIndex];
            external._socketLocal        = def._offset;
            if ( def._space.empty() == false )
            {
                external._spaceBone = _pUnit->getSkeleton().findBoneIndex( def._space );
                if ( external._spaceBone < 0 )
                {
                    SW_LOG_ERROR( "Rig '%#': target '%#' has unknown space bone '%#'", label, def._name.c_str(), def._space.c_str() );
                    continue;
                }
            }

            if ( def._kind == RigTargetKind::Object )
            {
                GameObject* pObject = findObjectByName( def._object );
                if ( pObject == nullptr )
                    continue; // 아직 없다 — bindObject 가 다시 묶는다
                external._object = pObject->getHandle();
                external._bBound = SW_TRUE;
                continue;
            }

            // 다른 유닛의 본 · 소켓. 유닛 이름이 없으면 해석된 소켓 표의 다른 유닛 소켓이다.
            SkeletalMeshComponent*      pTargetUnit = nullptr;
            shared_ptr<const SocketSet> sockets;
            hashed_string               boneName = def._bone;
            BoneTransform               socketLocal{};
            bool                        bSocketRoot = false;
            if ( def._unit.empty() == false )
            {
                pTargetUnit = findUnitByName( def._unit, &sockets );
                if ( pTargetUnit != nullptr && def._kind == RigTargetKind::Socket )
                {
                    const SocketDef* pDef = ( sockets != nullptr ) ? sockets->findSocket( def._socket ) : nullptr;
                    if ( pDef == nullptr )
                    {
                        SW_LOG_ERROR( "Rig '%#': target '%#' names socket '%#' but unit '%#' has no such socket (bindUnit with its sockets)", label, def._name.c_str(),
                                      def._socket.c_str(), def._unit.c_str() );
                        continue;
                    }
                    boneName    = pDef->_parent;
                    socketLocal = BoneTransform::makeFromMatrix( pDef->makeLocalTransform() );
                    bSocketRoot = boneName.empty();
                }
            }
            else if ( _pSocketTable != nullptr )
            {
                const SocketId   socketId  = _pSocketTable->resolveTarget( _pSocketTable->findSocket( def._socket ) );
                const SocketDef* pDef      = ( socketId != kInvalidSocketId ) ? _pSocketTable->findSocketDef( socketId ) : nullptr;
                const uint32     unitIndex = ( pDef != nullptr ) ? _pSocketTable->getSocketUnit( socketId ) : MathUtil::kMaxUInt32;
                if ( unitIndex < _listTableUnit.size() && getOwner()->getManager() != nullptr )
                    pTargetUnit = castTo<SkeletalMeshComponent>( getOwner()->getManager()->resolveComponent( _listTableUnit[unitIndex] ) );
                if ( pDef != nullptr )
                {
                    boneName    = pDef->_parent;
                    socketLocal = BoneTransform::makeFromMatrix( pDef->makeLocalTransform() );
                    bSocketRoot = boneName.empty();
                }
            }
            if ( pTargetUnit == nullptr || pTargetUnit == _pUnit )
                continue; // 아직 없다 — bindUnit 이 다시 묶는다
            external._unitBone = bSocketRoot ? -1 : pTargetUnit->getSkeleton().findBoneIndex( boneName );
            if ( bSocketRoot == false && external._unitBone < 0 )
            {
                SW_LOG_ERROR( "Rig '%#': target '%#' names bone '%#' that unit '%#' does not have", label, def._name.c_str(), boneName.c_str(),
                              pTargetUnit->getOwner() != nullptr ? pTargetUnit->getOwner()->getName().c_str() : "" );
                continue;
            }
            // 데이터 오프셋은 소켓 공간에서 먼저 걸린다(행벡터: 오프셋 × 소켓 로컬).
            external._socketLocal = BoneTransform::makeFromMatrix( def._offset.toMatrix() * socketLocal.toMatrix() );
            external._unit        = pTargetUnit->getHandle();
            if ( external._spaceBone >= 0 )
            {
                external._bBound = SW_TRUE; // 프레임 시작에 찍어 자기 본에 얹는다 — 의존을 걸지 않는다
                continue;
            }
            if ( wouldCreateCycle( pTargetUnit ) )
            {
                SW_LOG_ERROR( "Rig '%#': target '%#' on unit '%#' would create an animation dependency cycle - the target is disabled", label, def._name.c_str(),
                              pTargetUnit->getOwner() != nullptr ? pTargetUnit->getOwner()->getName().c_str() : "" );
                continue;
            }
            _pUnit->addAnimationDependency( pTargetUnit );
            _listDependency.push_back( pTargetUnit->getHandle() );
            external._bLive  = SW_TRUE;
            external._bBound = SW_TRUE;
        }
    }

    void PoseModifierComponent::loadSharedColliders()
    {
        vector<RigSpringCollider> listCollider;
        if ( _physicsAssetPath.empty() == false && _pUnit != nullptr )
        {
            PhysicsAsset asset;
            if ( asset.loadFromResource( _physicsAssetPath ) == false )
                SW_LOG_ERROR( "Physics asset '%#' could not be loaded for spring colliders", _physicsAssetPath.c_str() );
            for ( const PhysicsAssetBodyDef& body : asset._listBody )
            {
                const int32 bone = _pUnit->getSkeleton().findBoneIndex( body._bone );
                if ( bone < 0 )
                    continue;
                for ( const PhysicsShapeDesc3D& shape : body._listShape )
                {
                    RigSpringCollider collider{};
                    collider._bone   = static_cast<uint32>( bone );
                    collider._radius = shape._radius;
                    if ( shape._type == PhysicsShapeType3D::Sphere )
                    {
                        collider._shape  = RigSpringColliderShape::Sphere;
                        collider._pointA = shape._localPosition;
                    }
                    else if ( shape._type == PhysicsShapeType3D::Capsule )
                    {
                        const float3 axis = float3::transform( float3{ 0.0f, shape._halfHeight, 0.0f }, quaternion::createFromYawPitchRoll( shape._localRotation ) );
                        collider._shape   = RigSpringColliderShape::Capsule;
                        collider._pointA  = shape._localPosition + axis;
                        collider._pointB  = shape._localPosition - axis;
                    }
                    else
                    {
                        continue; // 상자 · 볼록 · 메시는 스프링 충돌체가 아니다
                    }
                    listCollider.push_back( collider );
                }
            }
        }
        _instance.setSharedColliders( listCollider );
    }

    void PoseModifierComponent::prepareFrame( SkeletalMeshComponent& unit, const AnimationFrameContext& context )
    {
        if ( _bRigEnabled == SW_FALSE )
            return;
        // 리그가 핫 리로드됐으면(내용 번호가 바뀜) 다시 묶는다.
        if ( _asset != nullptr && _asset->getContentId() != _boundContentId )
            bindRig();
        if ( _instance.isInitialized() == false )
            return;

        _pFrameAnimator                        = ( getOwner() != nullptr ) ? getOwner()->getComponent<SkeletalAnimatorComponent>() : nullptr;
        _worldFromModel                        = unit.getWorldMatrix();
        const float4x4          modelFromWorld = _worldFromModel.invert();
        GameObjectManager*      pManager       = ( getOwner() != nullptr ) ? getOwner()->getManager() : nullptr;
        const vector<float4x4>& listOwnModel   = unit.getModelSpaceTransforms(); // 지난 프레임의 최종 포즈
        for ( uint32 targetIndex = 0; targetIndex < static_cast<uint32>( _listExternal.size() ); ++targetIndex )
        {
            ExternalTarget& external = _listExternal[targetIndex];
            if ( external._bBound == SW_FALSE || pManager == nullptr )
                continue;
            float4x4 targetInModel{};
            if ( external._object.isValid() )
            {
                const GameObject*     pObject = pManager->resolveGameObject( external._object );
                const SceneComponent* pScene  = ( pObject != nullptr ) ? pObject->getPrimarySceneComponent() : nullptr;
                if ( pScene == nullptr )
                {
                    _instance.setExternalTarget( targetIndex, RigTargetValue{} );
                    continue;
                }
                targetInModel = external._socketLocal.toMatrix() * pScene->getWorldMatrix() * modelFromWorld;
            }
            else
            {
                const SkeletalMeshComponent* pTargetUnit = castTo<SkeletalMeshComponent>( pManager->resolveComponent( external._unit ) );
                external._pFrameUnit                     = pTargetUnit;
                if ( pTargetUnit == nullptr )
                {
                    _instance.setExternalTarget( targetIndex, RigTargetValue{} );
                    continue;
                }
                external._unitToModel = pTargetUnit->getWorldMatrix() * modelFromWorld;
                if ( external._bLive == SW_TRUE )
                    continue; // 평가 중에 그 유닛의 이번 프레임 포즈를 읽는다
                const vector<float4x4>& listModel = pTargetUnit->getModelSpaceTransforms();
                const float4x4          boneModel = ( 0 <= external._unitBone && static_cast<size_t>( external._unitBone ) < listModel.size() )
                                                      ? listModel[static_cast<size_t>( external._unitBone )]
                                                      : float4x4::Identity;
                targetInModel                     = external._socketLocal.toMatrix() * boneModel * external._unitToModel;
            }

            RigTargetValue value{};
            value._bValid = SW_TRUE;
            PoseModifierComponentInternal::decomposeRigid( targetInModel, value._position, value._rotation );
            if ( 0 <= external._spaceBone && static_cast<size_t>( external._spaceBone ) < listOwnModel.size() )
            {
                // 지난 프레임의 그 본 기준으로 바꿔 두면 평가 때 이번 프레임의 그 본에 다시 얹힌다.
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
        if ( pManager != nullptr && pManager->getAnimationSystem().findLodViewPosition( prepare._viewPosition ) )
            prepare._bHasViewPosition = SW_TRUE;
        _instance.prepare( prepare );
    }

    void PoseModifierComponent::evaluateFrame( SkeletalMeshComponent& unit )
    {
        // 꺼졌어도 유닛은 다른 일(애니메이터)로 돌 수 있다 — 그때 리그는 아무것도 하지 않는다.
        if ( _bRigEnabled == SW_FALSE || _instance.isInitialized() == false )
            return;
        // 의존을 건 유닛은 앞 레벨에서 이번 프레임 포즈를 끝냈다 — 그 본을 읽어 대상 값을 채운다(워커, 그 유닛은 읽기만).
        for ( uint32 targetIndex = 0; targetIndex < static_cast<uint32>( _listExternal.size() ); ++targetIndex )
        {
            const ExternalTarget&        external    = _listExternal[targetIndex];
            const SkeletalMeshComponent* pTargetUnit = external._pFrameUnit;
            if ( external._bLive == SW_FALSE || pTargetUnit == nullptr )
                continue;
            const vector<float4x4>& listModel = pTargetUnit->getModelSpaceTransforms();
            const float4x4          boneModel = ( 0 <= external._unitBone && static_cast<size_t>( external._unitBone ) < listModel.size() ) ? listModel[static_cast<size_t>( external._unitBone )]
                                                                                                                                            : float4x4::Identity;
            RigTargetValue          value{};
            value._bValid = SW_TRUE;
            PoseModifierComponentInternal::decomposeRigid( external._socketLocal.toMatrix() * boneModel * external._unitToModel, value._position, value._rotation );
            _instance.setExternalTarget( targetIndex, value );
        }
        _instance.evaluate( unit.getLocalPose(), unit.getSkeleton().getParentIndices(), _worldFromModel );
        // 포즈 구동의 보정 모프 — 이름이 그리는 메시의 모프 타깃과 같으면 그 가중치에 더한다(GPU 모프 풀이 스키닝 앞에 건다). 기본 포즈 단계가 비운 뒤다.
        for ( const RigMorphWeight& morph : _instance.getMorphWeights() )
        {
            const int32 targetIndex = unit.findMorphTargetIndex( morph._name );
            if ( targetIndex >= 0 )
                unit.addMorphWeight( static_cast<uint32>( targetIndex ), morph._weight );
        }
    }
} // namespace sw
