/**
 * @file EconomyStoreLogic.h
 * @brief 경제 서비스의 저장 부분 — 저장소 스레드(`IServiceStoreWork::run`)에서 부르는 동기 함수입니다. 서비스 멤버를 모른다(입력은 값으로).
 * @details - 구매 = 원장 이동 한 건(계정 → 소각 가격, 발행 → 계정 지급) + 계정당 구매 수 레코드(`econ_purchase`, 한도가 있을 때), 한 트랜잭션.
 *            분개 키는 클라이언트 멱등 키라 응답을 잃은 재시도는 **잔액 · 한도 판정보다 먼저** 지난 결과를 받는다.
 *          - 가상 화폐 가격은 재원 자산의 지금 잔액을 적힌 순서대로 나눠 다리를 만든다(무상 먼저가 관례) — 커밋 사이에 잔액이 바뀌면 판 충돌로 다시 짠다.
 *            재원 하나라도 빚(환불 회수의 음수)이면 그 가상 화폐로는 사지 못한다(`InsufficientFunds` — 빚을 먼저 갚는다).
 *          - 영수증 지급의 분개 키는 거래 id(`rcpt.<스토어>/<거래 id 16 진>`)라 같은 영수증은 어느 계정으로든 한 번이다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Online/Ledger/LedgerTypes.h"
#include "GameFramework/Base/Online/Store/ServiceStore.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Online/Economy/EconomyProtocol.h"
#include "GameFramework/Kits/Online/Server/Economy/Receipt/ReceiptValidator.h"

namespace sw
{
    class CurrencyCatalog;
    class OfferCatalog;

    /** @brief 구매 입력입니다. */
    struct EconomyPurchaseInput
    {
        string _offerId{};
        string _journalKey{}; ///< `LedgerJournalKey::makeFromIdempotency( makeAccountScope( 계정 ), 멱등 키 )`
        uint64 _accountId{ 0 };
        int64  _nowMs{ 0 };
        int32  _count{ 1 };
    };
} // namespace sw

namespace sw
{
    /** @brief 영수증 지급 입력입니다(검증이 끝난 결과). */
    struct EconomyRedeemInput
    {
        ReceiptValidationResult _receipt{};
        uint64                  _accountId{ 0 };
        int64                   _nowMs{ 0 };
        uint8                   _bAcceptSandbox{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /** @brief 경제 저장 함수입니다(저장소 스레드). */
    struct SW_GF_API EconomyStoreLogic
    {
        static const hashed_string& getPurchaseCountTable(); ///< "econ_purchase" — 키 `<계정 16 진>/<상품 id>`
        static EconomyResult        toEconomyResult( LedgerResult result );

        /** @brief 삽니다. @p outReply 의 `_listBalance` 는 이 계정의 바뀐 잔액입니다. */
        static EconomyResult purchase( IServiceStoreConnection& connection, const CurrencyCatalog& currencies, const OfferCatalog& offers, const EconomyPurchaseInput& input,
                                       EconomyReply& outReply );
        /** @brief 검증이 끝난 영수증으로 지급합니다. */
        static EconomyResult redeemReceipt( IServiceStoreConnection& connection, const CurrencyCatalog& currencies, const OfferCatalog& offers, const EconomyRedeemInput& input,
                                            EconomyReply& outReply );
        static EconomyResult readWallet( IServiceStoreConnection& connection, uint64 accountId, EconomyReply& outReply );
        static EconomyResult readHistory( IServiceStoreConnection& connection, uint64 accountId, const EconomyHistoryRequest& request, EconomyReply& outReply );
        /** @brief 이동 결과에서 @p accountId 의 잔액만 고릅니다. */
        static void collectAccountBalances( const LedgerTransferOutcome& outcome, uint64 accountId, vector<LedgerBalance>& outListBalance );
    };
} // namespace sw
