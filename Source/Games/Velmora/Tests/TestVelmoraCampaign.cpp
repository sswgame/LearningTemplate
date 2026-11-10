// VelmoraCampaignTest — 벨모라 한 판의 규칙(nogpu): 지역 표 읽기와 거절, 진입 → 봉화 → 클리어 → 퀘스트 보상(지갑 · 하트), 야영지 · 쓰러짐, 저장 왕복,
// 확장 팩 퀘스트가 든 일지를 확장 팩 없는 표로 불러도 기본 진행이 이어지는 승계.
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Runtime/Foundation/Condition/GameFlags.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Shop.h"
#include "GameFramework/Runtime/Gameplay/Quest/QuestCatalog.h"
#include "GameFramework/Runtime/Gameplay/Quest/QuestLog.h"

#include "Games/Velmora/VelmoraCampaign.h"
#include "Games/Velmora/VelmoraRegionCatalog.h"

using namespace sw;

namespace
{
    constexpr const utf8* kRegionXML = R"(<VelmoraRegionCatalog>
  <Region id="hearth" min="-10,-10" max="10,10" respawn="0,0" startQuest="oath"><Camp id="hearth_camp" at="0,2"/></Region>
  <Region id="ridge" min="10,-10" max="30,10" respawn="12,0"><Beacon id="ridge_beacon" at="20,6"/><Hall monster="imp" at="22,0"/><Boss monster="warden" at="26,0"/></Region>
</VelmoraRegionCatalog>)";

    constexpr const utf8* kBaseQuestXML = R"(<QuestCatalog>
  <Quest id="oath">
    <Stage id="reach" next="light"><Objective kind="Enter" target="ridge"/></Stage>
    <Stage id="light" next="defeat"><Objective kind="Light" target="ridge_beacon"/></Stage>
    <Stage id="defeat" next="done"><Objective kind="Defeat" target="warden"/></Stage>
    <Stage id="done" complete="true"><Reward gold="50" heart="1"/></Stage>
  </Quest>
</QuestCatalog>)";

    /** @brief 확장 팩이 층으로 더한 뒤의 표(기본 퀘스트 + 확장 퀘스트)와 같은 내용입니다. */
    constexpr const utf8* kExpandedQuestXML = R"(<QuestCatalog>
  <Quest id="oath">
    <Stage id="reach" next="light"><Objective kind="Enter" target="ridge"/></Stage>
    <Stage id="light" next="defeat"><Objective kind="Light" target="ridge_beacon"/></Stage>
    <Stage id="defeat" next="done"><Objective kind="Defeat" target="warden"/></Stage>
    <Stage id="done" complete="true"><Reward gold="50" heart="1"/></Stage>
  </Quest>
  <Quest id="tide" requires="oath">
    <Stage id="dive" next="done"><Objective kind="Defeat" target="lurker"/></Stage>
    <Stage id="done" complete="true"><Reward gold="80"/></Stage>
  </Quest>
</QuestCatalog>)";

    struct TestVelmoraCampaignInternal
    {
        static AdventureVitalsSettings makeVitals()
        {
            AdventureVitalsSettings settings;
            settings._maxHeartCount   = 10;
            settings._startHeartCount = 3;
            return settings;
        }
    };
} // namespace

/** @brief [VelmoraCampaignTest] 지역 표: 경계 · 자리 · 전투를 읽고, 자리로 지역을 찾는다. 겹치거나 자리가 경계 밖인 지역은 넣지 않는다 */
SW_TEST_CASE( VelmoraCampaignTest, RegionCatalogReadsAndRejects )
{
    VelmoraRegionCatalog regions;
    SW_REQUIRE_TRUE( regions.loadFromXMLText( kRegionXML, "VelmoraCampaignTest" ) );
    SW_REQUIRE_EQUAL( size_t{ 2 }, regions.getCount() );
    const VelmoraRegionDef* pRidge = regions.find( hashed_string( "ridge" ) );
    SW_REQUIRE_TRUE( pRidge != nullptr );
    SW_EXPECT_TRUE( pRidge->hasEncounter() );
    SW_EXPECT_EQUAL( size_t{ 1 }, pRidge->makeLayout()._listBossSpawn.size() );
    SW_EXPECT_TRUE( regions.findAt( float2{ 25.0f, 0.0f } ) == pRidge );
    SW_EXPECT_TRUE( regions.findAt( float2{ 40.0f, 0.0f } ) == nullptr );
    SW_EXPECT_TRUE( regions.find( hashed_string( "hearth" ) )->_listStartQuest.size() == 1 );

    VelmoraRegionCatalog overlapping;
    SW_EXPECT_LOG( "Velmora.RegionOverlap" );
    SW_EXPECT_LOG( "Velmora.RegionSkipped" );
    SW_EXPECT_TRUE( overlapping.loadFromXMLText( R"(<VelmoraRegionCatalog>
      <Region id="a" min="0,0" max="10,10" respawn="1,1"/>
      <Region id="b" min="5,5" max="20,20" respawn="15,15"/>
    </VelmoraRegionCatalog>)",
                                                 "VelmoraCampaignTest" ) );
    SW_EXPECT_EQUAL( size_t{ 1 }, overlapping.getCount() );

    VelmoraRegionCatalog outside;
    SW_EXPECT_LOG( "Velmora.RegionPointOutside" );
    SW_EXPECT_LOG( "Velmora.RegionSkipped" );
    // 하나도 못 읽은 표는 실패다 — 틀린 원소의 오류가 까닭을 말했으므로 빈 표 경고(XMLCatalog.NoEntries)는 따로 내지 않는다(XMLCatalogReadResult::reportIfEmpty)
    SW_EXPECT_FALSE( outside.loadFromXMLText( R"(<VelmoraRegionCatalog><Region id="a" min="0,0" max="10,10" respawn="1,1"><Camp id="c" at="50,50"/></Region></VelmoraRegionCatalog>)",
                                              "VelmoraCampaignTest" ) );
}

