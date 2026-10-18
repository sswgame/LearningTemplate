// 경제 카탈로그 — 화폐(재원 순서 · 상한 · 잘못된 재원 버림) · 상품(가격 xor 상품 · 지급 필수 · 스토어 상품 찾기 · 판매 기간 반열림).
#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Economy/Shared/Catalog/CurrencyCatalog.h"
#include "GameFramework/Kits/Feature/Online/Economy/Shared/Catalog/OfferCatalog.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

SW_TEST_CASE( EconomyCatalogTest, CurrenciesLoadWithFundingOrderAndCaps )
{
    CurrencyCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXMLText( R"(<CurrencyCatalog>
        <Currency id="cur.gem"><Funding asset="cur.gem_free"/><Funding asset="cur.gem_paid"/></Currency>
        <Currency id="cur.gem_free" cap="5000"/>
        <Currency id="cur.gem_paid" paid="true"/>
    </CurrencyCatalog>)",
                                             "currency" ) );
    const CurrencyDef* pGem = catalog.findCurrency( "cur.gem" );
    SW_ASSERT_TRUE( pGem != nullptr );
    SW_EXPECT_TRUE( pGem->isVirtual() );
    SW_ASSERT_EQUAL( pGem->_listFundingAsset.size(), size_t( 2 ) );
    SW_EXPECT_EQUAL( pGem->_listFundingAsset[0], string( "cur.gem_free" ) ); // 무상 먼저 — 적힌 순서가 차감 순서
    SW_EXPECT_EQUAL( catalog.getBalanceCap( "cur.gem_free" ), int64( 5000 ) );
    SW_EXPECT_EQUAL( catalog.getBalanceCap( "cur.gem_paid" ), int64( 0 ) );
    SW_EXPECT_TRUE( catalog.findCurrency( "cur.gem_paid" )->_bPaid );
}

/**
 * @brief [EconomyCatalogTest] 없는 재원 · 깨진 상한 · 잘못된 id · 재원 없는 `<Funding>` 은 "상한 없음" 이나 빈 재원으로 물러나지 않고 그 화폐를 넣지 않는 오류다
 */
SW_TEST_CASE( EconomyCatalogTest, CurrenciesRejectBrokenElementsInsteadOfReadingDefaults )
{
    CurrencyCatalog catalog;
    {
        SW_TEST_DEFENSIVE_SCOPE( "broken currencies are rejected" );
        SW_ASSERT_TRUE( catalog.loadFromXMLText( R"(<CurrencyCatalog>
        <Currency id="cur.gold"/>
        <Currency id="cur.bad"><Funding asset="cur.none"/></Currency>
        <Currency id="cur.capless" cap="lots"/>
        <Currency id="Cur.Upper"/>
        <Currency id="cur.empty"><Funding/></Currency>
    </CurrencyCatalog>)",
                                                 "currency" ) );
    }
    SW_EXPECT_TRUE( catalog.findCurrency( "cur.gold" ) != nullptr );
    SW_EXPECT_TRUE( catalog.findCurrency( "cur.bad" ) == nullptr );
    SW_EXPECT_TRUE( catalog.findCurrency( "cur.capless" ) == nullptr );
    SW_EXPECT_TRUE( catalog.findCurrency( "Cur.Upper" ) == nullptr );
    SW_EXPECT_TRUE( catalog.findCurrency( "cur.empty" ) == nullptr );
}

SW_TEST_CASE( EconomyCatalogTest, OffersNeedGrantsAndEitherPriceOrProduct )
{
    OfferCatalog catalog;
    {
        SW_TEST_DEFENSIVE_SCOPE( "malformed offers are rejected" );
        SW_ASSERT_TRUE( catalog.loadFromXMLText( R"(<OfferCatalog>
        <Offer id="starter" limit="1"><Price currency="cur.gem" amount="100"/><Grant asset="item.sword" amount="1"/></Offer>
        <Offer id="gem_100"><Product store="fake" id="com.example.gem100"/><Grant asset="cur.gem_paid" amount="100"/></Offer>
        <Offer id="both"><Price currency="cur.gold" amount="1"/><Product store="fake" id="x"/><Grant asset="cur.gold" amount="1"/></Offer>
        <Offer id="nogrant"><Price currency="cur.gold" amount="1"/></Offer>
        <Offer id="zero"><Price currency="cur.gold" amount="0"/><Grant asset="cur.gold" amount="1"/></Offer>
        <Offer id="nostore"><Product id="x"/><Grant asset="cur.gold" amount="1"/></Offer>
        <Offer id="badstart" startMs="soon"><Price currency="cur.gold" amount="1"/><Grant asset="cur.gold" amount="1"/></Offer>
    </OfferCatalog>)",
                                                 "offer" ) );
    }
    SW_EXPECT_EQUAL( catalog.getOffers().size(), size_t( 2 ) );
    SW_EXPECT_EQUAL( catalog.findOffer( "starter" )->_limitPerAccount, 1 );
    SW_EXPECT_TRUE( catalog.findOffer( "gem_100" )->isRealMoney() );
}

SW_TEST_CASE( EconomyCatalogTest, FindsOfferByStoreProduct )
{
    OfferCatalog catalog;
    OfferDef     def;
    def._id = "gem_500";
    def._listProduct.push_back( OfferProduct{ "fake", "com.example.gem500" } );
    def._listProduct.push_back( OfferProduct{ "apple", "com.example.ios.gem500" } );
    def._listGrant.push_back( OfferGrant{ "cur.gem_paid", 500 } );
    SW_ASSERT_TRUE( catalog.addOffer( def ) );
    SW_EXPECT_FALSE( catalog.addOffer( def ) ); // 같은 id 둘
    SW_EXPECT_TRUE( catalog.findOfferByProduct( "apple", "com.example.ios.gem500" ) != nullptr );
    SW_EXPECT_TRUE( catalog.findOfferByProduct( "fake", "com.example.ios.gem500" ) == nullptr );
}

SW_TEST_CASE( EconomyCatalogTest, SaleWindowIsHalfOpen )
{
    OfferDef def;
    def._startMs = 100;
    def._endMs   = 200;
    SW_EXPECT_FALSE( def.isOnSale( 99 ) );
    SW_EXPECT_TRUE( def.isOnSale( 100 ) );
    SW_EXPECT_TRUE( def.isOnSale( 199 ) );
    SW_EXPECT_FALSE( def.isOnSale( 200 ) );
}
