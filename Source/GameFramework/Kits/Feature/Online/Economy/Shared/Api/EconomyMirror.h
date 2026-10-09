/**
 * @file EconomyMirror.h
 * @brief 서버 원장의 잔액을 클라이언트의 `Wallet`(화폐) · `Inventory`(아이템)에 차이만큼 맞춥니다 — 원장이 정본, 둘은 읽기 사본입니다.
 * @details 자산 `cur.<이름>` → 지갑 통화 `<이름>`, `item.<이름>` → 아이템 id `<이름>`(hashed_string — 대소문자 무시). 다른 접두는 건너뛴다(게임 고유 자산).
 *          지갑은 `add` · `charge` 로 맞춰 화면 사건("+20 Gold")이 그대로 나고 빚(환불 회수의 음수 잔액)도 그대로 비춘다. 인벤토리는 `addItem` · `removeItem` —
 *          칸 배치는 클라이언트 것이고 빚은 0 으로 비춘다. @p bSnapshot 이면 목록에 없는 화폐 · 아이템은 0 으로 맞춘다(지갑 전체 응답),
 *          아니면 목록에 든 것만(구매 · 거래 · 우편 응답의 바뀐 잔액). 온라인 게임은 지갑을 로컬에서 바꾸지 않는다(`add` · `trySpend` 는 이것만 부른다).
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

    /** @brief 원장 잔액 → 지갑 · 인벤토리 거울입니다. */
    struct SW_GF_API EconomyMirror
    {
        static constexpr const utf8* kCurrencyPrefix = "cur.";
        static constexpr const utf8* kItemPrefix     = "item.";

        static void applyToWallet( const vector<LedgerBalance>& listBalance, bool bSnapshot, Wallet& inoutWallet );
        /** @brief 맞춥니다. 칸 · 무게가 모자라 넣지 못한 개수의 합을 돌려줍니다(화면이 "가방이 가득 참" 을 띄운다). */
        static int32 applyToInventory( const vector<LedgerBalance>& listBalance, bool bSnapshot, Inventory& inoutInventory );
    };
} // namespace sw
