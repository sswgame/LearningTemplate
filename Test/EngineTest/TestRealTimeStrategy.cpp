#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Kits/Strategy/RealTimeStrategy/RtsAiController.h"
#include "GameFramework/Kits/Strategy/RealTimeStrategy/RtsCatalog.h"
#include "GameFramework/Kits/Strategy/RealTimeStrategy/RtsSelection.h"
#include "GameFramework/Kits/Strategy/RealTimeStrategy/RtsWorld.h"

#include "TestFramework/TestFramework.h"

// 실시간 전략 키트(스타크래프트 장르) — 카탈로그, 채취 순환과 다 캔 광물, 생산 대기열 · 보급 · 테크, 일꾼 건설과 정제소 가스,
// 방어 · 최소 피해 · 공중 목표 · 공격 이동, 전장의 안개, 흐름장 무리 이동, 고르기 · 부대, 행동 트리 AI 의 운영과 승패.

using namespace sw;

namespace
{
    constexpr const utf8* kRtsTestXml = R"(
<RtsCatalog supplyMax="200">
  <Unit id="minerals" kind="Resource" resource="Minerals" amount="1500" footprint="1"/>
  <Unit id="shard" kind="Resource" resource="Minerals" amount="10" footprint="1"/>
  <Unit id="geyser" kind="Resource" resource="Gas" amount="500" footprint="2"/>
  <Unit id="base" kind="Building" hp="1500" armor="1" footprint="4" depot="true" provides="10" producedBy="worker" minerals="400" buildTime="60" sight="10"/>
  <Unit id="worker" hp="40" speed="3" radius="0.35" damage="5" range="0.2" cooldown="1.1" worker="true" producedBy="base" minerals="50" supply="1" buildTime="12" gatherTime="2" cargo="5" sight="7"/>
  <Unit id="depot" kind="Building" hp="400" footprint="2" provides="8" producedBy="worker" minerals="100" buildTime="20"/>
  <Unit id="barracks" kind="Building" hp="1000" footprint="3" producedBy="worker" minerals="150" buildTime="40" requires="base"/>
  <Unit id="academy" kind="Building" hp="600" footprint="3" producedBy="worker" minerals="150" buildTime="40"/>
  <Unit id="refinery" kind="Building" extractor="true" hp="500" footprint="2" producedBy="worker" minerals="75" buildTime="20"/>
  <Unit id="turret" kind="Building" hp="200" footprint="2" damage="10" range="7" cooldown="1" targets="Air" producedBy="worker" minerals="100" buildTime="20" sight="9"/>
  <Unit id="wall" kind="Building" hp="300" armor="10" footprint="2" producedBy="worker" minerals="50" buildTime="10"/>
  <Unit id="marine" hp="40" speed="2.25" radius="0.35" damage="6" range="4" cooldown="0.86" targets="Ground,Air" producedBy="barracks" minerals="50" supply="1" buildTime="18" sight="9"/>
  <Unit id="zealot" hp="60" armor="1" speed="2.75" radius="0.45" damage="16" range="0.2" cooldown="1.2" producedBy="barracks" minerals="100" supply="2" buildTime="27" requires="academy"/>
  <Unit id="wraith" air="true" hp="120" speed="4" radius="0.5" damage="8" range="5" cooldown="1.5" targets="Ground,Air" producedBy="barracks" minerals="150" gas="100" supply="2" buildTime="40"/>
  <Unit id="tank" kind="Vehicle" hp="150"/>
</RtsCatalog>
)";

    struct RtsTestScene
    {
        RtsCatalog       _catalog;
        RtsWorld         _world;
        vector<RtsEvent> _listEvent;

        bool initialize( int32 width = 48, int32 height = 48 )
        {
            if ( _catalog.loadFromXmlText( kRtsTestXml, "RealTimeStrategyTest" ) == false )
                return false;
            _world.initialize( &_catalog, width, height, RtsSettings{} );
            return true;
        }

        RtsUnitId spawn( const utf8* pId, int32 owner, float32 x, float32 z ) { return _world.spawnUnit( hashed_string( pId ), owner, float3{ x, 0.0f, z } ); }

        void run( float32 seconds )
        {
            for ( float32 time = 0.0f; time < seconds; time += 0.1f )
            {
                _world.update( 0.1f );
                _world.drainEvents( _listEvent );
            }
        }

        int32 countEvents( RtsEvent::Kind kind, int32 player = -2 ) const
        {
            int32 count = 0;
            for ( const RtsEvent& event : _listEvent )
                count += event._kind == kind && ( player == -2 || event._player == player ) ? 1 : 0;
            return count;
        }

        float32 computeDistance( RtsUnitId unitId, const float3& point ) const
        {
            const RtsUnit* pUnit = _world.findUnit( unitId );
            if ( pUnit == nullptr )
                return MathUtil::MaxFloat;
            const float32 dx = pUnit->_position._x - point._x;
            const float32 dz = pUnit->_position._z - point._z;
            return MathUtil::sqrt( dx * dx + dz * dz );
        }
    };
} // namespace

