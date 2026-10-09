#include "pch.h"

#include "GameFramework/Base/Inventory/Inventory.h"
#include "GameFramework/Base/Inventory/ItemCatalog.h"
#include "GameFramework/Base/Inventory/Shop.h"

#include "TestFramework/TestFramework.h"

// 장르 공통 지갑과 가게 — 여러 통화, 사기 · 팔기, 재고와 재입고, 돈 부족 · 자리 없음 · 잠금 조건 보관과 평가, 사들이지 않는 분류, 시세 하락과 날마다 회복, 가격 배율.

using namespace sw;

namespace
{
    constexpr const utf8* kShopItemXml = R"(
<ItemCatalog>
  <Item id="potion" category="Consumable" maxStack="10" value="12"/>
  <Item id="ether" category="Consumable" maxStack="10" value="40"/>
  <Item id="sword" category="Weapon" value="100"/>
  <Item id="key" category="Key" value="50"/>
  <Item id="scrap" category="Scrap" maxStack="99" value="20"/>
</ItemCatalog>
)";

    constexpr const utf8* kShopXml = R"(
<ShopCatalog>
  <Shop id="general" buyMultiplier="1" sellMultiplier="0.5" restockDays="2" refuses="Key">
    <Stock item="potion" price="20" count="5" restock="3"/>
    <Stock item="ether" count="2"/>
    <Stock item="sword" price="150" requires="flag:chapter2 &amp;&amp; !flag:banned"/>
  </Shop>
  <Shop id="company" currency="Credits" sellMultiplier="1" saturation="0.1" minSellFactor="0.4" recovery="0.15" restockDays="0"/>
</ShopCatalog>
)";

    /** @brief "flag:chapter2 ..." 를 받으면 열림 여부를 그대로 돌려주는 시험용 평가기입니다. */
    struct TestConditionEvaluator final : public IShopConditionEvaluator
    {
        bool isConditionMet( string_view expression ) const override
        {
            _lastExpression = string( expression.data(), expression.size() );
            return _bOpen;
        }

        mutable string _lastExpression{};
        bool           _bOpen{ false };
    };

    struct ShopScene
    {
        ItemCatalog _itemCatalog;
        ShopCatalog _shopCatalog;
        ShopState   _shop;
        Inventory   _inventory;
        Wallet      _wallet;
        bool        _bLoaded{ false };

        ShopScene()
        {
            _bLoaded = _itemCatalog.loadFromXmlText( kShopItemXml, "ShopTest" ) && _shopCatalog.loadFromXmlText( kShopXml, "ShopTest" );
            _shop.initialize( &_shopCatalog, &_itemCatalog );
            _inventory.initialize( &_itemCatalog, 4 );
        }
    };

    hashed_string idGeneral() { return hashed_string( "general" ); }
    hashed_string idCompany() { return hashed_string( "company" ); }
    hashed_string idPotion() { return hashed_string( "potion" ); }
    hashed_string idEther() { return hashed_string( "ether" ); }
    hashed_string idSword() { return hashed_string( "sword" ); }
    hashed_string idScrap() { return hashed_string( "scrap" ); }
    hashed_string idGold() { return hashed_string( "Gold" ); }
    hashed_string idCredits() { return hashed_string( "Credits" ); }
} // namespace

