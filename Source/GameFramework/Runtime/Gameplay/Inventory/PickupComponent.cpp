#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Inventory/PickupComponent.h"

#include "Core/Encoding/FourCCUtil.h"
#include "Core/Log/Logger.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectEvents.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabCache.h"

#include "GameFramework/Runtime/Foundation/Save/StatefulComponentRegistrar.h"
#include "GameFramework/Runtime/Gameplay/Interaction/InteractableComponent.h"
#include "GameFramework/Runtime/Gameplay/Inventory/IItemReceiver.h"

namespace sw
{
    namespace
    {
        struct PickupComponentInternal
        {
            static constexpr uint32 kStateTag     = FourCCUtil::make( "PICK" );
            static constexpr uint32 kStateVersion = 1;
        };

        const StatefulComponentRegistrar<PickupComponent> s_stateRegistration{ "PickupComponent" };
    } // namespace
} // namespace sw

namespace sw
{
    PickupComponent::PickupComponent()
        : Component{}
        , _item{}
        , _itemID{}
        , _count{ 1 }
        , _bPickUpOnTouch{ false }
    {
    }

    PickupComponent::~PickupComponent() = default;

    void PickupComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        if ( _item.isEmpty() && _itemID.empty() == false )
        {
            _item._itemID = _itemID;
            _item._count  = _count;
        }
        _usedSubscription = InteractableComponent::listenCompleted<&PickupComponent::handleUsed>( *this ); // 닿아서만 줍는 것은 상호작용이 없다
    }

    void PickupComponent::handleUsed( const GameObject& user )
    {
        (void)giveTo( user ); // 못 주면 giveTo 가 경고를 남기고 그대로 둔다
    }

    void PickupComponent::setItem( const InventorySlot& item )
    {
        _item   = item;
        _itemID = item._itemID; // 씬 데이터는 id · 수를, 개체 상태까지는 상태 코덱이 싣는다(`serializeState`)
        _count  = item._count;
    }

    void PickupComponent::serializeState( StateIO& io )
    {
        if ( io.isWriting() && _item.isEmpty() )
            return; // 시작 전 — 씬 데이터가 곧 상태다
        io.header( PickupComponentInternal::kStateTag, PickupComponentInternal::kStateVersion );
        InventorySlot item = _item;
        Inventory::serializeSlot( io, item );
        if ( io.isWriting() )
            return;
        io.end();
        if ( io.isOk() )
            setItem( item );
    }

    void PickupComponent::restoreState( vector<uint8>&& bytes )
    {
        Archive archive( bytes.data(), bytes.size() );
        if ( readState( archive ) == false )
            SW_LOG_WARNING_ID( "Pickup.SavedItemUnreadable", "The saved item of '%#' did not read - it keeps %# x%# from the scene data", getOwner() != nullptr ? getOwner()->getName().c_str() : "?", _itemID.c_str(), _count );
    }

    void PickupComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        _overlapSubscription = GameObjectEvents::listen<&PickupComponent::handleOverlapBegin>( *this, &GameObjectEvents::_overlapBegin );
    }

    void PickupComponent::handleOverlapBegin( const OverlapInfo& overlap )
    {
        // 받을 가방이 없는 몸(상자 · 탄)은 조용히 지나간다 — `ItemBoxComponent` 와 같은 거름.
        const bool bBody = _bPickUpOnTouch && overlap._pOther != nullptr && overlap._bOtherTrigger == SW_FALSE;
        if ( bBody && IItemReceiver::findForTarget( *overlap._pOther ) != nullptr )
            (void)giveTo( *overlap._pOther );
    }

    bool PickupComponent::giveTo( const GameObject& target )
    {
        if ( _item.isEmpty() )
            return false;
        IItemReceiver* const pReceiver = IItemReceiver::findForTarget( target );
        if ( pReceiver == nullptr || pReceiver->receiveStack( _item ) != ItemGrantResult::Granted )
        {
            SW_LOG_WARNING_ID( "Pickup.NotGranted", "'%#' could not pick up %# x%# (%#) - it stays", target.getName().c_str(), _item._itemID.c_str(), _item._count,
                               pReceiver == nullptr ? "no bag on it or its controller" : "its bag is full" );
            return false;
        }
        consume();
        return true;
    }

    void PickupComponent::consume()
    {
        SW_LOG_INFO_ID( "Pickup.PickedUp", "Picked up %# x%#", _item._itemID.c_str(), _item._count );
        _item                       = InventorySlot{};
        GameObjectManager* pManager = getOwner() != nullptr ? getOwner()->getMutableManager() : nullptr;
        if ( pManager != nullptr )
            pManager->destroyGameObject( getOwner() );
    }

    GameObject* PickupComponent::spawn( GameObjectManager& manager, string_view prefabPath, const InventorySlot& item, const float3& position )
    {
        if ( item.isEmpty() )
            return nullptr;
        AssetManager* const pAssets = manager.findAssetManager();
        GameObject* const   pObject = pAssets != nullptr && prefabPath.empty() == false ? pAssets->getAssetCache<PrefabCache>().spawn( &manager, prefabPath, kSpawnedName ) : nullptr;
        PickupComponent*    pPickup = pObject != nullptr ? pObject->getComponent<PickupComponent>() : nullptr;
        SceneComponent*     pRoot   = pObject != nullptr ? pObject->getPrimarySceneComponent() : nullptr;
        if ( pPickup == nullptr || pRoot == nullptr )
        {
            SW_LOG_ERROR_ID( "Pickup.SpawnFailed", "Could not drop %# x%#: the pickup prefab '%#' did not spawn or has no PickupComponent / transform", item._itemID.c_str(), item._count, prefabPath );
            if ( pObject != nullptr )
                manager.destroyGameObject( pObject );
            return nullptr;
        }
        pRoot->setWorldPosition( position );
        pPickup->setItem( item );
        return pObject;
    }

    bool PickupComponent::pickUp( Inventory& bag )
    {
        if ( _item.isEmpty() || bag.addStack( _item ) == false )
            return false;
        consume();
        return true;
    }
} // namespace sw
