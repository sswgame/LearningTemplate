/**
 * @file StorePurchaseFlow.h
 * @brief 스토어 결제의 클라이언트 길입니다. 구매 요청에 계정 토큰을 싣고, 스토어가 낸 영수증을 서버에 내어 지급받고, 지급되면 스토어 거래를 마칩니다.
 * @details 순서: `begin` 이 `ReceiptBuyerToken` 을 만들어 다리(`IStoreBillingBridge`)에 넘긴다 → 스토어 결제 → 다리가 영수증을 낸다 → `update` 가 서버에 낸다(`EconomyMethod::RedeemReceipt`) →
 *          지급(Ok)이면 다리의 `finishPurchase`(구글 소비 · 확인, 애플 `finish`).
 *          서버가 토큰을 대조하므로(`ReceiptBuyerBinding::AccountToken`) 다리가 토큰을 빠뜨리면 결제는 됐는데 지급이 거절된다(`EconomyResult::BuyerTokenMissing`, 양쪽 로그가 원인을 말한다).
 *          토큰을 만드는 곳을 이 흐름 하나로 두어 게임 코드가 빠뜨릴 수 없게 한다 — 다리의 요청 구조체에 토큰이 필수로 온다.
 *          거절된 거래는 마치지 않는다 — 스토어가 다음 기동에 다시 내므로 맞는 계정으로 로그인했을 때 지급된다. 구글은 그대로 두면 3 일 뒤 환불된다.
 *          외부 SDK 는 링크하지 않는다(`IPlatformServices` 와 같은 원칙). SDK 를 쥔 쪽(플랫폼 모듈)이 다리를 구현한다 — 언리얼 `IOnlinePurchase::Checkout`, 유니티 IAP 의 `SetObfuscatedAccountId` · `SetAppAccountToken` 자리다(기억에 의존).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Types.h"
#include "Core/Network/Message/NetRequest.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Economy/Runtime/API/EconomyClient.h"
#include "GameFramework/Kits/Feature/Online/Economy/Runtime/Protocol/EconomyProtocol.h"

namespace sw
{
    /** @brief 스토어 결제 하나의 끝 상태입니다. */
    enum class StoreBillingStatus : uint8
    {
        Purchased = 0, ///< 결제됐다. `_receiptPayload` 가 있다
        Pending,       ///< 결제 수단을 기다린다(구글의 보류 구매 등). 끝나면 다리가 다시 낸다
        Cancelled,     ///< 사용자가 취소했다
        Failed         ///< 스토어가 실패했다. `_failureText` 가 있다
    };
} // namespace sw

namespace sw
{
    /** @brief 다리에 넘기는 구매 요청입니다. */
    struct StoreBillingRequest
    {
        string _productID{};  ///< 스토어 상품 id(`OfferCatalog` 의 `<Product>`)
        string _buyerToken{}; ///< `ReceiptBuyerToken`. 구글 `BillingFlowParams.Builder.setObfuscatedAccountId`, 애플 `Product.PurchaseOption.appAccountToken( UUID )` 에 그대로 넣는다
    };

    /** @brief 다리가 내는 결과 하나입니다. */
    struct StoreBillingResult
    {
        string             _receiptPayload{}; ///< 서버 제공자가 받는 영수증 글. 구글은 `<상품 id>|<구매 토큰>`, 애플은 거래 id(십진 숫자)다
        string             _failureText{};    ///< 로그용 문자열이며 영어로 쓴다
        StoreBillingStatus _status{ StoreBillingStatus::Failed };
    };
} // namespace sw

namespace sw
{
    /**
     * @class IStoreBillingBridge
     * @brief 플랫폼 결제 SDK 를 쥔 쪽이 구현하는 다리입니다. 게임 스레드에서만 부릅니다.
     */
    class IStoreBillingBridge
    {
    public:
        IStoreBillingBridge()          = default;
        virtual ~IStoreBillingBridge() = default;

        IStoreBillingBridge( const IStoreBillingBridge& )            = delete;
        IStoreBillingBridge& operator=( const IStoreBillingBridge& ) = delete;

