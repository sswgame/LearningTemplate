// RTSCommanderAITest — 지휘관 AI: 생각 빈도, 공격 알림 거르기, 작업 일곱의 조건과 내리는 명령, 상태 바이트. 작업 하나만 잎으로 둔 트리로 돌려 작업의 규칙만 본다. 키트 GF_RealTimeStrategy(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Behavior/ActionRegistry.h"
#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/RTSCatalog.h"
#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/RTSCommanderAI.h"
#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/RTSWorld.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Shop.h"

#include <limits>

using namespace sw;

namespace
{
    constexpr const utf8* kCommanderUnitXML = R"(<RTSCatalog supplyMax="200">
  <Unit id="minerals" name="minerals" kind="Resource" resource="Minerals" amount="1500" footprint="1"/>
  <Unit id="command_center" name="command_center" kind="Building" hp="1500" armor="1" footprint="4" depot="true" provides="10" producedBy="worker" minerals="400" buildTime="75" sight="10"/>
  <Unit id="supply_depot" name="supply_depot" kind="Building" hp="500" armor="1" footprint="2" provides="8" producedBy="worker" minerals="100" buildTime="25" sight="6"/>
  <Unit id="barracks" name="barracks" kind="Building" hp="1000" armor="1" footprint="3" producedBy="worker" minerals="150" buildTime="40" sight="6"/>
  <Unit id="worker" name="worker" hp="60" speed="2.8" radius="0.35" damage="5" range="0.2" cooldown="1.1" worker="true" producedBy="command_center" minerals="50" supply="1" buildTime="13" gatherTime="2.8" cargo="8" sight="7"/>
  <Unit id="marine" name="marine" hp="40" speed="2.25" radius="0.35" damage="6" range="4" cooldown="0.86" targets="Ground,Air" producedBy="barracks" minerals="50" supply="1" buildTime="18" sight="7"/>
</RTSCatalog>)";

    /** @brief 플레이어 넷을 꾸린 빈 판과, 작업 하나로 된 트리를 도는 AI 입니다. 플레이어 0 이 AI 이고 1 은 같은 팀, 2 · 3 은 적입니다. */
    struct CommanderFixture
    {
        static inline const hashed_string kMinerals{ "minerals" };
        static inline const hashed_string kDepot{ "command_center" };
        static inline const hashed_string kSupply{ "supply_depot" };
        static inline const hashed_string kBarracks{ "barracks" };
        static inline const hashed_string kWorker{ "worker" };
        static inline const hashed_string kMarine{ "marine" };
        static inline const hashed_string kThreat{ "Threat" };
        static inline int32               s_thinkCount = 0;

        RTSCatalog             _catalog;
        RTSWorld               _world;
        Wallet                 _wallet;
        RTSCommanderAI         _ai;
        RTSCommanderAISettings _settings;

        /** @brief 생각한 횟수를 세는 작업입니다. */
        static BehaviorStatus taskCountThink( BehaviorContext& )
        {
            ++s_thinkCount;
            return BehaviorStatus::Success;
        }

        [[nodiscard]] bool setUp()
        {
            s_thinkCount = 0;
            if ( _catalog.loadFromXMLText( kCommanderUnitXML, "RTSCommanderAITest" ) == false || _world.initialize( &_catalog, 48, 48, RTSSettings{} ) == false )
                return false;
            _wallet.add( _world.getSettings()._mineralCurrency, 1000 );
            (void)_world.addPlayer( 0, &_wallet, float3{ 6.0f, 0.0f, 6.0f } );
            (void)_world.addPlayer( 0, nullptr, float3{ 8.0f, 0.0f, 8.0f } );   // 같은 팀 - 가장 가깝지만 적이 아니다
            (void)_world.addPlayer( 1, nullptr, float3{ 25.0f, 0.0f, 25.0f } ); // 가까운 적
            (void)_world.addPlayer( 1, nullptr, float3{ 42.0f, 0.0f, 42.0f } ); // 먼 적
            _settings._workerID     = kWorker;
            _settings._depotID      = kDepot;
            _settings._supplyID     = kSupply;
            _settings._productionID = kBarracks;
            _settings._armyUnitID   = kMarine;
            return true;
        }

        /** @brief 테스트가 만든 작업 @p function 하나로 된 트리로 AI 를 시작합니다(생각 횟수를 세는 작업 등). */
        void start( BehaviorTaskFunction function, int32 player = 0 )
        {
            BehaviorTree tree;
            const int32  root = tree.addSequence( nullopt, "Root" ).value();
            (void)tree.addAction( root, "Task", function );
            _ai.initialize( &_world, player, _settings, tree );
        }

        /** @brief 등록된 동작 @p pAction(`rts.defend` 등) 하나로 된 트리로 AI 를 시작합니다. 설정(`_settings`)을 고친 뒤에 부릅니다. */
        void start( const utf8* pAction, int32 player = 0 )
        {
            GameActionCall call;
            SW_REQUIRE_TRUE( GameActionCall::compileText( pAction, "RTSCommanderAITest", call ) );
            BehaviorTree tree;
            const int32  root = tree.addSequence( nullopt, "Root" ).value();
            (void)tree.addAction( root, "Task", call );
            _ai.initialize( &_world, player, _settings, tree );
        }

        /** @brief 생각 간격 하나를 흘립니다. 한 번 생각합니다(첫 번째는 시작하자마자, 그 뒤는 간격이 찰 때). */
        void think() { _ai.update( _settings._thinkInterval ); }

        RTSUnitID spawnDepot( int32 owner = 0 ) { return _world.spawnUnit( kDepot, owner, float3{ 4.5f, 0.0f, 4.5f } ); }
        RTSUnitID spawnMinerals() { return _world.spawnUnit( kMinerals, RTSWorld::kNoOwner, float3{ 14.5f, 0.0f, 4.5f } ); }
        RTSUnitID spawnWorker( float32 z = 12.5f ) { return _world.spawnUnit( kWorker, 0, float3{ 10.5f, 0.0f, z } ); }
        RTSUnitID spawnMarine( float32 x = 12.5f ) { return _world.spawnUnit( kMarine, 0, float3{ x, 0.0f, 14.5f } ); }

        const RTSOrder* findOrder( RTSUnitID unitID ) const
        {
            const RTSUnit* pUnit = _world.findUnit( unitID );
            return pUnit != nullptr ? pUnit->findOrder() : nullptr;
        }
        size_t countProduction( RTSUnitID unitID ) const
        {
            const RTSUnit* pUnit = _world.findUnit( unitID );
            return pUnit != nullptr ? pUnit->_listProduction.size() : 0;
        }

        RTSEvent makeAttackEvent( const float3& position, int32 player = 0 ) const
        {
            RTSEvent event;
            event._kind     = RTSEvent::Kind::UnderAttack;
            event._player   = player;
            event._position = position;
            return event;
        }
    };
    using Rig = CommanderFixture;

    struct RTSCommanderAITestUtil
    {
        /** @brief `writeState` 와 같은 모양의 바이트를 @p inoutAI 로 읽습니다. */
        static bool readForged( RTSCommanderAI& inoutAI, float32 thinkTimer, int32 attackWaveCount )
        {
            Archive written;
            written << thinkTimer;
            written << attackWaveCount;
            Archive reading( written.getData(), written.getSize() );
            return inoutAI.readState( reading );
        }
    };
} // namespace