SW_TEST_CASE( RealTimeStrategyTest, CatalogReadsUnitsBuildingsAndResources )
{
    RtsCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( kRtsTestXml, "RealTimeStrategyTest" ) );
    SW_EXPECT_EQUAL( 15, static_cast<int32>( catalog.getUnits().size() ) );
    SW_EXPECT_EQUAL( 200, catalog.getSupplyMax() );

    const RtsUnitDef* pBase = catalog.findUnit( hashed_string( "base" ) );
    SW_ASSERT_NOT_NULL( pBase );
    SW_EXPECT_TRUE( pBase->_kind == RtsUnitKind::Building );
    SW_EXPECT_TRUE( pBase->_bDepot != SW_FALSE );
    SW_EXPECT_EQUAL( 10, pBase->_supplyProvided );
    SW_EXPECT_NEAR_EQUAL( 2.0f, pBase->_radius, 1.0e-4f ); // 건물의 몸은 자리 반 변
    SW_EXPECT_NEAR_EQUAL( 0.0f, pBase->_speed, 1.0e-4f );

    const RtsUnitDef* pMarine = catalog.findUnit( hashed_string( "marine" ) );
    SW_ASSERT_NOT_NULL( pMarine );
    SW_EXPECT_TRUE( pMarine->_bTargetsGround != SW_FALSE && pMarine->_bTargetsAir != SW_FALSE );
    SW_EXPECT_TRUE( pMarine->canAttack() && pMarine->isMobile() );
    const RtsUnitDef* pZealot = catalog.findUnit( hashed_string( "zealot" ) );
    SW_EXPECT_TRUE( pZealot->_bTargetsGround != SW_FALSE && pZealot->_bTargetsAir == SW_FALSE );
    SW_EXPECT_TRUE( pZealot->_requires == hashed_string( "academy" ) );
    SW_EXPECT_TRUE( catalog.findUnit( hashed_string( "turret" ) )->_bTargetsGround == SW_FALSE );
    SW_EXPECT_TRUE( catalog.findUnit( hashed_string( "geyser" ) )->_resourceType == RtsResourceType::Gas );
    SW_EXPECT_TRUE( catalog.findUnit( hashed_string( "tank" ) )->_kind == RtsUnitKind::Unit ); // 모르는 종류는 유닛으로 읽는다

    vector<const RtsUnitDef*> listProduct;
    catalog.findProducts( hashed_string( "barracks" ), listProduct );
    SW_EXPECT_EQUAL( 3, static_cast<int32>( listProduct.size() ) );
    catalog.findProducts( hashed_string( "worker" ), listProduct );
    SW_EXPECT_EQUAL( 7, static_cast<int32>( listProduct.size() ) );
}

SW_TEST_CASE( RealTimeStrategyTest, WorkersGatherReturnCargoAndMoveOnFromDepletedPatches )
{
    RtsTestScene scene;
    SW_ASSERT_TRUE( scene.initialize() );
    const int32     player = scene._world.addPlayer( 0, 0, 0, float3{ 6.0f, 0.0f, 6.0f } );
    const RtsUnitId baseId = scene.spawn( "base", player, 4.5f, 4.5f );
    SW_ASSERT_TRUE( baseId.isValid() );
    SW_EXPECT_TRUE( scene._world.getGrid().isWalkable( 5, 5 ) == false ); // 본진이 칸을 막는다
    const RtsUnitId shardId = scene.spawn( "shard", RtsWorld::kNoOwner, 5.5f, 12.5f );
    (void)scene.spawn( "minerals", RtsWorld::kNoOwner, 6.5f, 12.5f );
    (void)scene.spawn( "minerals", RtsWorld::kNoOwner, 7.5f, 12.5f );
    const RtsUnitId workerA = scene.spawn( "worker", player, 10.5f, 10.5f );
    const RtsUnitId workerB = scene.spawn( "worker", player, 10.5f, 9.5f );
    SW_EXPECT_TRUE( scene._world.findUnit( shardId )->_owner == RtsWorld::kNoOwner );

    SW_EXPECT_TRUE( scene._world.issueGather( workerA, shardId ) == RtsCommandResult::Ok );
    SW_EXPECT_TRUE( scene._world.issueGather( workerB, shardId ) == RtsCommandResult::Ok );
    SW_EXPECT_TRUE( scene._world.issueGather( workerA, baseId ) == RtsCommandResult::CannotDo ); // 본진은 캐지 않는다
    SW_EXPECT_TRUE( scene._world.issueGather( baseId, shardId ) == RtsCommandResult::CannotDo ); // 일꾼만
    SW_EXPECT_TRUE( scene._world.issueGather( workerA, shardId ) == RtsCommandResult::Ok );

    scene.run( 40.0f );
    const RtsPlayer* pPlayer = scene._world.findPlayer( player );
    SW_EXPECT_TRUE( pPlayer->_minerals >= 40 );
    SW_EXPECT_EQUAL( 0, pPlayer->_minerals % 5 );
    SW_EXPECT_TRUE( scene.countEvents( RtsEvent::Kind::ResourcesDeposited, player ) >= 8 );
    // 조각(10)은 두 번에 다 캐 사라지고, 두 일꾼은 옆 광물로 옮겨 계속 캔다.
    SW_EXPECT_NULL( scene._world.findUnit( shardId ) );
    SW_EXPECT_EQUAL( 1, scene.countEvents( RtsEvent::Kind::ResourceDepleted ) );
    SW_EXPECT_TRUE( scene._world.getGrid().isWalkable( 5, 12 ) ); // 사라진 광물의 칸이 열린다
    for ( const RtsUnitId workerId : { workerA, workerB } )
    {
        const RtsOrder* pOrder = scene._world.findUnit( workerId )->findOrder();
        SW_ASSERT_NOT_NULL( pOrder );
        SW_EXPECT_TRUE( pOrder->_type == RtsOrderType::Gather );
    }
    const int32 left = scene._world.findUnit( scene._world.findNearestResource( float3{ 6.5f, 0.0f, 12.5f }, RtsResourceType::Minerals, 3.0f ) )->_resourceLeft;
    SW_EXPECT_TRUE( left < 1500 );
}

