#include "pch.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Foundation/Framework/GameStateRefs.h"
#include "GameFramework/Base/Foundation/Utility/Random/GameRandom.h"
#include "GameFramework/Base/Gameplay/Inventory/Crafting.h"
#include "GameFramework/Base/Gameplay/Inventory/Inventory.h"
#include "GameFramework/Base/Gameplay/Inventory/ItemCatalog.h"
#include "GameFramework/Base/Gameplay/Inventory/Shop.h"
#include "GameFramework/Base/Gameplay/Progression/LevelProgress.h"
#include "GameFramework/Base/Gameplay/Progression/Reputation.h"
#include "GameFramework/Base/World/Environment/WorldClock.h"
#include "GameFramework/Kits/Simulation/RestaurantSim/IngredientStock.h"
#include "GameFramework/Kits/Simulation/RestaurantSim/RestaurantCatalog.h"
#include "GameFramework/Kits/Simulation/RestaurantSim/RestaurantSimulation.h"

#include "TestFramework/TestFramework.h"

// 식당 경영 키트(셰프 RPG 류) — 메뉴 품질 단계, 재료 신선도(오래된 것부터 · 상함), 시장 시세, 요리사 · 스테이션 병렬 조리, 인내 초과 이탈, 가격 수요, 숙련, 일 결산 · 결정성.

using namespace sw;

namespace
{
    constexpr const utf8* kRestaurantTestXml = R"(
<RestaurantCatalog open="11" close="14" window="5">
  <Dish id="ramen" recipe="ramen" category="Noodle" price="40" quality="1,3,5"/>
  <Dish id="omelette" category="Breakfast" price="20" quality="1,4"/>
  <Dish id="cake" category="Dessert" price="30" quality="3,6"/>
  <Dish id="free" price="0"/>
  <Ingredient item="egg" shelfLife="2"/>
  <Ingredient item="noodle" shelfLife="5"/>
  <Ingredient item="broth" shelfLife="1"/>
  <Customer id="student" weight="3" categories="Noodle" patience="20" budget="1.2" tipRate="0.1" eat="10"/>
  <Customer id="foodie" weight="1" categories="Dessert" patience="40" budget="2" tipRate="0.3" eat="20"/>
  <Arrival hour="11" rate="12"/><Arrival hour="12" rate="20"/><Arrival hour="13" rate="6"/>
  <Weather id="rain" arrival="0.5"/>
</RestaurantCatalog>
)";

    constexpr const utf8* kRestaurantTestRecipeXml = R"(
<RecipeCatalog>
  <Recipe id="ramen" station="Stove" time="6"><In item="noodle" count="1"/><In item="broth" count="1"/><Out item="ramen"/></Recipe>
  <Recipe id="omelette" station="Stove" time="4"><In item="egg" count="2"/><Out item="omelette"/></Recipe>
  <Recipe id="cake" station="Oven" time="10" level="3"><In item="flour" count="1"/><In item="egg" count="1"/><Out item="cake"/></Recipe>
</RecipeCatalog>
)";

    constexpr const utf8* kRestaurantTestShopXml = R"(
<ShopCatalog><Shop id="market"><Stock item="egg" price="3"/><Stock item="noodle" price="5"/><Stock item="broth" price="4"/><Stock item="flour" price="2"/></Shop></ShopCatalog>
)";

    constexpr const utf8* kRestaurantTestReputationXml = R"(