/**
 * @brief [RTSCommanderAITest] 월드가 없거나 없는 플레이어를 맡은 AI 는 생각하지 않는다
 */
SW_TEST_CASE( RTSCommanderAITest, DoesNotThinkWithoutWorldOrPlayer )
{
    RTSCommanderAI idle;
    idle.update( 1.0f ); // 시작하지 않아도 죽지 않는다
    SW_EXPECT_EQUAL( -1, idle.getPlayer() );

    Rig rig;
    SW_REQUIRE_TRUE( rig.setUp() );
    rig.start( &Rig::taskCountThink, 9 );
    rig.think();
    SW_EXPECT_EQUAL( 0, Rig::s_thinkCount );
}

/**
 * @brief [RTSCommanderAITest] 시작하자마자 한 번 생각하고, 간격이 차기 전에는 다시 생각하지 않는다
 */
SW_TEST_CASE( RTSCommanderAITest, ThinksAtOnceThenWaitsForTheInterval )
{
    Rig rig;
    SW_REQUIRE_TRUE( rig.setUp() );
    rig._settings._thinkInterval = 0.5f;
    rig.start( &Rig::taskCountThink );
    rig._ai.update( 0.1f );
    SW_EXPECT_EQUAL( 1, Rig::s_thinkCount );
    rig._ai.update( 0.1f );
    rig._ai.update( 0.1f );
    SW_EXPECT_EQUAL( 1, Rig::s_thinkCount );
    rig._ai.update( 0.4f );
    SW_EXPECT_EQUAL( 2, Rig::s_thinkCount );
}

