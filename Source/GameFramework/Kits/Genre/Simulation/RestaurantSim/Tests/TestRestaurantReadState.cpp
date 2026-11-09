#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/Simulation/RestaurantSim/IngredientStock.h"
#include "GameFramework/Kits/Genre/Simulation/RestaurantSim/RestaurantCatalog.h"
#include "GameFramework/Kits/Genre/Simulation/RestaurantSim/RestaurantSimulation.h"
#include "GameFramework/Runtime/Foundation/Framework/SessionStateRefs.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Inventory.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Shop.h"

// RestaurantReadStateTest — 식당 상태 바이트의 신뢰 경계: 음수 값, 없는 손님 · 요리 · 직원, 겹친 줄과 자리, 규칙으로 나올 수 없는 주문을 거절하고 상태를 그대로 둔다.
// 꾸민 바이트는 `writeState` 의 형식을 그대로 손으로 적는다. 칸마다 맞는 값을 기본으로 두고 한 칸씩 망가뜨린다. 키트 GF_RestaurantSim(nogpu).

namespace
{
    constexpr const utf8* kCatalogXML = R"(<RestaurantCatalog open="11" close="21" window="2">
  <Dish id="ramen" name="Ramen" recipe="ramen" price="40" quality="1,3"/>
  <Dish id="soup" name="Soup" recipe="soup" price="20"/>
  <Customer id="student" patience="30"/>
</RestaurantCatalog>)";

    /** @brief 스테이션 한 줄입니다(`RestaurantSimulation::StationSlot` 은 비공개라 형식만 맞춥니다). */
    struct ForgedStation
    {
        sw::hashed_string _name{};
        int32             _count{ 0 };
    };

    /** @brief 재료 배치 한 줄입니다. */
    struct ForgedBatch
    {
        sw::hashed_string _itemID{ "noodle" };
        int64             _unitCost{ 1 };
        int32             _count{ 1 };
        int32             _daysLeft{ 2 };
    };

    /** @brief 요리사 하나, 앉아 요리를 기다리는 손님 하나, 조리 중인 주문 하나인 식당을 꾸밉니다. */
    struct RestaurantForged
    {
        sw::vector<sw::StaffMember>        _listStaff{};
        sw::vector<sw::RestaurantCustomer> _listCustomer{};
        sw::vector<sw::KitchenOrder>       _listOrder{};
        sw::vector<sw::MenuEntry>          _listMenu{};
        sw::vector<ForgedStation>          _listStation{};
        sw::vector<float32>                _listSatisfaction{ 0.5f };
        sw::vector<ForgedBatch>            _listBatch{ ForgedBatch{} };
        sw::RestaurantDaySummary           _today{};
        float32                            _minutes{ 5.0f };
        int64                              _pendingSpoilageCost{ 0 };
        int32                              _nextCustomerID{ 2 };

        RestaurantForged()
        {
            sw::StaffMember cook;
            cook._name         = "cook";
            cook._busyMinutes  = 3.0f;
            cook._dailyWage    = 10;
            cook._taskCustomer = 1;
            _listStaff.push_back( cook );

            sw::RestaurantCustomer customer;
            customer._typeID = "student";
            customer._dishID = "ramen";
            customer._price  = 40;
            customer._id     = 1;
            customer._seat   = 0;
            customer._state  = sw::CustomerState::WaitingFood;
            _listCustomer.push_back( customer );

            sw::KitchenOrder order;
            order._dishID     = "ramen";
            order._remaining  = 3.0f;
            order._customerID = 1;
            order._cook       = 0;
            order._quality    = 1;
            _listOrder.push_back( order );

            _listMenu.push_back( sw::MenuEntry{ "ramen", 40, SW_TRUE } );
            _listMenu.push_back( sw::MenuEntry{ "soup", 20, SW_TRUE } );
            _listStation.push_back( ForgedStation{ "stove", 1 } );
        }

        void write( sw::Archive& outArchive, const sw::ShopState& market ) const
        {
            outArchive << static_cast<uint32>( _listStaff.size() );
            for ( const sw::StaffMember& staff : _listStaff )
            {
                sw::StateIO::writeName( outArchive, staff._name );
                staff._level.writeState( outArchive );
                outArchive << staff._busyMinutes;
                outArchive << staff._dailyWage;
                outArchive << staff._taskCustomer;
                outArchive << static_cast<uint8>( staff._role );
                outArchive << staff._bHired;
            }
            outArchive << static_cast<uint32>( _listCustomer.size() );
            for ( const sw::RestaurantCustomer& customer : _listCustomer )
            {
                sw::StateIO::writeName( outArchive, customer._typeID );
                sw::StateIO::writeName( outArchive, customer._dishID );
                outArchive << customer._waited;
                outArchive << customer._patience;
                outArchive << customer._eatRemaining;
                outArchive << customer._price;
                outArchive << customer._id;
                outArchive << customer._seat;
                outArchive << customer._quality;
                outArchive << static_cast<uint8>( customer._state );
                outArchive << customer._bServing;
                outArchive << customer._bCheckingOut;
            }
            outArchive << static_cast<uint32>( _listOrder.size() );
            for ( const sw::KitchenOrder& order : _listOrder )
            {
                sw::StateIO::writeName( outArchive, order._dishID );
                sw::StateIO::writeName( outArchive, order._station );
                outArchive << order._remaining;
                outArchive << order._customerID;
                outArchive << order._cook;
                outArchive << order._quality;
                outArchive << order._bReady;
            }
            outArchive << static_cast<uint32>( _listMenu.size() );
            for ( const sw::MenuEntry& entry : _listMenu )
            {
                sw::StateIO::writeName( outArchive, entry._dishID );
                outArchive << entry._price;
                outArchive << entry._bOnMenu;
            }
            outArchive << static_cast<uint32>( _listStation.size() );
            for ( const ForgedStation& station : _listStation )
            {
                sw::StateIO::writeName( outArchive, station._name );
                outArchive << station._count;
            }
            outArchive << static_cast<uint32>( _listSatisfaction.size() );
            for ( const float32 satisfaction : _listSatisfaction )
            {
                outArchive << satisfaction;
            }
            outArchive << static_cast<uint32>( _listBatch.size() );
            for ( const ForgedBatch& batch : _listBatch )
            {
                sw::StateIO::writeName( outArchive, batch._itemID );
                outArchive << batch._unitCost;
                outArchive << batch._count;
                outArchive << batch._daysLeft;
            }
            // 조리(`Crafter`): 배운 레시피 수, 작업 수
            outArchive << static_cast<uint32>( 0 );
            outArchive << static_cast<uint32>( 0 );
            market.writeState( outArchive );
            outArchive << _today._revenue;
            outArchive << _today._tips;
            outArchive << _today._ingredientCost;
            outArchive << _today._wages;
            outArchive << _today._spoilageCost;
            outArchive << _today._profit;
            outArchive << _today._rating;
            outArchive << _today._arrivals;
            outArchive << _today._served;
            outArchive << _today._walkouts;
            outArchive << _today._noChoice;
            outArchive << _today._unservedAtClose;
            sw::StateIO::writeStepTimer( outArchive, sw::FixedStepTimer( sw::RestaurantSimulation::kStepMinutes, 24.0f * 60.0f ) );
            sw::StateIO::writeRandom( outArchive, sw::GameRandom( 9u ) );
            sw::StateIO::writeName( outArchive, "rain" );
            outArchive << _minutes;
            sw::StateIO::writeRateAccumulator( outArchive, sw::RateAccumulator{} );
            outArchive << _pendingSpoilageCost;
            outArchive << _nextCustomerID;
            outArchive << static_cast<uint8>( SW_TRUE );
        }
    };

    struct RestaurantReadStateTest
    {
        sw::RestaurantCatalog    _catalog{};
        sw::Inventory            _pantry{};
        sw::RestaurantSimulation _simulation{};

        RestaurantReadStateTest()
        {
            SW_REQUIRE_TRUE( _catalog.loadFromXMLText( kCatalogXML, "RestaurantReadStateTest" ) );
            _simulation.initialize( &_catalog, nullptr, nullptr, nullptr, nullptr, sw::SessionStateRefs{}, _pantry, sw::RestaurantSettings{} );
        }

        bool read( const RestaurantForged& forged )
        {
            sw::Archive written;
            forged.write( written, _simulation.getMarket() );
            sw::Archive reading( written.getData(), written.getSize() );
            return _simulation.readState( reading );
        }
    };
} // namespace

