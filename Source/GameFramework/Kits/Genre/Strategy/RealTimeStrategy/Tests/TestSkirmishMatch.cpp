// SkirmishMatchTest — RTS 교전 한 판의 규칙(nogpu): 지도 데이터(절벽 레이어 · 시작 지점 · 배치)가 판을 정하고, 필수 칸이 빠지거나 놓을 수 없으면 판을 열지 않음,
// 같은 입력의 두 판(락스텝)이 같은 체크섬을 내고 상태를 쓰고 읽은 판도 같은 체크섬을 냄. 두 쪽 모두 사람이라 컴퓨터 상대(행동 트리)는 돌지 않는다.
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/RTSCatalog.h"
#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/SkirmishMatch.h"

using namespace sw;

namespace
{
    constexpr const utf8* kSkirmishUnitXML = R"(<RTSCatalog supplyMax="200">
  <Rules fixedStep="0.05" minimumDamage="0.5" repathInterval="0.5" visionInterval="0.25" interactSlack="0.35" resourceSearchRadius="8" constructionStartRatio="0.1" underAttackCooldown="10" autoTargetLeashScale="1.5" productionQueueMax="5" flowFieldGroupSize="6" bucketSize="4" mineralCurrency="Minerals" gasCurrency="Gas"/>
  <Unit id="minerals" name="minerals" kind="Resource" resource="Minerals" amount="1500" footprint="1"/>
  <Unit id="rich_minerals" name="rich_minerals" kind="Resource" resource="Minerals" amount="900" footprint="1"/>
  <Unit id="geyser" name="geyser" kind="Resource" resource="Gas" amount="2500" footprint="2"/>
  <Unit id="command_center" name="command_center" kind="Building" hp="1500" armor="1" footprint="4" depot="true" provides="10" producedBy="worker" minerals="400" buildTime="75" sight="10"/>
  <Unit id="worker" name="worker" hp="60" speed="2.8" radius="0.35" damage="5" range="0.2" cooldown="1.1" worker="true" producedBy="command_center" minerals="50" supply="1" buildTime="13" gatherTime="2.8" cargo="8" sight="7"/>
</RTSCatalog>)";

    /** @brief 간헐천이 없는 카탈로그 — 맵 배치가 놓는 유닛이 빠졌다. */
    constexpr const utf8* kNoGeyserUnitXML = R"(<RTSCatalog supplyMax="200">
  <Rules fixedStep="0.05" minimumDamage="0.5" repathInterval="0.5" visionInterval="0.25" interactSlack="0.35" resourceSearchRadius="8" constructionStartRatio="0.1" underAttackCooldown="10" autoTargetLeashScale="1.5" productionQueueMax="5" flowFieldGroupSize="6" bucketSize="4" mineralCurrency="Minerals" gasCurrency="Gas"/>
  <Unit id="minerals" name="minerals" kind="Resource" resource="Minerals" amount="1500" footprint="1"/>
  <Unit id="rich_minerals" name="rich_minerals" kind="Resource" resource="Minerals" amount="900" footprint="1"/>
  <Unit id="command_center" name="command_center" kind="Building" hp="1500" armor="1" footprint="4" depot="true" provides="10" producedBy="worker" minerals="400" buildTime="75" sight="10"/>
  <Unit id="worker" name="worker" hp="60" speed="2.8" radius="0.35" damage="5" range="0.2" cooldown="1.1" worker="true" producedBy="command_center" minerals="50" supply="1" buildTime="13" gatherTime="2.8" cargo="8" sight="7"/>
