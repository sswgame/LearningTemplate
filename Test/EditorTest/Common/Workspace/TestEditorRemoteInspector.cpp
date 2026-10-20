#include "pch.h"

#include "Editor/Common/Workspace/EditorRemoteInspector.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

/**
 * @brief [EditorRemoteInspectorTest] 채널 `help` 답에서 개발 명령 이름(사용법의 첫 낱말)과 서버 루프 · 콘솔 내장 이름을 뽑아 이름 순으로 담는다
 */
SW_TEST_CASE( EditorRemoteInspectorTest, HelpAnswerGivesTheCompletionNames )
{
    const string_view helpBody = "server loop: pause | resume | step [N]\n"
                                 "get <variable> | set <variable> <value> | <variable> [value] | help [prefix]\n"
                                 "  object.dump <name> - Print the components\n"
                                 "  scene.tree [limit] - List the objects\n"
                                 "  net.stats - One line of stats";
    vector<string>    listName;
    EditorRemoteInspector::parseCommandNames( helpBody, listName );
    const vector<string> listExpected{ "get", "help", "net.stats", "object.dump", "pause", "resume", "scene.tree", "set", "step" };
    SW_EXPECT_TRUE( listName == listExpected );
}

/**
 * @brief [EditorRemoteInspectorTest] `scene.tree` 답을 줄마다 나눈다 — 첫 줄 · 한도 줄은 건너뛰고, 이름의 빈칸 · 이름 없는 오브젝트 · 비활성 줄을 그대로 옮긴다
 */
SW_TEST_CASE( EditorRemoteInspectorTest, TreeAnswerSplitsIntoRows )
{
    const string_view             treeBody = "objects 3\n"
                                             "4 Main Camera parent=- active=1 components=TransformComponent,CameraComponent\n"
                                             "9 <unnamed> parent=Main Camera active=0 components=\n"
                                             "12 Crate parent=- active=1 components=TransformComponent\n"
                                             "... (limit reached)\n";
    vector<EditorRemoteObjectRow> listRow;
    SW_ASSERT_TRUE( EditorRemoteInspector::parseObjectRows( treeBody, listRow ) );
    SW_ASSERT_EQUAL( size_t{ 3 }, listRow.size() );
    SW_EXPECT_EQUAL( uint64{ 4 }, listRow[0]._objectID );
    SW_EXPECT_TRUE( listRow[0]._name == "Main Camera" );
    SW_EXPECT_TRUE( listRow[0]._parentName == "-" );
    SW_EXPECT_TRUE( listRow[0]._bActive );
    SW_EXPECT_TRUE( listRow[0]._components == "TransformComponent,CameraComponent" );
    SW_EXPECT_TRUE( listRow[1]._name == EditorRemoteInspector::kUnnamedObject );
    SW_EXPECT_TRUE( listRow[1]._parentName == "Main Camera" );
    SW_EXPECT_FALSE( listRow[1]._bActive );
    SW_EXPECT_TRUE( listRow[1]._components.empty() );
    SW_EXPECT_EQUAL( uint64{ 12 }, listRow[2]._objectID );
}

/**
 * @brief [EditorRemoteInspectorTest] 형식이 다른 답(오류 글 · 칸이 빠진 줄)은 false 다 — 빈 표로 조용히 넘어가지 않는다
 */
SW_TEST_CASE( EditorRemoteInspectorTest, MalformedTreeAnswerIsRejected )
{
    vector<EditorRemoteObjectRow> listRow;
    SW_EXPECT_FALSE( EditorRemoteInspector::parseObjectRows( "no active scene", listRow ) );
    SW_EXPECT_FALSE( EditorRemoteInspector::parseObjectRows( "objects 1\n7 Crate active=1 components=Transform\n", listRow ) );
    SW_EXPECT_FALSE( EditorRemoteInspector::parseObjectRows( "objects 1\nx Crate parent=- active=1 components=Transform\n", listRow ) );
}

/**
 * @brief [EditorRemoteInspectorTest] 붙지 않은 검사기의 요청은 false 이고 이유를 남긴다 — 서버가 없을 때 빈 하이어라키를 "읽었다" 고 하지 않는다
 */
SW_TEST_CASE( EditorRemoteInspectorTest, RequestsWithoutAConnectionFail )
{
    EditorRemoteInspector inspector;
    SW_EXPECT_FALSE( inspector.isConnected() );
    SW_EXPECT_FALSE( inspector.refreshTree() );
    SW_EXPECT_FALSE( inspector.getLastError().empty() );
    SW_EXPECT_FALSE( inspector.execute( "scene.tree" ) );
    SW_ASSERT_EQUAL( size_t{ 2 }, inspector.getConsoleLines().size() );
    SW_EXPECT_TRUE( inspector.getConsoleLines()[0] == "> scene.tree" );
}
