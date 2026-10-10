#include "pch.h"

#include "Editor/Common/Widgets/EditorNodeGraphRules.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    struct TestEditorNodeGraphRulesInternal
    {
        /** @brief 묶음 둘이 섞인 종류 목록입니다(묶음 순서: Flow → Data). */
        static vector<EditorGraphNodeKind> makeKinds()
        {
            return vector<EditorGraphNodeKind>{
                EditorGraphNodeKind{  "Branch", "Flow", 10},
                EditorGraphNodeKind{     "Add", "Data", 20},
                EditorGraphNodeKind{"Sequence", "Flow", 11},
                EditorGraphNodeKind{"Multiply", "Data", 21}
            };
        }
    };
} // namespace

/**
 * @brief [EditorNodeGraphRulesTest] 빈 검색은 전부이고, 묶음이 처음 나온 순서로 묶인다
 */
SW_TEST_CASE( EditorNodeGraphRulesTest, EmptySearchListsEveryKindByCategory )
{
    vector<uint32> listIndex;
    EditorNodeGraphRules::filterNodeKinds( TestEditorNodeGraphRulesInternal::makeKinds(), "", listIndex );
    SW_ASSERT_EQUAL( listIndex.size(), size_t{ 4 } );
    SW_EXPECT_EQUAL( listIndex[0], 0u ); // Flow: Branch
    SW_EXPECT_EQUAL( listIndex[1], 2u ); //       Sequence
    SW_EXPECT_EQUAL( listIndex[2], 1u ); // Data: Add
    SW_EXPECT_EQUAL( listIndex[3], 3u ); //       Multiply
}

/**
 * @brief [EditorNodeGraphRulesTest] 검색은 이름 · 묶음의 대소문자 무시 부분 일치다
 */
SW_TEST_CASE( EditorNodeGraphRulesTest, SearchMatchesNameOrCategory )
{
    vector<uint32> listIndex;
    EditorNodeGraphRules::filterNodeKinds( TestEditorNodeGraphRulesInternal::makeKinds(), "mul", listIndex );
    SW_ASSERT_EQUAL( listIndex.size(), size_t{ 1 } );
    SW_EXPECT_EQUAL( listIndex[0], 3u );
    EditorNodeGraphRules::filterNodeKinds( TestEditorNodeGraphRulesInternal::makeKinds(), "FLOW", listIndex );
    SW_EXPECT_EQUAL( listIndex.size(), size_t{ 2 } );
    EditorNodeGraphRules::filterNodeKinds( TestEditorNodeGraphRulesInternal::makeKinds(), "zzz", listIndex );
    SW_EXPECT_TRUE( listIndex.empty() );
}

/**
 * @brief [EditorNodeGraphRulesTest] 나가는 핀 → 들어오는 핀으로 순서를 맞추고, 와일드카드는 어느 타입과도 잇는다
 */
SW_TEST_CASE( EditorNodeGraphRulesTest, ConnectionOrdersFromOutputToInput )
{
    int32       fromPin{ 0 };
    int32       toPin{ 0 };
    const utf8* pReason = nullptr;
    SW_EXPECT_TRUE( EditorNodeGraphRules::canConnect( 11, EditorGraphPinInfo{ 1, true }, 22, EditorGraphPinInfo{ 1, false }, fromPin, toPin, pReason ) );
    SW_EXPECT_EQUAL( fromPin, 22 );
    SW_EXPECT_EQUAL( toPin, 11 );
    SW_EXPECT_TRUE( EditorNodeGraphRules::canConnect( 5, EditorGraphPinInfo{ 3, false }, 6, EditorGraphPinInfo{ EditorNodeGraphRules::kWildcardPinType, true }, fromPin,
                                                      toPin, pReason ) );
    SW_EXPECT_EQUAL( fromPin, 5 );
}

/**
 * @brief [EditorNodeGraphRulesTest] 같은 방향 · 다른 타입 · 자기 자신은 거절하고 이유를 준다
 */
SW_TEST_CASE( EditorNodeGraphRulesTest, RejectionsCarryReasons )
{
    int32       fromPin{ 0 };
    int32       toPin{ 0 };
    const utf8* pReason = nullptr;
    SW_EXPECT_FALSE( EditorNodeGraphRules::canConnect( 1, EditorGraphPinInfo{ 0, true }, 2, EditorGraphPinInfo{ 0, true }, fromPin, toPin, pReason ) );
    SW_ASSERT_TRUE( pReason != nullptr );
    SW_EXPECT_TRUE( string_view{ pReason } == "Both pins are inputs" );
    SW_EXPECT_FALSE( EditorNodeGraphRules::canConnect( 1, EditorGraphPinInfo{ 0, false }, 2, EditorGraphPinInfo{ 1, true }, fromPin, toPin, pReason ) );
    SW_EXPECT_TRUE( string_view{ pReason } == "Pin types differ" );
    SW_EXPECT_FALSE( EditorNodeGraphRules::canConnect( 4, EditorGraphPinInfo{ 0, false }, 4, EditorGraphPinInfo{ 0, true }, fromPin, toPin, pReason ) );
}

/**
 * @brief [EditorNodeGraphRulesTest] 들어오는 링크가 없는 노드만 문제다(자기 자신으로 도는 링크는 들어온 것으로 치지 않는다)
 */
SW_TEST_CASE( EditorNodeGraphRulesTest, UnreachableNodesAreThoseWithoutIncomingLinks )
{
    const vector<int32>           listInputNode{ 2, 3, 4 };
    const vector<EditorGraphEdge> listEdge{
        EditorGraphEdge{1, 2},
        EditorGraphEdge{2, 3},
        EditorGraphEdge{4, 4}
    };
    vector<EditorGraphNodeIssue> listIssue;
    EditorNodeGraphRules::collectUnreachableNodes( listInputNode, listEdge, listIssue );
    SW_ASSERT_EQUAL( listIssue.size(), size_t{ 1 } );
    SW_EXPECT_EQUAL( listIssue[0]._nodeID, 4 );
    SW_EXPECT_FALSE( listIssue[0]._message.empty() );
}
