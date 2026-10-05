// 횡스크롤 정복 키트 — 카탈로그, 건물 · 일꾼 · 생산 주기 · 수입, 훈련 비용 · 인구 한도, 부대 명령(따라오기 · 대기 · 돌격)과 진형,
// 지휘관 근처 사기, 성벽 보호 · 충차 · 사다리 공성과 점령 → 영토 · 수입, 시간 · 영토에 비례하는 반격 웨이브, 같은 입력의 같은 결과.
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/Kits/Strategy/SideScrollConquest/ConquestCatalog.h"
#include "GameFramework/Kits/Strategy/SideScrollConquest/ConquestWorld.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    constexpr const utf8* kConquestTestXml = R"(
<ConquestCatalog>
  <Rules waveUnit="raider" waveInterval="30" waveBaseCount="2" waveCountPerMinute="1" waveCountPerSite="2" captureTime="2" incomeInterval="10"
         moraleRadius="5" moraleDamageBonus="0.5" wallProtection="0.5" commanderHealth="100" commanderDamage="10" commanderRange="1.5"
         commanderSpeed="4" formationSpacing="1" followLeash="6" aggroRange="6" basePopulation="3" fixedStep="0.1"/>
  <Start wood="100" iron="20" gold="50"/>
  <Unit id="spearman" hp="40" damage="5" range="1" attackInterval="1" speed="2" pop="1" train="3"><Cost gold="10" iron="2"/></Unit>
  <Unit id="archer" hp="20" damage="3" range="5" speed="2" pop="1" train="3"><Cost gold="10" wood="5"/></Unit>
  <Unit id="cavalry" hp="60" damage="8" range="1" speed="5" pop="2" train="5"><Cost gold="30" iron="10"/></Unit>
  <Unit id="ram" hp="80" damage="10" range="1" speed="1" pop="2" siege="Ram" structureScale="5"><Cost wood="40"/></Unit>
  <Unit id="ladder" hp="20" damage="0" speed="2" pop="1" siege="Ladder"><Cost wood="10"/></Unit>
  <Unit id="raider" hp="30" damage="4" range="1" speed="2"/>
  <Unit id="guard" hp="50" damage="4" range="1" speed="1"/>
  <Building id="lumberMill" produces="wood" amount="2" cycleTime="5" workerSlots="3"><Cost gold="10"/></Building>
  <Building id="mine" produces="iron" amount="1" cycleTime="5" workerSlots="2"><Cost wood="30"/></Building>
  <Building id="barracks" trains="spearman,archer,cavalry,ram,ladder" housing="2"><Cost wood="40"/></Building>
  <Site id="home" x="0" kind="Village" owner="Player" workers="4" slots="3" housing="2" captureRadius="3"><Income gold="5"/></Site>
  <Site id="outpost" x="30" kind="Outpost" owner="Neutral" captureRadius="3" workers="2"><Income gold="3"/></Site>
  <Site id="keep" x="60" kind="Fortress" owner="Enemy" gate="100" wall="200" captureRadius="4" garrison="guard:2"/>
</ConquestCatalog>
)";

    void runWorld( ConquestWorld& world, float32 seconds )
    {
        const int32 stepCount = static_cast<int32>( seconds * 10.0f + 0.5f );
        for ( int32 stepIndex = 0; stepIndex < stepCount; ++stepIndex )
            world.update( 0.1f );
    }

    int32 countUnits( const ConquestWorld& world, const hashed_string& unitId, ConquestTeam team )
    {
        int32 count = 0;
        for ( const ConquestUnit& unit : world.getUnits() )
        {
            if ( unit._pDef->_id == unitId && unit._team == team )
                ++count;
        }
        return count;
    }

    bool hasEvent( const vector<ConquestEvent>& listEvent, ConquestEvent::Kind kind )
    {
        for ( const ConquestEvent& event : listEvent )
        {
            if ( event._kind == kind )
                return true;
        }
        return false;
    }

    /** @brief @p text 에서 처음 나오는 @p pFrom 을 @p pTo 로 바꿉니다. 없으면 false 입니다. */
    bool replaceFirst( string& text, const utf8* pFrom, const utf8* pTo )
    {
        const size_t at = text.find( pFrom );
        if ( at == string::npos )
            return false;
        text.replace( at, std::strlen( pFrom ), pTo );
        return true;
    }

    /** @brief 웨이브가 없는 카탈로그(부대 · 공성만 보는 시험)입니다. */
    void disableWaves( ConquestCatalog& catalog )
    {
        ConquestRules rules = catalog.getRules();
        rules._waveInterval = 0.0f;
        catalog.setRules( rules );
    }
} // namespace