SW_TEST_CASE( RealTimeStrategyTest, ProductionQueuesChargeUpFrontAndWaitForSupplyAndTech )
{
    RtsTestScene scene;
    SW_ASSERT_TRUE( scene.initialize() );
    const int32     player     = scene._world.addPlayer( 0, 1000, 0, float3{ 6.0f, 0.0f, 6.0f } );
    const RtsUnitId baseId     = scene.spawn( "base", player, 4.5f, 4.5f );
    const RtsUnitId barracksId = scene.spawn( "barracks", player, 20.5f, 4.5f );
    for ( int32 index = 0; index < 9; ++index )
        (void)scene.spawn( "worker", player, 10.5f + static_cast<float32>( index ), 20.5f );
    scene.run( 0.1f );
    const RtsPlayer* pPlayer = scene._world.findPlayer( player );
    SW_EXPECT_EQUAL( 9, pPlayer->_supplyUsed );
    SW_EXPECT_EQUAL( 10, pPlayer->_supplyCap );

    SW_EXPECT_TRUE( scene._world.train( baseId, hashed_string( "marine" ) ) == RtsCommandResult::CannotDo );
    SW_EXPECT_TRUE( scene._world.train( barracksId, hashed_string( "zealot" ) ) == RtsCommandResult::TechRequired );
    SW_EXPECT_TRUE( scene._world.train( barracksId, hashed_string( "wraith" ) ) == RtsCommandResult::NotEnoughGas );
    SW_EXPECT_TRUE( scene._world.train( baseId, hashed_string( "worker" ) ) == RtsCommandResult::Ok );
    SW_EXPECT_TRUE( scene._world.train( baseId, hashed_string( "worker" ) ) == RtsCommandResult::Ok );
    SW_EXPECT_EQUAL( 900, pPlayer->_minerals ); // 넣을 때 낸다

    // 첫 일꾼은 보급 10/10 으로 나오고, 둘째는 보급이 막힌다.
    scene.run( 25.0f );
    SW_EXPECT_EQUAL( 10, scene._world.countUnits( player, hashed_string( "worker" ), true ) );
    SW_EXPECT_EQUAL( 1, scene.countEvents( RtsEvent::Kind::SupplyBlocked, player ) );
    SW_EXPECT_EQUAL( 1, static_cast<int32>( scene._world.findUnit( baseId )->_listProduction.size() ) );

    // 보급 건물이 서면 이어서 나온다.
    (void)scene.spawn( "depot", player, 30.5f, 30.5f );
    scene.run( 13.0f );
    SW_EXPECT_EQUAL( 11, scene._world.countUnits( player, hashed_string( "worker" ), true ) );
    SW_EXPECT_EQUAL( 18, pPlayer->_supplyCap );
    SW_EXPECT_EQUAL( 2, scene.countEvents( RtsEvent::Kind::ProductionComplete, player ) );

    // 대기열은 다섯 — 여섯째는 거절, 취소는 값을 돌려준다.
    for ( int32 index = 0; index < 5; ++index )
        SW_EXPECT_TRUE( scene._world.train( barracksId, hashed_string( "marine" ) ) == RtsCommandResult::Ok );
    SW_EXPECT_TRUE( scene._world.train( barracksId, hashed_string( "marine" ) ) == RtsCommandResult::QueueFull );
    SW_EXPECT_EQUAL( 650, pPlayer->_minerals );
    SW_EXPECT_TRUE( scene._world.cancelTrain( barracksId ) );
    SW_EXPECT_EQUAL( 700, pPlayer->_minerals );

    // 집결지 — 나온 해병은 그리로 간다.
    const float3 rally{ 30.5f, 0.0f, 12.5f };
    scene._world.setRallyPoint( barracksId, rally );
    scene.run( 19.0f );
    SW_EXPECT_EQUAL( 1, scene._world.countUnits( player, hashed_string( "marine" ), true ) );
    scene.run( 10.0f );
    RtsUnitId marineId{};
    scene._world.forEachUnit( [&]( const RtsUnit& unit )
    {
        if ( unit._pDef->_id == hashed_string( "marine" ) && marineId.isValid() == false )
            marineId = unit._id;
    } );
    SW_EXPECT_TRUE( scene.computeDistance( marineId, rally ) < 1.0f );

    // 테크 — 아카데미가 서면 질럿을 뽑을 수 있다.
    (void)scene.spawn( "academy", player, 30.5f, 20.5f );
    SW_EXPECT_TRUE( scene._world.hasConstructed( player, hashed_string( "academy" ) ) );
    SW_EXPECT_TRUE( scene._world.train( barracksId, hashed_string( "zealot" ) ) == RtsCommandResult::Ok );
}

