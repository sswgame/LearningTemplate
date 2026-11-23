#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Inventory/PickupComponent.h"

#include "Core/Log/Logger.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectEvents.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Visual/Component/MeshComponent.h"

#include "GameFramework/Runtime/Gameplay/Interaction/InteractableComponent.h"
#include "GameFramework/Runtime/Gameplay/Inventory/IItemReceiver.h"

namespace sw
{
    namespace
    {
        struct PickupComponentInternal
        {
            static constexpr const utf8* kMeshID    = "Cube"; ///< 떨어진 조각의 모습(엔진 기본 메시)
            static constexpr float32     kMeshScale = 0.3f;   ///< 상자 한 변(m)
        };
    } // namespace

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
        InteractableComponent* pInteractable = getOwner() != nullptr ? getOwner()->getComponent<InteractableComponent>() : nullptr;
        if ( pInteractable != nullptr )
        {
            _usedSubscription = pInteractable->getCompletedDelegate().add( [this]( const GameObject& user )
            { (void)giveTo( user ); } ); // 못 주면 giveTo 가 경고를 남기고 그대로 둔다
        }
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

    GameObject* PickupComponent::spawn( GameObjectManager& manager, const InventorySlot& item, const float3& position )
    {
        if ( item.isEmpty() )
            return nullptr;
        using Internal      = PickupComponentInternal;
        GameObject* pObject = manager.createGameObject( hashed_string( kSpawnedName ) );
        if ( pObject == nullptr )
            return nullptr;
        MeshComponent* pMesh = pObject->addComponent<MeshComponent>();
        if ( pMesh != nullptr )
        {
            pMesh->setMeshID( Internal::kMeshID );
            pMesh->setLocalPosition( position );
            pMesh->setLocalScale( float3{ Internal::kMeshScale, Internal::kMeshScale, Internal::kMeshScale } );
        }
        PickupComponent* pItem = pObject->addComponent<PickupComponent>();
        if ( pItem != nullptr )
        {
            pItem->_item   = item;
            pItem->_itemID = item._itemID; // 씬 저장 · 불러오기는 id · 수를 싣는다(개체 상태는 이 실행 동안만)
            pItem->_count  = item._count;
        }
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