SW_TEST_CASE( SideScrollConquestTest, CatalogReadsUnitsBuildingsSitesAndRules )
{
    ConquestCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( kConquestTestXml, "SideScrollConquestTest" ) );
    const ConquestUnitDef* pRam = catalog.findUnit( "ram" );
    SW_ASSERT_NOT_NULL( pRam );
    SW_EXPECT_TRUE( pRam->_siegeRole == ConquestSiegeRole::Ram );
    SW_EXPECT_NEAR_EQUAL( 5.0f, pRam->_structureScale, 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( 40.0f, pRam->_cost.getValue( "wood" ), 1.0e-4f );
    SW_EXPECT_TRUE( catalog.findUnit( "ladder" )->_siegeRole == ConquestSiegeRole::Ladder );

    const ConquestBuildingDef* pBarracks = catalog.findBuilding( "barracks" );
    SW_ASSERT_NOT_NULL( pBarracks );
    SW_EXPECT_EQUAL( 5, static_cast<int32>( pBarracks->_listTrainable.size() ) );
    SW_EXPECT_EQUAL( 2, pBarracks->_housing );
    SW_EXPECT_TRUE( catalog.findBuilding( "lumberMill" )->_produces == hashed_string( "wood" ) );

    const ConquestSiteDef* pKeep = catalog.findSite( "keep" );
    SW_ASSERT_NOT_NULL( pKeep );
    SW_EXPECT_TRUE( pKeep->_kind == ConquestSiteKind::Fortress );
    SW_EXPECT_TRUE( pKeep->_owner == ConquestTeam::Enemy );
    SW_EXPECT_NEAR_EQUAL( 100.0f, pKeep->_gateHealth, 1.0e-4f );
    SW_ASSERT_TRUE( pKeep->_listGarrison.size() == 1 );
    SW_EXPECT_EQUAL( 2, pKeep->_listGarrison[0]._count );
    SW_EXPECT_NEAR_EQUAL( 3.0f, catalog.findSite( "outpost" )->_income.getValue( "gold" ), 1.0e-4f );

    const ConquestRules& rules = catalog.getRules();
    SW_EXPECT_TRUE( rules._waveUnit == hashed_string( "raider" ) );
    SW_EXPECT_NEAR_EQUAL( 100.0f, rules._startResources.getValue( "wood" ), 1.0e-4f );
    SW_EXPECT_EQUAL( 3, rules._basePopulation );
}