/** @brief [VelmoraCampaignTest] 진입이 퀘스트를 받고 세고, 봉화 · 클리어가 단계를 넘겨 끝내며, 끝 단계의 보상이 지갑과 하트 그릇으로 간다 */
SW_TEST_CASE( VelmoraCampaignTest, RegionLoopCompletesTheQuest )
{
    VelmoraRegionCatalog regions;
    QuestCatalog         quests;
    SW_REQUIRE_TRUE( regions.loadFromXMLText( kRegionXML, "VelmoraCampaignTest" ) );
    SW_REQUIRE_TRUE( quests.loadFromXMLText( kBaseQuestXML, "VelmoraCampaignTest" ) );
    QuestLog  log;
    GameFlags flags;
    Wallet    wallet;
    log.initialize( &quests );
    VelmoraCampaign campaign;
    SW_REQUIRE_TRUE( campaign.initialize( TestVelmoraCampaignInternal::makeVitals(), hashed_string( "hearth" ) ) );
    const VelmoraRegionDef& hearth = *regions.find( hashed_string( "hearth" ) );
    const VelmoraRegionDef& ridge  = *regions.find( hashed_string( "ridge" ) );

    SW_EXPECT_TRUE( campaign.enterRegion( hearth, log, flags ) ); // 받는다
    SW_EXPECT_TRUE( log.getStatus( hashed_string( "oath" ) ) == QuestStatus::Active );
    SW_EXPECT_FALSE( campaign.enterRegion( hearth, log, flags ) ); // 두 번째 진입은 처음이 아니다
    SW_EXPECT_TRUE( campaign.enterRegion( ridge, log, flags ) );
    SW_EXPECT_FALSE( campaign.lightBeacon( hearth, log, flags ) ); // 봉화가 없는 지역
    SW_EXPECT_TRUE( campaign.lightBeacon( ridge, log, flags ) );
    SW_EXPECT_FALSE( campaign.lightBeacon( ridge, log, flags ) ); // 이미 켰다
    SW_EXPECT_TRUE( VelmoraCampaign::isBeaconLit( ridge, flags ) );
    SW_EXPECT_FALSE( VelmoraCampaign::isEncounterCleared( ridge, flags ) );
    campaign.clearEncounter( ridge, log, flags );
    SW_EXPECT_TRUE( VelmoraCampaign::isEncounterCleared( ridge, flags ) );
    SW_EXPECT_TRUE( log.getStatus( hashed_string( "oath" ) ) == QuestStatus::Completed );

    vector<QuestEvent> listEvent;
    log.pollEvents( listEvent );
    SW_EXPECT_EQUAL( 1, campaign.applyQuestEvents( listEvent, wallet ) );
    SW_EXPECT_EQUAL( int64{ 50 }, wallet.getBalance( Wallet::getDefaultCurrency() ) );
    SW_EXPECT_EQUAL( 4, campaign.getVitals().getHeartCount() );
}

