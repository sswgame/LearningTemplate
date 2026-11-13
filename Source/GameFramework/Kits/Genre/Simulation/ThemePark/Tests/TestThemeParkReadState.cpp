#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/Simulation/ThemePark/ThemePark.h"
#include "GameFramework/Runtime/Foundation/Framework/SessionStateRefs.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Shop.h"

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
            written << _entryFee;
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
                written << ride._cycleTimer._remaining;
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
                written << static_cast<uint8>( guest._state );
                written << static_cast<uint8>( guest._thought );
            }
            sw::StateIO::writeStepTimer( written, sw::FixedStepTimer{} );
            sw::StateIO::writeRandom( written, sw::GameRandom( 9u ) );
            written << _arrival;
            written << _runningCost;
            written << _elapsedTime;
            written << _parkRating;
            written << _nextGuestID;
            written << _totalVisitorCount;
            sw::Archive reading( written.getData(), written.getSize() );
            return inoutPark.readState( reading );
        }
    };

    struct ThemeParkReadStateTest
    {
        sw::Wallet              _wallet{};
        sw::ThemeParkSimulation _park{};

        ThemeParkReadStateTest()
        {
            _wallet.add( sw::ThemeParkSettings{}._currency, 100000 );
            sw::SessionStateRefs refs;
            refs._pWallet = &_wallet;
            _park.initialize( sw::ThemeParkSettings{}, refs );
        }
    };
} // namespace

/**
 * @brief [ThemeParkReadStateTest] 돌아간 실제 공원은 왕복하고, 형식에 맞게 꾸민 공원도 읽힌다
 */
SW_TEST_CASE_F( ThemeParkReadStateTest, RealAndForgedStatesRead )
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
    restored.initialize( sw::ThemeParkSettings{}, refs );
    sw::Archive reading( written.getData(), written.getSize() );
    SW_REQUIRE_TRUE( restored.readState( reading ) );
    SW_EXPECT_EQUAL( _park.getGuests().size(), restored.getGuests().size() );
    SW_EXPECT_EQUAL( _park.getTotalVisitorCount(), restored.getTotalVisitorCount() );

    SW_REQUIRE_TRUE( ThemeParkFixture{}.readInto( _park ) );
    SW_EXPECT_EQUAL( size_t( 2 ), _park.getGuests().size() );
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
