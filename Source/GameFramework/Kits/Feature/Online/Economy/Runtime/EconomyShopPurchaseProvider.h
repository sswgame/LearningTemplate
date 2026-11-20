/**
 * @file EconomyShopPurchaseProvider.h
 * @brief 온라인 상점의 구매 공급자입니다. 경제 키트 클라이언트로 서버 구매(`EconomyMethod::Purchase`)를 맡기고, 서버 답을 요청과 대조한 뒤 상점 화면(`ShopScreen`)에 알립니다.
 * @details 같은 상점 화면에 오프라인 공급자(`LocalShopPurchaseProvider`) 대신 꽂는다(합성). 지갑은 서버 원장이 정본이고 클라이언트는 사본만 든다 — 실패해도 되돌리지 않는다.
 *          **서버 답을 믿지 않는다.** 성공 답이어도 바뀐 잔액이 이 상품의 자산(값 화폐 · 지급 자산)만 담고, 범위(`LedgerConstant::kMaxAmount`) 안이며, 값 화폐가 정확히 값 × 수만큼 줄고
 *          지급 자산이 지급 × 수만큼 늘었는지 본다. 어긋나면 `Unverified` 로 알리고 지갑 전체를 다시 받는다(`GetWallet` 스냅샷 — 같은 사이의 다른 키트 알림도 이 길로 맞는다).
 *          **중복 누름.** 답을 기다리는 구매가 있으면 새 구매는 `Busy` 로 바로 답한다. 재시도는 같은 멱등 키로만 하므로 서버는 한 번만 적용한다(영수증 id = 그 키).
 *          **목록은 클라이언트 카탈로그를 믿지 않는다.** 화면이 넘긴 상품이 아니라 카탈로그의 id 로 다시 찾은 정의로 값을 셈한다. 결제 상품(`<Product>`)과 값이 둘 이상인 상품은 이 화면에 싣지 않는다.
 *          유니티 Gaming Services Economy 의 `MakeVirtualPurchaseAsync` 와 같은 자리다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Economy/Runtime/EconomyProtocol.h"
#include "GameFramework/Runtime/Gameplay/Inventory/ShopPurchase.h"
#include "GameFramework/Runtime/Online/Ledger/LedgerTypes.h"
#include "GameFramework/Runtime/Online/Service/OnlineProtocol.h"

namespace sw
{
    class EconomyClient;
    class OfferCatalog;
    struct OfferDef;

    /**
     * @class EconomyShopPurchaseProvider
     * @brief 경제 키트 서버 구매의 공급자입니다. 클라이언트와 카탈로그는 빌려 쓰며 이 객체보다 오래 살아야 합니다. 이 객체가 먼저 사라지면 늦게 온 답은 버립니다.
     */
    class SW_KIT_API EconomyShopPurchaseProvider final : public IShopPurchaseProvider
    {
    public:
        EconomyShopPurchaseProvider( EconomyClient& client, const OfferCatalog& offers );
        ~EconomyShopPurchaseProvider() override;

        void   collectOffers( vector<ShopOffer>& outListOffer ) const override;
        int64  getBalance( const hashed_string& currency ) const override;
        uint32 getRevision() const override;
        bool   isPurchasePending() const override;
        void   beginPurchase( const ShopOffer& offer, int32 count, const ShopPurchaseDelegate& onFinished ) override;

        /** @brief 상품 정의 @p def 가 이 화면에 실을 수 있는 것(값 하나 · 결제 상품 아님 · 범위 안)이면 그 줄을 @p outOffer 에 씁니다. */
        [[nodiscard]] static bool makeOffer( const OfferDef& def, ShopOffer& outOffer );
        /**
         * @brief 서버 답 @p result 를 판정합니다. @p listBefore 는 보내기 전 클라이언트 사본의 잔액(상품의 자산마다)입니다.
         * @details 공통 오류는 `ServerError`, 업무 결과는 상태로 옮기고, 성공 답은 위 머리말의 대조를 거쳐 `Succeeded` 또는 `Unverified` 입니다.
         */
        [[nodiscard]] static ShopPurchaseStatus evaluateReply( const OfferDef& def, int32 count, const vector<LedgerBalance>& listBefore, const OnlineResult<EconomyPurchaseReply>& result );

    private:
        struct PurchaseState;

        /** @brief 답을 받은 상태입니다(늦게 온 답은 약한 참조가 끊겨 버린다). */
        static void onReply( const weak_ptr<PurchaseState>& weakState, const OnlineResult<EconomyPurchaseReply>& result );

    private:
        shared_ptr<PurchaseState> _state;
    };
} // namespace sw