SW_TEST_CASE( SideScrollConquestTest, BuildingsWorkersProductionAndTrainingRespectCostsAndPopulation )
{
    ConquestCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( kConquestTestXml, "SideScrollConquestTest" ) );
    disableWaves( catalog );
    ConquestWorld world;
    world.initialize( &catalog );
    SW_EXPECT_EQUAL( 2, countUnits( world, "guard", ConquestTeam::Enemy ) ); // 요새 주둔군

    SW_EXPECT_TRUE( world.placeBuilding( "lumberMill", "home" ) == ConquestResult::Ok );
    SW_EXPECT_TRUE( world.placeBuilding( "mine", "home" ) == ConquestResult::Ok );
    SW_EXPECT_TRUE( world.placeBuilding( "barracks", "home" ) == ConquestResult::Ok );
    SW_EXPECT_TRUE( world.placeBuilding( "barracks", "home" ) == ConquestResult::NoBuildSlot );
    SW_EXPECT_TRUE( world.placeBuilding( "mine", "keep" ) == ConquestResult::SiteNotOwned );
    SW_EXPECT_TRUE( world.placeBuilding( "castle", "home" ) == ConquestResult::UnknownDef );
    SW_EXPECT_EQUAL( 30, world.getResource( "wood" ) );
    SW_EXPECT_EQUAL( 40, world.getResource( "gold" ) );

    // 일꾼은 마을이 준 넷뿐.
    SW_EXPECT_TRUE( world.assignWorkers( 0, 3 ) == ConquestResult::Ok );
    SW_EXPECT_TRUE( world.assignWorkers( 1, 2 ) == ConquestResult::NoFreeWorkers );
    SW_EXPECT_TRUE( world.assignWorkers( 1, 1 ) == ConquestResult::Ok );
    SW_EXPECT_EQUAL( 0, world.computeFreeWorkers() );

    // 생산 주기 5 초: 나무 2 × 일꾼 3, 철 1 × 1. 수입 10 초마다 마을 금 5.
    runWorld( world, 5.5f );
    SW_EXPECT_EQUAL( 36, world.getResource( "wood" ) );
    SW_EXPECT_EQUAL( 21, world.getResource( "iron" ) );
    runWorld( world, 5.0f );
    SW_EXPECT_EQUAL( 42, world.getResource( "wood" ) );
    SW_EXPECT_EQUAL( 22, world.getResource( "iron" ) );
    SW_EXPECT_EQUAL( 45, world.getResource( "gold" ) );

    // 인구 한도 = 기본 3 + 마을 2 + 병영 2. 대기열도 센다.
    SW_EXPECT_EQUAL( 7, world.computePopulationCap() );
    SW_EXPECT_TRUE( world.trainUnit( 2, "cavalry" ) == ConquestResult::Ok );
    SW_EXPECT_TRUE( world.trainUnit( 2, "spearman" ) == ConquestResult::Ok );
    SW_EXPECT_EQUAL( 5, world.getResource( "gold" ) );
    SW_EXPECT_TRUE( world.trainUnit( 2, "archer" ) == ConquestResult::NotEnoughResources );
    SW_EXPECT_TRUE( world.trainUnit( 0, "spearman" ) == ConquestResult::CannotTrainHere );
    world.addResource( "gold", 100.0f );
    world.addResource( "iron", 50.0f );
    SW_EXPECT_TRUE( world.trainUnit( 2, "cavalry" ) == ConquestResult::Ok );
    SW_EXPECT_TRUE( world.trainUnit( 2, "cavalry" ) == ConquestResult::Ok ); // 정확히 7
    SW_EXPECT_EQUAL( 7, world.computePopulation() );
    SW_EXPECT_TRUE( world.trainUnit( 2, "spearman" ) == ConquestResult::PopulationCap );

    vector<ConquestEvent> listEvent;
    world.drainEvents( listEvent );
    listEvent.clear();
    runWorld( world, 5.5f ); // 기병(5 초) 하나만 나온다
    world.drainEvents( listEvent );
    SW_EXPECT_TRUE( hasEvent( listEvent, ConquestEvent::Kind::UnitTrained ) );
    SW_EXPECT_EQUAL( 1, world.getSquadSize() );
    SW_EXPECT_EQUAL( 1, countUnits( world, "cavalry", ConquestTeam::Player ) );
    SW_EXPECT_EQUAL( 7, world.computePopulation() ); // 나온 병사 + 대기열
}

