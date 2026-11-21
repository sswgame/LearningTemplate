#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Reflection/TypeFqn.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"
#include "Engine/Serialization/Format/Tests/StateBytesTestUtil.h"

#include "GameFramework/Kits/Genre/Simulation/ThemePark/Tests/ThemeParkTestRules.h"
#include "GameFramework/Kits/Genre/Simulation/ThemePark/ThemePark.h"
#include "GameFramework/Runtime/Foundation/Framework/SessionStateRefs.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Wallet.h"

// ThemeParkReadStateTest — 놀이공원 상태 바이트의 신뢰 경계: 음수 · 범위 밖 값, 손님과 맞지 않는 줄과 탑승자를 거절하고 공원을 그대로 둔다.
// 꾸민 바이트는 `writeState` 의 형식을 그대로 손으로 적는다. 칸마다 맞는 값을 기본으로 두고 한 칸씩 망가뜨린다.

namespace
{
    /** @brief 놀이기구 하나, 줄 선 손님 1 번, 타고 있는 손님 2 번인 공원을 꾸밉니다. */
    struct ThemeParkFixture
    {
        sw::vector<sw::ParkRide>  _listRide{};
        sw::vector<sw::ParkGuest> _listGuest{};
        float32                   _arrival{ 0.0f };
        float32                   _runningCost{ 0.5f };
        float32                   _elapsedTime{ 10.0f };
        int32                     _entryFee{ 0 };
        int32                     _parkRating{ 500 };
        uint32                    _nextGuestID{ 3 };
        uint32                    _totalVisitorCount{ 2 };

        ThemeParkFixture()
        {
            sw::ParkRide ride;
            ride._id = "wheel";
            ride._listQueue.push_back( 1 );
            ride._listRider.push_back( 2 );
            _listRide.push_back( ride );

            sw::ParkGuest queuing;
            queuing._id              = 1;
            queuing._cash            = 10;
            queuing._targetRideIndex = 0;
            queuing._state           = sw::ParkGuestState::Queuing;
            _listGuest.push_back( queuing );

            sw::ParkGuest riding = queuing;
            riding._id           = 2;
            riding._state        = sw::ParkGuestState::Riding;
            _listGuest.push_back( riding );
        }

        bool readInto( sw::ThemeParkSimulation& inoutPark ) const
        {
            sw::Archive written;
            // 리플렉션 상태 칸(머리 + 선언 순서 — 열거는 int64, 남은 운행 시간은 칸이 아니다) 뒤에 손 본문(입장료, 운행 시간, 걸음 타이머, 난수, 누적)이다
            sw::StateIO::writeReflectedHeader<sw::ThemeParkSimulation>( written );
            written << static_cast<uint32>( _listRide.size() );
            for ( const sw::ParkRide& ride : _listRide )
            {
                sw::StateIO::writeName( written, ride._id );
                sw::StateIO::writeName( written, "Wheel" ); // 표시 이름도 길이가 붙은 글자이다
                written << ride._excitement;
                written << ride._intensity;
                written << ride._nausea;
                written << ride._cycleTime;
                written << ride._entrance;
                written << ride._capacity;
                written << ride._price;
                written << ride._runningCostPerMinute;
                written << ride._bOpen;
                sw::StateIO::writeIDList( written, ride._listQueue );
                sw::StateIO::writeIDList( written, ride._listRider );
                written << ride._totalRiders;
                written << ride._totalIncome;
            }
            written << static_cast<uint32>( _listGuest.size() );
            for ( const sw::ParkGuest& guest : _listGuest )
            {
                written << guest._position;
                written << guest._walkFrom;
                written << guest._walkTo;
                written << guest._id;
                written << guest._cash;
                written << guest._targetRideIndex;
                written << guest._rideCount;
                written << guest._happiness;
                written << guest._nausea;
                written << guest._energy;
                written << guest._minIntensity;
                written << guest._maxIntensity;
                written << guest._nauseaTolerance;
                written << guest._walkTimer;
                written << guest._walkDuration;
                written << guest._queueTime;
                written << static_cast<int64>( guest._state );
                written << static_cast<int64>( guest._thought );
            }
            written << _elapsedTime;
            written << _parkRating;
            written << _nextGuestID;
            written << _totalVisitorCount;
            written << _entryFee;
            for ( const sw::ParkRide& ride : _listRide )
            {
                written << ride._cycleTimer._remaining;
            }
            sw::StateIO::writeStepTimer( written, sw::FixedStepTimer{} );
            sw::StateIO::writeRandom( written, sw::GameRandom( 9u ) );
            written << _arrival;
            written << _runningCost;
            sw::Archive reading( written.getData(), written.getSize() );
            return inoutPark.readState( reading );
        }
    };

