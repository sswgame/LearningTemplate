#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Inherit/DataInheritance.h"

#include "GameFramework/Runtime/Character/AI/BehaviorTreeAsset.h"
#include "GameFramework/Runtime/Character/AI/Blackboard.h"
#include "GameFramework/Runtime/Character/Control/Controller/AIControllerActions.h"
#include "GameFramework/Runtime/Character/Control/Controller/AIControllerComponent.h"

using namespace sw;

// AIControllerTreeTest — AI 조종자의 판단을 데이터 트리로 옮긴 모양(nogpu): 동작 어휘 `ai.*` · `blackboard.add` · `blackboard.copy` 가 블랙보드 값으로 조종자의 목표를 정하고,
// 판단마다 루트부터 다시 고르는 트리가 옛 상태 기계 `switch` 와 같은 결과를 낸다. 칸이 틀린 트리는 읽기 오류다.

namespace
{
    /** @brief 하위 트리 파일이 없는 트리만 읽는다. */
    class XTestSource final : public IDataInheritanceSource
    {
    public:
        [[nodiscard]] bool readText( string_view assetID, string& outText ) override
        {
            (void)assetID;
            (void)outText;
            return false;
        }
    };

    struct XTestUtil
    {
        /** @brief AbilityArena 근접 적 트리(`game/abilityarena/data/ai/grunt.bt.xml`)와 같은 모양입니다. */
        static constexpr const utf8* kGruntTree = R"XML(<BehaviorTree>
  <Selector name="Grunt">
    <Sequence name="NoTarget">
      <Condition test="!hasBlackboard(TargetPosition)"/>
      <Action task="ai.state" state="Idle"/>
      <Action task="ai.stop"/>
      <Action task="ai.clearFocus"/>
    </Sequence>
    <Sequence name="Fight">
      <Action task="ai.focus" key="TargetPosition"/>
      <Selector name="Approach">
        <Sequence name="Chase">
          <Condition test="blackboard(TargetDistance) > 1.4"/>
          <Action task="ai.state" state="Chase"/>
          <Action task="ai.moveTo" key="TargetPosition"/>
        </Sequence>
        <Sequence name="Strike">
          <Action task="ai.state" state="Strike"/>
          <Action task="ai.stop"/>
        </Sequence>
      </Selector>
    </Sequence>
  </Selector>
</BehaviorTree>)XML";

        /** @brief 조종자를 칸으로 넘겨 트리를 한 번 돌립니다(조종자의 `think` 와 같은 문맥). */
        static BehaviorStatus tickOnce( const BehaviorTree& tree, AIControllerComponent& controller, BehaviorTreeRunner& runner )
        {
            GameplayContext world;
            world.set<AIControllerSlot>( &controller );
            runner.initialize( &tree );
            runner.setGameplayContext( world );
            return runner.tick( controller.getBlackboard(), &controller, 0.1f );
        }
    };
} // namespace

/** @brief [AIControllerTreeTest] 블랙보드 거리로 쫓기와 때리기를 고르고, 대상이 없으면 멈춘다 — 옛 `ArenaEnemyAIComponent::think` 의 갈래와 같다 */
SW_TEST_CASE( AIControllerTreeTest, TreeChoosesChaseOrStrikeFromTheBlackboard )
{
    XTestSource       source;
    BehaviorTreeAsset asset;
    SW_REQUIRE_TRUE( asset.loadFromXMLText( XTestUtil::kGruntTree, "grunt.bt.xml", source ) );

    AIControllerComponent controller;
    BehaviorTreeRunner    runner;
    Blackboard&           blackboard = controller.getBlackboard();
    blackboard.setVector( "TargetPosition", float3{ 5.0f, 0.0f, 0.0f } );
    blackboard.setFloat( "TargetDistance", 5.0f );
    SW_EXPECT_TRUE( XTestUtil::tickOnce( asset.getTree(), controller, runner ) == BehaviorStatus::Success );
    SW_EXPECT_TRUE( controller.getStateName() == hashed_string( "Chase" ) );
    SW_EXPECT_TRUE( controller.hasDestination() );
    SW_EXPECT_TRUE( ( controller.getDestination() == float3{ 5.0f, 0.0f, 0.0f } ) );

    blackboard.setFloat( "TargetDistance", 1.0f );
    (void)XTestUtil::tickOnce( asset.getTree(), controller, runner );
    SW_EXPECT_TRUE( controller.getStateName() == hashed_string( "Strike" ) );
    SW_EXPECT_FALSE( controller.hasDestination() );

    blackboard.clearValue( "TargetPosition" );
    (void)XTestUtil::tickOnce( asset.getTree(), controller, runner );
    SW_EXPECT_TRUE( controller.getStateName() == hashed_string( "Idle" ) );
}

