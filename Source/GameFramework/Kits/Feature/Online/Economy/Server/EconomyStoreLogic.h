/**
 * @file EconomyStoreLogic.h
 * @brief 경제 서비스의 저장 부분입니다. 저장소 스레드(`IServiceStoreJob::run`)에서 부르는 동기 함수이며 서비스 멤버는 모릅니다. 입력은 값으로 받습니다.
 * @details - 구매는 원장 이동 한 건과 계정당 구매 수 레코드(`econ_purchase`, 한도가 있을 때)를 한 트랜잭션으로 처리한다. 원장 이동은 계정에서 소각으로 가격을 내고, 발행에서 계정으로 지급하는 것이다.
 *            분개 키는 클라이언트의 멱등 키이므로, 응답을 잃고 재시도하면 **잔액과 한도 판정보다 먼저** 지난 결과를 받는다.
 *          - 가상 화폐의 가격은 재원 자산의 현재 잔액을 적힌 순서대로 나눠서 분개 항목을 만든다. 무상 재화를 먼저 쓰는 것이 관례이다.
 *            커밋 사이에 잔액이 바뀌면 버전 충돌이 나므로 다시 짠다. 재원 중 하나라도 빚(환불 회수로 생긴 음수)이면 그 가상 화폐로는 살 수 없으며(`InsufficientFunds`) 빚을 먼저 갚아야 한다.
 *          - 영수증 지급의 분개 키는 거래 id(`rcpt.<스토어>/<거래 id 16진>`)이다. 그래서 같은 영수증은 어느 계정으로든 한 번만 지급된다.
 *            "누구에게" 는 구매자 대조가 정한다(`ReceiptBuyerBinding`) — 스토어 답의 구매자가 이 계정이 아니면 `NotPurchaser` 이고 지급 분개를 쓰지 않는다.
 *          - 지급 뒤 스토어에 알려야 하는 영수증(`_deliveryKey`)은 지급 분개와 **같은 트랜잭션**으로 `econ_delivery` 레코드를 쓴다. 지급이 진 영수증에는 레코드가 없어 알리지 않고(스토어가 환불한다),
 *            지급이 커밋되면 레코드도 있어 서버가 죽어도 다음 기동이 다시 알린다(트랜잭션 아웃박스 — 언리얼 · 유니티는 이 일을 게임 백엔드에 맡긴다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Economy/Runtime/Protocol/EconomyProtocol.h"
#include "GameFramework/Kits/Feature/Online/Economy/Server/EconomyKitConfig.h"
#include "GameFramework/Kits/Feature/Online/Economy/Server/Receipt/ReceiptValidator.h"
#include "GameFramework/Runtime/Online/Identity/Server/AccountDirectory.h"
#include "GameFramework/Runtime/Online/Ledger/LedgerTypes.h"
#include "GameFramework/Runtime/Online/Store/Server/ServiceStore.h"

namespace sw
{
    /** @brief 회수로 잔액이 바뀐 계정 하나와 그 자산의 잔액입니다. */
    struct EconomyBalanceChange
    {
        AccountID     _accountID{ kInvalidAccountID };
        LedgerBalance _balance{};
    };
} // namespace sw

namespace sw
{
    class CurrencyCatalog;
    class OfferCatalog;

    /** @brief 구매 입력입니다. */
    struct EconomyPurchaseInput
    {
        string _offerID{};
        string _journalKey{}; ///< `LedgerJournalKey::makeFromIdempotency( makeAccountScope( 계정 ), 멱등 키 )`
        uint64 _accountID{ 0 };
        int64  _nowMs{ 0 };
        int32  _count{ 1 };
    };
} // namespace sw

