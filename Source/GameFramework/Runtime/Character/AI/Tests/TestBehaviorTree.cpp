#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Navigation/INavMover.h"

#include "GameFramework/Runtime/Character/AI/BehaviorTree.h"
#include "GameFramework/Runtime/Character/AI/Blackboard.h"

using namespace sw;

namespace
{
    /** @brief 작업이 돌려줄 값과 불린 기록입니다. 러너가 행위자로 넘깁니다. */
    struct BehaviorProbe
    {
        BehaviorStatus _arrResult[32]{};
        uint32         _arrTick[32]{};
        uint32         _arrStart[32]{};
        uint32         _arrAbort[32]{};

        static BehaviorStatus run( BehaviorContext& context )
        {
            BehaviorProbe& probe = *static_cast<BehaviorProbe*>( context._pOwner );
            const int32    index = context._nodeIndex;
            if ( context._bAborted != SW_FALSE )
            {
                ++probe._arrAbort[index];
                return BehaviorStatus::Failure;
            }
            ++probe._arrTick[index];
            if ( context._bJustStarted != SW_FALSE )
                ++probe._arrStart[index];
            return probe._arrResult[index];
        }
    };

    /** @brief 트리 하나와 그 러너입니다. */
    struct BehaviorFixture
    {
        BehaviorTree       _tree;
        BehaviorTreeRunner _runner;
        Blackboard         _blackboard;
        BehaviorProbe      _probe;

        void           build() { _runner.initialize( &_tree ); }
        BehaviorStatus tick( float32 deltaTime = 0.1f ) { return _runner.tick( _blackboard, &_probe, deltaTime ); }
        int32          addAction( int32 parent, BehaviorStatus result, const utf8* pName = "Action" )
        {
            const int32 index                               = _tree.addAction( parent, pName, &BehaviorProbe::run ).value();
            _probe._arrResult[static_cast<size_t>( index )] = result;
            return index;
        }
    };

    /** @brief 이동 인터페이스 흉내입니다. 시킨 일을 기록합니다. */
    class FakeMover final : public INavMover
    {
    public:
        NavMoveStatus _status{ NavMoveStatus::Idle };
        float3        _destination{};
        uint32        _moveCount{ 0 };
        uint32        _stopCount{ 0 };
        uint8         _bRefuse : 1;

        FakeMover()
            : _bRefuse{ SW_FALSE }
        {
        }

        bool moveTo( const float3& destination ) override
        {
            ++_moveCount;
            _destination = destination;
            _status      = NavMoveStatus::Moving;
            return _bRefuse == SW_FALSE;
        }
        void stopMoving() override
        {
            ++_stopCount;
            _status = NavMoveStatus::Idle;
        }
        NavMoveStatus getMoveStatus() const override { return _status; }
        float3        getMoveVelocity() const override { return float3{}; }
        float3        getMovePosition() const override { return float3{}; }
    };
} // namespace

// ---------------------------------------------------------------------- 트리 만들기

SW_TEST_CASE( BehaviorTreeTest, BuildingRefusesASecondRootAndUnknownParents )
{
    BehaviorTree tree;
    const int32  root = tree.addSequence( nullopt ).value();
    SW_EXPECT_EQUAL( 0, root );
    SW_EXPECT_FALSE( tree.addSelector( nullopt ).has_value() );
    SW_EXPECT_FALSE( tree.addWait( 99, 1.0f ).has_value() );
    SW_EXPECT_EQUAL( 1, tree.getNodeCount() );
}

