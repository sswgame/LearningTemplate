#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "Editor/Common/Workspace/Undo/DocumentEditRecorder.h"
#include "Editor/Panels/MaterialGraph/Model/MaterialGraphViewModel.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    struct MaterialGraphViewModelTestUtil
    {
        /** @brief 곱하기 둘과 출력이 있는 모델입니다(핀 표가 등록표에서 온다). */
        static void addThreeNodes( MaterialGraphViewModel& model )
        {
            SW_EXPECT_TRUE( model.addNode( "Multiply", float2{ 0.0f, 0.0f } ) );
            SW_EXPECT_TRUE( model.addNode( "Multiply", float2{ 100.0f, 0.0f } ) );
            SW_EXPECT_TRUE( model.addNode( "Output", float2{ 200.0f, 0.0f } ) );
        }
    };
} // namespace

/** @brief [MaterialGraphViewModelTest] 새 노드 이름은 종류 이름 + 겹치지 않는 번호이고 없는 종류는 넣지 않는다 */
SW_TEST_CASE( MaterialGraphViewModelTest, AddNodeNamesUniquelyAndRejectsUnknownKinds )
{
    MaterialGraphViewModel model;
    MaterialGraphViewModelTestUtil::addThreeNodes( model );
    SW_EXPECT_EQUAL( string( "Multiply1" ), model.getGraph()._listNode[0]._name );
    SW_EXPECT_EQUAL( string( "Multiply2" ), model.getGraph()._listNode[1]._name );
    SW_EXPECT_FALSE( model.addNode( "NoSuchKind", float2{ 0.0f, 0.0f } ) );
    SW_EXPECT_EQUAL( size_t{ 3 }, model.getGraph()._listNode.size() );
}

/** @brief [MaterialGraphViewModelTest] 입력 핀 하나는 링크 하나만 받고, 고리를 만드는 연결은 거절한다 */
SW_TEST_CASE( MaterialGraphViewModelTest, ConnectReplacesLinkAndRejectsCycle )
{
    MaterialGraphViewModel model;
    MaterialGraphViewModelTestUtil::addThreeNodes( model );
    const int32 outFirst  = MaterialGraphViewModel::makeOutputPinID( 0, 0 );
    const int32 outSecond = MaterialGraphViewModel::makeOutputPinID( 1, 0 );
    const int32 inFirstA  = MaterialGraphViewModel::makeInputPinID( 0, 0 );
    const int32 inSecondA = MaterialGraphViewModel::makeInputPinID( 1, 0 );
    const int32 inOutput  = MaterialGraphViewModel::makeInputPinID( 2, 0 );

    SW_EXPECT_TRUE( model.connectPins( outFirst, inSecondA ) );
    SW_EXPECT_FALSE( model.connectPins( outSecond, inFirstA ) ); // 1 → 2 → 1
    SW_EXPECT_TRUE( model.getGraph()._listNode[0]._listLink.empty() );

    SW_EXPECT_TRUE( model.connectPins( outFirst, inOutput ) );
    SW_EXPECT_TRUE( model.connectPins( outSecond, inOutput ) );
    SW_REQUIRE_TRUE( model.getGraph()._listNode[2]._listLink.size() == 1 );
    SW_EXPECT_EQUAL( string( "Multiply2" ), model.getGraph()._listNode[2]._listLink[0]._fromNode );
}

/** @brief [MaterialGraphViewModelTest] 노드를 지우면 그 노드를 읽던 링크도 사라진다 */
SW_TEST_CASE( MaterialGraphViewModelTest, RemoveNodeDropsLinksReadingIt )
{
    MaterialGraphViewModel model;
    MaterialGraphViewModelTestUtil::addThreeNodes( model );
    SW_EXPECT_TRUE( model.connectPins( MaterialGraphViewModel::makeOutputPinID( 0, 0 ), MaterialGraphViewModel::makeInputPinID( 2, 0 ) ) );
    model.removeNodes( { MaterialGraphViewModel::makeNodeID( 0 ) } );
    SW_REQUIRE_TRUE( model.getGraph()._listNode.size() == 2 );
    SW_EXPECT_TRUE( model.getGraph()._listNode[1]._listLink.empty() );
}

/** @brief [MaterialGraphViewModelTest] 되돌리기 글로 돌아가면 사본과 핀 표가 함께 돌아온다 */
SW_TEST_CASE( MaterialGraphViewModelTest, RestoreBringsBackGraphAndPins )
{
    MaterialGraphViewModel model;
    MaterialGraphViewModelTestUtil::addThreeNodes( model );
    const string snapshot = model.captureText();
    model.removeNodes( { MaterialGraphViewModel::makeNodeID( 2 ) } );
    model.restore( snapshot );
    SW_REQUIRE_TRUE( model.getGraph()._listNode.size() == 3 );
    SW_EXPECT_EQUAL( size_t{ 6 }, model.getInputPins( 2 ).size() ); // 출력 노드의 표면 칸 여섯
}

/** @brief [MaterialGraphViewModelTest] 컴파일 문제는 노드 이름과 함께 모델에 남고 값을 채우면 사라진다 */
SW_TEST_CASE( MaterialGraphViewModelTest, IssuesFollowEdits )
{
    MaterialGraphViewModel model;
    SW_EXPECT_TRUE( model.addNode( "Output", float2{ 0.0f, 0.0f } ) );
    model.refreshCompile( "game/x/materials/a.matgraph.xml" );
    SW_REQUIRE_TRUE( model.getIssues().size() == 1 );
    SW_EXPECT_EQUAL( string( "MissingPin" ), model.getIssues()[0]._id );
    SW_EXPECT_EQUAL( string( "Output1" ), model.getIssues()[0]._nodeName );

    model.setValue( 0, "BaseColor", "1 0 0" );
    model.refreshCompile( "game/x/materials/a.matgraph.xml" );
    SW_EXPECT_TRUE( model.getIssues().empty() );
}

/** @brief [MaterialGraphViewModelTest] 노드 넣기 한 번은 그리기 끝(publishEdit)에 알림 한 번이고, 이름은 모델이 정한다(감사 E-16) */
SW_TEST_CASE( MaterialGraphViewModelTest, AddNodeNotifiesOnceWithModelName )
{
    uint32                     count{ 0 };
    string                     lastLabel;
    const DocumentEditRecorder recorder{ SW_DELEGATE_LAMBDA( DocumentEditRecorder::RecordDelegate, [&count, &lastLabel]( string_view label )
    {
        ++count;
        lastLabel = string{ label };
    } ) };
    MaterialGraphViewModel     model;
    model.bindDocumentEditRecorder( recorder );
    SW_REQUIRE_TRUE( model.addNode( "Multiply", float2{ 0.0f, 0.0f } ) );
    model.publishEdit();
    model.publishEdit();
    SW_EXPECT_EQUAL( count, 1u );
    SW_EXPECT_TRUE( lastLabel == "Add Material Graph Node" );
}