SW_TEST_CASE( RealTimeStrategyTest, WorkersConstructBuildingsAndRefineriesYieldGas )
{
    RtsTestScene scene;
    SW_ASSERT_TRUE( scene.initialize() );
    const int32 player = scene._world.addPlayer( 0, 300, 0, float3{ 6.0f, 0.0f, 6.0f } );
    (void)scene.spawn( "base", player, 4.5f, 4.5f );
    const RtsUnitId mineralId = scene.spawn( "minerals", RtsWorld::kNoOwner, 12.5f, 12.5f );
    const RtsUnitId geyserId  = scene.spawn( "geyser", RtsWorld::kNoOwner, 4.5f, 14.5f );
    const RtsUnitId builderA  = scene.spawn( "worker", player, 10.5f, 4.5f );
    const RtsUnitId builderB  = scene.spawn( "worker", player, 10.5f, 6.5f );
    SW_ASSERT_TRUE( mineralId.isValid() && geyserId.isValid() );

    SW_EXPECT_TRUE( scene._world.issueBuild( builderA, hashed_string( "barracks" ), int2{ 11, 11 } ) == RtsCommandResult::InvalidPlacement ); // 광물 위
    SW_EXPECT_TRUE( scene._world.issueBuild( builderA, hashed_string( "barracks" ), int2{ 46, 46 } ) == RtsCommandResult::InvalidPlacement ); // 맵 밖으로
    SW_EXPECT_TRUE( scene._world.issueBuild( builderA, hashed_string( "marine" ), int2{ 20, 4 } ) == RtsCommandResult::CannotDo );
    SW_EXPECT_TRUE( scene._world.issueBuild( builderA, hashed_string( "refinery" ), int2{ 20, 4 } ) == RtsCommandResult::InvalidPlacement ); // 간헐천 아님
    SW_EXPECT_TRUE( scene._world.issueBuild( builderA, hashed_string( "barracks" ), int2{ 20, 4 } ) == RtsCommandResult::Ok );
    SW_EXPECT_TRUE( scene._world.issueBuild( builderB, hashed_string( "refinery" ), int2{ 4, 14 } ) == RtsCommandResult::Ok );
    SW_EXPECT_EQUAL( 1, scene._world.countPlanned( player, hashed_string( "barracks" ) ) );
    SW_EXPECT_EQUAL( 300, scene._world.findPlayer( player )->_minerals ); // 아직 안 냈다

    scene.run( 8.0f );
    SW_EXPECT_EQUAL( 75, scene._world.findPlayer( player )->_minerals ); // 자리에 닿아 짓기 시작하며 냈다
    SW_EXPECT_EQUAL( 1, scene._world.countUnits( player, hashed_string( "barracks" ), true ) );
    SW_EXPECT_EQUAL( 0, scene._world.countUnits( player, hashed_string( "barracks" ), false ) );
    SW_EXPECT_TRUE( scene._world.canPlaceBuilding( hashed_string( "refinery" ), int2{ 4, 14 } ) == false ); // 간헐천 하나에 정제소 하나

    scene.run( 45.0f );
    SW_EXPECT_TRUE( scene._world.hasConstructed( player, hashed_string( "barracks" ) ) );
    SW_EXPECT_TRUE( scene._world.hasConstructed( player, hashed_string( "refinery" ) ) );
    SW_EXPECT_EQUAL( 2, scene.countEvents( RtsEvent::Kind::ConstructionComplete, player ) );
    SW_EXPECT_TRUE( scene._world.findUnit( builderA )->isIdle() );
    RtsUnitId barracksId{};
    RtsUnitId refineryId{};
    scene._world.forEachUnit( [&]( const RtsUnit& unit )
    {
        if ( unit._pDef->_id == hashed_string( "barracks" ) )
            barracksId = unit._id;
        if ( unit._pDef->_id == hashed_string( "refinery" ) )
            refineryId = unit._id;
    } );
    SW_EXPECT_NEAR_EQUAL( 1000.0f, scene._world.findUnit( barracksId )->_hp, 1.0f ); // 체력도 함께 올랐다
    SW_EXPECT_TRUE( scene._world.findUnit( refineryId )->_linkedResource == geyserId );

    // 정제소에서 가스를 캔다(간헐천은 캐지 못한다).
    SW_EXPECT_TRUE( scene._world.issueGather( builderB, geyserId ) == RtsCommandResult::CannotDo );
    SW_EXPECT_TRUE( scene._world.issueSmart( builderB, scene._world.findUnit( refineryId )->_position, refineryId ) == RtsCommandResult::Ok );
    scene.run( 20.0f );
    SW_EXPECT_TRUE( scene._world.findPlayer( player )->_gas >= 10 );
    SW_EXPECT_TRUE( scene._world.findUnit( geyserId )->_resourceLeft < 500 );

    // 돈이 모자라면 명령부터 거절한다.
    SW_EXPECT_TRUE( scene._world.issueBuild( builderA, hashed_string( "base" ), int2{ 30, 30 } ) == RtsCommandResult::NotEnoughMinerals );
}

