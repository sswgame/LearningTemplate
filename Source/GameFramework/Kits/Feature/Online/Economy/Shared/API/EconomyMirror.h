/**
 * @file EconomyMirror.h
 * @brief 서버 원장의 잔액을 클라이언트의 `Wallet`(화폐)과 `Inventory`(아이템)에 차이만큼 맞춥니다. 원장이 원본이고 둘은 읽기 사본입니다.
 * @details 자산 `cur.<이름>` 은 지갑 통화 `<이름>` 으로, `item.<이름>` 은 아이템 id `<이름>` 으로 바꾼다. 아이템 id 는 hashed_string 이라 대소문자를 구분하지 않는다. 다른 접두어는 게임 고유 자산이므로 건너뛴다.
 *          지갑은 `add` 와 `charge` 로 맞추므로 화면 이벤트("+20 Gold")가 그대로 발생하고 빚(환불 회수로 생긴 음수 잔액)도 그대로 반영한다.
 *          인벤토리는 `addItem` 과 `removeItem` 으로 맞춘다. 슬롯 배치는 클라이언트의 것이고 빚은 0으로 반영한다.
 *          @p bSnapshot 이 true 이면 목록에 없는 화폐와 아이템을 0으로 맞춘다(지갑 전체 응답). 아니면 목록에 든 것만 맞춘다(구매, 거래, 우편 응답의 바뀐 잔액).
 *          온라인 게임은 지갑을 로컬에서 바꾸지 않으며 `add` 와 `trySpend` 는 이 클래스만 부른다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include "GameFramework/Base/Online/Ledger/LedgerTypes.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class Inventory;
    class Wallet;

    /** @brief 원장 잔액을 지갑과 인벤토리에 비추는 미러입니다. */
    struct SW_GF_API EconomyMirror
    {
        static constexpr const utf8* kCurrencyPrefix = "cur.";
        static constexpr const utf8* kItemPrefix     = "item.";

        static void applyToWallet( const vector<LedgerBalance>& listBalance, bool bSnapshot, Wallet& inoutWallet );
        /** @brief 잔액을 맞춥니다. 슬롯이나 무게가 모자라 넣지 못한 개수의 합을 반환하며, 화면이 "가방이 가득 참" 을 띄우는 데 씁니다. */
        static int32 applyToInventory( const vector<LedgerBalance>& listBalance, bool bSnapshot, Inventory& inoutInventory );
    };
} // namespace sw
