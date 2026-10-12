#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Engine/Animation/Graph/AnimationGraphAsset.h"
#include "Engine/Animation/Graph/AnimationGraphPlayer.h"

#include "EngineTest/AnimationTestUtil.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

// ------------------------------------------------------------------------------
// 1) AnimationGraphTest — 그래프 애셋의 JSON 왕복과 그래프 플레이어의 노드 이동
// ------------------------------------------------------------------------------

namespace
{
    /** @brief Idle -> Attack 링크 하나를 가진 두 노드짜리 그래프를 만듭니다. */
    AnimationGraphAsset makeTwoNodeGraph()
    {
        AnimationGraphAsset asset;

        AnimationGraphNode idleNode{};
        idleNode._id       = 1;
        idleNode._name     = "Idle";
        idleNode._position = float2{ 10.0f, 20.0f };
        AnimationGraphNode attackNode{};
        attackNode._id       = 2;
        attackNode._name     = "Attack";
        attackNode._position = float2{ 210.0f, 20.0f };
        asset._listNode.push_back( idleNode );
        asset._listNode.push_back( attackNode );

        AnimationGraphLink link{};
        link._id       = 1;
        link._fromNode = 1;
        link._toNode   = 2;
        asset._listLink.push_back( link );

        return asset;
    }
} // namespace

/**
 * @brief [AnimationGraphTest] toJSON / parseJSON 왕복이 노드·링크·좌표를 모두 보존하는지 검증
 */
SW_TEST_CASE( AnimationGraphTest, JSONRoundTripKeepsNodesAndLinks )
{
    const AnimationGraphAsset source = makeTwoNodeGraph();

    AnimationGraphAsset parsed;
    SW_EXPECT_TRUE( parsed.parseJSON( source.toJSON() ) );

    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( parsed._listNode.size() ) );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( parsed._listLink.size() ) );
    SW_EXPECT_EQUAL( string( "Idle" ), parsed._listNode[0]._name );
    SW_EXPECT_EQUAL( 2, parsed._listNode[1]._id );
    SW_EXPECT_NEAR_EQUAL( 210.0f, parsed._listNode[1]._position._x, 1e-3f );
    SW_EXPECT_EQUAL( 1, parsed._listLink[0]._fromNode );
    SW_EXPECT_EQUAL( 2, parsed._listLink[0]._toNode );
}

/**
 * @brief [AnimationGraphTest] loadFromFile 이 parseJSON 과 같은 결과를 내는지 검증
 * @details loadFromFile 은 읽어 둔 문서를 다시 문자열로 덤프하지 않고 한 번만 파싱한다. 두 경로의 결과가 같음을 못박아 둔다.
 */
SW_TEST_CASE( AnimationGraphTest, LoadFromFileMatchesParseJSON )
{
    const AnimationGraphAsset source   = makeTwoNodeGraph();
    const string              filePath = test::makeTempPath( "test_animation_graph.json" );

    SW_EXPECT_TRUE( source.saveToFile( filePath ) );

    AnimationGraphAsset loaded;
    SW_EXPECT_TRUE( loaded.loadFromFile( filePath ) );

    AnimationGraphAsset parsed;
    SW_EXPECT_TRUE( parsed.parseJSON( source.toJSON() ) );

    SW_EXPECT_EQUAL( static_cast<uint32>( parsed._listNode.size() ), static_cast<uint32>( loaded._listNode.size() ) );
    SW_EXPECT_EQUAL( static_cast<uint32>( parsed._listLink.size() ), static_cast<uint32>( loaded._listLink.size() ) );
    SW_EXPECT_EQUAL( parsed._listNode[1]._name, loaded._listNode[1]._name );
    SW_EXPECT_NEAR_EQUAL( parsed._listNode[1]._position._y, loaded._listNode[1]._position._y, 1e-3f );
}