<ReputationCatalog><Faction id="restaurant.guests" min="-1000" max="1000" start="100"/></ReputationCatalog>
)";

    /** @brief 식당 하나가 빌리는 것 — 섞인 게임에서는 공유 상태의 것입니다. */
    struct RestaurantTestBorrowed
    {
        Inventory       _pantry;     ///< 주방 창고
        Wallet          _wallet;     ///< 식당 금고
        WorldClock      _clock;      ///< 날(시세 굴림의 씨앗)
        ReputationState _reputation; ///< 손님 평판(세력 restaurant.guests)

        GameStateRefs makeRefs()
        {
            GameStateRefs refs;
            refs._pWallet     = &_wallet;
            refs._pClock      = &_clock;
            refs._pReputation = &_reputation;
            return refs;
        }

        /** @brief 하루를 넘기고 식당에 알립니다 — 게임에서는 디렉터가 시계의 날 넘김에 부른다. */
        void passDay( RestaurantSimulation& sim )
        {
            _clock.advanceToHour( _clock.getHour() );
            _reputation.advanceDay(); // 공유 평판은 주인이 날 넘김에 식힌다
            sim.advanceDay();
        }
    };

    /** @brief 시험이 함께 쓰는 카탈로그들입니다. */
    struct RestaurantTestWorld
    {
        RestaurantCatalog _catalog;
        RecipeCatalog     _recipes;
        ItemCatalog       _items;
        ShopCatalog       _shops;
        ReputationCatalog _reputation;
        ExperienceCurve   _curve;

        [[nodiscard]] bool load()
        {
            for ( const utf8* pItemId : { "egg", "noodle", "broth", "flour" } )
            {
                ItemDef item;
                item._id       = hashed_string( pItemId );
                item._maxStack = 99;
                item._value    = 3;
                _items.addItem( item );
            }
            _curve.setFormula( 20.0f, 1.0f, 0.0f, 10 ); // 레벨 L → L+1 에 20 × L
            return _catalog.loadFromXmlText( kRestaurantTestXml, "RestaurantSimTest" ) && _recipes.loadFromXmlText( kRestaurantTestRecipeXml, "RestaurantSimTest" ) &&
                   _shops.loadFromXmlText( kRestaurantTestShopXml, "RestaurantSimTest" ) &&
                   _reputation.loadFromXmlText( kRestaurantTestReputationXml, "RestaurantSimTest" );
        }

        void initialize( RestaurantSimulation& sim, const RestaurantSettings& settings, RestaurantTestBorrowed& outBorrowed ) const
        {
            outBorrowed._pantry.initialize( &_items, 40 );
            outBorrowed._clock.initialize( WorldClockSettings{} );
            outBorrowed._reputation.initialize( &_reputation );
            sim.initialize( &_catalog, &_recipes, &_items, &_shops, &_curve, outBorrowed.makeRefs(), outBorrowed._pantry, settings );
        }
    };

    void stockPantry( RestaurantSimulation& sim, int32 count )
    {
        (void)sim.addIngredient( "noodle", count, 5 );
        (void)sim.addIngredient( "broth", count, 4 );
        (void)sim.addIngredient( "egg", count, 3 );
        (void)sim.addIngredient( "flour", count, 2 );
    }

    /** @brief 요리사 둘 · 서버 · 계산원, 화구 둘 · 오븐 하나인 식당 하루를 굴리고 결산합니다. */
    RestaurantDaySummary runRestaurantDay( const RestaurantTestWorld& world, const hashed_string& weatherId, float32 stepMinutes )
    {
        RestaurantSettings settings;
        settings._seatCount = 6;
        RestaurantSimulation   sim;
        RestaurantTestBorrowed simBorrowed;
        world.initialize( sim, settings, simBorrowed );
        sim.setStationCount( "Stove", 2 );
        sim.setStationCount( "Oven", 1 );
        (void)sim.hireStaff( "ann", StaffRole::Cook, 30, 3 );
        (void)sim.hireStaff( "bob", StaffRole::Cook, 20 );
        (void)sim.hireStaff( "cid", StaffRole::Server, 15 );
        (void)sim.hireStaff( "dee", StaffRole::Cashier, 15 );
        stockPantry( sim, 60 );
        simBorrowed._wallet.add( sim.getCurrency(), 500 );
        sim.openDay( weatherId );
        const int32 updateCount = static_cast<int32>( 300.0f / stepMinutes );
        for ( int32 updateIndex = 0; updateIndex < updateCount; ++updateIndex )
        {
            sim.update( stepMinutes );
        }
        RestaurantDaySummary summary;
        sim.closeDay( summary );
        return summary;
    }

    int32 countRestaurantEvents( const vector<RestaurantEvent>& listEvent, RestaurantEvent::Kind kind )
    {
        int32 count = 0;
        for ( const RestaurantEvent& restaurantEvent : listEvent )
        {
            if ( restaurantEvent._kind == kind )
                ++count;
        }
        return count;
    }

    /** @brief 빌린 것(창고 · 지갑 · 시계 · 평판)을 상태 바이트로 옮깁니다 — 되살린 식당이 따로 든 같은 공유 상태로 같은 걸음을 걷게 합니다. */
    [[nodiscard]] bool copyRestaurantBorrowed( const RestaurantTestBorrowed& source, RestaurantTestBorrowed& outTarget )
    {
        Archive written;
        source._pantry.writeState( written );
        source._wallet.writeState( written );
        source._clock.writeState( written );
        source._reputation.writeState( written );
        Archive reader( written.getData(), written.getSize() );
        return outTarget._pantry.readState( reader ) && outTarget._wallet.readState( reader ) && outTarget._clock.readState( reader ) &&
               outTarget._reputation.readState( reader );
    }

    /** @brief 시험의 뽑기 흉내가 쓰는 가중치 읽기입니다 — 실수 하나가 곧 가중치입니다. */
    float32 getRestaurantTestWeight( float32 weight )
    {
        return weight;
    }
} // namespace

/**
 * @brief [RestaurantSimTest] 카탈로그 — 값이 없는 요리는 빠지고, 품질은 요리사 레벨이 넘은 문턱 수(최소 1), 시간대 도착 · 날씨 배율 · 유통 기한을 읽는다
 */