/**
 * @brief [RestaurantReadStateTest] 실제 식당은 왕복하고, 형식에 맞게 꾸민 식당도 읽힌다
 */
SW_TEST_CASE_F( RestaurantReadStateTest, RealAndForgedStatesRead )
{
    (void)_simulation.hireStaff( "cook", sw::StaffRole::Cook, 10 );
    _simulation.setStationCount( "stove", 2 );
    SW_EXPECT_TRUE( _simulation.setMenuPrice( "ramen", 55 ) );
    _simulation.openDay( "rain" );
    const int32 customerID = _simulation.admitCustomer( "student" );
    SW_REQUIRE_EQUAL( 1, customerID );

    sw::Archive written;
    _simulation.writeState( written );
    sw::Archive reading( written.getData(), written.getSize() );
    SW_REQUIRE_TRUE( _simulation.readState( reading ) );
    SW_EXPECT_EQUAL( size_t( 1 ), _simulation.getCustomers().size() );
    SW_EXPECT_EQUAL( int64( 55 ), _simulation.getMenuPrice( "ramen" ) );

    SW_REQUIRE_TRUE( read( RestaurantForged{} ) );
    SW_EXPECT_EQUAL( size_t( 1 ), _simulation.getOrders().size() );
    SW_EXPECT_EQUAL( 1, _simulation.countOccupiedSeats() );
}