</RTSCatalog>)";

    constexpr uint8   kBothHuman      = SkirmishMatch::kHumanPlayer0 | SkirmishMatch::kHumanPlayer1;
    constexpr int32   kStartMinerals  = 50;
    constexpr float32 kFrameDelta     = 0.05f;
    constexpr int32   kGatherFrames   = 200; ///< 일꾼이 광물에 닿아 한 번 캐어 올 만큼(10 초)
    constexpr int32   kStartWorkers   = 4;
    constexpr int32   kStartBuildings = 1;
    constexpr int32   kMapWidth       = 40;
    constexpr int32   kMapHeight      = 32;

    /** @brief 테스트 지도 하나 — 남서 · 북동 두 기지와 가운데 절벽 셋입니다. */
    struct SkirmishTestMap
    {
        SkirmishMap    _map;
        TileMapXMLData _tileMap;

        SkirmishTestMap()
        {
            SW_REQUIRE_TRUE( _tileMap.resetTiles( kMapWidth, kMapHeight ) );
            SW_REQUIRE_TRUE( _tileMap.addDataLayer( SkirmishMap::kCliffLayer, 0 ) );
            for ( int32 x = 18; x <= 20; ++x )
            {
                SW_REQUIRE_TRUE( _tileMap.setCellData( 0, x, 16, 1 ) );
            }
            addStart( 0, float2{ 8.0f, 8.0f } );
            addStart( 1, float2{ 30.0f, 24.0f } );
            for ( int32 player = 0; player < 2; ++player )
            {
                addBase( player, player == 0 ? int2{ 6, 6 } : int2{ 28, 22 } );
            }
        }

        void addStart( int32 team, const float2& position )
        {
            SkirmishPlayerStart start;
            start._team     = team;
            start._position = position;
            _map._listPlayerStart.push_back( start );
        }

        /** @brief 본진(@p cell 이 왼쪽 아래) · 그 왼쪽의 광물 · 일꾼 넷입니다. */
        void addBase( int32 player, const int2& cell )
        {
            add( "command_center", player, cell );
            for ( int32 index = 0; index < kStartWorkers; ++index )
            {
                add( "minerals", RTSWorld::kNoOwner, int2{ cell._x - 5, cell._y + index } );
                add( "worker", player, int2{ cell._x - 2, cell._y + index } );
            }
        }

        void add( const utf8* pUnitID, int32 owner, const int2& cell )
        {
            SkirmishUnitPlacement placement;
            placement._unitID = CatalogKey<RTSUnitDef>{ hashed_string( pUnitID ) };
            placement._owner  = owner;
            placement._cell   = cell;
            _map._listPlacement.push_back( placement );
        }

        [[nodiscard]] bool open( SkirmishMatch& match, const RTSCatalog& catalog, uint8 humanMask, const SkirmishMatchSettings& settings ) const
        {
            return match.initialize( &catalog, _map, _tileMap, humanMask, settings );
        }
    };

    SkirmishMatchSettings makeSettings()
    {
        SkirmishMatchSettings settings;
        settings._startMinerals = kStartMinerals;
        settings._workerID      = hashed_string{ "worker" };
        settings._depotID       = hashed_string{ "command_center" };
        return settings;
    }

    void run( SkirmishMatch& match, int32 frames )
    {
        for ( int32 frame = 0; frame < frames; ++frame )
        {
            match.update( kFrameDelta, 1.0f );
        }
    }
} // namespace

/**
 * @brief [SkirmishMatchTest] 새 판은 지도 데이터 그대로다 — 크기 · 절벽 칸 · 플레이어마다의 배치 · 시작 광물
 */