SW_TEST_CASE( RestaurantSimTest, CatalogReadsMenuQualityAndArrivals )
{
    RestaurantTestWorld world;
    SW_ASSERT_TRUE( world.load() );
    SW_EXPECT_EQUAL( static_cast<size_t>( 3 ), world._catalog.getDishes().size() );
    const DishDef* pRamen = world._catalog.findDish( "ramen" );
    SW_ASSERT_NOT_NULL( pRamen );
    SW_EXPECT_EQUAL( 1, pRamen->computeQuality( 1 ) );
    SW_EXPECT_EQUAL( 1, pRamen->computeQuality( 2 ) );
    SW_EXPECT_EQUAL( 2, pRamen->computeQuality( 3 ) );
    SW_EXPECT_EQUAL( 3, pRamen->computeQuality( 9 ) );
    SW_EXPECT_EQUAL( 3, pRamen->getMaxQuality() );
    SW_EXPECT_EQUAL( 1, world._catalog.findDish( "cake" )->computeQuality( 1 ) ); // 문턱 아래도 ★1
    SW_EXPECT_TRUE( world._catalog.findDish( "omelette" )->_recipeId == hashed_string( "omelette" ) );

    SW_EXPECT_NEAR_EQUAL( 20.0f, world._catalog.getArrivalRate( 12 ), 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, world._catalog.getArrivalRate( 15 ), 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, world._catalog.findWeatherScale( "rain" ), 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, world._catalog.findWeatherScale( "sunny" ), 1.0e-4f );
    SW_EXPECT_EQUAL( 2, world._catalog.findShelfLife( "egg" ) );
    SW_EXPECT_EQUAL( 0, world._catalog.findShelfLife( "flour" ) );
    SW_EXPECT_EQUAL( 11, world._catalog.getOpenHour() );
    SW_EXPECT_EQUAL( 14, world._catalog.getCloseHour() );
    SW_EXPECT_NEAR_EQUAL( 40.0f, world._catalog.findCustomerType( "foodie" )->_patience, 1.0e-4f );
}

/**
 * @brief [RestaurantSimTest] 재료는 기한이 가까운 묶음부터 쓰고(원가도 그 묶음 값), 기한이 다한 묶음은 인벤토리에서 버리며, 모자란 꺼내기는 아무것도 바꾸지 않는다
 */
SW_TEST_CASE( RestaurantSimTest, IngredientsUseOldestFirstAndSpoil )
{
    RestaurantTestWorld world;
    SW_ASSERT_TRUE( world.load() );
    Inventory inventory;
    inventory.initialize( &world._items, 8 );
    IngredientStock stock;
    stock.initialize( &inventory );

    SW_EXPECT_EQUAL( 3, stock.addFresh( "egg", 3, 5, 4 ) );
    SW_EXPECT_EQUAL( 2, stock.addFresh( "egg", 2, 2, 2 ) );
    SW_EXPECT_EQUAL( 2, stock.findEarliestExpiry( "egg" ) );
    int64 cost = 0;
    SW_EXPECT_FALSE( stock.consume( "egg", 6, cost ) );
    SW_EXPECT_EQUAL( 5, inventory.getItemCount( "egg" ) );
    SW_EXPECT_TRUE( stock.consume( "egg", 3, cost ) );
    SW_EXPECT_EQUAL( 8, static_cast<int32>( cost ) ); // 2 × 2(기한 2 묶음) + 1 × 4
    SW_EXPECT_EQUAL( 5, stock.findEarliestExpiry( "egg" ) );
    SW_EXPECT_EQUAL( 2, inventory.getItemCount( "egg" ) );

    (void)stock.addFresh( "egg", 1, 1, 1 );
    (void)stock.addFresh( "flour", 4, 0, 2 ); // 상하지 않는다
    vector<IngredientSpoilage> listSpoilage;
    stock.advanceDay( listSpoilage );
    SW_ASSERT_TRUE( listSpoilage.size() == 1 );
    SW_EXPECT_EQUAL( 1, listSpoilage[0]._count );
    SW_EXPECT_EQUAL( 1, static_cast<int32>( listSpoilage[0]._cost ) );
    SW_EXPECT_EQUAL( 2, inventory.getItemCount( "egg" ) );
    SW_EXPECT_EQUAL( 4, stock.findEarliestExpiry( "egg" ) );
    SW_EXPECT_EQUAL( -1, stock.findEarliestExpiry( "flour" ) );

    // 밖에서 뺀 만큼 묶음을 맞춘다.
    SW_EXPECT_TRUE( inventory.removeItem( "egg", 1 ) );
    cost = 0;
    SW_EXPECT_TRUE( stock.consume( "egg", 1, cost ) );
    SW_EXPECT_EQUAL( 0, stock.getBatchCount( "egg" ) );

    ItemStackList items;
    items.addItem( "flour", 2 );
    items.addItem( "egg", 1 );
    cost = 0;
    SW_EXPECT_FALSE( stock.consumeItems( items, 1, cost ) ); // 달걀이 없다 — 밀가루도 그대로
    SW_EXPECT_EQUAL( 4, inventory.getItemCount( "flour" ) );
    items.clear();
    items.addItem( "flour", 2 );
    SW_EXPECT_TRUE( stock.consumeItems( items, 2, cost ) );
    SW_EXPECT_EQUAL( 8, static_cast<int32>( cost ) );
    SW_EXPECT_EQUAL( 0, inventory.getItemCount( "flour" ) );
}

/**
 * @brief [RestaurantSimTest] 시장 — 산 재료는 그날 시세 단가의 신선도 묶음이 되고, 시세는 날 · 씨앗으로 정해져 두 식당이 같은 값을 본다
 */
