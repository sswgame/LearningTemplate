#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Runtime/Character/Combat/Health/HealthStateMachine.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Wallet.h"
#include "GameFramework/Runtime/Gameplay/Respawn/CheckpointState.h"
#include "GameFramework/Runtime/Gameplay/Respawn/RespawnFlow.h"
#include "GameFramework/Runtime/Gameplay/Respawn/RespawnRules.h"
#include "GameFramework/Runtime/Gameplay/Respawn/Tests/RespawnTestWorld.h"

// CheckpointStateTest — 켜기 규칙(처음 · 다시 쓰기 · 한 번뿐 · 앞으로만), 상태 바이트 왕복과 어긋난 바이트 거절.
// RespawnRulesTest — `<Respawn>` 읽기와 틀린 규칙 거절. RespawnFlowTest — 정책 고르기 차례 · 제자리(안전 지점 · 낭떠러지) · 대기 · 자리 준비 · 무적 · 벌칙 · 고르기 시한 · 동료 부활 · 상태 왕복.

namespace
{
    constexpr float32 kRespawnTolerance = 1.0e-4f;

    const sw::hashed_string kGold{ "gold" };

    struct RespawnFlowTestUtil
    {
        static sw::RespawnPolicyEntry makePolicy( sw::RespawnPolicyKind kind )
        {
            sw::RespawnPolicyEntry entry;
            entry._kind = kind;
            return entry;
        }

        static sw::CheckpointDef makeDef( const utf8* pID, int32 order, bool bReusable )
        {
            sw::CheckpointDef def;
            def._id        = sw::hashed_string( pID );
            def._order     = order;
            def._bReusable = bReusable;
            return def;
        }

        /** @brief 땅이 [0, 10] · [14, 30] 이고 그 사이가 낭떠러지인 세계입니다. */
        static sw::RespawnTestWorld makeWorldWithPit()
        {
            sw::RespawnTestWorld world;
            world._listGround.push_back( sw::RespawnTestGround{ 0.0f, 10.0f } );
            world._listGround.push_back( sw::RespawnTestGround{ 14.0f, 30.0f } );
            world._groundY = 1.0f;
            return world;
        }
    };
} // namespace

/**
 * @brief [CheckpointStateTest] 처음 닿으면 부활 지점이 되고, 다시 쓸 수 있는 것은 다시 닿으면 돌아오며, 한 번뿐인 것은 다른 곳으로 옮긴 뒤 다시 쓰지 못한다
 */
SW_TEST_CASE( CheckpointStateTest, ReusableAndSpentCheckpoints )
{
    sw::CheckpointState state;
    SW_EXPECT_TRUE( state.getCurrent() == nullptr );
    SW_EXPECT_TRUE( state.activate( RespawnFlowTestUtil::makeDef( "", 0, false ), sw::float3{}, 0.0f ) == sw::CheckpointActivation::Invalid );

    SW_EXPECT_TRUE( state.activate( RespawnFlowTestUtil::makeDef( "bonfire", 0, true ), sw::float3{ 1.0f, 0.0f, 0.0f }, 0.5f ) == sw::CheckpointActivation::Activated );
    SW_EXPECT_TRUE( state.activate( RespawnFlowTestUtil::makeDef( "bonfire", 0, true ), sw::float3{ 9.0f, 0.0f, 0.0f }, 0.0f ) == sw::CheckpointActivation::AlreadyCurrent );
    SW_EXPECT_TRUE( state.activate( RespawnFlowTestUtil::makeDef( "flag", 1, false ), sw::float3{ 5.0f, 0.0f, 0.0f }, 0.0f ) == sw::CheckpointActivation::Activated );
    SW_EXPECT_EQUAL( sw::hashed_string( "flag" ), state.getCurrent()->_id );

    SW_EXPECT_TRUE( state.activate( RespawnFlowTestUtil::makeDef( "bonfire", 0, true ), sw::float3{}, 0.0f ) == sw::CheckpointActivation::Reused );
    SW_EXPECT_EQUAL( sw::hashed_string( "bonfire" ), state.getCurrent()->_id );
    SW_EXPECT_NEAR( 1.0f, state.getCurrent()->_position._x, kRespawnTolerance ); // 처음 켠 자리를 남긴다
    SW_EXPECT_NEAR( 0.5f, state.getCurrent()->_yaw, kRespawnTolerance );
    SW_EXPECT_TRUE( state.activate( RespawnFlowTestUtil::makeDef( "flag", 1, false ), sw::float3{}, 0.0f ) == sw::CheckpointActivation::Spent );
    SW_EXPECT_EQUAL( sw::hashed_string( "bonfire" ), state.getCurrent()->_id );
    SW_EXPECT_EQUAL( size_t( 2 ), state.getActivated().size() );

    state.reset();
    SW_EXPECT_TRUE( state.getCurrent() == nullptr );
    SW_EXPECT_TRUE( state.getActivated().empty() );
}

/**
 * @brief [CheckpointStateTest] 앞으로만 가는 상태는 건너뛴 체크포인트를 인정하고, 지난 것 · 앞쪽 것으로 되돌아가지 않는다
 */
