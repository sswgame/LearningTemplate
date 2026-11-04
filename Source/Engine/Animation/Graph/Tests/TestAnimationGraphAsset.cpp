// AnimationGraphAsset — 원소 스키마(AnimationGraphFields.h)로 읽고 쓰기: 왕복, 기본값 생략, 열거자 이름, 칸 오류(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Animation/Graph/AnimationGraphAsset.h"

namespace
{
    /** @brief 노드 · 블렌드 스페이스 · 포즈 노드 · 조건 링크 · inertialization 링크 · 마스크 · 레이어 · 주석을 모두 쓰는 그래프입니다. */
    constexpr sw::string_view kGraph = "<AnimationGraphAsset>"
                                       "<nodes>"
                                       "<node id=\"1\" name=\"Idle\" x=\"40\" y=\"40\" loop=\"true\"/>"
                                       "<node id=\"2\" name=\"Move\" sync_group=\"Feet\"><blend_space x=\"Speed\" y=\"Direction\" notify=\"HighestWeight\"><samples>"
                                       "<sample clip=\"Walk\" x=\"1\"/><sample clip=\"Run\" x=\"4\" y=\"1\"/></samples></blend_space></node>"
                                       "<node id=\"3\" name=\"Wave\"><pose_node type=\"BlendPoses\" weight=\"WaveWeight\"><inputs><item>Idle</item><item>Hello</item></inputs></pose_node></node>"
                                       "</nodes>"
                                       "<links>"
                                       "<link id=\"1\" from=\"1\" to=\"2\" blend=\"0.2\"><condition param=\"Speed\" op=\"GreaterEqual\" value=\"0.5\"/></link>"
                                       "<link id=\"2\" from=\"2\" to=\"1\" blend_mode=\"Inertialization\" half_life=\"0.1\"><condition param=\"Hit\" op=\"Trigger\"/></link>"
                                       "</links>"
                                       "<bone_masks><bone_mask name=\"Upper\"><roots><entry bone=\"spine\" weight=\"1\"/></roots></bone_mask></bone_masks>"
                                       "<layers><layer name=\"Aim\" clip=\"AimPose\" mask=\"Upper\" weight=\"0.5\" blend=\"Additive\" loop=\"false\" weight_param=\"AimWeight\"/></layers>"
                                       "<comments><comment id=\"1\" text=\"Locomotion\" x=\"10\" y=\"20\"/></comments>"
                                       "</AnimationGraphAsset>";
} // namespace

/**
 * @brief [AnimationGraphAssetTest] 원소마다의 칸을 스키마로 읽는다 — 열거자 이름 · 불리언 · 적지 않은 칸의 기본값
 */
SW_TEST_CASE( AnimationGraphAssetTest, ReadsElementsThroughSchemas )
{
    sw::AnimationGraphAsset graph;
    SW_REQUIRE_TRUE( graph.parseXML( kGraph ) );
    SW_REQUIRE_EQUAL( size_t{ 3 }, graph._listNode.size() );
    SW_EXPECT_EQUAL( int8{ 1 }, graph._listNode[0]._loopOverride );
    SW_EXPECT_EQUAL( int8{ -1 }, graph._listNode[1]._loopOverride );
    SW_EXPECT_TRUE( graph._listNode[1]._position == sw::float2( 40.0f, 40.0f ) ); // 적지 않은 자리는 스키마 기본값
    SW_EXPECT_TRUE( graph._listNode[1]._blendSpace._notifyMode == sw::AnimationBlendSpaceNotifyMode::HighestWeight );
    SW_EXPECT_EQUAL( uint32{ 2 }, graph._listNode[1]._blendSpace._space.getSampleCount() );
    SW_EXPECT_TRUE( graph._listNode[2]._poseNode._type == sw::AnimationPoseNodeType::BlendPoses );
    SW_REQUIRE_EQUAL( size_t{ 2 }, graph._listLink.size() );
    SW_EXPECT_TRUE( graph._listLink[0]._op == sw::AnimationConditionOp::GreaterEqual );
    SW_EXPECT_EQUAL( -1.0f, graph._listLink[1]._blendSeconds );
    SW_EXPECT_TRUE( graph._listLink[1]._blendMode == sw::AnimationBlendMode::Inertialization );
    SW_REQUIRE_EQUAL( size_t{ 1 }, graph._listLayer.size() );
    SW_EXPECT_TRUE( graph._listLayer[0]._blend == sw::AnimationLayerBlend::Additive );
    SW_EXPECT_EQUAL( uint8{ SW_FALSE }, graph._listLayer[0]._bLoop );
    SW_REQUIRE_EQUAL( size_t{ 1 }, graph._listComment.size() );
    SW_EXPECT_TRUE( graph._listComment[0]._size == sw::float2( 320.0f, 200.0f ) );
}

/**
 * @brief [AnimationGraphAssetTest] 쓴 글을 다시 읽으면 같은 글이고, 기본값은 쓰지 않는다
 */
SW_TEST_CASE( AnimationGraphAssetTest, WriteReadRoundTrips )
{
    sw::AnimationGraphAsset graph;
    SW_REQUIRE_TRUE( graph.parseXML( kGraph ) );
    const sw::string        written = graph.toXML();
    sw::AnimationGraphAsset reread;
    SW_REQUIRE_TRUE( reread.parseXML( written ) );
    SW_EXPECT_EQUAL( written, reread.toXML() );
    SW_EXPECT_TRUE( written.find( "blend_mode=\"Crossfade\"" ) == sw::string::npos ); // 기본 전이 방식은 생략
    SW_EXPECT_TRUE( written.find( "op=\"GreaterEqual\"" ) != sw::string::npos );
}

/**
 * @brief [AnimationGraphAssetTest] 모르는 속성 · 빠진 필수 · 범위 밖 값은 그래프를 읽지 않는다
 */
SW_TEST_CASE( AnimationGraphAssetTest, RejectsFieldErrors )
{
    SW_EXPECT_LOG( "XMLFields.UnknownField" );
    SW_EXPECT_LOG( "XMLFields.RequiredFieldMissing" );
    SW_EXPECT_LOG( "XMLFields.ValueOutOfRange" );
    SW_EXPECT_LOG( "Serialization.ValueOutOfRange" );
    sw::AnimationGraphAsset graph;
    SW_EXPECT_FALSE( graph.parseXML( "<AnimationGraphAsset><nodes><node id=\"1\" name=\"A\" colour=\"red\"/></nodes></AnimationGraphAsset>" ) );
    SW_EXPECT_FALSE( graph.parseXML( "<AnimationGraphAsset><nodes><node name=\"A\"/></nodes></AnimationGraphAsset>" ) );
    SW_EXPECT_FALSE( graph.parseXML( "<AnimationGraphAsset><layers><layer name=\"L\" clip=\"C\" weight=\"2\" blend=\"Override\"/></layers></AnimationGraphAsset>" ) );
    SW_EXPECT_TRUE( graph._listNode.empty() );
}