SW_TEST_CASE( RestaurantSimTest, MarketBuysRecordBatchesAndPricesMoveDeterministically )
{
    RestaurantTestWorld world;
    SW_ASSERT_TRUE( world.load() );
    RestaurantSimulation   simA;
    RestaurantTestBorrowed simABorrowed;
    RestaurantSimulation   simB;
    RestaurantTestBorrowed simBBorrowed;
    world.initialize( simA, RestaurantSettings{}, simABorrowed );
    world.initialize( simB, RestaurantSettings{}, simBBorrowed );
    simABorrowed._wallet.add( simA.getCurrency(), 100 );

    const int32 eggPrice = simA.getMarket().computeBuyPrice( "market", "egg" );
    SW_EXPECT_TRUE( eggPrice >= 2 && eggPrice <= 4 ); // 3 ± 20 %
    SW_EXPECT_TRUE( simA.buyIngredient( "market", "egg", 10 ) == ShopResult::Ok );
    SW_EXPECT_EQUAL( 100 - eggPrice * 10, static_cast<int32>( simABorrowed._wallet.getBalance( simA.getCurrency() ) ) );
    SW_EXPECT_EQUAL( 10, simA.getStock().getBatchCount( "egg" ) );
    SW_EXPECT_EQUAL( 2, simA.getStock().findEarliestExpiry( "egg" ) );
    SW_EXPECT_EQUAL( eggPrice, static_cast<int32>( simA.getStock().getBatches()[0]._unitCost ) );
    SW_EXPECT_TRUE( simA.buyIngredient( "market", "egg", 1000 ) == ShopResult::NotEnoughMoney );
    SW_EXPECT_TRUE( simA.buyIngredient( "market", "truffle", 1 ) == ShopResult::UnknownItem );

    int32 lowestPrice  = 1000;
    int32 highestPrice = 0;
    for ( int32 dayIndex = 0; dayIndex < 8; ++dayIndex )
    {
        simABorrowed.passDay( simA );
        simBBorrowed.passDay( simB );
        const int32 priceA = simA.getMarket().computeBuyPrice( "market", "noodle" );
        SW_EXPECT_EQUAL( priceA, simB.getMarket().computeBuyPrice( "market", "noodle" ) );
        lowestPrice  = priceA < lowestPrice ? priceA : lowestPrice;
        highestPrice = priceA > highestPrice ? priceA : highestPrice;
    }
    SW_EXPECT_TRUE( lowestPrice >= 4 && highestPrice <= 6 );
    SW_EXPECT_TRUE( lowestPrice < highestPrice );                 // 시세가 움직였다
    SW_EXPECT_EQUAL( 0, simA.getStock().getBatchCount( "egg" ) ); // 이틀 지나 상했다
    vector<RestaurantEvent> listEvent;
    simA.drainEvents( listEvent );
    SW_EXPECT_EQUAL( 1, countRestaurantEvents( listEvent, RestaurantEvent::Kind::IngredientSpoiled ) );
}

/**
 * @brief [RestaurantSimTest] 조리는 요리사 수와 그 스테이션 수 중 작은 만큼만 함께 한다 — 화구가 하나면 요리사가 둘이어도 하나씩
 */
SW_TEST_CASE( RestaurantSimTest, KitchenCooksInParallelUpToCooksAndStations )
{
    RestaurantTestWorld world;
    SW_ASSERT_TRUE( world.load() );
    RestaurantSimulation   sim;
    RestaurantTestBorrowed simBorrowed;
    world.initialize( sim, RestaurantSettings{}, simBorrowed );
    sim.setStationCount( "Stove", 1 );
    (void)sim.hireStaff( "ann", StaffRole::Cook, 30 );
    (void)sim.hireStaff( "bob", StaffRole::Cook, 30 );
    stockPantry( sim, 10 );
    SW_EXPECT_FALSE( sim.canServe( "cake" ) ); // 오븐이 없고 레벨 3 이 필요하다
    SW_EXPECT_TRUE( sim.canServe( "ramen" ) );

    for ( int32 customerIndex = 0; customerIndex < 3; ++customerIndex )
    {
        SW_EXPECT_TRUE( sim.admitCustomer( "student" ) > 0 );
    }
    SW_EXPECT_EQUAL( -1, sim.admitCustomer( "ghost" ) );
    sim.update( 1.0f );
    SW_EXPECT_EQUAL( 3, sim.countOccupiedSeats() );
    SW_EXPECT_EQUAL( static_cast<size_t>( 3 ), sim.getOrders().size() );
    SW_EXPECT_EQUAL( 1, sim.countCooking() );

    sim.setStationCount( "Stove", 2 );
    sim.update( 1.0f );
    SW_EXPECT_EQUAL( 2, sim.countCooking() );
    sim.setStationCount( "Stove", 4 );
    sim.update( 1.0f );
    SW_EXPECT_EQUAL( 2, sim.countCooking() ); // 이제는 요리사가 모자란다
}

/**
 * @brief [RestaurantSimTest] 인내를 넘겨 기다린 손님은 떠나고 평판 · 별점이 떨어진다 — 같은 손님을 요리사 · 서버 · 계산원이 맞으면 모두 내고 간다
 */