SW_TEST_CASE( BehaviorTreeTest, ValidityChecksEveryNodeShape )
{
    BehaviorTree empty;
    SW_EXPECT_FALSE( empty.isValid() );

    BehaviorTree noChildren;
    (void)noChildren.addSequence( nullopt );
    SW_EXPECT_FALSE( noChildren.isValid() );

    BehaviorTree noAction;
    (void)noAction.addAction( nullopt, "A", nullptr );
    SW_EXPECT_FALSE( noAction.isValid() );

    BehaviorTree bareDecorator;
    (void)bareDecorator.addInverter( nullopt );
    SW_EXPECT_FALSE( bareDecorator.isValid() );

    BehaviorTree twoUnderDecorator;
    const int32  inverter = twoUnderDecorator.addInverter( nullopt ).value();
    (void)twoUnderDecorator.addWait( inverter, 1.0f );
    SW_EXPECT_TRUE( twoUnderDecorator.isValid() );
    (void)twoUnderDecorator.addWait( inverter, 1.0f );
    SW_EXPECT_FALSE( twoUnderDecorator.isValid() );

    BehaviorTree childUnderLeaf;
    const int32  wait = childUnderLeaf.addWait( nullopt, 1.0f ).value();
    (void)childUnderLeaf.addWait( wait, 1.0f );
    SW_EXPECT_FALSE( childUnderLeaf.isValid() );

    BehaviorTree moveWithoutKey;
    (void)moveWithoutKey.addMoveTo( nullopt, hashed_string{} );
    SW_EXPECT_FALSE( moveWithoutKey.isValid() );
}

SW_TEST_CASE( BehaviorTreeTest, NoTreeFails )
{
    BehaviorTreeRunner runner;
    Blackboard         blackboard;
    SW_EXPECT_TRUE( runner.tick( blackboard, nullptr, 0.1f ) == BehaviorStatus::Failure );
    SW_EXPECT_TRUE( string_view( runner.getActiveLeafName() ).empty() );
    runner.reset(); // 트리가 없어도 괜찮다
}

// ---------------------------------------------------------------------- 합성 노드

SW_TEST_CASE( BehaviorTreeTest, SequenceStopsAtTheFirstFailure )
{
    BehaviorFixture rig;
    const int32     root = rig._tree.addSequence( nullopt ).value();
    const int32     a    = rig.addAction( root, BehaviorStatus::Success );
    const int32     b    = rig.addAction( root, BehaviorStatus::Failure );
    const int32     c    = rig.addAction( root, BehaviorStatus::Success );
    rig.build();
    SW_EXPECT_TRUE( rig.tick() == BehaviorStatus::Failure );
    SW_EXPECT_EQUAL( 1u, rig._probe._arrTick[a] );
    SW_EXPECT_EQUAL( 1u, rig._probe._arrTick[b] );
    SW_EXPECT_EQUAL( 0u, rig._probe._arrTick[c] );
}

// 돌던 자식부터 이어 돈다 — 앞의 성공한 자식을 다시 부르지 않는다.
SW_TEST_CASE( BehaviorTreeTest, SequenceResumesAtTheRunningChild )
{
    BehaviorFixture rig;
    const int32     root = rig._tree.addSequence( nullopt ).value();
    const int32     a    = rig.addAction( root, BehaviorStatus::Success );
    const int32     b    = rig.addAction( root, BehaviorStatus::Running );
    rig.build();
    SW_EXPECT_TRUE( rig.tick() == BehaviorStatus::Running );
    SW_EXPECT_TRUE( rig.tick() == BehaviorStatus::Running );
    SW_EXPECT_EQUAL( 1u, rig._probe._arrTick[a] );
    SW_EXPECT_EQUAL( 2u, rig._probe._arrTick[b] );
    SW_EXPECT_EQUAL( 1u, rig._probe._arrStart[b] ); // 처음 한 번만 시작이다
    SW_EXPECT_TRUE( rig._runner.getActiveLeaf() == b );

    rig._probe._arrResult[b] = BehaviorStatus::Success;
    SW_EXPECT_TRUE( rig.tick() == BehaviorStatus::Success );
    SW_EXPECT_TRUE( rig.tick() == BehaviorStatus::Success ); // 끝나면 처음부터 다시 돈다
    SW_EXPECT_EQUAL( 2u, rig._probe._arrTick[a] );
    SW_EXPECT_EQUAL( 2u, rig._probe._arrStart[b] );
}

