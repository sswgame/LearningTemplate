// EconomyShopProviderTest — 온라인 상점 공급자의 서버 답 판정: 성공 답의 값 · 지급 대조, 무관한 자산 · 어긋난 차이 · 범위 밖은 Unverified, 업무 결과 · 공통 오류의 상태,
// 화면에 실을 상품 고르기(결제 상품 · 값 둘 제외). 온라인 키트라 `UnitTests.OnlineKits` 가 돌린다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Kits/Feature/Online/Economy/Runtime/EconomyShopPurchaseProvider.h"
#include "GameFramework/Kits/Feature/Online/Economy/Runtime/OfferCatalog.h"

using namespace sw;

namespace
{
    /** @brief 금화 100 으로 보석 1 을 사는 상품입니다. */
    OfferDef makeGemOffer()
    {
        OfferDef def{};
        def._id = "gem_from_gold";
        OfferPrice price{};
        price._currencyID = "cur.gold";
        price._amount     = 100;
        def._listPrice.push_back( price );
        OfferGrant grant{};
        grant._assetID = "cur.gem";
        grant._amount  = 1;
        def._listGrant.push_back( grant );
        def._maxCountPerPurchase = 5;
        return def;
    }

    LedgerBalance makeBalance( const utf8* pAssetID, int64 amount )
    {
        LedgerBalance balance{};
        balance._assetID = pAssetID;
        balance._amount  = amount;
        return balance;
    }

    /** @brief 금화 250 · 보석 0 에서 산 사본의 잔액입니다. */
    vector<LedgerBalance> makeBefore()
    {
        return vector<LedgerBalance>{ makeBalance( "cur.gold", 250 ), makeBalance( "cur.gem", 0 ) };
    }

    OnlineResult<EconomyPurchaseReply> makeReply( vector<LedgerBalance> listBalance, EconomyResult result = EconomyResult::Ok )
    {
        OnlineResult<EconomyPurchaseReply> reply{};
        reply._reply._listBalance = std::move( listBalance );
        reply._reply._result      = result;
        return reply;
    }
} // namespace

/** @brief [EconomyShopProviderTest] 값 × 수만큼 줄고 지급 × 수만큼 는 성공 답만 Succeeded 다 */
SW_TEST_CASE( EconomyShopProviderTest, SucceededOnlyWhenTheReplyMatchesTheOffer )
{
    const OfferDef def = makeGemOffer();
    SW_EXPECT_TRUE( EconomyShopPurchaseProvider::evaluateReply( def, 2, makeBefore(), makeReply( { makeBalance( "cur.gold", 50 ), makeBalance( "cur.gem", 2 ) } ) ) ==
                    ShopPurchaseStatus::Succeeded );
    // 값이 덜 빠졌다 · 지급이 더 왔다 · 무관한 자산이 섞였다 · 값 화폐가 답에 없다 · 범위 밖 — 모두 대조 실패
    SW_EXPECT_TRUE( EconomyShopPurchaseProvider::evaluateReply( def, 2, makeBefore(), makeReply( { makeBalance( "cur.gold", 150 ), makeBalance( "cur.gem", 2 ) } ) ) ==
                    ShopPurchaseStatus::Unverified );
    SW_EXPECT_TRUE( EconomyShopPurchaseProvider::evaluateReply( def, 1, makeBefore(), makeReply( { makeBalance( "cur.gold", 150 ), makeBalance( "cur.gem", 9 ) } ) ) ==
                    ShopPurchaseStatus::Unverified );
    SW_EXPECT_TRUE( EconomyShopPurchaseProvider::evaluateReply( def, 1, makeBefore(), makeReply( { makeBalance( "cur.gold", 150 ), makeBalance( "cur.vip", 1 ) } ) ) ==
                    ShopPurchaseStatus::Unverified );
    SW_EXPECT_TRUE( EconomyShopPurchaseProvider::evaluateReply( def, 1, makeBefore(), makeReply( { makeBalance( "cur.gem", 1 ) } ) ) == ShopPurchaseStatus::Unverified );
    SW_EXPECT_TRUE( EconomyShopPurchaseProvider::evaluateReply( def, 1, makeBefore(), makeReply( { makeBalance( "cur.gold", LedgerConstant::kMaxAmount + 1 ) } ) ) ==
                    ShopPurchaseStatus::Unverified );
}

/** @brief [EconomyShopProviderTest] 업무 결과와 공통 오류는 상태로 옮기고, 클라이언트는 재화를 되돌리지 않는다(답만 옮긴다) */
SW_TEST_CASE( EconomyShopProviderTest, BusinessResultsAndErrorsMapToStatus )
{
    const OfferDef def = makeGemOffer();
    SW_EXPECT_TRUE( EconomyShopPurchaseProvider::evaluateReply( def, 1, makeBefore(), makeReply( {}, EconomyResult::InsufficientFunds ) ) == ShopPurchaseStatus::NotEnoughMoney );
    SW_EXPECT_TRUE( EconomyShopPurchaseProvider::evaluateReply( def, 1, makeBefore(), makeReply( {}, EconomyResult::LimitReached ) ) == ShopPurchaseStatus::LimitReached );
    SW_EXPECT_TRUE( EconomyShopPurchaseProvider::evaluateReply( def, 1, makeBefore(), makeReply( {}, EconomyResult::NotOnSale ) ) == ShopPurchaseStatus::Rejected );
    OnlineResult<EconomyPurchaseReply> unavailable = makeReply( {} );
    unavailable._errorCode                         = OnlineError::kUnavailable;
    SW_EXPECT_TRUE( EconomyShopPurchaseProvider::evaluateReply( def, 1, makeBefore(), unavailable ) == ShopPurchaseStatus::ServerError );
}

/** @brief [EconomyShopProviderTest] 화면에는 값 하나인 가상 화폐 상품만 싣는다 — 결제 상품 · 값 둘 · 지급 없음은 뺀다 */
SW_TEST_CASE( EconomyShopProviderTest, OnlyOneVirtualPriceOffersAreShown )
{
    ShopOffer offer{};
    SW_REQUIRE_TRUE( EconomyShopPurchaseProvider::makeOffer( makeGemOffer(), offer ) );
    SW_EXPECT_EQUAL( string( "gem_from_gold" ), offer._offerID );
    SW_EXPECT_EQUAL( int64( 100 ), offer._unitPrice );
    SW_EXPECT_EQUAL( 5, offer._maxCount );
    SW_EXPECT_TRUE( offer._currency == hashed_string( "cur.gold" ) );

    OfferDef realMoney = makeGemOffer();
    realMoney._listPrice.clear();
    OfferProduct product{};
    product._storeName = "apple";
    product._productID = "com.x.gem";
    realMoney._listProduct.push_back( product );
    SW_EXPECT_FALSE( EconomyShopPurchaseProvider::makeOffer( realMoney, offer ) );

    OfferDef twoPrices = makeGemOffer();
    twoPrices._listPrice.push_back( twoPrices._listPrice[0] );
    SW_EXPECT_FALSE( EconomyShopPurchaseProvider::makeOffer( twoPrices, offer ) );
}