SW_TEST_CASE( RestaurantSimTest, ImpatientCustomersWalkOutAndHurtReputation )
{
    RestaurantTestWorld world;
    SW_ASSERT_TRUE( world.load() );
    RestaurantSettings settings;
    settings._seatCount = 1;
    RestaurantSimulation   sim;
    RestaurantTestBorrowed simBorrowed;
    world.initialize( sim, settings, simBorrowed );
    sim.setStationCount( "Stove", 1 );
    (void)sim.hireStaff( "cid", StaffRole::Server, 15 ); // 요리사가 없다
    stockPantry( sim, 10 );
    SW_EXPECT_EQUAL( 100, sim.getReputation() );
    (void)sim.admitCustomer( "student" );
    (void)sim.admitCustomer( "student" );
    sim.update( 20.0f );
    SW_EXPECT_EQUAL( 0, sim.getToday()._walkouts ); // 20 분까지는 참는다
    sim.update( 1.0f );
    SW_EXPECT_EQUAL( 2, sim.getToday()._walkouts ); // 앉은 손님도 줄 선 손님도
    SW_EXPECT_EQUAL( 70, sim.getReputation() );
    SW_EXPECT_NEAR_EQUAL( 1.0f, sim.computeRating(), 1.0e-4f );
    SW_EXPECT_EQUAL( 0, static_cast<int32>( sim.getOrders().size() ) );

    RestaurantSettings fullSettings;
    fullSettings._seatCount = 4;
    RestaurantSimulation   staffed;
    RestaurantTestBorrowed staffedBorrowed;
    world.initialize( staffed, fullSettings, staffedBorrowed );
    staffed.setStationCount( "Stove", 1 );
    (void)staffed.hireStaff( "ann", StaffRole::Cook, 30, 5 ); // 레벨 5 — 품질 최고
    (void)staffed.hireStaff( "cid", StaffRole::Server, 15 );
    (void)staffed.hireStaff( "dee", StaffRole::Cashier, 15 );
    stockPantry( staffed, 10 );
    (void)staffed.admitCustomer( "student" );
    (void)staffed.admitCustomer( "student" );
    staffed.update( 60.0f );
    SW_EXPECT_EQUAL( 0, staffed.getToday()._walkouts );
    SW_EXPECT_EQUAL( 2, staffed.getToday()._served );
    SW_EXPECT_TRUE( staffed.getReputation() > 100 );
    SW_EXPECT_TRUE( staffed.computeRating() > 3.0f );
    SW_EXPECT_TRUE( staffed.getToday()._tips > 0 );
}

/**
 * @brief [RestaurantSimTest] 가격 — 지불 의사를 넘는 요리는 시키지 않고, 메뉴 값을 올리면 손님 도착 배율이 줄어든다
 */
SW_TEST_CASE( RestaurantSimTest, MenuPricesShiftDemand )
{
    RestaurantTestWorld world;
    SW_ASSERT_TRUE( world.load() );
    RestaurantSimulation   sim;
    RestaurantTestBorrowed simBorrowed;
    world.initialize( sim, RestaurantSettings{}, simBorrowed );
    sim.setStationCount( "Stove", 1 );
    (void)sim.hireStaff( "ann", StaffRole::Cook, 30 );
    (void)sim.addIngredient( "noodle", 5, 5 );
    (void)sim.addIngredient( "broth", 5, 4 ); // 라멘 재료만
    SW_EXPECT_FALSE( sim.canServe( "omelette" ) );
    SW_EXPECT_NEAR_EQUAL( 1.0f, sim.computePriceDemandScale(), 1.0e-4f );

    SW_EXPECT_TRUE( sim.setMenuPrice( "ramen", 60 ) ); // 1.5 배 — 학생 지불 의사 1.2 를 넘는다
    SW_EXPECT_FALSE( sim.setMenuPrice( "pizza", 10 ) );
    SW_EXPECT_NEAR_EQUAL( 1.0f - 0.5f / 3.0f, sim.computePriceDemandScale(), 1.0e-4f );
    sim.openDay( "sunny" );
    const float32 expensiveRate = sim.computeArrivalRate();
    (void)sim.admitCustomer( "student" );
    sim.update( 1.0f );
    SW_EXPECT_EQUAL( 1, sim.getToday()._noChoice );
    SW_EXPECT_EQUAL( 0, static_cast<int32>( sim.getOrders().size() ) );
    SW_EXPECT_EQUAL( 5, simBorrowed._pantry.getItemCount( "noodle" ) ); // 주문이 없으니 재료도 그대로

    SW_EXPECT_TRUE( sim.setMenuPrice( "ramen", 44 ) ); // 1.1 배 — 시킨다
    const float32 fairRate = sim.computeArrivalRate();
    SW_EXPECT_TRUE( fairRate > expensiveRate );
    SW_EXPECT_NEAR_EQUAL( 12.0f * 1.1f * ( 1.0f - 0.1f / 3.0f ), fairRate, 1.0e-3f ); // 11 시 12 명 × 평판 100 × 가격
    (void)sim.admitCustomer( "student" );
    sim.update( 1.0f );
    SW_EXPECT_EQUAL( 1, static_cast<int32>( sim.getOrders().size() ) );
    SW_EXPECT_EQUAL( 44, static_cast<int32>( sim.getCustomers().back()._price ) );
    SW_EXPECT_EQUAL( 9, static_cast<int32>( sim.getToday()._ingredientCost ) ); // 면 5 + 육수 4
}

