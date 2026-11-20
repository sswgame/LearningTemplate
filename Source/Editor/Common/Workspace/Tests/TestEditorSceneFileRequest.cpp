/**
 * @file TestEditorSceneFileRequest.cpp
 * @brief UI 가 남기는 씬 파일 요청(`EditorWorkspace::requestLoadScene` · `requestSaveSceneAs`)이 값으로 쌓이고, 게임 단계가 요청한 순서대로 한 번만 꺼내는지 봅니다.
 * @details 실행(열기 · 저장)은 게임 단계 `sceneSession` 이 한다 — 이 테스트는 씬 없이 요청 줄만 본다.
 */
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Workspace/EditorWorkspace.h"

/**
 * @brief [EditorSceneFileRequestTest] 열기 · 다른 이름 저장 요청은 요청한 순서대로 꺼내지고, 꺼낸 뒤에는 비어 있다(대화상자 결과가 씬을 바로 쓰지 않는다)
 */
SW_TEST_CASE( EditorSceneFileRequestTest, RequestsAreTakenInOrderOnce )
{
    sw::editor::EditorWorkspace                    workspace{ nullptr };
    sw::vector<sw::editor::EditorSceneFileRequest> listRequest;

    SW_EXPECT_FALSE( workspace.takeSceneFileRequests( listRequest ) );
    SW_EXPECT_TRUE( listRequest.empty() );

    workspace.requestSaveSceneAs( "C:/Out/a.scene.xml" );
    workspace.requestLoadScene( "game/playground/maps/b.scene.xml" );

    SW_REQUIRE_TRUE( workspace.takeSceneFileRequests( listRequest ) );
    SW_REQUIRE_EQUAL( size_t{ 2 }, listRequest.size() );
    SW_EXPECT_TRUE( listRequest[0]._kind == sw::editor::EditorSceneFileRequestKind::SaveAs );
    SW_EXPECT_TRUE( listRequest[0]._path == "C:/Out/a.scene.xml" );
    SW_EXPECT_TRUE( listRequest[1]._kind == sw::editor::EditorSceneFileRequestKind::Open );
    SW_EXPECT_TRUE( listRequest[1]._path == "game/playground/maps/b.scene.xml" );

    SW_EXPECT_FALSE( workspace.takeSceneFileRequests( listRequest ) );
    SW_EXPECT_TRUE( listRequest.empty() );
}

/**
 * @brief [EditorSceneFileRequestTest] 미저장 확인 창의 선택은 고른 값을 든 요청으로 줄에 서고(창이 저장 · 버리기를 바로 하지 않는다), 앞의 열기 요청 뒤에 온다
 */
SW_TEST_CASE( EditorSceneFileRequestTest, UnsavedChoiceIsARequest )
{
    sw::editor::EditorWorkspace                    workspace{ nullptr };
    sw::vector<sw::editor::EditorSceneFileRequest> listRequest;

    workspace.requestLoadScene( "game/playground/maps/b.scene.xml" );
    workspace.requestUnsavedSceneChoice( sw::editor::EditorUnsavedChoice::Discard );

    SW_REQUIRE_TRUE( workspace.takeSceneFileRequests( listRequest ) );
    SW_REQUIRE_EQUAL( size_t{ 2 }, listRequest.size() );
    SW_EXPECT_TRUE( listRequest[0]._choice == sw::editor::EditorUnsavedChoice::None );
    SW_EXPECT_TRUE( listRequest[1]._kind == sw::editor::EditorSceneFileRequestKind::UnsavedChoice );
    SW_EXPECT_TRUE( listRequest[1]._choice == sw::editor::EditorUnsavedChoice::Discard );
    SW_EXPECT_TRUE( listRequest[1]._path.empty() );
}
