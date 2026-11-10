// AnimationGraphAsset — 원소 스키마(AnimationGraphParams.h)로 읽고 쓰기: 왕복, 기본값 생략, 열거자 이름, 칸 오류(nogpu).
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Animation/Graph/AnimationGraphAsset.h"
#include "Engine/Content/Tests/ScopedScratchDomain.h"

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

    /** @brief 차이 파일의 부모입니다. 상태 둘 · 전이 하나 · 레이어 둘(클립 · 슬롯)입니다. */
    constexpr sw::string_view kBaseGraph = "<AnimationGraphAsset>"
                                           "<nodes><node id=\"1\" name=\"Idle\" loop=\"true\"/><node id=\"2\" name=\"Move\"><blend_space x=\"Speed\"><samples><sample clip=\"Walk\" x=\"1\"/></samples></blend_space></node></nodes>"
                                           "<links><link id=\"1\" from=\"1\" to=\"2\" blend=\"0.2\"><condition param=\"Speed\" op=\"Greater\" value=\"0.5\"/></link></links>"
                                           "<layers><layer name=\"Breath\" clip=\"Breathe\" weight=\"0.5\" blend=\"Additive\"/><layer name=\"Upper\" slot=\"UpperBody\" weight=\"1\" blend=\"Override\"/></layers>"
                                           "</AnimationGraphAsset>";

    struct AnimationGraphTestUtil
    {
        /** @brief 부모 파일을 쓰고 @p pChildBody 를 그 자식으로 읽습니다. */
        static bool parseChild( sw::AnimationGraphAsset& outGraph, const sw::test::ScopedScratchDomain& domain, const utf8* pChildBody )
        {
            SW_EXPECT_TRUE( sw::FileUtil::writeTextFile( domain.makeDiskPath( "base.animgraph.xml" ), sw::string{ kBaseGraph } ) );
            const sw::string baseID = sw::test::ScopedScratchDomain::makeID( "base.animgraph.xml" ).str();
            return outGraph.loadFromXMLText( "<AnimationGraphAsset _base=\"" + baseID + "\">" + pChildBody + "</AnimationGraphAsset>" );
        }
    };
} // namespace

/**
 * @brief [AnimationGraphAssetTest] 원소마다의 칸을 스키마로 읽는다 — 열거자 이름 · 불리언 · 적지 않은 칸의 기본값
 */
SW_TEST_CASE( AnimationGraphAssetTest, ReadsElementsThroughSchemas )
{
    sw::AnimationGraphAsset graph;
    SW_REQUIRE_TRUE( graph.loadFromXMLText( kGraph ) );
    SW_REQUIRE_EQUAL( size_t{ 3 }, graph._listNode.size() );
    SW_EXPECT_TRUE( graph._listNode[0]._loop == sw::optional<bool>( true ) );
    SW_EXPECT_FALSE( graph._listNode[1]._loop.has_value() );                      // 적지 않음 — 값이 없다
    SW_EXPECT_TRUE( graph._listNode[1]._position == sw::float2( 40.0f, 40.0f ) ); // 적지 않은 자리는 스키마 기본값
    SW_EXPECT_TRUE( graph._listNode[1]._blendSpace._notifyMode == sw::AnimationBlendSpaceNotifyMode::HighestWeight );
    SW_EXPECT_EQUAL( uint32{ 2 }, graph._listNode[1]._blendSpace._space.getSampleCount() );
    SW_EXPECT_TRUE( graph._listNode[2]._poseNode._type == sw::hashed_string( "BlendPoses" ) );
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
    SW_REQUIRE_TRUE( graph.loadFromXMLText( kGraph ) );
    const sw::string        written = graph.toXML();
    sw::AnimationGraphAsset reread;
    SW_REQUIRE_TRUE( reread.loadFromXMLText( written ) );
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
    SW_EXPECT_FALSE( graph.loadFromXMLText( "<AnimationGraphAsset><nodes><node id=\"1\" name=\"A\" colour=\"red\"/></nodes></AnimationGraphAsset>" ) );
    SW_EXPECT_FALSE( graph.loadFromXMLText( "<AnimationGraphAsset><nodes><node name=\"A\"/></nodes></AnimationGraphAsset>" ) );
    SW_EXPECT_FALSE( graph.loadFromXMLText( "<AnimationGraphAsset><layers><layer name=\"L\" clip=\"C\" weight=\"2\" blend=\"Override\"/></layers></AnimationGraphAsset>" ) );
    SW_EXPECT_TRUE( graph._listNode.empty() );
}