namespace sw
{
    /** @brief 영수증 지급 입력입니다. 검증이 끝난 결과를 담습니다. */
    struct EconomyRedeemInput
    {
        ReceiptValidationResult      _receipt{};
        const IAccountExternalLinks* _pExternalLinks{ nullptr }; ///< 구매자를 외부 로그인으로 묶는 제공자가 있으면 필수다(서비스가 기동 때 확인한다)
        uint64                       _accountID{ 0 };
        int64                        _nowMs{ 0 };
        uint8                        _bAcceptSandbox{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /** @brief 청약철회 입력입니다. */
    struct EconomyWithdrawInput
    {
        string _storeName{};
        string _transactionID{};
        uint64 _accountID{ 0 };
        int64  _nowMs{ 0 };
        int64  _windowMs{ 0 }; ///< 지급 뒤 이 시간 안이어야 한다(서비스 설정)
    };
} // namespace sw

namespace sw
{
    /** @brief 경제 저장 함수 모음입니다. 저장소 스레드에서 부릅니다. */
    struct SW_KIT_SERVER_API EconomyStoreLogic
    {
        static const hashed_string& getPurchaseCountTable(); ///< "econ_purchase" — 키 `<계정 16 진>/<상품 id>`
        static const hashed_string& getRefundCursorTable();  ///< "econ_refund_cursor" — 키는 스토어 이름, 값은 환불 조회의 기준 시각(가변 정수)
        /**
         * @brief 영수증 상태 표입니다(키 = 지급 분개 키 `rcpt.<스토어>/<거래 id>`, 값 `[판 번호][상태]`). 보존 정리가 지우지 않는다.
         * @details 거래 하나의 상태(지급 · 지급 전 환불 · 환불 회수 · 청약철회)가 레코드 하나라서 지급과 환불 · 철회가 서로 이 레코드의 버전을 커밋 조건으로 건다 —
         *          어느 쪽이 먼저 커밋해도 다른 쪽은 Conflict 로 다시 읽으므로 "환불된 구매의 지급" 이 통과하지 않는다(감사 O-1). 분개는 감사 내역이라 지워지고 이 레코드가 멱등 기록이다(감사 P0-7).
         *          지우지 않는 레코드라 받은 계정은 담지 않는다 — 탈퇴한 사람과 이어지지 않는다(받은 계정은 보존 기간이 있는 지급 분개에만 있다).
         */
        static const hashed_string& getReceiptTable();
        static const hashed_string& getDeliveryTable(); ///< "econ_delivery" — 스토어에 아직 알리지 못한 지급. 키는 지급 분개 키, 값은 `[판 번호][스토어][알림 키][기한]`
        static EconomyServiceResult toEconomyResult( LedgerResult result );

        /** @brief 검증 결과의 지급 알림 하나를 만듭니다. 결과에 알림 키가 없거나 거래 id 로 레코드 키를 만들 수 없으면 false 입니다. */
        [[nodiscard]] static bool makeDeliveryTask( const ReceiptValidationResult& receipt, ReceiptDeliveryTask& outTask );
        /** @brief 남은 지급 알림을 @p maxCount 까지 읽어 @p outListTask 뒤에 붙입니다. 읽을 수 없는 레코드는 오류 로그를 남기고 건너뜁니다. */
        [[nodiscard]] static ServiceStoreResult listDeliveries( IServiceStoreConnection& connection, int32 maxCount, vector<ReceiptDeliveryTask>& outListTask );
        /** @brief 끝난 지급 알림의 레코드를 지웁니다. */
        [[nodiscard]] static ServiceStoreResult eraseDelivery( IServiceStoreConnection& connection, string_view recordKey );

        /** @brief 스토어의 환불 조회 기준 시각을 읽습니다. 레코드가 없으면 `Ok` 와 0 입니다. 값을 읽을 수 없으면 오류 로그와 `Invalid` 입니다. */
        [[nodiscard]] static ServiceStoreResult readRefundCursor( IServiceStoreConnection& connection, string_view storeName, int64& outLastReadMs );
        /** @brief 스토어의 환불 조회 기준 시각을 씁니다. 서버 여럿이 조건 없이 쓴다 — 뒤처진 값이 이겨도 겹쳐 읽을 뿐이다. */
        [[nodiscard]] static ServiceStoreResult writeRefundCursor( IServiceStoreConnection& connection, string_view storeName, int64 lastReadMs );

