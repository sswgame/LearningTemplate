#include "pch.h"

#include "GameFramework/Appearance/CharacterAppearanceComponent.h"

#include "Engine/Character/CharacterPoseUtil.h"
#include "Engine/Character/SocketBindingComponent.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/3D/SkeletalMeshComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Resource/AssetManager.h"

#include "GameFramework/Appearance/AppearanceDatabase.h"
#include "GameFramework/Appearance/CharacterAppearance.h"
#include "GameFramework/Framework/GameService.h"

namespace sw
{
    SW_LOG_CALLER( "CharacterAppearance" );

    namespace
    {
        struct CharacterAppearanceComponentInternal
        {
            static bool isSamePart( const ResolvedPart& part, const hashed_string& owner, const hashed_string& partName, const hashed_string& asset )
            {
                return part._owner == owner && part._partName == partName && part._asset == asset;
            }

            /** @brief 오브젝트의 메시 컴포넌트마다 @p func 를 부릅니다. */
            template <typename Func>
            static void forEachMesh( const GameObject& object, Func&& func )
            {
                object.forEachComponentOfType<MeshComponent>( [&func]( MeshComponent* pMesh )
                { func( *pMesh ); } );
            }

            /** @brief 메시에 머티리얼 값을 겁니다 — 메시 몫 인스턴스(없으면 메시의 머티리얼에서 새로)에 넣습니다. */
            static void applyParameter( MeshComponent& mesh, const ResolvedMaterialValue& value )
            {
                shared_ptr<MaterialInstance> instance = mesh.getMaterialInstance();
                if ( instance == nullptr )
                {
                    if ( mesh.getMaterial() == nullptr )
                        return;
                    instance = MaterialInstance::create( mesh.getMaterial() );
                    if ( instance == nullptr )
                        return;
                    mesh.setMaterialInstance( instance );
                }
                instance->setVectorParameter( value._name, value._value );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    CharacterAppearanceSocketTask::CharacterAppearanceSocketTask( CharacterAppearanceComponent& owner )
        : _owner{ owner }
    {
    }

    void CharacterAppearanceSocketTask::runAnimationPhase( AnimationPhase phase, SkeletalMeshComponent& unit, const AnimationFrameContext& context )
    {
        (void)phase;
        (void)unit;
        (void)context;
    }

    void CharacterAppearanceSocketTask::finishAnimationFrame( SkeletalMeshComponent& unit )
    {
        _owner.refreshSocketTransforms( unit );
    }

    void CharacterAppearanceSocketTask::onAnimationUnitDetached( SkeletalMeshComponent& unit )
    {
        (void)unit;
        _owner._taskUnit = ComponentHandle{};
    }
} // namespace sw

namespace sw
{
    CharacterAppearanceComponent::CharacterAppearanceComponent()
        : _presetId{}
        , _seed{ 0 }
        , _socketTask{ *this }
        , _rig{}
        , _socketCache{}
        , _resolved{}
        , _bodyBindBones{}
        , _bodyBones{}
        , _listSpawnedPart{}
        , _listSlotOverride{}
        , _taskUnit{}
        , _assembleCount{ 0 }
        , _bodyPart{ -1 }
        , _bPartsVisible{ true }
        , _bAssembleScheduled{ SW_FALSE }
        , _bStarted{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    CharacterAppearanceComponent::~CharacterAppearanceComponent() = default;

    void CharacterAppearanceComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        _bStarted = SW_TRUE;
        if ( _presetId.empty() == false )
            requestAssemble();
    }

    void CharacterAppearanceComponent::onEndPlay()
    {
        unregisterSocketTask();
        despawnParts();
        _bStarted = SW_FALSE;
        Component::onEndPlay();
    }

    void CharacterAppearanceComponent::onPropertyChanged( hashed_string propertyName )
    {
        Component::onPropertyChanged( propertyName );
        if ( _bStarted == SW_TRUE && ( propertyName == hashed_string( "_presetId" ) || propertyName == hashed_string( "_seed" ) ) )
            requestAssemble();
    }

    void CharacterAppearanceComponent::setPreset( const hashed_string& presetId, uint32 seed )
    {
        _presetId = string{ presetId.c_str() };
        _seed     = static_cast<int32>( seed );
        if ( _bStarted == SW_TRUE )
            requestAssemble();
    }

