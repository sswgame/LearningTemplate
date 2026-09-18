#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Engine/Animation/AnimClip.h"
#include "Engine/Animation/AnimationGraphAsset.h"
#include "Engine/Animation/AnimationGraphPlayer.h"

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
 * @brief [AnimationGraphTest] toJson / parseJson 왕복이 노드·링크·좌표를 모두 보존하는지 검증
 */
SW_TEST_CASE( AnimationGraphTest, JsonRoundTripKeepsNodesAndLinks )
{
    const AnimationGraphAsset source = makeTwoNodeGraph();

    AnimationGraphAsset parsed;
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
 * @brief [AnimationGraphTest] loadFromFile 이 parseJson 과 같은 결과를 내는지 검증
 * @details loadFromFile 은 읽어 둔 문서를 다시 문자열로 덤프해 parseJson 에 넘기고 있었다.
 *          같은 JSON 을 두 번 파싱하던 것을 한 번으로 줄였으므로, 결과가 같음을 못박아 둔다.
 */
SW_TEST_CASE( AnimationGraphTest, LoadFromFileMatchesParseJson )
{
    const AnimationGraphAsset source   = makeTwoNodeGraph();
    const string              filePath = FileUtil::joinPath( FileUtil::getTempDirectory(), "test_anim_graph.json" );

    SW_EXPECT_TRUE( source.saveToFile( filePath ) );

    AnimationGraphAsset loaded;
    SW_EXPECT_TRUE( loaded.loadFromFile( filePath ) );

    AnimationGraphAsset parsed;
    SW_EXPECT_TRUE( parsed.parseJson( source.toJson() ) );

    SW_EXPECT_EQUAL( static_cast<uint32>( parsed._listNode.size() ), static_cast<uint32>( loaded._listNode.size() ) );
    SW_EXPECT_EQUAL( static_cast<uint32>( parsed._listLink.size() ), static_cast<uint32>( loaded._listLink.size() ) );
    SW_EXPECT_EQUAL( parsed._listNode[1]._name, loaded._listNode[1]._name );
    SW_EXPECT_NEAR_EQUAL( parsed._listNode[1]._position._y, loaded._listNode[1]._position._y, 1e-3f );

    FileUtil::removeFile( filePath );
}

/**
 * @brief [AnimationGraphTest] 빈 이름으로 play 하면 들어오는 링크가 없는 노드에서 시작하는지 검증
 */
SW_TEST_CASE( AnimationGraphTest, PlayStartsAtEntryNode )
{
    AnimationGraphPlayer player;
    player.setGraph( makeTwoNodeGraph() );

    SW_EXPECT_TRUE( player.play() );
    SW_EXPECT_EQUAL( string( "Idle" ), player.getCurrentNodeName() );
    SW_EXPECT_EQUAL( 1, player.getCurrentNodeId() );
}

/**
 * @brief [AnimationGraphTest] 클립이 없는 노드로 넘어가면 재생도 같이 비는지 검증
 * @details 예전에는 플레이어를 그대로 두어서, getCurrentNodeName() 은 새 노드를 말하는데
 *          evaluate() 는 이전 노드의 포즈를 계속 돌려줬다 — 둘이 다른 말을 하고 있었다.
 */
SW_TEST_CASE( AnimationGraphTest, AdvanceToCliplessNodeClearsPlayback )
{
    AnimClip idleClip( "Idle", 1.0f );

    AnimationGraphPlayer player;
    player.setGraph( makeTwoNodeGraph() );
    player.registerClip( "Idle", &idleClip );

    SW_EXPECT_TRUE( player.play( "Idle", false ) );
    SW_EXPECT_EQUAL( &idleClip, player.getAnimPlayer().getCurrentClip() );

    // Attack 노드에는 클립을 붙이지 않았다.
    SW_EXPECT_TRUE( player.advance() );
    SW_EXPECT_EQUAL( string( "Attack" ), player.getCurrentNodeName() );
    SW_EXPECT_EQUAL( 2, player.getCurrentNodeId() );
    SW_EXPECT_NULL( player.getAnimPlayer().getCurrentClip() );

    // 나가는 링크가 더 없으므로 전진은 실패한다.
    SW_EXPECT_FALSE( player.advance() );
}

/**
 * @brief [AnimationGraphTest] stop 이 노드와 재생 상태를 함께 비우는지 검증
 */
SW_TEST_CASE( AnimationGraphTest, StopClearsNodeAndPlayback )
{
    AnimClip idleClip( "Idle", 1.0f );

    AnimationGraphPlayer player;
    player.setGraph( makeTwoNodeGraph() );
    player.registerClip( "Idle", &idleClip );
    SW_EXPECT_TRUE( player.play( "Idle", true ) );

    player.stop();
    SW_EXPECT_EQUAL( 0, player.getCurrentNodeId() );
    SW_EXPECT_TRUE( player.getCurrentNodeName().empty() );
    SW_EXPECT_NULL( player.getAnimPlayer().getCurrentClip() );
}
