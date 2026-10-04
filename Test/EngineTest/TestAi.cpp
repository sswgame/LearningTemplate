#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/AI/AiPerception.h"
#include "GameFramework/AI/BehaviorTree.h"
#include "GameFramework/AI/Blackboard.h"
#include "GameFramework/Navigation/NavGrid.h"
#include "GameFramework/Utility/TimerQueue.h"

#include "TestFramework/TestFramework.h"

// 장르 공통 AI — 블랙보드, 행동 트리(시퀀스 · 셀렉터 · 반응형 · 데코레이터 · 관찰 중단 · 작업 중단 뒷정리), 감각(시야각 · 가림 · 소리 · 기억), 타이머.

using namespace sw;

namespace
{
    /** @brief 작업이 몇 번 불리고 몇 번 중단됐는지 셉니다(행위자 자리에 넘긴다). */
    struct AiTestAgent
    {
        int32 _patrolTicks{ 0 };
        int32 _patrolStarts{ 0 };
        int32 _patrolAborts{ 0 };
        int32 _attackTicks{ 0 };
        int32 _attackStarts{ 0 };
        int32 _attackAborts{ 0 };
        int32 _counter{ 0 };
        int32 _attackDuration{ 3 }; ///< 틱
    };

    AiTestAgent& getAiTestAgent( BehaviorContext& context )
    {
        return *static_cast<AiTestAgent*>( context._pOwner );
    }

    /** @brief 끝나지 않는 순찰 — 중단될 때까지 Running. */
    BehaviorStatus patrolTask( BehaviorContext& context )
    {
        AiTestAgent& agent = getAiTestAgent( context );
        if ( context._bAborted != SW_FALSE )
        {
            ++agent._patrolAborts;
            return BehaviorStatus::Failure;
        }
        agent._patrolStarts += context._bJustStarted != SW_FALSE ? 1 : 0;
        ++agent._patrolTicks;
        return BehaviorStatus::Running;
    }

    /** @brief `_attackDuration` 틱 걸려 끝나는 공격. */
    BehaviorStatus attackTask( BehaviorContext& context )
    {
        AiTestAgent& agent = getAiTestAgent( context );
        if ( context._bAborted != SW_FALSE )
        {
            ++agent._attackAborts;
            return BehaviorStatus::Failure;
        }
        if ( context._bJustStarted != SW_FALSE )
        {
            ++agent._attackStarts;
            agent._counter = 0;
        }
        ++agent._attackTicks;
        ++agent._counter;
        return agent._counter >= agent._attackDuration ? BehaviorStatus::Success : BehaviorStatus::Running;
    }

    BehaviorStatus succeedTask( BehaviorContext& context )
    {
        ++getAiTestAgent( context )._counter;
        return BehaviorStatus::Success;
    }

    BehaviorStatus failTask( BehaviorContext& /*context*/ )
    {
        return BehaviorStatus::Failure;
    }

    BehaviorStatus isHealthyCondition( BehaviorContext& context )
    {
        return context._pBlackboard->getFloat( "Health", 1.0f ) > 0.3f ? BehaviorStatus::Success : BehaviorStatus::Failure;
    }

    void countTimer( int32* pCount )
    {
        ++*pCount;
    }
} // namespace

/**
 * @brief [AiTest] 블랙보드는 종류별로 읽고 쓰며 없는 이름은 기본값 · 오브젝트 0 과 거짓은 "없음" · 바꿀 때마다 리비전이 오른다
 */
SW_TEST_CASE( AiTest, BlackboardStoresTypedValues )
{
    Blackboard blackboard;
    SW_EXPECT_FALSE( blackboard.isSet( "Target" ) );
    SW_EXPECT_NEAR_EQUAL( 0.25f, blackboard.getFloat( "Missing", 0.25f ), 1.0e-6f );
    const uint32 revision = blackboard.getRevision();
    blackboard.setFloat( "Health", 0.5f );
    blackboard.setInt( "Ammo", 12 );
    blackboard.setBool( "Alert", false );
    blackboard.setVector( "Home", float3{ 1.0f, 2.0f, 3.0f } );
    blackboard.setObject( "Target", 0 );
    SW_EXPECT_EQUAL( revision + 5, blackboard.getRevision() );
    SW_EXPECT_EQUAL( 12, blackboard.getInt( "Ammo" ) );
    SW_EXPECT_NEAR_EQUAL( 12.0f, blackboard.getFloat( "Ammo" ), 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, blackboard.getVector( "Home" )._y, 1.0e-6f );
    SW_EXPECT_FALSE( blackboard.isSet( "Alert" ) );
    SW_EXPECT_FALSE( blackboard.isSet( "Target" ) );
    blackboard.setObject( "Target", 77u );
    SW_EXPECT_TRUE( blackboard.isSet( "Target" ) );
    SW_EXPECT_EQUAL( 77u, static_cast<uint32>( blackboard.getObject( "Target" ) ) );
    blackboard.clearValue( "Target" );
    SW_EXPECT_FALSE( blackboard.isSet( "Target" ) );
}

