/**
 * @file Ledger.h
 * @brief 원장 — 표 셋(`ledger_balance` · `ledger_journal` · `ledger_history`) 위의 복식 이동입니다. 모두 저장소 스레드(`IServiceStoreWork::run`)의 동기 함수입니다.
 * @details - 잔액: 계정 · 맡김마다 (보유자, 자산) 레코드 하나(키 `<보유자 키>/<자산>`). 0 이 되면 지운다. 발행 · 소각에는 없다.
 *          - 분개: 이동 한 건의 정본(다리 · 사유 · 주체 · 시각 · 이동 뒤 잔액, 키 = 분개 키). "없어야 한다" 조건으로 넣어 분개 키가 멱등 키다.
 *          - 내역 색인: 계정 · 맡김마다 `<보유자 키>/<시각 16 진 16>/<분개 키>`(값 없음) — 최근 것부터 읽는다.
 *          - 불변식: 자산마다 Σ(계정 + 맡김 잔액) = 발행에서 나간 양 − 소각으로 들어간 양, 맡김 잔액 ≥ 0, 계정 잔액은 빚(환불 회수)만 음수(`LedgerAudit`).
 *          - 잔액은 판 조건으로 쓴다 — 읽은 뒤 다른 이동이 끼면 커밋이 `Conflict` 이고 `executeTransfer` 가 다시 읽어 다시 한다.
 *          PlayFab Economy v2 의 거래 기록 · Nakama wallet_ledger(단식)와 달리 복식 + 분개 키 멱등이다(회계의 차변 = 대변).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Online/Ledger/LedgerTypes.h"
#include "GameFramework/Base/Online/Store/ServiceStore.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 원장 함수입니다(저장소 스레드). */
    struct SW_GF_API Ledger
    {
        static const hashed_string& getBalanceTable();
        static const hashed_string& getJournalTable();
        static const hashed_string& getHistoryTable();

        /**
         * @brief 이동을 @p inoutTransaction 에 붙입니다(커밋하지 않는다) — 자기 레코드와 한 트랜잭션에 넣는 쪽(거래 · 우편 수령 · 구매 한도)이 씁니다.
         * @details 분개 키가 이미 있으면 붙이지 않고 `Ok` + `_bReplayed`(내용이 다르면 `JournalKeyReused`). 실패면 아무것도 붙이지 않는다.
         *          붙이는 쓰기 = 바뀐 잔액 수 + 분개 1 + 계정 · 맡김 보유자 수. 부르는 쪽 쓰기와 합쳐 `ServiceTransaction::kMaxWriteCount` 를 넘으면 `Invalid`.
         *          커밋이 `Conflict` · `Unavailable` 이면 `resolveConflict` 로 가린다.
         */
        [[nodiscard]] static LedgerResult stageTransfer( IServiceStoreConnection& connection, const LedgerTransferRequest& request, ServiceTransaction& inoutTransaction,
                                                         LedgerTransferOutcome& outOutcome );
        /** @brief 붙인 트랜잭션의 커밋이 실패했을 때 — 분개가 이제 있으면 `Ok` + `_bReplayed`(다른 서버가 먼저 · 응답만 잃음), 없으면 `Conflict`, 읽지 못하면 `Unavailable` 입니다. */
        [[nodiscard]] static LedgerResult resolveConflict( IServiceStoreConnection& connection, const LedgerTransferRequest& request, LedgerTransferOutcome& outOutcome );
        /**
         * @brief 이동 하나를 혼자 커밋합니다 — 붙이기 → 커밋 → 판 충돌이면 다시 읽어 다시(`kMaxRetryCount`).
         * @details 커밋이 `Unavailable` 이고 분개가 없으면 `Unavailable` 이다(적용 여부를 모른다 — 같은 키로 나중에 다시 하면 가려진다).
         */
        [[nodiscard]] static LedgerResult executeTransfer( IServiceStoreConnection& connection, const LedgerTransferRequest& request, LedgerTransferOutcome& outOutcome );

        /** @brief 계정 · 맡김의 모든 잔액(자산 id 순)을 @p outListBalance 뒤에 붙입니다. 시스템 보유자 · 무효 보유자는 `Invalid` 입니다. */
        [[nodiscard]] static ServiceStoreResult listBalances( IServiceStoreConnection& connection, const LedgerHolder& holder, vector<LedgerBalance>& outListBalance );
        /** @brief 잔액 하나입니다. 레코드가 없으면 `Ok` 와 0(판 0) 입니다. */
        [[nodiscard]] static ServiceStoreResult readBalance( IServiceStoreConnection& connection, const LedgerHolder& holder, string_view assetId, LedgerBalance& outBalance );
        /** @brief 최근 것부터 분개를 읽습니다. @p cursor 는 앞 쪽의 @p outNextCursor(처음은 빈 글), 다 읽었으면 @p outNextCursor 가 빕니다. */
        [[nodiscard]] static ServiceStoreResult listHistory( IServiceStoreConnection& connection, const LedgerHolder& holder, string_view cursor, int32 maxCount,
                                                             vector<LedgerJournalEntry>& outListEntry, string& outNextCursor );
        [[nodiscard]] static ServiceStoreResult findJournal( IServiceStoreConnection& connection, string_view journalKey, LedgerJournalEntry& outEntry );
        /** @brief @p source 의 쓰기를 @p inoutTarget 뒤에 그대로 붙입니다 — 부분을 따로 만들어 상한을 본 뒤 합칠 때(우편 넣기 · 구매 한도). */
        static void appendWrites( const ServiceTransaction& source, ServiceTransaction& inoutTarget );
        /** @brief 잔액 레코드 바이트를 읽습니다(검사 · 시험). */
        [[nodiscard]] static bool decodeBalanceRecord( const vector<uint8>& bytes, int64& outAmount );
    };
} // namespace sw