/**
 * @brief [RestaurantSimTest] 요리사는 만든 요리마다 경험치를 받아 레벨이 오르고, 오른 레벨로 만든 요리는 품질이 높다
 */
SW_TEST_CASE( RestaurantSimTest, CooksLevelUpAndCookBetterDishes )
{
    RestaurantTestWorld world;
    SW_ASSERT_TRUE( world.load() );
    RestaurantSettings settings;
    settings._cookXp = 20;
    RestaurantSimulation   sim;
    RestaurantTestBorrowed simBorrowed;
    world.initialize( sim, settings, simBorrowed );
    sim.setStationCount( "Stove", 1 );
    sim.setDishOnMenu( "omelette", false );
    (void)sim.hireStaff( "ann", StaffRole::Cook, 30 );
    (void)sim.hireStaff( "cid", StaffRole::Server, 15 );
    stockPantry( sim, 10 );

    vector<RestaurantEvent> listEvent;
    vector<int32>           listQuality;
    for ( int32 customerIndex = 0; customerIndex < 4; ++customerIndex )
    {
        (void)sim.admitCustomer( "student" );
        sim.update( 40.0f );
        listEvent.clear();
        sim.drainEvents( listEvent );
        for ( const RestaurantEvent& restaurantEvent : listEvent )
        {
            if ( restaurantEvent._kind == RestaurantEvent::Kind::DishCooked )
                listQuality.push_back( static_cast<int32>( restaurantEvent._value ) );
        }
    }
    // 20 xp 씩: 1 → 2(20) → 3(40 더) — 세 번째 그릇부터 레벨 3, 품질 ★2.
    SW_ASSERT_TRUE( listQuality.size() == 4 );
    SW_EXPECT_EQUAL( 1, listQuality[0] );
    SW_EXPECT_EQUAL( 1, listQuality[1] );
    SW_EXPECT_EQUAL( 1, listQuality[2] );
    SW_EXPECT_EQUAL( 2, listQuality[3] );
    SW_EXPECT_EQUAL( 3, sim.getStaff()[0]._level.getLevel() );
    SW_EXPECT_EQUAL( 4, sim.getToday()._served ); // 계산원이 없으면 서버가 계산한다
}

/**
 * @brief [RestaurantSimTest] 하루를 굴려 결산한다 — 이익 = 매출 + 팁 − 재료 − 급여, 걸음을 어떻게 나눠도 같은 하루(결정적), 비 오는 날은 손님이 적다
 */
SW_TEST_CASE( RestaurantSimTest, FullDayClosesTheBooksDeterministically )
{
    RestaurantTestWorld world;
    SW_ASSERT_TRUE( world.load() );
    const RestaurantDaySummary sunny = runRestaurantDay( world, "sunny", 1.0f );
    SW_EXPECT_TRUE( sunny._arrivals > 20 );
    SW_EXPECT_TRUE( sunny._served > 10 );
    SW_EXPECT_TRUE( sunny._revenue > 0 && sunny._tips > 0 && sunny._ingredientCost > 0 );
    SW_EXPECT_EQUAL( 80, static_cast<int32>( sunny._wages ) );
    SW_EXPECT_EQUAL( static_cast<int32>( sunny._revenue + sunny._tips - sunny._ingredientCost - sunny._wages - sunny._spoilageCost ),
                     static_cast<int32>( sunny._profit ) );
    SW_EXPECT_EQUAL( sunny._arrivals, sunny._served + sunny._walkouts + sunny._noChoice + sunny._unservedAtClose );
    SW_EXPECT_TRUE( sunny._rating >= 1.0f && sunny._rating <= 5.0f );

    const RestaurantDaySummary again = runRestaurantDay( world, "sunny", 2.5f );
    SW_EXPECT_EQUAL( sunny._arrivals, again._arrivals );
    SW_EXPECT_EQUAL( sunny._served, again._served );
    SW_EXPECT_EQUAL( sunny._walkouts, again._walkouts );
    SW_EXPECT_EQUAL( static_cast<int32>( sunny._revenue ), static_cast<int32>( again._revenue ) );
    SW_EXPECT_EQUAL( static_cast<int32>( sunny._tips ), static_cast<int32>( again._tips ) );
    SW_EXPECT_NEAR_EQUAL( sunny._rating, again._rating, 1.0e-5f );

    const RestaurantDaySummary rainy = runRestaurantDay( world, "rain", 1.0f );
    SW_EXPECT_TRUE( rainy._arrivals < sunny._arrivals );
}

/**
 * @brief [RestaurantSimTest] 손님은 (좋아하는 분류 배율 × 가격 수요) 가중치로 요리를 고른다 — 같은 씨앗 · 같은 가중치의 뽑기와 한 그릇씩 같다
 * @details 학생(Noodle 선호)에게 라멘은 1 × `_preferredWeight` 3, 오믈렛은 1 이다. 케이크는 오븐이 없어 후보가 아니다. 시뮬레이션의 난수는 손님 도착과
 *          요리 고르기에만 쓰이고 이 시험은 영업을 열지 않으므로(도착 없음), 같은 씨앗의 `GameRandom` 으로 같은 순서를 다시 뽑아 견준다.
 */
