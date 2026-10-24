#include "pch.h"

#include "GameFramework/Base/Actor/AI/BehaviorTaskRegistry.h"
#include "GameFramework/Base/Actor/AI/BehaviorTree.h"
#include "GameFramework/Base/Actor/AI/BehaviorTreeAsset.h"
#include "GameFramework/Base/Actor/AI/Blackboard.h"

#include "TestFramework/TestFramework.h"

// BehaviorTreeAssetTest — 행동 트리 데이터 파일(`*.bt.xml`): 모양 읽기, 코드로 지은 트리와 같은 동작, 깨진 파일 거절(폴백 없음), 작업 이름 레지스트리, 게임 팩 지휘관 트리.

using namespace sw;

namespace
{
    /** @brief 작업이 몇 번 불렸는지 셉니다(행위자 자리에 넘긴다). */
    struct BehaviorTreeAssetAgent
    {
        int32 _patrolTicks{ 0 };
        int32 _attackTicks{ 0 };
    };

    BehaviorStatus patrolTask( BehaviorContext& context )
    {
        if ( context._bAborted == SW_FALSE )
            ++static_cast<BehaviorTreeAssetAgent*>( context._pOwner )->_patrolTicks;
        return BehaviorStatus::Running;
    }

    BehaviorStatus attackTask( BehaviorContext& context )
    {
        if ( context._bAborted == SW_FALSE )
            ++static_cast<BehaviorTreeAssetAgent*>( context._pOwner )->_attackTicks;
        return BehaviorStatus::Running;
    }

    constexpr const utf8* kTreeXML = R"(
        <BehaviorTree>
            <Selector name="Root" reactive="true">
                <BlackboardCondition key="Alert" compare="IsSet" abort="LowerPriority">
                    <Action name="Fight" task="test.btasset.attack"/>
                </BlackboardCondition>
                <Action name="Patrol" task="test.btasset.patrol"/>
            </Selector>
        </BehaviorTree>)";
} // namespace

namespace sw
{
    SW_BEHAVIOR_TASK( TestPatrol, "test.btasset.patrol", &patrolTask );
    SW_BEHAVIOR_TASK( TestAttack, "test.btasset.attack", &attackTask );
} // namespace sw

/**
 * @brief [BehaviorTreeAssetTest] 데이터 파일은 노드 종류 · 이름 · 블랙보드 키 · 비교 · 중단 규칙 · 작업 함수를 그대로 읽고, 코드로 지은 같은 트리와 틱마다 같은 잎을 고른다
 * @details 변이: 로더가 `abort` 를 읽지 않으면(None 으로 둔다) 경보가 켜져도 순찰이 중단되지 않아 두 트리의 잎이 어긋난다.
 */
