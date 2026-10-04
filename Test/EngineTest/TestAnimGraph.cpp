#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Engine/Animation/AnimGraphAsset.h"
#include "Engine/Animation/AnimGraphPlayer.h"

#include "EngineTest/AnimationTestUtil.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

// ------------------------------------------------------------------------------
// 1) AnimGraphTest — 그래프 애셋의 JSON 왕복과 그래프 플레이어의 노드 이동
// ------------------------------------------------------------------------------

namespace
{
    /** @brief Idle -> Attack 링크 하나를 가진 두 노드짜리 그래프를 만듭니다. */
    AnimGraphAsset makeTwoNodeGraph()
    {
        AnimGraphAsset asset;

        AnimGraphNode idleNode{};
        idleNode._id       = 1;
        idleNode._name     = "Idle";
        idleNode._position = float2{ 10.0f, 20.0f };
        AnimGraphNode attackNode{};
        attackNode._id       = 2;
        attackNode._name     = "Attack";
        attackNode._position = float2{ 210.0f, 20.0f };
        asset._listNode.push_back( idleNode );
        asset._listNode.push_back( attackNode );

        AnimGraphLink link{};
        link._id       = 1;
        link._fromNode = 1;
        link._toNode   = 2;
        asset._listLink.push_back( link );

        return asset;
    }
} // namespace

/**
 * @brief [AnimGraphTest] toJson / parseJson 왕복이 노드·링크·좌표를 모두 보존하는지 검증
 */
SW_TEST_CASE( AnimGraphTest, JsonRoundTripKeepsNodesAndLinks )
{
    const AnimGraphAsset source = makeTwoNodeGraph();

    AnimGraphAsset parsed;
    SW_EXPECT_TRUE( parsed.parseJson( source.toJson() ) );

    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( parsed._listNode.size() ) );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( parsed._listLink.size() ) );
    SW_EXPECT_EQUAL( string( "Idle" ), parsed._listNode[0]._name );
    SW_EXPECT_EQUAL( 2, parsed._listNode[1]._id );
    SW_EXPECT_NEAR_EQUAL( 210.0f, parsed._listNode[1]._position._x, 1e-3f );
    SW_EXPECT_EQUAL( 1, parsed._listLink[0]._fromNode );
    SW_EXPECT_EQUAL( 2, parsed._listLink[0]._toNode );
}

/**
 * @brief [AnimGraphTest] loadFromFile 이 parseJson 과 같은 결과를 내는지 검증
 * @details loadFromFile 은 읽어 둔 문서를 다시 문자열로 덤프하지 않고 한 번만 파싱한다. 두 경로의 결과가 같음을 못박아 둔다.
 */
SW_TEST_CASE( AnimGraphTest, LoadFromFileMatchesParseJson )
{
    const AnimGraphAsset source   = makeTwoNodeGraph();
    const string         filePath = test::makeTempPath( "test_anim_graph.json" );

    SW_EXPECT_TRUE( source.saveToFile( filePath ) );

    AnimGraphAsset loaded;
    SW_EXPECT_TRUE( loaded.loadFromFile( filePath ) );

    AnimGraphAsset parsed;
    SW_EXPECT_TRUE( parsed.parseJson( source.toJson() ) );

    SW_EXPECT_EQUAL( static_cast<uint32>( parsed._listNode.size() ), static_cast<uint32>( loaded._listNode.size() ) );
    SW_EXPECT_EQUAL( static_cast<uint32>( parsed._listLink.size() ), static_cast<uint32>( loaded._listLink.size() ) );
    SW_EXPECT_EQUAL( parsed._listNode[1]._name, loaded._listNode[1]._name );
    SW_EXPECT_NEAR_EQUAL( parsed._listNode[1]._position._y, loaded._listNode[1]._position._y, 1e-3f );
}

/**
 * @brief [AnimGraphTest] 빈 이름으로 play 하면 들어오는 링크가 없는 노드에서 시작하는지 검증
 */