SW_TEST_CASE( RestaurantSimTest, CustomersPickDishesLikeTheSeededWeightedDraw )
{
    RestaurantTestWorld world;
    SW_ASSERT_TRUE( world.load() );
    RestaurantSettings settings;
    settings._seatCount = 6;
    RestaurantSimulation   sim;
    RestaurantTestBorrowed simBorrowed;
    world.initialize( sim, settings, simBorrowed );
    sim.setStationCount( "Stove", 1 );
    (void)sim.hireStaff( "ann", StaffRole::Cook, 30 );
    stockPantry( sim, 20 );
    SW_ASSERT_TRUE( sim.canServe( "ramen" ) );
    SW_ASSERT_TRUE( sim.canServe( "omelette" ) );
    SW_ASSERT_FALSE( sim.canServe( "cake" ) );
    for ( int32 customerIndex = 0; customerIndex < 6; ++customerIndex )
    {
        (void)sim.admitCustomer( "student" );
    }
    sim.update( 1.0f );
    SW_ASSERT_TRUE( sim.getOrders().size() == 6 );

    GameRandom          oracle( settings._randomSeed );
    const float32       arrWeight[] = { 3.0f, 1.0f }; // 메뉴 순서 — 라멘 · 오믈렛
    const hashed_string arrDishId[] = { hashed_string( "ramen" ), hashed_string( "omelette" ) };
    for ( const KitchenOrder& order : sim.getOrders() )
    {
        const int32 expectedIndex = oracle.pickWeightedIndex( arrWeight, getRestaurantTestWeight );
        SW_ASSERT_TRUE( 0 <= expectedIndex && expectedIndex <= 1 );
        SW_EXPECT_TRUE( order._dishId == arrDishId[expectedIndex] );
    }
    SW_EXPECT_EQUAL( 0, sim.getToday()._noChoice );
}

/**
 * @brief [RestaurantSimTest] 가중치가 0 인 요리는 양수 가중치 요리 옆에서 뽑히지 않는다 — 좋아하는 분류 배율이 0 이면 학생은 라멘 대신 오믈렛만 시킨다
 */
SW_TEST_CASE( RestaurantSimTest, ZeroWeightDishIsNotOrderedBesidePositiveOnes )
{
    RestaurantTestWorld world;
    SW_ASSERT_TRUE( world.load() );
    RestaurantSettings settings;
    settings._seatCount       = 6;
    settings._preferredWeight = 0.0f; // 학생이 좋아하는 Noodle(라멘)의 가중치가 0
    RestaurantSimulation   sim;
    RestaurantTestBorrowed simBorrowed;
    world.initialize( sim, settings, simBorrowed );
    sim.setStationCount( "Stove", 1 );
    (void)sim.hireStaff( "ann", StaffRole::Cook, 30 );
    stockPantry( sim, 20 );
    SW_ASSERT_TRUE( sim.canServe( "ramen" ) );
    SW_ASSERT_TRUE( sim.canServe( "omelette" ) );
    for ( int32 customerIndex = 0; customerIndex < 6; ++customerIndex )
    {
        (void)sim.admitCustomer( "student" );
    }
    sim.update( 1.0f );
    SW_ASSERT_TRUE( sim.getOrders().size() == 6 );
    for ( const KitchenOrder& order : sim.getOrders() )
    {
        SW_EXPECT_TRUE( order._dishId == hashed_string( "omelette" ) );
    }
    SW_EXPECT_EQUAL( 0, sim.getToday()._noChoice );
}

/**
 * @brief [RestaurantSimTest] 후보가 모두 가중치 0 이면 시키지 않고 나간다 — 0 은 "후보 아님" 이라 다른 후보가 없어도 되살아나지 않는다
 * @details 좋아하는 분류 배율 0 · 라멘 재료만 — 학생의 후보는 가중치 0 인 라멘 하나다. 주문이 없으니 재료도 그대로다.
 */
SW_TEST_CASE( RestaurantSimTest, CustomerWithOnlyZeroWeightDishesLeavesWithoutOrdering )
{
    RestaurantTestWorld world;
    SW_ASSERT_TRUE( world.load() );
    RestaurantSettings settings;
    settings._preferredWeight = 0.0f;
    RestaurantSimulation   sim;
    RestaurantTestBorrowed simBorrowed;
    world.initialize( sim, settings, simBorrowed );
    sim.setStationCount( "Stove", 1 );
    (void)sim.hireStaff( "ann", StaffRole::Cook, 30 );
    (void)sim.addIngredient( "noodle", 5, 5 );
    (void)sim.addIngredient( "broth", 5, 4 );
    SW_ASSERT_TRUE( sim.canServe( "ramen" ) );
    SW_ASSERT_FALSE( sim.canServe( "omelette" ) );
    (void)sim.admitCustomer( "student" );
    sim.update( 1.0f );
    SW_EXPECT_EQUAL( 0, static_cast<int32>( sim.getOrders().size() ) );
    SW_EXPECT_EQUAL( 1, sim.getToday()._noChoice );
    SW_EXPECT_TRUE( sim.getCustomers().back()._state == CustomerState::Left );
    SW_EXPECT_EQUAL( 5, simBorrowed._pantry.getItemCount( "noodle" ) ); // 주문이 없으니 재료도 그대로
}