/**
 * @brief [AnimationGraphAssetTest] 차이 파일 — 상태는 이름, 전이는 id, 레이어는 이름으로 합친다. 적지 않은 칸은 물려받고 `blend_space` 는 통째로 바뀐다
 */
SW_TEST_CASE( AnimationGraphAssetTest, ChildMergesByKeys )
{
    const sw::test::ScopedScratchDomain domain;
    sw::AnimationGraphAsset             graph;
    SW_REQUIRE_TRUE( AnimationGraphTestUtil::parseChild( graph, domain,
                                                         "<nodes><node name=\"Move\"><blend_space x=\"Pace\"><samples><sample clip=\"Run\" x=\"2\"/></samples></blend_space></node><node id=\"3\" name=\"Dance\"/></nodes>"
                                                         "<links><link id=\"1\" blend=\"0.4\"/><link id=\"2\" from=\"2\" to=\"3\"/></links>"
                                                         "<layers><layer name=\"Breath\" _op=\"Remove\"/><layer name=\"Upper\" weight=\"0.25\"/></layers>" ) );
    SW_REQUIRE_EQUAL( size_t{ 3 }, graph._listNode.size() );
    SW_EXPECT_TRUE( graph._listNode[0]._loop == sw::optional<bool>( true ) ); // 물려받음
    SW_EXPECT_EQUAL( int32{ 2 }, graph._listNode[1]._id );                    // 이름으로 짚었으니 id 는 물려받음
    SW_EXPECT_TRUE( graph._listNode[1]._blendSpace._parameterX == sw::hashed_string( "Pace" ) );
    SW_REQUIRE_EQUAL( uint32{ 1 }, graph._listNode[1]._blendSpace._space.getSampleCount() );
    SW_EXPECT_TRUE( graph._listNode[1]._blendSpace._space.getSamples()[0]._clipName == sw::hashed_string( "Run" ) );
    SW_REQUIRE_EQUAL( size_t{ 2 }, graph._listLink.size() );
    SW_EXPECT_EQUAL( 0.4f, graph._listLink[0]._blendSeconds );
    SW_EXPECT_TRUE( graph._listLink[0]._op == sw::AnimationConditionOp::Greater ); // 조건은 물려받음
    SW_REQUIRE_EQUAL( size_t{ 1 }, graph._listLayer.size() );
    SW_EXPECT_TRUE( graph._listLayer[0]._slotName == sw::hashed_string( "UpperBody" ) );
    SW_EXPECT_EQUAL( 0.25f, graph._listLayer[0]._weight );
}

/**
 * @brief [AnimationGraphAssetTest] 빈 조건 원소는 물려받은 조건을 비워 "끝나면 다음" 이 되고, 클립 레이어를 슬롯으로 바꾸려면 `_op="Replace"` 다
 */
SW_TEST_CASE( AnimationGraphAssetTest, ChildClearsConditionAndReplacesLayer )
{
    const sw::test::ScopedScratchDomain domain;
    sw::AnimationGraphAsset             graph;
    SW_REQUIRE_TRUE( AnimationGraphTestUtil::parseChild( graph, domain,
                                                         "<links><link id=\"1\"><condition/></link></links>"
                                                         "<layers><layer name=\"Breath\" _op=\"Replace\" slot=\"Chest\" weight=\"1\" blend=\"Additive\"/></layers>" ) );
    SW_EXPECT_TRUE( graph._listLink[0]._op == sw::AnimationConditionOp::None );
    SW_EXPECT_TRUE( graph._listLayer[0]._clipName.empty() );
    SW_EXPECT_TRUE( graph._listLayer[0]._slotName == sw::hashed_string( "Chest" ) );

    SW_EXPECT_LOG( "AnimationGraphAsset.AnimationGraphRepeatsLayer" );
    SW_EXPECT_FALSE( AnimationGraphTestUtil::parseChild( graph, domain, "<layers><layer name=\"Breath\" slot=\"Chest\"/></layers>" ) );
}

/**
 * @brief [AnimationGraphAssetTest] 슬롯 레이어는 왕복하고, clip 과 slot 을 함께 적거나 둘 다 빼면 읽지 않는다
 */