SW_TEST_CASE( BehaviorTreeTest, SelectorTakesTheFirstThatDoesNotFail )
{
    BehaviorFixture rig;
    const int32     root = rig._tree.addSelector( nullopt ).value();
    const int32     a    = rig.addAction( root, BehaviorStatus::Failure );
    const int32     b    = rig.addAction( root, BehaviorStatus::Success );
    const int32     c    = rig.addAction( root, BehaviorStatus::Success );
    rig.build();
    SW_EXPECT_TRUE( rig.tick() == BehaviorStatus::Success );
    SW_EXPECT_EQUAL( 1u, rig._probe._arrTick[a] );
    SW_EXPECT_EQUAL( 1u, rig._probe._arrTick[b] );
    SW_EXPECT_EQUAL( 0u, rig._probe._arrTick[c] );

    rig._probe._arrResult[b] = BehaviorStatus::Failure;
    rig._probe._arrResult[c] = BehaviorStatus::Failure;
    SW_EXPECT_TRUE( rig.tick() == BehaviorStatus::Failure ); // 모두 실패하면 실패다
}

// 보통 셀렉터는 돌던 자식을 이어 돌리고 앞 자식을 다시 보지 않는다.
SW_TEST_CASE( BehaviorTreeTest, PlainSelectorKeepsItsRunningChild )
{
    BehaviorFixture rig;
    const int32     root = rig._tree.addSelector( nullopt ).value();
    const int32     a    = rig.addAction( root, BehaviorStatus::Failure );
    const int32     b    = rig.addAction( root, BehaviorStatus::Running );
    rig.build();
    SW_EXPECT_TRUE( rig.tick() == BehaviorStatus::Running );
    rig._probe._arrResult[a] = BehaviorStatus::Success;
    SW_EXPECT_TRUE( rig.tick() == BehaviorStatus::Running );
    SW_EXPECT_EQUAL( 1u, rig._probe._arrTick[a] );
    SW_EXPECT_EQUAL( 0u, rig._probe._arrAbort[b] );
}

// 반응 셀렉터는 매 틱 앞부터 다시 보고, 앞 자식이 살아나면 돌던 자식을 중단한다.
SW_TEST_CASE( BehaviorTreeTest, ReactiveSelectorPreemptsWithAnAbort )
{
    BehaviorFixture rig;
    const int32     root = rig._tree.addSelector( nullopt, "Root", true ).value();
    const int32     a    = rig.addAction( root, BehaviorStatus::Failure, "High" );
    const int32     b    = rig.addAction( root, BehaviorStatus::Running, "Low" );
    rig.build();
    SW_EXPECT_TRUE( rig.tick() == BehaviorStatus::Running );
    SW_EXPECT_TRUE( string_view( rig._runner.getActiveLeafName() ) == "Low" );
    rig.tick();
    SW_EXPECT_EQUAL( 2u, rig._probe._arrTick[a] );  // 앞 자식을 매번 본다
    SW_EXPECT_EQUAL( 1u, rig._probe._arrStart[b] ); // 돌던 자식은 다시 시작하지 않는다

    rig._probe._arrResult[a] = BehaviorStatus::Running;
    SW_EXPECT_TRUE( rig.tick() == BehaviorStatus::Running );
    SW_EXPECT_EQUAL( 1u, rig._probe._arrAbort[b] );
    SW_EXPECT_TRUE( string_view( rig._runner.getActiveLeafName() ) == "High" );
    SW_EXPECT_FALSE( rig._runner.isNodeActive( b ) );
}

// ---------------------------------------------------------------------- 장식

SW_TEST_CASE( BehaviorTreeTest, InverterAndForceSuccessKeepRunning )
{
    BehaviorFixture rig;
    const int32     root = rig._tree.addInverter( nullopt ).value();
    const int32     a    = rig.addAction( root, BehaviorStatus::Success );
    rig.build();
    SW_EXPECT_TRUE( rig.tick() == BehaviorStatus::Failure );
    rig._probe._arrResult[a] = BehaviorStatus::Failure;
    SW_EXPECT_TRUE( rig.tick() == BehaviorStatus::Success );
    rig._probe._arrResult[a] = BehaviorStatus::Running;
    SW_EXPECT_TRUE( rig.tick() == BehaviorStatus::Running );

    BehaviorFixture force;
    const int32     forceRoot = force._tree.addForceSuccess( nullopt ).value();
    const int32     b         = force.addAction( forceRoot, BehaviorStatus::Failure );
    force.build();
    SW_EXPECT_TRUE( force.tick() == BehaviorStatus::Success );
    force._probe._arrResult[b] = BehaviorStatus::Running;
    SW_EXPECT_TRUE( force.tick() == BehaviorStatus::Running );
}

