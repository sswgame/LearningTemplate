#include "pch.h"

#include "Engine/Animation/AnimationPlayer.h"
#include "Engine/Animation/Graph/AnimationGraphAsset.h"
#include "Engine/Animation/Graph/AnimationGraphPlayer.h"
#include "Engine/Animation/Skeletal/AnimationPoseNode.h"
#include "Engine/Animation/Skeletal/Pose.h"

#include "EngineTest/AnimationTestUtil.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

// ------------------------------------------------------------------------------
// 1) AnimationPoseNodeTest — 그래프 JSON 의 "pose_node" 와 포즈를 평가해 내는 노드(BlendPosesNode)
// ------------------------------------------------------------------------------

namespace
{
    /** @brief "Idle", "Wave" 클립과 그 둘을 섞는 포즈 노드 상태 "Mix" 를 가진 그래프입니다. */
    constexpr string_view kMixGraphJSON = R"({ "nodes": [
        { "id": 1, "name": "Mix", "pose_node": { "type": "blend_poses", "inputs": [ "Idle", "Wave" ], "weight": "WaveWeight" } },
        { "id": 2, "name": "Idle" } ], "links": [] })";

    /**
     * @class TestPoseSampler
     * @brief 본 0 의 x 위치에 "클립 기준값 + 시각" 을 쓰는 샘플러입니다. 어떤 입력을 어느 시각에 샘플했는지 포즈로 드러납니다.
     */
    class TestPoseSampler final : public IAnimationPoseSampler
    {
    public:
        TestPoseSampler( const IAnimationPlayable& first, float32 firstBase, float32 secondBase )
            : _pFirst{ &first }
            , _firstBase{ firstBase }
            , _secondBase{ secondBase }
        {
        }

        void samplePose( const IAnimationPlayable& playable, float32 time, Pose& inoutPose ) override
        {
            BoneTransform transform = inoutPose.getBoneTransform( 0 );
            transform._translation  = float3{ ( &playable == _pFirst ? _firstBase : _secondBase ) + time, 0.0f, 0.0f };
            inoutPose.setBoneTransform( 0, transform );
        }

    private:
        const IAnimationPlayable* _pFirst;
        float32                   _firstBase;
        float32                   _secondBase;
    };
} // namespace

/**
 * @brief [AnimationPoseNodeTest] "pose_node" 가 JSON 왕복에서 남는다(종류 · 입력 · 가중치 파라미터)
 */
SW_TEST_CASE( AnimationPoseNodeTest, PoseNodeRoundTrip )
{
    AnimationGraphAsset graph;
    SW_ASSERT_TRUE( graph.parseJSON( kMixGraphJSON ) );
    AnimationGraphAsset parsed;
    SW_ASSERT_TRUE( parsed.parseJSON( graph.toJSON() ) );
    SW_ASSERT_EQUAL( 2u, static_cast<uint32>( parsed._listNode.size() ) );

    const AnimationGraphNode& mix = parsed._listNode[0];
    SW_EXPECT_TRUE( mix.hasPoseNode() );
    SW_EXPECT_FALSE( mix.hasBlendSpace() );
    SW_EXPECT_TRUE( mix._poseNode._type == AnimationPoseNodeType::BlendPoses );
    SW_ASSERT_EQUAL( 2u, static_cast<uint32>( mix._poseNode._listInput.size() ) );
    SW_EXPECT_TRUE( mix._poseNode._listInput[1] == hashed_string( "Wave" ) );
    SW_EXPECT_TRUE( mix._poseNode._weightParameter == hashed_string( "WaveWeight" ) );
    SW_EXPECT_FALSE( parsed._listNode[1].hasPoseNode() );
}

/**
 * @brief [AnimationPoseNodeTest] 모르는 종류, 입력 수가 틀린 노드, 가중치 없는 노드, 블렌드 스페이스와 함께 적은 노드는 로드 오류다
 */
SW_TEST_CASE( AnimationPoseNodeTest, InvalidPoseNodeIsLoadError )
{
    const string_view arrBad[] = {
        R"({ "nodes": [ { "id": 1, "name": "A", "pose_node": { "type": "motion_magic", "inputs": [ "Idle", "Wave" ], "weight": "W" } } ] })",
        R"({ "nodes": [ { "id": 1, "name": "A", "pose_node": { "type": "blend_poses", "inputs": [ "Idle" ], "weight": "W" } } ] })",
        R"({ "nodes": [ { "id": 1, "name": "A", "pose_node": { "type": "blend_poses", "inputs": [ "Idle", "Wave" ] } } ] })",
        R"({ "nodes": [ { "id": 1, "name": "A", "pose_node": { "type": "blend_poses", "inputs": [ "Idle", "Wave" ], "weight": "W" },
                          "blend_space": { "x": "Speed", "samples": [ { "clip": "Idle", "x": 0 } ] } } ] })",
    };
    test::ScopedDefensiveTestLog expected( "invalid pose nodes are rejected" );
    for ( const string_view bad : arrBad )
    {
        AnimationGraphAsset graph;
        SW_EXPECT_FALSE( graph.parseJSON( bad ) );
        SW_EXPECT_TRUE( graph._listNode.empty() );
    }
}

