#include "pch.h"

#include "Core/Subsystem/SubsystemScope.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/Undo/CommandStack.h"
#include "Editor/Common/Workspace/Undo/UndoStep.h"
#include "Editor/Panels/Scene/Model/HistoryViewModel.h"

// HistoryViewModelTest — 히스토리 창의 ViewModel. 창이 스택을 직접 읽던 것을 값(ViewState)으로 옮겼으므로 이름 · 위치 · 이동이 스택과 같은지 본다.

namespace
{
    struct HistoryViewModelTestInternal
    {
        static void doNothing() {}

        static sw::CommandStack::Command makeCommand( const sw::editor::UndoStepOwner& owner, const char* pLabel )
        {
            sw::CommandStack::Command cmd;
            cmd._label = pLabel;
            cmd._undo  = sw::editor::UndoStep::makeTransient( sw::Delegate<void()>::create<&HistoryViewModelTestInternal::doNothing>(), owner, 0u );
            cmd._redo  = sw::editor::UndoStep::makeTransient( sw::Delegate<void()>::create<&HistoryViewModelTestInternal::doNothing>(), owner, 0u );
            return cmd;
        }
    };
} // namespace

/** @brief [HistoryViewModelTest] gather 는 이름(빈 이름은 "Command") · 지금 위치를 담고, jumpTo · undo · redo 가 범위의 스택을 움직인다 */
SW_TEST_CASE( HistoryViewModelTest, GatherAndJumpFollowTheStack )
{
    using Internal = HistoryViewModelTestInternal;

    sw::editor::UndoStepOwner owner;
    sw::CommandStack          stack{
        sw::UndoBufferLimit{ 128u, 1ull << 20 }
    };
    stack.push( Internal::makeCommand( owner, "Move" ) );
    stack.push( Internal::makeCommand( owner, "" ) );

    sw::SubsystemScope scope;
    SW_REQUIRE_TRUE( scope.bind( &stack ) );
    const sw::editor::ScopedEditorScope scopedEditor( scope );

    sw::editor::HistoryViewModel viewModel;
    sw::editor::HistoryViewState state;
    viewModel.gather( state );
    SW_EXPECT_TRUE( state._bAvailable );
    SW_REQUIRE_TRUE( state._listLabel.size() == 2u );
    SW_EXPECT_TRUE( state._listLabel[0] == "Move" );
    SW_EXPECT_TRUE( state._listLabel[1] == "Command" );
    SW_EXPECT_TRUE( state._currentIndex == 2u );
    SW_EXPECT_TRUE( state._bCanUndo && state._bCanRedo == false );

    viewModel.jumpTo( 0 );
    viewModel.gather( state );
    SW_EXPECT_TRUE( state._currentIndex == 0u );
    SW_EXPECT_TRUE( state._bCanUndo == false && state._bCanRedo );

    viewModel.redo();
    viewModel.gather( state );
    SW_EXPECT_TRUE( state._currentIndex == 1u );

    viewModel.undo();
    viewModel.gather( state );
    SW_EXPECT_TRUE( state._currentIndex == 0u );
}

/** @brief [HistoryViewModelTest] History 의 이동은 Ctrl+Z 와 같은 입구다 — 스택에 열린 편집(끌기 상호작용)이 있으면 jumpTo · undo · 지우기가 아무것도 바꾸지 않는다(감사 E-2) */
SW_TEST_CASE( HistoryViewModelTest, RefusedWhileEditOpen )
{
    using Internal = HistoryViewModelTestInternal;

    sw::editor::UndoStepOwner owner;
    sw::CommandStack          stack{
        sw::UndoBufferLimit{ 128u, 1ull << 20 }
    };
    stack.push( Internal::makeCommand( owner, "Move" ) );

    sw::SubsystemScope scope;
    SW_REQUIRE_TRUE( scope.bind( &stack ) );
    const sw::editor::ScopedEditorScope scopedEditor( scope );

    sw::editor::HistoryViewModel     viewModel;
    sw::CommandStackInteractionScope interaction = stack.openInteraction();
    viewModel.jumpTo( 0 );
    viewModel.undo();
    viewModel.clear();
    SW_EXPECT_EQUAL( size_t{ 1 }, stack.getCommandCount() );
    SW_EXPECT_EQUAL( size_t{ 1 }, stack.getCurrentIndex() );

    sw::editor::HistoryViewState state;
    viewModel.gather( state );
    SW_EXPECT_FALSE( state._bCanUndo ); // 끄는 동안 단추가 꺼진다

    interaction.reset();
    viewModel.jumpTo( 0 );
    SW_EXPECT_EQUAL( size_t{ 0 }, stack.getCurrentIndex() );
}
