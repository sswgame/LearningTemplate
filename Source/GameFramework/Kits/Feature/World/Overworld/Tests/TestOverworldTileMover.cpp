#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Kits/Feature/World/Overworld/OverworldTileMover.h"
#include "GameFramework/Kits/Feature/World/Overworld/TileMap.h"
#include "GameFramework/Kits/Feature/World/Overworld/ZoneTracker.h"
#include "GameFramework/Runtime/Character/Control/Intent/ControlIntent.h"

using namespace sw;

namespace
{
    /** @brief 가운데에 선 폰과 모두 걸을 수 있는 5 x 5 맵입니다. */
    struct OverworldFixture
    {
        static constexpr int32   kInteractButton = 0;
        static constexpr float32 kStepSeconds    = 0.1f;

        TileMap            _map;
        OverworldTileMover _mover;

        OverworldFixture()
        {
            _map.resize( 5, 5 );
            for ( int32 y = 0; y < 5; ++y )
            {
                for ( int32 x = 0; x < 5; ++x )
                {
                    _map.setWalkable( x, y, true );
                }
            }
            _mover.setTileMap( &_map );
            OverworldTileMoverSettings settings;
            settings._stepSeconds   = kStepSeconds;
            settings._encounterRate = 0.5f;
            _mover.setSettings( settings );
            _mover.setPosition( 2, 2 );
        }

        void push( float32 x, float32 y )
        {
            ControlIntent intent;
            intent._move = float2{ x, y };
            _mover.update( 0.0f, intent, kInteractButton );
        }

        void finishStep()
        {
            ControlIntent intent;
            _mover.update( kStepSeconds * 2.0f, intent, kInteractButton );
        }

        void walkRight()
        {
            push( 1.0f, 0.0f );
            finishStep();
        }

        static vector<uint8> save( const OverworldTileMover& mover )
        {
            Archive archive;
            mover.writeState( archive );
            return vector<uint8>( archive.getData(), archive.getData() + archive.getSize() );
        }
    };
} // namespace

SW_TEST_CASE( OverworldTileMoverTest, AxisMapsToOneTileAndYIsUp )
{
    OverworldFixture rig;
    rig.push( 0.0f, 1.0f );
    SW_EXPECT_EQUAL( 2, rig._mover.getTileX() );
    SW_EXPECT_EQUAL( 1, rig._mover.getTileY() );
    SW_EXPECT_TRUE( rig._mover.getLocomotion().getFacing() == FacingDir::Up );
    SW_EXPECT_TRUE( rig._mover.getLocomotion().getState() == LocomotionState::Walk );
    SW_EXPECT_TRUE( rig._mover.consumeMovedFlag() );
    SW_EXPECT_FALSE( rig._mover.consumeMovedFlag() );

    int32 facingX = 0;
    int32 facingY = 0;
    rig._mover.getFacingTile( facingX, facingY );
    SW_EXPECT_EQUAL( 2, facingX );
    SW_EXPECT_EQUAL( 0, facingY );
}

SW_TEST_CASE( OverworldTileMoverTest, DeadZoneAndUnfinishedStepIgnoreInput )
{
    OverworldFixture rig;
    rig.push( 0.4f, 0.4f );
    SW_EXPECT_EQUAL( 2, rig._mover.getTileX() );
    SW_EXPECT_EQUAL( 2, rig._mover.getTileY() );

    rig.push( 1.0f, 0.0f );
    rig.push( 1.0f, 0.0f ); // 걸음이 끝나기 전 입력은 받지 않는다
    SW_EXPECT_EQUAL( 3, rig._mover.getTileX() );
    rig.finishStep();
    SW_EXPECT_TRUE( rig._mover.getLocomotion().getState() == LocomotionState::Idle );
    rig.push( 1.0f, 0.0f );
    SW_EXPECT_EQUAL( 4, rig._mover.getTileX() );
}

SW_TEST_CASE( OverworldTileMoverTest, BlockedTileOnlyTurns )
{
    OverworldFixture rig;
    rig._map.setWalkable( 3, 2, false );
    rig.push( 1.0f, 0.0f );
    SW_EXPECT_EQUAL( 2, rig._mover.getTileX() );
    SW_EXPECT_TRUE( rig._mover.getLocomotion().getFacing() == FacingDir::Right );
    SW_EXPECT_TRUE( rig._mover.getLocomotion().getState() == LocomotionState::Idle );
    SW_EXPECT_FALSE( rig._mover.consumeMovedFlag() );

    rig._mover.setPosition( 4, 4 );
    rig.push( 1.0f, 0.0f ); // 맵 밖도 막힌 칸이다
    SW_EXPECT_EQUAL( 4, rig._mover.getTileX() );
}