SW_TEST_CASE( SkirmishMatchTest, MatchOpensFromTheMapData )
{
    RTSCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kSkirmishUnitXML, "SkirmishMatchTest" ) );
    SkirmishMatch         match;
    const SkirmishTestMap map;
    SW_REQUIRE_TRUE( map.open( match, catalog, kBothHuman, makeSettings() ) );

    SW_REQUIRE_EQUAL( 2, match.getPlayerCount() );
    for ( int32 player = 0; player < match.getPlayerCount(); ++player )
    {
        const SkirmishPlayerSummary summary = match.makeSummary( player );
        SW_EXPECT_EQUAL( kStartWorkers, summary._workers );
        SW_EXPECT_EQUAL( kStartBuildings, summary._buildings );
        SW_EXPECT_EQUAL( 0, summary._army );
        SW_EXPECT_EQUAL( kStartMinerals, summary._minerals );
        SW_EXPECT_TRUE( match.isHumanPlayer( player ) );
        SW_EXPECT_TRUE( match.findAI( player ) == nullptr );
    }
    SW_EXPECT_EQUAL( kMapWidth, match.getWorld().getGrid().getWidth() );
    SW_EXPECT_EQUAL( kMapHeight, match.getWorld().getGrid().getHeight() );
    int32 cliffCount = 0;
    for ( int32 y = 0; y < kMapHeight; ++y )
    {
        for ( int32 x = 0; x < kMapWidth; ++x )
        {
            cliffCount += match.getWorld().isTerrainBlocked( x, y ) ? 1 : 0;
        }
    }
    SW_EXPECT_EQUAL( 3, cliffCount );
    SW_EXPECT_TRUE( match.getWorld().isTerrainBlocked( 18, 16 ) );
    SW_EXPECT_FALSE( match.getWorld().isTerrainBlocked( -1, 0 ) );
    SW_EXPECT_FALSE( match.getWorld().isTerrainBlocked( kMapWidth, 0 ) );
    SW_EXPECT_FALSE( match.isOver() );
}

/**
 * @brief [SkirmishMatchTest] 지도가 놓는 유닛이 카탈로그에 없으면 조용히 건너뛰지 않고 판을 열지 않는다
 */
SW_TEST_CASE( SkirmishMatchTest, MissingLayoutUnitRefusesTheMatch )
{
    RTSCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kNoGeyserUnitXML, "SkirmishMatchTest" ) );
    SW_EXPECT_LOG( "SkirmishMatch.MapLayoutPlacesBut" );
    SkirmishTestMap map;
    map.add( "geyser", RTSWorld::kNoOwner, int2{ 8, 2 } );
    SkirmishMatch match;
    SW_EXPECT_FALSE( map.open( match, catalog, kBothHuman, makeSettings() ) );
}

/**
 * @brief [SkirmishMatchTest] 지도의 필수 칸(절벽 레이어 · 시작 지점 · 배치)이 빠지거나 틀리면(맵 밖 시작 · 없는 주인 · 막힌 칸) 까닭마다 오류이고 판을 열지 않는다
 */
SW_TEST_CASE( SkirmishMatchTest, IncompleteMapRefusesTheMatch )
{
    RTSCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kSkirmishUnitXML, "SkirmishMatchTest" ) );
    const auto isRefused = [&catalog]( const SkirmishTestMap& map )
    {
        SkirmishMatch match;
        return map.open( match, catalog, kBothHuman, makeSettings() ) == false;
    };
    {
        SW_EXPECT_LOG( "SkirmishMatch.MapLayerMissing" );
        SkirmishTestMap map;
        map._tileMap._listDataLayer.clear();
        SW_EXPECT_TRUE( isRefused( map ) );
    }
    {
        SW_EXPECT_LOG( "SkirmishMatch.MapPlayerCount" );
        SW_EXPECT_LOG( "SkirmishMatch.MapOwnerUnknown" ); // 둘째 기지의 주인이 없어졌다
        SkirmishTestMap map;
        map._map._listPlayerStart.resize( 1 );
        SW_EXPECT_TRUE( isRefused( map ) );
    }
    {
        SW_EXPECT_LOG( "SkirmishMatch.MapStartOutside" );
        SkirmishTestMap map;
        map._map._listPlayerStart[1]._position = float2{ static_cast<float32>( kMapWidth ), 4.0f };
        SW_EXPECT_TRUE( isRefused( map ) );
    }
    {
        SW_EXPECT_LOG( "SkirmishMatch.MapPlacementsMissing" );
        SkirmishTestMap map;
        map._map._listPlacement.clear();
        SW_EXPECT_TRUE( isRefused( map ) );
    }
    {
        SW_EXPECT_LOG( "SkirmishMatch.MapPlacementBlocked" );
        SkirmishTestMap map;
        map.add( "command_center", 0, int2{ 18, 14 } ); // 절벽(18..20, 16)과 겹친다
        SW_EXPECT_TRUE( isRefused( map ) );
    }
}