/**
 * @brief [AiTest] 시퀀스는 도는 자식에서 이어가고 실패하면 멈춘다 · 셀렉터는 첫 성공에서 멈춘다 · 반전 · 성공 강제 · 반복 · 쿨다운 · 시간 제한 · 대기
 */
SW_TEST_CASE( AiTest, BehaviorTreeCompositesAndDecorators )
{
    // 뿌리 시퀀스: [조건 Healthy] → [공격(3 틱)] → [성공]
    BehaviorTree tree;
    const int32  root = tree.addSequence( -1, "Root" );
    tree.addCondition( root, "Healthy", &isHealthyCondition );
    tree.addAction( root, "Attack", &attackTask );
    tree.addAction( root, "Count", &succeedTask );
    SW_ASSERT_TRUE( tree.isValid() );
    SW_EXPECT_EQUAL( -1, tree.addSequence( -1, "SecondRoot" ) ); // 뿌리는 하나

    Blackboard         blackboard;
    AiTestAgent        agent;
    BehaviorTreeRunner runner;
    runner.initialize( &tree );
    SW_EXPECT_TRUE( runner.tick( blackboard, &agent, 0.1f ) == BehaviorStatus::Running );
    SW_EXPECT_TRUE( string_view( runner.getActiveLeafName() ) == "Attack" );
    SW_EXPECT_TRUE( runner.tick( blackboard, &agent, 0.1f ) == BehaviorStatus::Running );
    SW_EXPECT_TRUE( runner.tick( blackboard, &agent, 0.1f ) == BehaviorStatus::Success );
    SW_EXPECT_EQUAL( 1, agent._attackStarts ); // 이어서 돌았다 — 다시 시작하지 않았다
    SW_EXPECT_EQUAL( 4, agent._counter );      // 공격 3 + Count 1
    blackboard.setFloat( "Health", 0.1f );
    SW_EXPECT_TRUE( runner.tick( blackboard, &agent, 0.1f ) == BehaviorStatus::Failure );
    SW_EXPECT_EQUAL( 1, agent._attackStarts );

    // 셀렉터: [반전(성공)] 은 실패 → [성공 강제(실패)] 는 성공 — 뒤는 돌지 않는다.
    BehaviorTree selectorTree;
    const int32  selector = selectorTree.addSelector( -1 );
    selectorTree.addAction( selectorTree.addInverter( selector ), "Succeed", &succeedTask );
    selectorTree.addAction( selectorTree.addForceSuccess( selector ), "Fail", &failTask );
    selectorTree.addAction( selector, "Never", &succeedTask );
    SW_ASSERT_TRUE( selectorTree.isValid() );
    agent._counter = 0;
    runner.initialize( &selectorTree );
    SW_EXPECT_TRUE( runner.tick( blackboard, &agent, 0.1f ) == BehaviorStatus::Success );
    SW_EXPECT_EQUAL( 1, agent._counter );

    // 반복 3 번(한 틱에 한 번) · 쿨다운 1 초 · 시간 제한 0.25 초 · 대기 0.3 초.
    BehaviorTree repeatTree;
    repeatTree.addAction( repeatTree.addRepeat( -1, 3 ), "Count", &succeedTask );
    agent._counter = 0;
    runner.initialize( &repeatTree );
    SW_EXPECT_TRUE( runner.tick( blackboard, &agent, 0.1f ) == BehaviorStatus::Running );
    SW_EXPECT_TRUE( runner.tick( blackboard, &agent, 0.1f ) == BehaviorStatus::Running );
    SW_EXPECT_TRUE( runner.tick( blackboard, &agent, 0.1f ) == BehaviorStatus::Success );
    SW_EXPECT_EQUAL( 3, agent._counter );

    BehaviorTree cooldownTree;
    cooldownTree.addAction( cooldownTree.addCooldown( -1, 1.0f ), "Count", &succeedTask );
    agent._counter = 0;
    runner.initialize( &cooldownTree );
    SW_EXPECT_TRUE( runner.tick( blackboard, &agent, 0.4f ) == BehaviorStatus::Success );
    SW_EXPECT_TRUE( runner.tick( blackboard, &agent, 0.4f ) == BehaviorStatus::Failure );
    SW_EXPECT_TRUE( runner.tick( blackboard, &agent, 0.4f ) == BehaviorStatus::Failure );
    SW_EXPECT_TRUE( runner.tick( blackboard, &agent, 0.4f ) == BehaviorStatus::Success ); // 1.6 초 — 1.4 초에 풀렸다
    SW_EXPECT_EQUAL( 2, agent._counter );

    BehaviorTree limitTree;
    limitTree.addAction( limitTree.addTimeLimit( -1, 0.25f ), "Patrol", &patrolTask );
    agent = AiTestAgent{};
    runner.initialize( &limitTree );
    SW_EXPECT_TRUE( runner.tick( blackboard, &agent, 0.1f ) == BehaviorStatus::Running );
    SW_EXPECT_TRUE( runner.tick( blackboard, &agent, 0.1f ) == BehaviorStatus::Running );
    SW_EXPECT_TRUE( runner.tick( blackboard, &agent, 0.1f ) == BehaviorStatus::Failure );
    SW_EXPECT_EQUAL( 1, agent._patrolAborts ); // 시간이 다해 중단 — 뒷정리 한 번

    BehaviorTree waitTree;
    waitTree.addWait( -1, 0.3f );
    SW_ASSERT_TRUE( waitTree.isValid() );
    runner.initialize( &waitTree );
    SW_EXPECT_TRUE( runner.tick( blackboard, &agent, 0.2f ) == BehaviorStatus::Running );
    SW_EXPECT_TRUE( runner.tick( blackboard, &agent, 0.2f ) == BehaviorStatus::Success );

    BehaviorTree broken;
    (void)broken.addInverter( -1 );
    SW_EXPECT_FALSE( broken.isValid() ); // 데코레이터에 자식이 없다
}

