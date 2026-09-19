#include "pch.h"

#include "Editor/Common/Workspace/EditorSessionPolicy.h"

#include "TestFramework/TestFramework.h"

SW_TEST_CASE( EditorSessionPolicyTest, UnsavedPromptFollowsDirtyFlag )
{
    SW_EXPECT_FALSE( sw::editor::EditorSessionPolicy::needsUnsavedPrompt( false ) );
    SW_EXPECT_TRUE( sw::editor::EditorSessionPolicy::needsUnsavedPrompt( true ) );
}

SW_TEST_CASE( EditorSessionPolicyTest, ChoiceRouting )
{
    SW_EXPECT_TRUE( sw::editor::EditorSessionPolicy::shouldSaveBeforeAction( sw::editor::EditorUnsavedChoice::Save ) );
    SW_EXPECT_FALSE( sw::editor::EditorSessionPolicy::shouldSaveBeforeAction( sw::editor::EditorUnsavedChoice::Discard ) );
    SW_EXPECT_FALSE( sw::editor::EditorSessionPolicy::shouldSaveBeforeAction( sw::editor::EditorUnsavedChoice::Cancel ) );

    SW_EXPECT_TRUE( sw::editor::EditorSessionPolicy::shouldProceedWithAction( sw::editor::EditorUnsavedChoice::Save ) );
    SW_EXPECT_TRUE( sw::editor::EditorSessionPolicy::shouldProceedWithAction( sw::editor::EditorUnsavedChoice::Discard ) );
    SW_EXPECT_FALSE( sw::editor::EditorSessionPolicy::shouldProceedWithAction( sw::editor::EditorUnsavedChoice::Cancel ) );
    SW_EXPECT_FALSE( sw::editor::EditorSessionPolicy::shouldProceedWithAction( sw::editor::EditorUnsavedChoice::None ) );

    SW_EXPECT_TRUE( sw::editor::EditorSessionPolicy::shouldClearDirtyWithoutSave( sw::editor::EditorUnsavedChoice::Discard ) );
    SW_EXPECT_FALSE( sw::editor::EditorSessionPolicy::shouldClearDirtyWithoutSave( sw::editor::EditorUnsavedChoice::Save ) );
}

SW_TEST_CASE( EditorSessionPolicyTest, SceneEditsAllowedOnlyWhenStopped )
{
    SW_EXPECT_TRUE( sw::editor::EditorSessionPolicy::areSceneEditsAllowed( true ) );
    SW_EXPECT_FALSE( sw::editor::EditorSessionPolicy::areSceneEditsAllowed( false ) );
}

SW_TEST_CASE( EditorSessionPolicyTest, QuitPromptUsesSceneAndDocumentDirty )
{
    SW_EXPECT_FALSE( sw::editor::EditorSessionPolicy::needsQuitPrompt( false, 0 ) );
    SW_EXPECT_TRUE( sw::editor::EditorSessionPolicy::needsQuitPrompt( true, 0 ) );
    SW_EXPECT_TRUE( sw::editor::EditorSessionPolicy::needsQuitPrompt( false, 1 ) );
    SW_EXPECT_TRUE( sw::editor::EditorSessionPolicy::needsQuitPrompt( true, 2 ) );
}

SW_TEST_CASE( EditorSessionPolicyTest, NodeMoveDirtyOnlyAfterLayoutReady )
{
    SW_EXPECT_FALSE( sw::editor::EditorSessionPolicy::shouldMarkDocumentDirtyOnNodeMove( false, true ) );
    SW_EXPECT_FALSE( sw::editor::EditorSessionPolicy::shouldMarkDocumentDirtyOnNodeMove( true, false ) );
    SW_EXPECT_TRUE( sw::editor::EditorSessionPolicy::shouldMarkDocumentDirtyOnNodeMove( true, true ) );
}

SW_TEST_CASE( EditorSessionPolicyTest, DocumentCoalesceKeyIsPerProperty )
{
    SW_EXPECT_TRUE( sw::editor::EditorSessionPolicy::shouldCoalesceDocumentEdits( "material-prop:Albedo", "material-prop:Albedo" ) );
    SW_EXPECT_FALSE( sw::editor::EditorSessionPolicy::shouldCoalesceDocumentEdits( "material-prop:Albedo", "material-prop:Roughness" ) );
    SW_EXPECT_FALSE( sw::editor::EditorSessionPolicy::shouldCoalesceDocumentEdits( "", "material-prop:Albedo" ) );
    SW_EXPECT_FALSE( sw::editor::EditorSessionPolicy::shouldCoalesceDocumentEdits( "material-prop:Albedo", "" ) );
}