SW_TEST_CASE( OverworldTileMoverTest, DisabledInputStillFinishesTheStep )
{
    OverworldFixture rig;
    rig.push( 1.0f, 0.0f );
    rig._mover.setInputEnabled( false );
    rig.finishStep(); // 화면 전환 중에도 걸음은 끝난다
    SW_EXPECT_TRUE( rig._mover.getLocomotion().getState() == LocomotionState::Idle );
    rig.push( 1.0f, 0.0f );
    SW_EXPECT_EQUAL( 3, rig._mover.getTileX() );
}

SW_TEST_CASE( OverworldTileMoverTest, InteractWinsOverMoveAndIsConsumedOnce )
{
    OverworldFixture rig;
    ControlIntent    intent;
    intent._move = float2{ 1.0f, 0.0f };
    intent.setButton( OverworldFixture::kInteractButton, true, true );
    rig._mover.update( 0.0f, intent, OverworldFixture::kInteractButton );
    SW_EXPECT_EQUAL( 2, rig._mover.getTileX() );
    SW_EXPECT_TRUE( rig._mover.getLocomotion().getState() == LocomotionState::Interact );
    SW_EXPECT_TRUE( rig._mover.consumeInteractRequest() );
    SW_EXPECT_FALSE( rig._mover.consumeInteractRequest() );

    rig.finishStep();
    rig._mover.update( 0.0f, intent, nullopt ); // 버튼이 없는 폰은 상호작용하지 않고 걷는다
    SW_EXPECT_EQUAL( 3, rig._mover.getTileX() );
    SW_EXPECT_FALSE( rig._mover.consumeInteractRequest() );
}

// 결함: 걸음 시간 0 이면 타이머가 서지 않아 `Walk` 에 영원히 갇혀 다시는 입력을 받지 못했다.
SW_TEST_CASE( OverworldTileMoverTest, ZeroStepTimeDoesNotLockTheWalker )
{
    OverworldFixture           rig;
    OverworldTileMoverSettings settings = rig._mover.getSettings();
    settings._stepSeconds               = 0.0f;
    rig._mover.setSettings( settings );
    rig.push( 1.0f, 0.0f );
    rig.push( 1.0f, 0.0f );
    SW_EXPECT_EQUAL( 4, rig._mover.getTileX() );
}

SW_TEST_CASE( OverworldTileMoverTest, WarpIsRequestedOnceAndWinsOverEncounter )
{
    OverworldFixture rig;
    TileWarp         warp{};
    warp._tileX       = 3;
    warp._tileY       = 2;
    warp._targetMap   = "maps/next.xml";
    warp._targetTileX = 7;
    warp._targetTileY = 8;
    rig._map.setOrUpdateWarp( warp );
    rig._map.setEncounter( 3, 2, true );
    OverworldTileMoverSettings settings = rig._mover.getSettings();
    settings._encounterRate             = 1.0f;
    rig._mover.setSettings( settings );

    rig.walkRight();
    string path;
    int32  spawnX = 0;
    int32  spawnY = 0;
    SW_REQUIRE_TRUE( rig._mover.consumeWarpRequest( path, spawnX, spawnY ) );
    SW_EXPECT_TRUE( path == "maps/next.xml" );
    SW_EXPECT_EQUAL( 7, spawnX );
    SW_EXPECT_EQUAL( 8, spawnY );
    SW_EXPECT_FALSE( rig._mover.consumeWarpRequest( path, spawnX, spawnY ) );
    SW_EXPECT_FALSE( rig._mover.consumeEncounterRequest() );
}

SW_TEST_CASE( OverworldTileMoverTest, EncounterCountsOnlyEncounterTilesByPeriod )
{
    OverworldFixture rig;
    rig._mover.setPosition( 0, 2 );
    rig._map.setEncounter( 2, 2, true );
    rig._map.setEncounter( 3, 2, true );
    rig.walkRight(); // 1,2 는 조우 칸이 아니다
    rig.walkRight();
    SW_EXPECT_FALSE( rig._mover.consumeEncounterRequest() );
    rig.walkRight(); // 조우 칸 두 걸음 = 주기 1 / 0.5
    SW_EXPECT_TRUE( rig._mover.consumeEncounterRequest() );
    SW_EXPECT_FALSE( rig._mover.consumeEncounterRequest() );

    OverworldFixture off;
    off._mover.setEncounterRate( 0.0f );
    off._map.setEncounter( 3, 2, true );
    off._map.setEncounter( 4, 2, true );
    off.walkRight();
    off.walkRight();
    SW_EXPECT_FALSE( off._mover.consumeEncounterRequest() );
}