SW_TEST_CASE( BehaviorTreeAssetTest, LoadsTheShapeAndRunsLikeTheCodeBuiltTree )
{
    BehaviorTreeAsset asset;
    SW_ASSERT_TRUE( asset.loadFromXMLText( kTreeXML, "BehaviorTreeAssetTest" ) );
    const BehaviorTree& tree = asset.getTree();
    SW_ASSERT_TRUE( tree.isValid() );
    SW_ASSERT_EQUAL( 4, tree.getNodeCount() );
    SW_EXPECT_TRUE( tree.getNode( 0 )._type == BehaviorNodeType::Selector );
    SW_EXPECT_STREQ( "Root", tree.getNode( 0 )._name.c_str() );
    SW_EXPECT_TRUE( tree.getNode( 0 )._bReactive != SW_FALSE );
    SW_EXPECT_TRUE( tree.getNode( 1 )._type == BehaviorNodeType::BlackboardCondition );
    SW_EXPECT_TRUE( tree.getNode( 1 )._key == hashed_string( "Alert" ) );
    SW_EXPECT_TRUE( tree.getNode( 1 )._compare == BlackboardCompare::IsSet );
    SW_EXPECT_TRUE( tree.getNode( 1 )._abortMode == BehaviorAbortMode::LowerPriority );
    SW_EXPECT_TRUE( tree.getNode( 2 )._type == BehaviorNodeType::Action );
    SW_EXPECT_STREQ( "Fight", tree.getNode( 2 )._name.c_str() );
    SW_EXPECT_TRUE( tree.getNode( 2 )._function == &attackTask );
    SW_EXPECT_STREQ( "Patrol", tree.getNode( 3 )._name.c_str() );
    SW_EXPECT_TRUE( tree.getNode( 3 )._function == &patrolTask );

    BehaviorTree coded;
    const int32  root  = coded.addSelector( -1, "Root", true );
    const int32  alert = coded.addBlackboardCondition( root, hashed_string( "Alert" ), BlackboardCompare::IsSet, 0.0f, BehaviorAbortMode::LowerPriority );
    coded.addAction( alert, "Fight", &attackTask );
    coded.addAction( root, "Patrol", &patrolTask );

    BehaviorTreeAssetAgent dataAgent;
    BehaviorTreeAssetAgent codedAgent;
    Blackboard             dataBoard;
    Blackboard             codedBoard;
    BehaviorTreeRunner     dataRunner;
    BehaviorTreeRunner     codedRunner;
    dataRunner.initialize( &tree );
    codedRunner.initialize( &coded );
    for ( int32 tick = 0; tick < 10; ++tick )
    {
        if ( tick == 3 )
        {
            dataBoard.setBool( "Alert", true );
            codedBoard.setBool( "Alert", true );
        }
        if ( tick == 7 )
        {
            dataBoard.clearValue( "Alert" );
            codedBoard.clearValue( "Alert" );
        }
        (void)dataRunner.tick( dataBoard, &dataAgent, 0.1f );
        (void)codedRunner.tick( codedBoard, &codedAgent, 0.1f );
        SW_EXPECT_STREQ( codedRunner.getActiveLeafName(), dataRunner.getActiveLeafName() );
    }
    SW_EXPECT_EQUAL( codedAgent._patrolTicks, dataAgent._patrolTicks );
    SW_EXPECT_EQUAL( codedAgent._attackTicks, dataAgent._attackTicks );
    SW_EXPECT_TRUE( dataAgent._attackTicks > 0 && dataAgent._patrolTicks > 0 ); // 경보로 순찰이 중단되고 경보가 꺼지면 돌아온다
}

/**
 * @brief [BehaviorTreeAssetTest] 깨진 파일은 읽지 않고 빈 트리로 남는다 - 모르는 노드 · 속성 · 작업 · 비교 · 중단 이름, 빠진 필수 속성, 음수 시간, 자식 수가 틀린 노드, 둘 이상의 루트
 * @details 읽는 쪽이 기본 노드로 바꾸거나 가지를 건너뛰지 않는다(폴백 없음). 변이: 모르는 작업 이름을 건너뛰면 `UnknownTask` 줄이 통과해 진다.
 */
SW_TEST_CASE( BehaviorTreeAssetTest, BrokenFilesAreRejected )
{
    SW_TEST_DEFENSIVE_SCOPE( "the loader logs one error per broken field; the files below are broken on purpose" );
    struct BrokenCase
    {
        const utf8* _pLabel;
        const utf8* _pXML;
    };
    const BrokenCase arrCase[] = {
        {       "UnknownNode",                                                                                      "<BehaviorTree><Fly/></BehaviorTree>"},
        {  "UnknownAttribute",                                        "<BehaviorTree><Sequence nam=\"x\"><Wait seconds=\"1\"/></Sequence></BehaviorTree>"},
        {       "UnknownTask",                                                              "<BehaviorTree><Action task=\"nobody.home\"/></BehaviorTree>"},
        {    "MissingTaskKey",                                                                                   "<BehaviorTree><Action/></BehaviorTree>"},
        {    "UnknownCompare", "<BehaviorTree><BlackboardCondition key=\"k\" compare=\"Huge\"><Wait seconds=\"1\"/></BlackboardCondition></BehaviorTree>"},
        {      "UnknownAbort",  "<BehaviorTree><BlackboardCondition key=\"k\" abort=\"Both2\"><Wait seconds=\"1\"/></BlackboardCondition></BehaviorTree>"},
        {    "MissingSeconds",                                                  "<BehaviorTree><Cooldown><Wait seconds=\"1\"/></Cooldown></BehaviorTree>"},
        {   "NegativeSeconds",                                                                      "<BehaviorTree><Wait seconds=\"-1\"/></BehaviorTree>"},
        {"MissingKeyOnMoveTo",                                                                                   "<BehaviorTree><MoveTo/></BehaviorTree>"},
        {     "LeafWithChild",                       "<BehaviorTree><Sequence><Wait seconds=\"1\"><Wait seconds=\"1\"/></Wait></Sequence></BehaviorTree>"},
        {  "DecoratorTwoKids",                             "<BehaviorTree><Inverter><Wait seconds=\"1\"/><Wait seconds=\"2\"/></Inverter></BehaviorTree>"},
        {    "EmptyComposite",                                                                                 "<BehaviorTree><Selector/></BehaviorTree>"},
        {          "TwoRoots",                                                  "<BehaviorTree><Wait seconds=\"1\"/><Wait seconds=\"2\"/></BehaviorTree>"},
        {            "NoRoot",                                                                                            "<BehaviorTree></BehaviorTree>"},
    };
    for ( const BrokenCase& brokenCase : arrCase )
    {
        BehaviorTreeAsset asset;
        SW_EXPECT_FALSE_MSG( asset.loadFromXMLText( brokenCase._pXML, brokenCase._pLabel ), brokenCase._pLabel );
        SW_EXPECT_FALSE_MSG( asset.getTree().isValid(), brokenCase._pLabel );
    }
}

