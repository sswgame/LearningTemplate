/**
 * @file TestEditorCommandRequest.cpp
 * @brief UI 쪽 명령 실행 입구(`EditorCommandRegistry::request`) 계약을 봅니다 — 씬을 바꾸지 않는 명령은 지금 실행하고, 씬 편집 명령은 게임 단계(`runRequestedSceneCommands`)까지 미룬다.
 * @details 등록부는 지역 객체로 만든다(에디터 컨텍스트 없음). 씬 편집 명령의 실행(거래 구간)은 이 테스트가 부르지 않는다 — 미뤄졌는지만 본다.
 */
#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

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

/**
 * @brief [EditorCommandRequestTest] 메뉴 · 단추(View)는 게임 단계가 낸 켜짐 값 묶음만 읽는다 — 내기 전 · 등록부가 바뀐 뒤에는 꺼져 있고, 조건이 바뀌어도 다시 낼 때까지 앞 값이다
 */
SW_TEST_CASE( EditorCommandRequestTest, MenusReadThePublishedEnabledState )
{
    EditorCommandRegistry registry;
    bool                  bCanRun{ true };

    EditorCommandDesc command;
    command._id               = "test.toggled";
    command._edit             = EditorCommandEdit::NoScene;
    command._action           = []( const EditorCommandTarget& /*target*/ ) {};
    command._enabledPredicate = [&bCanRun]( const EditorCommandTarget& /*target*/ )
    { return bCanRun; };
    registry.registerCommand( command );

    SW_EXPECT_FALSE( registry.isEnabledInSnapshot( 0 ) ); // 아직 내지 않았다

    registry.publishEnabledState();
    SW_EXPECT_TRUE( registry.isEnabledInSnapshot( 0 ) );
    SW_EXPECT_FALSE( registry.isEnabledInSnapshot( 1 ) ); // 없는 인덱스

    bCanRun = false;
    SW_EXPECT_TRUE( registry.isEnabledInSnapshot( 0 ) ); // UI 는 낸 값을 읽는다 — 판정을 다시 돌리지 않는다
    registry.publishEnabledState();
    SW_EXPECT_FALSE( registry.isEnabledInSnapshot( 0 ) );

    bCanRun = true;
    registry.publishEnabledState();
    EditorCommandDesc other;
    other._id     = "test.other";
    other._edit   = EditorCommandEdit::NoScene;
    other._action = []( const EditorCommandTarget& /*target*/ ) {};
    registry.registerCommand( other );
    SW_EXPECT_FALSE( registry.isEnabledInSnapshot( 0 ) ); // 등록부가 바뀌면 낡은 값을 읽지 않는다
    registry.publishEnabledState();
    SW_EXPECT_TRUE( registry.isEnabledInSnapshot( 0 ) );
    SW_EXPECT_TRUE( registry.isEnabledInSnapshot( 1 ) );
}

/**
 * @brief [EditorCommandRequestTest] 맥락 메뉴는 그 종류의 항목만 순서대로 모으고, 켜짐은 그 대상으로 판정한 값이다(View 는 이 값만 읽는다)
 */
SW_TEST_CASE( EditorCommandRequestTest, ContextMenuStateIsGatheredForTheTarget )
{
    EditorCommandRegistry registry;

    EditorCommandDesc single;
    single._id               = "test.renameOne";
    single._edit             = EditorCommandEdit::NoScene;
    single._contextMenuKind  = EditorCommandTargetKind::GameObject;
    single._contextMenuOrder = 20;
    single._action           = []( const EditorCommandTarget& /*target*/ ) {};
    single._enabledPredicate = []( const EditorCommandTarget& target )
    { return target.hasSingleSelection(); };
    registry.registerCommand( single );

    EditorCommandDesc always;
    always._id               = "test.copy";
    always._edit             = EditorCommandEdit::NoScene;
    always._contextMenuKind  = EditorCommandTargetKind::GameObject;
    always._contextMenuOrder = 10;
    always._action           = []( const EditorCommandTarget& /*target*/ ) {};
    registry.registerCommand( always );

    EditorCommandDesc otherKind;
    otherKind._id              = "test.assetOnly";
    otherKind._edit            = EditorCommandEdit::NoScene;
    otherKind._contextMenuKind = EditorCommandTargetKind::Asset;
    otherKind._action          = []( const EditorCommandTarget& /*target*/ ) {};
    registry.registerCommand( otherKind );

    EditorCommandTarget target{};
    target._kind           = EditorCommandTargetKind::GameObject;
    target._listSelectedID = { 7, 8 };

    EditorContextMenuState state;
    registry.gatherContextMenu( target, state );
    SW_REQUIRE_EQUAL( size_t{ 2 }, state._listItem.size() );
    SW_REQUIRE_EQUAL( size_t{ 2 }, state._listEnabled.size() );
    SW_EXPECT_TRUE( registry.getCommands()[state._listItem[0]._commandIndex]._id == "test.copy" ); // 순서는 _contextMenuOrder
    SW_EXPECT_EQUAL( uint8{ 1 }, state._listEnabled[0] );
    SW_EXPECT_EQUAL( uint8{ 0 }, state._listEnabled[1] ); // 둘을 골랐다 — 하나에만 뜻이 있는 명령은 꺼진다
    SW_EXPECT_TRUE( state.hasEnabledItem() );

    target._listSelectedID = { 7 };
    registry.gatherContextMenu( target, state );
    SW_EXPECT_EQUAL( uint8{ 1 }, state._listEnabled[1] );
}
