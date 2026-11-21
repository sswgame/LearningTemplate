/**
 * @file ItemReceiverTestComponent.h
 * @brief 아이템 지급 테스트(`TestLootGrants.cpp`)가 다는 아이템 받는 쪽입니다 — 개발 구성만 코드젠하고 "Add Component" 메뉴에 나오지 않는다.
 * @details 상자가 가방 주인의 구체 형식이 아니라 계약(`IItemReceiver`)만 보는지, 거절이면 상자가 열리지 않는지 확인한다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Runtime/Gameplay/Inventory/IItemReceiver.h"

namespace sw
{
    /** @brief `_room` 개까지 받아 세고, 넘치면 `NoRoom` 으로 거절합니다. */
    REFLECT( HideInMenu )
    struct ItemReceiverTestComponent : public Component
    {
        REFLECT_BODY();
        REFLECT_INTERFACE( IItemReceiver, _itemReceiverPort );

        /** @brief 주인에게 넘기는 포트입니다. */
        struct ItemReceiverPort final : IItemReceiver
        {
            [[nodiscard]] ItemGrantResult receiveItem( const hashed_string& item, int32 count ) override
            {
                ItemReceiverTestComponent& owner = getPortOwner<ItemReceiverTestComponent>( *this, offsetof( ItemReceiverTestComponent, _itemReceiverPort ) );
                if ( owner._received + count > owner._room )
                    return ItemGrantResult::NoRoom;
                owner._received += count;
                owner._lastItem = item;
                return ItemGrantResult::Granted;
            }
        };

        int32            _room{ 0 };
        int32            _received{ 0 };
        hashed_string    _lastItem{};
        ItemReceiverPort _itemReceiverPort;
    };
} // namespace sw