SW_TEST_CASE( OverworldTileMoverTest, StateRoundTripsMidStep )
{
    OverworldFixture rig;
    rig.push( 1.0f, 0.0f );
    const vector<uint8> listSaved = OverworldFixture::save( rig._mover );

    OverworldFixture restored;
    Archive          reading( listSaved.data(), listSaved.size() );
    SW_REQUIRE_TRUE( restored._mover.readState( reading ) );
    SW_EXPECT_EQUAL( 3, restored._mover.getTileX() );
    SW_EXPECT_EQUAL( 2, restored._mover.getTileY() );
    SW_EXPECT_TRUE( restored._mover.getLocomotion().getState() == LocomotionState::Walk );
    SW_EXPECT_TRUE( restored._mover.getLocomotion().getFacing() == FacingDir::Right );
    SW_EXPECT_TRUE( OverworldFixture::save( restored._mover ) == listSaved );
}

// 결함: 꾸민 바이트의 워프 대기 + 빈 맵 경로를 받아 게임이 빈 경로로 맵을 옮기려 했다.
SW_TEST_CASE( OverworldTileMoverTest, ForgedPendingWarpWithoutAMapIsRefused )
{
    OverworldFixture rig;
    vector<uint8>    listBytes                 = OverworldFixture::save( rig._mover );
    constexpr size_t kFlagFromEnd              = 4; // 플래그 다섯(이동 · 워프 · 조우 · 상호작용 · 입력) 중 워프는 끝에서 넷째다
    listBytes[listBytes.size() - kFlagFromEnd] = SW_TRUE;

    OverworldFixture target;
    target._mover.setPosition( 1, 1 );
    Archive reading( listBytes.data(), listBytes.size() );
    SW_EXPECT_FALSE( target._mover.readState( reading ) );
    SW_EXPECT_EQUAL( 1, target._mover.getTileX() );
}

SW_TEST_CASE( OverworldTileMoverTest, TruncatedOrBadFlagBytesLeaveTheStateAlone )
{
    OverworldFixture rig;
    rig._mover.setPosition( 4, 4 );
    const vector<uint8> listSaved = OverworldFixture::save( rig._mover );

    OverworldFixture target;
    Archive          truncated( listSaved.data(), listSaved.size() - 1 );
    SW_EXPECT_FALSE( target._mover.readState( truncated ) );
    SW_EXPECT_EQUAL( 2, target._mover.getTileX() );

    vector<uint8> listBadFlag = listSaved;
    listBadFlag.back()        = 2;
    Archive badFlag( listBadFlag.data(), listBadFlag.size() );
    SW_EXPECT_FALSE( target._mover.readState( badFlag ) );
    SW_EXPECT_EQUAL( 2, target._mover.getTileX() );
}

// 결함: 걷는 중인데 남은 시간이 없는 상태는 다시 깨어날 길이 없다.
SW_TEST_CASE( OverworldLocomotionTest, BusyStateWithoutATimerIsRefused )
{
    PlayerLocomotion stuck;
    stuck.setState( LocomotionState::Walk );
    Archive writing;
    stuck.writeState( writing );
    Archive          reading( writing.getData(), writing.getSize() );
    PlayerLocomotion target;
    SW_EXPECT_FALSE( target.readState( reading ) );
    SW_EXPECT_TRUE( target.getState() == LocomotionState::Idle );
}

SW_TEST_CASE( OverworldTileMapTest, SolidAndOutsideAreNotWalkable )
{
    TileMap map;
    map.resize( 2, 2 );
    map.setWalkable( 0, 0, true );
    map.setWalkable( 1, 0, false );
    SW_EXPECT_TRUE( map.isWalkable( 0, 0 ) );
    SW_EXPECT_FALSE( map.isSolid( 0, 0 ) );
    SW_EXPECT_TRUE( map.isSolid( 1, 0 ) );
    SW_EXPECT_FALSE( map.isWalkable( -1, 0 ) );
    SW_EXPECT_TRUE( map.isSolid( 2, 2 ) );
    SW_EXPECT_FALSE( map.isEncounterTile( 9, 9 ) );
    map.setWalkable( 9, 9, true ); // 맵 밖 쓰기는 무시한다
    SW_EXPECT_FALSE( map.isWalkable( 9, 9 ) );
}

