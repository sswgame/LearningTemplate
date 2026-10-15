// 행동 트리 이동 노드 — 블랙보드 벡터로 공통 이동 인터페이스(`INavMover`)에 목적지를 요청하고, 그 상태를 노드 상태로 옮긴다(격자 · 내비메시 공통).
#include "pch.h"

#include "Engine/Navigation/INavMover.h"
#include "Engine/Object/Component/Navigation/NavMeshAgentComponent.h"
#include "Engine/Object/Component/Navigation/SceneNavigation.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "EngineTest/NavMeshTestUtil.h"

#include "GameFramework/Base/Actor/AI/BehaviorTree.h"
#include "GameFramework/Base/Actor/AI/Blackboard.h"

#include "TestFramework/TestFramework.h"
#include "TestFramework/TestTick.h"

namespace
{
    /** @brief 부른 횟수와 마지막 목적지를 적고, 상태는 테스트가 정하는 이동 인터페이스입니다. */
    class RecordingNavMover final : public sw::INavMover
    {
    public:
        [[nodiscard]] bool moveTo( const sw::float3& destination ) override
        {
            ++_moveCount;
            _destination = destination;
            _status      = _bAccept ? sw::NavMoveStatus::Moving : sw::NavMoveStatus::Failed;
            return _bAccept;
        }
        void stopMoving() override
        {
            ++_stopCount;
            _status = sw::NavMoveStatus::Idle;
        }
        sw::NavMoveStatus getMoveStatus() const override { return _status; }
        sw::float3        getMoveVelocity() const override { return sw::float3{}; }
        sw::float3        getMovePosition() const override { return sw::float3{}; }

        sw::float3        _destination{};
        sw::NavMoveStatus _status{ sw::NavMoveStatus::Idle };
        uint32            _moveCount{ 0 };
        uint32            _stopCount{ 0 };
        bool              _bAccept{ true };
    };
} // namespace

/**
 * @brief [BehaviorMoveToTest] 이동 노드가 블랙보드 목적지를 한 번 요청하고, 가는 동안 Running · 닿으면 성공 · 길이 없으면 실패이며,
 *        목적지가 다시 요청할 거리보다 멀리 바뀌면 다시 요청하고, 중단되면 멈춘다. 키나 이동 인터페이스가 없으면 실패한다
 */