/**
 * @brief [RTSCommanderAITest] 결함: 간격이 찰 때 지나친 시간을 버려 생각 빈도가 프레임률에 매였다(0.2 초 프레임에 0.5 초 간격이면 0.6 초마다). 늦음을 다음 간격으로 넘겨 긴 시간의 빈도가 설계값이다
 */
SW_TEST_CASE( RTSCommanderAITest, ThinkRateDoesNotDependOnFrameTime )
{
    Rig rig;
    SW_REQUIRE_TRUE( rig.setUp() );
    rig._settings._thinkInterval = 0.5f;
    rig.start( &Rig::taskCountThink );
    for ( int32 frame = 0; frame < 50; ++frame ) // 10 초
    {
        rig._ai.update( 0.2f );
    }
    SW_EXPECT_TRUE( Rig::s_thinkCount >= 20 );
    SW_EXPECT_TRUE( Rig::s_thinkCount <= 21 );
}

/**
 * @brief [RTSCommanderAITest] 공격 알림은 내 플레이어의 "공격받음" 이고 본진 반경 안일 때만 위협으로 적는다. 본진이 없으면 어디든 막으러 간다
 */
SW_TEST_CASE( RTSCommanderAITest, NotifyFiltersAttackEvents )
{
    Rig rig;
    SW_REQUIRE_TRUE( rig.setUp() );
    rig._settings._defendRadius = 10.0f;
    rig.start( &Rig::taskCountThink );

    rig._ai.notify( rig.makeAttackEvent( float3{ 40.0f, 0.0f, 40.0f } ) );
    SW_EXPECT_TRUE( rig._ai.getBlackboard().isSet( Rig::kThreat ) ); // 본진 없음
    rig.start( &Rig::taskCountThink );                               // 블랙보드를 비운다
    SW_EXPECT_FALSE( rig._ai.getBlackboard().isSet( Rig::kThreat ) );

    const RTSUnitID depotID = rig.spawnDepot();
    SW_REQUIRE_TRUE( depotID.isValid() );
    const float3 depot = rig._world.findUnit( depotID )->_position;

    rig._ai.notify( rig.makeAttackEvent( depot + float3{ 30.0f, 0.0f, 0.0f } ) ); // 반경 밖
    SW_EXPECT_FALSE( rig._ai.getBlackboard().isSet( Rig::kThreat ) );
    rig._ai.notify( rig.makeAttackEvent( depot + float3{ 2.0f, 0.0f, 0.0f }, 1 ) ); // 다른 플레이어
    SW_EXPECT_FALSE( rig._ai.getBlackboard().isSet( Rig::kThreat ) );
    RTSEvent otherKind = rig.makeAttackEvent( depot + float3{ 2.0f, 0.0f, 0.0f } );
    otherKind._kind    = RTSEvent::Kind::UnitDied;
    rig._ai.notify( otherKind );
    SW_EXPECT_FALSE( rig._ai.getBlackboard().isSet( Rig::kThreat ) );

    rig._ai.notify( rig.makeAttackEvent( depot + float3{ 2.0f, 0.0f, 0.0f } ) );
    const sw::optional<float3> threat = rig._ai.getBlackboard().getVector( Rig::kThreat );
    SW_REQUIRE_TRUE( threat.has_value() );
    SW_EXPECT_NEAR( depot._x + 2.0f, threat->_x, 1.0e-4f );
}

/**
 * @brief [RTSCommanderAITest] 막기: 위협이 없으면 아무것도 안 한다. 있으면 전투 유닛만 공격 이동으로 보내고(일꾼은 그대로) 위협을 지운다
 */