// 한 틱에 한 번만 돈다 — 바로 끝나는 자식이 한 틱 안에서 끝없이 돌지 않는다.
SW_TEST_CASE( BehaviorTreeTest, RepeatRunsOncePerTickUpToItsCount )
{
    BehaviorFixture rig;
    const int32     root = rig._tree.addRepeat( nullopt, 3 ).value();
    const int32     a    = rig.addAction( root, BehaviorStatus::Success );
    rig.build();
    SW_EXPECT_TRUE( rig.tick() == BehaviorStatus::Running );
    SW_EXPECT_EQUAL( 1u, rig._probe._arrTick[a] );
    SW_EXPECT_TRUE( rig.tick() == BehaviorStatus::Running );
    SW_EXPECT_TRUE( rig.tick() == BehaviorStatus::Success );
    SW_EXPECT_EQUAL( 3u, rig._probe._arrStart[a] );
    SW_EXPECT_TRUE( rig.tick() == BehaviorStatus::Running ); // 끝나면 수를 다시 센다
}

SW_TEST_CASE( BehaviorTreeTest, RepeatForeverEndsOnlyOnFailure )
{
    BehaviorFixture rig;
    const int32     root = rig._tree.addRepeat( nullopt, 0 ).value();
    const int32     a    = rig.addAction( root, BehaviorStatus::Success );
    rig.build();
    for ( int32 index = 0; index < 5; ++index )
    {
        SW_EXPECT_TRUE( rig.tick() == BehaviorStatus::Running );
    }
    rig._probe._arrResult[a] = BehaviorStatus::Failure;
    SW_EXPECT_TRUE( rig.tick() == BehaviorStatus::Failure );
}

SW_TEST_CASE( BehaviorTreeTest, CooldownFailsUntilItsTimePassed )
{
    BehaviorFixture rig;
    const int32     root = rig._tree.addCooldown( nullopt, 1.0f ).value();
    const int32     a    = rig.addAction( root, BehaviorStatus::Success );
    rig.build();
    SW_EXPECT_TRUE( rig.tick( 0.1f ) == BehaviorStatus::Success );
    SW_EXPECT_TRUE( rig.tick( 0.5f ) == BehaviorStatus::Failure );
    SW_EXPECT_EQUAL( 1u, rig._probe._arrTick[a] );
    SW_EXPECT_TRUE( rig.tick( 0.6f ) == BehaviorStatus::Success );
    SW_EXPECT_EQUAL( 2u, rig._probe._arrTick[a] );

    // 도는 동안에는 쿨다운이 시작되지 않는다.
    rig._probe._arrResult[a] = BehaviorStatus::Running;
    rig._runner.reset();
    SW_EXPECT_TRUE( rig.tick( 2.0f ) == BehaviorStatus::Running );
    SW_EXPECT_TRUE( rig.tick( 2.0f ) == BehaviorStatus::Running );
}

SW_TEST_CASE( BehaviorTreeTest, TimeLimitAbortsTheChildAndFails )
{
    BehaviorFixture rig;
    const int32     root = rig._tree.addTimeLimit( nullopt, 1.0f ).value();
    const int32     a    = rig.addAction( root, BehaviorStatus::Running );
    rig.build();
    SW_EXPECT_TRUE( rig.tick( 0.6f ) == BehaviorStatus::Running );
    SW_EXPECT_TRUE( rig.tick( 0.6f ) == BehaviorStatus::Failure );
    SW_EXPECT_EQUAL( 1u, rig._probe._arrAbort[a] );
    SW_EXPECT_TRUE( rig.tick( 0.6f ) == BehaviorStatus::Running ); // 다음 번은 시간을 새로 잰다
    SW_EXPECT_EQUAL( 2u, rig._probe._arrStart[a] );
}

// ---------------------------------------------------------------------- 블랙보드 조건