SW_TEST_CASE( RealTimeStrategyTest, CombatAppliesArmorMinimumDamageAirTargetsAndAttackMove )
{
    RtsTestScene scene;
    SW_ASSERT_TRUE( scene.initialize() );
    const int32 blue = scene._world.addPlayer( 0, 0, 0, float3{ 5.0f, 0.0f, 5.0f } );
    const int32 red  = scene._world.addPlayer( 1, 0, 0, float3{ 40.0f, 0.0f, 40.0f } );
    SW_EXPECT_TRUE( scene._world.areEnemies( blue, red ) );

    // 방어 — 일꾼(5) 이 본진(방어 1)을 치면 4, 벽(방어 10)은 최소 0.5.
    const RtsUnitId redBase = scene.spawn( "base", red, 30.5f, 30.5f );
    const RtsUnitId wallId  = scene.spawn( "wall", red, 30.5f, 40.5f );
    const RtsUnitId workerA = scene.spawn( "worker", blue, 28.5f, 29.5f );
    const RtsUnitId workerB = scene.spawn( "worker", blue, 29.5f, 39.5f );
    SW_EXPECT_TRUE( scene._world.issueAttack( workerA, redBase ) == RtsCommandResult::Ok );
    SW_EXPECT_TRUE( scene._world.issueSmart( workerB, float3{ 31.0f, 0.0f, 41.0f }, wallId ) == RtsCommandResult::Ok ); // 적을 오른쪽 클릭 = 공격
    scene.run( 3.0f );
    const float32 baseLoss = 1500.0f - scene._world.findUnit( redBase )->_hp;
    const float32 wallLoss = 300.0f - scene._world.findUnit( wallId )->_hp;
    SW_EXPECT_TRUE( baseLoss >= 4.0f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, MathUtil::abs( baseLoss / 4.0f - static_cast<float32>( static_cast<int32>( baseLoss / 4.0f + 0.5f ) ) ), 1.0e-3f );
    SW_EXPECT_TRUE( wallLoss >= 0.5f && wallLoss <= 1.5f );
    SW_EXPECT_EQUAL( 1, scene.countEvents( RtsEvent::Kind::UnderAttack, red ) ); // 간격 안에서는 한 번만
    (void)scene._world.issueStop( workerA );
    (void)scene._world.issueStop( workerB );

    // 공중 — 질럿은 레이스를 치지 못하고, 대공 포탑은 레이스만 친다.
    const RtsUnitId zealotId = scene.spawn( "zealot", red, 10.5f, 30.5f );
    const RtsUnitId wraithId = scene.spawn( "wraith", blue, 12.5f, 30.5f );
    SW_EXPECT_TRUE( scene._world.issueAttack( zealotId, wraithId ) == RtsCommandResult::CannotDo );
    const RtsUnitId turretId = scene.spawn( "turret", red, 14.5f, 34.5f );
    SW_ASSERT_TRUE( turretId.isValid() );
    scene.run( 3.0f );
    SW_EXPECT_TRUE( scene._world.findUnit( wraithId )->_hp < 120.0f );
    SW_EXPECT_TRUE( scene._world.findUnit( turretId )->_attackTarget == wraithId );
    (void)scene._world.issueMove( wraithId, float3{ 2.0f, 0.0f, 46.0f } );
    scene.run( 6.0f );
    SW_EXPECT_TRUE( scene._world.findUnit( wraithId ) != nullptr );

    // 공격 이동 — 해병 셋이 가는 길의 질럿을 잡고 계속 간다.
    vector<RtsUnitId> listMarine;
    for ( int32 index = 0; index < 3; ++index )
        listMarine.push_back( scene.spawn( "marine", blue, 3.5f, 28.5f + static_cast<float32>( index ) ) );
    const float3 goal{ 20.5f, 0.0f, 22.5f };
    SW_EXPECT_EQUAL( 3, scene._world.issueGroupMove( listMarine, goal, true ) );
    scene.run( 25.0f );
    SW_EXPECT_NULL( scene._world.findUnit( zealotId ) );
    int32 aliveMarine = 0;
    for ( const RtsUnitId marineId : listMarine )
        aliveMarine += scene._world.findUnit( marineId ) != nullptr ? 1 : 0;
    SW_EXPECT_TRUE( aliveMarine >= 2 );
    bool bKilledByMarine = false;
    for ( const RtsEvent& event : scene._listEvent )
    {
        if ( event._kind == RtsEvent::Kind::UnitDied && event._unit == zealotId )
            bKilledByMarine = std::find( listMarine.begin(), listMarine.end(), event._other ) != listMarine.end();
    }
    SW_EXPECT_TRUE( bKilledByMarine );
    for ( const RtsUnitId marineId : listMarine )
    {
        if ( scene._world.findUnit( marineId ) != nullptr )
            SW_EXPECT_TRUE( scene.computeDistance( marineId, goal ) < 3.5f );
    }
}

SW_TEST_CASE( RealTimeStrategyTest, FogOfWarTracksVisibleAndExploredCellsPerTeam )
{
    RtsTestScene scene;
    SW_ASSERT_TRUE( scene.initialize() );
    const int32     blue     = scene._world.addPlayer( 0, 0, 0, float3{} );
    const int32     ally     = scene._world.addPlayer( 0, 0, 0, float3{} );
    const int32     red      = scene._world.addPlayer( 1, 0, 0, float3{} );
    const RtsUnitId marineId = scene.spawn( "marine", blue, 5.5f, 5.5f );
    const RtsUnitId enemyId  = scene.spawn( "marine", red, 40.5f, 40.5f );
    scene.run( 0.3f );
    SW_EXPECT_TRUE( scene._world.getVisibility( blue, int2{ 5, 5 } ) == RtsVisibility::Visible );
    SW_EXPECT_TRUE( scene._world.getVisibility( ally, int2{ 8, 8 } ) == RtsVisibility::Visible ); // 같은 팀은 시야를 나눈다
    SW_EXPECT_TRUE( scene._world.getVisibility( blue, int2{ 30, 30 } ) == RtsVisibility::Unexplored );
    SW_EXPECT_TRUE( scene._world.getVisibility( red, int2{ 5, 5 } ) == RtsVisibility::Unexplored );
    SW_EXPECT_TRUE( scene._world.isVisibleTo( blue, marineId ) );
    SW_EXPECT_TRUE( scene._world.isVisibleTo( blue, enemyId ) == false );

    SW_EXPECT_TRUE( scene._world.issueMove( marineId, float3{ 34.5f, 0.0f, 34.5f } ) == RtsCommandResult::Ok );
    scene.run( 20.0f );
    SW_EXPECT_TRUE( scene._world.getVisibility( blue, int2{ 2, 2 } ) == RtsVisibility::Explored );
    SW_EXPECT_TRUE( scene._world.isVisibleTo( blue, enemyId ) );
    SW_EXPECT_TRUE( scene._world.isVisibleTo( red, marineId ) );
}