SW_TEST_CASE( OverworldTileMapTest, WarpsReplaceByTileAndRemoveKeepsTheIndex )
{
    TileMap map;
    map.resize( 3, 3 );
    TileWarp first{};
    first._tileX     = 0;
    first._tileY     = 0;
    first._targetMap = "a";
    TileWarp second{};
    second._tileX     = 1;
    second._tileY     = 1;
    second._targetMap = "b";
    map.setOrUpdateWarp( first );
    map.setOrUpdateWarp( second );
    first._targetMap = "c";
    map.setOrUpdateWarp( first );
    SW_REQUIRE_NOT_NULL( map.findWarp( 0, 0 ) );
    SW_EXPECT_TRUE( map.findWarp( 0, 0 )->_targetMap == "c" );

    map.removeWarp( 0, 0 ); // 뒤 원소의 번호가 당겨져도 찾아야 한다
    SW_EXPECT_TRUE( map.findWarp( 0, 0 ) == nullptr );
    SW_REQUIRE_NOT_NULL( map.findWarp( 1, 1 ) );
    SW_EXPECT_TRUE( map.findWarp( 1, 1 )->_targetMap == "b" );
}

SW_TEST_CASE( OverworldTileMapTest, EdgePresetStampsOneEdge )
{
    TileMap map;
    map.resize( 3, 2 );
    map.setWalkable( 1, 0, false );
    map.paintEdgeWarpPreset( 1, "east", 5, 6 ); // 동쪽
    SW_EXPECT_TRUE( map.isWalkable( 2, 0 ) );
    SW_EXPECT_TRUE( map.isWalkable( 2, 1 ) );
    SW_EXPECT_FALSE( map.isWalkable( 1, 0 ) );
    SW_REQUIRE_NOT_NULL( map.findWarp( 2, 1 ) );
    SW_EXPECT_EQUAL( 5, map.findWarp( 2, 1 )->_targetTileX );

    map.paintEdgeWarpPreset( 0, "", 0, 0 ); // 대상이 없으면 칠하지 않는다
    SW_EXPECT_TRUE( map.findWarp( 0, 0 ) == nullptr );
}

SW_TEST_CASE( OverworldZoneTrackerTest, MapMakesOneActiveZoneWithItsTags )
{
    ZoneTracker tracker;
    SW_EXPECT_TRUE( tracker.getActiveZone() == nullptr );
    SW_EXPECT_TRUE( tracker.getActiveZoneID().empty() );
    SW_EXPECT_FALSE( tracker.isClearGateLocked() );

    tracker.setFromMap( "maps/gym.xml", "Gym", 10, 4, "gym, clear_gate\tindoors" );
    SW_EXPECT_TRUE( tracker.getActiveZoneID() == "Gym" );
    SW_EXPECT_TRUE( tracker.hasActiveZoneTag( hashed_string( "indoors" ) ) );
    SW_EXPECT_FALSE( tracker.hasActiveZoneTag( hashed_string( "dungeon" ) ) );
    SW_EXPECT_TRUE( tracker.isClearGateLocked() ); // 태그가 잠근다
    SW_EXPECT_EQUAL( 9, tracker.getCameraBounds()._max._x );
    SW_EXPECT_EQUAL( 3, tracker.getCameraBounds()._max._y );

    tracker.setClearGateLocked( false );
    SW_EXPECT_FALSE( tracker.isClearGateLocked() );

    tracker.setFromMap( "maps/town.xml", "", 0, 0 ); // 이름이 없으면 경로가 id 다
    SW_EXPECT_TRUE( tracker.getActiveZoneID() == "maps/town.xml" );
    SW_EXPECT_FALSE( tracker.isClearGateLocked() );
    SW_EXPECT_EQUAL( 0, tracker.getCameraBounds()._max._x );

    tracker.activate( "no such zone" );
    SW_EXPECT_TRUE( tracker.getActiveZoneID() == "maps/town.xml" );
    tracker.clear();
    SW_EXPECT_TRUE( tracker.getActiveZone() == nullptr );
    SW_EXPECT_EQUAL( 0, tracker.getCameraBounds()._max._x );
}