/**
 * @brief [AnimationPoseNodeTest] 포즈 노드 표가 상태 이름을 노드로, 나머지를 클립으로 풀고, 그래프 플레이어가 그 노드를 재생한다
 * @details 노드는 `findPoseNode` 로 자신을 알리고 클립은 nullptr 이다 — 포즈를 내는 쪽은 캐스팅 전에 이 값으로 가른다.
 */
SW_TEST_CASE( AnimationPoseNodeTest, NodeSetResolvesNodesAndClips )
{
    AnimationGraphAsset graph;
    SW_ASSERT_TRUE( graph.parseJSON( kMixGraphJSON ) );
    test::TestPlayable       idle( 1.0f, true );
    test::TestPlayable       wave( 2.0f, false );
    test::TestPlayableSource clipSource;
    clipSource.add( "Idle", &idle );
    clipSource.add( "Wave", &wave );

    AnimationPoseNodeSet nodeSet;
    SW_ASSERT_TRUE( nodeSet.rebuild( graph, clipSource ) );
    SW_EXPECT_EQUAL( 1u, nodeSet.getNodeCount() );
    const IAnimationPlayable* pMix = nodeSet.findPlayable( hashed_string( "Mix" ) );
    SW_ASSERT_NOT_NULL( pMix );
    SW_EXPECT_NOT_NULL( pMix->findPoseNode() );
    SW_EXPECT_NEAR_EQUAL( 1.0f, pMix->getPlayLength(), 1e-6f ); // 길이 · 반복은 첫 입력(Idle)
    SW_EXPECT_TRUE( pMix->isLoopingByDefault() );
    SW_EXPECT_TRUE( nodeSet.findPlayable( hashed_string( "Idle" ) ) == &idle );
    SW_EXPECT_NULL( idle.findPoseNode() );

    AnimationGraphPlayer player;
    player.setGraph( &graph );
    player.setPlayableSource( &nodeSet );
    SW_ASSERT_TRUE( player.play( hashed_string{}, true, 0.0f ) );
    SW_EXPECT_TRUE( player.getPlayer().getCurrentPlayable() == pMix );

    // 입력을 클립 풀이에서 찾지 못하면 오류이고 표는 빈다.
    test::TestPlayableSource missingSource;
    missingSource.add( "Idle", &idle );
    test::ScopedDefensiveTestLog expected( "a pose node input that is not a clip is an error" );
    SW_EXPECT_FALSE( nodeSet.rebuild( graph, missingSource ) );
    SW_EXPECT_EQUAL( 0u, nodeSet.getNodeCount() );
}

/**
 * @brief [AnimationPoseNodeTest] BlendPosesNode 가 두 입력을 같은 위상에서 샘플해 파라미터 가중치로 섞는다
 * @details Idle(1 초)의 0.5 초는 Wave(2 초)의 1 초다. 샘플러는 x = 기준값 + 시각이므로 Idle 0.5, Wave 2 + 1 = 3 이고, 가중치 0.25 면 1.125 다.
 */
SW_TEST_CASE( AnimationPoseNodeTest, BlendPosesEvaluatesWeightedPose )
{
    AnimationGraphAsset graph;
    SW_ASSERT_TRUE( graph.parseJSON( kMixGraphJSON ) );
    test::TestPlayable       idle( 1.0f, true );
    test::TestPlayable       wave( 2.0f, false );
    test::TestPlayableSource clipSource;
    clipSource.add( "Idle", &idle );
    clipSource.add( "Wave", &wave );
    AnimationPoseNodeSet nodeSet;
    SW_ASSERT_TRUE( nodeSet.rebuild( graph, clipSource ) );
    const IAnimationPoseNode* pMix = nodeSet.findPlayable( hashed_string( "Mix" ) )->findPoseNode();
    SW_ASSERT_NOT_NULL( pMix );

    TestPoseSampler       sampler( idle, 0.0f, 2.0f );
    AnimationParameterSet parameter;
    parameter.setFloat( hashed_string( "WaveWeight" ), 0.25f );
    const AnimationPoseParams params{ &sampler, &parameter };

    Pose pose;
    pose.resize( 1 );
    SW_ASSERT_TRUE( pMix->evaluatePose( params, 0.5f, pose ) );
    SW_EXPECT_NEAR_EQUAL( 1.125f, pose.getTranslations()[0]._x, 1e-5f );

    // 가중치는 0..1 로 자른다.
    parameter.setFloat( hashed_string( "WaveWeight" ), 4.0f );
    SW_ASSERT_TRUE( pMix->evaluatePose( params, 0.5f, pose ) );
    SW_EXPECT_NEAR_EQUAL( 3.0f, pose.getTranslations()[0]._x, 1e-5f );

    // 샘플러가 없으면 오류이고 포즈는 그대로다.
    test::ScopedDefensiveTestLog expected( "pose node evaluation needs a sampler" );
    SW_EXPECT_FALSE( pMix->evaluatePose( AnimationPoseParams{ nullptr, &parameter }, 0.5f, pose ) );
    SW_EXPECT_NEAR_EQUAL( 3.0f, pose.getTranslations()[0]._x, 1e-5f );
}