SW_TEST_CASE( RTSCommanderAITest, DefendSendsArmyAndClearsThreat )
{
    Rig rig;
    SW_REQUIRE_TRUE( rig.setUp() );
    const RTSUnitID marineID = rig.spawnMarine();
    const RTSUnitID workerID = rig.spawnWorker();
    rig.start( "rts.defend" );
    rig.think();
    SW_EXPECT_NULL( rig.findOrder( marineID ) );

    const float3 threat{ 20.0f, 0.0f, 20.0f };
    rig._ai.notify( rig.makeAttackEvent( threat ) );
    rig.think();
    const RTSOrder* pOrder = rig.findOrder( marineID );
    SW_REQUIRE_NOT_NULL( pOrder );
    SW_EXPECT_TRUE( pOrder->_type == RTSOrderType::AttackMove );
    SW_EXPECT_NEAR( threat._x, pOrder->_target._x, 1.0e-4f );
    SW_EXPECT_NULL( rig.findOrder( workerID ) );
    SW_EXPECT_FALSE( rig._ai.getBlackboard().isSet( Rig::kThreat ) );
}

/**
 * @brief [RTSCommanderAITest] 노는 일꾼 보내기: 내 본진 둘레의 광물로 일꾼만 보낸다. 내 본진이 없거나 광물이 없으면 보내지 않는다
 */
SW_TEST_CASE( RTSCommanderAITest, GatherIdleSendsOnlyWorkersWhenDepotAndMineralsExist )
{
    Rig rig;
    SW_REQUIRE_TRUE( rig.setUp() );
    const RTSUnitID workerID = rig.spawnWorker();
    const RTSUnitID marineID = rig.spawnMarine();
    rig.start( "rts.gatherIdle" );

    SW_REQUIRE_TRUE( rig.spawnDepot( 1 ).isValid() ); // 남의 본진은 내 본진이 아니다
    rig.think();
    SW_EXPECT_NULL( rig.findOrder( workerID ) );

    Rig noMinerals;
    SW_REQUIRE_TRUE( noMinerals.setUp() );
    const RTSUnitID loneWorkerID = noMinerals.spawnWorker();
    SW_REQUIRE_TRUE( noMinerals.spawnDepot().isValid() );
    noMinerals.start( "rts.gatherIdle" );
    noMinerals.think();
    SW_EXPECT_NULL( noMinerals.findOrder( loneWorkerID ) );

    Rig ready;
    SW_REQUIRE_TRUE( ready.setUp() );
    const RTSUnitID readyWorkerID = ready.spawnWorker();
    const RTSUnitID readyMarineID = ready.spawnMarine();
    SW_REQUIRE_TRUE( ready.spawnDepot().isValid() );
    const RTSUnitID mineralID = ready.spawnMinerals();
    SW_REQUIRE_TRUE( mineralID.isValid() );
    ready.start( "rts.gatherIdle" );
    ready.think();
    const RTSOrder* pOrder = ready.findOrder( readyWorkerID );
    SW_REQUIRE_NOT_NULL( pOrder );
    SW_EXPECT_TRUE( pOrder->_type == RTSOrderType::Gather );
    SW_EXPECT_TRUE( pOrder->_targetUnit == mineralID );
    SW_EXPECT_NULL( ready.findOrder( readyMarineID ) );
    SW_EXPECT_NULL( rig.findOrder( marineID ) );
}

/**
 * @brief [RTSCommanderAITest] 일꾼 만들기: 목표 수 아래이고 본진이 놀 때만 한 번에 하나 넣는다. 대기열이 찼거나 목표에 닿으면 넣지 않아 돈을 쓰지 않는다
 */
SW_TEST_CASE( RTSCommanderAITest, TrainWorkersStopsAtTargetAndOneAtATime )
{
    Rig rig;
    SW_REQUIRE_TRUE( rig.setUp() );
    rig._settings._workerTarget = 3;
    const RTSUnitID depotID     = rig.spawnDepot();
    SW_REQUIRE_TRUE( rig.spawnWorker().isValid() );
    rig.start( "rts.trainWorkers" );
    const int64 mineralsBefore = rig._world.getMinerals( 0 );

    rig.think();
    SW_EXPECT_EQUAL( size_t( 1 ), rig.countProduction( depotID ) );
    rig.think();
    SW_EXPECT_EQUAL( size_t( 1 ), rig.countProduction( depotID ) ); // 노는 본진만
    SW_EXPECT_EQUAL( mineralsBefore - 50, rig._world.getMinerals( 0 ) );

    Rig atTarget;
    SW_REQUIRE_TRUE( atTarget.setUp() );
    atTarget._settings._workerTarget = 1;
    const RTSUnitID fullDepotID      = atTarget.spawnDepot();
    SW_REQUIRE_TRUE( atTarget.spawnWorker().isValid() );
    atTarget.start( "rts.trainWorkers" );
    atTarget.think();
    SW_EXPECT_EQUAL( size_t( 0 ), atTarget.countProduction( fullDepotID ) );
}