SW_TEST_CASE( RealTimeStrategyTest, LargeGroupsShareOneFlowFieldAroundWalls )
{
    RtsTestScene scene;
    SW_ASSERT_TRUE( scene.initialize() );
    for ( int32 y = 0; y < 34; ++y )
        scene._world.setTerrainBlocked( 20, y, true ); // 위쪽 끝만 열린 벽
    const int32       player = scene._world.addPlayer( 0, 0, 0, float3{} );
    vector<RtsUnitId> listMarine;
    for ( int32 index = 0; index < 8; ++index )
        listMarine.push_back( scene.spawn( "marine", player, 8.5f + static_cast<float32>( index % 4 ), 9.5f + static_cast<float32>( index / 4 ) ) );
    const float3 goal{ 30.5f, 0.0f, 10.5f };
    SW_EXPECT_EQUAL( 8, scene._world.issueGroupMove( listMarine, goal, false ) );
    const FlowField* pShared = scene._world.findUnit( listMarine[0] )->findOrder()->_pFlowField;
    SW_ASSERT_NOT_NULL( pShared );
    for ( const RtsUnitId marineId : listMarine )
        SW_EXPECT_TRUE( scene._world.findUnit( marineId )->findOrder()->_pFlowField == pShared );

    scene.run( 50.0f );
    for ( const RtsUnitId marineId : listMarine )
    {
        SW_EXPECT_TRUE( scene.computeDistance( marineId, goal ) < 3.5f );
        SW_EXPECT_TRUE( scene._world.findUnit( marineId )->isIdle() );
    }

    // 둘은 A* 로(무리가 작다).
    vector<RtsUnitId> listPair{ listMarine[0], listMarine[1] };
    (void)scene._world.issueGroupMove( listPair, float3{ 8.5f, 0.0f, 8.5f }, false );
    SW_EXPECT_NULL( scene._world.findUnit( listMarine[0] )->findOrder()->_pFlowField );
}

SW_TEST_CASE( RealTimeStrategyTest, SelectionPrefersOwnUnitsAndControlGroupsPruneTheDead )
{
    RtsTestScene scene;
    SW_ASSERT_TRUE( scene.initialize() );
    const int32     blue    = scene._world.addPlayer( 0, 0, 0, float3{} );
    const int32     red     = scene._world.addPlayer( 1, 0, 0, float3{} );
    const RtsUnitId baseId  = scene.spawn( "base", blue, 4.5f, 4.5f );
    const RtsUnitId marineA = scene.spawn( "marine", blue, 10.5f, 5.5f );
    const RtsUnitId marineB = scene.spawn( "marine", blue, 11.5f, 5.5f );
    const RtsUnitId workerA = scene.spawn( "worker", blue, 12.5f, 5.5f );
    const RtsUnitId enemyId = scene.spawn( "base", red, 30.5f, 30.5f );
    scene.run( 0.3f );

    RtsSelection selection;
    selection.setPlayer( blue );
    selection.selectInRect( scene._world, float3{ 0.0f, 0.0f, 0.0f }, float3{ 14.0f, 0.0f, 8.0f }, false );
    SW_EXPECT_EQUAL( 3, static_cast<int32>( selection.getSelected().size() ) ); // 유닛만 — 본진은 빠진다
    SW_EXPECT_TRUE( selection.isSelected( baseId ) == false );
    SW_EXPECT_TRUE( selection.isCommandable( scene._world ) );

    selection.selectInRect( scene._world, float3{ 3.0f, 0.0f, 3.0f }, float3{ 6.0f, 0.0f, 6.0f }, false );
    SW_EXPECT_TRUE( selection.getPrimary() == baseId );

    selection.selectSameType( scene._world, marineA, float3{ 0.0f, 0.0f, 0.0f }, float3{ 20.0f, 0.0f, 20.0f } );
    SW_EXPECT_EQUAL( 2, static_cast<int32>( selection.getSelected().size() ) );
    selection.selectUnit( scene._world, workerA, true );
    SW_EXPECT_EQUAL( 3, static_cast<int32>( selection.getSelected().size() ) );
    selection.selectUnit( scene._world, marineB, true );
    SW_EXPECT_EQUAL( 2, static_cast<int32>( selection.getSelected().size() ) );
    selection.assignGroup( 1 );
    selection.selectUnit( scene._world, marineB, false );
    selection.addToGroup( 1 );
    SW_EXPECT_EQUAL( 3, static_cast<int32>( selection.getGroup( 1 ).size() ) );

    // 남의 것은 보일 때만, 고르면 명령할 수 없다.
    selection.selectInRect( scene._world, float3{ 29.0f, 0.0f, 29.0f }, float3{ 35.0f, 0.0f, 35.0f }, false );
    SW_EXPECT_TRUE( selection.getPrimary() == marineB ); // 안 보인다 — 그대로
    (void)scene._world.issueMove( marineA, float3{ 24.5f, 0.0f, 24.5f } );
    scene.run( 12.0f );
    selection.selectInRect( scene._world, float3{ 29.0f, 0.0f, 29.0f }, float3{ 35.0f, 0.0f, 35.0f }, false );
    SW_EXPECT_TRUE( selection.getPrimary() == enemyId );
    SW_EXPECT_TRUE( selection.isCommandable( scene._world ) == false );

    // 부대의 해병이 죽으면 부대에서 빠진다.
    for ( int32 index = 0; index < 3; ++index )
        (void)scene.spawn( "marine", red, 11.5f + static_cast<float32>( index ), 9.5f );
    scene.run( 10.0f );
    SW_EXPECT_NULL( scene._world.findUnit( marineB ) );
    int32 aliveCount = 0;
    for ( const RtsUnitId unitId : { marineA, workerA, marineB } )
        aliveCount += scene._world.findUnit( unitId ) != nullptr ? 1 : 0;
    SW_EXPECT_TRUE( selection.recallGroup( scene._world, 1 ) );
    SW_EXPECT_EQUAL( aliveCount, static_cast<int32>( selection.getSelected().size() ) );
    SW_EXPECT_TRUE( selection.recallGroup( scene._world, 5 ) == false );
}

