/**
 * @file EconomyStoreLogic.h
 * @brief 경제 서비스의 저장 부분입니다. 저장소 스레드(`IServiceStoreWork::run`)에서 부르는 동기 함수이며 서비스 멤버는 모릅니다. 입력은 값으로 받습니다.
 * @details - 구매는 원장 이동 한 건과 계정당 구매 수 레코드(`econ_purchase`, 한도가 있을 때)를 한 트랜잭션으로 처리한다. 원장 이동은 계정에서 소각으로 가격을 내고, 발행에서 계정으로 지급하는 것이다.
 *            분개 키는 클라이언트의 멱등 키이므로, 응답을 잃고 재시도하면 **잔액과 한도 판정보다 먼저** 지난 결과를 받는다.
 *          - 가상 화폐의 가격은 재원 자산의 현재 잔액을 적힌 순서대로 나눠서 분개 항목을 만든다. 무상 재화를 먼저 쓰는 것이 관례이다.
 *            커밋 사이에 잔액이 바뀌면 버전 충돌이 나므로 다시 짠다. 재원 중 하나라도 빚(환불 회수로 생긴 음수)이면 그 가상 화폐로는 살 수 없으며(`InsufficientFunds`) 빚을 먼저 갚아야 한다.
 *          - 영수증 지급의 분개 키는 거래 id(`rcpt.<스토어>/<거래 id 16진>`)이다. 그래서 같은 영수증은 어느 계정으로든 한 번만 지급된다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Online/Ledger/LedgerTypes.h"
#include "GameFramework/Base/Online/Store/ServiceStore.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Economy/Server/Receipt/ReceiptValidator.h"
#include "GameFramework/Kits/Feature/Online/Economy/Shared/Protocol/EconomyProtocol.h"

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
        ReceiptValidationResult _receipt{};
        uint64                  _accountID{ 0 };
        int64                   _nowMs{ 0 };
        uint8                   _bAcceptSandbox{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /** @brief 경제 저장 함수 모음입니다. 저장소 스레드에서 부릅니다. */
    struct SW_KIT_SERVER_API EconomyStoreLogic
    {
        static const hashed_string& getPurchaseCountTable(); ///< "econ_purchase" — 키 `<계정 16 진>/<상품 id>`
        static EconomyResult        toEconomyResult( LedgerResult result );

        /** @brief 구매를 처리합니다. @p outReply 의 `_listBalance` 는 이 계정의 바뀐 잔액입니다. */
        static EconomyResult purchase( IServiceStoreConnection& connection, const CurrencyCatalog& currencies, const OfferCatalog& offers, const EconomyPurchaseInput& input,
                                       EconomyReply& outReply );
        /** @brief 검증이 끝난 영수증으로 지급합니다. */
        static EconomyResult redeemReceipt( IServiceStoreConnection& connection, const CurrencyCatalog& currencies, const OfferCatalog& offers, const EconomyRedeemInput& input,
                                            EconomyReply& outReply );
        static EconomyResult readWallet( IServiceStoreConnection& connection, uint64 accountID, EconomyReply& outReply );
        static EconomyResult readHistory( IServiceStoreConnection& connection, uint64 accountID, const EconomyHistoryRequest& request, EconomyReply& outReply );
        /** @brief 이동 결과에서 @p accountID 의 잔액만 골라냅니다. */
        static void collectAccountBalances( const LedgerTransferOutcome& outcome, uint64 accountID, vector<LedgerBalance>& outListBalance );
    };
} // namespace sw