/**
 * @brief [RTSCommanderAITest] 보급 건물: 남은 보급이 여유보다 많으면 짓지 않는다. 적으면 짓되 지으러 가는 동안 또 시키지 않고, 일꾼은 다 지은 뒤 다시 캐러 간다
 */
SW_TEST_CASE( RTSCommanderAITest, BuildSupplyWaitsForMarginAndDoesNotRepeat )
{
    Rig rig;
    SW_REQUIRE_TRUE( rig.setUp() );
    SW_REQUIRE_TRUE( rig.spawnDepot().isValid() );
    SW_REQUIRE_TRUE( rig.spawnMinerals().isValid() );
    const RTSUnitID workerID = rig.spawnWorker();
    const RTSUnitID spareID  = rig.spawnWorker( 14.5f );
    rig._world.update( 0.1f ); // 보급을 센다: 상한 10, 쓴 2

    rig._settings._supplyMargin = 3;
    rig.start( "rts.buildSupply" );
    rig.think();
    SW_EXPECT_NULL( rig.findOrder( workerID ) );
    SW_EXPECT_NULL( rig.findOrder( spareID ) );

    rig._settings._supplyMargin = 8;
    rig.start( "rts.buildSupply" );
    rig.think();
    const RTSOrder* pFirst  = rig.findOrder( workerID );
    const RTSOrder* pSecond = rig.findOrder( spareID );
    SW_REQUIRE_TRUE( ( pFirst != nullptr ) != ( pSecond != nullptr ) ); // 한 명만 간다
    const RTSOrder* pOrder = pFirst != nullptr ? pFirst : pSecond;
    SW_EXPECT_TRUE( pOrder->_type == RTSOrderType::Build );
    SW_EXPECT_TRUE( pOrder->_buildID == Rig::kSupply );
    const RTSUnit* pBuilder = rig._world.findUnit( pFirst != nullptr ? workerID : spareID );
    SW_REQUIRE_EQUAL( size_t( 2 ), pBuilder->_listOrder.size() );
    SW_EXPECT_TRUE( pBuilder->_listOrder.back()._type == RTSOrderType::Gather );

    rig.think();
    SW_EXPECT_TRUE( ( rig.findOrder( workerID ) != nullptr ) != ( rig.findOrder( spareID ) != nullptr ) ); // 또 시키지 않는다
}

/**
 * @brief [RTSCommanderAITest] 건설 일꾼 고르기: 노는 일꾼이 캐러 가는 일꾼보다 먼저이고, 다른 일을 하는 일꾼은 뽑지 않는다
 */
SW_TEST_CASE( RTSCommanderAITest, ConstructionPrefersIdleWorkerAndSkipsBusyOnes )
{
    Rig rig;
    SW_REQUIRE_TRUE( rig.setUp() );
    SW_REQUIRE_TRUE( rig.spawnDepot().isValid() );
    const RTSUnitID mineralID = rig.spawnMinerals();
    const RTSUnitID gatherID  = rig.spawnWorker();
    const RTSUnitID idleID    = rig.spawnWorker( 14.5f );
    SW_REQUIRE_TRUE( rig._world.issueGather( gatherID, mineralID ) == RTSCommandResult::Ok );
    rig._settings._workerTarget = 2;
    rig.start( "rts.buildProduction" );
    rig.think();
    SW_EXPECT_TRUE( rig.findOrder( gatherID )->_type == RTSOrderType::Gather );
    SW_REQUIRE_NOT_NULL( rig.findOrder( idleID ) );
    SW_EXPECT_TRUE( rig.findOrder( idleID )->_type == RTSOrderType::Build );

    Rig busy;
    SW_REQUIRE_TRUE( busy.setUp() );
    SW_REQUIRE_TRUE( busy.spawnDepot().isValid() );
    const RTSUnitID movingID = busy.spawnWorker();
    SW_REQUIRE_TRUE( busy._world.issueMove( movingID, float3{ 30.5f, 0.0f, 30.5f } ) == RTSCommandResult::Ok );
    busy._settings._workerTarget = 2;
    busy.start( "rts.buildProduction" );
    busy.think();
    SW_EXPECT_TRUE( busy.findOrder( movingID )->_type == RTSOrderType::Move );
    SW_EXPECT_EQUAL( 0, busy._world.countPlanned( 0, Rig::kBarracks ) );
}