        /** @brief 구매를 처리합니다. @p outReply 의 `_listBalance` 는 이 계정의 바뀐 잔액입니다. */
        static EconomyServiceResult purchase( IServiceStoreConnection& connection, const CurrencyCatalog& currencies, const OfferCatalog& offers, const EconomyPurchaseInput& input,
                                              EconomyPurchaseReply& outReply );
        /** @brief 검증이 끝난 영수증으로 지급합니다. 구매자가 이 계정이 아니면 `NotPurchaser` 입니다. */
        static EconomyServiceResult redeemReceipt( IServiceStoreConnection& connection, const CurrencyCatalog& currencies, const OfferCatalog& offers, const EconomyRedeemInput& input,
                                                   EconomyRedeemReply& outReply );
        /**
         * @brief 스토어 환불 알림으로 지급한 재화를 회수합니다. 사유는 "refund.revoke" 입니다.
         * @param outListChange 회수로 잔액이 바뀐 계정과 그 잔액입니다. 회수하지 않았으면(이미 환불했거나 지급 전이다) 비어 있습니다.
         * @details 영수증 상태 레코드(`getReceiptTable`)를 읽은 버전으로 바꾸는 쓰기와 회수 분개를 한 트랜잭션에 넣으므로 두 번 오면 아무것도 하지 않는다(AlreadyRefunded).
         *          지급 전에 환불이 먼저 오면 상태만 "지급 전 환불" 로 남기고, 뒤에 온 지급은 ReceiptRefunded 로 거절된다. 청약철회한 거래면 이미 돌려받았으므로 상태만 본다.
         *          지급 뒤면 지급 분개(`rcpt.<스토어>` 범위, 거래 id 토큰)의 다리를 거꾸로 소각으로 보내며, 양은 @p recovery 가 정한다(`Debt` 는 모두 · 빚 허용, `Unspent` 는 남은 잔액 안에서).
         *          지급 분개가 보존 정리로 지워졌으면 지급 내용을 모르므로 회수하지 않고 상태만 바꾸며 오류 로그를 남긴다.
         *          받은 계정이 탈퇴로 지워졌으면(`LedgerResult::AccountDeleted` — 잔액은 탈퇴 때 소각됐다) 회수하지 않고 상태만 바꾼다.
         */
        static EconomyServiceResult revokeRefund( IServiceStoreConnection& connection, const CurrencyCatalog& currencies, const ReceiptRefundNotification& notification,
                                                  EconomyRefundRecoveryMode recovery, int64 nowMs, vector<EconomyBalanceChange>& outListChange );
        /**
         * @brief 유상 구매를 청약철회합니다. 지급한 계정만, 기한 안에, 아직 쓰지 않은 만큼만 회수하며 빚은 만들지 않습니다. 회수한 양은 `_listWithdrawn` 입니다.
         * @details 사유는 "iap.withdraw" 이고 분개 키는 `withdraw.<스토어>` 범위의 거래 id 토큰이라 다시 와도 한 번만 적용된다. 실제 결제 환불은 스토어나 결제 대행이 회수한 양으로 한다.
         */
        static EconomyServiceResult withdrawPurchase( IServiceStoreConnection& connection, const CurrencyCatalog& currencies, const EconomyWithdrawInput& input,
                                                      EconomyWithdrawReply& outReply );
        static EconomyServiceResult readWallet( IServiceStoreConnection& connection, uint64 accountID, EconomyWalletReply& outReply );
        /**
         * @brief 탈퇴한 계정의 경제 키트 레코드를 지웁니다(`IAfterDeleteAccount`). 구매 수(`econ_purchase`)를 지운다.
         * @details 잔액은 탈퇴 정리가 원장에서 소각하고(`Ledger::burnDeletedAccounts`), 쓴 영수증(`econ_receipt`)은 처음부터 계정을 담지 않는다.
         */
        [[nodiscard]] static ServiceStoreResult eraseAccount( IServiceStoreConnection& connection, AccountID accountID, int64 nowMs );
        static EconomyServiceResult             readHistory( IServiceStoreConnection& connection, uint64 accountID, const EconomyHistoryRequest& request, EconomyHistoryReply& outReply );
        /** @brief 이동 결과에서 @p accountID 의 잔액만 골라냅니다. */
        static void collectAccountBalances( const LedgerTransferResult& serviceResult, uint64 accountID, vector<LedgerBalance>& outListBalance );
    };
} // namespace sw
