#include "pch.h"

#include "Editor/Common/Workspace/EditorWindowTitle.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

/**
 * @brief [EditorWindowTitleTest] 씬 경로의 마지막 조각에서 `.scene.xml` 을 뗀 이름이 게임 이름과 에디터 이름 사이에 선다
 */
SW_TEST_CASE( EditorWindowTitleTest, SceneNameIsTheLastPathSegment )
{
    SW_EXPECT_EQUAL( EditorWindowTitleUtil::makeTitle( "Shooter3D", "game/shooter3d/maps/arena.scene.xml", false, nullptr ),
                     string{ "Shooter3D \xE2\x80\x94 arena \xE2\x80\x94 SW Editor" } );
    // 저장한 적 없는 씬은 경로 대신 이름이 온다 — 접미사도 경로도 없다.
    SW_EXPECT_EQUAL( EditorWindowTitleUtil::makeTitle( "Empty", "Untitled", false, nullptr ), string{ "Empty \xE2\x80\x94 Untitled \xE2\x80\x94 SW Editor" } );
}

/**
 * @brief [EditorWindowTitleTest] 미저장이면 씬 이름 뒤에 `*` 가 붙고, isDirtyTitle 이 그것만 본다
 */
SW_TEST_CASE( EditorWindowTitleTest, DirtyMarkFollowsTheSceneName )
{
    const string dirtyTitle = EditorWindowTitleUtil::makeTitle( "Empty", "game/empty/maps/editortest.scene.xml", true, nullptr );
    SW_EXPECT_EQUAL( dirtyTitle, string{ "Empty \xE2\x80\x94 editortest* \xE2\x80\x94 SW Editor" } );
    SW_EXPECT_TRUE( EditorWindowTitleUtil::isDirtyTitle( dirtyTitle ) );
    SW_EXPECT_FALSE( EditorWindowTitleUtil::isDirtyTitle( EditorWindowTitleUtil::makeTitle( "Empty", "editortest.scene.xml", false, nullptr ) ) );
}

/**
 * @brief [EditorWindowTitleTest] Play · Simulate 중이면 끝에 괄호로 상태가 붙는다
 */
SW_TEST_CASE( EditorWindowTitleTest, PlayStateIsAppended )
{
    SW_EXPECT_EQUAL( EditorWindowTitleUtil::makeTitle( "Empty", "a.scene.xml", true, "Playing" ),
                     string{ "Empty \xE2\x80\x94 a* \xE2\x80\x94 SW Editor (Playing)" } );
    SW_EXPECT_TRUE( EditorWindowTitleUtil::isDirtyTitle( EditorWindowTitleUtil::makeTitle( "Empty", "a.scene.xml", true, "Simulating" ) ) );
}

/**
 * @brief [EditorWindowTitleTest] 활성 씬이 없으면 씬 자리에 `(no scene)` 이 선다
 */
SW_TEST_CASE( EditorWindowTitleTest, NoSceneIsNamed )
{
    SW_EXPECT_EQUAL( EditorWindowTitleUtil::makeTitle( "Empty", "", false, nullptr ), string{ "Empty \xE2\x80\x94 (no scene) \xE2\x80\x94 SW Editor" } );
}