SW_TEST_CASE( ShopTest, WalletKeepsSeveralCurrenciesAndNeverGoesNegative )
{
    Wallet wallet;
    SW_EXPECT_TRUE( Wallet::getDefaultCurrency() == idGold() );
    wallet.add( idGold(), 100 );
    wallet.add( idCredits(), 7 );
    wallet.add( idGold(), -50 ); // 음수 더하기는 무시 — 쓰기는 trySpend 하나로
    SW_EXPECT_EQUAL( 100, wallet.getBalance( idGold() ) );
    SW_EXPECT_EQUAL( 7, wallet.getBalance( idCredits() ) );
    SW_EXPECT_EQUAL( 0, wallet.getBalance( hashed_string( "Souls" ) ) );

    SW_EXPECT_TRUE( wallet.trySpend( idGold(), 101 ) == false );
    SW_EXPECT_EQUAL( 100, wallet.getBalance( idGold() ) ); // 모자라면 그대로
    SW_EXPECT_TRUE( wallet.trySpend( idGold(), 100 ) );
    SW_EXPECT_EQUAL( 0, wallet.getBalance( idGold() ) );
    SW_EXPECT_TRUE( wallet.trySpend( idCredits(), 8 ) == false );

    vector<WalletEvent> listEvent;
    wallet.drainEvents( listEvent );
    SW_ASSERT_TRUE( listEvent.size() == 3 ); // +100 · +7 · −100 (실패한 쓰기 · 무시된 더하기는 알리지 않는다)
    SW_EXPECT_EQUAL( -100, listEvent[2]._delta );
    SW_EXPECT_EQUAL( 0, listEvent[2]._balance );
    SW_EXPECT_TRUE( wallet.getBalances()[0]._currency == idGold() ); // 처음 만난 순서
    listEvent.clear();
    wallet.drainEvents( listEvent );
    SW_EXPECT_TRUE( listEvent.empty() );
}

SW_TEST_CASE( ShopTest, BuyTakesMoneyAndStockAndFallsBackToItemValue )
{
    ShopScene scene;
    SW_ASSERT_TRUE( scene._bLoaded );
    const ShopDef* pGeneral = scene._shopCatalog.findShop( idGeneral() );
    SW_ASSERT_NOT_NULL( pGeneral );
    SW_EXPECT_TRUE( pGeneral->_currency == idGold() ); // 통화를 적지 않으면 Gold
    SW_EXPECT_EQUAL( 3, static_cast<int32>( pGeneral->_listStock.size() ) );

    SW_EXPECT_EQUAL( 20, scene._shop.computeBuyPrice( idGeneral(), idPotion() ) );
    SW_EXPECT_EQUAL( 40, scene._shop.computeBuyPrice( idGeneral(), idEther() ) ); // 가격이 없으면 ItemDef::_value
    SW_EXPECT_EQUAL( -1, scene._shop.computeBuyPrice( idGeneral(), idScrap() ) ); // 팔지 않는다

    scene._wallet.add( idGold(), 100 );
    SW_EXPECT_TRUE( scene._shop.buy( idGeneral(), idPotion(), 3, scene._wallet, scene._inventory ) == ShopResult::Ok );
    SW_EXPECT_EQUAL( 40, scene._wallet.getBalance( idGold() ) );
    SW_EXPECT_EQUAL( 3, scene._inventory.getItemCount( idPotion() ) );
    SW_EXPECT_EQUAL( 2, scene._shop.getStockCount( idGeneral(), idPotion() ) );

    SW_EXPECT_TRUE( scene._shop.buy( idGeneral(), idScrap(), 1, scene._wallet, scene._inventory ) == ShopResult::UnknownItem );
    SW_EXPECT_TRUE( scene._shop.buy( hashed_string( "nowhere" ), idPotion(), 1, scene._wallet, scene._inventory ) == ShopResult::UnknownShop );
    SW_EXPECT_TRUE( scene._shop.buy( idGeneral(), idPotion(), 0, scene._wallet, scene._inventory ) == ShopResult::InvalidCount );

    vector<ShopEvent> listEvent;
    scene._shop.drainEvents( listEvent );
    SW_ASSERT_TRUE( listEvent.size() == 1 );
    SW_EXPECT_TRUE( listEvent[0]._kind == ShopEvent::Kind::Bought );
    SW_EXPECT_EQUAL( 60, listEvent[0]._money );
    SW_EXPECT_EQUAL( 3, listEvent[0]._count );
}

