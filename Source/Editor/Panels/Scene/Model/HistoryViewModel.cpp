#include "pch.h"

#include "Editor/Panels/Scene/Model/HistoryViewModel.h"

#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/Undo/CommandStack.h"
#include "Editor/Common/Workspace/Undo/EditorUndoEntry.h"

namespace sw::editor
{
    namespace
    {
        struct HistoryViewModelInternal
        {
            /**
             * @brief Ctrl+Z 가 가는 스택(포커스한 문서 패널의 스택, 없으면 메인 스택)입니다 — 문서마다 스택이라(감사 E-3) 메인만 보이면 문서 기록이 보이지 않는다.
             *        선택 서비스: 메인 스택은 EditorModule 보다 오래 살아 종료할 때 결합이 먼저 풀린다(그때 nullptr).
             */
            static CommandStack* findStack() { return editor::getFocusedCommandStack(); }
        };
    } // namespace

    void HistoryViewModel::gather( HistoryViewState& outState ) const
    {
        const CommandStack* pStack = HistoryViewModelInternal::findStack();
        outState._bAvailable       = pStack != nullptr;
        if ( pStack == nullptr )
        {
            outState._listLabel.clear();
            outState._currentIndex = 0;
            outState._bCanUndo     = false;
            outState._bCanRedo     = false;
            return;
        }
        const size_t count = pStack->getCommandCount();
        outState._listLabel.resize( count );
        for ( size_t index = 0; index < count; ++index )
        {
            const string& label        = pStack->getCommand( index )._label;
            outState._listLabel[index] = label.empty() == false ? label : string( "Command" );
        }
        outState._currentIndex = pStack->getCurrentIndex();
        outState._bCanUndo     = pStack->canUndo();
        outState._bCanRedo     = pStack->canRedo();
    }

    // 움직임은 Ctrl+Z 와 같은 입구다(`EditorUndoEntry`) — 편집 잠금(플레이 · 베이크)과 열린 편집(끌기 · 거래)을 같은 판정으로 거절한다(감사 E-2).
    void HistoryViewModel::undo()
    {
        (void)EditorUndoEntry::undo();
    }

    void HistoryViewModel::redo()
    {
        (void)EditorUndoEntry::redo();
    }

    void HistoryViewModel::clear()
    {
        (void)EditorUndoEntry::clear();
    }

    void HistoryViewModel::jumpTo( size_t stepIndex )
    {
        (void)EditorUndoEntry::jumpTo( stepIndex );
    }
} // namespace sw::editor