        /** @brief 스토어 이름입니다. 서버 제공자의 이름(`IReceiptValidator::getStoreName`)과 같은 글입니다. */
        virtual const utf8* getStoreName() const = 0;
        /** @brief 결제 화면을 엽니다. 스토어를 쓸 수 없으면 false 입니다. 결과는 `pollResults` 로 나옵니다. */
        [[nodiscard]] virtual bool beginPurchase( const StoreBillingRequest& request ) = 0;
        /** @brief 끝난 결제를 @p outListResult 뒤에 붙입니다. 앱을 다시 켰을 때 스토어에 남은(마치지 않은) 구매도 여기로 냅니다 — 지급 전에 꺼진 결제를 잇는 길이다. */
        virtual void pollResults( vector<StoreBillingResult>& outListResult ) = 0;
        /** @brief 지급이 끝난 거래를 마칩니다(구글 `consumeAsync` · `acknowledgePurchase`, 애플 `Transaction.finish`). */
        virtual void finishPurchase( string_view receiptPayload ) = 0;
    };
} // namespace sw

namespace sw
{
    /** @brief 구매 하나의 끝을 게임에 알립니다. */
    struct StorePurchaseReport
    {
        EconomyRedeemRequest _redeem{};      ///< 서버에 낸 영수증이다(`_billing` 이 Purchased 일 때)
        string               _failureText{}; ///< 스토어가 실패했을 때의 까닭(영어)
        NetIdempotencyKey    _key{};         ///< 다시 낼 때 그대로 쓴다
        StoreBillingStatus   _billing{ StoreBillingStatus::Failed };
        EconomyServiceResult _result{ EconomyServiceResult::fail( OnlineError::kUnavailable ) }; ///< 서버의 지급 결과(`_billing` 이 Purchased 일 때). 공통 범주(장애 · 충돌 · 로그인)는 `_errorCode`
        uint8                _bRetryable{ SW_FALSE };                                            ///< 같은 영수증으로 다시 내면 된다(`retryRedeem`)
    };
} // namespace sw

namespace sw
{
    /**
     * @class StorePurchaseFlow
     * @brief 스토어 결제 흐름입니다. 다리와 경제 클라이언트는 빌려 쓰며 이 객체보다 오래 살아야 합니다.
     */
    class SW_KIT_API StorePurchaseFlow
    {
    public:
        using ReportDelegate = Delegate<void( const StorePurchaseReport& )>;

        StorePurchaseFlow( IStoreBillingBridge& bridge, EconomyClient& client, const ReportDelegate& onReport );

        StorePurchaseFlow( const StorePurchaseFlow& )            = delete;
        StorePurchaseFlow& operator=( const StorePurchaseFlow& ) = delete;

        /** @brief 계정 @p accountID 로 @p productID 의 결제를 시작합니다. 로그인 전(계정 0) · 빈 상품이면 경고와 false 입니다 — 토큰 없이 결제하면 돈만 나가고 지급되지 않는다. */
        [[nodiscard]] bool begin( uint64 accountID, string_view productID );
        /** @brief 프레임마다 부릅니다. 다리의 결과를 거두어 영수증을 서버에 내고, 나머지는 바로 알립니다. */
        void update();
        /** @brief 다시 낼 수 있는 결과(`_bRetryable`)의 영수증을 같은 멱등 키로 다시 냅니다. 아니면 아무것도 하지 않습니다. */
        void retryRedeem( const StorePurchaseReport& finished );

    private:
        struct PendingRedeem
        {
            EconomyRedeemRequest _request{};
            NetIdempotencyKey    _key{};
        };

        void redeem( const EconomyRedeemRequest& request, const NetIdempotencyKey& key );
        void onRedeemReply( const OnlineResult<EconomyRedeemReply>& reply );
        void notifyReport( const StorePurchaseReport& finished );

    private:
        vector<PendingRedeem> _listPending;
        ReportDelegate        _onReport;
        IStoreBillingBridge&  _bridge;
        EconomyClient&        _client;
    };
} // namespace sw