/**
 * @brief [AiTest] 관찰 중단 — 적이 생기면(LowerPriority) 순찰을 멈추고 공격으로, 적이 사라지면(Self) 공격을 멈추고 순찰로 · 중단된 작업은 뒷정리를 한 번 받는다
 */
SW_TEST_CASE( AiTest, BlackboardObserverAbortsSwitchBranches )
{
    BehaviorTree tree;
    const int32  root   = tree.addSelector( -1, "Root" );
    const int32  attack = tree.addBlackboardCondition( root, "Target", BlackboardCompare::IsSet, 0.0f, BehaviorAbortMode::Both );
    tree.addAction( attack, "Attack", &attackTask );
    tree.addAction( root, "Patrol", &patrolTask );
    SW_ASSERT_TRUE( tree.isValid() );

    Blackboard  blackboard;
    AiTestAgent agent;
    agent._attackDuration = 1000; // 끝나지 않는 공격
    BehaviorTreeRunner runner;
    runner.initialize( &tree );
    for ( int32 tickIndex = 0; tickIndex < 3; ++tickIndex )
        SW_EXPECT_TRUE( runner.tick( blackboard, &agent, 0.1f ) == BehaviorStatus::Running );
    SW_EXPECT_EQUAL( 1, agent._patrolStarts );
    SW_EXPECT_EQUAL( 3, agent._patrolTicks );

    blackboard.setObject( "Target", 5u );
    SW_EXPECT_TRUE( runner.tick( blackboard, &agent, 0.1f ) == BehaviorStatus::Running );
    SW_EXPECT_EQUAL( 1, agent._patrolAborts );
    SW_EXPECT_EQUAL( 1, agent._attackStarts );
    SW_EXPECT_TRUE( string_view( runner.getActiveLeafName() ) == "Attack" );
    SW_EXPECT_TRUE( runner.tick( blackboard, &agent, 0.1f ) == BehaviorStatus::Running );
    SW_EXPECT_EQUAL( 1, agent._attackStarts ); // 공격을 이어간다

    blackboard.clearValue( "Target" );
    SW_EXPECT_TRUE( runner.tick( blackboard, &agent, 0.1f ) == BehaviorStatus::Running );
    SW_EXPECT_EQUAL( 1, agent._attackAborts );
    SW_EXPECT_EQUAL( 2, agent._patrolStarts );
    SW_EXPECT_TRUE( string_view( runner.getActiveLeafName() ) == "Patrol" );

    // 반응형 셀렉터 — 조건 작업(Healthy)이 성공하는 순간 뒤 가지를 멈춘다.
    BehaviorTree reactive;
    const int32  reactiveRoot = reactive.addSelector( -1, "Root", true );
    const int32  healthy      = reactive.addSequence( reactiveRoot, "Fight" );
    reactive.addCondition( healthy, "Healthy", &isHealthyCondition );
    reactive.addAction( healthy, "Attack", &attackTask );
    reactive.addAction( reactiveRoot, "Patrol", &patrolTask );
    agent                 = AiTestAgent{};
    agent._attackDuration = 1000;
    blackboard.setFloat( "Health", 0.1f );
    runner.initialize( &reactive );
    SW_EXPECT_TRUE( runner.tick( blackboard, &agent, 0.1f ) == BehaviorStatus::Running );
    SW_EXPECT_EQUAL( 1, agent._patrolStarts );
    blackboard.setFloat( "Health", 0.9f );
    SW_EXPECT_TRUE( runner.tick( blackboard, &agent, 0.1f ) == BehaviorStatus::Running );
    SW_EXPECT_EQUAL( 1, agent._patrolAborts );
    SW_EXPECT_EQUAL( 1, agent._attackStarts );

    runner.reset();
    SW_EXPECT_EQUAL( 1, agent._attackAborts );
    SW_EXPECT_EQUAL( -1, runner.getActiveLeaf() );
}