/**
 * @brief [AnimationGraphTest] 빈 이름으로 play 하면 들어오는 링크가 없는 노드에서 시작하는지 검증
 */
SW_TEST_CASE( AnimationGraphTest, PlayStartsAtEntryNode )
{
    const AnimationGraphAsset graph = makeTwoNodeGraph();
    test::TestPlayable        idle( 1.0f, true );
    test::TestPlayableSource  source;
    source.add( "Idle", &idle );
    AnimationGraphPlayer player;
    player.setGraph( &graph );
    player.setPlayableSource( &source );

    SW_EXPECT_TRUE( player.play( hashed_string{}, true, 0.0f ) );
    SW_EXPECT_EQUAL( string( "Idle" ), string( player.getCurrentStateName().c_str() ) );
    SW_EXPECT_EQUAL( 1, player.getCurrentNodeID() );
    SW_EXPECT_TRUE( player.getPlayer().getCurrentPlayable() == &idle );
}

/**
 * @brief [AnimationGraphTest] 재생할 것이 없는 노드로 넘어가면 재생도 같이 비는지 검증
 * @details 플레이어를 그대로 두면 상태 이름은 새 노드를 말하는데 포즈는 이전 노드의 것이 남는다 — 둘이 다른 말을 한다.
 */
SW_TEST_CASE( AnimationGraphTest, AdvanceToCliplessNodeClearsPlayback )
{
    const AnimationGraphAsset graph = makeTwoNodeGraph();
    test::TestPlayable        idle( 1.0f, false );
    test::TestPlayableSource  source;
    source.add( "Idle", &idle );
    AnimationGraphPlayer player;
    player.setGraph( &graph );
    player.setPlayableSource( &source );

    SW_EXPECT_TRUE( player.play( hashed_string( "Idle" ), false, 0.0f ) );
    SW_EXPECT_TRUE( player.getPlayer().getCurrentPlayable() == &idle );

    // Attack 노드에는 재생할 것이 없다.
    SW_EXPECT_TRUE( player.advance() );
    SW_EXPECT_EQUAL( string( "Attack" ), string( player.getCurrentStateName().c_str() ) );
    SW_EXPECT_EQUAL( 2, player.getCurrentNodeID() );
    SW_EXPECT_NULL( player.getPlayer().getCurrentPlayable() );

    // 나가는 링크가 더 없으므로 전진은 실패한다.
    SW_EXPECT_FALSE( player.advance() );
}

/**
 * @brief [AnimationGraphTest] stop 이 노드와 재생 상태를 함께 비우는지 검증
 */
SW_TEST_CASE( AnimationGraphTest, StopClearsNodeAndPlayback )
{
    const AnimationGraphAsset graph = makeTwoNodeGraph();
    test::TestPlayable        idle( 1.0f, true );
    test::TestPlayableSource  source;
    source.add( "Idle", &idle );
    AnimationGraphPlayer player;
    player.setGraph( &graph );
    player.setPlayableSource( &source );
    SW_EXPECT_TRUE( player.play( hashed_string( "Idle" ), true, 0.0f ) );

    player.stop();
    SW_EXPECT_EQUAL( 0, player.getCurrentNodeID() );
    SW_EXPECT_TRUE( player.getCurrentStateName().empty() );
    SW_EXPECT_NULL( player.getPlayer().getCurrentPlayable() );
}

/**
 * @brief [AnimationGraphTest] 상태 기계 — 조건 링크는 파라미터가 참이 되는 갱신에 넘어가고, 트리거는 쓰이면 꺼지고, 조건 없는 링크는 반복 없이 끝날 때 넘어간다
 * @details Idle →(Speed > 0.5) Walk →(트리거 Jump) Jump →(끝나면) Land. 조건은 JSON 으로 왕복한다(모르는 비교 표기는 로드 오류).
 */