SW_TEST_CASE( SideScrollConquestTest, SquadFollowsHoldsAndChargesInFormation )
{
    ConquestCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( kConquestTestXml, "SideScrollConquestTest" ) );
    disableWaves( catalog );
    ConquestWorld world;
    world.initialize( &catalog );
    const int32 first  = world.spawnUnit( "spearman", ConquestTeam::Player, 0.0f, ConquestOrder::Follow );
    const int32 second = world.spawnUnit( "spearman", ConquestTeam::Player, 0.0f, ConquestOrder::Follow );
    const int32 third  = world.spawnUnit( "spearman", ConquestTeam::Player, 0.0f, ConquestOrder::Follow );
    SW_EXPECT_EQUAL( 3, world.getSquadSize() );

    // 따라오기: 지휘관(10) 뒤로 간격 1 씩.
    world.setCommanderMove( 1.0f );
    runWorld( world, 2.5f );
    world.setCommanderMove( 0.0f );
    runWorld( world, 5.0f );
    SW_EXPECT_NEAR_EQUAL( 10.0f, world.getCommander()._x, 1.0e-3f );
    SW_EXPECT_NEAR_EQUAL( 9.0f, world.findUnit( first )->_x, 1.0e-3f );
    SW_EXPECT_NEAR_EQUAL( 8.0f, world.findUnit( second )->_x, 1.0e-3f );
    SW_EXPECT_NEAR_EQUAL( 7.0f, world.findUnit( third )->_x, 1.0e-3f );

    // 대기: 지휘관이 떠나도 그 자리.
    world.issueOrder( ConquestOrder::Hold );
    world.setCommanderMove( 1.0f );
    runWorld( world, 2.5f );
    world.setCommanderMove( 0.0f );
    runWorld( world, 3.0f );
    SW_EXPECT_NEAR_EQUAL( 20.0f, world.getCommander()._x, 1.0e-3f );
    SW_EXPECT_NEAR_EQUAL( 9.0f, world.findUnit( first )->_x, 1.0e-3f );
    SW_EXPECT_NEAR_EQUAL( 7.0f, world.findUnit( third )->_x, 1.0e-3f );

    // 다시 따라오기 — 멀리(40) 있는 적은 알아채지 못한다(알아채는 거리 6).
    const int32 raider = world.spawnUnit( "raider", ConquestTeam::Enemy, 40.0f, ConquestOrder::Hold );
    world.issueOrder( ConquestOrder::Follow );
    runWorld( world, 8.0f );
    SW_EXPECT_NEAR_EQUAL( 19.0f, world.findUnit( first )->_x, 1.0e-3f );
    SW_EXPECT_NEAR_EQUAL( 17.0f, world.findUnit( third )->_x, 1.0e-3f );
    SW_EXPECT_TRUE( world.findUnit( raider ) != nullptr );

    // 돌격: 거리와 상관없이 가장 가까운 적에게 — 셋이서 쓰러뜨린다.
    world.issueOrder( ConquestOrder::Charge );
    runWorld( world, 20.0f );
    SW_EXPECT_TRUE( world.findUnit( raider ) == nullptr );
    SW_EXPECT_TRUE( world.findUnit( first )->_x > 30.0f );
}

SW_TEST_CASE( SideScrollConquestTest, MoraleNearTheCommanderAndWallsChangeDamage )
{
    ConquestCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( kConquestTestXml, "SideScrollConquestTest" ) );
    disableWaves( catalog );

    // 같은 싸움을 지휘관 근처(4)와 먼 곳(20)에서 — 사기 버프는 피해 1.5 배.
    ConquestWorld nearWorld;
    nearWorld.initialize( &catalog );
    const int32 nearSpear = nearWorld.spawnUnit( "spearman", ConquestTeam::Player, 4.0f, ConquestOrder::Hold );
    (void)nearWorld.spawnUnit( "guard", ConquestTeam::Enemy, 4.5f, ConquestOrder::Hold );
    ConquestWorld farWorld;
    farWorld.initialize( &catalog );
    const int32 farSpear = farWorld.spawnUnit( "spearman", ConquestTeam::Player, 20.0f, ConquestOrder::Hold );
    (void)farWorld.spawnUnit( "guard", ConquestTeam::Enemy, 20.5f, ConquestOrder::Hold );
    runWorld( nearWorld, 3.0f );
    runWorld( farWorld, 3.0f );
    const float32 nearDealt = nearWorld.findUnit( nearSpear )->_damageDealt;
    const float32 farDealt  = farWorld.findUnit( farSpear )->_damageDealt;
    SW_EXPECT_NEAR_EQUAL( 15.0f, farDealt, 1.0e-3f ); // 0 · 1 · 2 초에 한 번씩
    SW_EXPECT_NEAR_EQUAL( farDealt * 1.5f, nearDealt, 1.0e-3f );

    // 사기 규칙을 끄면 같다.
    ConquestCatalog flatCatalog = catalog;
    ConquestRules   rules       = flatCatalog.getRules();
    rules._moraleDamageBonus    = 0.0f;
    flatCatalog.setRules( rules );
    ConquestWorld flatWorld;
    flatWorld.initialize( &flatCatalog );
    const int32 flatSpear = flatWorld.spawnUnit( "spearman", ConquestTeam::Player, 4.0f, ConquestOrder::Hold );
    (void)flatWorld.spawnUnit( "guard", ConquestTeam::Enemy, 4.5f, ConquestOrder::Hold );
    runWorld( flatWorld, 3.0f );
    SW_EXPECT_NEAR_EQUAL( farDealt, flatWorld.findUnit( flatSpear )->_damageDealt, 1.0e-3f );

    // 성벽이 선 요새 안의 수비대는 절반만 받는다.
    ConquestWorld wallWorld;
    wallWorld.initialize( &catalog );
    const int32 inside = wallWorld.spawnUnit( "spearman", ConquestTeam::Player, 60.5f, ConquestOrder::Hold );
    runWorld( wallWorld, 0.5f );
    SW_EXPECT_NEAR_EQUAL( 2.5f, wallWorld.findUnit( inside )->_damageDealt, 1.0e-3f );
}