/**
 * @brief [RestaurantReadStateTest] 결함: 음수 임금(`trySpend` 가 돈을 만든다), 음수 가격, 음수 시간 · 양, 범위 밖 만족도와 별점을 받았다. 거절하고 식당을 둔다
 */
SW_TEST_CASE_F( RestaurantReadStateTest, RefusesNegativeValues )
{
    SW_REQUIRE_TRUE( read( RestaurantForged{} ) );

    RestaurantForged wage;
    wage._listStaff[0]._dailyWage = -1;
    SW_EXPECT_FALSE( read( wage ) );

    RestaurantForged busy;
    busy._listStaff[0]._busyMinutes = -1.0f;
    SW_EXPECT_FALSE( read( busy ) );

    RestaurantForged customerPrice;
    customerPrice._listCustomer[0]._price = -1;
    SW_EXPECT_FALSE( read( customerPrice ) );

    RestaurantForged waited;
    waited._listCustomer[0]._waited = -1.0f;
    SW_EXPECT_FALSE( read( waited ) );

    RestaurantForged patience;
    patience._listCustomer[0]._patience = -1.0f;
    SW_EXPECT_FALSE( read( patience ) );

    RestaurantForged menuPrice;
    menuPrice._listMenu[0]._price = 0;
    SW_EXPECT_FALSE( read( menuPrice ) );

    RestaurantForged stationCount;
    stationCount._listStation[0]._count = -1;
    SW_EXPECT_FALSE( read( stationCount ) );

    RestaurantForged stationName;
    stationName._listStation[0]._name = sw::hashed_string{};
    SW_EXPECT_FALSE( read( stationName ) );

    RestaurantForged satisfactionOver;
    satisfactionOver._listSatisfaction[0] = 1.5f;
    SW_EXPECT_FALSE( read( satisfactionOver ) );

    RestaurantForged satisfactionNegative;
    satisfactionNegative._listSatisfaction[0] = -0.5f;
    SW_EXPECT_FALSE( read( satisfactionNegative ) );

    RestaurantForged satisfactionLong;
    satisfactionLong._listSatisfaction = { 0.5f, 0.5f, 0.5f }; // 별점 창은 둘이다
    SW_EXPECT_FALSE( read( satisfactionLong ) );

    RestaurantForged revenue;
    revenue._today._revenue = -1;
    SW_EXPECT_FALSE( read( revenue ) );

    RestaurantForged arrivals;
    arrivals._today._arrivals = -1;
    SW_EXPECT_FALSE( read( arrivals ) );

    RestaurantForged rating;
    rating._today._rating = 6.0f;
    SW_EXPECT_FALSE( read( rating ) );

    RestaurantForged minutes;
    minutes._minutes = -1.0f;
    SW_EXPECT_FALSE( read( minutes ) );

    RestaurantForged pending;
    pending._pendingSpoilageCost = -1;
    SW_EXPECT_FALSE( read( pending ) );

    RestaurantForged nextID;
    nextID._nextCustomerID = 0;
    SW_EXPECT_FALSE( read( nextID ) );

    SW_EXPECT_NEAR( 5.0f, _simulation.getMinutes(), 1.0e-5f );
    SW_EXPECT_EQUAL( int64( 10 ), _simulation.getStaff()[0]._dailyWage );
    SW_EXPECT_EQUAL( int64( 40 ), _simulation.getCustomers()[0]._price );
}

