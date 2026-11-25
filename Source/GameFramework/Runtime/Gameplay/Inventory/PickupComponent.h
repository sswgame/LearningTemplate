/**
 * @file PickupComponent.h
 * @brief 월드에 놓인 아이템(떨어뜨린 것 · 씬에 둔 것)입니다. 캐릭터 가방이 주워 가면 오브젝트가 사라지고, 가방에 자리가 없으면 그대로 남습니다.
 */
#pragma once
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/String/Name/hashed_string.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Inventory.h"

namespace sw
{
    class GameObject;
    class GameObjectManager;

    /**
     * @class PickupComponent
     * @brief 아이템 조각 하나를 월드에 둡니다(언리얼 Lyra 의 `ALyraWorldCollectable` · 유니티 픽업 프리팹 자리).
     * @details 씬 데이터는 `_itemID` · `_count` 를 적고, 버리기(`InventoryCommandKind::Drop`)로 생긴 것은 `spawn` 이 꺼낸 조각(내구도 · 꾸미기째)을 그대로 든다.
     *          주인은 같은 오브젝트의 상호작용(`InteractableComponent`)을 끝낸 쪽, `_bPickUpOnTouch` 면 닿은 쪽이다 — 그 가방(`IItemReceiver`)에 조각째 넣는다(`giveTo`).
     *          줍기(`giveTo` · `pickUp`)는 전부 들어갈 때만 한다(겹치는 것은 칸 · 무게, 개체는 빈 칸) — 반만 주워 바닥에 남은 수가 갈라지지 않는다.
     */
    REFLECT( Category = "Gameplay", DisplayName = "World Item", Tooltip = "An item stack lying in the world that a character bag can pick up" )
    class SW_GF_API PickupComponent : public Component
    {
    public:
        REFLECT_BODY();

        static constexpr const utf8* kSpawnedName = "Pickup"; ///< `spawn` 이 만든 오브젝트의 이름(같은 이름이면 저장소가 `_2` 를 붙인다)

        PickupComponent();
        ~PickupComponent() override;

        /** @brief 씬 데이터의 아이템(`_itemID` · `_count`)을 조각으로 세우고(`spawn` 이 넣은 조각이 있으면 그대로 둔다) 같은 오브젝트의 상호작용 완료를 듣습니다. */
        void onBeginPlay() override;
        /** @brief 겹침 진입 사건을 구독합니다. */
        void onRegister( GameObjectManager& manager ) override;

        /**
         * @brief 프리팹 @p prefabPath(모습 · 상호작용 · 이 컴포넌트)를 @p position 에 세워 아이템 조각 @p item 을 쥐입니다. 못 세우면 오류를 남기고 nullptr 입니다(빈 조각이면 조용히 nullptr).
         * @details 버리기 화면 · 서버의 버리기 처리가 부른다. 모습과 줍는 상호작용은 프리팹 데이터다(`CharacterEquipmentComponent` 의 `_pickupPrefab`) — 세운 것은 다시 상호작용으로 주울 수 있다.
         *          부르는 쪽은 nullptr 이면 조각을 가방에 돌려준다(잃지 않는다).
         */
        [[nodiscard]] static GameObject* spawn( GameObjectManager& manager, string_view prefabPath, const InventorySlot& item, const float3& position );
        /** @brief 든 조각을 정합니다. 시작 전에 부릅니다(`spawn` · 테스트). */
        void setItem( const InventorySlot& item );

        /** @brief 가방 @p bag(캐릭터의 것 — `CharacterEquipmentComponent::getBagForTrade`)에 이 조각을 모두 넣고 오브젝트를 없앱니다. 다 들어가지 않으면 아무것도 바꾸지 않고 false 입니다. */
        [[nodiscard]] bool pickUp( Inventory& bag );
        /**
         * @brief @p target 이 받을 가방(`IItemReceiver::findForTarget`)에 이 조각을 모두 넣고 오브젝트를 없앱니다. 받을 가방이 없거나 차면 경고를 남기고 그대로 둡니다(false).
         * @details 게임 스레드(틱 밖)에서 부른다 — 가방은 다른 오브젝트의 상태다.
         */
        [[nodiscard]] bool giveTo( const GameObject& target );

        const InventorySlot& getItem() const { return _item; }

        /** @brief 든 조각의 개체 상태(내구도 · 꾸미기 · 뗀 부품)까지입니다(`Inventory::serializeSlot`). id · 수만 실리면 주운 물건이 새것이 된다. */
        SW_DECLARE_STATE_IO_IN_PLACE();
        /** @brief `serializeState` 의 바이트를 되돌립니다. 읽지 못하면 씬 데이터의 조각으로 둡니다. */
        void restoreState( vector<uint8>&& bytes );

    private:
        /** @brief 닿은 몸이 줍습니다(`_bPickUpOnTouch`, 받을 가방이 있는 몸만). */
        void handleOverlapBegin( const OverlapInfo& overlap );
        /** @brief 상호작용을 끝낸 쪽이 줍습니다. */
        void handleUsed( const GameObject& user );
        /** @brief 조각을 비우고 오브젝트를 없앱니다. */
        void consume();

        Subscription  _overlapSubscription; ///< 겹침 진입 구독(`handleOverlapBegin`)
        Subscription  _usedSubscription;    ///< 같은 오브젝트의 상호작용 완료 구독
        InventorySlot _item;                ///< 든 조각(개체 상태째)
        PROPERTY( DisplayName = "Item", Tooltip = "Item id of the stack (scene data; a dropped stack keeps its own instance)" )
        hashed_string _itemID;
        PROPERTY( DisplayName = "Count", Min = 1 )
        int32 _count;
        PROPERTY( DisplayName = "Pick Up On Touch", Tooltip = "Touching it picks it up (needs a trigger body); off: only this object's interaction" )
        bool _bPickUpOnTouch;
    };
} // namespace sw