SW_TEST_CASE( BehaviorTreeTest, BlackboardComparisonsNeedAValue )
{
    Blackboard          blackboard;
    const hashed_string key( "Hp" );
    GameplayContext     world;
    world.set<BlackboardSlot>( &blackboard );

    const auto check = [&]( BlackboardCompare compare )
    {
        BehaviorTree tree;
        const int32  root  = tree.addSelector( nullopt, "Root" ).value();
        const int32  index = tree.addBlackboardCondition( root, key, compare, 5.0f ).value();
        return BehaviorTree::evaluateCondition( tree.getNode( index ), world );
    };
    SW_EXPECT_FALSE( check( BlackboardCompare::IsSet ) );
    SW_EXPECT_TRUE( check( BlackboardCompare::IsNotSet ) );
    SW_EXPECT_FALSE( check( BlackboardCompare::Less ) ); // 없는 키를 0 으로 보지 않는다
    SW_EXPECT_FALSE( check( BlackboardCompare::NotEqual ) );

    blackboard.setFloat( key, 5.0f );
    SW_EXPECT_TRUE( check( BlackboardCompare::IsSet ) );
    SW_EXPECT_TRUE( check( BlackboardCompare::Equal ) );
    SW_EXPECT_FALSE( check( BlackboardCompare::NotEqual ) );
    SW_EXPECT_TRUE( check( BlackboardCompare::LessOrEqual ) );
    SW_EXPECT_TRUE( check( BlackboardCompare::GreaterOrEqual ) );
    SW_EXPECT_FALSE( check( BlackboardCompare::Less ) );
    SW_EXPECT_FALSE( check( BlackboardCompare::Greater ) );

    blackboard.setInt( key, 9 );
    SW_EXPECT_TRUE( check( BlackboardCompare::Greater ) );
    blackboard.clearValue( key );
    SW_EXPECT_TRUE( check( BlackboardCompare::IsNotSet ) );
}

SW_TEST_CASE( BehaviorTreeTest, ConditionGuardsItsChildWithoutAbortMode )
{
    BehaviorFixture     rig;
    const hashed_string key( "Enemy" );
    const int32         root = rig._tree.addBlackboardCondition( nullopt, key, BlackboardCompare::IsSet ).value();
    const int32         a    = rig.addAction( root, BehaviorStatus::Running );
    rig.build();
    SW_EXPECT_TRUE( rig.tick() == BehaviorStatus::Failure );
    SW_EXPECT_EQUAL( 0u, rig._probe._arrTick[a] );

    rig._blackboard.setBool( key, true );
    SW_EXPECT_TRUE( rig.tick() == BehaviorStatus::Running );
    rig._blackboard.clearValue( key );
    SW_EXPECT_TRUE( rig.tick() == BehaviorStatus::Running ); // 관찰하지 않으면 시작할 때만 본다
    SW_EXPECT_EQUAL( 0u, rig._probe._arrAbort[a] );
}

SW_TEST_CASE( BehaviorTreeTest, SelfAbortStopsTheChildWhenTheConditionBreaks )
{
    BehaviorFixture     rig;
    const hashed_string key( "Enemy" );
    const int32         root = rig._tree.addBlackboardCondition( nullopt, key, BlackboardCompare::IsSet, 0.0f, BehaviorAbortMode::Self ).value();
    const int32         a    = rig.addAction( root, BehaviorStatus::Running );
    rig.build();
    rig._blackboard.setBool( key, true );
    SW_EXPECT_TRUE( rig.tick() == BehaviorStatus::Running );
    rig._blackboard.clearValue( key );
    SW_EXPECT_TRUE( rig.tick() == BehaviorStatus::Failure );
    SW_EXPECT_EQUAL( 1u, rig._probe._arrAbort[a] );
}