SW_TEST_CASE( ShopTest, FailedBuyChangesNothing )
{
    ShopScene scene;
    SW_ASSERT_TRUE( scene._bLoaded );
    scene._wallet.add( idGold(), 50 );

    // 재고 — 다섯 개뿐이다.
    SW_EXPECT_TRUE( scene._shop.evaluateBuy( idGeneral(), idPotion(), 6, scene._wallet, scene._inventory ) == ShopResult::OutOfStock );
    // 돈 — 20 × 3 = 60 > 50.
    SW_EXPECT_TRUE( scene._shop.buy( idGeneral(), idPotion(), 3, scene._wallet, scene._inventory ) == ShopResult::NotEnoughMoney );
    SW_EXPECT_EQUAL( 50, scene._wallet.getBalance( idGold() ) );
    SW_EXPECT_EQUAL( 5, scene._shop.getStockCount( idGeneral(), idPotion() ) );
    SW_EXPECT_EQUAL( 0, scene._inventory.getItemCount( idPotion() ) );

    // 자리 — 네 칸을 칼(겹치지 않는다)로 채우면 물약이 들어갈 데가 없다.
    for ( int32 slotIndex = 0; slotIndex < 4; ++slotIndex )
    {
        SW_EXPECT_EQUAL( 1, scene._inventory.addItem( idSword(), 1 ) );
    }
    SW_EXPECT_TRUE( scene._shop.buy( idGeneral(), idPotion(), 1, scene._wallet, scene._inventory ) == ShopResult::NoRoom );
    SW_EXPECT_EQUAL( 50, scene._wallet.getBalance( idGold() ) ); // 돈을 먼저 거두고 실패하지 않는다
    SW_EXPECT_EQUAL( 5, scene._shop.getStockCount( idGeneral(), idPotion() ) );

    vector<ShopEvent> listEvent;
    scene._shop.drainEvents( listEvent );
    SW_EXPECT_TRUE( listEvent.empty() );
}

SW_TEST_CASE( ShopTest, RequirementIsKeptAsTextAndLockedUntilTheGameSaysSo )
{
    ShopScene scene;
    SW_ASSERT_TRUE( scene._bLoaded );
    const ShopStockDef* pStock = scene._shopCatalog.findShop( idGeneral() )->findStock( idSword() );
    SW_ASSERT_NOT_NULL( pStock );
    SW_EXPECT_TRUE( pStock->_requirement == "flag:chapter2 && !flag:banned" ); // 뜻은 모른 채 글자 그대로
    SW_EXPECT_EQUAL( -1, pStock->_count );                                     // 재고를 적지 않으면 끝없음
    scene._wallet.add( idGold(), 1000 );

    // 평가기가 없으면 조건 있는 줄은 잠겨 있다.
    SW_EXPECT_TRUE( scene._shop.buy( idGeneral(), idSword(), 1, scene._wallet, scene._inventory ) == ShopResult::Locked );
    TestConditionEvaluator evaluator;
    scene._shop.setConditionEvaluator( &evaluator );
    SW_EXPECT_TRUE( scene._shop.buy( idGeneral(), idSword(), 1, scene._wallet, scene._inventory ) == ShopResult::Locked );
    SW_EXPECT_TRUE( evaluator._lastExpression == "flag:chapter2 && !flag:banned" );
    SW_EXPECT_TRUE( scene._shop.isUnlocked( *scene._shopCatalog.findShop( idGeneral() )->findStock( idPotion() ) ) ); // 조건 없는 줄은 묻지 않는다

    evaluator._bOpen = true;
    SW_EXPECT_TRUE( scene._shop.buy( idGeneral(), idSword(), 2, scene._wallet, scene._inventory ) == ShopResult::Ok );
    SW_EXPECT_EQUAL( 700, scene._wallet.getBalance( idGold() ) );
    SW_EXPECT_EQUAL( -1, scene._shop.getStockCount( idGeneral(), idSword() ) ); // 끝없는 재고는 줄지 않는다
}

