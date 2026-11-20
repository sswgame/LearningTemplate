/**
 * @file WorldItemComponent.h
 * @brief 월드에 놓인 아이템(떨어뜨린 것 · 씬에 둔 것)입니다. 캐릭터 가방이 주워 가면 오브젝트가 사라지고, 가방에 자리가 없으면 그대로 남습니다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Inventory.h"

namespace sw
{
    class GameObject;
    class GameObjectManager;

    /**
     * @class WorldItemComponent
     * @brief 아이템 조각 하나를 월드에 둡니다(언리얼 Lyra 의 `ALyraWorldCollectable` · 유니티 픽업 프리팹 자리).
     * @details 씬 데이터는 `_itemID` · `_count` 를 적고, 버리기(`InventoryCommandKind::Drop`)로 생긴 것은 `spawn` 이 꺼낸 조각(내구도 · 꾸미기째)을 그대로 든다.
     *          줍기(`pickUp`)는 전부 들어갈 때만 한다(겹치는 것은 칸 · 무게, 개체는 빈 칸) — 반만 주워 바닥에 남은 수가 갈라지지 않는다.
     */
    REFLECT( Category = "Gameplay", DisplayName = "World Item", Tooltip = "An item stack lying in the world that a character bag can pick up" )
    class SW_GF_API WorldItemComponent : public Component
    {
    public:
        REFLECT_BODY();

        static constexpr const utf8* kSpawnedName = "WorldItem"; ///< `spawn` 이 만든 오브젝트의 이름

        WorldItemComponent();
        ~WorldItemComponent() override;

        /** @brief 씬 데이터의 아이템(`_itemID` · `_count`)을 조각으로 세웁니다(`spawn` 이 넣은 조각이 있으면 그대로 둔다). */
        void onBeginPlay() override;

        /**
         * @brief @p manager 에 아이템 조각 @p item 을 든 오브젝트를 @p position 에 만듭니다(작은 상자 메시 + 이 컴포넌트). 빈 조각이면 만들지 않고 nullptr 입니다.
         * @details 버리기 화면 · 서버의 버리기 처리가 부른다. 이름은 `kSpawnedName` 이다(같은 이름이면 저장소가 `_2` 를 붙인다).
         */
        static GameObject* spawn( GameObjectManager& manager, const InventorySlot& item, const float3& position );

        /** @brief 가방 @p bag(캐릭터의 것 — `CharacterEquipmentComponent::getBagForTrade`)에 이 조각을 모두 넣고 오브젝트를 없앱니다. 다 들어가지 않으면 아무것도 바꾸지 않고 false 입니다. */
        [[nodiscard]] bool pickUp( Inventory& bag );

        const InventorySlot& getItem() const { return _item; }

    private:
        InventorySlot _item; ///< 든 조각(개체 상태째)
        PROPERTY( DisplayName = "Item", Tooltip = "Item id of the stack (scene data; a dropped stack keeps its own instance)" )
        hashed_string _itemID;
        PROPERTY( DisplayName = "Count", Min = 1 )
        int32 _count;
    };
} // namespace sw
