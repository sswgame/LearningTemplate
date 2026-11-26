#include "pch.h"

#include "Core/Subsystem/SubsystemScope.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Workspace/EditorSubsystems.h"
#include "Editor/Common/Workspace/Undo/CommandStack.h"
#include "Editor/Common/Workspace/Undo/EditorUndoEntry.h"
#include "Editor/Common/Workspace/Undo/EditorUndoHistory.h"
#include "Editor/Common/Workspace/Undo/UndoStep.h"

// HistoryViewModelTest — 히스토리 창이 읽는 값(`EditorUndoHistorySnapshot`)과 이동의 실행(`EditorUndoEntry`). ViewModel 은 이 값을 옮기고 이동을 게임 단계로 요청만 하므로,
// 값의 이름 · 위치와 실행 쪽의 이동 · 거절을 직접 본다(실행 중인 에디터 안에서 돌아도 그 에디터의 기록을 건드리지 않는다).

namespace
{
    struct HistoryViewModelTestInternal
    {
        static void doNothing() {}

        static sw::CommandStack::Command makeCommand( const sw::editor::UndoStepOwner& owner, const char* pLabel )
        {
            sw::CommandStack::Command cmd;
            cmd._label = pLabel;
            cmd._undo  = sw::editor::UndoStep::makeTransient( SW_DELEGATE_FUNCTION( sw::Delegate<void()>, &HistoryViewModelTestInternal::doNothing ), owner, 0u );
            cmd._redo  = sw::editor::UndoStep::makeTransient( SW_DELEGATE_FUNCTION( sw::Delegate<void()>, &HistoryViewModelTestInternal::doNothing ), owner, 0u );
            return cmd;
        }
    };
} // namespace

/** @brief [HistoryViewModelTest] 값 사본은 이름(빈 이름은 "Command") · 지금 위치를 담고, jumpTo · undo · redo 의 실행이 범위의 스택을 움직인다 */
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

    sw::editor::EditorUndoHistorySnapshot state;
    state.captureFrom( &stack );
    SW_EXPECT_TRUE( state._bAvailable );
    SW_REQUIRE_TRUE( state._listLabel.size() == 2u );
    SW_EXPECT_TRUE( state._listLabel[0] == "Move" );
    SW_EXPECT_TRUE( state._listLabel[1] == "Command" );
    SW_EXPECT_TRUE( state._currentIndex == 2u );
    SW_EXPECT_TRUE( state._bCanUndo && state._bCanRedo == false );

    SW_EXPECT_TRUE( sw::editor::EditorUndoEntry::jumpTo( 0 ) );
    state.captureFrom( &stack );
    SW_EXPECT_TRUE( state._currentIndex == 0u );
    SW_EXPECT_TRUE( state._bCanUndo == false && state._bCanRedo );

    SW_EXPECT_TRUE( sw::editor::EditorUndoEntry::redo() );
    state.captureFrom( &stack );
    SW_EXPECT_TRUE( state._currentIndex == 1u );

    SW_EXPECT_TRUE( sw::editor::EditorUndoEntry::undo() );
    state.captureFrom( &stack );
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

    sw::CommandStackInteractionScope interaction = stack.openInteraction();
    SW_EXPECT_FALSE( sw::editor::EditorUndoEntry::jumpTo( 0 ) );
    SW_EXPECT_FALSE( sw::editor::EditorUndoEntry::undo() );
    SW_EXPECT_FALSE( sw::editor::EditorUndoEntry::clear() );
    SW_EXPECT_EQUAL( size_t{ 1 }, stack.getCommandCount() );
    SW_EXPECT_EQUAL( size_t{ 1 }, stack.getCurrentIndex() );

    sw::editor::EditorUndoHistorySnapshot state;
    state.captureFrom( &stack );
    SW_EXPECT_FALSE( state._bCanUndo ); // 끄는 동안 단추가 꺼진다

    interaction.reset();
    SW_EXPECT_TRUE( sw::editor::EditorUndoEntry::jumpTo( 0 ) );
    SW_EXPECT_EQUAL( size_t{ 0 }, stack.getCurrentIndex() );
}