/**
 * @brief [BehaviorTreeAssetTest] 작업 이름은 대소문자를 가리지 않고 찾으며 같은 이름의 두 번째 등록은 거절하고, 없는 이름은 찾지 못한다
 */
SW_TEST_CASE( BehaviorTreeAssetTest, TaskRegistryFindsByNameAndRejectsDuplicates )
{
    SW_TEST_DEFENSIVE_SCOPE( "a duplicate task registration logs an error on purpose" );
    BehaviorTaskRegistry& registry = BehaviorTaskRegistry::get();
    SW_EXPECT_TRUE( registry.findTask( "TEST.BTAsset.Patrol" ) == &patrolTask );
    SW_EXPECT_TRUE( registry.findTask( "test.btasset.nothing" ) == nullptr );
    const BehaviorTaskEntry duplicate{ "test.btasset.patrol", &attackTask };
    SW_EXPECT_FALSE( registry.registerTask( &duplicate ) );
    SW_EXPECT_TRUE( registry.findTask( "test.btasset.patrol" ) == &patrolTask ); // 먼저 것을 둔다
    SW_EXPECT_TRUE( registry.joinNames().find( "rts.defend" ) != string::npos ); // 키트가 올린 이름
}

/**
 * @brief [BehaviorTreeAssetTest] 게임 팩의 지휘관 트리(`game/starskirmish/data/commander.bt.xml`)는 키트가 등록한 `rts.*` 작업만 가리키며 코드로 짓던 모양 그대로다
 * @details 루트 반응형 셀렉터 · Threat 조건(LowerPriority) 아래 Defend · 경제 시퀀스의 ForceSuccess 여섯.
 */
SW_TEST_CASE( BehaviorTreeAssetTest, StarSkirmishCommanderTreeMatchesTheKitTasks )
{
    SW_TEST_REQUIRES_FEATURE( SourceResources );
    BehaviorTreeAsset asset;
    SW_ASSERT_TRUE( asset.loadFromResource( "game/starskirmish/data/commander.bt.xml" ) );
    const BehaviorTree& tree = asset.getTree();
    SW_ASSERT_TRUE( tree.isValid() );
    SW_EXPECT_STREQ( "Root", tree.getNode( 0 )._name.c_str() );
    SW_EXPECT_TRUE( tree.getNode( 0 )._bReactive != SW_FALSE );
    SW_EXPECT_TRUE( tree.getNode( 1 )._type == BehaviorNodeType::BlackboardCondition );
    SW_EXPECT_TRUE( tree.getNode( 1 )._key == hashed_string( "Threat" ) );
    SW_EXPECT_TRUE( tree.getNode( 1 )._abortMode == BehaviorAbortMode::LowerPriority );
    uint32 actionCount = 0;
    uint32 forceCount  = 0;
    for ( int32 index = 0; index < tree.getNodeCount(); ++index )
    {
        actionCount += tree.getNode( index )._type == BehaviorNodeType::Action ? 1u : 0u;
        forceCount += tree.getNode( index )._type == BehaviorNodeType::ForceSuccess ? 1u : 0u;
    }
    SW_EXPECT_EQUAL( 7u, actionCount ); // Defend + 경제 여섯
    SW_EXPECT_EQUAL( 6u, forceCount );
}