/**
 * @brief [RestaurantReadStateTest] 결함: 손님과 주문이 서로와 카탈로그에 맞는지 보지 않았다. 겹친 id · 자리, 없는 성향 · 요리, 자리 없이 앉은 손님, 다음 번호를 넘는 id, 최대를 넘는 품질, 요리를 기다리지 않는 손님의 주문(나르면 또 계산한다), 요리사 아닌 직원의 주문을 거절하고 식당을 둔다
 */
SW_TEST_CASE_F( RestaurantReadStateTest, RefusesBrokenLinks )
{
    SW_REQUIRE_TRUE( read( RestaurantForged{} ) );

    RestaurantForged idRepeated;
    idRepeated._listCustomer.push_back( idRepeated._listCustomer[0] );
    idRepeated._listCustomer[1]._seat = 1;
    SW_EXPECT_FALSE( read( idRepeated ) );

    RestaurantForged seatRepeated;
    seatRepeated._listCustomer.push_back( seatRepeated._listCustomer[0] );
    seatRepeated._listCustomer[1]._id = 2;
    seatRepeated._nextCustomerID      = 3;
    SW_EXPECT_FALSE( read( seatRepeated ) );

    RestaurantForged seatOutside;
    seatOutside._listCustomer[0]._seat = 4; // 설정의 자리는 넷이다
    SW_EXPECT_FALSE( read( seatOutside ) );

    RestaurantForged seatedNowhere;
    seatedNowhere._listCustomer[0]._seat = -1;
    SW_EXPECT_FALSE( read( seatedNowhere ) );

    RestaurantForged queuedOnSeat;
    queuedOnSeat._listOrder.clear();
    queuedOnSeat._listStaff[0]._taskCustomer = -1;
    queuedOnSeat._listCustomer[0]._state     = sw::CustomerState::Queued;
    SW_EXPECT_FALSE( read( queuedOnSeat ) );

    RestaurantForged typeUnknown;
    typeUnknown._listCustomer[0]._typeID = "ghost";
    SW_EXPECT_FALSE( read( typeUnknown ) );

    RestaurantForged dishUnknown;
    dishUnknown._listCustomer[0]._dishID = "stone";
    SW_EXPECT_FALSE( read( dishUnknown ) );

    RestaurantForged idAhead;
    idAhead._nextCustomerID = 1;
    SW_EXPECT_FALSE( read( idAhead ) );

    RestaurantForged qualityOver;
    qualityOver._listCustomer[0]._quality = 3; // 라멘의 품질은 둘까지다
    SW_EXPECT_FALSE( read( qualityOver ) );

    RestaurantForged taskMissing;
    taskMissing._listStaff[0]._taskCustomer = 7;
    SW_EXPECT_FALSE( read( taskMissing ) );

    RestaurantForged taskBroken;
    taskBroken._listStaff[0]._taskCustomer = -2;
    SW_EXPECT_FALSE( read( taskBroken ) );

    RestaurantForged firedWorking;
    firedWorking._listStaff[0]._bHired = SW_FALSE;
    SW_EXPECT_FALSE( read( firedWorking ) );

    RestaurantForged orderMissing;
    orderMissing._listOrder[0]._customerID = 7;
    SW_EXPECT_FALSE( read( orderMissing ) );

    RestaurantForged orderOfPaid;
    orderOfPaid._listCustomer[0]._state = sw::CustomerState::Paid;
    orderOfPaid._listCustomer[0]._seat  = -1;
    SW_EXPECT_FALSE( read( orderOfPaid ) );

    RestaurantForged orderDish;
    orderDish._listOrder[0]._dishID = "stone";
    SW_EXPECT_FALSE( read( orderDish ) );

    RestaurantForged orderQuality;
    orderQuality._listOrder[0]._quality = 3;
    SW_EXPECT_FALSE( read( orderQuality ) );

    RestaurantForged orderCookOutside;
    orderCookOutside._listOrder[0]._cook = 1;
    SW_EXPECT_FALSE( read( orderCookOutside ) );

    RestaurantForged orderServer;
    orderServer._listStaff[0]._role = sw::StaffRole::Server;
    SW_EXPECT_FALSE( read( orderServer ) );

    RestaurantForged orderReadyCooking;
    orderReadyCooking._listOrder[0]._bReady = SW_TRUE;
    SW_EXPECT_FALSE( read( orderReadyCooking ) );

    RestaurantForged orderRemaining;
    orderRemaining._listOrder[0]._remaining = -1.0f;
    SW_EXPECT_FALSE( read( orderRemaining ) );

    RestaurantForged menuUnknown;
    menuUnknown._listMenu[1]._dishID = "stone";
    SW_EXPECT_FALSE( read( menuUnknown ) );

    RestaurantForged menuRepeated;
    menuRepeated._listMenu[1]._dishID = "ramen";
    SW_EXPECT_FALSE( read( menuRepeated ) );

    RestaurantForged stationRepeated;
    stationRepeated._listStation.push_back( stationRepeated._listStation[0] );
    SW_EXPECT_FALSE( read( stationRepeated ) );

    SW_EXPECT_EQUAL( 1, _simulation.getCustomers()[0]._id );
    SW_EXPECT_EQUAL( size_t( 1 ), _simulation.getOrders().size() );
    SW_EXPECT_EQUAL( 0, _simulation.getOrders()[0]._cook );
}