SW_TEST_CASE( ShopTest, SellPaysInShopCurrencyAndRefusesCategories )
{
    ShopScene scene;
    SW_ASSERT_TRUE( scene._bLoaded );
    SW_EXPECT_EQUAL( 4, scene._inventory.addItem( idPotion(), 4 ) );
    SW_EXPECT_EQUAL( 1, scene._inventory.addItem( hashed_string( "key" ), 1 ) );

    // 12 × 0.5 = 6 씩.
    SW_EXPECT_EQUAL( 12, scene._shop.computeSellTotal( idGeneral(), idPotion(), 2 ) );
    SW_EXPECT_TRUE( scene._shop.sell( idGeneral(), idPotion(), 2, scene._wallet, scene._inventory ) == ShopResult::Ok );
    SW_EXPECT_EQUAL( 12, scene._wallet.getBalance( idGold() ) );
    SW_EXPECT_EQUAL( 2, scene._inventory.getItemCount( idPotion() ) );
    SW_EXPECT_TRUE( scene._shop.sell( idGeneral(), idPotion(), 3, scene._wallet, scene._inventory ) == ShopResult::NotOwned );
    SW_EXPECT_EQUAL( 2, scene._inventory.getItemCount( idPotion() ) );

    // 열쇠는 XML 의 refuses, 소모품은 실행 중에 더한 거절.
    SW_EXPECT_TRUE( scene._shop.sell( idGeneral(), hashed_string( "key" ), 1, scene._wallet, scene._inventory ) == ShopResult::Refused );
    scene._shop.refuseCategory( idGeneral(), hashed_string( "Consumable" ) );
    SW_EXPECT_TRUE( scene._shop.sell( idGeneral(), idPotion(), 1, scene._wallet, scene._inventory ) == ShopResult::Refused );
    SW_EXPECT_EQUAL( 12, scene._wallet.getBalance( idGold() ) );

    // 회사는 Credits 로 산다(모든 분류).
    SW_EXPECT_TRUE( scene._shop.sell( idCompany(), hashed_string( "key" ), 1, scene._wallet, scene._inventory ) == ShopResult::Ok );
    SW_EXPECT_EQUAL( 50, scene._wallet.getBalance( idCredits() ) );
    SW_EXPECT_EQUAL( 12, scene._wallet.getBalance( idGold() ) );
}

SW_TEST_CASE( ShopTest, SellingFloodsThePriceAndItRecoversDaily )
{
    ShopScene scene;
    SW_ASSERT_TRUE( scene._bLoaded );
    scene._inventory.initialize( &scene._itemCatalog, 4 );
    SW_EXPECT_EQUAL( 20, scene._inventory.addItem( idScrap(), 20 ) );

    // 고철 20 Credits, 한 개마다 시세 −0.1: 20 + 18 + 16 = 54.
    SW_EXPECT_EQUAL( 54, scene._shop.computeSellTotal( idCompany(), idScrap(), 3 ) );
    SW_EXPECT_TRUE( scene._shop.sell( idCompany(), idScrap(), 3, scene._wallet, scene._inventory ) == ShopResult::Ok );
    SW_EXPECT_EQUAL( 54, scene._wallet.getBalance( idCredits() ) );
    SW_EXPECT_NEAR_EQUAL( 0.7f, scene._shop.getSellFactor( idCompany(), idScrap() ), 1.0e-4f );
    // 같은 셋을 또 팔면 덜 받는다(14 + 12 + 10).
    SW_EXPECT_EQUAL( 36, scene._shop.computeSellTotal( idCompany(), idScrap(), 3 ) );
    // 바닥 0.4 아래로는 내려가지 않는다.
    SW_EXPECT_TRUE( scene._shop.sell( idCompany(), idScrap(), 10, scene._wallet, scene._inventory ) == ShopResult::Ok );
    SW_EXPECT_NEAR_EQUAL( 0.4f, scene._shop.getSellFactor( idCompany(), idScrap() ), 1.0e-4f );
    SW_EXPECT_EQUAL( 8, scene._shop.computeSellTotal( idCompany(), idScrap(), 1 ) );
    // 다른 아이템의 시세는 그대로다.
    SW_EXPECT_NEAR_EQUAL( 1.0f, scene._shop.getSellFactor( idCompany(), idPotion() ), 1.0e-4f );

    // 하루에 0.15 씩 돌아오고 1 을 넘지 않는다.
    scene._shop.advanceDay();
    SW_EXPECT_NEAR_EQUAL( 0.55f, scene._shop.getSellFactor( idCompany(), idScrap() ), 1.0e-4f );
    for ( int32 dayIndex = 0; dayIndex < 10; ++dayIndex )
    {
        scene._shop.advanceDay();
    }
    SW_EXPECT_NEAR_EQUAL( 1.0f, scene._shop.getSellFactor( idCompany(), idScrap() ), 1.0e-4f );
    SW_EXPECT_EQUAL( 20, scene._shop.computeSellTotal( idCompany(), idScrap(), 1 ) );

    // 가격 배율 — 그날의 매입률 0.3 (리썰 컴퍼니), 평판 할인 0.8.
    scene._shop.setPriceModifier( idCompany(), 1.0f, 0.3f );
    SW_EXPECT_EQUAL( 6, scene._shop.computeSellTotal( idCompany(), idScrap(), 1 ) );
    scene._shop.setPriceModifier( idGeneral(), 0.8f, 1.0f );
    SW_EXPECT_EQUAL( 16, scene._shop.computeBuyPrice( idGeneral(), idPotion() ) );
}

