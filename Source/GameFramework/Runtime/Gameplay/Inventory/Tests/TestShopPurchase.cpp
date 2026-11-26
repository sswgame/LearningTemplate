#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "GameFramework/Runtime/Gameplay/Inventory/Inventory.h"
#include "GameFramework/Runtime/Gameplay/Inventory/ItemCatalog.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Shop.h"
#include "GameFramework/Runtime/Gameplay/Inventory/ShopPurchase.h"

// ShopPurchaseTest — 상점 화면의 구매 공급자 규칙: 합계 검사(수량 범위 · 곱 넘침), 오프라인 공급자의 목록 · 사기 · 잔액 부족 · 재고 · 인벤토리 반영(nogpu).

namespace
{
    /** @brief 가게 하나(포션 무한 · 칼 재고 1 · 잠긴 열쇠)와 지갑 · 인벤토리입니다. */
    struct LocalShopFixture
    {
        sw::ItemCatalog               _items;
        sw::ShopCatalog               _shops;
        sw::ShopManager               _shop;
        sw::Wallet                    _wallet;
        sw::Inventory                 _inventory;
        sw::LocalShopPurchaseProvider _provider;

        LocalShopFixture()
            : _items{}
            , _shops{}
            , _shop{}
            , _wallet{}
            , _inventory{}
            , _provider{ _shops, _items, _shop, _wallet, _inventory, sw::hashed_string( "general" ) }
        {
            SW_REQUIRE_TRUE( _items.loadFromXMLText( R"(<ItemCatalog>
  <Item id="potion" name="Potion" category="Food" maxStack="10" weight="0.1" value="20"/>
  <Item id="sword" name="Sword" category="Weapon" slot="MainHand" maxStack="1" weight="3" value="40"/>
  <Item id="key" name="Key" category="Key" maxStack="1" weight="0.1" value="5"/>
</ItemCatalog>)",
                                                     "ShopPurchaseTest" ) );
            SW_REQUIRE_TRUE( _shops.loadFromXMLText( R"(<ShopCatalog><Shop id="general" currency="Gold" buybackLimit="0">
  <Stock item="potion" price="20"/>
  <Stock item="sword" count="1"/>
  <Stock item="key" requires="hasMasterKey"/>
</Shop></ShopCatalog>)",
                                                     "ShopPurchaseTest" ) );
            _shop.initialize( _shops, _items );
            SW_REQUIRE_TRUE( _inventory.initialize( _items, 4 ) );
        }

        /** @brief 목록에서 @p offerID 를 찾습니다. */
        sw::ShopOffer findOffer( const utf8* pOfferID ) const
        {
            sw::vector<sw::ShopOffer> listOffer;
            _provider.collectOffers( listOffer );
            for ( const sw::ShopOffer& offer : listOffer )
            {
                if ( offer._offerID == pOfferID )
                    return offer;
            }
            return sw::ShopOffer{};
        }

        /** @brief 사고 답을 돌려줍니다(오프라인은 바로 답한다). */
        sw::ShopPurchaseReceipt buy( const sw::ShopOffer& offer, int32 count )
        {
            sw::ShopPurchaseReceipt  receipt{};
            sw::ShopPurchaseReceipt* pReceipt = &receipt;
            int32                    calls    = 0;
            int32*                   pCalls   = &calls;
            _provider.beginPurchase( offer, count, SW_DELEGATE_LAMBDA( sw::ShopPurchaseDelegate, [pReceipt, pCalls]( const sw::ShopPurchaseReceipt& answer )
            {
                *pReceipt = answer;
                ++*pCalls;
            } ) );
            SW_EXPECT_EQUAL( 1, calls ); // 답은 정확히 한 번
            return receipt;
        }
    };
} // namespace

/** @brief [ShopPurchaseTest] 합계는 수량이 1 ~ 최대 안이고 곱이 넘치지 않을 때만 낸다 — 넘친 곱이 음수 값이 되어 재화가 느는 길을 막는다 */
SW_TEST_CASE( ShopPurchaseTest, TotalRejectsBadCountsAndOverflow )
{
    sw::ShopOffer offer{};
    offer._unitPrice = 30;
    offer._maxCount  = 5;
    int64 total      = 0;
    SW_EXPECT_TRUE( offer.computeTotal( 3, total ) );
    SW_EXPECT_EQUAL( int64( 90 ), total );
    SW_EXPECT_FALSE( offer.computeTotal( 0, total ) );
    SW_EXPECT_FALSE( offer.computeTotal( 6, total ) );
    offer._unitPrice = -1;
    SW_EXPECT_FALSE( offer.computeTotal( 1, total ) );
    offer._unitPrice = int64( 1 ) << 62;
    offer._maxCount  = 1000;
    SW_EXPECT_FALSE( offer.computeTotal( 4, total ) );
}