    void CharacterAppearanceComponent::setSlotItem( const hashed_string& slot, const hashed_string& itemId )
    {
        for ( SlotOverride& slotOverride : _listSlotOverride )
        {
            if ( slotOverride._slot != slot )
                continue;
            if ( slotOverride._itemId == itemId )
                return;
            slotOverride._itemId = itemId;
            if ( _bStarted == SW_TRUE )
                requestAssemble();
            return;
        }
        _listSlotOverride.push_back( SlotOverride{ slot, itemId } );
        if ( _bStarted == SW_TRUE )
            requestAssemble();
    }

    void CharacterAppearanceComponent::setPartsVisible( bool bVisible )
    {
        if ( _bPartsVisible == bVisible )
            return;
        _bPartsVisible              = bVisible;
        GameObjectManager* pManager = findManager();
        if ( pManager == nullptr )
            return;
        // 메시 보임은 렌더 상태다 — 틱 안이면 틱 뒤에 건다.
        const ComponentHandle self = getHandle();
        pManager->executeOrDeferPostTick( [pManager, self]()
        {
            CharacterAppearanceComponent* pAppearance = static_cast<CharacterAppearanceComponent*>( pManager->resolveComponent( self ) );
            if ( pAppearance != nullptr )
                pAppearance->applyVisibility( *pManager );
        } );
    }

    void CharacterAppearanceComponent::despawnParts()
    {
        GameObjectManager* pManager = findManager();
        if ( pManager != nullptr )
        {
            for ( const SpawnedPart& part : _listSpawnedPart )
            {
                GameObject* pObject = pManager->resolveGameObject( part._object );
                if ( pObject != nullptr )
                    pManager->destroyObject( pObject );
            }
        }
        _listSpawnedPart.clear();
    }

    bool CharacterAppearanceComponent::findSocketWorldTransform( const hashed_string& fullName, float4x4& outWorldTransform ) const
    {
        GameObjectManager* pManager = findManager();
        if ( pManager == nullptr || _assembleCount == 0 )
            return false;
        // 유닛 번호 순서의 월드 — 몸은 이 오브젝트, 부품은 그 부품 오브젝트(지난 프레임에 적용된 자리).
        vector<float4x4> listUnitWorld;
        listUnitWorld.reserve( _rig.getUnitCount() );
        for ( uint32 unitIndex = 0; unitIndex < _rig.getUnitCount(); ++unitIndex )
        {
            const GameObject*     pObject = findUnitObject( *pManager, unitIndex );
            const SceneComponent* pScene  = pObject != nullptr ? pObject->getPrimarySceneComponent() : nullptr;
            if ( pScene == nullptr )
                break;
            listUnitWorld.push_back( pScene->getWorldMatrix() );
        }
        return _rig.findSocketWorldTransform( fullName, _bodyBones, vector_reference<const float4x4>( listUnitWorld ), outWorldTransform );
    }

    bool CharacterAppearanceComponent::findBindSocketTransform( const hashed_string& fullName, float4x4& outUnitTransform ) const
    {
        const float4x4 arrIdentity[1] = { float4x4::Identity };
        return _rig.findSocketWorldTransform( fullName, _bodyBindBones, vector_reference<const float4x4>( arrIdentity ), outUnitTransform );
    }

    void CharacterAppearanceComponent::collectPartObjects( vector<GameObjectHandle>& outListObject ) const
    {
        outListObject.clear();
        for ( const SpawnedPart& part : _listSpawnedPart )
        {
            outListObject.push_back( part._object );
        }
    }

    // ------------------------------------------------------------------------------
    // 조립(게임 스레드, 틱 밖)
    // ------------------------------------------------------------------------------
    void CharacterAppearanceComponent::requestAssemble()
    {
        GameObjectManager* pManager = findManager();
        if ( pManager == nullptr || _bAssembleScheduled == SW_TRUE )
            return;
        _bAssembleScheduled        = SW_TRUE;
        const ComponentHandle self = getHandle();
        pManager->executeOrDeferPostTick( [pManager, self]()
        {
            CharacterAppearanceComponent* pAppearance = static_cast<CharacterAppearanceComponent*>( pManager->resolveComponent( self ) );
            if ( pAppearance == nullptr )
                return;
            pAppearance->_bAssembleScheduled = SW_FALSE;
            pAppearance->assemble();
        } );
    }