/**
 * @brief [AiTest] 감각 — 시야각 밖 · 벽 뒤는 안 보이고 가까우면 등 뒤도 느낀다 · 소리는 벽을 넘는다 · 놓친 대상은 기억하다 잊는다 · 보던 대상은 조금 더 멀리까지 본다
 */
SW_TEST_CASE( AiTest, PerceptionSeesHearsAndForgets )
{
    NavGrid grid;
    grid.initialize( 30, 30, 1.0f, float3{} );
    grid.setAreaCost( 15, 0, 15, 20, kNavBlockedCost );
    AiPerceptionSettings settings;
    settings._sightRange     = 7.0f;
    settings._loseSightRange = 9.0f;
    settings._memoryDuration = 2.0f;
    AiPerception perception;
    perception.setSettings( settings );
    const float3 eye{ 5.5f, 0.0f, 5.5f };
    const float3 forward{ 1.0f, 0.0f, 0.0f };

    SW_EXPECT_TRUE( perception.canSee( eye, forward, float3{ 11.5f, 0.0f, 6.5f }, &grid, false ) );
    SW_EXPECT_FALSE( perception.canSee( eye, forward, float3{ 5.5f, 0.0f, 12.5f }, &grid, false ) ); // 옆 90°
    SW_EXPECT_TRUE( perception.canSee( eye, forward, float3{ 4.5f, 0.0f, 5.5f }, &grid, false ) );   // 등 뒤지만 1 m
    SW_EXPECT_FALSE( perception.canSee( eye, forward, float3{ 14.0f, 0.0f, 5.5f }, &grid, false ) ); // 8.5 m — 7 m 넘음
    SW_EXPECT_TRUE( perception.canSee( eye, forward, float3{ 14.0f, 0.0f, 5.5f }, &grid, true ) );   // 보던 대상은 9 m 까지
    const float3 eyeNearWall{ 13.5f, 0.0f, 5.5f };
    SW_EXPECT_FALSE( perception.canSee( eyeNearWall, forward, float3{ 18.5f, 0.0f, 5.5f }, &grid, false ) ); // 벽 뒤

    vector<AiStimulus> listCandidate;
    listCandidate.push_back( AiStimulus{
        float3{ 18.5f, 0.0f, 5.5f },
        1u, 6.0f
    } ); // 벽 뒤에서 큰 소리
    listCandidate.push_back( AiStimulus{
        float3{ 10.5f, 0.0f, 6.5f },
        2u, 0.0f
    } ); // 조용히 — 벽 옆의 눈에는 등 뒤 3 m, 처음 눈에는 앞 5 m
    perception.sense( eyeNearWall, forward, listCandidate, &grid, 0.1f );
    const AiPerceivedTarget* pHeard = perception.findTarget( 1u );
    SW_ASSERT_NOT_NULL( pHeard );
    SW_EXPECT_TRUE( pHeard->_bHeard == SW_TRUE && pHeard->_bSeen == SW_FALSE );
    SW_EXPECT_TRUE( perception.findTarget( 2u ) == nullptr ); // 등 뒤 3 m(주변 감지 2 m 밖) — 안 보인다

    perception.sense( eye, forward, listCandidate, &grid, 0.1f );
    SW_ASSERT_NOT_NULL( perception.findNearestSeen( eye ) );
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( perception.findNearestSeen( eye )->_id ) );

    // 아무것도 감지되지 않으면 마지막 자리를 기억하다 2 초 뒤 잊는다.
    const vector<AiStimulus> listNothing;
    perception.sense( eye, forward, listNothing, &grid, 1.0f );
    SW_ASSERT_NOT_NULL( perception.findTarget( 2u ) );
    SW_EXPECT_TRUE( perception.findTarget( 2u )->_bSeen == SW_FALSE );
    SW_EXPECT_NEAR_EQUAL( 10.5f, perception.findTarget( 2u )->_lastKnownPosition._x, 1.0e-5f );
    perception.sense( eye, forward, listNothing, &grid, 1.5f );
    SW_EXPECT_TRUE( perception.findTarget( 2u ) == nullptr );
}

