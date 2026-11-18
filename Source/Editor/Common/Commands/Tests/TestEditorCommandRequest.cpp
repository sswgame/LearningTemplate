/**
 * @file TestEditorCommandRequest.cpp
 * @brief UI 쪽 명령 실행 입구(`EditorCommandRegistry::request`) 계약을 봅니다 — 씬을 바꾸지 않는 명령은 지금 실행하고, 씬 편집 명령은 게임 단계(`runRequestedSceneCommands`)까지 미룬다.
 * @details 등록부는 지역 객체로 만든다(에디터 컨텍스트 없음). 씬 편집 명령의 실행(거래 구간)은 이 테스트가 부르지 않는다 — 미뤄졌는지만 본다.
 */
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/Workspace/EditorTransaction.h"

using namespace sw;
using namespace sw::editor;

/**
 * @brief [EditorCommandRequestTest] 씬을 바꾸지 않는 명령은 요청한 자리에서 실행되고, 씬 편집 명령은 요청만 남는다(UI 프레임이 씬을 바꾸지 않는다)
 */
SW_TEST_CASE( EditorCommandRequestTest, SceneEditsWaitForGameStep )
{
    EditorCommandRegistry registry;
    uint32                uiRunCount{ 0 };
    uint32                sceneRunCount{ 0 };

    EditorCommandDesc uiCommand;
    uiCommand._id     = "test.window";
    uiCommand._edit   = EditorCommandEdit::NoScene;
    uiCommand._action = [&uiRunCount]( const EditorCommandTarget& /*target*/ )
    { ++uiRunCount; };
    registry.registerCommand( uiCommand );

    EditorCommandDesc sceneCommand;
    sceneCommand._id          = "test.sceneEdit";
    sceneCommand._edit        = EditorCommandEdit::Scene;
    sceneCommand._sceneAction = [&sceneRunCount]( const EditorCommandTarget& /*target*/, EditorTransaction& /*transaction*/ )
    { ++sceneRunCount; };
    registry.registerCommand( sceneCommand );

    SW_EXPECT_TRUE( registry.request( "test.window", EditorCommandTarget::none() ) );
    SW_EXPECT_EQUAL( 1u, uiRunCount );

    SW_EXPECT_TRUE( registry.request( "test.sceneEdit", EditorCommandTarget::none() ) );
    SW_EXPECT_EQUAL( 0u, sceneRunCount ); // 게임 단계가 실행한다

    SW_EXPECT_FALSE( registry.request( "test.missing", EditorCommandTarget::none() ) );
}
