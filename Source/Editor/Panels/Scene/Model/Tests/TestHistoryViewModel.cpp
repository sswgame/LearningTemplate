#include "pch.h"

#include "Core/Subsystem/SubsystemScope.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/Undo/CommandStack.h"
#include "Editor/Common/Workspace/Undo/UndoStep.h"
#include "Editor/Panels/Scene/Model/HistoryViewModel.h"

// HistoryViewModelTest — 히스토리 창의 ViewModel: 단계 줄 모으기와 이동 명령. 메인 스택 대신 지역 스택을 범위에 건다.

namespace
{
    struct HistoryViewModelTestInternal
    {
        static void doNothing() {}

        static sw::CommandStack::Command makeCommand( const sw::editor::UndoStepOwner& owner, const utf8* pLabel )
        {
            sw::CommandStack::Command command;
            command._label = pLabel;
            command._undo  = sw::editor::UndoStep::makeTransient( sw::Delegate<void()>::create<&HistoryViewModelTestInternal::doNothing>(), owner );
            command._redo  = sw::editor::UndoStep::makeTransient( sw::Delegate<void()>::create<&HistoryViewModelTestInternal::doNothing>(), owner );
            return command;
        }
    };
} // namespace

/**
 * @brief [HistoryViewModelTest] 단계 줄은 기록 순서이고 지금 위치를 따른다 — 처음 상태로 이동하면 되돌릴 것이 없고 다시 실행할 수 있다
 */
SW_TEST_CASE( HistoryViewModelTest, GatherFollowsJumpTo )
{
    using Internal = HistoryViewModelTestInternal;

    sw::editor::UndoStepOwner owner;
    sw::CommandStack          stack;
    sw::SubsystemScope        scope;
    SW_REQUIRE_TRUE( scope.bind( &stack ) );
    const sw::editor::ScopedEditorScope scopedEditor( scope );
    stack.push( Internal::makeCommand( owner, "First" ) );
    stack.push( Internal::makeCommand( owner, "" ) );

    sw::editor::HistoryViewModel viewModel;
    sw::editor::HistoryViewState state;
    viewModel.gather( state );
    SW_EXPECT_TRUE( state._bAvailable != SW_FALSE );
    SW_REQUIRE_EQUAL( state._listStep.size(), static_cast<size_t>( 2 ) );
    SW_EXPECT_TRUE( state._listStep[0]._label == "First" );
    SW_EXPECT_TRUE( state._listStep[1]._label == "Command" ); // 이름 없는 명령
    SW_EXPECT_EQUAL( state._currentStep, 2u );

    viewModel.jumpTo( 0 );
    viewModel.gather( state );
    SW_EXPECT_EQUAL( state._currentStep, 0u );
    SW_EXPECT_TRUE( state._bCanUndo == SW_FALSE );
    SW_EXPECT_TRUE( state._bCanRedo != SW_FALSE );

    viewModel.redo();
    viewModel.gather( state );
    SW_EXPECT_EQUAL( state._currentStep, 1u );
}