SW_TEST_CASE( SideScrollConquestTest, GatesBlockRamsBreachLaddersBypassAndCaptureEndsTheWar )
{
    ConquestCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( kConquestTestXml, "SideScrollConquestTest" ) );
    disableWaves( catalog );

    // 성문(요새 구역 가장자리 56)은 막는다 — 창병은 그 앞에서 성문을 조금씩 때릴 뿐.
    ConquestWorld blocked;
    blocked.initialize( &catalog );
    const int32 lone = blocked.spawnUnit( "spearman", ConquestTeam::Player, 50.0f, ConquestOrder::Charge );
    runWorld( blocked, 10.0f );
    SW_EXPECT_TRUE( blocked.findUnit( lone )->_x <= 55.5f + 1.0e-3f );
    SW_EXPECT_TRUE( blocked.findSite( "keep" )->_gateHealth > 90.0f ); // 성문에는 피해 배율 0.2

    // 사다리가 걸치면 성문을 부수지 않고 넘는다.
    ConquestWorld laddered;
    laddered.initialize( &catalog );
    const int32 ladder = laddered.spawnUnit( "ladder", ConquestTeam::Player, 50.0f, ConquestOrder::Charge );
    runWorld( laddered, 4.0f );
    SW_EXPECT_TRUE( laddered.findUnit( ladder )->_bPlanted == SW_TRUE );
    const int32 climber = laddered.spawnUnit( "spearman", ConquestTeam::Player, 50.0f, ConquestOrder::Charge );
    runWorld( laddered, 4.0f );
    SW_EXPECT_TRUE( laddered.findUnit( climber ) != nullptr && laddered.findUnit( climber )->_x > 56.0f );
    SW_EXPECT_NEAR_EQUAL( 100.0f, laddered.findSite( "keep" )->_gateHealth, 1.0e-3f );

    // 충차가 성문(그다음 성벽)을 부수고 기병이 수비대를 쓸면 점령 — 적 거점이 없으니 승리.
    ConquestWorld siege;
    siege.initialize( &catalog );
    (void)siege.spawnUnit( "ram", ConquestTeam::Player, 50.0f, ConquestOrder::Charge );
    for ( int32 index = 0; index < 4; ++index )
        (void)siege.spawnUnit( "cavalry", ConquestTeam::Player, 45.0f - static_cast<float32>( index ), ConquestOrder::Charge );
    vector<ConquestEvent> listEvent;
    for ( int32 second = 0; second < 90 && siege.isVictory() == false; ++second )
        runWorld( siege, 1.0f );
    siege.drainEvents( listEvent );
    SW_EXPECT_TRUE( hasEvent( listEvent, ConquestEvent::Kind::GateBroken ) );
    SW_EXPECT_TRUE( hasEvent( listEvent, ConquestEvent::Kind::SiteCaptured ) );
    SW_EXPECT_TRUE( siege.findSite( "keep" )->_owner == ConquestTeam::Player );
    SW_EXPECT_EQUAL( 0, countUnits( siege, "guard", ConquestTeam::Enemy ) );
    SW_EXPECT_EQUAL( 2, siege.computeTerritory() );
    SW_EXPECT_TRUE( siege.isVictory() );
}