SW_TEST_CASE( AnimationGraphAssetTest, LayerSlotRoundTripsAndNeedsOneSource )
{
    sw::AnimationGraphAsset graph;
    SW_REQUIRE_TRUE( graph.loadFromXMLText( kBaseGraph ) );
    SW_EXPECT_TRUE( graph.findLayer( sw::hashed_string( "Upper" ) )->_slotName == sw::hashed_string( "UpperBody" ) );
    sw::AnimationGraphAsset reread;
    SW_REQUIRE_TRUE( reread.loadFromXMLText( graph.toXML() ) );
    SW_EXPECT_TRUE( reread.findLayer( sw::hashed_string( "Upper" ) )->_slotName == sw::hashed_string( "UpperBody" ) );

    SW_EXPECT_LOG( "AnimationGraphAsset.AnimationGraphRepeatsLayer" );
    SW_EXPECT_FALSE( graph.loadFromXMLText( "<AnimationGraphAsset><layers><layer name=\"L\" clip=\"C\" slot=\"S\" weight=\"1\" blend=\"Override\"/></layers></AnimationGraphAsset>" ) );
    SW_EXPECT_FALSE( graph.loadFromXMLText( "<AnimationGraphAsset><layers><layer name=\"L\" weight=\"1\" blend=\"Override\"/></layers></AnimationGraphAsset>" ) );
}

/**
 * @brief [AnimationGraphAssetTest] 부모와 어긋난 차이는 로드 오류다 — 겹치는 노드 id, 없는 노드를 가리키는 전이, 없는 상태의 삭제
 */
SW_TEST_CASE( AnimationGraphAssetTest, ChildThatDisagreesWithBaseIsRejected )
{
    const sw::test::ScopedScratchDomain domain;
    sw::AnimationGraphAsset             graph;
    SW_EXPECT_LOG( "AnimationGraphAsset.NodeIDRepeats" );
    SW_EXPECT_FALSE( AnimationGraphTestUtil::parseChild( graph, domain, "<nodes><node id=\"2\" name=\"Dance\"/></nodes>" ) );
    SW_EXPECT_LOG( "AnimationGraphAsset.LinkNodeMissing" );
    SW_EXPECT_FALSE( AnimationGraphTestUtil::parseChild( graph, domain, "<links><link id=\"2\" from=\"2\" to=\"9\"/></links>" ) );
    SW_EXPECT_LOG( "DataInheritance.OpKeyMissing" );
    SW_EXPECT_FALSE( AnimationGraphTestUtil::parseChild( graph, domain, "<nodes><node name=\"Nope\" _op=\"Remove\"/></nodes>" ) );
    SW_EXPECT_TRUE( graph._listNode.empty() );
}

/**
 * @brief [AnimationGraphAssetTest] 저장은 부모와 다른 칸만 쓰고, 칸을 형식 기본값으로 되돌려 차이로 적을 수 없으면 쓰지 않는다(부모 없는 한 벌로 끊지 않는다)
 */
SW_TEST_CASE( AnimationGraphAssetTest, SaveWritesOnlyTheDifference )
{
    const sw::test::ScopedScratchDomain domain;
    sw::AnimationGraphAsset             graph;
    SW_REQUIRE_TRUE( AnimationGraphTestUtil::parseChild( graph, domain, "<links><link id=\"1\" blend=\"0.4\"/></links>" ) );

    const sw::string                     childPath = domain.makeDiskPath( "child.animgraph.xml" );
    const sw::optional<sw::AbsolutePath> absPath   = sw::FileUtil::requireAbsolutePath( childPath );
    SW_REQUIRE_TRUE( absPath.has_value() );
    SW_REQUIRE_TRUE( graph.saveToFile( *absPath ) );
    sw::string written;
    SW_REQUIRE_TRUE( sw::FileUtil::readTextFile( childPath, written ) );
    SW_EXPECT_TRUE( written.find( "_base=" ) != sw::string::npos );
    SW_EXPECT_TRUE( written.find( "Breathe" ) == sw::string::npos ); // 물려받은 칸은 쓰지 않는다

    sw::AnimationGraphAsset reread;
    SW_REQUIRE_TRUE( reread.loadFromDiskFile( childPath ) );
    SW_EXPECT_EQUAL( graph.toXML(), reread.toXML() );

    graph._listLink[0]._blendSeconds = -1.0f; // 형식 기본값은 쓰지 않으므로 부모의 0.2 를 덮을 수 없다
    SW_EXPECT_LOG( "ResourceSave.DeltaNotSaved" );
    SW_EXPECT_FALSE( graph.saveToFile( *absPath ) ); // 부모 없는 한 벌로 끊지 않는다
    SW_REQUIRE_TRUE( sw::FileUtil::readTextFile( childPath, written ) );
    SW_EXPECT_TRUE( written.find( "_base=" ) != sw::string::npos ); // 앞 저장이 그대로 남는다
}