// 앞 가지의 조건이 참이 되면 도는 뒤 가지를 중단하고 앞 가지를 고른다.
SW_TEST_CASE( BehaviorTreeTest, LowerPriorityAbortLetsAHigherBranchIn )
{
    BehaviorFixture     rig;
    const hashed_string key( "Enemy" );
    const int32         root  = rig._tree.addSelector( nullopt ).value();
    const int32         guard = rig._tree.addBlackboardCondition( root, key, BlackboardCompare::IsSet, 0.0f, BehaviorAbortMode::LowerPriority ).value();
    const int32         fight = rig.addAction( guard, BehaviorStatus::Running, "Fight" );
    const int32         walk  = rig.addAction( root, BehaviorStatus::Running, "Walk" );
    rig.build();
    SW_EXPECT_TRUE( rig.tick() == BehaviorStatus::Running );
    SW_EXPECT_TRUE( string_view( rig._runner.getActiveLeafName() ) == "Walk" );
    rig.tick();
    SW_EXPECT_EQUAL( 1u, rig._probe._arrStart[walk] );

    rig._blackboard.setBool( key, true );
    SW_EXPECT_TRUE( rig.tick() == BehaviorStatus::Running );
    SW_EXPECT_EQUAL( 1u, rig._probe._arrAbort[walk] );
    SW_EXPECT_EQUAL( 1u, rig._probe._arrStart[fight] );
    SW_EXPECT_TRUE( string_view( rig._runner.getActiveLeafName() ) == "Fight" );
}

SW_TEST_CASE( BehaviorTreeTest, ResetAbortsWhatIsRunning )
{
    BehaviorFixture rig;
    const int32     root = rig._tree.addSequence( nullopt ).value();
    const int32     a    = rig.addAction( root, BehaviorStatus::Success );
    const int32     b    = rig.addAction( root, BehaviorStatus::Running );
    rig.build();
    rig.tick();
    rig._runner.reset();
    SW_EXPECT_EQUAL( 1u, rig._probe._arrAbort[b] );
    SW_EXPECT_FALSE( rig._runner.getActiveLeaf().has_value() );
    rig.tick();
    SW_EXPECT_EQUAL( 2u, rig._probe._arrTick[a] ); // 처음부터 다시 돈다
}

SW_TEST_CASE( BehaviorTreeTest, WaitLastsItsSeconds )
{
    BehaviorFixture rig;
    (void)rig._tree.addWait( nullopt, 1.0f );
    rig.build();
    SW_EXPECT_TRUE( rig.tick( 0.6f ) == BehaviorStatus::Running );
    SW_EXPECT_TRUE( rig._runner.getActiveLeaf() == 0 );
    SW_EXPECT_TRUE( rig.tick( 0.6f ) == BehaviorStatus::Success );
    SW_EXPECT_TRUE( rig.tick( 0.6f ) == BehaviorStatus::Running ); // 새로 잰다
}

// ---------------------------------------------------------------------- 이동

SW_TEST_CASE( BehaviorMoveToTest, NeedsAMoverAndATarget )
{
    BehaviorFixture     rig;
    const hashed_string key( "Goal" );
    (void)rig._tree.addMoveTo( nullopt, key );
    rig.build();
    SW_EXPECT_TRUE( rig.tick() == BehaviorStatus::Failure ); // 이동 인터페이스가 없다
    FakeMover mover;
    rig._runner.setMover( &mover );
    SW_EXPECT_TRUE( rig.tick() == BehaviorStatus::Failure ); // 목적지가 없다
    SW_EXPECT_EQUAL( 0u, mover._moveCount );

    rig._blackboard.setVector( key, float3{ 1.0f, 0.0f, 0.0f } );
    mover._bRefuse = SW_TRUE;
    SW_EXPECT_TRUE( rig.tick() == BehaviorStatus::Failure ); // 요청을 거절했다
}