SW_TEST_CASE( SideScrollConquestTest, TerritoryRaisesIncomeAndCounterAttackWavesGrowWithTimeAndLand )
{
    ConquestCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( kConquestTestXml, "SideScrollConquestTest" ) );
    ConquestWorld world;
    world.initialize( &catalog );
    SW_EXPECT_EQUAL( 2, world.computeWaveSize() );

    // 지휘관이 중립 전초기지(30)에 2 초 머물면 점령.
    world.setCommanderMove( 1.0f );
    runWorld( world, 7.5f );
    world.setCommanderMove( 0.0f );
    runWorld( world, 2.5f );
    SW_EXPECT_TRUE( world.findSite( "outpost" )->_owner == ConquestTeam::Player );
    SW_EXPECT_EQUAL( 2, world.computeTerritory() );
    SW_EXPECT_EQUAL( 4, world.computeWaveSize() ); // 기본 2 + 영토(2 − 1) × 2
    SW_EXPECT_EQUAL( 6, world.computeFreeWorkers() );

    // 다음 수입: 마을 5 + 전초기지 3(수입 시각 10 초 경계를 비켜서 잰다).
    runWorld( world, 0.5f );
    const int32 goldBefore = world.getResource( "gold" );
    runWorld( world, 10.0f );
    SW_EXPECT_EQUAL( goldBefore + 8, world.getResource( "gold" ) );

    // 30 초 웨이브는 지금 크기(4)로 가장 오른쪽 적 거점에서 나온다.
    vector<ConquestEvent> listEvent;
    world.drainEvents( listEvent );
    listEvent.clear();
    runWorld( world, 30.0f - world.getElapsed() + 0.05f );
    world.drainEvents( listEvent );
    SW_EXPECT_TRUE( hasEvent( listEvent, ConquestEvent::Kind::WaveSpawned ) );
    SW_EXPECT_EQUAL( 4, countUnits( world, "raider", ConquestTeam::Enemy ) );

    // 시간도 크기를 키운다 — 1 분이 지나면 하나 더(영토는 그대로인 조용한 판).
    ConquestCatalog quietCatalog = catalog;
    ConquestRules   rules        = quietCatalog.getRules();
    rules._waveInterval          = 1000.0f;
    quietCatalog.setRules( rules );
    ConquestWorld quiet;
    quiet.initialize( &quietCatalog );
    runWorld( quiet, 61.0f );
    SW_EXPECT_EQUAL( 3, quiet.computeWaveSize() );
}