/** @brief [VelmoraCampaignTest] 야영지에서 쉬면 체력이 차고 일어설 지역이 그곳이 된다. 쓰러진 뒤 일어서면 체력이 다시 찬다. 몸 · 일어설 지역은 저장 왕복한다 */
SW_TEST_CASE( VelmoraCampaignTest, CampFallAndStateRoundTrip )
{
    VelmoraRegionCatalog regions;
    SW_REQUIRE_TRUE( regions.loadFromXMLText( kRegionXML, "VelmoraCampaignTest" ) );
    VelmoraCampaign campaign;
    SW_REQUIRE_TRUE( campaign.initialize( TestVelmoraCampaignInternal::makeVitals(), hashed_string( "ridge" ) ) );
    const int32 fullQuarters = campaign.getVitals().getMaxHealthQuarters();
    SW_EXPECT_FALSE( campaign.takeDamage( 5 ) );
    SW_EXPECT_EQUAL( fullQuarters - 5, campaign.getVitals().computeHealthQuarters() );
    SW_EXPECT_FALSE( campaign.restAtCamp( *regions.find( hashed_string( "ridge" ) ) ) ); // 야영지가 없다
    SW_REQUIRE_TRUE( campaign.restAtCamp( *regions.find( hashed_string( "hearth" ) ) ) );
    SW_EXPECT_EQUAL( fullQuarters, campaign.getVitals().computeHealthQuarters() );
    SW_EXPECT_TRUE( campaign.getRespawnRegion() == hashed_string( "hearth" ) );

    SW_EXPECT_TRUE( campaign.takeDamage( fullQuarters ) );
    SW_EXPECT_TRUE( campaign.getVitals().isDead() );
    SW_REQUIRE_TRUE( campaign.respawn() );
    SW_EXPECT_EQUAL( fullQuarters, campaign.getVitals().computeHealthQuarters() );
    SW_EXPECT_FALSE( campaign.takeDamage( 3 ) );

    Archive archive;
    campaign.writeState( archive );
    VelmoraCampaign restored;
    SW_REQUIRE_TRUE( restored.initialize( TestVelmoraCampaignInternal::makeVitals(), hashed_string( "ridge" ) ) );
    Archive reading( archive.getData(), archive.getSize() );
    SW_REQUIRE_TRUE( restored.readState( reading ) );
    SW_EXPECT_EQUAL( fullQuarters - 3, restored.getVitals().computeHealthQuarters() );
    SW_EXPECT_TRUE( restored.getRespawnRegion() == hashed_string( "hearth" ) );
}

/**
 * @brief [VelmoraCampaignTest] 승계: 기본 판의 일지를 확장 팩이 더한 표로 열면 기본 진행이 그대로이고 확장 퀘스트는 처음부터 받을 수 있다.
 *        확장 판의 일지를 확장 팩 없는 표로 열면 확장 퀘스트만 경고와 함께 버리고 기본 진행은 이어진다(일지 전체를 막지 않는다).
 */
SW_TEST_CASE( VelmoraCampaignTest, QuestLogCarriesAcrossTheExpansion )
{
    QuestCatalog baseQuests;
    QuestCatalog expandedQuests;
    SW_REQUIRE_TRUE( baseQuests.loadFromXMLText( kBaseQuestXML, "VelmoraCampaignTest" ) );
    SW_REQUIRE_TRUE( expandedQuests.loadFromXMLText( kExpandedQuestXML, "VelmoraCampaignTest" ) );

    QuestLog baseLog;
    baseLog.initialize( &baseQuests );
    SW_REQUIRE_TRUE( baseLog.start( hashed_string( "oath" ), 0 ) == QuestStartResult::Ok );
    (void)baseLog.notify( hashed_string( "Enter" ), hashed_string( "ridge" ) );
    (void)baseLog.notify( hashed_string( "Light" ), hashed_string( "ridge_beacon" ) );
    (void)baseLog.notify( hashed_string( "Defeat" ), hashed_string( "warden" ) );
    SW_REQUIRE_TRUE( baseLog.getStatus( hashed_string( "oath" ) ) == QuestStatus::Completed );
    Archive baseSave;
    baseLog.writeState( baseSave );

    QuestLog expandedLog;
    expandedLog.initialize( &expandedQuests );
    Archive baseReading( baseSave.getData(), baseSave.getSize() );
    SW_REQUIRE_TRUE( expandedLog.readState( baseReading ) );
    SW_EXPECT_TRUE( expandedLog.getStatus( hashed_string( "oath" ) ) == QuestStatus::Completed );
    SW_REQUIRE_TRUE( expandedLog.start( hashed_string( "tide" ), 0 ) == QuestStartResult::Ok ); // 선행 퀘스트(oath)가 이어져 받을 수 있다
    Archive expandedSave;
    expandedLog.writeState( expandedSave );

    QuestLog backLog;
    backLog.initialize( &baseQuests );
    Archive readingExpanded( expandedSave.getData(), expandedSave.getSize() );
    SW_EXPECT_LOG( "QuestLog.QuestLogSavedQuestNot" );
    SW_REQUIRE_TRUE( backLog.readState( readingExpanded ) );
    SW_EXPECT_TRUE( backLog.getStatus( hashed_string( "oath" ) ) == QuestStatus::Completed );
    SW_EXPECT_TRUE( backLog.getStatus( hashed_string( "tide" ) ) == QuestStatus::NotStarted );
}