/**
 * @brief [AiTest] 타이머 — 한 번 · 반복(긴 프레임은 따라잡는다) · 멈춤 · 지우기 · 콜백 안에서 지우기
 */
SW_TEST_CASE( AiTest, TimerQueueFiresOnceRepeatsAndPauses )
{
    TimerQueue        queue;
    int32             onceCount   = 0;
    int32             repeatCount = 0;
    int32             pausedCount = 0;
    const TimerHandle once        = queue.schedule( 1.0f, SW_DELEGATE_LAMBDA( TimerQueue::Callback, [&onceCount]()
           { countTimer( &onceCount ); } ) );
    const TimerHandle repeat      = queue.schedule( 0.5f, SW_DELEGATE_LAMBDA( TimerQueue::Callback, [&repeatCount]()
         { countTimer( &repeatCount ); } ),
                                                    true );
    const TimerHandle paused      = queue.schedule( 0.5f, SW_DELEGATE_LAMBDA( TimerQueue::Callback, [&pausedCount]()
         { countTimer( &pausedCount ); } ) );
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( queue.schedule( 1.0f, TimerQueue::Callback{} ) ) );
    queue.setPaused( paused, true );

    (void)queue.advance( 0.4f );
    SW_EXPECT_EQUAL( 0, onceCount + repeatCount );
    (void)queue.advance( 0.7f ); // 1.1 초
    SW_EXPECT_EQUAL( 1, onceCount );
    SW_EXPECT_EQUAL( 2, repeatCount ); // 0.5 · 1.0
    SW_EXPECT_FALSE( queue.isActive( once ) );
    SW_EXPECT_NEAR_EQUAL( 0.4f, queue.getRemaining( repeat ), 1.0e-4f );
    (void)queue.advance( 1.0f ); // 2.1 초 — 1.5 · 2.0
    SW_EXPECT_EQUAL( 4, repeatCount );

    SW_EXPECT_EQUAL( 0, pausedCount );
    SW_EXPECT_NEAR_EQUAL( 0.5f, queue.getRemaining( paused ), 1.0e-4f );
    queue.setPaused( paused, false );
    (void)queue.advance( 0.6f );
    SW_EXPECT_EQUAL( 1, pausedCount );

    SW_EXPECT_TRUE( queue.cancel( repeat ) );
    SW_EXPECT_FALSE( queue.cancel( repeat ) );
    (void)queue.advance( 5.0f );
    SW_EXPECT_EQUAL( 5, repeatCount ); // 2.5 에 한 번 더 불린 뒤 지워졌다

    // 콜백이 자기 자신을 지운다.
    TimerHandle selfHandle = 0;
    int32       selfCount  = 0;
    selfHandle             = queue.schedule( 0.1f, SW_DELEGATE_LAMBDA( TimerQueue::Callback, [&queue, &selfHandle, &selfCount]()
                {
        ++selfCount;
        (void)queue.cancel( selfHandle );
    } ),
                                             true );
    (void)queue.advance( 1.0f );
    SW_EXPECT_EQUAL( 1, selfCount );
    SW_EXPECT_EQUAL( static_cast<size_t>( 0 ), queue.getCount() );
}