    void CharacterAppearanceComponent::assemble()
    {
        GameObject*            pOwner    = getOwner();
        GameObjectManager*     pManager  = findManager();
        SkeletalMeshComponent* pBody     = pOwner != nullptr ? pOwner->getComponent<SkeletalMeshComponent>() : nullptr;
        AppearanceDatabase*    pDatabase = game::getService<AppearanceDatabase>();
        if ( pManager == nullptr || pBody == nullptr )
        {
            SW_LOG_WARNING( "'%#' has no skeletal mesh to wear appearance '%#'", pOwner != nullptr ? pOwner->getName().c_str() : "?", _presetId.c_str() );
            return;
        }
        if ( pDatabase == nullptr )
        {
            SW_LOG_ERROR( "Appearance '%#' cannot be assembled - no AppearanceDatabase game service", _presetId.c_str() );
            return;
        }

        CharacterAppearanceSpec spec;
        if ( pDatabase->getPresets().expand( hashed_string( _presetId ), static_cast<uint32>( _seed ), pDatabase->getSlotTable(), pDatabase->getSets(),
                                             pDatabase->getSchemas(), spec ) == false )
        {
            SW_LOG_ERROR( "Appearance preset '%#' could not be expanded", _presetId.c_str() );
            return;
        }
        for ( const SlotOverride& slotOverride : _listSlotOverride )
        {
            AppearanceSlotRequest* pSlot = spec.findSlot( slotOverride._slot );
            if ( pSlot == nullptr )
            {
                spec._listSlot.push_back( AppearanceSlotRequest{} );
                pSlot        = &spec._listSlot.back();
                pSlot->_slot = slotOverride._slot;
            }
            pSlot->_itemId        = slotOverride._itemId;
            pSlot->_visibleVisual = hashed_string{};
            pSlot->_state         = hashed_string{};
        }
        AppearanceResolver::resolve( *pDatabase, spec, _resolved );

        _bodyPart = applyBodyPart( *pBody );
        CharacterPoseUtil::makeBindBones( pBody->getSkeleton(), _bodyBindBones );
        _bodyBones = _bodyBindBones;
        (void)CharacterPoseUtil::copyUnitPose( *pBody, _bodyBones );
        string socketError;
        if ( _rig.rebuild( _resolved, _bodyBindBones, _socketCache, &socketError ) == false )
            SW_LOG_ERROR( "Appearance '%#' sockets: %#", _presetId.c_str(), socketError.c_str() );

        syncParts( *pManager, *pBody, _bodyPart );
        bindSocketParts( *pManager );
        applyMaterialValues( *pManager, *pBody );
        applyVisibility( *pManager );
        registerSocketTask( *pBody );
        ++_assembleCount;
    }

    int32 CharacterAppearanceComponent::applyBodyPart( SkeletalMeshComponent& body )
    {
        for ( size_t partIndex = 0; partIndex < _resolved._listPart.size(); ++partIndex )
        {
            const ResolvedPart& part = _resolved._listPart[partIndex];
            if ( part._owner.empty() == false || part._kind != AppearancePartKind::Skinned )
                continue;
            // 스켈레톤 먼저 — 메시의 스킨 사본이 그 본 수를 본다.
            if ( string_view( body.getSkeletonPath() ) != string_view( part._skeleton.c_str() ) )
                body.setSkeletonPath( part._skeleton.c_str() );
            if ( string_view( body.getMeshId() ) != string_view( part._asset.c_str() ) )
                body.setMeshId( part._asset.c_str() );
            if ( part._material.empty() == false && body.getMaterialPath() != part._material )
                body.setMaterialPath( part._material.c_str() );
            return static_cast<int32>( partIndex );
        }
        SW_LOG_WARNING( "Appearance '%#' has no body part (a Skinned part without an owner)", _presetId.c_str() );
        return -1;
    }

