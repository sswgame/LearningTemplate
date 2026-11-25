// ThemeParkWalkTest — 손님은 막힌 칸(가판 · 지을 땅)을 돌아 놀이기구 줄에 서고, 긴 줄도 막힌 칸을 돌아 늘어서며, 걷던 길이 상태로 왕복하며, 격자가 없으면 곧게 걷는다(nogpu, 에셋 없음).
#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Kits/Genre/Simulation/ThemePark/Tests/ThemeParkTestRules.h"
#include "GameFramework/Kits/Genre/Simulation/ThemePark/ThemePark.h"
#include "GameFramework/Runtime/Character/Navigation/NavGrid.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Wallet.h"

using namespace sw;

namespace
{
    constexpr int32   kThemeParkWalkGridSize  = 40;
    constexpr int32   kThemeParkWalkStepLimit = 400;
    constexpr float32 kThemeParkWalkStep      = 0.25f;

    /** @brief 정문(0.5, 0.5)과 놀이기구 입구(0.5, 20.5) 사이를 벽(x -5..5, z 10..11)이 막는 공원입니다. 두 자리는 칸 가운데다. */
    struct ThemeParkWalkPark
    {
        NavGrid             _grid{};
        Wallet              _wallet{};
        ThemeParkSettings   _settings{ ThemeParkTestRules::make() };
        ThemeParkSimulation _park{};

        ThemeParkWalkPark()
        {
            SW_REQUIRE_TRUE( _grid.initialize( kThemeParkWalkGridSize, kThemeParkWalkGridSize, 1.0f, float3{ -20.0f, 0.0f, -10.0f } ) );
            _grid.setAreaCost( 15, 20, 24, 20, kNavBlockedCost );
            _settings._gatePosition          = float3{ 0.5f, 0.0f, 0.5f };
            _settings._guestArrivalPerMinute = 0.0f;
            _settings._fixedStep             = kThemeParkWalkStep;
            _wallet.add( _settings._currency, 10000 );
            SW_REQUIRE_TRUE( _park.initialize( _settings, _wallet, _grid ) );
            ParkRide ride;
            ride._id       = hashed_string( "wheel" );
            ride._entrance = float3{ 0.5f, 0.0f, 20.5f };
            SW_REQUIRE_TRUE( _park.buildRide( ride, 0 ) == 0 );
            SW_REQUIRE_TRUE( _park.admitGuest( 50, 0.0f, 10.0f, 1.0f ) );
        }

        bool isGuestOnBlockedCell() const { return _grid.isWalkable( _grid.computeCell( _park.getGuests().front()._position ) ) == false; }
    };
} // namespace

/** @brief [ThemeParkWalkTest] 격자를 빌린 공원의 손님은 벽을 돌아 줄에 선다 — 걷는 내내 막힌 칸을 밟지 않고, 도중의 상태가 같은 길로 이어진다 */
SW_TEST_CASE( ThemeParkWalkTest, GuestWalksAroundBlockedCells )
{
    ThemeParkWalkPark test;
    bool              bRestored = false;
    int32             step      = 0;
    for ( ; step < kThemeParkWalkStepLimit && test._park.getGuests().front()._state == ParkGuestState::Walking; ++step )
    {
        test._park.update( kThemeParkWalkStep );
        SW_EXPECT_FALSE( test.isGuestOnBlockedCell() );
        if ( bRestored || test._park.getGuests().front()._listWalkPoint.empty() )
            continue;
        Archive written;
        test._park.writeState( written );
        Archive             reading( written.getData(), written.getSize() );
        ThemeParkSimulation restored;
        SW_REQUIRE_TRUE( restored.initialize( test._settings, test._wallet, test._grid ) );
        SW_REQUIRE_TRUE( restored.readState( reading ) );
        SW_EXPECT_EQUAL( test._park.getGuests().front()._listWalkPoint.size(), restored.getGuests().front()._listWalkPoint.size() );
        bRestored = true;
    }
    SW_EXPECT_TRUE( bRestored ); // 벽을 돌았다(점이 여럿인 길)
    SW_EXPECT_TRUE( test._park.getGuests().front()._state != ParkGuestState::Walking );
}

/** @brief [ThemeParkWalkTest] 칸이 없는 격자면 곧게 걸어 벽 칸을 지난다 — 막는 것은 격자다 */
SW_TEST_CASE( ThemeParkWalkTest, EmptyGridGuestWalksStraight )
{
    const NavGrid     emptyGrid{};
    ThemeParkWalkPark test;
    SW_REQUIRE_TRUE( test._park.initialize( test._settings, test._wallet, emptyGrid ) );
    ParkRide ride;
    ride._id       = hashed_string( "wheel" );
    ride._entrance = float3{ 0.5f, 0.0f, 20.5f };
    SW_REQUIRE_TRUE( test._park.buildRide( ride, 0 ) == 0 );
    SW_REQUIRE_TRUE( test._park.admitGuest( 50, 0.0f, 10.0f, 1.0f ) );
    bool bCrossed = false;
    for ( int32 step = 0; step < kThemeParkWalkStepLimit && test._park.getGuests().front()._state == ParkGuestState::Walking; ++step )
    {
        test._park.update( kThemeParkWalkStep );
        bCrossed = bCrossed || test.isGuestOnBlockedCell();
    }
    SW_EXPECT_TRUE( bCrossed );
}

/** @brief [ThemeParkWalkTest] 벽 너머까지 닿는 긴 줄도 걷는 길을 따라 늘어서 막힌 칸에 서지 않고, 줄이 길어도 정문을 넘지 않는다 */
SW_TEST_CASE( ThemeParkWalkTest, LongQueueFollowsTheWalkRoute )
{
    constexpr int32   kGuestCount = 60; // 간격 0.6 m — 길(약 23 m)보다 길다
    ThemeParkWalkPark test;
    ThemeParkSettings settings = test._settings;
    settings._queuePatience    = 100000.0f;
    SW_REQUIRE_TRUE( test._park.initialize( settings, test._wallet, test._grid ) );
    ParkRide ride;
    ride._id        = hashed_string( "wheel" );
    ride._entrance  = float3{ 0.5f, 0.0f, 20.5f };
    ride._capacity  = 1;
    ride._cycleTime = 1000.0f;
    SW_REQUIRE_TRUE( test._park.buildRide( ride, 0 ) == 0 );
    for ( int32 guest = 0; guest < kGuestCount; ++guest )
    {
        SW_REQUIRE_TRUE( test._park.admitGuest( 50, 0.0f, 10.0f, 1.0f ) );
    }
    for ( int32 step = 0; step < kThemeParkWalkStepLimit && test._park.getRides()[0]._listQueue.size() + 1 < static_cast<size_t>( kGuestCount ); ++step )
    {
        test._park.update( kThemeParkWalkStep );
    }
    test._park.update( kThemeParkWalkStep );
    SW_REQUIRE_EQUAL( size_t( kGuestCount - 1 ), test._park.getRides()[0]._listQueue.size() );
    bool bPastWall = false;
    for ( const ParkGuest& guest : test._park.getGuests() )
    {
        SW_EXPECT_TRUE( test._grid.isWalkable( test._grid.computeCell( guest._position ) ) );
        SW_EXPECT_TRUE( guest._position._z >= 0.5f );
        bPastWall = bPastWall || guest._position._z < 10.0f;
    }
    SW_EXPECT_TRUE( bPastWall );
}