/**
 * @brief [RTSCommanderAITest] 생산 건물: 목표 수에 닿았거나 일꾼이 목표의 절반에 못 미치면 짓지 않는다
 */
SW_TEST_CASE( RTSCommanderAITest, BuildProductionChecksTargetAndWorkerCount )
{
    Rig tooFewWorkers;
    SW_REQUIRE_TRUE( tooFewWorkers.setUp() );
    SW_REQUIRE_TRUE( tooFewWorkers.spawnDepot().isValid() );
    const RTSUnitID loneID                = tooFewWorkers.spawnWorker();
    tooFewWorkers._settings._workerTarget = 4; // 2 명은 있어야 시작한다
    tooFewWorkers.start( "rts.buildProduction" );
    tooFewWorkers.think();
    SW_EXPECT_NULL( tooFewWorkers.findOrder( loneID ) );

    Rig atTarget;
    SW_REQUIRE_TRUE( atTarget.setUp() );
    SW_REQUIRE_TRUE( atTarget.spawnDepot().isValid() );
    SW_REQUIRE_TRUE( atTarget._world.spawnUnit( Rig::kBarracks, 0, float3{ 20.5f, 0.0f, 20.5f } ).isValid() );
    const RTSUnitID idleID               = atTarget.spawnWorker();
    atTarget._settings._workerTarget     = 2;
    atTarget._settings._productionTarget = 1;
    atTarget.start( "rts.buildProduction" );
    atTarget.think();
    SW_EXPECT_NULL( atTarget.findOrder( idleID ) );

    atTarget._settings._productionTarget = 2;
    atTarget.start( "rts.buildProduction" );
    atTarget.think();
    SW_REQUIRE_NOT_NULL( atTarget.findOrder( idleID ) );
    SW_EXPECT_TRUE( atTarget.findOrder( idleID )->_buildID == Rig::kBarracks );
}

/**
 * @brief [RTSCommanderAITest] 병력 만들기: 노는 생산 건물마다 하나씩 넣고, 이미 만드는 건물에는 더 넣지 않는다
 */
SW_TEST_CASE( RTSCommanderAITest, TrainArmyFillsEachIdleProducerOnce )
{
    Rig rig;
    SW_REQUIRE_TRUE( rig.setUp() );
    const RTSUnitID firstID  = rig._world.spawnUnit( Rig::kBarracks, 0, float3{ 20.5f, 0.0f, 20.5f } );
    const RTSUnitID secondID = rig._world.spawnUnit( Rig::kBarracks, 0, float3{ 30.5f, 0.0f, 20.5f } );
    SW_REQUIRE_TRUE( firstID.isValid() && secondID.isValid() );
    rig.start( "rts.trainArmy" );
    rig.think();
    SW_EXPECT_EQUAL( size_t( 1 ), rig.countProduction( firstID ) );
    SW_EXPECT_EQUAL( size_t( 1 ), rig.countProduction( secondID ) );
    rig.think();
    SW_EXPECT_EQUAL( size_t( 1 ), rig.countProduction( firstID ) );
    SW_EXPECT_EQUAL( size_t( 1 ), rig.countProduction( secondID ) );
}

/**
 * @brief [RTSCommanderAITest] 공격: 노는 병력이 정한 수에 못 미치면 가지 않는다. 차면 가장 가까운 적의 시작 지점으로 공격 이동하고 횟수를 센다(같은 팀은 더 가까워도 고르지 않는다)
 */
