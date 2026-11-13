/**
 * @file Ledger.h
 * @brief 원장을 정의합니다. 테이블 여섯(`ledger_balance`, `ledger_journal`, `ledger_history`, `ledger_carry`, `ledger_tombstone`, `ledger_deletion_queue`) 위의 복식 이동입니다.
 * @details 모두 저장소 스레드(`IServiceStoreJob::run`)에서 도는 동기 함수이다.
 *          PlayFab Economy v2 의 거래 기록이나 Nakama 의 wallet_ledger(단식)와 달리 복식이며 분개 키로 멱등하다. 회계의 차변과 대변이 같다.
 *
 *          - 잔액: 계정과 맡김마다 (보유자, 자산) 레코드 하나를 둔다(키 `<보유자 키>/<자산>`). 0 이 되면 지운다. 발행과 소각에는 잔액이 없다.
 *          - 분개: 이동 한 건의 원본이다(다리, 사유, 주체, 시각, 이동 뒤 잔액, 키는 분개 키). "없어야 한다" 조건으로 넣으므로 분개 키가 멱등 키이다.
 *          - 내역 색인: 계정과 맡김마다 `<보유자 키>/<시각 16 진 16>/<분개 키>` 를 두며 값은 없다. 최근 것부터 읽는다.
 *          - 이월: 보존 쓸기가 분개를 지울 때 그 분개의 발행 · 소각 합을 자산별 이월 레코드(키 `<자산>`)에 같은 트랜잭션으로 더한다.
 *            같은 트랜잭션이 그 분개의 내역 색인 줄도 지운다 — 내역은 분개의 색인이라 분개와 함께 사라진다(가리키는 분개가 없는 내역이 남지 않는다).
 *            분개를 넣는 트랜잭션이 TTL 색인 `ledger_journal_expiry`(`<이동 시각>/<분개 키>`)를 함께 넣어 쓸기가 기간이 지난 분개만 범위로 읽는다.
 *            분개는 감사 내역이라 지워지지만 공급 합은 남는다. 멱등 기록이 필요한 쪽(영수증 지급)은 분개가 아니라 자기 표에 지우지 않는 레코드를 둔다.
 *            PlayFab 이 영수증 거래 id 를 거래 내역과 따로 타이틀 수명 동안 두는 자리다. 분개 쓸기 규칙은 `makeJournalTTLRule` 하나로만 만든다.
 *          - 계정 삭제: 탈퇴 정리가 계정을 지우는 트랜잭션에 묘비(`ledger_tombstone`, 키 `acct/<16 진>`)와 소각 대기(`ledger_deletion_queue`)를 함께 넣는다(`stageAccountDeletion`).
 *            묘비가 선 계정은 어떤 이동에도 들 수 없고(`AccountDeleted` — 묘비가 없어야 함을 커밋 조건으로 건다), 남은 잔액은 `burnDeletedAccounts` 가 소각 분개(사유
 *            `account.delete`)로 Sink 에 보내고 빚은 Mint 에서 메워 0 으로 만든다. 잔액 레코드는 사라지고 공급 합은 분개 · 이월에 남아 감사 균형이 맞는다.
 *            Nakama 의 DeleteAccount 가 남기는 user_tombstone, 회계의 잔액 소각 · 대손 상각과 같은 자리다. 묘비는 지우지 않는다(지우면 늦게 온 이동이 유령 잔액을 만든다).
 *          - 불변식: 자산마다 계정 잔액과 맡김 잔액의 합은 이월과 남은 분개의 발행 합에서 소각 합을 뺀 값과 같다.
 *            맡김 잔액은 0 이상이고, 계정 잔액은 빚(환불 회수)일 때만 음수이다(`LedgerAuditReport`).
 *          - 잔액은 버전 조건으로 쓴다. 읽은 뒤 다른 이동이 끼면 커밋이 `Conflict` 이고, `executeTransfer` 가 다시 읽어 다시 한다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Online/Ledger/LedgerTypes.h"
#include "GameFramework/Runtime/Online/Store/Server/ServiceStore.h"
#include "GameFramework/Runtime/Online/Store/Server/ServiceTTLRule.h"

namespace sw
{
    /** @brief 자산 하나의 공급 합입니다. 분개 하나 이상 또는 이월 레코드에서 셉니다. */
    struct LedgerSupplyStats
    {
        string _assetID{};
        int64  _issued{ 0 }; ///< 발행에서 나간 양의 합
        int64  _burned{ 0 }; ///< 소각으로 들어간 양의 합
    };
} // namespace sw