    /** @brief 결정성 테스트의 판 하나입니다. 공원과 빌려 준 지갑을 함께 싣는다 — 지갑은 공원 밖이라 공원 바이트에 없다. */
    struct ThemeParkRun
    {
        sw::Wallet              _wallet{};
        sw::ThemeParkSimulation _park{};

        SW_DECLARE_STATE_IO_IN_PLACE()
        {
            io.value( _park );
            io.value( _wallet );
        }
    };

    struct ThemeParkReadStateTest
    {
        sw::Wallet              _wallet{};
        sw::ThemeParkSimulation _park{};

        ThemeParkReadStateTest()
        {
            _wallet.add( sw::ThemeParkTestRules::make()._currency, 100000 );
            sw::SessionStateRefs refs;
            refs._pWallet = &_wallet;
            SW_REQUIRE_TRUE( _park.initialize( sw::ThemeParkTestRules::make(), refs ) );
        }
    };
} // namespace

/**
 * @brief [ThemeParkReadStateTest] 돌아간 실제 공원은 왕복하고, 형식에 맞게 꾸민 공원도 읽힌다
 */
SW_TEST_CASE_F( ThemeParkReadStateTest, RealAndCraftedStatesRead )
{
    sw::ParkRide ride;
    ride._id        = "wheel";
    ride._capacity  = 2;
    ride._cycleTime = 4.0f;
    SW_REQUIRE_EQUAL( 0, _park.buildRide( ride, 10 ) );
    for ( int32 guestIndex = 0; guestIndex < 6; ++guestIndex )
    {
        SW_REQUIRE_TRUE( _park.admitGuest( 50, 0.0f, 9.0f, 0.9f ) );
    }
    for ( int32 frame = 0; frame < 40; ++frame )
    {
        _park.update( 0.5f );
    }

    sw::Archive written;
    _park.writeState( written );
    sw::ThemeParkSimulation restored;
    sw::SessionStateRefs    refs;
    refs._pWallet = &_wallet;
    SW_REQUIRE_TRUE( restored.initialize( sw::ThemeParkTestRules::make(), refs ) );
    sw::Archive reading( written.getData(), written.getSize() );
    SW_REQUIRE_TRUE( restored.readState( reading ) );
    SW_EXPECT_EQUAL( _park.getGuests().size(), restored.getGuests().size() );
    SW_EXPECT_EQUAL( _park.getTotalVisitorCount(), restored.getTotalVisitorCount() );

    SW_REQUIRE_TRUE( ThemeParkFixture{}.readInto( _park ) );
    SW_EXPECT_EQUAL( size_t( 2 ), _park.getGuests().size() );
}

/**
 * @brief [ThemeParkReadStateTest] 결정성: 놀이기구 둘에 손님 도착(난수) · 직접 입장 · 줄 · 탑승 · 가격 바꾸기 · 닫고 열기를 걸음 번호로 돌린 공원은 같은 바이트를 내고, 중간에 쓰고 읽은 공원도 같게 이어 돈다
 */
SW_TEST_CASE( ThemeParkSimulationTest, ParkDeterminism )
{
    const auto setup = []( ThemeParkRun& run )
    {
        const sw::ThemeParkSettings settings = sw::ThemeParkTestRules::make();
        run._wallet.add( settings._currency, 100000 );
        sw::SessionStateRefs refs;
        refs._pWallet = &run._wallet;
        return run._park.initialize( settings, refs );
    };
    const auto step = []( ThemeParkRun& run, int32 stepIndex )
    {
        sw::ThemeParkSimulation& park = run._park;
        if ( stepIndex == 0 )
        {
            sw::ParkRide wheel;
            wheel._id        = "wheel";
            wheel._capacity  = 4;
            wheel._cycleTime = 6.0f;
            wheel._entrance  = sw::float3{ 10.0f, 0.0f, 0.0f };
            sw::ParkRide coaster;
            coaster._id         = "coaster";
            coaster._excitement = 7.0f;
            coaster._intensity  = 6.0f;
            coaster._nausea     = 4.0f;
            coaster._capacity   = 8;
            coaster._cycleTime  = 12.0f;
            coaster._entrance   = sw::float3{ -20.0f, 0.0f, 15.0f };
            (void)park.buildRide( wheel, 10 );
            (void)park.buildRide( coaster, 50 );
        }
        if ( stepIndex % 5 == 2 )
            (void)park.admitGuest( 20 + stepIndex % 40, 0.0f, 3.0f + static_cast<float32>( stepIndex % 6 ), 0.5f );
        if ( stepIndex == 50 )
            park.setRidePrice( 1, 8 );
        if ( stepIndex == 80 )
            park.setRideOpen( 1, false );
        if ( stepIndex == 130 )
            park.setRideOpen( 1, true );
        park.update( 0.5f );
    };
    SW_EXPECT_TRUE( sw::StateBytesTestUtil::isDeterministic<ThemeParkRun>( setup, step, 200 ) );
}