SW_TEST_CASE( RealTimeStrategyTest, AiGrowsEconomyBuildsArmyAndWinsByRazingBuildings )
{
    RtsTestScene scene;
    SW_ASSERT_TRUE( scene.initialize( 64, 64 ) );
    const int32     human     = scene._world.addPlayer( 0, 0, 0, float3{ 8.0f, 0.0f, 8.0f } );
    const int32     cpu       = scene._world.addPlayer( 1, 50, 0, float3{ 44.0f, 0.0f, 44.0f } );
    const RtsUnitId humanBase = scene.spawn( "base", human, 6.5f, 6.5f );
    (void)scene.spawn( "base", cpu, 42.5f, 42.5f );
    for ( int32 index = 0; index < 8; ++index )
        (void)scene.spawn( "minerals", RtsWorld::kNoOwner, 40.5f + static_cast<float32>( index ), 52.5f );
    for ( int32 index = 0; index < 4; ++index )
        (void)scene.spawn( "worker", cpu, 41.5f + static_cast<float32>( index ), 49.5f );

    RtsAiSettings aiSettings;
    aiSettings._workerId         = hashed_string( "worker" );
    aiSettings._depotId          = hashed_string( "base" );
    aiSettings._supplyId         = hashed_string( "depot" );
    aiSettings._productionId     = hashed_string( "barracks" );
    aiSettings._armyUnitId       = hashed_string( "marine" );
    aiSettings._workerTarget     = 8;
    aiSettings._productionTarget = 1;
    aiSettings._attackArmySize   = 3;
    RtsAiController ai;
    ai.initialize( &scene._world, cpu, aiSettings );

    bool bDefeatedSeen = false;
    for ( float32 time = 0.0f; time < 900.0f && scene._world.getWinningTeam() < 0; time += 0.1f )
    {
        scene._world.update( 0.1f );
        ai.update( 0.1f );
        vector<RtsEvent> listEvent;
        scene._world.drainEvents( listEvent );
        for ( const RtsEvent& event : listEvent )
        {
            ai.notify( event );
            bDefeatedSeen = bDefeatedSeen || ( event._kind == RtsEvent::Kind::PlayerDefeated && event._player == human );
        }
        scene._listEvent.insert( scene._listEvent.end(), listEvent.begin(), listEvent.end() );
    }
    SW_EXPECT_TRUE( scene._world.countUnits( cpu, hashed_string( "worker" ), true ) >= 8 );
    SW_EXPECT_TRUE( scene._world.hasConstructed( cpu, hashed_string( "barracks" ) ) );
    SW_EXPECT_TRUE( scene._world.hasConstructed( cpu, hashed_string( "depot" ) ) );
    SW_EXPECT_TRUE( ai.getAttackWaveCount() >= 1 );
    SW_EXPECT_NULL( scene._world.findUnit( humanBase ) );
    SW_EXPECT_TRUE( bDefeatedSeen );
    SW_EXPECT_EQUAL( 1, scene._world.getWinningTeam() );
    SW_EXPECT_EQUAL( 1, scene.countEvents( RtsEvent::Kind::GameOver ) );
    SW_EXPECT_TRUE( scene._world.findPlayer( human )->_bDefeated != SW_FALSE );
}

/**
 * @brief [RealTimeStrategyTest] 판의 상태를 쓰고 같은 카탈로그 · 크기로 시작한 새 월드에 읽으면 같은 판이 이어진다
 * @details 핫 리로드 · 세이브가 디렉터의 판을 이 바이트로 옮긴다. 길(경로 · 흐름장)은 싣지 않으므로 읽은 쪽은 앞 명령의 길을 다시 구한다 —
 *          그래서 움직이던 무리가 "멈춰 있다 = 다 왔다" 로 명령을 끝내지 않고 목표까지 가야 하고, 흐름장을 쓰던 무리는 하나를 다시 나눠 쓴다.
 *          건물 발자국은 격자에 다시 칠해지고, 일꾼은 계속 캔다. 고름 · 부대도 같은 id 로 돌아온다.
 */