namespace sw
{
    /** @brief 원장 함수 모음입니다. 저장소 스레드에서 부릅니다. */
    struct SW_GF_API Ledger
    {
        static const hashed_string& getBalanceTable();
        static const hashed_string& getJournalTable();
        static const hashed_string& getHistoryTable();
        static const hashed_string& getCarryTable();         ///< "ledger_carry" — 키 `<자산>`, 값 `[판][발행 합][소각 합]`. 지운 분개의 공급 합이며 지우지 않는다
        static const hashed_string& getTombstoneTable();     ///< "ledger_tombstone" — 키 `acct/<16 진>`, 값 `[판][삭제 시각]`. 지운 계정이며 지우지 않는다
        static const hashed_string& getDeletionQueueTable(); ///< "ledger_deletion_queue" — 키 `acct/<16 진>`, 값 없음. 잔액을 아직 다 소각하지 않은 지운 계정
        /**
         * @brief 계정 @p accountID 를 지우는 쓰기(묘비와 소각 대기)를 @p inoutTransaction 에 붙입니다. 탈퇴 정리가 계정 레코드를 지우는 트랜잭션에 넣습니다.
         * @details 함께 커밋되므로 탈퇴가 취소돼 계정 커밋이 지면 묘비도 서지 않는다. 잔액은 커밋 뒤 `burnDeletedAccounts` 가 소각한다.
         */
        static void stageAccountDeletion( uint64 accountID, int64 timeMs, ServiceTransaction& inoutTransaction );
        /**
         * @brief 계정 @p accountID 에 묘비가 없어야 함을 @p inoutTransaction 의 조건으로 겁니다. 새 계정을 만드는 트랜잭션이 넣습니다.
         * @details 계정 id 는 64 비트 난수이고 지운 계정의 프로필은 사라지므로 같은 id 가 다시 뽑힐 수 있다(확률 2^-64). 그 id 는 묘비가 막아 커밋이 Conflict 이고, 만드는 쪽이 새 id 로 다시 한다 —
         *          계정 id 는 다시 쓰지 않는다.
         */
        static void requireNoTombstone( uint64 accountID, ServiceTransaction& inoutTransaction );
        /**
         * @brief 소각 대기의 계정을 @p maxCount 개까지 골라 남은 잔액을 모두 소각합니다. 다 소각한 계정은 대기에서 뺍니다.
         * @details 분개 하나에 자산 `kMaxPostingCount` 개씩, 분개 키는 `acct.<16 진>/delete.<첫 자산>` 이다. 인스턴스 아이템은 상태 레코드(`item_state`)도 지운다.
         *          커밋이 지면 다음 회차가 다시 읽어 남은 것만 소각하므로 두 번 소각하지 않는다.
         * @return 저장소에 문제가 생기면 그 결과로 멈춥니다. 다 소각한 계정 수는 @p outBurnedCount 입니다.
         */
        [[nodiscard]] static ServiceStoreResult burnDeletedAccounts( IServiceStoreConnection& connection, int64 timeMs, int32 maxCount, int32& outBurnedCount );
        /**
         * @brief 분개 표의 TTL 규칙을 만듭니다(TTL 색인 + 색인 없는 옛 분개의 백필). 지우는 분개의 발행 · 소각 합을 이월 레코드에 더하고 내역 줄을 지우는 쓰기를 같은 트랜잭션에 넣습니다.
         * @details 분개를 끝까지 읽지 못하면 시각도 읽지 않은 것으로 보고 지우지 않는다 — 이월에 넣지 못하는 분개를 지우면 감사 균형이 깨진다.
         */
        [[nodiscard]] static ServiceTTLRule makeJournalTTLRule( int64 retentionMs );
        /** @brief 분개 @p entry 의 발행 · 소각 다리를 자산별로 @p inoutListSupply 에 더합니다. 이월과 감사(`LedgerAuditReport`)가 같은 셈을 씁니다. */
        static void addSupply( const LedgerJournalEntry& entry, vector<LedgerSupplyStats>& inoutListSupply );
        /** @brief 이월 레코드를 모두 읽어 @p inoutListSupply 에 더합니다. 읽지 못한 레코드는 @p outUnreadableCount 에 셉니다. */
        [[nodiscard]] static ServiceStoreResult addCarriedSupply( IServiceStoreConnection& connection, vector<LedgerSupplyStats>& inoutListSupply, int32& outUnreadableCount );

        /**
         * @brief 이동을 @p inoutTransaction 에 붙입니다. 커밋은 하지 않으며, 자기 레코드와 한 트랜잭션에 넣는 쪽(거래, 우편 수령, 구매 한도)이 씁니다.
         * @details 분개 키가 이미 있으면 붙이지 않고 `Ok` 와 `_bReplayed` 를 반환한다. 내용이 다르면 `JournalKeyReused` 이다. 실패하면 아무것도 붙이지 않는다.
         *          붙이는 쓰기 수는 바뀐 잔액 수에 분개 1, 분개의 TTL 색인 1 과 계정, 맡김 보유자 수(내역)와 계정 보유자 수(묘비가 없어야 한다는 조건)를 더한 값이다.
         *          다리의 계정에 묘비가 서 있으면(탈퇴로 지움) `AccountDeleted` 이다.
         *          부르는 쪽 쓰기와 합쳐 `ServiceTransaction::kMaxWriteCount` 를 넘으면 `Invalid` 이다.
         *          커밋이 `Conflict` 나 `Unavailable` 이면 `resolveConflict` 로 가린다.
         */
        [[nodiscard]] static LedgerResult stageTransfer( IServiceStoreConnection& connection, const LedgerTransferRequest& request, ServiceTransaction& inoutTransaction,
                                                         LedgerTransferResult& outResult );
        /** @brief 붙인 트랜잭션의 커밋이 실패했을 때 결과를 가립니다. 분개가 이제 있으면 `Ok` 와 `_bReplayed` 이며(다른 서버가 먼저 했거나 응답만 잃은 경우), 없으면 `Conflict` 이고, 읽지 못하면 `Unavailable` 입니다. */
        [[nodiscard]] static LedgerResult resolveConflict( IServiceStoreConnection& connection, const LedgerTransferRequest& request, LedgerTransferResult& outResult );
        /**
         * @brief 이동 하나를 혼자 커밋합니다. 붙이고, 커밋하고, 버전 충돌이면 다시 읽어 다시 합니다(`kMaxRetryCount`).
         * @details 커밋이 `Unavailable` 이고 분개가 없으면 `Unavailable` 이다. 적용 여부를 모르는 상태이며, 같은 키로 나중에 다시 하면 가려진다.
         */
        [[nodiscard]] static LedgerResult executeTransfer( IServiceStoreConnection& connection, const LedgerTransferRequest& request, LedgerTransferResult& outResult );

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
