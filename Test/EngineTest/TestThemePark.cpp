#include "pch.h"

#include "GameFramework/Kits/ThemePark/CoasterTrain.h"
#include "GameFramework/Kits/ThemePark/ThemePark.h"

#include "TestFramework/TestFramework.h"

// 테마파크 키트의 경영 시뮬레이션(롤러코스터 타이쿤) — 손님이 강도 · 값 · 줄로 놀이기구를 고르고, 놀이기구가 정원만큼 태워 표를 받고, 입장료가
// 손님 도착을 줄이고, 지친 손님이 떠나는지 본다. 난수는 씨앗으로 고정한다.

using namespace sw;

namespace
{
    ParkRide makeThemeParkTestRide( float32 excitement, float32 intensity, int32 price, int32 capacity, float32 cycleTime )
    {
        ParkRide ride;
        ride._id                   = hashed_string( "TestRide" );
        ride._name                 = "Test Ride";
        ride._excitement           = excitement;
        ride._intensity            = intensity;
        ride._nausea               = 1.0f;
        ride._price                = price;
        ride._capacity             = capacity;
        ride._cycleTime            = cycleTime;
        ride._runningCostPerMinute = 0;
        ride._entrance             = float3{ 10.0f, 0.0f, 0.0f };
        return ride;
    }

    /** @brief 손님이 저절로 오지 않는 공원입니다(시험이 `admitGuest` 로만 들인다). */
    ThemeParkSettings makeClosedGateSettings()
    {
        ThemeParkSettings settings;
        settings._guestArrivalPerMinute = 0.0f;
        settings._walkSpeed             = 10.0f;
        settings._randomSeed            = 7u;
        return settings;
    }
} // namespace

/**
 * @brief [ThemeParkTest] 놀이기구는 정원만큼 태워 한 바퀴를 돌고 탑승료를 받는다 — 받은 돈은 탄 사람 × 값이다
 */
SW_TEST_CASE( ThemeParkTest, RideCyclesCarryCapacityAndCollectTickets )
{
    ThemeParkSimulation park;
    park.initialize( makeClosedGateSettings(), 1000 );
    const int32 rideIndex = park.buildRide( makeThemeParkTestRide( 5.0f, 3.0f, 4, 4, 10.0f ), 600 );
    SW_ASSERT_TRUE( rideIndex == 0 );
    SW_EXPECT_EQUAL( 400, park.getCash() );
    for ( int32 guestIndex = 0; guestIndex < 8; ++guestIndex )
        SW_EXPECT_TRUE( park.admitGuest( 100, 0.0f, 9.0f, 1.0f ) );

    park.update( 3.0f ); // 걸어가 줄 선다(10 m / 10 m/s)
    const ParkRide& ride = park.getRides()[0];
    SW_EXPECT_EQUAL( static_cast<size_t>( 4 ), ride._listRider.size() );
    for ( int32 stepIndex = 0; stepIndex < 30; ++stepIndex )
        park.update( 1.0f );

    SW_EXPECT_TRUE( ride._totalRiders >= 8u );
    SW_EXPECT_EQUAL( static_cast<int32>( ride._totalRiders + ride._listRider.size() ) * 4, ride._totalIncome );
    SW_EXPECT_EQUAL( 400 + ride._totalIncome, park.getCash() );
    SW_EXPECT_TRUE( park.getAverageHappiness() > 0.7f ); // 탄 뒤 즐거워졌다
}

/**
 * @brief [ThemeParkTest] 손님은 자기 상한보다 센 놀이기구 · 가치의 두 배보다 비싼 놀이기구를 타지 않고 그 이유를 생각한다
 */
SW_TEST_CASE( ThemeParkTest, GuestsRefuseTooIntenseOrOverpricedRides )
{
    ThemeParkSimulation intensePark;
    intensePark.initialize( makeClosedGateSettings(), 1000 );
    (void)intensePark.buildRide( makeThemeParkTestRide( 8.0f, 9.0f, 3, 8, 10.0f ), 0 );
    for ( int32 guestIndex = 0; guestIndex < 6; ++guestIndex )
        SW_EXPECT_TRUE( intensePark.admitGuest( 100, 0.0f, 6.0f, 1.0f ) );
    intensePark.update( 5.0f );
    SW_EXPECT_EQUAL( 0u, intensePark.getRides()[0]._totalRiders );
    SW_EXPECT_TRUE( intensePark.getRides()[0]._listRider.empty() );
    SW_EXPECT_EQUAL( 6u, intensePark.countGuestsThinking( ParkGuestThought::TooIntense ) );

    ThemeParkSimulation pricedPark;
    pricedPark.initialize( makeClosedGateSettings(), 1000 );
    (void)pricedPark.buildRide( makeThemeParkTestRide( 5.0f, 3.0f, 25, 8, 10.0f ), 0 ); // 가치 10 → 21 넘으면 비싸다
    for ( int32 guestIndex = 0; guestIndex < 6; ++guestIndex )
        SW_EXPECT_TRUE( pricedPark.admitGuest( 100, 0.0f, 9.0f, 1.0f ) );
    pricedPark.update( 5.0f );
    SW_EXPECT_TRUE( pricedPark.getRides()[0]._listRider.empty() );
    SW_EXPECT_EQUAL( 6u, pricedPark.countGuestsThinking( ParkGuestThought::TooExpensive ) );

    pricedPark.setRidePrice( 0, 15 );
    pricedPark.update( 10.0f );
    SW_EXPECT_TRUE( pricedPark.getRides()[0]._totalRiders + pricedPark.getRides()[0]._listRider.size() > 0u );
}

