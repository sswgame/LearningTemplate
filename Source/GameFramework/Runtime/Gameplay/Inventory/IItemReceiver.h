/**
 * @file IItemReceiver.h
 * @brief 아이템을 받는 가방 주인이 내보내는 리플렉션 인터페이스입니다. 아이템 상자 · 채집 지점 같은 범용 기믹이 가방 주인의 구체 형식을 모르고 아이템을 넣습니다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/String/Name/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class GameObject;
    struct InventorySlot;

    /** @brief `IItemReceiver::receiveItem` 의 결과입니다. 넣거나 하나도 넣지 않는다(일부만 넣지 않는다). */
    enum class ItemGrantResult : uint8
    {
        Granted, ///< 모두 넣었다
        NoRoom,  ///< 가방에 모두 들어갈 자리(슬롯 · 무게)가 없다 — 아무것도 넣지 않았다. 가방이 없으면 받는 쪽이 없다(`findForTarget` 이 nullptr)
    };

    /**
     * @class IItemReceiver
     * @brief 가방(`Inventory`) 하나에 아이템을 넣는 계약입니다. 가방 주인(조종자의 `InventoryComponent` · 캐릭터의 `CharacterEquipmentComponent`)이 포트 멤버로 내보내고(`REFLECT_INTERFACE( IItemReceiver, _itemReceiverPort )`), 주는 쪽은
     *        `findForTarget` 으로 찾는다. 지갑 · 퀘스트 같은 다른 상태는 이 계약에 넣지 않는다.
     * @details 언리얼 Lyra 의 `ULyraInventoryManagerComponent::AddItemDefinition` 을 픽업이 부르는 자리이고, 찾기는 유니티 `GetComponent<IItemReceiver>()` 와 같다.
     *          모두 넣거나 하나도 넣지 않으므로 주는 쪽은 거절이면 아이템을 소비하지 않는다(상자는 닫힌 채, 채집 횟수는 그대로).
     */
    REFLECT( Interface, Category = "Inventory", DisplayName = "Item Receiver", Tooltip = "Contract of the bag owner that accepts items from pickups, item boxes and gathering nodes" )
    struct SW_GF_API IItemReceiver
    {
        REFLECT_BODY();

        virtual ~IItemReceiver() = default;

        /** @brief @p item × @p count 를 모두 넣습니다. 다 들어가지 않으면 아무것도 넣지 않고 그 까닭을 돌려줍니다. */
        [[nodiscard]] virtual ItemGrantResult receiveItem( const hashed_string& item, int32 count ) = 0;
        /** @brief 조각 @p stack 을 개체 상태(내구도 · 꾸미기)째 모두 넣습니다. 다 들어가지 않으면 아무것도 넣지 않습니다. 월드에 놓인 아이템(`PickupComponent`)을 주울 때 쓴다 — id 로 다시 넣으면 닳은 장비가 새것이 된다. */
        [[nodiscard]] virtual ItemGrantResult receiveStack( const InventorySlot& stack ) = 0;

        /**
         * @brief @p target 이 받을 가방입니다. @p target 자신이 내보내면 그것, 아니면 @p target 의 폰을 쥔 조종자 오브젝트가 내보내는 것입니다(가방은 조종자 쪽 — 언리얼 PlayerState 자리).
         * @return 없으면 nullptr 입니다. 다른 오브젝트를 읽으므로 게임 스레드(틱 밖)에서 부릅니다.
         */
        [[nodiscard]] static IItemReceiver* findForTarget( const GameObject& target );
    };
} // namespace sw