SW_TEST_CASE( AnimGraphTest, PlayStartsAtEntryNode )
{
    const AnimGraphAsset     graph = makeTwoNodeGraph();
    test::TestPlayable       idle( 1.0f, true );
    test::TestPlayableSource source;
    source.add( "Idle", &idle );
    AnimGraphPlayer player;
    player.setGraph( &graph );
    player.setPlayableSource( &source );

    SW_EXPECT_TRUE( player.play( hashed_string{}, true, 0.0f ) );
    SW_EXPECT_EQUAL( string( "Idle" ), string( player.getCurrentStateName().c_str() ) );
    SW_EXPECT_EQUAL( 1, player.getCurrentNodeId() );
    SW_EXPECT_TRUE( player.getPlayer().getCurrentPlayable() == &idle );
}

/**
 * @brief [AnimGraphTest] 재생할 것이 없는 노드로 넘어가면 재생도 같이 비는지 검증
 * @details 플레이어를 그대로 두면 상태 이름은 새 노드를 말하는데 포즈는 이전 노드의 것이 남는다 — 둘이 다른 말을 한다.
 */
SW_TEST_CASE( AnimGraphTest, AdvanceToCliplessNodeClearsPlayback )
{
    const AnimGraphAsset     graph = makeTwoNodeGraph();
    test::TestPlayable       idle( 1.0f, false );
    test::TestPlayableSource source;
    source.add( "Idle", &idle );
    AnimGraphPlayer player;
    player.setGraph( &graph );
    player.setPlayableSource( &source );

    SW_EXPECT_TRUE( player.play( hashed_string( "Idle" ), false, 0.0f ) );
    SW_EXPECT_TRUE( player.getPlayer().getCurrentPlayable() == &idle );

    // Attack 노드에는 재생할 것이 없다.
    SW_EXPECT_TRUE( player.advance() );
    SW_EXPECT_EQUAL( string( "Attack" ), string( player.getCurrentStateName().c_str() ) );
    SW_EXPECT_EQUAL( 2, player.getCurrentNodeId() );
    SW_EXPECT_NULL( player.getPlayer().getCurrentPlayable() );

    // 나가는 링크가 더 없으므로 전진은 실패한다.
    SW_EXPECT_FALSE( player.advance() );
}

/**
 * @brief [AnimGraphTest] stop 이 노드와 재생 상태를 함께 비우는지 검증
 */
SW_TEST_CASE( AnimGraphTest, StopClearsNodeAndPlayback )
{
    const AnimGraphAsset     graph = makeTwoNodeGraph();
    test::TestPlayable       idle( 1.0f, true );
    test::TestPlayableSource source;
    source.add( "Idle", &idle );
    AnimGraphPlayer player;
    player.setGraph( &graph );
    player.setPlayableSource( &source );
    SW_EXPECT_TRUE( player.play( hashed_string( "Idle" ), true, 0.0f ) );

    player.stop();
    SW_EXPECT_EQUAL( 0, player.getCurrentNodeId() );
    SW_EXPECT_TRUE( player.getCurrentStateName().empty() );
    SW_EXPECT_NULL( player.getPlayer().getCurrentPlayable() );
}

/**
 * @brief [AnimGraphTest] 상태 기계 — 조건 링크는 파라미터가 참이 되는 갱신에 넘어가고, 트리거는 쓰이면 꺼지고, 조건 없는 링크는 반복 없이 끝날 때 넘어간다
 * @details Idle →(Speed > 0.5) Walk →(트리거 Jump) Jump →(끝나면) Land. 조건은 JSON 으로 왕복한다(모르는 비교 표기는 로드 오류).
 */