/**
 * @brief [AnimationGraphAssetTest] LOD 규칙(`max_lod`)은 칸이라 변형은 그 칸만 적는다 — 블렌드 스페이스 · 다른 칸은 물려받고, 적지 않은 레이어는 -1(항상) 이다
 */
SW_TEST_CASE( AnimationGraphAssetTest, ChildSetsOnlyLODGrades )
{
    const sw::test::ScopedScratchDomain domain;
    sw::AnimationGraphAsset             graph;
    SW_REQUIRE_TRUE( AnimationGraphTestUtil::parseChild( graph, domain, "<nodes><node name=\"Move\" max_lod=\"1\"/></nodes><layers><layer name=\"Breath\" max_lod=\"0\"/></layers>" ) );
    SW_REQUIRE_EQUAL( size_t{ 2 }, graph._listNode.size() );
    SW_EXPECT_EQUAL( int32{ -1 }, graph._listNode[0]._maxLODGrade );
    SW_EXPECT_EQUAL( int32{ 1 }, graph._listNode[1]._maxLODGrade );
    SW_EXPECT_EQUAL( uint32{ 1 }, graph._listNode[1]._blendSpace._space.getSampleCount() ); // 블렌드 스페이스는 물려받음
    SW_REQUIRE_EQUAL( size_t{ 2 }, graph._listLayer.size() );
    SW_EXPECT_EQUAL( int32{ 0 }, graph._listLayer[0]._maxLODGrade );
    SW_EXPECT_EQUAL( 0.5f, graph._listLayer[0]._weight ); // 다른 칸은 물려받음
    SW_EXPECT_EQUAL( int32{ -1 }, graph._listLayer[1]._maxLODGrade );

    const sw::string        written = graph.toXML();
    sw::AnimationGraphAsset reread;
    SW_REQUIRE_TRUE( reread.loadFromXMLText( written ) );
    SW_EXPECT_EQUAL( written, reread.toXML() );
    SW_EXPECT_TRUE( written.find( "max_lod=\"1\"" ) != sw::string::npos );

    // 기본값(-1)은 쓰지 않는다 — LOD 를 적지 않은 그래프의 파일은 그대로다.
    sw::AnimationGraphAsset plain;
    SW_REQUIRE_TRUE( plain.loadFromXMLText( kGraph ) );
    SW_EXPECT_TRUE( plain.toXML().find( "max_lod" ) == sw::string::npos );
}

/**
 * @brief [AnimationGraphAssetTest] 노드의 `max_lod` 는 블렌드 스페이스 상태만 받는다 — 클립 하나인 상태에는 접을 가지가 없다
 */
SW_TEST_CASE( AnimationGraphAssetTest, RejectsMaxLODOnClipState )
{
    SW_EXPECT_LOG( "AnimationGraphAsset.AnimationGraphNodeMaxLOD" );
    sw::AnimationGraphAsset graph;
    SW_EXPECT_FALSE( graph.loadFromXMLText( "<AnimationGraphAsset><nodes><node id=\"1\" name=\"Idle\" max_lod=\"1\"/></nodes></AnimationGraphAsset>" ) );
    SW_EXPECT_TRUE( graph._listNode.empty() );
}

/**
 * @brief [AnimationGraphAssetTest] 포즈 노드 종류는 표(`AnimationPoseNodeRegistry`)의 이름이다 — 모르는 이름과 종류의 계약(입력 수 · weight)을 어긴 정의는 로드 오류다
 */
SW_TEST_CASE( AnimationGraphAssetTest, PoseNodeTypeComesFromRegistry )
{
    sw::AnimationGraphAsset graph;
    SW_EXPECT_LOG( "AnimationGraphAsset.AnimationGraphNodeUnknown" );
    SW_EXPECT_FALSE( graph.loadFromXMLText( "<AnimationGraphAsset><nodes><node id=\"1\" name=\"W\"><pose_node type=\"Mystery\" weight=\"W\"><inputs><item>A</item><item>B</item></inputs></pose_node></node></nodes></AnimationGraphAsset>" ) );
    SW_EXPECT_LOG( "AnimationGraphAsset.AnimationGraphNodeBlend7" );
    SW_EXPECT_FALSE( graph.loadFromXMLText( "<AnimationGraphAsset><nodes><node id=\"1\" name=\"W\"><pose_node type=\"BlendPoses\" weight=\"W\"><inputs><item>A</item></inputs></pose_node></node></nodes></AnimationGraphAsset>" ) );
    SW_EXPECT_TRUE( graph._listNode.empty() );
}