SW_TEST_CASE( AnimationGraphTest, StateMachineFollowsConditionsTriggersAndFinish )
{
    AnimationGraphAsset graph;
    const utf8*         arrName[4] = { "Idle", "Walk", "Jump", "Land" };
    for ( int32 nodeIndex = 0; nodeIndex < 4; ++nodeIndex )
    {
        AnimationGraphNode node{};
        node._id   = nodeIndex + 1;
        node._name = arrName[nodeIndex];
        graph._listNode.push_back( node );
    }
    graph._listNode[1]._loopOverride = 1;
    AnimationGraphLink toWalk{};
    toWalk._id        = 1;
    toWalk._fromNode  = 1;
    toWalk._toNode    = 2;
    toWalk._op        = AnimationConditionOp::Greater;
    toWalk._parameter = hashed_string( "Speed" );
    toWalk._threshold = 0.5f;
    AnimationGraphLink toJump{};
    toJump._id        = 2;
    toJump._fromNode  = 2;
    toJump._toNode    = 3;
    toJump._op        = AnimationConditionOp::Trigger;
    toJump._parameter = hashed_string( "Jump" );
    AnimationGraphLink toLand{};
    toLand._id       = 3;
    toLand._fromNode = 3;
    toLand._toNode   = 4;
    graph._listLink  = { toWalk, toJump, toLand };

    // JSON 왕복 — 조건 · 반복이 남는다.
    AnimationGraphAsset parsed;
    SW_ASSERT_TRUE( parsed.parseJSON( graph.toJSON() ) );
    SW_ASSERT_EQUAL( 3u, static_cast<uint32>( parsed._listLink.size() ) );
    SW_EXPECT_TRUE( parsed._listLink[1]._op == AnimationConditionOp::Trigger );
    SW_EXPECT_EQUAL( 1, static_cast<int32>( parsed._listNode[1]._loopOverride ) );
    {
        test::ScopedDefensiveTestLog expected( "unknown condition op is a load error" );
        AnimationGraphAsset          broken;
        SW_EXPECT_FALSE( broken.parseJSON( R"({ "nodes": [ { "id": 1, "name": "A" } ], "links": [ { "id": 1, "from": 1, "to": 1, "condition": { "param": "x", "op": "~" } } ] })" ) );
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
    AnimationGraphPlayer player;
    player.setGraph( &parsed );
    player.setPlayableSource( &source );
    player.setDefaultBlendSeconds( 0.0f );
    AnimationParameterSet parameter;
    SW_ASSERT_TRUE( player.play( hashed_string{}, true, 0.0f ) );

    player.update( 0.1f, &parameter, nullptr );
    SW_EXPECT_EQUAL( 1, player.getCurrentNodeID() ); // Speed 0 — 그대로
    parameter.setFloat( hashed_string( "Speed" ), 1.0f );
    player.update( 0.1f, &parameter, nullptr );
    SW_EXPECT_EQUAL( 2, player.getCurrentNodeID() );
    SW_EXPECT_TRUE( player.getPlayer().isCurrentLooping() ); // 노드의 "loop": true

    parameter.setTrigger( hashed_string( "Jump" ) );
    player.update( 0.1f, &parameter, nullptr );
    SW_EXPECT_EQUAL( 3, player.getCurrentNodeID() );
    SW_EXPECT_FALSE( parameter.isTrue( hashed_string( "Jump" ) ) ); // 트리거는 쓰이면 꺼진다

    // Jump(0.5 초, 반복 없음)가 끝나면 조건 없는 링크로 Land.
    player.update( 0.3f, &parameter, nullptr );
    SW_EXPECT_EQUAL( 3, player.getCurrentNodeID() );
    player.update( 0.3f, &parameter, nullptr );
    SW_EXPECT_EQUAL( 4, player.getCurrentNodeID() );
    SW_EXPECT_TRUE( player.getPlayer().getCurrentPlayable() == &land );
}

/**
 * @brief [AnimationGraphTest] 블렌드 스페이스 상태와 동기 그룹이 JSON 왕복에서 남는다(1D 는 x 순으로 정렬, 2D 는 y 까지)
 */
SW_TEST_CASE( AnimationGraphTest, BlendSpaceNodeAndSyncGroupRoundTrip )
{
    const string_view   json = R"({ "nodes": [
        { "id": 1, "name": "Locomotion", "sync_group": "Feet",
          "blend_space": { "x": "Speed", "samples": [ { "clip": "Running_A", "x": 6 }, { "clip": "Idle", "x": 0 }, { "clip": "Walking_A", "x": 2 } ] } },
        { "id": 2, "name": "Strafe",
          "blend_space": { "x": "Right", "y": "Forward", "samples": [ { "clip": "Walking_Left", "x": -1, "y": 0 }, { "clip": "Walking_A", "x": 0, "y": 1 } ] } },
        { "id": 3, "name": "Jump" } ], "links": [] })";
    AnimationGraphAsset graph;
    SW_ASSERT_TRUE( graph.parseJSON( json ) );

    AnimationGraphAsset parsed;
    SW_ASSERT_TRUE( parsed.parseJSON( graph.toJSON() ) );
    SW_ASSERT_EQUAL( 3u, static_cast<uint32>( parsed._listNode.size() ) );

    const AnimationGraphNode& locomotion = parsed._listNode[0];
    SW_EXPECT_TRUE( locomotion.hasBlendSpace() );
    SW_EXPECT_TRUE( locomotion._syncGroup == hashed_string( "Feet" ) );
    SW_EXPECT_TRUE( locomotion._blendSpace._parameterX == hashed_string( "Speed" ) );
    SW_EXPECT_TRUE( locomotion._blendSpace._parameterY.empty() );
    SW_EXPECT_EQUAL( 1u, locomotion._blendSpace._space.getAxisCount() );
    SW_ASSERT_EQUAL( 3u, locomotion._blendSpace._space.getSampleCount() );
    SW_EXPECT_TRUE( locomotion._blendSpace._space.getSamples()[0]._clipName == hashed_string( "Idle" ) );
    SW_EXPECT_TRUE( locomotion._blendSpace._space.getSamples()[2]._clipName == hashed_string( "Running_A" ) );

    const AnimationGraphNode& strafe = parsed._listNode[1];
    SW_EXPECT_EQUAL( 2u, strafe._blendSpace._space.getAxisCount() );
    SW_EXPECT_TRUE( strafe._blendSpace._parameterY == hashed_string( "Forward" ) );
    SW_EXPECT_NEAR_EQUAL( 1.0f, strafe._blendSpace._space.getSamples()[1]._position._y, 1e-6f );
    SW_EXPECT_TRUE( strafe._syncGroup.empty() );

    SW_EXPECT_FALSE( parsed._listNode[2].hasBlendSpace() );
}

/**
 * @brief [AnimationGraphTest] 표본이 없거나 클립 · x 파라미터가 빈 블렌드 스페이스는 로드 오류다(조용히 보통 상태가 되지 않는다)
 */
SW_TEST_CASE( AnimationGraphTest, InvalidBlendSpaceIsLoadError )
{
    const string_view arrBad[] = {
        R"({ "nodes": [ { "id": 1, "name": "A", "blend_space": { "x": "Speed", "samples": [] } } ] })",
        R"({ "nodes": [ { "id": 1, "name": "A", "blend_space": { "x": "Speed", "samples": [ { "x": 1 } ] } } ] })",
        R"({ "nodes": [ { "id": 1, "name": "A", "blend_space": { "samples": [ { "clip": "Idle", "x": 1 } ] } } ] })",
    };
    for ( const string_view bad : arrBad )
    {
        AnimationGraphAsset graph;
        SW_EXPECT_FALSE( graph.parseJSON( bad ) );
        SW_EXPECT_TRUE( graph._listNode.empty() );
    }
}