SW_TEST_CASE( AnimGraphTest, StateMachineFollowsConditionsTriggersAndFinish )
{
    AnimGraphAsset graph;
    const utf8*    arrName[4] = { "Idle", "Walk", "Jump", "Land" };
    for ( int32 nodeIndex = 0; nodeIndex < 4; ++nodeIndex )
    {
        AnimGraphNode node{};
        node._id   = nodeIndex + 1;
        node._name = arrName[nodeIndex];
        graph._listNode.push_back( node );
    }
    graph._listNode[1]._loopOverride = 1;
    AnimGraphLink toWalk{};
    toWalk._id        = 1;
    toWalk._fromNode  = 1;
    toWalk._toNode    = 2;
    toWalk._op        = AnimConditionOp::Greater;
    toWalk._parameter = hashed_string( "Speed" );
    toWalk._threshold = 0.5f;
    AnimGraphLink toJump{};
    toJump._id        = 2;
    toJump._fromNode  = 2;
    toJump._toNode    = 3;
    toJump._op        = AnimConditionOp::Trigger;
    toJump._parameter = hashed_string( "Jump" );
    AnimGraphLink toLand{};
    toLand._id       = 3;
    toLand._fromNode = 3;
    toLand._toNode   = 4;
    graph._listLink  = { toWalk, toJump, toLand };

    // JSON 왕복 — 조건 · 반복이 남는다.
    AnimGraphAsset parsed;
    SW_ASSERT_TRUE( parsed.parseJson( graph.toJson() ) );
    SW_ASSERT_EQUAL( 3u, static_cast<uint32>( parsed._listLink.size() ) );
    SW_EXPECT_TRUE( parsed._listLink[1]._op == AnimConditionOp::Trigger );
    SW_EXPECT_EQUAL( 1, static_cast<int32>( parsed._listNode[1]._loopOverride ) );
    {
        test::ScopedDefensiveTestLog expected( "unknown condition op is a load error" );
        AnimGraphAsset               broken;
        SW_EXPECT_FALSE( broken.parseJson( R"({ "nodes": [ { "id": 1, "name": "A" } ], "links": [ { "id": 1, "from": 1, "to": 1, "condition": { "param": "x", "op": "~" } } ] })" ) );
    }

    test::TestPlayable       idle( 1.0f, true );
    test::TestPlayable       walk( 1.0f, true );
    test::TestPlayable       jump( 0.5f, false );
    test::TestPlayable       land( 0.5f, false );
    test::TestPlayableSource source;
    source.add( "Idle", &idle );
    source.add( "Walk", &walk );
    source.add( "Jump", &jump );
    source.add( "Land", &land );
    AnimGraphPlayer player;
    player.setGraph( &parsed );
    player.setPlayableSource( &source );
    player.setDefaultBlendSeconds( 0.0f );
    AnimParameterSet parameter;
    SW_ASSERT_TRUE( player.play( hashed_string{}, true, 0.0f ) );

    player.update( 0.1f, &parameter, nullptr );
    SW_EXPECT_EQUAL( 1, player.getCurrentNodeId() ); // Speed 0 — 그대로
    parameter.setFloat( hashed_string( "Speed" ), 1.0f );
    player.update( 0.1f, &parameter, nullptr );
    SW_EXPECT_EQUAL( 2, player.getCurrentNodeId() );
    SW_EXPECT_TRUE( player.getPlayer().isCurrentLooping() ); // 노드의 "loop": true

    parameter.setTrigger( hashed_string( "Jump" ) );
    player.update( 0.1f, &parameter, nullptr );
    SW_EXPECT_EQUAL( 3, player.getCurrentNodeId() );
    SW_EXPECT_FALSE( parameter.isTrue( hashed_string( "Jump" ) ) ); // 트리거는 쓰이면 꺼진다

    // Jump(0.5 초, 반복 없음)가 끝나면 조건 없는 링크로 Land.
    player.update( 0.3f, &parameter, nullptr );
    SW_EXPECT_EQUAL( 3, player.getCurrentNodeId() );
    player.update( 0.3f, &parameter, nullptr );
    SW_EXPECT_EQUAL( 4, player.getCurrentNodeId() );
    SW_EXPECT_TRUE( player.getPlayer().getCurrentPlayable() == &land );
}
