/**
 * @file Ledger.h
 * @brief 원장을 정의합니다. 테이블 세 개(`ledger_balance`, `ledger_journal`, `ledger_history`) 위의 복식 이동입니다.
 * @details 모두 저장소 스레드(`IServiceStoreWork::run`)에서 도는 동기 함수이다.
 *          PlayFab Economy v2 의 거래 기록이나 Nakama 의 wallet_ledger(단식)와 달리 복식이며 분개 키로 멱등하다. 회계의 차변과 대변이 같다.
 *
 *          - 잔액: 계정과 맡김마다 (보유자, 자산) 레코드 하나를 둔다(키 `<보유자 키>/<자산>`). 0 이 되면 지운다. 발행과 소각에는 잔액이 없다.
 *          - 분개: 이동 한 건의 원본이다(다리, 사유, 주체, 시각, 이동 뒤 잔액, 키는 분개 키). "없어야 한다" 조건으로 넣으므로 분개 키가 멱등 키이다.
 *          - 내역 색인: 계정과 맡김마다 `<보유자 키>/<시각 16 진 16>/<분개 키>` 를 두며 값은 없다. 최근 것부터 읽는다.
 *          - 불변식: 자산마다 계정 잔액과 맡김 잔액의 합은 발행에서 나간 양에서 소각으로 들어간 양을 뺀 값과 같다.
 *            맡김 잔액은 0 이상이고, 계정 잔액은 빚(환불 회수)일 때만 음수이다(`LedgerAudit`).
 *          - 잔액은 버전 조건으로 쓴다. 읽은 뒤 다른 이동이 끼면 커밋이 `Conflict` 이고, `executeTransfer` 가 다시 읽어 다시 한다.
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
    /** @brief 원장 함수 모음입니다. 저장소 스레드에서 부릅니다. */
    struct SW_GF_API Ledger
    {
        static const hashed_string& getBalanceTable();
        static const hashed_string& getJournalTable();
        static const hashed_string& getHistoryTable();

        /**
         * @brief 이동을 @p inoutTransaction 에 붙입니다. 커밋은 하지 않으며, 자기 레코드와 한 트랜잭션에 넣는 쪽(거래, 우편 수령, 구매 한도)이 씁니다.
         * @details 분개 키가 이미 있으면 붙이지 않고 `Ok` 와 `_bReplayed` 를 반환한다. 내용이 다르면 `JournalKeyReused` 이다. 실패하면 아무것도 붙이지 않는다.
         *          붙이는 쓰기 수는 바뀐 잔액 수에 분개 1 과 계정, 맡김 보유자 수를 더한 값이다.
         *          부르는 쪽 쓰기와 합쳐 `ServiceTransaction::kMaxWriteCount` 를 넘으면 `Invalid` 이다.
         *          커밋이 `Conflict` 나 `Unavailable` 이면 `resolveConflict` 로 가린다.
         */
        [[nodiscard]] static LedgerResult stageTransfer( IServiceStoreConnection& connection, const LedgerTransferRequest& request, ServiceTransaction& inoutTransaction,
                                                         LedgerTransferOutcome& outOutcome );
        /** @brief 붙인 트랜잭션의 커밋이 실패했을 때 결과를 가립니다. 분개가 이제 있으면 `Ok` 와 `_bReplayed` 이며(다른 서버가 먼저 했거나 응답만 잃은 경우), 없으면 `Conflict` 이고, 읽지 못하면 `Unavailable` 입니다. */
        [[nodiscard]] static LedgerResult resolveConflict( IServiceStoreConnection& connection, const LedgerTransferRequest& request, LedgerTransferOutcome& outOutcome );
        /**
         * @brief 이동 하나를 혼자 커밋합니다. 붙이고, 커밋하고, 버전 충돌이면 다시 읽어 다시 합니다(`kMaxRetryCount`).
         * @details 커밋이 `Unavailable` 이고 분개가 없으면 `Unavailable` 이다. 적용 여부를 모르는 상태이며, 같은 키로 나중에 다시 하면 가려진다.
         */
        [[nodiscard]] static LedgerResult executeTransfer( IServiceStoreConnection& connection, const LedgerTransferRequest& request, LedgerTransferOutcome& outOutcome );

        /** @brief 계정이나 맡김의 모든 잔액을 자산 id 순으로 @p outListBalance 뒤에 붙입니다. 시스템 보유자와 무효 보유자는 `Invalid` 입니다. */
        [[nodiscard]] static ServiceStoreResult listBalances( IServiceStoreConnection& connection, const LedgerHolder& holder, vector<LedgerBalance>& outListBalance );
        /** @brief 잔액 하나를 읽습니다. 레코드가 없으면 `Ok` 와 0(버전 0)을 반환합니다. */
        [[nodiscard]] static ServiceStoreResult readBalance( IServiceStoreConnection& connection, const LedgerHolder& holder, string_view assetID, LedgerBalance& outBalance );
        /** @brief 최근 것부터 분개를 읽습니다. @p cursor 는 앞 쪽이 반환한 @p outNextCursor 이며 처음은 빈 글입니다. 다 읽었으면 @p outNextCursor 가 빕니다. */
        [[nodiscard]] static ServiceStoreResult listHistory( IServiceStoreConnection& connection, const LedgerHolder& holder, string_view cursor, int32 maxCount,
                                                             vector<LedgerJournalEntry>& outListEntry, string& outNextCursor );
        [[nodiscard]] static ServiceStoreResult findJournal( IServiceStoreConnection& connection, string_view journalKey, LedgerJournalEntry& outEntry );
        /** @brief @p source 의 쓰기를 @p inoutTarget 뒤에 그대로 붙입니다. 부분을 따로 만들어 상한을 본 뒤 합칠 때 쓰며, 우편 넣기와 구매 한도가 그 예입니다. */
        static void appendWrites( const ServiceTransaction& source, ServiceTransaction& inoutTarget );
        /** @brief 잔액 레코드 바이트를 읽습니다. 검사와 테스트가 씁니다. */
        [[nodiscard]] static bool decodeBalanceRecord( const vector<uint8>& bytes, int64& outAmount );
    };
} // namespace sw
