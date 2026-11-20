/**
 * @file InventoryCommand.h
 * @brief 가방 · 장비 칸 조작 하나(옮기기 · 나누기 · 버리기 · 장착 · 해제 · 사용 · 정렬)를 값 하나로 적은 명령입니다. 화면 · 패드 · 서버가 같은 명령과 같은 검사를 거칩니다.
 * @details 멀티플레이에서는 클라이언트가 이 명령을 요청으로 보내고 서버가 같은 `validate` 를 거쳐 정본 인벤토리에 적용한다(언리얼 Lyra 의 인벤토리 이동 서버 RPC, 유니티 Netcode 의 ServerRpc).
 *          검사는 형식 한 곳이다 — 범위 밖 칸 · 0 이하 수량 · 가진 것보다 많은 수량(복제) · 같은 칸으로의 이동은 적용 전에 거절한다. 그래서 화면에서 막은 길과 서버가 막는 길이 갈리지 않는다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class Inventory;
    class Equipment;
    struct InventorySlot;

    /** @brief 명령 종류입니다. */
    ENUM()
    enum class InventoryCommandKind : uint8
    {
        Move = 0, ///< 칸 → 칸. 같은 아이템이면 합치고(넘치면 남김) 다르면 바꾼다
        Split,    ///< 칸에서 `_count` 개를 떼어 빈 칸에 둔다
        Drop,     ///< 칸에서 `_count` 개를 꺼내 월드에 떨어뜨린다(꺼낸 조각은 부르는 쪽이 받는다)
        Equip,    ///< 칸의 아이템을 장비 칸 `_equipSlot` 에 낀다(비우면 받는 칸을 고른다)
        Unequip,  ///< 장비 칸 `_equipSlot` 을 벗겨 가방에 넣는다
        Use,      ///< 칸의 아이템 하나를 쓴다(쓰임 `ItemDef::_useEffect` 가 있는 것만)
        Sort      ///< 가방을 정렬하고 겹칠 수 있는 것을 합친다
    };

    /** @brief 명령 검사 · 적용의 결과입니다. */
    ENUM()
    enum class InventoryCommandResult : uint8
    {
        Ok = 0,
        InvalidSlot,  ///< 범위 밖 칸, 같은 칸으로의 이동, 없는 장비 칸
        EmptySlot,    ///< 빈 칸을 옮기거나 쓰려 했다
        InvalidCount, ///< 0 이하이거나 가진 것보다 많은 수량(복제 요청), 나누기에 전부를 적음
        NoRoom,       ///< 빈 칸 · 무게가 모자라다
        Refused,      ///< 장비 조건 · 종류가 맞지 않는다
        NotUsable     ///< 쓰임이 없는 아이템이다
    };

    /**
     * @struct InventoryCommand
     * @brief 가방 조작 하나입니다. 필요 없는 칸은 기본값 그대로 둡니다.
     */
    struct SW_GF_API InventoryCommand
    {
        static constexpr int32 kNoSlot = -1;

        hashed_string        _equipSlot{};         ///< `Equip` · `Unequip` 의 장비 칸 이름
        int32                _fromSlot{ kNoSlot }; ///< 가방 칸(Move · Split · Drop · Equip · Use)
        int32                _toSlot{ kNoSlot };   ///< 가방 칸(Move)
        int32                _count{ 0 };          ///< Split · Drop 의 수량
        InventoryCommandKind _kind{ InventoryCommandKind::Move };

        /** @brief 지금 가방 @p bag 과 장비 @p equipment 에 대해 이 명령이 적용될 수 있는지 봅니다. 바꾸지 않습니다. 서버는 받은 요청을 이것으로 먼저 거른다. */
        [[nodiscard]] InventoryCommandResult validate( const Inventory& bag, const Equipment& equipment ) const;
        /**
         * @brief 검사한 뒤 적용합니다. 실패하면 아무것도 바뀌지 않습니다. `Drop` 이 꺼낸 조각(개체 상태째)은 @p outListDropped 뒤에 붙습니다 — 월드에 놓는 일은 부르는 쪽이다.
         * @details `Use` 는 하나를 소비만 한다 — 쓰임(`_useEffect`)의 효과는 게임이 이 결과를 보고 건다.
         */
        [[nodiscard]] InventoryCommandResult apply( Inventory& bag, Equipment& equipment, vector<InventorySlot>& outListDropped ) const;
    };
} // namespace sw
