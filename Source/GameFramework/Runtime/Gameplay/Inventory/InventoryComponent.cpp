#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Inventory/InventoryComponent.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Automation/Watch/AutomationWatchComponent.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"

#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/Framework/Save/ComponentStateStore.h"
#include "GameFramework/Runtime/Foundation/GameData/ConfigRangeUtil.h"
#include "GameFramework/Runtime/Gameplay/Inventory/ItemCatalog.h"

namespace sw
{
    namespace
    {
        /** @brief 가방을 모든 게임의 상태 스냅샷에 싣습니다(게임마다 올리지 않는다 — 형제 `PlayerStateComponent` 와 같다). */
        const StatefulComponentRegistrar<InventoryComponent> s_inventoryStateRegistration{ "InventoryComponent" };

        /** @brief 감시 값 `Inventory.ItemCount` — 가방(target = 조종자 오브젝트)에 든 아이템 key 의 수입니다. */
        [[nodiscard]] bool readInventoryItemCount( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
        {
            const InventoryComponent* pBag = query.findTargetComponent<InventoryComponent>( "inventory", outValue );
            if ( pBag == nullptr )
                return false;
            outValue.setNumber( static_cast<float64>( pBag->getInventory().getItemCount( hashed_string( query._key ) ) ) );
            return true;
        }
    } // namespace

    InventoryComponent::InventoryComponent()
        : _inventory{}
        , _restore{}
        , _itemReceiverPort{}
        , _openSlotCount{ 0 }
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

    void InventoryComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        if ( _openSlotCount <= 0 )
            return;
        InventorySettings settings;
        settings._pItemCatalog = game::findSubsystem<const ItemCatalog>();
        settings._slotCount    = _openSlotCount;
        if ( settings._pItemCatalog == nullptr )
        {
            SW_LOG_ERROR_ID( "InventoryComponent.NoItemCatalog", "InventoryComponent: cannot open %# slot(s) at begin play - gamesettings must declare <RequiredData kind=\"items\">", _openSlotCount );
            return;
        }
        (void)initialize( settings ); // 거절되면 initialize 가 까닭을 남긴다
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

    ItemGrantResult InventoryComponent::ItemReceiverPort::receiveStack( const InventorySlot& stack )
    {
        Inventory& bag = getPortOwner<InventoryComponent>( *this, offsetof( InventoryComponent, _itemReceiverPort ) )._inventory;
        return bag.addStack( stack ) ? ItemGrantResult::Granted : ItemGrantResult::NoRoom;
    }
} // namespace sw

namespace sw
{
    SW_AUTOMATION_QUERY_WATCH( inventoryItemCount, "Inventory.ItemCount", "Count of the item (key) in the bag of the InventoryComponent on target", &readInventoryItemCount, true, true );
} // namespace sw