/**
 * @brief [RestaurantSimTest] 상태 바이트로 되살린 식당이 같은 영업을 잇는다 — 직원 · 손님 · 주문 · 재료 묶음 · 시세 · 오늘 결산 · 도착 누적이 같은 바이트이고,
 *        같은 걸음을 둘 다 더 돌려도(빌린 것은 따로 옮겨 든다) 같은 바이트 · 같은 결산이다. 잘린 바이트는 거절하고 그대로 둔다
 */
SW_TEST_CASE( RestaurantSimTest, StateRoundTripContinuesTheSameRestaurant )
{
    RestaurantTestWorld world;
    SW_ASSERT_TRUE( world.load() );
    RestaurantSettings settings;
    settings._seatCount = 6;
    RestaurantSimulation   sim;
    RestaurantTestBorrowed simBorrowed;
    world.initialize( sim, settings, simBorrowed );
    sim.setStationCount( "Stove", 2 );
    sim.setStationCount( "Oven", 1 );
    (void)sim.hireStaff( "ann", StaffRole::Cook, 30, 3 );
    (void)sim.hireStaff( "bob", StaffRole::Cook, 20 );
    (void)sim.hireStaff( "cid", StaffRole::Server, 15 );
    (void)sim.hireStaff( "dee", StaffRole::Cashier, 15 );
    stockPantry( sim, 60 );
    simBorrowed._wallet.add( sim.getCurrency(), 500 );
    SW_EXPECT_TRUE( sim.setMenuPrice( "ramen", 45 ) );
    sim.openDay( "sunny" );
    for ( int32 minute = 0; minute < 60; ++minute )
    {
        sim.update( 1.0f );
    }
    SW_EXPECT_TRUE( sim.getToday()._arrivals > 0 );

    Archive written;
    sim.writeState( written );
    RestaurantSimulation   restored;
    RestaurantTestBorrowed restoredBorrowed;
    world.initialize( restored, settings, restoredBorrowed );
    SW_ASSERT_TRUE( copyRestaurantBorrowed( simBorrowed, restoredBorrowed ) );
    Archive reader( written.getData(), written.getSize() );
    SW_ASSERT_TRUE( restored.readState( reader ) );
    SW_EXPECT_EQUAL( uint64{ 0 }, reader.getRemainingBytes() );
    Archive rewritten;
    restored.writeState( rewritten );
    vector<uint8> originalBytes;
    vector<uint8> restoredBytes;
    written.writeData( originalBytes );
    rewritten.writeData( restoredBytes );
    SW_EXPECT_TRUE( originalBytes == restoredBytes );
    SW_EXPECT_TRUE( restored.isOpen() );
    SW_EXPECT_EQUAL( 45, static_cast<int32>( restored.getMenuPrice( "ramen" ) ) );
    SW_EXPECT_EQUAL( 4, static_cast<int32>( restored.getStaff().size() ) );
    SW_EXPECT_EQUAL( sim.getStaff()[0]._level.getLevel(), restored.getStaff()[0]._level.getLevel() );
    SW_EXPECT_EQUAL( sim.getToday()._arrivals, restored.getToday()._arrivals );
    SW_EXPECT_EQUAL( static_cast<int32>( sim.getOrders().size() ), static_cast<int32>( restored.getOrders().size() ) );
    SW_EXPECT_NEAR_EQUAL( sim.getMinutes(), restored.getMinutes(), 1.0e-4f );

    // 같은 걸음을 둘 다 — 한 시간 더 영업하고 닫는다. 도착 · 고르기 · 조리 · 계산이 같은 때에 같은 것으로 일어난다.
    for ( int32 minute = 0; minute < 60; ++minute )
    {
        sim.update( 1.0f );
        restored.update( 1.0f );
    }
    Archive afterOriginal;
    Archive afterRestored;
    sim.writeState( afterOriginal );
    restored.writeState( afterRestored );
    afterOriginal.writeData( originalBytes );
    afterRestored.writeData( restoredBytes );
    SW_EXPECT_TRUE( originalBytes == restoredBytes );
    RestaurantDaySummary originalSummary;
    RestaurantDaySummary restoredSummary;
    sim.closeDay( originalSummary );
    restored.closeDay( restoredSummary );
    SW_EXPECT_EQUAL( originalSummary._served, restoredSummary._served );
    SW_EXPECT_EQUAL( static_cast<int32>( originalSummary._revenue ), static_cast<int32>( restoredSummary._revenue ) );
    SW_EXPECT_EQUAL( static_cast<int32>( originalSummary._profit ), static_cast<int32>( restoredSummary._profit ) );

    RestaurantSimulation   truncated;
    RestaurantTestBorrowed truncatedBorrowed;
    world.initialize( truncated, settings, truncatedBorrowed );
    Archive cut( written.getData(), written.getSize() - 1 );
    SW_EXPECT_FALSE( truncated.readState( cut ) );
    SW_EXPECT_TRUE( truncated.getStaff().empty() );
    SW_EXPECT_FALSE( truncated.isOpen() );
}