/** @brief [AIControllerTreeTest] `ai.moveTo` 는 목표가 걸린 목적지에서 `repathDistance` 안이면 다시 걸지 않고, `ai.moveAway` 는 수평으로 반대쪽에 선다 */
SW_TEST_CASE( AIControllerTreeTest, MoveActionsKeepOrReplaceTheDestination )
{
    XTestSource       source;
    BehaviorTreeAsset chase;
    SW_REQUIRE_TRUE( chase.loadFromXMLText( R"(<BehaviorTree><Action task="ai.moveTo" key="Goal" repathDistance="0.5"/></BehaviorTree>)", "chase.bt.xml", source ) );
    AIControllerComponent controller;
    BehaviorTreeRunner    runner;
    controller.getBlackboard().setVector( "Goal", float3{ 2.0f, 0.0f, 0.0f } );
    (void)XTestUtil::tickOnce( chase.getTree(), controller, runner );
    controller.getBlackboard().setVector( "Goal", float3{ 2.3f, 0.0f, 0.0f } );
    (void)XTestUtil::tickOnce( chase.getTree(), controller, runner );
    SW_EXPECT_TRUE( ( controller.getDestination() == float3{ 2.0f, 0.0f, 0.0f } ) ); // 0.3 m 는 다시 걸 만큼 움직이지 않았다
    controller.getBlackboard().setVector( "Goal", float3{ 3.0f, 0.0f, 0.0f } );
    (void)XTestUtil::tickOnce( chase.getTree(), controller, runner );
    SW_EXPECT_TRUE( ( controller.getDestination() == float3{ 3.0f, 0.0f, 0.0f } ) );

    BehaviorTreeAsset retreat;
    SW_REQUIRE_TRUE( retreat.loadFromXMLText( R"(<BehaviorTree><Action task="ai.moveAway" from="Self" away="Threat" distance="5"/></BehaviorTree>)", "retreat.bt.xml", source ) );
    controller.getBlackboard().setVector( "Self", float3{ 0.0f, 1.0f, 0.0f } );
    controller.getBlackboard().setVector( "Threat", float3{ 0.0f, 3.0f, 2.0f } );
    (void)XTestUtil::tickOnce( retreat.getTree(), controller, runner );
    SW_EXPECT_TRUE( ( controller.getDestination() == float3{ 0.0f, 1.0f, -5.0f } ) ); // 높이 차는 보지 않는다
}