SW_TEST_CASE( RTSCommanderAITest, AttackWaitsForArmyAndTargetsNearestEnemy )
{
    Rig rig;
    SW_REQUIRE_TRUE( rig.setUp() );
    rig._settings._attackArmySize = 2;
    const RTSUnitID firstID       = rig.spawnMarine( 12.5f );
    rig.start( "rts.attack" );
    rig.think();
    SW_EXPECT_EQUAL( 0, rig._ai.getAttackWaveCount() );
    SW_EXPECT_NULL( rig.findOrder( firstID ) );

    const RTSUnitID secondID = rig.spawnMarine( 14.5f );
    rig.think();
    SW_EXPECT_EQUAL( 1, rig._ai.getAttackWaveCount() );
    const RTSOrder* pOrder = rig.findOrder( secondID );
    SW_REQUIRE_NOT_NULL( pOrder );
    SW_EXPECT_TRUE( pOrder->_type == RTSOrderType::AttackMove );
    SW_EXPECT_NEAR( 25.0f, pOrder->_target._x, 1.0e-4f );
    SW_EXPECT_NEAR( 25.0f, pOrder->_target._z, 1.0e-4f );
    SW_EXPECT_NOT_NULL( rig.findOrder( firstID ) );

    rig.think(); // 이제 노는 병력이 없다
    SW_EXPECT_EQUAL( 1, rig._ai.getAttackWaveCount() );
}

/**
 * @brief [RTSCommanderAITest] 상태 바이트: 쓴 값을 그대로 읽는다 - 남은 생각 시간이 이어져 간격 안에서는 생각하지 않는다
 */
SW_TEST_CASE( RTSCommanderAITest, StateRoundTripKeepsTimerAndWaveCount )
{
    Rig rig;
    SW_REQUIRE_TRUE( rig.setUp() );
    rig.start( &Rig::taskCountThink );
    SW_REQUIRE_TRUE( RTSCommanderAITestUtil::readForged( rig._ai, 0.4f, 3 ) );
    SW_EXPECT_EQUAL( 3, rig._ai.getAttackWaveCount() );

    Archive written;
    rig._ai.writeState( written );
    Archive reading( written.getData(), written.getSize() );
    float32 thinkTimer      = 0.0f;
    int32   attackWaveCount = 0;
    reading >> thinkTimer;
    reading >> attackWaveCount;
    SW_EXPECT_NEAR( 0.4f, thinkTimer, 1.0e-6f );
    SW_EXPECT_EQUAL( 3, attackWaveCount );

    rig._ai.update( 0.1f );
    SW_EXPECT_EQUAL( 0, Rig::s_thinkCount );
    rig._ai.update( 0.4f );
    SW_EXPECT_EQUAL( 1, Rig::s_thinkCount );
}

/**
 * @brief [RTSCommanderAITest] 결함: 무한대 타이머를 받아 AI 가 영영 생각하지 않았다. 깨진 바이트(음수 횟수 · 무한대 타이머 · 잘린 데이터)를 거절하고 상태를 둔다
 */
SW_TEST_CASE( RTSCommanderAITest, ReadStateRefusesCorruptBytes )
{
    Rig rig;
    SW_REQUIRE_TRUE( rig.setUp() );
    rig.start( &Rig::taskCountThink );
    SW_REQUIRE_TRUE( RTSCommanderAITestUtil::readForged( rig._ai, 0.0f, 2 ) );

    SW_EXPECT_FALSE( RTSCommanderAITestUtil::readForged( rig._ai, 0.5f, -1 ) );
    SW_EXPECT_FALSE( RTSCommanderAITestUtil::readForged( rig._ai, std::numeric_limits<float32>::infinity(), 5 ) );
    SW_EXPECT_FALSE( RTSCommanderAITestUtil::readForged( rig._ai, std::numeric_limits<float32>::quiet_NaN(), 5 ) );

    Archive written;
    written << 0.5f; // 횟수가 없다
    Archive reading( written.getData(), written.getSize() );
    SW_EXPECT_FALSE( rig._ai.readState( reading ) );

    SW_EXPECT_EQUAL( 2, rig._ai.getAttackWaveCount() );
    rig.think();
    SW_EXPECT_EQUAL( 1, Rig::s_thinkCount ); // 타이머도 그대로라 바로 생각한다
}