    void CharacterAppearanceComponent::syncParts( GameObjectManager& manager, SkeletalMeshComponent& body, int32 bodyPart )
    {
        using Internal = CharacterAppearanceComponentInternal;
        // 남길 것을 고르고 없어진 것은 지운다 — 같은 부품(주인 · 이름 · 에셋)은 그 오브젝트를 그대로 쓴다(무기를 바꿔도 투구는 그대로).
        vector<SpawnedPart> listKeep;
        vector<uint8>       listPartCovered( _resolved._listPart.size(), SW_FALSE );
        for ( const SpawnedPart& spawned : _listSpawnedPart )
        {
            int32 matched = -1;
            for ( size_t partIndex = 0; partIndex < _resolved._listPart.size(); ++partIndex )
            {
                const bool bCandidate = listPartCovered[partIndex] == SW_FALSE && static_cast<int32>( partIndex ) != bodyPart;
                if ( bCandidate && Internal::isSamePart( _resolved._listPart[partIndex], spawned._owner, spawned._partName, spawned._asset ) )
                {
                    matched = static_cast<int32>( partIndex );
                    break;
                }
            }
            GameObject* pObject = manager.resolveGameObject( spawned._object );
            if ( matched < 0 || pObject == nullptr )
            {
                if ( pObject != nullptr )
                    manager.destroyObject( pObject );
                continue;
            }
            listPartCovered[static_cast<size_t>( matched )] = SW_TRUE;
            SpawnedPart kept                                = spawned;
            kept._partIndex                                 = static_cast<uint32>( matched );
            listKeep.push_back( kept );
        }
        _listSpawnedPart = std::move( listKeep );

        AssetManager*         pAssetManager = game::getService<AssetManager>();
        SceneComponent*       pBodyScene    = body.getOwner() != nullptr ? body.getOwner()->getPrimarySceneComponent() : nullptr;
        const ComponentHandle bodyHandle    = body.getHandle();
        for ( size_t partIndex = 0; partIndex < _resolved._listPart.size(); ++partIndex )
        {
            const ResolvedPart& part = _resolved._listPart[partIndex];
            if ( listPartCovered[partIndex] == SW_TRUE || static_cast<int32>( partIndex ) == bodyPart || part._asset.empty() )
                continue;
            GameObject* pObject = nullptr;
            SpawnedPart spawned;
            spawned._owner     = part._owner;
            spawned._partName  = part._partName;
            spawned._asset     = part._asset;
            spawned._partIndex = static_cast<uint32>( partIndex );
            if ( part._kind == AppearancePartKind::SocketPrefab )
            {
                pObject = pAssetManager != nullptr ? pAssetManager->getPrefabCache().spawn( &manager, part._asset.c_str(), part._partName.c_str() ) : nullptr;
                if ( pObject != nullptr && pObject->getComponent<SocketBindingComponent>() == nullptr )
                    (void)pObject->addComponent<SocketBindingComponent>();
            }
            else if ( part._kind == AppearancePartKind::Skinned )
            {
                // 몸을 리더로 따르는 스킨드 부품(옷 · 머리카락) — 몸의 자식, 자리는 몸과 같다.
                pObject                         = manager.createGameObject( part._partName );
                SkeletalMeshComponent* pSkinned = pObject->addComponent<SkeletalMeshComponent>();
                pSkinned->setSkeletonPath( part._skeleton.empty() ? string_view( body.getSkeletonPath() ) : string_view( part._skeleton.c_str() ) );
                pSkinned->setMeshId( part._asset.c_str() );
                if ( part._material.empty() == false )
                    pSkinned->setMaterialPath( part._material.c_str() );
                pSkinned->setLeaderPose( static_cast<SkeletalMeshComponent*>( manager.resolveComponent( bodyHandle ) ) );
                if ( pBodyScene != nullptr )
                    (void)pSkinned->attachToComponent( pBodyScene, AttachRule::KeepRelative );
                spawned._bSocketPart = SW_FALSE;
            }
            if ( pObject == nullptr )
            {
                SW_LOG_WARNING( "Appearance '%#' part '%#' (%#) could not be spawned", _presetId.c_str(), part._partName.c_str(), part._asset.c_str() );
                continue;
            }
            spawned._object = pObject->getHandle();
            _listSpawnedPart.push_back( spawned );
        }
    }

    void CharacterAppearanceComponent::bindSocketParts( GameObjectManager& manager )
    {
        for ( SpawnedPart& spawned : _listSpawnedPart )
        {
            if ( spawned._bSocketPart == SW_FALSE )
                continue;
            GameObject*             pObject  = manager.resolveGameObject( spawned._object );
            SocketBindingComponent* pBinding = pObject != nullptr ? pObject->getComponent<SocketBindingComponent>() : nullptr;
            const ResolvedPart&     part     = _resolved._listPart[spawned._partIndex];
            float4x4                inUnit;
            uint32                  unitIndex = AppearanceSocketRig::kNoUnit;
            if ( pBinding == nullptr || _rig.computePlacement( part._placement, _bodyBones, inUnit, unitIndex ) == false )
            {
                SW_LOG_WARNING( "Appearance '%#' part '%#' has no active socket among its candidates", _presetId.c_str(), part._partName.c_str() );
                continue;
            }
            GameObject* pHolder = findUnitObject( manager, unitIndex );
            if ( pHolder == nullptr || pHolder == pObject )
                continue;
            spawned._holderUnit            = unitIndex;
            const hashed_string socketName = part._placement._listSocket.empty() ? hashed_string{} : part._placement._listSocket[0];
            // 같은 holder · 소켓이면 자리만 고친다(다시 붙이면 되돌아가기 블렌드가 끊긴다).
            if ( pBinding->getState() == SocketBindingState::Bound && pBinding->getHolder() == pHolder->getHandle() && pBinding->getSocketName() == socketName )
                pBinding->updateSocketTransform( inUnit );
            else
                (void)pBinding->bindToSocket( pHolder, socketName, inUnit );
        }
    }

