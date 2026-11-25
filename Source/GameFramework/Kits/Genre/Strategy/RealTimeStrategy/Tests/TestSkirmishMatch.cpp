// SkirmishMatchTest — RTS 1 대 1 한 판의 규칙(nogpu): 점 대칭 맵과 두 기지, 맵 배치가 놓는 유닛이 카탈로그에 없으면 판을 열지 않음,
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
 * @brief [SkirmishMatchTest] 새 판: 두 기지가 같은 형편(일꾼 넷 · 본진 하나 · 시작 광물)이고 절벽은 점 대칭이며 판은 끝나지 않았다
 */
SW_TEST_CASE( SkirmishMatchTest, BasesOpenMirrored )
{
    RTSCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kSkirmishUnitXML, "SkirmishMatchTest" ) );
    SkirmishMatch match;
    SW_REQUIRE_TRUE( match.initialize( catalog, kBothHuman, makeSettings() ) );

    for ( int32 player = 0; player < SkirmishMatch::kPlayerCount; ++player )
    {
        const SkirmishPlayerSummary summary = match.makeSummary( player );
        SW_EXPECT_EQUAL( kStartWorkers, summary._workers );
        SW_EXPECT_EQUAL( kStartBuildings, summary._buildings );
        SW_EXPECT_EQUAL( 0, summary._army );
        SW_EXPECT_EQUAL( kStartMinerals, summary._minerals );
        SW_EXPECT_TRUE( match.isHumanPlayer( player ) );
        SW_EXPECT_TRUE( match.findAI( player ) == nullptr );
    }
    int32 mismatchedCliffs = 0;
    int32 cliffCount       = 0;
    for ( int32 y = 0; y < SkirmishMatch::kMapSize; ++y )
    {
        for ( int32 x = 0; x < SkirmishMatch::kMapSize; ++x )
        {
            const bool bCliff = match.getWorld().isTerrainBlocked( x, y );
            cliffCount += bCliff ? 1 : 0;
            mismatchedCliffs += bCliff != match.getWorld().isTerrainBlocked( SkirmishMatch::kMapSize - 1 - x, SkirmishMatch::kMapSize - 1 - y ) ? 1 : 0;
        }
    }
    SW_EXPECT_TRUE( cliffCount > 0 );
    SW_EXPECT_EQUAL( 0, mismatchedCliffs );
    SW_EXPECT_FALSE( match.getWorld().isTerrainBlocked( -1, 0 ) );
    SW_EXPECT_FALSE( match.getWorld().isTerrainBlocked( SkirmishMatch::kMapSize, 0 ) );
    SW_EXPECT_FALSE( match.isOver() );
}

/**
 * @brief [SkirmishMatchTest] 맵 배치가 놓는 유닛(간헐천)이 카탈로그에 없으면 조용히 건너뛰지 않고 판을 열지 않는다
 */
SW_TEST_CASE( SkirmishMatchTest, MissingLayoutUnitRefusesTheMatch )
{
    RTSCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kNoGeyserUnitXML, "SkirmishMatchTest" ) );
    SW_EXPECT_LOG( "SkirmishMatch.MapLayoutPlacesBut" );
    SkirmishMatch match;
    SW_EXPECT_FALSE( match.initialize( catalog, kBothHuman, makeSettings() ) );
}

/**
 * @brief [SkirmishMatchTest] 같은 입력으로 돈 두 판(락스텝)은 체크섬이 같고, 상태를 쓰고 새 판에 읽으면 체크섬이 같다
 */
SW_TEST_CASE( SkirmishMatchTest, LockstepAndStateRoundTripKeepTheChecksum )
{
    RTSCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kSkirmishUnitXML, "SkirmishMatchTest" ) );
    SkirmishMatch host;
    SkirmishMatch client;
    SW_REQUIRE_TRUE( host.initialize( catalog, kBothHuman, makeSettings() ) );
    SW_REQUIRE_TRUE( client.initialize( catalog, kBothHuman, makeSettings() ) );
    const uint32 startChecksum = host.computeChecksum();
    run( host, kGatherFrames );
    run( client, kGatherFrames );
    SW_EXPECT_EQUAL( host.computeChecksum(), client.computeChecksum() );
    SW_EXPECT_TRUE( host.computeChecksum() != startChecksum ); // 일꾼이 움직이고 캤다

    Archive written;
    host.writeState( written );
    SkirmishMatch restored;
    SW_REQUIRE_TRUE( restored.initialize( catalog, kBothHuman, makeSettings() ) );
    Archive reading( written.getData(), written.getSize() );
    SW_REQUIRE_TRUE( restored.readState( reading ) );
    SW_EXPECT_EQUAL( host.computeChecksum(), restored.computeChecksum() );
}