SW_TEST_CASE( EditorSessionPolicyTest, RestoreClearsDirtyWhenMatchingLastSave )
{
    SW_EXPECT_TRUE( sw::editor::EditorSessionPolicy::shouldClearDocumentDirtyOnRestore( true ) );
    SW_EXPECT_FALSE( sw::editor::EditorSessionPolicy::shouldClearDocumentDirtyOnRestore( false ) );
}

SW_TEST_CASE( EditorSessionPolicyTest, PrefabIsolationDoesNotRequireCleanScene )
{
    SW_EXPECT_FALSE( sw::editor::EditorSessionPolicy::requiresCleanSceneForPrefabIsolation() );
}

/**
 * @brief [EditorSessionPolicyTest] 축 하나만 움직여도 **움직인 것**이다
 * @details 그래프 패널 둘이 이 판단을 각자 적고 있었고 연산자가 서로 달랐다 — 애니메이션은 `||`,
 *          대화는 `&&`. `&&` 쪽에서는 노드를 정확히 수평으로만(또는 수직으로만) 옮기면 "안 움직였다"
 *          가 되어 그 레이아웃 변경이 dirty 로 잡히지 않고 **조용히 사라졌다.** 캔버스 정렬이
 *          축 하나만 움직이는 일을 흔하게 만들므로 드문 경우도 아니다.
 * @note 그래서 이 케이스의 핵심은 아래 **한 축만 움직인 두 줄**이다. 둘 다 true 여야 한다.
 */
SW_TEST_CASE( EditorSessionPolicyTest, NodeCountsAsMovedWhenEitherAxisChanges )
{
    using Policy = sw::editor::EditorSessionPolicy;

    // 아무것도 안 움직였다.
    SW_EXPECT_FALSE( Policy::hasNodeMoved( 10.0f, 20.0f, 10.0f, 20.0f ) );

    // 한 축만 움직였다 — `&&` 로 적으면 여기서 걸린다.
    SW_EXPECT_TRUE_MSG( Policy::hasNodeMoved( 10.0f, 20.0f, 33.0f, 20.0f ),
                        "수평으로만 옮긴 노드를 '안 움직였다' 고 봅니다 — 레이아웃이 저장되지 않습니다" );
    SW_EXPECT_TRUE_MSG( Policy::hasNodeMoved( 10.0f, 20.0f, 10.0f, 44.0f ),
                        "수직으로만 옮긴 노드를 '안 움직였다' 고 봅니다 — 레이아웃이 저장되지 않습니다" );

    // 둘 다 움직였다.
    SW_EXPECT_TRUE( Policy::hasNodeMoved( 10.0f, 20.0f, 33.0f, 44.0f ) );

    // dirty 로 치는 것은 레이아웃이 한 번 동기된 뒤부터다 — 두 규칙이 짝으로 동작하는지 함께 본다.
    SW_EXPECT_FALSE( Policy::shouldMarkDocumentDirtyOnNodeMove( false, true ) );
    SW_EXPECT_TRUE( Policy::shouldMarkDocumentDirtyOnNodeMove( true, true ) );
}

/**
 * @brief 컨테이너 원소 편집은 **두 조건이 모두** 맞아야 열린다.
 * @details 인스펙터는 이 둘을 따로 물었고, 그래서 `ReadOnly` 컨테이너의 원소가 그대로 편집됐다
 *          (읽기 전용 표시는 `+ Add`·`Clear` 버튼만 가리고 있었다). 연관 컨테이너에서는 그 편집이
 *          정렬 키를 제자리에서 바꾸는 일이라 트리를 깨뜨린다.
 */
SW_TEST_CASE( EditorSessionPolicyTest, ContainerElementEditsNeedBothConditions )
{
    using Policy = sw::editor::EditorSessionPolicy;

    // 편집 가능한 유일한 조합 — 읽기 전용이 아니고, 컨테이너가 제자리 쓰기를 허용한다.
    SW_EXPECT_TRUE( Policy::areContainerElementEditsAllowed( false, true ) );

    SW_EXPECT_FALSE_MSG( Policy::areContainerElementEditsAllowed( true, true ),
                         "ReadOnly 컨테이너의 원소가 편집 가능하면 안 됩니다" );
    SW_EXPECT_FALSE_MSG( Policy::areContainerElementEditsAllowed( false, false ),
                         "제자리 쓰기를 거부한 컨테이너(set 등)의 원소가 편집 가능하면 안 됩니다" );
    SW_EXPECT_FALSE( Policy::areContainerElementEditsAllowed( true, false ) );
}