SW_TEST_CASE( CheckpointStateTest, ForwardOnlyNeverGoesBack )
{
    sw::CheckpointState state;
    state.setForwardOnly( true );
    SW_EXPECT_TRUE( state.activate( RespawnFlowTestUtil::makeDef( "cp2", 1, false ), sw::float3{}, 0.0f ) == sw::CheckpointActivation::Activated );
    SW_EXPECT_TRUE( state.activate( RespawnFlowTestUtil::makeDef( "cp1", 0, false ), sw::float3{}, 0.0f ) == sw::CheckpointActivation::Behind );
    SW_EXPECT_TRUE( state.activate( RespawnFlowTestUtil::makeDef( "cp2", 1, false ), sw::float3{}, 0.0f ) == sw::CheckpointActivation::AlreadyCurrent );
    SW_EXPECT_TRUE( state.activate( RespawnFlowTestUtil::makeDef( "cp3", 2, true ), sw::float3{}, 0.0f ) == sw::CheckpointActivation::Activated );
    SW_EXPECT_EQUAL( sw::hashed_string( "cp3" ), state.getCurrent()->_id );
    SW_EXPECT_TRUE( state.isConsistent() );
}

/**
 * @brief [CheckpointStateTest] 쓰고 읽은 상태는 같은 부활 지점을 내고, 자리 밖 · 겹친 id · 앞으로만을 어긴 바이트는 거절하며 상태는 그대로다
 */
SW_TEST_CASE( CheckpointStateTest, StateRoundTripAndRejectsInconsistentBytes )
{
    sw::CheckpointState original;
    original.setForwardOnly( true );
    (void)original.activate( RespawnFlowTestUtil::makeDef( "cp1", 0, false ), sw::float3{ 3.0f, 1.0f, 0.0f }, 0.25f );
    (void)original.activate( RespawnFlowTestUtil::makeDef( "cp2", 1, false ), sw::float3{ 15.0f, 1.0f, 0.0f }, 0.0f );
    sw::Archive written;
    original.writeState( written );

    sw::CheckpointState restored;
    restored.setForwardOnly( true );
    sw::Archive reading( written.getData(), written.getSize() );
    SW_REQUIRE_TRUE( restored.readState( reading ) );
    SW_REQUIRE_TRUE( restored.getCurrent() != nullptr );
    SW_EXPECT_EQUAL( sw::hashed_string( "cp2" ), restored.getCurrent()->_id );
    SW_EXPECT_NEAR( 15.0f, restored.getCurrent()->_position._x, kRespawnTolerance );
    SW_EXPECT_EQUAL( size_t( 2 ), restored.getActivated().size() );

    // 지금 자리가 마지막이 아닌 상태(다시 쓰기로 앞쪽에 돌아감)는 앞으로만 가는 상태에서 만들어지지 않는다 — 그 바이트를 거절한다.
    sw::CheckpointState reused;
    (void)reused.activate( RespawnFlowTestUtil::makeDef( "a", 0, true ), sw::float3{}, 0.0f );
    (void)reused.activate( RespawnFlowTestUtil::makeDef( "b", 1, true ), sw::float3{}, 0.0f );
    (void)reused.activate( RespawnFlowTestUtil::makeDef( "a", 0, true ), sw::float3{}, 0.0f );
    sw::Archive reusedBytes;
    reused.writeState( reusedBytes );
    sw::CheckpointState forwardTarget;
    forwardTarget.setForwardOnly( true );
    sw::Archive forwardReading( reusedBytes.getData(), reusedBytes.getSize() );
    SW_EXPECT_FALSE( forwardTarget.readState( forwardReading ) );
    SW_EXPECT_TRUE( forwardTarget.getCurrent() == nullptr );
    // 같은 바이트를 다시 쓰기 상태는 받는다(대조).
    sw::CheckpointState freeTarget;
    sw::Archive         freeReading( reusedBytes.getData(), reusedBytes.getSize() );
    SW_EXPECT_TRUE( freeTarget.readState( freeReading ) );
    SW_EXPECT_EQUAL( sw::hashed_string( "a" ), freeTarget.getCurrent()->_id );

    sw::CheckpointState truncatedTarget;
    sw::Archive         truncated( written.getData(), written.getSize() - 1 );
    SW_EXPECT_FALSE( truncatedTarget.readState( truncated ) );
    SW_EXPECT_TRUE( truncatedTarget.getActivated().empty() );
}

/**
 * @brief [RespawnRulesTest] `<Respawn>` 의 속성과 `<Policy>` 자식을 차례대로 읽는다
 */