SW_TEST_CASE( SideScrollConquestTest, SameInputsGiveTheSameBattle )
{
    ConquestCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( kConquestTestXml, "SideScrollConquestTest" ) );
    ConquestWorld  first;
    ConquestWorld  second;
    ConquestWorld* arrWorld[2] = { &first, &second };
    for ( ConquestWorld* pWorld : arrWorld )
    {
        pWorld->initialize( &catalog );
        (void)pWorld->placeBuilding( "barracks", "home" );
        (void)pWorld->trainUnit( 0, "spearman" );
        (void)pWorld->trainUnit( 0, "archer" );
        (void)pWorld->spawnUnit( "ram", ConquestTeam::Player, 10.0f, ConquestOrder::Charge );
        pWorld->setCommanderMove( 1.0f );
    }
    for ( int32 frame = 0; frame < 400; ++frame )
    {
        // 프레임 시간이 들쭉날쭉해도 고정 걸음이라 같다.
        const float32 deltaTime = frame % 3 == 0 ? 0.13f : 0.07f;
        first.update( deltaTime );
        second.update( deltaTime );
        if ( frame == 150 )
        {
            first.issueOrder( ConquestOrder::Charge );
            second.issueOrder( ConquestOrder::Charge );
        }
    }
    SW_ASSERT_TRUE( first.getUnits().size() == second.getUnits().size() );
    SW_EXPECT_TRUE( first.getUnits().size() > 2 ); // 웨이브가 나왔다
    for ( size_t index = 0; index < first.getUnits().size(); ++index )
    {
        SW_EXPECT_EQUAL( first.getUnits()[index]._unitId, second.getUnits()[index]._unitId );
        SW_EXPECT_NEAR_EQUAL( first.getUnits()[index]._x, second.getUnits()[index]._x, 1.0e-6f );
        SW_EXPECT_NEAR_EQUAL( first.getUnits()[index]._health, second.getUnits()[index]._health, 1.0e-6f );
    }
    SW_EXPECT_NEAR_EQUAL( first.getCommander()._x, second.getCommander()._x, 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( first.getCommander()._health, second.getCommander()._health, 1.0e-6f );
    SW_EXPECT_EQUAL( first.getResource( "gold" ), second.getResource( "gold" ) );
    SW_EXPECT_NEAR_EQUAL( first.findSite( "keep" )->_gateHealth, second.findSite( "keep" )->_gateHealth, 1.0e-6f );
}

/**
 * @brief [SideScrollConquestTest] 공격 빈도는 간격을 따른다 — 0.1 초 걸음에서 0.75 초 간격의 병사 · 지휘관이 60 초에 81 번(첫 타 포함, ±1) 친다
 * @details 간격이 걸음의 배수가 아니면, 끝난 걸음에 간격으로 덮을 때 지나친 몫을 버려 8 걸음(0.8 초)마다 76 번이 된다.
 */
SW_TEST_CASE( SideScrollConquestTest, AttackRateFollowsTheIntervalNotTheStep )
{
    string xml = kConquestTestXml;
    SW_ASSERT_TRUE( replaceFirst( xml, R"(damage="5" range="1" attackInterval="1")", R"(damage="5" range="1" attackInterval="0.75")" ) );
    SW_ASSERT_TRUE( replaceFirst( xml, R"(<Unit id="guard" hp="50" damage="4")", R"(<Unit id="guard" hp="100000" damage="0")" ) );
    ConquestCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( xml.c_str(), "SideScrollConquestTest" ) );
    disableWaves( catalog );
    ConquestRules rules            = catalog.getRules();
    rules._moraleDamageBonus       = 0.0f;
    rules._commanderAttackInterval = 0.75f;
    catalog.setRules( rules );
    const float32 seconds = 60.0f;
    const float32 design  = seconds / 0.75f + 1.0f; // 0 초에 첫 타

    // 병사 — 지휘관에게서 먼 곳에 세워 둔 과녁을 친다.
    ConquestWorld unitWorld;
    unitWorld.initialize( &catalog );
    const int32 spear = unitWorld.spawnUnit( "spearman", ConquestTeam::Player, 20.0f, ConquestOrder::Hold );
    (void)unitWorld.spawnUnit( "guard", ConquestTeam::Enemy, 20.5f, ConquestOrder::Hold );
    runWorld( unitWorld, seconds );
    SW_EXPECT_NEAR_EQUAL( design, unitWorld.findUnit( spear )->_damageDealt / 5.0f, 1.0f );

    // 지휘관 — 바로 앞의 과녁을 친다(지휘관 피해 10). 요새의 수비대(60)는 세지 않는다.
    ConquestWorld commanderWorld;
    commanderWorld.initialize( &catalog );
    (void)commanderWorld.spawnUnit( "guard", ConquestTeam::Enemy, commanderWorld.getCommander()._x + 1.0f, ConquestOrder::Hold );
    runWorld( commanderWorld, seconds );
    float32 guardLoss = 0.0f;
    for ( const ConquestUnit& unit : commanderWorld.getUnits() )
    {
        if ( unit._team == ConquestTeam::Enemy && unit._x < 30.0f )
            guardLoss = 100000.0f - unit._health;
    }
    SW_EXPECT_NEAR_EQUAL( design, guardLoss / 10.0f, 1.0f );
}
