#include "pch.h"

#include "Editor/Common/Workspace/EditorSceneGenerationSync.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

#include "Engine/Utility/CommandStack.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    struct TestEditorSceneGenerationSyncInternal
    {
        /** @brief undo · redo 가 있는(스택이 받는) 빈 명령입니다. */
        static CommandStack::Command makeProbeCommand()
        {
            CommandStack::Command command;
            command._label = "probe";
            command._undo  = SW_DELEGATE_LAMBDA( Delegate<void()>, []() {} );
            command._redo  = SW_DELEGATE_LAMBDA( Delegate<void()>, []() {} );
            return command;
        }
    };
} // namespace

/**
 * @brief [EditorSceneGenerationSyncTest] 새 씬 세대를 보면 dirty · Undo · 프리팹 격리를 버리고, 같은 세대면 아무것도 건드리지 않는다
 */
SW_TEST_CASE( EditorSceneGenerationSyncTest, NewGenerationDropsTheOldSceneStateOnce )
{
    using Internal = TestEditorSceneGenerationSyncInternal;
    EditorSelection selection;
    EditorWorkspace workspace{ &selection };
    CommandStack    stack;
    stack.push( Internal::makeProbeCommand() );
    workspace.markSceneDirty();
    workspace.pushPrefabIsolation( PrefabIsolationFrame{} );
    SW_ASSERT_TRUE( stack.canUndo() );
    SW_ASSERT_TRUE( workspace.isPrefabIsolationActive() );

    SW_EXPECT_TRUE( EditorSceneGenerationSync::apply( workspace, workspace.getObservedSceneGeneration() + 1, &stack ) );
    SW_EXPECT_FALSE( workspace.isSceneDirty() );
    SW_EXPECT_FALSE( stack.canUndo() );
    SW_EXPECT_FALSE( workspace.isPrefabIsolationActive() );

    // 같은 세대 — 그 뒤의 편집은 그대로 남는다.
    workspace.markSceneDirty();
    stack.push( Internal::makeProbeCommand() );
    SW_EXPECT_FALSE( EditorSceneGenerationSync::apply( workspace, workspace.getObservedSceneGeneration(), &stack ) );
    SW_EXPECT_TRUE( workspace.isSceneDirty() );
    SW_EXPECT_TRUE( stack.canUndo() );
}
