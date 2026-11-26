/**
 * @file TestEditorSceneFileRequest.cpp
 * @brief UI 가 남기는 씬 파일 요청(`EditorWorkspace::requestLoadScene` · `requestSaveSceneAs`)이 값으로 쌓이고, 게임 단계가 요청한 순서대로 한 번만 꺼내는지 봅니다.
 * @details 실행(열기 · 저장)은 게임 단계 `sceneSession` 이 한다 — 이 테스트는 씬 없이 요청 줄만 본다.
 */
#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

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

/**
 * @brief [EditorWorkspaceResetTest] 새로 띄운 에디터로 되돌리면 앞 단계의 기즈모 · 잠금 · 북마크 · 열기 요청 · 씬 파일 요청이 남지 않고, 편집 번호는 낮아지지 않는다
 */
SW_TEST_CASE( EditorWorkspaceResetTest, ForgetsViewStateAndRequests )
{
    sw::editor::EditorWorkspace workspace{ nullptr };
    const int32                 freshGizmoOperation = workspace.getGizmoOperation();
    const bool                  bFreshLocalSpace    = workspace.isGizmoLocalSpace();
    workspace.setGizmoOperation( freshGizmoOperation + 1 );
    workspace.setGizmoLocalSpace( bFreshLocalSpace == false );
    workspace.setObjectLocked( 7, true );
    sw::editor::CameraBookmark bookmark;
    bookmark._bValid = true;
    workspace.setCameraBookmark( 0, bookmark );
    workspace.setFocusedAssetPath( "game/hub/data/a.xml" );
    workspace.requestOpenPanelByID( "material" );
    workspace.requestLoadScene( "game/hub/maps/a.scene.xml" );
    workspace.setPendingSceneAction( sw::editor::EditorPendingSceneAction::New );
    const uint32 editSerial = workspace.getSceneEditSerial();

    workspace.resetToFreshEditor();

    SW_EXPECT_EQUAL( freshGizmoOperation, workspace.getGizmoOperation() );
    SW_EXPECT_TRUE( workspace.isGizmoLocalSpace() == bFreshLocalSpace );
    SW_EXPECT_FALSE( workspace.isObjectLocked( 7 ) );
    SW_EXPECT_FALSE( workspace.hasCameraBookmark( 0 ) );
    SW_EXPECT_TRUE( workspace.getFocusedAssetPath().empty() );
    SW_EXPECT_TRUE( workspace.getPendingSceneAction() == sw::editor::EditorPendingSceneAction::None );
    SW_EXPECT_EQUAL( editSerial, workspace.getSceneEditSerial() );
    sw::editor::EditorPanelOpenRequest openRequest;
    SW_EXPECT_FALSE( workspace.consumeOpenPanel( openRequest ) );
    sw::vector<sw::editor::EditorSceneFileRequest> listRequest;
    SW_EXPECT_FALSE( workspace.takeSceneFileRequests( listRequest ) );
}