/**
 * @brief [SkirmishMatchTest] 지도 데이터만 바꾸면 판이 바뀐다 — 세 번째 시작 지점과 배치를 더하면 세 사람 판이고, 절벽을 더하면 같은 입력의 결과(체크섬)가 다르다
 */
SW_TEST_CASE( SkirmishMatchTest, ChangingTheMapChangesTheMatch )
{
    RTSCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kSkirmishUnitXML, "SkirmishMatchTest" ) );
    SkirmishTestMap threePlayers;
    threePlayers.addStart( 2, float2{ 30.0f, 8.0f } );
    threePlayers.addBase( 2, int2{ 28, 6 } );
    SkirmishMatch match;
    SW_REQUIRE_TRUE( threePlayers.open( match, catalog, 0x7, makeSettings() ) );
    SW_REQUIRE_EQUAL( 3, match.getPlayerCount() );
    SW_EXPECT_TRUE( match.isHumanPlayer( 2 ) );
    SW_EXPECT_EQUAL( kStartWorkers, match.makeSummary( 2 )._workers );
    SW_EXPECT_TRUE( match.getWorld().areEnemies( 0, 2 ) );

    const SkirmishTestMap baseMap;
    SkirmishTestMap       walled;
    for ( int32 y = 4; y <= 12; ++y )
    {
        SW_REQUIRE_TRUE( walled._tileMap.setCellData( 0, 3, y, 1 ) ); // 광물(x 1)과 일꾼(x 4) 사이를 막는다
    }
    SkirmishMatch first;
    SkirmishMatch second;
    SW_REQUIRE_TRUE( baseMap.open( first, catalog, kBothHuman, makeSettings() ) );
    SW_REQUIRE_TRUE( walled.open( second, catalog, kBothHuman, makeSettings() ) );
    run( first, kGatherFrames );
    run( second, kGatherFrames );
    SW_EXPECT_TRUE( first.computeChecksum() != second.computeChecksum() );
}

/**
 * @brief [SkirmishMatchTest] 같은 입력으로 돈 두 판(락스텝)은 체크섬이 같고, 상태를 쓰고 새 판에 읽으면 체크섬이 같다
 */
SW_TEST_CASE( SkirmishMatchTest, LockstepAndStateRoundTripKeepTheChecksum )
{
    RTSCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kSkirmishUnitXML, "SkirmishMatchTest" ) );
    const SkirmishTestMap map;
    SkirmishMatch         host;
    SkirmishMatch         client;
    SW_REQUIRE_TRUE( map.open( host, catalog, kBothHuman, makeSettings() ) );
    SW_REQUIRE_TRUE( map.open( client, catalog, kBothHuman, makeSettings() ) );
    const uint32 startChecksum = host.computeChecksum();
    run( host, kGatherFrames );
    run( client, kGatherFrames );
    SW_EXPECT_EQUAL( host.computeChecksum(), client.computeChecksum() );
    SW_EXPECT_TRUE( host.computeChecksum() != startChecksum ); // 일꾼이 움직이고 캤다

    Archive written;
    host.writeState( written );
    SkirmishMatch restored;
    SW_REQUIRE_TRUE( map.open( restored, catalog, kBothHuman, makeSettings() ) );
    Archive reading( written.getData(), written.getSize() );
    SW_REQUIRE_TRUE( restored.readState( reading ) );
    SW_EXPECT_EQUAL( host.computeChecksum(), restored.computeChecksum() );
}