/**
 * @brief [ThemeParkTest] 입장료는 도착을 줄이고(100 이면 아무도 안 온다) 받은 입장료는 돈이 된다
 */
SW_TEST_CASE( ThemeParkTest, EntryFeeLowersArrivalsAndIsCollected )
{
    ThemeParkSettings settings;
    settings._guestArrivalPerMinute = 30.0f;
    settings._randomSeed            = 99u;
    settings._maxGuests             = 1000;

    ThemeParkSimulation freePark;
    freePark.initialize( settings, 0 );
    (void)freePark.buildRide( makeThemeParkTestRide( 5.0f, 3.0f, 2, 8, 10.0f ), 0 );
    ThemeParkSimulation paidPark;
    settings._entryFee = 15;
    paidPark.initialize( settings, 0 );
    (void)paidPark.buildRide( makeThemeParkTestRide( 5.0f, 3.0f, 2, 8, 10.0f ), 0 );
    ThemeParkSimulation closedPark;
    settings._entryFee = 100;
    closedPark.initialize( settings, 0 );
    (void)closedPark.buildRide( makeThemeParkTestRide( 5.0f, 3.0f, 2, 8, 10.0f ), 0 );

    for ( int32 stepIndex = 0; stepIndex < 600; ++stepIndex )
    {
        freePark.update( 1.0f );
        paidPark.update( 1.0f );
        closedPark.update( 1.0f );
    }
    SW_EXPECT_TRUE( freePark.getTotalVisitorCount() > 50u );
    SW_EXPECT_TRUE( paidPark.getTotalVisitorCount() < freePark.getTotalVisitorCount() );
    SW_EXPECT_TRUE( paidPark.getTotalVisitorCount() > 0u );
    SW_EXPECT_EQUAL( 0u, closedPark.getTotalVisitorCount() );
    // 입장료 수입은 들어온 손님 × 15 이상이다(탑승료가 더해진다).
    SW_EXPECT_TRUE( paidPark.getCash() >= static_cast<int32>( paidPark.getTotalVisitorCount() ) * 15 );
}

/**
 * @brief [ThemeParkTest] 지친 손님은 정문으로 걸어가 떠난다 · 운영비는 열린 놀이기구마다 분당 나간다
 */
SW_TEST_CASE( ThemeParkTest, TiredGuestsLeaveAndRunningCostsAreCharged )
{
    ThemeParkSettings settings     = makeClosedGateSettings();
    settings._energyDrainPerSecond = 0.05f; // 18 초면 바닥
    ThemeParkSimulation park;
    park.initialize( settings, 1000 );
    ParkRide ride              = makeThemeParkTestRide( 5.0f, 3.0f, 0, 8, 10.0f );
    ride._runningCostPerMinute = 60; // 초당 1
    (void)park.buildRide( ride, 0 );
    for ( int32 guestIndex = 0; guestIndex < 5; ++guestIndex )
        SW_EXPECT_TRUE( park.admitGuest( 100, 0.0f, 9.0f, 1.0f ) );

    for ( int32 stepIndex = 0; stepIndex < 60; ++stepIndex )
        park.update( 1.0f );
    SW_EXPECT_EQUAL( 0u, park.getGuestCount() );
    SW_EXPECT_EQUAL( 5u, park.getTotalVisitorCount() );
    SW_EXPECT_EQUAL( 940, park.getCash() );

    park.setRideOpen( 0, false );
    park.update( 60.0f ); // 한 프레임 상한(5 초)에 잘린다
    SW_EXPECT_EQUAL( 940, park.getCash() );
}

/**
 * @brief [ThemeParkTest] 코스터 시험 운행 결과로 놀이기구를 만들면 평가 셋이 옮겨지고 한 바퀴 시간에 태우는 시간이 더해진다
 */
SW_TEST_CASE( ThemeParkTest, CoasterStatsBecomeARide )
{
    CoasterRideStats stats;
    stats._lapTime      = 40.0f;
    stats._excitement   = 6.5f;
    stats._intensity    = 5.0f;
    stats._nausea       = 3.0f;
    stats._bCompleted   = SW_TRUE;
    const ParkRide ride = ThemeParkSimulation::makeRideFromCoaster( "Coaster", "Big One", stats, 12, 15.0f );
    SW_EXPECT_NEAR_EQUAL( 55.0f, ride._cycleTime, 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( 6.5f, ride._excitement, 1.0e-4f );
    SW_EXPECT_EQUAL( 12, ride._capacity );
    SW_EXPECT_EQUAL( 13, ride._price );
}