SW_TEST_CASE( RespawnRulesTest, ReadsAttributesAndPolicies )
{
    sw::XMLDocument document;
    SW_REQUIRE_TRUE( document.parse( R"(<Respawn delay="2" invulnerableTime="1.5" penaltyCurrency="gold" penaltyCurrencyRatio="0.1" penaltyExperienceRatio="0.05">
  <Policy kind="PlayerChoice" choiceTimeout="10"/>
  <Policy kind="InPlace" maxUses="1" costCurrency="gold" costAmount="50" safeRadius="6" requestOnly="true"/>
  <Policy kind="LastCheckpoint"/>
</Respawn>)" ) );
    sw::RespawnRules rules;
    SW_REQUIRE_TRUE( sw::RespawnRules::readXML( document.getRoot(), "RespawnRulesTest", rules ) );
    SW_EXPECT_NEAR( 2.0f, rules._delay, kRespawnTolerance );
    SW_EXPECT_NEAR( 1.5f, rules._invulnerableTime, kRespawnTolerance );
    SW_EXPECT_EQUAL( kGold, rules._penaltyCurrency );
    SW_REQUIRE_EQUAL( size_t( 3 ), rules._listPolicy.size() );
    SW_EXPECT_TRUE( rules._listPolicy[0]._kind == sw::RespawnPolicyKind::PlayerChoice );
    SW_EXPECT_TRUE( rules._listPolicy[1]._kind == sw::RespawnPolicyKind::InPlace );
    SW_EXPECT_EQUAL( 50, rules._listPolicy[1]._costAmount );
    SW_EXPECT_TRUE( rules._listPolicy[1]._bRequestOnly );
    SW_EXPECT_TRUE( rules.findPolicyIndex( sw::RespawnPolicyKind::InPlace ) == 1 );
    SW_EXPECT_FALSE( rules.findPolicyIndex( sw::RespawnPolicyKind::TeamBase ).has_value() );
}

/**
 * @brief [RespawnRulesTest] 정책 없음 · 모르는 자식 원소 · 같은 정책 두 번 · 통화 없는 비용 · 모두 요청 전용 · 범위 밖 값은 읽기와 흐름 시작을 거절한다(대신 정한 정책으로 돌지 않는다)
 */
SW_TEST_CASE( RespawnRulesTest, InvalidRulesAreRejected )
{
    SW_EXPECT_LOG( "RespawnRules.Invalid" );
    SW_EXPECT_LOG( "Xml.UnknownElement" );
    SW_EXPECT_LOG( "RespawnFlow.RefusedRules" );
    SW_EXPECT_LOG( "RespawnFlow.RefusedRules" );
    SW_EXPECT_LOG( "RespawnFlow.RefusedRules" );
    SW_EXPECT_LOG( "RespawnFlow.RefusedRules" );
    SW_EXPECT_LOG( "RespawnFlow.RefusedRules" );

    sw::XMLDocument document;
    SW_REQUIRE_TRUE( document.parse( R"(<Respawn delay="1"/>)" ) );
    sw::RespawnRules fromXML;
    SW_EXPECT_FALSE( sw::RespawnRules::readXML( document.getRoot(), "noPolicy", fromXML ) );
    // 틀린 자식 이름은 말없이 빠지지 않는다(정책 하나가 사라진 채 읽히지 않는다).
    sw::XMLDocument typo;
    SW_REQUIRE_TRUE( typo.parse( R"(<Respawn><Polcy kind="InPlace"/><Policy kind="LastCheckpoint"/></Respawn>)" ) );
    SW_EXPECT_FALSE( sw::RespawnRules::readXML( typo.getRoot(), "typo", fromXML ) );

    sw::RespawnFlow  flow;
    sw::RespawnRules empty;
    SW_EXPECT_FALSE( flow.initialize( empty ) );

    sw::RespawnRules twice;
    twice._listPolicy.push_back( RespawnFlowTestUtil::makePolicy( sw::RespawnPolicyKind::LastCheckpoint ) );
    twice._listPolicy.push_back( RespawnFlowTestUtil::makePolicy( sw::RespawnPolicyKind::LastCheckpoint ) );
    SW_EXPECT_FALSE( flow.initialize( twice ) );

    sw::RespawnRules costWithoutCurrency;
    costWithoutCurrency._listPolicy.push_back( RespawnFlowTestUtil::makePolicy( sw::RespawnPolicyKind::InPlace ) );
    costWithoutCurrency._listPolicy[0]._costAmount = 10;
    SW_EXPECT_FALSE( flow.initialize( costWithoutCurrency ) );

    sw::RespawnRules onlyRequests;
    onlyRequests._listPolicy.push_back( RespawnFlowTestUtil::makePolicy( sw::RespawnPolicyKind::InPlace ) );
    onlyRequests._listPolicy[0]._bRequestOnly = true;
    SW_EXPECT_FALSE( flow.initialize( onlyRequests ) );

    sw::RespawnRules negativeDelay;
    negativeDelay._listPolicy.push_back( RespawnFlowTestUtil::makePolicy( sw::RespawnPolicyKind::LastCheckpoint ) );
    negativeDelay._delay = -1.0f;
    SW_EXPECT_FALSE( flow.initialize( negativeDelay ) );
    SW_EXPECT_FALSE( flow.notifyDied( sw::float3{}, 0.0f ) ); // 규칙 없는 흐름은 죽음을 받지 않는다
}

/**
 * @brief [RespawnFlowTest] 정책은 적힌 차례로 시도하고 자리를 못 내는 것(체크포인트 없음)은 건너뛴다. 대기가 끝난 걸음에 스폰하고 체력을 채운다
 */
SW_TEST_CASE( RespawnFlowTest, PoliciesAreTriedInOrderAfterTheDelay )
{
    sw::RespawnRules rules;
    rules._delay = 1.0f;
    rules._listPolicy.push_back( RespawnFlowTestUtil::makePolicy( sw::RespawnPolicyKind::LastCheckpoint ) );
    rules._listPolicy.push_back( RespawnFlowTestUtil::makePolicy( sw::RespawnPolicyKind::NearestSpawnPoint ) );
    sw::RespawnFlow flow;
    SW_REQUIRE_TRUE( flow.initialize( rules ) );

    const sw::vector<sw::RespawnSpawnPoint> listPoint = {
        sw::RespawnSpawnPoint{  sw::hashed_string( "far" ), sw::float3{ 40.0f, 0.0f, 0.0f }, 0.0f, -1},
        sw::RespawnSpawnPoint{ sw::hashed_string( "near" ), sw::float3{ 12.0f, 0.0f, 0.0f }, 0.0f, -1},
        sw::RespawnSpawnPoint{sw::hashed_string( "enemy" ), sw::float3{ 11.0f, 0.0f, 0.0f }, 0.0f,  2}
    };
    sw::CheckpointState    checkpoints;
    sw::HealthStateMachine health;
    health.kill();
    sw::RespawnContext context;
    context._pCheckpoints = &checkpoints;
    context._pSpawnPoints = &listPoint;
    context._pHealth      = &health;
    context._team         = 1;

    SW_EXPECT_FALSE( flow.update( 5.0f, context ).has_value() ); // 살아 있으면 쉰다
    SW_REQUIRE_TRUE( flow.notifyDied( sw::float3{ 10.0f, 0.0f, 0.0f }, 0.0f ) );
    SW_EXPECT_FALSE( flow.notifyDied( sw::float3{}, 0.0f ) ); // 이미 죽었다
    SW_EXPECT_FALSE( flow.update( 0.5f, context ).has_value() );
    SW_EXPECT_TRUE( flow.getPhase() == sw::RespawnPhase::Waiting );
    SW_EXPECT_NEAR( 0.5f, flow.getDelayRemaining(), kRespawnTolerance );

    // 체크포인트가 없어 다음 정책 — 내 팀 · 모든 팀 지점 가운데 가장 가까운 것(남의 팀 지점은 더 가까워도 고르지 않는다)
    const sw::optional<sw::RespawnSpawn> first = flow.update( 0.5f, context );
    SW_REQUIRE_TRUE( first.has_value() );
    SW_EXPECT_TRUE( first->_policy == sw::RespawnPolicyKind::NearestSpawnPoint );
    SW_EXPECT_EQUAL( sw::hashed_string( "near" ), first->_pointID );
    SW_EXPECT_TRUE( flow.getPhase() == sw::RespawnPhase::Alive );
    SW_EXPECT_TRUE( health.isAlive() );

    // 체크포인트를 켠 뒤에는 첫 정책이 그 자리를 낸다(대기 0 이 아니라 1 초).
    (void)checkpoints.activate( RespawnFlowTestUtil::makeDef( "cp1", 0, false ), sw::float3{ 3.0f, 1.0f, 0.0f }, 1.0f );
    health.kill();
    SW_REQUIRE_TRUE( flow.notifyDied( sw::float3{ 20.0f, 0.0f, 0.0f }, 0.0f ) );
    const sw::optional<sw::RespawnSpawn> second = flow.update( 1.0f, context );
    SW_REQUIRE_TRUE( second.has_value() );
    SW_EXPECT_TRUE( second->_policy == sw::RespawnPolicyKind::LastCheckpoint );
    SW_EXPECT_EQUAL( sw::hashed_string( "cp1" ), second->_pointID );
    SW_EXPECT_NEAR( 3.0f, second->_position._x, kRespawnTolerance );
    SW_EXPECT_NEAR( 1.0f, second->_yaw, kRespawnTolerance );
    SW_EXPECT_EQUAL( 1, flow.getPolicyUses( sw::RespawnPolicyKind::LastCheckpoint ) );
    SW_EXPECT_EQUAL( 1, flow.getPolicyUses( sw::RespawnPolicyKind::NearestSpawnPoint ) );
}

/**
 * @brief [RespawnFlowTest] 제자리 부활: 안전한 자리면 그 자리, 낭떠러지면 반경 안 가장 가까운 땅 가장자리, 반경 밖이거나 세계 질의가 없으면 다음 정책이다. 횟수가 끝나면 다음 정책이다
 */
SW_TEST_CASE( RespawnFlowTest, InPlaceUsesTheNearestSafeLocation )
{
    sw::RespawnRules rules;
    rules._listPolicy.push_back( RespawnFlowTestUtil::makePolicy( sw::RespawnPolicyKind::InPlace ) );
    rules._listPolicy[0]._safeRadius = 3.0f;
    rules._listPolicy[0]._maxUses    = 2;
    rules._listPolicy.push_back( RespawnFlowTestUtil::makePolicy( sw::RespawnPolicyKind::TeamBase ) );
    sw::RespawnFlow flow;
    SW_REQUIRE_TRUE( flow.initialize( rules ) );

    const sw::RespawnTestWorld              world     = RespawnFlowTestUtil::makeWorldWithPit();
    const sw::vector<sw::RespawnSpawnPoint> listPoint = {
        sw::RespawnSpawnPoint{ sw::hashed_string( "base" ), sw::float3{ 0.0f, 1.0f, 0.0f }, 0.0f, 1 }
    };
    sw::RespawnContext context;
    context._pWorld       = &world;
    context._pSpawnPoints = &listPoint;
    context._team         = 1;

    // 땅 위에서 죽었다 — 그 자리 그대로
    SW_REQUIRE_TRUE( flow.notifyDied( sw::float3{ 5.0f, 1.0f, 0.0f }, 0.75f ) );
    const sw::optional<sw::RespawnSpawn> onGround = flow.update( 0.016f, context );
    SW_REQUIRE_TRUE( onGround.has_value() );
    SW_EXPECT_TRUE( onGround->_policy == sw::RespawnPolicyKind::InPlace );
    SW_EXPECT_TRUE( onGround->_pointID.empty() );
    SW_EXPECT_NEAR( 5.0f, onGround->_position._x, kRespawnTolerance );
    SW_EXPECT_NEAR( 1.0f, onGround->_position._y, kRespawnTolerance );
    SW_EXPECT_NEAR( 0.75f, onGround->_yaw, kRespawnTolerance );

    // 낭떠러지(구덩이 x 10..14) 바닥에 떨어져 죽었다 — 가까운 땅 가장자리(x 11 → 10)
    SW_REQUIRE_TRUE( flow.notifyDied( sw::float3{ 11.0f, -1.0f, 0.0f }, 0.0f ) );
    const sw::optional<sw::RespawnSpawn> fromPit = flow.update( 0.016f, context );
    SW_REQUIRE_TRUE( fromPit.has_value() );
    SW_EXPECT_TRUE( fromPit->_policy == sw::RespawnPolicyKind::InPlace );
    SW_EXPECT_NEAR( 10.0f, fromPit->_position._x, kRespawnTolerance );
    SW_EXPECT_NEAR( 1.0f, fromPit->_position._y, kRespawnTolerance );
    SW_EXPECT_EQUAL( 2, flow.getPolicyUses( sw::RespawnPolicyKind::InPlace ) );

    // 횟수가 끝났다 — 땅 위여도 팀 거점
    SW_REQUIRE_TRUE( flow.notifyDied( sw::float3{ 20.0f, 1.0f, 0.0f }, 0.0f ) );
    const sw::optional<sw::RespawnSpawn> exhausted = flow.update( 0.016f, context );
    SW_REQUIRE_TRUE( exhausted.has_value() );
    SW_EXPECT_TRUE( exhausted->_policy == sw::RespawnPolicyKind::TeamBase );
    SW_EXPECT_EQUAL( sw::hashed_string( "base" ), exhausted->_pointID );

    // 반경 밖 낭떠러지 · 세계 질의 없음 — 제자리를 건너뛴다(죽은 자리에 세우지 않는다)
    sw::RespawnFlow fresh;
    SW_REQUIRE_TRUE( fresh.initialize( rules ) );
    SW_REQUIRE_TRUE( fresh.notifyDied( sw::float3{ 12.0f, -20.0f, 0.0f }, 0.0f ) );
    const sw::optional<sw::RespawnSpawn> tooDeep = fresh.update( 0.016f, context );
    SW_REQUIRE_TRUE( tooDeep.has_value() );
    SW_EXPECT_TRUE( tooDeep->_policy == sw::RespawnPolicyKind::TeamBase );
    sw::RespawnContext noWorld = context;
    noWorld._pWorld            = nullptr;
    SW_REQUIRE_TRUE( fresh.notifyDied( sw::float3{ 5.0f, 1.0f, 0.0f }, 0.0f ) );
    const sw::optional<sw::RespawnSpawn> blind = fresh.update( 0.016f, noWorld );
    SW_REQUIRE_TRUE( blind.has_value() );
    SW_EXPECT_TRUE( blind->_policy == sw::RespawnPolicyKind::TeamBase );
}

/**
 * @brief [RespawnFlowTest] 어떤 정책도 자리를 못 내면 다음 걸음에 다시 보고(거점이 생길 때까지), 고른 자리의 셀이 준비되지 않았으면 기다린다
 */
SW_TEST_CASE( RespawnFlowTest, WaitsForALocationAndForItToBeReady )
{
    sw::RespawnRules rules;
    rules._listPolicy.push_back( RespawnFlowTestUtil::makePolicy( sw::RespawnPolicyKind::TeamBase ) );
    sw::RespawnFlow flow;
    SW_REQUIRE_TRUE( flow.initialize( rules ) );

    sw::RespawnTestWorld world = RespawnFlowTestUtil::makeWorldWithPit();
    world._bReady              = SW_FALSE;
    sw::vector<sw::RespawnSpawnPoint> listPoint;
    sw::RespawnContext                context;
    context._pWorld       = &world;
    context._pSpawnPoints = &listPoint;
    context._team         = 0;

    SW_REQUIRE_TRUE( flow.notifyDied( sw::float3{}, 0.0f ) );
    SW_EXPECT_FALSE( flow.update( 0.1f, context ).has_value() );
    SW_EXPECT_FALSE( flow.update( 0.1f, context ).has_value() );
    SW_EXPECT_TRUE( flow.getPhase() == sw::RespawnPhase::Search );

    listPoint.push_back( sw::RespawnSpawnPoint{
        sw::hashed_string( "home" ), sw::float3{ 2.0f, 1.0f, 0.0f },
        0.0f, 0
    } );
    SW_EXPECT_FALSE( flow.update( 0.1f, context ).has_value() ); // 자리는 골랐지만 셀이 오르지 않았다
    SW_EXPECT_TRUE( flow.isDead() );
    world._bReady                                = SW_TRUE;
    const sw::optional<sw::RespawnSpawn> spawned = flow.update( 0.1f, context );
    SW_REQUIRE_TRUE( spawned.has_value() );
    SW_EXPECT_EQUAL( sw::hashed_string( "home" ), spawned->_pointID );
}

/**
 * @brief [RespawnFlowTest] 스폰하면 체력을 채우고 규칙의 무적을 걸며, 벌칙 통화는 잔액 × 비율 + 고정(잔액까지)을 빼고 경험치 · 내구도 비율은 결과로 넘긴다
 */
SW_TEST_CASE( RespawnFlowTest, SpawnRestoresHealthGrantsInvulnerabilityAndPenalizes )
{
    sw::RespawnRules rules;
    rules._invulnerableTime       = 2.0f;
    rules._penaltyCurrency        = kGold;
    rules._penaltyCurrencyRatio   = 0.1f;
    rules._penaltyCurrencyAmount  = 5;
    rules._penaltyExperienceRatio = 0.25f;
    rules._penaltyDurabilityRatio = 0.1f;
    rules._listPolicy.push_back( RespawnFlowTestUtil::makePolicy( sw::RespawnPolicyKind::NearestSpawnPoint ) );
    sw::RespawnFlow flow;
    SW_REQUIRE_TRUE( flow.initialize( rules ) );

    const sw::vector<sw::RespawnSpawnPoint> listPoint = {
        sw::RespawnSpawnPoint{ sw::hashed_string( "start" ), sw::float3{}, 0.0f, -1 }
    };
    sw::HealthStateMachine health;
    sw::Wallet             wallet;
    wallet.add( kGold, 100 );
    sw::RespawnContext context;
    context._pSpawnPoints = &listPoint;
    context._pHealth      = &health;
    context._pWallet      = &wallet;

    health.kill();
    SW_REQUIRE_TRUE( flow.notifyDied( sw::float3{}, 0.0f ) );
    const sw::optional<sw::RespawnSpawn> spawned = flow.update( 0.016f, context );
    SW_REQUIRE_TRUE( spawned.has_value() );
    SW_EXPECT_TRUE( health.isAlive() );
    SW_EXPECT_NEAR( health.getSettings()._maxHealth, health.getHealth(), kRespawnTolerance );
    SW_EXPECT_TRUE( health.isInvulnerable() );
    SW_EXPECT_TRUE( health.applyDamage( 10.0f )._bIgnored == SW_TRUE );
    health.update( 2.1f );
    SW_EXPECT_FALSE( health.isInvulnerable() );
    SW_EXPECT_EQUAL( int64( 15 ), spawned->_currencyPenalty ); // 100 × 0.1 + 5
    SW_EXPECT_EQUAL( int64( 85 ), wallet.getBalance( kGold ) );
    SW_EXPECT_NEAR( 0.25f, spawned->_experiencePenaltyRatio, kRespawnTolerance );
    SW_EXPECT_NEAR( 0.1f, spawned->_durabilityPenaltyRatio, kRespawnTolerance );

    // 잔액보다 큰 벌칙은 잔액까지만(빚을 지우지 않는다)
    wallet.setBalance( kGold, 3 );
    health.kill();
    SW_REQUIRE_TRUE( flow.notifyDied( sw::float3{}, 0.0f ) );
    const sw::optional<sw::RespawnSpawn> poor = flow.update( 0.016f, context );
    SW_REQUIRE_TRUE( poor.has_value() );
    SW_EXPECT_EQUAL( int64( 3 ), poor->_currencyPenalty );
    SW_EXPECT_EQUAL( int64( 0 ), wallet.getBalance( kGold ) );
}

/**
 * @brief [RespawnFlowTest] 비용 있는 정책은 낼 수 있을 때만 쓰고 스폰 때 뺀다. 낼 수 없으면 다음 정책이다
 */
SW_TEST_CASE( RespawnFlowTest, CostIsChargedOnlyWhenAffordable )
{
    sw::RespawnRules rules;
    rules._listPolicy.push_back( RespawnFlowTestUtil::makePolicy( sw::RespawnPolicyKind::InPlace ) );
    rules._listPolicy[0]._costCurrency = kGold;
    rules._listPolicy[0]._costAmount   = 50;
    rules._listPolicy[0]._safeRadius   = 1.0f;
    rules._listPolicy.push_back( RespawnFlowTestUtil::makePolicy( sw::RespawnPolicyKind::NearestSpawnPoint ) );
    sw::RespawnFlow flow;
    SW_REQUIRE_TRUE( flow.initialize( rules ) );

    const sw::RespawnTestWorld              world     = RespawnFlowTestUtil::makeWorldWithPit();
    const sw::vector<sw::RespawnSpawnPoint> listPoint = {
        sw::RespawnSpawnPoint{ sw::hashed_string( "town" ), sw::float3{ 0.0f, 1.0f, 0.0f }, 0.0f, -1 }
    };
    sw::Wallet wallet;
    wallet.add( kGold, 70 );
    sw::RespawnContext context;
    context._pWorld       = &world;
    context._pSpawnPoints = &listPoint;
    context._pWallet      = &wallet;

    SW_REQUIRE_TRUE( flow.notifyDied( sw::float3{ 5.0f, 1.0f, 0.0f }, 0.0f ) );
    const sw::optional<sw::RespawnSpawn> paid = flow.update( 0.016f, context );
    SW_REQUIRE_TRUE( paid.has_value() );
    SW_EXPECT_TRUE( paid->_policy == sw::RespawnPolicyKind::InPlace );
    SW_EXPECT_EQUAL( int64( 50 ), paid->_costCharged );
    SW_EXPECT_EQUAL( int64( 20 ), wallet.getBalance( kGold ) );

    SW_REQUIRE_TRUE( flow.notifyDied( sw::float3{ 5.0f, 1.0f, 0.0f }, 0.0f ) );
    const sw::optional<sw::RespawnSpawn> broke = flow.update( 0.016f, context );
    SW_REQUIRE_TRUE( broke.has_value() );
    SW_EXPECT_TRUE( broke->_policy == sw::RespawnPolicyKind::NearestSpawnPoint );
    SW_EXPECT_EQUAL( int64( 20 ), wallet.getBalance( kGold ) );
}

/**
 * @brief [RespawnFlowTest] 고르기 단계는 시한까지 기다리고, 시한이 지나면 고르기 다음 정책으로 넘어간다(요청 전용 정책은 저절로 시도하지 않는다)
 */
SW_TEST_CASE( RespawnFlowTest, PlayerChoiceTimesOutToTheNextPolicy )
{
    sw::RespawnRules rules;
    rules._listPolicy.push_back( RespawnFlowTestUtil::makePolicy( sw::RespawnPolicyKind::PlayerChoice ) );
    rules._listPolicy[0]._choiceTimeout = 5.0f;
    rules._listPolicy.push_back( RespawnFlowTestUtil::makePolicy( sw::RespawnPolicyKind::InPlace ) );
    rules._listPolicy[1]._bRequestOnly = true;
    rules._listPolicy[1]._safeRadius   = 1.0f;
    rules._listPolicy.push_back( RespawnFlowTestUtil::makePolicy( sw::RespawnPolicyKind::NearestSpawnPoint ) );
    sw::RespawnFlow flow;
    SW_REQUIRE_TRUE( flow.initialize( rules ) );

    const sw::RespawnTestWorld              world     = RespawnFlowTestUtil::makeWorldWithPit();
    const sw::vector<sw::RespawnSpawnPoint> listPoint = {
        sw::RespawnSpawnPoint{ sw::hashed_string( "town" ), sw::float3{ 0.0f, 1.0f, 0.0f }, 0.0f, -1 }
    };
    sw::RespawnContext context;
    context._pWorld       = &world;
    context._pSpawnPoints = &listPoint;

    SW_REQUIRE_TRUE( flow.notifyDied( sw::float3{ 5.0f, 1.0f, 0.0f }, 0.0f ) );
    SW_EXPECT_FALSE( flow.update( 0.016f, context ).has_value() );
    SW_EXPECT_TRUE( flow.getPhase() == sw::RespawnPhase::Selection );
    sw::vector<sw::hashed_string> listChoice;
    flow.collectChoices( context, listChoice );
    SW_REQUIRE_EQUAL( size_t( 1 ), listChoice.size() );
    SW_EXPECT_EQUAL( sw::hashed_string( "town" ), listChoice[0] );

    SW_EXPECT_FALSE( flow.update( 4.0f, context ).has_value() );
    SW_EXPECT_NEAR( 1.0f, flow.getChoiceRemaining(), 1.0e-3f );
    const sw::optional<sw::RespawnSpawn> timedOut = flow.update( 1.0f, context );
    SW_REQUIRE_TRUE( timedOut.has_value() );
    SW_EXPECT_TRUE( timedOut->_policy == sw::RespawnPolicyKind::NearestSpawnPoint );
    SW_EXPECT_EQUAL( 0, flow.getPolicyUses( sw::RespawnPolicyKind::InPlace ) );
}

/**
 * @brief [RespawnFlowTest] 동료 부활은 흐름이 아니다: 기절은 체력 상태 기계가 갖고 흐름은 죽음에서만 시작한다 — 기절 중 부활 받으면 흐름을 거치지 않고, 출혈사하면 흐름이 돈다
 */
SW_TEST_CASE( RespawnFlowTest, DownedAllyReviveDoesNotStartTheFlow )
{
    sw::HealthSettings settings;
    settings._bDownedEnabled = SW_TRUE;
    settings._reviveTime     = 1.0f;
    sw::HealthStateMachine health;
    SW_REQUIRE_TRUE( health.initialize( settings ) );
    sw::RespawnRules rules;
    rules._listPolicy.push_back( RespawnFlowTestUtil::makePolicy( sw::RespawnPolicyKind::NearestSpawnPoint ) );
    sw::RespawnFlow flow;
    SW_REQUIRE_TRUE( flow.initialize( rules ) );

    // 게임은 체력 알림의 `Died` 에서만 흐름에 알린다.
    const auto notifyOnDeath = [&flow, &health]()
    {
        sw::vector<sw::HealthEvent> listEvent;
        health.pollEvents( listEvent );
        for ( const sw::HealthEvent& event : listEvent )
        {
            if ( event._type == sw::HealthEventKind::Died )
                (void)flow.notifyDied( sw::float3{}, 0.0f );
        }
    };
    (void)health.applyDamage( 1000.0f );
    SW_REQUIRE_TRUE( health.isDowned() );
    notifyOnDeath();
    SW_EXPECT_FALSE( flow.isDead() );
    SW_REQUIRE_TRUE( health.startRevive( 7 ) );
    health.update( 1.1f );
    SW_EXPECT_TRUE( health.isAlive() );
    notifyOnDeath();
    SW_EXPECT_FALSE( flow.isDead() );

    health.update( settings._invulnerableAfterRevive + 0.1f ); // 부활 직후 무적이 풀린 뒤에 맞는다
    (void)health.applyDamage( 1000.0f );
    SW_REQUIRE_TRUE( health.isDowned() );
    health.update( 1000.0f ); // 출혈사
    SW_REQUIRE_TRUE( health.isDead() );
    notifyOnDeath();
    SW_EXPECT_TRUE( flow.isDead() );
}

/**
 * @brief [RespawnFlowTest] 쓰고 읽은 흐름은 같은 단계 · 남은 대기 · 횟수에서 이어 가고, 정책 수가 다른 규칙 · 어긋난 바이트는 거절한다
 */
SW_TEST_CASE( RespawnFlowTest, StateRoundTripContinuesTheFlow )
{
    sw::RespawnRules rules;
    rules._delay = 3.0f;
    rules._listPolicy.push_back( RespawnFlowTestUtil::makePolicy( sw::RespawnPolicyKind::NearestSpawnPoint ) );
    rules._listPolicy[0]._maxUses = 5;
    sw::RespawnFlow original;
    SW_REQUIRE_TRUE( original.initialize( rules ) );
    const sw::vector<sw::RespawnSpawnPoint> listPoint = {
        sw::RespawnSpawnPoint{ sw::hashed_string( "start" ), sw::float3{ 1.0f, 2.0f, 3.0f }, 0.0f, -1 }
    };
    sw::RespawnContext context;
    context._pSpawnPoints = &listPoint;
    SW_REQUIRE_TRUE( original.notifyDied( sw::float3{}, 0.0f ) );
    SW_REQUIRE_TRUE( original.update( 3.0f, context ).has_value() );
    SW_REQUIRE_TRUE( original.notifyDied( sw::float3{ 4.0f, 0.0f, 0.0f }, 0.0f ) );
    SW_EXPECT_FALSE( original.update( 1.0f, context ).has_value() );

    sw::Archive written;
    original.writeState( written );

    sw::RespawnFlow restored;
    SW_REQUIRE_TRUE( restored.initialize( rules ) );
    sw::Archive reading( written.getData(), written.getSize() );
    SW_REQUIRE_TRUE( restored.readState( reading ) );
    SW_EXPECT_TRUE( restored.getPhase() == sw::RespawnPhase::Waiting );
    SW_EXPECT_NEAR( 2.0f, restored.getDelayRemaining(), kRespawnTolerance );
    SW_EXPECT_NEAR( 4.0f, restored.getDeathPosition()._x, kRespawnTolerance );
    SW_EXPECT_EQUAL( 1, restored.getPolicyUses( sw::RespawnPolicyKind::NearestSpawnPoint ) );
    SW_EXPECT_FALSE( restored.update( 1.0f, context ).has_value() );
    SW_EXPECT_TRUE( restored.update( 1.0f, context ).has_value() );

    sw::RespawnRules longer = rules;
    longer._listPolicy.push_back( RespawnFlowTestUtil::makePolicy( sw::RespawnPolicyKind::TeamBase ) );
    sw::RespawnFlow otherRules;
    SW_REQUIRE_TRUE( otherRules.initialize( longer ) );
    sw::Archive otherReading( written.getData(), written.getSize() );
    SW_EXPECT_FALSE( otherRules.readState( otherReading ) );
    SW_EXPECT_TRUE( otherRules.getPhase() == sw::RespawnPhase::Alive );

    sw::RespawnFlow truncatedTarget;
    SW_REQUIRE_TRUE( truncatedTarget.initialize( rules ) );
    sw::Archive truncated( written.getData(), written.getSize() - 1 );
    SW_EXPECT_FALSE( truncatedTarget.readState( truncated ) );
    SW_EXPECT_TRUE( truncatedTarget.getPhase() == sw::RespawnPhase::Alive );
}