/**
 * @brief [ThemeParkReadStateTest] 결함: 목표 없이 줄 선 손님은 `updateGuests` 가 놀이기구 표의 범위 밖을 읽었다. 없는 손님 · 겹친 손님 · 다른 놀이기구의 손님이 든 줄, 목록에 없는 탑승 손님, 정렬되지 않은 손님(이분 탐색이 깨진다)도 받았다. 거절하고 공원을 둔다
 */
SW_TEST_CASE_F( ThemeParkReadStateTest, RefusesBrokenLinks )
{
    SW_REQUIRE_TRUE( ThemeParkFixture{}.readInto( _park ) );

    ThemeParkFixture queuingNoTarget;
    queuingNoTarget._listGuest[0]._targetRideIndex = -1;
    SW_EXPECT_FALSE( queuingNoTarget.readInto( _park ) );

    ThemeParkFixture targetOutside;
    targetOutside._listGuest[0]._targetRideIndex = 1;
    SW_EXPECT_FALSE( targetOutside.readInto( _park ) );

    ThemeParkFixture queueMissing;
    queueMissing._listRide[0]._listQueue = { 7 };
    SW_EXPECT_FALSE( queueMissing.readInto( _park ) );

    ThemeParkFixture queueRepeated;
    queueRepeated._listRide[0]._listQueue = { 1, 1 };
    SW_EXPECT_FALSE( queueRepeated.readInto( _park ) );

    ThemeParkFixture queueAndRider;
    queueAndRider._listRide[0]._listQueue = { 2 };
    SW_EXPECT_FALSE( queueAndRider.readInto( _park ) );

    ThemeParkFixture notInQueue;
    notInQueue._listRide[0]._listQueue.clear();
    SW_EXPECT_FALSE( notInQueue.readInto( _park ) );

    ThemeParkFixture notRiding;
    notRiding._listRide[0]._listRider.clear();
    SW_EXPECT_FALSE( notRiding.readInto( _park ) );

    ThemeParkFixture walkingInQueue;
    walkingInQueue._listGuest[0]._state = sw::ParkGuestState::Walking;
    SW_EXPECT_FALSE( walkingInQueue.readInto( _park ) );

    ThemeParkFixture guestsUnsorted;
    guestsUnsorted._listGuest[0]._id = 2;
    guestsUnsorted._listGuest[1]._id = 1;
    SW_EXPECT_FALSE( guestsUnsorted.readInto( _park ) );

    ThemeParkFixture guestsRepeated;
    guestsRepeated._listGuest[1]._id = 1;
    SW_EXPECT_FALSE( guestsRepeated.readInto( _park ) );

    ThemeParkFixture guestAhead;
    guestAhead._nextGuestID = 2;
    SW_EXPECT_FALSE( guestAhead.readInto( _park ) );

    SW_EXPECT_EQUAL( size_t( 2 ), _park.getGuests().size() );
    SW_EXPECT_EQUAL( 0, _park.getGuests()[0]._targetRideIndex );
}

/**
 * @brief [ThemeParkReadStateTest] 결함: 놀이기구와 손님의 값 범위를 보지 않았다. 정원 0(예상 대기가 0 나누기), 음수 요금 · 운영비 · 입장료(돈이 거꾸로 간다), 정원을 넘는 탑승, 0..1 밖의 기분, 음수 돈, 뒤집힌 강도, 범위 밖 평가 · 시간 · 누적을 거절하고 공원을 둔다
 */