/** @brief [AIControllerTreeTest] `ai.arrive` 는 허용 거리 안이면 멈추고 성공, 밖이면 그리로 가며 실패한다(뒤따르는 잎을 돌리지 않는다) */
SW_TEST_CASE( AIControllerTreeTest, ArriveSucceedsOnlyOnTheSpot )
{
    XTestSource       source;
    BehaviorTreeAsset asset;
    SW_REQUIRE_TRUE( asset.loadFromXMLText(
        R"(<BehaviorTree><Sequence><Action task="ai.arrive" key="Spot" self="Me" tolerance="0.2"/><Action task="ai.state" state="Use"/></Sequence></BehaviorTree>)", "arrive.bt.xml", source ) );
    AIControllerComponent controller;
    BehaviorTreeRunner    runner;
    controller.getBlackboard().setVector( "Spot", float3{ 1.0f, 0.0f, 0.0f } );
    controller.getBlackboard().setVector( "Me", float3{ 0.0f, 0.0f, 0.0f } );
    SW_EXPECT_TRUE( XTestUtil::tickOnce( asset.getTree(), controller, runner ) == BehaviorStatus::Failure );
    SW_EXPECT_TRUE( controller.hasDestination() );
    SW_EXPECT_TRUE( controller.getStateName().empty() );
    controller.getBlackboard().setVector( "Me", float3{ 0.9f, 5.0f, 0.0f } );
    SW_EXPECT_TRUE( XTestUtil::tickOnce( asset.getTree(), controller, runner ) == BehaviorStatus::Success );
    SW_EXPECT_FALSE( controller.hasDestination() );
    SW_EXPECT_TRUE( controller.getStateName() == hashed_string( "Use" ) );
}

/** @brief [AIControllerTreeTest] `blackboard.add` 는 수에 더하고(없는 키는 실패), `blackboard.copy` 는 종류째 옮긴다 */
SW_TEST_CASE( AIControllerTreeTest, BlackboardAddAndCopy )
{
    XTestSource       source;
    BehaviorTreeAsset asset;
    SW_REQUIRE_TRUE( asset.loadFromXMLText(
        R"(<BehaviorTree><Sequence><Action task="blackboard.add" key="Count" value="-1"/><Action task="blackboard.copy" from="Spot" to="Stand"/></Sequence></BehaviorTree>)", "board.bt.xml",
        source ) );
    AIControllerComponent controller;
    BehaviorTreeRunner    runner;
    Blackboard&           blackboard = controller.getBlackboard();
    SW_EXPECT_TRUE( XTestUtil::tickOnce( asset.getTree(), controller, runner ) == BehaviorStatus::Failure ); // Count 가 없다
    blackboard.setFloat( "Count", 6.0f );
    blackboard.setVector( "Spot", float3{ 1.0f, 2.0f, 3.0f } );
    SW_EXPECT_TRUE( XTestUtil::tickOnce( asset.getTree(), controller, runner ) == BehaviorStatus::Success );
    SW_EXPECT_TRUE( blackboard.getFloat( "Count" ) == std::optional<float32>( 5.0f ) );
    SW_EXPECT_TRUE( ( blackboard.getVector( "Stand" ) == std::optional<float3>( float3{ 1.0f, 2.0f, 3.0f } ) ) );
}

/** @brief [AIControllerTreeTest] 조종자 칸이 없는 실행기(지휘관 · 스케줄)에서 `ai.*` 는 실패이고 조건 `aiHasDestination` 은 거짓이다 */
SW_TEST_CASE( AIControllerTreeTest, ActionsFailWithoutAController )
{
    XTestSource       source;
    BehaviorTreeAsset asset;
    SW_REQUIRE_TRUE( asset.loadFromXMLText( R"(<BehaviorTree><Selector><Condition test="aiHasDestination"/><Action task="ai.stop"/></Selector></BehaviorTree>)", "bare.bt.xml", source ) );
    BehaviorTreeRunner runner;
    runner.initialize( &asset.getTree() );
    Blackboard blackboard;
    SW_EXPECT_TRUE( runner.tick( blackboard, nullptr, 0.1f ) == BehaviorStatus::Failure );
}

/** @brief [AIControllerTreeTest] 필수 칸이 빠진 `ai.*` 잎은 읽기 오류다 — 트리를 읽지 않는다(기본 키로 바꾸지 않는다) */
SW_TEST_CASE( AIControllerTreeTest, MissingRequiredFieldIsALoadError )
{
    XTestSource source;
    SW_EXPECT_LOG( "XMLFields.RequiredFieldMissing" );
    BehaviorTreeAsset broken;
    SW_EXPECT_FALSE( broken.loadFromXMLText( R"(<BehaviorTree><Action task="ai.moveTo"/></BehaviorTree>)", "broken.bt.xml", source ) );
}