SW_TEST_CASE( RealTimeStrategyTest, StateRoundTripContinuesTheSameMatch )
{
    RtsTestScene original;
    SW_ASSERT_TRUE( original.initialize() );
    RtsWorld& world = original._world;
    for ( int32 y = 0; y < 34; ++y )
        world.setTerrainBlocked( 20, y, true ); // 위쪽 끝만 열린 벽
    const int32     player = world.addPlayer( 0, 0, 0, float3{ 6.0f, 0.0f, 6.0f } );
    const RtsUnitId baseId = original.spawn( "base", player, 4.5f, 4.5f );
    const RtsUnitId oreId  = original.spawn( "minerals", RtsWorld::kNoOwner, 6.5f, 12.5f );
    const RtsUnitId worker = original.spawn( "worker", player, 10.5f, 10.5f );
    SW_ASSERT_TRUE( baseId.isValid() && oreId.isValid() && worker.isValid() );
    SW_ASSERT_TRUE( world.issueGather( worker, oreId ) == RtsCommandResult::Ok );
    vector<RtsUnitId> listMarine;
    for ( int32 index = 0; index < 8; ++index )
        listMarine.push_back( original.spawn( "marine", player, 8.5f + static_cast<float32>( index % 4 ), 16.5f + static_cast<float32>( index / 4 ) ) );
    const float3 goal{ 30.5f, 0.0f, 10.5f };
    SW_ASSERT_EQUAL( 8, world.issueGroupMove( listMarine, goal, false ) );
    RtsSelection selection;
    selection.setPlayer( player );
    selection.selectUnit( world, listMarine[2], false );
    selection.assignGroup( 3 );
    original.run( 3.0f );
    SW_ASSERT_TRUE( original.computeDistance( listMarine[0], goal ) > 10.0f ); // 아직 가는 중
    const int32 mineralsAtSave = world.findPlayer( player )->_minerals;

    Archive written;
    world.writeState( written );
    selection.writeState( written );

    RtsTestScene restored;
    SW_ASSERT_TRUE( restored.initialize() );
    RtsSelection restoredSelection;
    Archive      reader( written.getData(), written.getSize() );
    SW_ASSERT_TRUE( restored._world.readState( reader ) );
    SW_ASSERT_TRUE( restoredSelection.readState( reader ) );
    SW_EXPECT_EQUAL( uint64( 0 ), reader.getRemainingBytes() );
    RtsWorld& restoredWorld = restored._world;

    BLOCK( "같은 id · 자리 · 자원 · 격자 · 고름이 돌아온다" )
    {
        Archive rewritten;
        restoredWorld.writeState( rewritten );
        Archive worldOnly;
        world.writeState( worldOnly );
        SW_ASSERT_EQUAL( worldOnly.getSize(), rewritten.getSize() );
        SW_EXPECT_TRUE( Memory::compare( worldOnly.getData(), rewritten.getData(), worldOnly.getSize() ) == 0 );
        SW_EXPECT_EQUAL( mineralsAtSave, restoredWorld.findPlayer( player )->_minerals );
        SW_EXPECT_TRUE( restoredWorld.getGrid().isWalkable( 5, 5 ) == false );   // 본진 발자국
        SW_EXPECT_TRUE( restoredWorld.getGrid().isWalkable( 20, 10 ) == false ); // 땅
        SW_EXPECT_TRUE( restoredWorld.getGrid().isWalkable( 12, 12 ) );
        SW_ASSERT_EQUAL( size_t( 1 ), restoredSelection.getSelected().size() );
        SW_EXPECT_TRUE( restoredSelection.getSelected()[0] == listMarine[2] );
        SW_EXPECT_TRUE( restoredSelection.getGroup( 3 ).size() == 1 );
        const FlowField* pShared = restoredWorld.findUnit( listMarine[0] )->findOrder()->_pFlowField;
        SW_ASSERT_NOT_NULL( pShared );
        for ( const RtsUnitId marineId : listMarine )
            SW_EXPECT_TRUE( restoredWorld.findUnit( marineId )->findOrder()->_pFlowField == pShared );
    }

    BLOCK( "움직이던 무리는 목표까지 가고 일꾼은 계속 캔다" )
    {
        restored.run( 0.2f );
        for ( const RtsUnitId marineId : listMarine )
            SW_EXPECT_FALSE( restoredWorld.findUnit( marineId )->isIdle() ); // 멈춰 있다고 명령을 끝내지 않는다
        restored.run( 50.0f );
        for ( const RtsUnitId marineId : listMarine )
        {
            SW_EXPECT_TRUE( restored.computeDistance( marineId, goal ) < 3.5f );
            SW_EXPECT_TRUE( restoredWorld.findUnit( marineId )->isIdle() );
        }
        SW_EXPECT_TRUE( restoredWorld.findPlayer( player )->_minerals > mineralsAtSave + 20 );
    }

    BLOCK( "크기가 다른 월드 · 잘린 바이트는 거절하고 그대로 둔다" )
    {
        RtsTestScene smallScene;
        SW_ASSERT_TRUE( smallScene.initialize( 16, 16 ) );
        Archive smallReader( written.getData(), written.getSize() );
        SW_EXPECT_FALSE( smallScene._world.readState( smallReader ) );
        SW_EXPECT_EQUAL( 0, smallScene._world.getPlayerCount() );

        RtsTestScene cutScene;
        SW_ASSERT_TRUE( cutScene.initialize() );
        Archive cut( written.getData(), 200 );
        SW_EXPECT_FALSE( cutScene._world.readState( cut ) );
        SW_EXPECT_EQUAL( 0, cutScene._world.getPlayerCount() );
    }
}