SW_TEST_CASE_F( ThemeParkReadStateTest, RefusesOutOfRangeValues )
{
    SW_REQUIRE_TRUE( ThemeParkFixture{}.readInto( _park ) );

    ThemeParkFixture capacityZero;
    capacityZero._listRide[0]._capacity = 0;
    SW_EXPECT_FALSE( capacityZero.readInto( _park ) );

    ThemeParkFixture ridersOver;
    ridersOver._listRide[0]._capacity = 1;
    ridersOver._listGuest[0]._state   = sw::ParkGuestState::Riding;
    ridersOver._listRide[0]._listQueue.clear();
    ridersOver._listRide[0]._listRider = { 1, 2 };
    SW_EXPECT_FALSE( ridersOver.readInto( _park ) );

    ThemeParkFixture cycleZero;
    cycleZero._listRide[0]._cycleTime = 0.0f;
    SW_EXPECT_FALSE( cycleZero.readInto( _park ) );

    ThemeParkFixture priceNegative;
    priceNegative._listRide[0]._price = -1;
    SW_EXPECT_FALSE( priceNegative.readInto( _park ) );

    ThemeParkFixture runningNegative;
    runningNegative._listRide[0]._runningCostPerMinute = -1;
    SW_EXPECT_FALSE( runningNegative.readInto( _park ) );

    ThemeParkFixture ratingNegative;
    ratingNegative._listRide[0]._excitement = -1.0f;
    SW_EXPECT_FALSE( ratingNegative.readInto( _park ) );

    ThemeParkFixture openBroken;
    openBroken._listRide[0]._bOpen = 2;
    SW_EXPECT_FALSE( openBroken.readInto( _park ) );

    ThemeParkFixture rideNoName;
    rideNoName._listRide[0]._id = sw::hashed_string{};
    SW_EXPECT_FALSE( rideNoName.readInto( _park ) );

    ThemeParkFixture feeNegative;
    feeNegative._entryFee = -1;
    SW_EXPECT_FALSE( feeNegative.readInto( _park ) );

    ThemeParkFixture cashNegative;
    cashNegative._listGuest[0]._cash = -1;
    SW_EXPECT_FALSE( cashNegative.readInto( _park ) );

    ThemeParkFixture happinessOver;
    happinessOver._listGuest[0]._happiness = 1.5f;
    SW_EXPECT_FALSE( happinessOver.readInto( _park ) );

    ThemeParkFixture nauseaNegative;
    nauseaNegative._listGuest[0]._nausea = -0.5f;
    SW_EXPECT_FALSE( nauseaNegative.readInto( _park ) );

    ThemeParkFixture energyOver;
    energyOver._listGuest[0]._energy = 1.5f;
    SW_EXPECT_FALSE( energyOver.readInto( _park ) );

    ThemeParkFixture intensityFlipped;
    intensityFlipped._listGuest[0]._minIntensity = 7.0f;
    SW_EXPECT_FALSE( intensityFlipped.readInto( _park ) );

    ThemeParkFixture parkRatingOver;
    parkRatingOver._parkRating = 1000;
    SW_EXPECT_FALSE( parkRatingOver.readInto( _park ) );

    ThemeParkFixture elapsedNegative;
    elapsedNegative._elapsedTime = -1.0f;
    SW_EXPECT_FALSE( elapsedNegative.readInto( _park ) );

    ThemeParkFixture arrivalNegative;
    arrivalNegative._arrival = -1.0f;
    SW_EXPECT_FALSE( arrivalNegative.readInto( _park ) );

    ThemeParkFixture costHuge;
    costHuge._runningCost = 1.0e20f;
    SW_EXPECT_FALSE( costHuge.readInto( _park ) );

    ThemeParkFixture visitorsFewer;
    visitorsFewer._totalVisitorCount = 1;
    SW_EXPECT_FALSE( visitorsFewer.readInto( _park ) );

    SW_EXPECT_EQUAL( 500, _park.getParkRating() );
    SW_EXPECT_EQUAL( 10, _park.getGuests()[0]._cash );
    SW_EXPECT_NEAR( 10.0f, _park.getElapsedTime(), 1.0e-5f );
}

/** @brief [ThemeParkSimulationTest] 나누는 칸이 0 인 설정 · 하한이 상한보다 큰 설정은 공원을 열지 않는다 — 받으면 도착률이 NaN 이 되어 공원이 조용히 멈춘다(감사 K-4) */
SW_TEST_CASE( ThemeParkSimulationTest, RejectsZeroDivisorSettings )
{
    SW_EXPECT_LOG( "ThemePark.RefusedSettings" );
    sw::ThemeParkSimulation park;
    sw::SessionStateRefs    refs;
    sw::ThemeParkSettings   zeroFee = sw::ThemeParkTestRules::make();
    zeroFee._noArrivalEntryFee      = 0.0f;
    SW_EXPECT_FALSE( park.initialize( zeroFee, refs ) );
    sw::ThemeParkSettings zeroScale = sw::ThemeParkTestRules::make();
    zeroScale._arrivalRatingScale   = 0.0f;
    SW_EXPECT_FALSE( park.initialize( zeroScale, refs ) );
    sw::ThemeParkSettings swappedCash = sw::ThemeParkTestRules::make();
    swappedCash._guestCashMin         = 100;
    swappedCash._guestCashMax         = 10;
    SW_EXPECT_FALSE( park.initialize( swappedCash, refs ) );
    SW_EXPECT_TRUE( park.initialize( sw::ThemeParkTestRules::make(), refs ) );
}
