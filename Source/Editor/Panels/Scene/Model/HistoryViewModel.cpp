#include "pch.h"

#include "Editor/Panels/Scene/Model/HistoryViewModel.h"

#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/Undo/CommandStack.h"

namespace sw::editor
{
    void HistoryViewModel::gather( HistoryViewState& outState ) const
    {
        outState._listStep.clear();
        outState._currentStep = 0;
        outState._bCanUndo    = SW_FALSE;
        outState._bCanRedo    = SW_FALSE;
        // 선택 서비스: 커맨드 스택은 EditorModule 보다 오래 살아 종료할 때 결합이 먼저 풀린다.
        const CommandStack* const pStack = editor::findSubsystem<CommandStack>();
        outState._bAvailable             = pStack != nullptr ? SW_TRUE : SW_FALSE;
        if ( pStack == nullptr )
            return;
        outState._currentStep = static_cast<uint32>( pStack->getCurrentIndex() );
        outState._bCanUndo    = pStack->canUndo() ? SW_TRUE : SW_FALSE;
        outState._bCanRedo    = pStack->canRedo() ? SW_TRUE : SW_FALSE;
        const size_t count    = pStack->getCommandCount();
        for ( size_t index = 0; index < count; ++index )
        {
            const CommandStack::Command& command = pStack->getCommand( index );
            HistoryStepRow&              row     = outState._listStep.emplace_back();
            row._step                            = static_cast<uint32>( index + 1 );
            row._label                           = command._label.empty() == false ? command._label.c_str() : "Command";
        }
    }

    void HistoryViewModel::undo()
    {
        if ( CommandStack* pStack = editor::findSubsystem<CommandStack>() )
            pStack->undo();
    }

    void HistoryViewModel::redo()
    {
        if ( CommandStack* pStack = editor::findSubsystem<CommandStack>() )
            pStack->redo();
    }

    void HistoryViewModel::jumpTo( uint32 step )
    {
        if ( CommandStack* pStack = editor::findSubsystem<CommandStack>() )
            pStack->jumpTo( step );
    }

    void HistoryViewModel::clear()
    {
        if ( CommandStack* pStack = editor::findSubsystem<CommandStack>() )
            pStack->clear();
    }
} // namespace sw::editor