SW_TEST_CASE( ShopTest, RestockEveryFewDaysUpToTheMaximum )
{
    ShopScene scene;
    SW_ASSERT_TRUE( scene._bLoaded );
    scene._wallet.add( idGold(), 1000 );
    SW_EXPECT_TRUE( scene._shop.buy( idGeneral(), idPotion(), 5, scene._wallet, scene._inventory ) == ShopResult::Ok );
    SW_EXPECT_TRUE( scene._shop.buy( idGeneral(), idEther(), 2, scene._wallet, scene._inventory ) == ShopResult::Ok );
    SW_EXPECT_TRUE( scene._shop.buy( idGeneral(), idPotion(), 1, scene._wallet, scene._inventory ) == ShopResult::OutOfStock );
    vector<ShopEvent> listEvent;
    scene._shop.drainEvents( listEvent );

    // restockDays=2 — 첫날은 그대로.
    scene._shop.advanceDay();
    SW_EXPECT_EQUAL( 0, scene._shop.getStockCount( idGeneral(), idPotion() ) );
    scene._shop.advanceDay();
    SW_EXPECT_EQUAL( 3, scene._shop.getStockCount( idGeneral(), idPotion() ) );
    SW_EXPECT_EQUAL( 0, scene._shop.getStockCount( idGeneral(), idEther() ) ); // restock 이 없으면 다시 들어오지 않는다
    listEvent.clear();
    scene._shop.drainEvents( listEvent );
    SW_ASSERT_TRUE( listEvent.size() == 1 );
    SW_EXPECT_TRUE( listEvent[0]._kind == ShopEvent::Kind::Restocked && listEvent[0]._itemId == idPotion() );
    SW_EXPECT_EQUAL( 3, listEvent[0]._count );

    // 최대(처음 재고 5) 를 넘지 않는다.
    scene._shop.advanceDay();
    scene._shop.advanceDay();
    SW_EXPECT_EQUAL( 5, scene._shop.getStockCount( idGeneral(), idPotion() ) );
    scene._shop.advanceDay();
    scene._shop.advanceDay();
    SW_EXPECT_EQUAL( 5, scene._shop.getStockCount( idGeneral(), idPotion() ) );
}

SW_TEST_CASE( ShopTest, BuyAndSellReportTheAmountThatMoved )
{
    ShopScene scene;
    SW_ASSERT_TRUE( scene._bLoaded );
    scene._wallet.add( idGold(), 100 );
    int64 paid = -1;
    SW_EXPECT_TRUE( scene._shop.buy( idGeneral(), idPotion(), 2, scene._wallet, scene._inventory, &paid ) == ShopResult::Ok );
    SW_EXPECT_EQUAL( 40, static_cast<int32>( paid ) );
    SW_EXPECT_EQUAL( 60, static_cast<int32>( scene._wallet.getBalance( idGold() ) ) );
    int64 refused = -1;
    SW_EXPECT_TRUE( scene._shop.buy( idGeneral(), idScrap(), 1, scene._wallet, scene._inventory, &refused ) == ShopResult::UnknownItem );
    SW_EXPECT_EQUAL( -1, static_cast<int32>( refused ) ); // 실패면 건드리지 않는다
    int64 received = 0;
    SW_EXPECT_TRUE( scene._shop.sell( idGeneral(), idPotion(), 1, scene._wallet, scene._inventory, &received ) == ShopResult::Ok );
    SW_EXPECT_TRUE( received > 0 );
    SW_EXPECT_EQUAL( 60 + static_cast<int32>( received ), static_cast<int32>( scene._wallet.getBalance( idGold() ) ) );
}