/**
 * @brief [RestaurantReadStateTest] 결함: 재료 배치가 빈 이름, 음수 원가(먹이면 원가가 줄어든다), 남은 날 0 · -2(하루 지나면 상하지 않는 배치가 된다)를 받았다. 거절하고 재고를 둔다
 */
SW_TEST_CASE_F( RestaurantReadStateTest, RefusesBrokenBatches )
{
    SW_REQUIRE_TRUE( read( RestaurantForged{} ) );

    RestaurantForged nameEmpty;
    nameEmpty._listBatch[0]._itemID = sw::hashed_string{};
    SW_EXPECT_FALSE( read( nameEmpty ) );

    RestaurantForged costNegative;
    costNegative._listBatch[0]._unitCost = -1;
    SW_EXPECT_FALSE( read( costNegative ) );

    RestaurantForged countNegative;
    countNegative._listBatch[0]._count = -1;
    SW_EXPECT_FALSE( read( countNegative ) );

    RestaurantForged daysZero;
    daysZero._listBatch[0]._daysLeft = 0;
    SW_EXPECT_FALSE( read( daysZero ) );

    RestaurantForged daysBroken;
    daysBroken._listBatch[0]._daysLeft = -2;
    SW_EXPECT_FALSE( read( daysBroken ) );

    RestaurantForged neverSpoils;
    neverSpoils._listBatch[0]._daysLeft = -1;
    SW_REQUIRE_TRUE( read( neverSpoils ) );
    SW_EXPECT_FALSE( _simulation.getStock().findEarliestExpiry( "noodle" ).has_value() );
}

/**
 * @brief [RestaurantReadStateTest] 결함: 시장이 음수 배율(`setPriceModifier` 는 거절한다)과 음수 날을 받았다. 기반 `ShopState` 가 거절하고 상태를 둔다
 */
SW_TEST_CASE( RestaurantReadStateTestShop, ShopStateRefusesNegativePrices )
{
    const auto readForged = []( float32 sellFactor, float32 buyModifier, float32 sellModifier, int32 daysSinceRestock )
    {
        sw::Archive written;
        written << static_cast<uint32>( 1 );
        sw::StateIO::writeName( written, "market" );
        written << static_cast<uint32>( 0 ); // 재고
        written << static_cast<uint32>( 1 ); // 포화 품목
        sw::StateIO::writeName( written, "noodle" );
        written << sellFactor;
        written << static_cast<uint32>( 0 ); // 거절 분류
        written << buyModifier;
        written << sellModifier;
        written << daysSinceRestock;
        sw::ShopState state;
        state.initialize( nullptr, nullptr );
        sw::Archive reading( written.getData(), written.getSize() );
        return state.readState( reading );
    };
    SW_EXPECT_TRUE( readForged( 1.0f, 1.0f, 1.0f, 0 ) ); // 형식은 맞다(모르는 가게는 버린다)
    SW_EXPECT_FALSE( readForged( -1.0f, 1.0f, 1.0f, 0 ) );
    SW_EXPECT_FALSE( readForged( 1.0f, -1.0f, 1.0f, 0 ) );
    SW_EXPECT_FALSE( readForged( 1.0f, 1.0f, -1.0f, 0 ) );
    SW_EXPECT_FALSE( readForged( 1.0f, 1.0f, 1.0f, -1 ) );
}