// 요청한 틱의 상태는 옛 요청의 것이다 — 바로 읽으면 지난번 도착을 이번 도착으로 본다.
SW_TEST_CASE( BehaviorMoveToTest, StatusIsReadFromTheNextTick )
{
    BehaviorFixture     rig;
    const hashed_string key( "Goal" );
    (void)rig._tree.addMoveTo( nullopt, key );
    rig.build();
    FakeMover mover;
    rig._runner.setMover( &mover );
    rig._blackboard.setVector( key, float3{ 1.0f, 0.0f, 0.0f } );
    SW_EXPECT_TRUE( rig.tick() == BehaviorStatus::Running );
    SW_EXPECT_EQUAL( 1u, mover._moveCount );
    SW_EXPECT_NEAR( 1.0f, mover._destination._x, 1e-5f );
    SW_EXPECT_TRUE( rig.tick() == BehaviorStatus::Running );

    mover._status = NavMoveStatus::Arrived;
    SW_EXPECT_TRUE( rig.tick() == BehaviorStatus::Success );

    SW_EXPECT_TRUE( rig.tick() == BehaviorStatus::Running ); // 다시 요청한다
    SW_EXPECT_EQUAL( 2u, mover._moveCount );
    mover._status = NavMoveStatus::Failed;
    SW_EXPECT_TRUE( rig.tick() == BehaviorStatus::Failure );
}

SW_TEST_CASE( BehaviorMoveToTest, RepathsOnlyWhenTheTargetMovesFarEnough )
{
    BehaviorFixture     rig;
    const hashed_string key( "Goal" );
    (void)rig._tree.addMoveTo( nullopt, key, 1.0f );
    rig.build();
    FakeMover mover;
    rig._runner.setMover( &mover );
    rig._blackboard.setVector( key, float3{} );
    (void)rig.tick();
    rig._blackboard.setVector( key, float3{ 0.5f, 0.0f, 0.0f } );
    (void)rig.tick();
    SW_EXPECT_EQUAL( 1u, mover._moveCount );
    rig._blackboard.setVector( key, float3{ 2.0f, 0.0f, 0.0f } );
    SW_EXPECT_TRUE( rig.tick() == BehaviorStatus::Running );
    SW_EXPECT_EQUAL( 2u, mover._moveCount );
}

SW_TEST_CASE( BehaviorMoveToTest, AbortStopsTheMover )
{
    BehaviorFixture     rig;
    const hashed_string key( "Goal" );
    (void)rig._tree.addMoveTo( nullopt, key );
    rig.build();
    FakeMover mover;
    rig._runner.setMover( &mover );
    rig._blackboard.setVector( key, float3{} );
    (void)rig.tick();
    rig._runner.reset();
    SW_EXPECT_EQUAL( 1u, mover._stopCount );
}

// 도는 중에 목적지가 사라지면 트리만 실패로 넘어가고 몸은 옛 목적지로 계속 가면 안 된다.
SW_TEST_CASE( BehaviorMoveToTest, LosingTheTargetStopsTheMover )
{
    BehaviorFixture     rig;
    const hashed_string key( "Goal" );
    (void)rig._tree.addMoveTo( nullopt, key );
    rig.build();
    FakeMover mover;
    rig._runner.setMover( &mover );
    rig._blackboard.setVector( key, float3{} );
    SW_REQUIRE_TRUE( rig.tick() == BehaviorStatus::Running );
    rig._blackboard.clearValue( key );
    SW_EXPECT_TRUE( rig.tick() == BehaviorStatus::Failure );
    SW_EXPECT_EQUAL( 1u, mover._stopCount );
    SW_EXPECT_TRUE( mover._status == NavMoveStatus::Idle );
}

/** @brief [BlackboardTest] 디버거 줄: 칸 이름은 이름순, 값은 종류에 맞는 글이다 */
SW_TEST_CASE( BlackboardTest, KeysAndTextForTheDebugger )
{
    Blackboard blackboard;
    blackboard.setInt( "zeta", 3 );
    blackboard.setBool( "alpha", true );
    blackboard.setObject( "mid", 42 );
    vector<hashed_string> listKey;
    blackboard.collectKeys( listKey );
    SW_REQUIRE_EQUAL( size_t( 3 ), listKey.size() );
    SW_EXPECT_TRUE( listKey[0] == hashed_string( "alpha" ) );
    SW_EXPECT_TRUE( listKey[2] == hashed_string( "zeta" ) );
    SW_EXPECT_EQUAL( string( "true" ), blackboard.findValue( "alpha" )->formatText() );
    SW_EXPECT_EQUAL( string( "3" ), blackboard.findValue( "zeta" )->formatText() );
    SW_EXPECT_EQUAL( string( "#42" ), blackboard.findValue( "mid" )->formatText() );
}
