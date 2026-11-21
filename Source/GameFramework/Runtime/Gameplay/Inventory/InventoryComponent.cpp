#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Inventory/InventoryComponent.h"

#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"

#include "GameFramework/Runtime/Foundation/Framework/Save/ComponentStateStore.h"
#include "GameFramework/Runtime/Foundation/GameData/ConfigRangeUtil.h"

namespace sw
{
    namespace
    {
        /** @brief 가방을 모든 게임의 상태 스냅샷에 싣습니다(게임마다 올리지 않는다 — 형제 `PlayerStateComponent` 와 같다). */
        const StatefulComponentRegistrar<InventoryComponent> s_inventoryStateRegistration{ "InventoryComponent" };
    } // namespace

    InventoryComponent::InventoryComponent()
        : _inventory{}
        , _restore{}
        , _itemReceiverPort{}
    {
    }

    InventoryComponent::~InventoryComponent() = default;

    GameStateInitResult InventoryComponent::initialize( const InventorySettings& settings )
    {
        if ( _restore.isOpen() )
            return GameStateInitResult::AlreadyInitialized; // 앞 디렉터가 연 가방을 다시 비우지 않는다
        const string rangeError = ConfigRangeUtil::describeRangeErrors( settings );
        if ( rangeError.empty() == false )
        {
            SW_LOG_ERROR_ID( "InventoryComponent.RefusedOpenSettings", "InventoryComponent: refused to open - settings '%#'", rangeError.c_str() );
            return GameStateInitResult::Refused;
        }
        if ( _inventory.initialize( settings._pItemCatalog, settings._slotCount ) == false )
            return GameStateInitResult::Refused;
        return _restore.open( *this );
    }

    void InventoryComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        manager.getComponentRegistry().add<InventoryComponent>( this );
    }

    void InventoryComponent::onUnregister( GameObjectManager& manager )
    {
        manager.getComponentRegistry().remove<InventoryComponent>( this );
        Component::onUnregister( manager );
    }

    void InventoryComponent::serializeState( StateIO& io )
    {
        _restore.serialize( io, kStateTag, kStateVersion, _inventory, "the inventory" );
    }

    void InventoryComponent::restoreState( vector<uint8>&& bytes )
    {
        _restore.receive( *this, std::move( bytes ) );
    }

    ItemGrantResult InventoryComponent::ItemReceiverPort::receiveItem( const hashed_string& item, int32 count )
    {
        Inventory& bag = getPortOwner<InventoryComponent>( *this, offsetof( InventoryComponent, _itemReceiverPort ) )._inventory;
        if ( bag.hasRoomFor( item, count ) == false )
            return ItemGrantResult::NoRoom; // 열리기 전(슬롯 없음)도 자리가 없다
        (void)bag.addItem( item, count );   // 자리를 먼저 봤으므로 모두 들어간다
        return ItemGrantResult::Granted;
    }
} // namespace sw