/** @brief [ShopPurchaseTest] 목록은 진열 순서이고 잠긴 진열은 빠진다. 가격 · 재고 · 한 번에 살 수(겹침 수)를 싣는다 */
SW_TEST_CASE( ShopPurchaseTest, LocalOffersFollowTheShop )
{
    LocalShopFixture          fixture;
    sw::vector<sw::ShopOffer> listOffer;
    fixture._provider.collectOffers( listOffer );
    SW_REQUIRE_EQUAL( size_t( 2 ), listOffer.size() );
    SW_EXPECT_EQUAL( sw::string( "potion" ), listOffer[0]._offerID );
    SW_EXPECT_EQUAL( sw::string( "Potion" ), listOffer[0]._displayName );
    SW_EXPECT_EQUAL( int64( 20 ), listOffer[0]._unitPrice );
    SW_EXPECT_EQUAL( 10, listOffer[0]._maxCount );
    SW_EXPECT_EQUAL( sw::ShopOffer::kUnlimitedStock, listOffer[0]._stock );
    SW_EXPECT_EQUAL( sw::string( "sword" ), listOffer[1]._offerID );
    SW_EXPECT_EQUAL( int64( 40 ), listOffer[1]._unitPrice );
    SW_EXPECT_EQUAL( 1, listOffer[1]._stock );
}

/** @brief [ShopPurchaseTest] 사면 지갑이 줄고 인벤토리에 들어온다. 잔액이 모자라면 아무것도 바뀌지 않는다. 재고가 다하면 OutOfStock */
SW_TEST_CASE( ShopPurchaseTest, LocalBuyMovesMoneyAndItems )
{
    LocalShopFixture fixture;
    fixture._wallet.add( sw::hashed_string( "Gold" ), 70 );
    const uint32 revision = fixture._provider.getRevision();

    const sw::ShopPurchaseReceipt bought = fixture.buy( fixture.findOffer( "potion" ), 2 );
    SW_EXPECT_TRUE( bought._status == sw::ShopPurchaseStatus::Succeeded );
    SW_EXPECT_EQUAL( int64( 40 ), bought._paid );
    SW_EXPECT_EQUAL( int64( 30 ), fixture._provider.getBalance( sw::hashed_string( "Gold" ) ) );
    SW_EXPECT_EQUAL( 2, fixture._inventory.getItemCount( sw::hashed_string( "potion" ) ) );
    SW_EXPECT_TRUE( fixture._provider.getRevision() != revision );

    const sw::ShopPurchaseReceipt poor = fixture.buy( fixture.findOffer( "sword" ), 1 );
    SW_EXPECT_TRUE( poor._status == sw::ShopPurchaseStatus::NotEnoughMoney );
    SW_EXPECT_EQUAL( int64( 0 ), poor._paid );
    SW_EXPECT_EQUAL( int64( 30 ), fixture._wallet.getBalance( sw::hashed_string( "Gold" ) ) );
    SW_EXPECT_EQUAL( 0, fixture._inventory.getItemCount( sw::hashed_string( "sword" ) ) );

    fixture._wallet.add( sw::hashed_string( "Gold" ), 100 );
    const sw::ShopOffer sword = fixture.findOffer( "sword" );
    SW_EXPECT_TRUE( fixture.buy( sword, 1 )._status == sw::ShopPurchaseStatus::Succeeded );
    SW_EXPECT_TRUE( fixture.buy( sword, 1 )._status == sw::ShopPurchaseStatus::OutOfStock );
    SW_EXPECT_TRUE( fixture.buy( fixture.findOffer( "potion" ), 11 )._status == sw::ShopPurchaseStatus::Rejected ); // 겹침 수(10) 위
}

// 화면이 공급자보다 오래 살지 않는다 — 공급자가 먼저 사라지면 묶인 화면에 알리고, 화면이 먼저 풀면 알리지 않는다(게임 인스턴스 재시작 뒤 날 포인터 호출 결함).
SW_TEST_CASE( ShopPurchaseTest, ProviderReleaseNotifiesTheBoundScreenOnce )
{
    int32 releasedCount = 0;
    {
        sw::unique_ptr<LocalShopFixture> pFixture = sw::make_unique<LocalShopFixture>();
        pFixture->_provider.bindScreen( SW_DELEGATE_LAMBDA( sw::Delegate<void()>, [&releasedCount]()
        { ++releasedCount; } ) );
    }
    SW_EXPECT_EQUAL( releasedCount, 1 );

    {
        sw::unique_ptr<LocalShopFixture> pFixture = sw::make_unique<LocalShopFixture>();
        pFixture->_provider.bindScreen( SW_DELEGATE_LAMBDA( sw::Delegate<void()>, [&releasedCount]()
        { ++releasedCount; } ) );
        pFixture->_provider.unbindScreen();
    }
    SW_EXPECT_EQUAL( releasedCount, 1 );
}