SW_TEST_CASE( BehaviorMoveToTest, MoveToFollowsTheBlackboardAndReportsTheMover )
{
    const sw::hashed_string goalKey( "Goal" );
    sw::BehaviorTree        tree;
    const int32             root = tree.addSequence( -1, "Root" );
    SW_ASSERT_TRUE( tree.addMoveTo( root, goalKey, 0.5f ) >= 0 );
    SW_ASSERT_TRUE( tree.isValid() );

    sw::Blackboard         blackboard;
    RecordingNavMover      mover;
    sw::BehaviorTreeRunner runner;
    runner.initialize( &tree );

    // 이동 인터페이스가 없거나 키가 비었으면 실패.
    blackboard.setVector( goalKey, sw::float3{ 4.0f, 0.0f, 0.0f } );
    SW_EXPECT_TRUE( runner.tick( blackboard, nullptr, 0.1f ) == sw::BehaviorStatus::Failure );
    runner.setMover( &mover );
    sw::Blackboard emptyBoard;
    SW_EXPECT_TRUE( runner.tick( emptyBoard, nullptr, 0.1f ) == sw::BehaviorStatus::Failure );
    SW_EXPECT_EQUAL( 0u, mover._moveCount );

    // 요청 → 가는 중 → 조금 바뀌면 그대로 · 멀리 바뀌면 다시 요청 → 닿으면 성공.
    SW_EXPECT_TRUE( runner.tick( blackboard, nullptr, 0.1f ) == sw::BehaviorStatus::Running );
    SW_EXPECT_EQUAL( 1u, mover._moveCount );
    SW_EXPECT_TRUE( ( mover._destination - sw::float3{ 4.0f, 0.0f, 0.0f } ).getLength() < 1.0e-4f );
    SW_EXPECT_TRUE( runner.tick( blackboard, nullptr, 0.1f ) == sw::BehaviorStatus::Running );
    blackboard.setVector( goalKey, sw::float3{ 4.2f, 0.0f, 0.0f } );
    SW_EXPECT_TRUE( runner.tick( blackboard, nullptr, 0.1f ) == sw::BehaviorStatus::Running );
    SW_EXPECT_EQUAL( 1u, mover._moveCount );
    blackboard.setVector( goalKey, sw::float3{ 8.0f, 0.0f, 0.0f } );
    SW_EXPECT_TRUE( runner.tick( blackboard, nullptr, 0.1f ) == sw::BehaviorStatus::Running );
    SW_EXPECT_EQUAL( 2u, mover._moveCount );
    mover._status = sw::NavMoveStatus::Arrived;
    SW_EXPECT_TRUE( runner.tick( blackboard, nullptr, 0.1f ) == sw::BehaviorStatus::Success );

    // 길이 없으면 실패 — 거절(moveTo false)도, 나중에 알린 실패도.
    mover._bAccept = false;
    SW_EXPECT_TRUE( runner.tick( blackboard, nullptr, 0.1f ) == sw::BehaviorStatus::Failure );
    mover._bAccept = true;
    SW_EXPECT_TRUE( runner.tick( blackboard, nullptr, 0.1f ) == sw::BehaviorStatus::Running );
    mover._status = sw::NavMoveStatus::Failed;
    SW_EXPECT_TRUE( runner.tick( blackboard, nullptr, 0.1f ) == sw::BehaviorStatus::Failure );

    // 가는 중에 중단하면 멈춘다.
    SW_EXPECT_TRUE( runner.tick( blackboard, nullptr, 0.1f ) == sw::BehaviorStatus::Running );
    const uint32 stopCount = mover._stopCount;
    runner.reset();
    SW_EXPECT_EQUAL( stopCount + 1, mover._stopCount );
}

/**
 * @brief [BehaviorMoveToTest] 이동 노드가 내비메시 에이전트(`NavMeshAgentComponent::getMover`)로 상자 벽을 돌아 블랙보드 목적지에 닿고 성공한다
 */
SW_TEST_CASE( BehaviorMoveToTest, MoveToDrivesANavMeshAgent )
{
    sw::GameObjectManager manager;
    navtest::spawnPhysicsCrateScene( manager );
    sw::GameObject*     pWalker = manager.createGameObject( sw::hashed_string( "Walker" ) );
    sw::SceneComponent* pRoot   = pWalker->addComponent<sw::SceneComponent>();
    pRoot->setLocalPosition( sw::float3{ -6.0f, 0.0f, 0.0f } );
    sw::NavMeshAgentComponent* pAgent = pWalker->addComponent<sw::NavMeshAgentComponent>();
    pAgent->setMaxSpeed( 4.0f );

    const sw::hashed_string goalKey( "Goal" );
    sw::BehaviorTree        tree;
    const int32             root = tree.addSequence( -1, "Root" );
    SW_ASSERT_TRUE( tree.addMoveTo( root, goalKey ) >= 0 );
    sw::Blackboard         blackboard;
    sw::BehaviorTreeRunner runner;
    runner.initialize( &tree );
    runner.setMover( &pAgent->getMover() );
    const sw::float3 goal{ 6.0f, 0.0f, 0.0f };
    blackboard.setVector( goalKey, goal );

    manager.beginPlay();
    test::updateFrames( manager, 2 );
    sw::BehaviorStatus status = sw::BehaviorStatus::Running;
    for ( uint32 frame = 0; frame < 600 && status == sw::BehaviorStatus::Running; ++frame )
    {
        status = runner.tick( blackboard, pWalker, 1.0f / 60.0f );
        manager.update( 1.0f / 60.0f );
        SW_ASSERT_FALSE( navtest::isInsideCrates( pRoot->getWorldPosition() ) );
    }
    SW_EXPECT_TRUE( status == sw::BehaviorStatus::Success );
    SW_EXPECT_TRUE( sw::float3::getHorizontalDistance( pRoot->getWorldPosition(), goal ) < 0.6f );
    manager.endPlay();
}
