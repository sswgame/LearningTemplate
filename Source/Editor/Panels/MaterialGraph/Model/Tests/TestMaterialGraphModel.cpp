#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Panels/MaterialGraph/Model/MaterialGraphModel.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    struct MaterialGraphModelTestUtil
    {
        /** @brief 곱하기 둘과 출력이 있는 모델입니다(핀 표가 등록표에서 온다). */
        static void addThreeNodes( MaterialGraphModel& model )
        {
            SW_EXPECT_TRUE( model.addNode( "Multiply", float2{ 0.0f, 0.0f } ) );
            SW_EXPECT_TRUE( model.addNode( "Multiply", float2{ 100.0f, 0.0f } ) );
            SW_EXPECT_TRUE( model.addNode( "Output", float2{ 200.0f, 0.0f } ) );
        }
    };
} // namespace

/** @brief [MaterialGraphModelTest] 새 노드 이름은 종류 이름 + 겹치지 않는 번호이고 없는 종류는 넣지 않는다 */
SW_TEST_CASE( MaterialGraphModelTest, AddNodeNamesUniquelyAndRejectsUnknownKinds )
{
    MaterialGraphModel model;
    MaterialGraphModelTestUtil::addThreeNodes( model );
    SW_EXPECT_EQUAL( string( "Multiply1" ), model.getGraph()._listNode[0]._name );
    SW_EXPECT_EQUAL( string( "Multiply2" ), model.getGraph()._listNode[1]._name );
    SW_EXPECT_FALSE( model.addNode( "NoSuchKind", float2{ 0.0f, 0.0f } ) );
    SW_EXPECT_EQUAL( size_t{ 3 }, model.getGraph()._listNode.size() );
}

/** @brief [MaterialGraphModelTest] 입력 핀 하나는 링크 하나만 받고, 고리를 만드는 연결은 거절한다 */
SW_TEST_CASE( MaterialGraphModelTest, ConnectReplacesLinkAndRejectsCycle )
{
    MaterialGraphModel model;
    MaterialGraphModelTestUtil::addThreeNodes( model );
    const int32 outFirst  = MaterialGraphModel::makeOutputPinID( 0, 0 );
    const int32 outSecond = MaterialGraphModel::makeOutputPinID( 1, 0 );
    const int32 inFirstA  = MaterialGraphModel::makeInputPinID( 0, 0 );
    const int32 inSecondA = MaterialGraphModel::makeInputPinID( 1, 0 );
    const int32 inOutput  = MaterialGraphModel::makeInputPinID( 2, 0 );

    SW_EXPECT_TRUE( model.connectPins( outFirst, inSecondA ) );
    SW_EXPECT_FALSE( model.connectPins( outSecond, inFirstA ) ); // 1 → 2 → 1
    SW_EXPECT_TRUE( model.getGraph()._listNode[0]._listLink.empty() );

    SW_EXPECT_TRUE( model.connectPins( outFirst, inOutput ) );
    SW_EXPECT_TRUE( model.connectPins( outSecond, inOutput ) );
    SW_REQUIRE_TRUE( model.getGraph()._listNode[2]._listLink.size() == 1 );
    SW_EXPECT_EQUAL( string( "Multiply2" ), model.getGraph()._listNode[2]._listLink[0]._fromNode );
}

/** @brief [MaterialGraphModelTest] 노드를 지우면 그 노드를 읽던 링크도 사라진다 */
SW_TEST_CASE( MaterialGraphModelTest, RemoveNodeDropsLinksReadingIt )
{
    MaterialGraphModel model;
    MaterialGraphModelTestUtil::addThreeNodes( model );
    SW_EXPECT_TRUE( model.connectPins( MaterialGraphModel::makeOutputPinID( 0, 0 ), MaterialGraphModel::makeInputPinID( 2, 0 ) ) );
    model.removeNodes( { MaterialGraphModel::makeNodeID( 0 ) } );
    SW_REQUIRE_TRUE( model.getGraph()._listNode.size() == 2 );
    SW_EXPECT_TRUE( model.getGraph()._listNode[1]._listLink.empty() );
}

/** @brief [MaterialGraphModelTest] 되돌리기 글로 돌아가면 사본과 핀 표가 함께 돌아온다 */
SW_TEST_CASE( MaterialGraphModelTest, RestoreBringsBackGraphAndPins )
{
    MaterialGraphModel model;
    MaterialGraphModelTestUtil::addThreeNodes( model );
    const string snapshot = model.captureText();
    model.removeNodes( { MaterialGraphModel::makeNodeID( 2 ) } );
    model.restore( snapshot );
    SW_REQUIRE_TRUE( model.getGraph()._listNode.size() == 3 );
    SW_EXPECT_EQUAL( size_t{ 6 }, model.getInputPins( 2 ).size() ); // 출력 노드의 표면 칸 여섯
}

/** @brief [MaterialGraphModelTest] 컴파일 문제는 노드 이름과 함께 모델에 남고 값을 채우면 사라진다 */
SW_TEST_CASE( MaterialGraphModelTest, IssuesFollowEdits )
{
    MaterialGraphModel model;
    SW_EXPECT_TRUE( model.addNode( "Output", float2{ 0.0f, 0.0f } ) );
    model.refreshCompile( "game/x/materials/a.matgraph.xml" );
    SW_REQUIRE_TRUE( model.getIssues().size() == 1 );
    SW_EXPECT_EQUAL( string( "MissingPin" ), model.getIssues()[0]._id );
    SW_EXPECT_EQUAL( string( "Output1" ), model.getIssues()[0]._nodeName );

    model.setValue( 0, "BaseColor", "1 0 0" );
    model.refreshCompile( "game/x/materials/a.matgraph.xml" );
    SW_EXPECT_TRUE( model.getIssues().empty() );
}
