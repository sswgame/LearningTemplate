/**
 * @file InventoryComponent.h
 * @brief 플레이어 한 명의 가방(`Inventory`)을 조종자 오브젝트에 두는 컴포넌트입니다. 언리얼 Lyra `ULyraInventoryManagerComponent` 자리입니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Encoding/FourCCUtil.h"
#include "Core/Foundation/Types.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Gameplay/GameState/PlayerComponentUtil.h"
#include "GameFramework/Runtime/Gameplay/Inventory/IItemReceiver.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Inventory.h"

namespace sw
{
    class GameObjectManager;
    class ItemCatalog;

    /**
     * @brief 가방을 여는 설정입니다. 카탈로그는 소유하지 않고 참조만 하므로 컴포넌트보다 오래 살아 있어야 합니다(보통 디렉터 · 게임 인스턴스가 보관한다).
     * @details 슬롯 수의 범위는 `PROPERTY( Min )` 이고, 벗어나면 `InventoryComponent::initialize` 가 오류로 거절한다(`ConfigRangeUtil`). 가방이 없는 게임은 컴포넌트를 붙이지 않는다.
     */
    REFLECT()
    struct SW_GF_API InventorySettings
    {
        REFLECT_BODY();
        const ItemCatalog* _pItemCatalog{ nullptr };
        PROPERTY( Min = 1 )
        int32 _slotCount{ 0 }; ///< 가방의 슬롯 수. 기본값 0 은 범위 밖이라 여는 쪽이 반드시 적는다
    };

    /**
     * @class InventoryComponent
     * @brief 플레이어 한 명의 가방입니다. 조종자 오브젝트에 다른 플레이어 상태 컴포넌트와 함께 둡니다. 아이템 받기 계약(`IItemReceiver`)을 포트로 내보냅니다.
     * @details - **가방 없음**: 컴포넌트가 없는 것이다. 가방을 선택으로 빌리는 키트(`Inventory*`)에는 `PlayerComponentUtil::findOnOwner` 의 결과로 넘긴다.
     *          - **찾기 · 세이브**: `PlayerComponentUtil` · `DeferredStateRestore`. 정의한 .cpp 가 `StatefulComponentRegistrar` 로 모든 게임의 스냅샷에 올린다.
     */
    REFLECT( Category = "Framework", DisplayName = "Inventory", Tooltip = "One player's bag, on the controller object (or the director object in a scene without a controller); accepts items from pickups, item boxes and gathering nodes" )
    class SW_GF_API InventoryComponent : public Component
    {
    public:
        REFLECT_BODY();
        REFLECT_INTERFACE( IItemReceiver, _itemReceiverPort );

        static constexpr uint32 kStateTag     = FourCCUtil::make( "INVC" );
        static constexpr uint32 kStateVersion = 1;

        InventoryComponent();
        ~InventoryComponent() override;

        /**
         * @brief 가방을 엽니다. 첫 호출만 설정을 쓰고, 들고 있던 복원 바이트가 있으면 그것으로 복원합니다. 디렉터의 `startGame` 이 부릅니다(`PlayerComponentUtil::openOnOwner`).
         * @return 새 판, 복원, 이미 열림, 거절(설정이 범위 밖 — 오류를 남겼다) 중 하나입니다.
         */
        [[nodiscard]] GameStateInitResult initialize( const InventorySettings& settings );

        /** @brief 씬의 컴포넌트 레지스트리에 오릅니다(`PlayerComponentUtil::findOpen`). */
        void onRegister( GameObjectManager& manager ) override;
        /** @brief 레지스트리에서 내립니다. */
        void onUnregister( GameObjectManager& manager ) override;

        /** @brief 가방 구간 하나를 씁니다. 열리기 전이면 들고 있던 바이트를 그대로 씁니다. `ComponentStateStore::capture` 가 부릅니다. */
        SW_DECLARE_STATE_IO_IN_PLACE();
        /** @brief 열리기 전이면 들고 있다가 `initialize` 에서, 열린 뒤면 바로 적용합니다. `ComponentStateStore::restore` 가 부릅니다. */
        void restoreState( vector<uint8>&& bytes );

        bool isOpen() const { return _restore.isOpen(); }
        /** @brief 새 판으로 열렸으면 true 입니다. 디렉터가 첫 아이템을 넣을지 볼 때 묻습니다. */
        bool             isFreshGame() const { return _restore.isFreshGame(); }
        Inventory&       getInventory() { return _inventory; }
        const Inventory& getInventory() const { return _inventory; }

    private:
        Inventory            _inventory;
        DeferredStateRestore _restore;

        /** @brief 아이템 받기 계약을 가방으로 넘기는 포트입니다(`REFLECT_INTERFACE`). */
        struct ItemReceiverPort final : IItemReceiver
        {
            [[nodiscard]] ItemGrantResult receiveItem( const hashed_string& item, int32 count ) override;
        };
        ItemReceiverPort _itemReceiverPort;
    };
} // namespace sw