    void CharacterAppearanceComponent::applyMaterialValues( GameObjectManager& manager, SkeletalMeshComponent& body )
    {
        using Internal = CharacterAppearanceComponentInternal;
        for ( const ResolvedMaterialValue& value : _resolved._listMaterialValue )
        {
            if ( value._target != ResolvedMaterialTarget::Parameter )
                continue; // 염색 마스크 채널 · 팔레트 칸은 그것을 읽는 머티리얼이 생기면 건다
            if ( value._owner.empty() && ( value._part.empty() || ( _bodyPart >= 0 && _resolved._listPart[static_cast<size_t>( _bodyPart )]._partName == value._part ) ) )
                Internal::applyParameter( body, value );
            for ( const SpawnedPart& spawned : _listSpawnedPart )
            {
                const bool  bOwned  = spawned._owner == value._owner && ( value._part.empty() || spawned._partName == value._part );
                GameObject* pObject = bOwned ? manager.resolveGameObject( spawned._object ) : nullptr;
                if ( pObject != nullptr )
                    Internal::forEachMesh( *pObject, [&value]( MeshComponent& mesh )
                    { Internal::applyParameter( mesh, value ); } );
            }
        }
    }

    void CharacterAppearanceComponent::applyVisibility( GameObjectManager& manager )
    {
        using Internal     = CharacterAppearanceComponentInternal;
        GameObject* pOwner = getOwner();
        if ( pOwner != nullptr )
            Internal::forEachMesh( *pOwner, [this]( MeshComponent& mesh )
            { mesh.setVisible( _bPartsVisible ); } );
        for ( const SpawnedPart& spawned : _listSpawnedPart )
        {
            GameObject* pObject = manager.resolveGameObject( spawned._object );
            if ( pObject != nullptr )
                Internal::forEachMesh( *pObject, [this]( MeshComponent& mesh )
                { mesh.setVisible( _bPartsVisible ); } );
        }
    }

    void CharacterAppearanceComponent::refreshSocketTransforms( const SkeletalMeshComponent& body )
    {
        GameObjectManager* pManager = findManager();
        if ( pManager == nullptr || CharacterPoseUtil::copyUnitPose( body, _bodyBones ) == false )
            return;
        for ( const SpawnedPart& spawned : _listSpawnedPart )
        {
            if ( spawned._bSocketPart == SW_FALSE || spawned._holderUnit != AppearanceSocketRig::kBodyUnit )
                continue;
            GameObject*             pObject  = pManager->resolveGameObject( spawned._object );
            SocketBindingComponent* pBinding = pObject != nullptr ? pObject->getComponent<SocketBindingComponent>() : nullptr;
            float4x4                inUnit;
            uint32                  unitIndex = AppearanceSocketRig::kNoUnit;
            if ( pBinding != nullptr && _rig.computePlacement( _resolved._listPart[spawned._partIndex]._placement, _bodyBones, inUnit, unitIndex ) )
                pBinding->updateSocketTransform( inUnit );
        }
    }

    void CharacterAppearanceComponent::registerSocketTask( SkeletalMeshComponent& body )
    {
        if ( _taskUnit == body.getHandle() )
            return;
        unregisterSocketTask();
        body.addAnimationPhaseTask( &_socketTask );
        _taskUnit = body.getHandle();
    }

    void CharacterAppearanceComponent::unregisterSocketTask()
    {
        GameObjectManager*     pManager = findManager();
        SkeletalMeshComponent* pUnit    = pManager != nullptr ? static_cast<SkeletalMeshComponent*>( pManager->resolveComponent( _taskUnit ) ) : nullptr;
        _taskUnit                       = ComponentHandle{};
        if ( pUnit != nullptr )
            pUnit->removeAnimationPhaseTask( &_socketTask );
    }

    GameObject* CharacterAppearanceComponent::findUnitObject( GameObjectManager& manager, uint32 unitIndex ) const
    {
        if ( unitIndex == AppearanceSocketRig::kBodyUnit )
            return getOwner();
        const uint32 partIndex = _rig.getUnitPart( unitIndex );
        for ( const SpawnedPart& spawned : _listSpawnedPart )
        {
            if ( spawned._partIndex == partIndex )
                return manager.resolveGameObject( spawned._object );
        }
        return nullptr;
    }

    GameObjectManager* CharacterAppearanceComponent::findManager() const
    {
        GameObject* pOwner = getOwner();
        return pOwner != nullptr ? pOwner->getManager() : nullptr;
    }
} // namespace sw
