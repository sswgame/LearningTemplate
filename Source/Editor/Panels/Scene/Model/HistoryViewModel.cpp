#include "pch.h"

#include "Editor/Panels/Scene/Model/HistoryViewModel.h"

#include "Editor/Common/Commands/EditorCommandIDs.h"
#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/Commands/EditorCommandTarget.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/Undo/EditorUndoEntry.h"
#include "Editor/Common/Workspace/Undo/EditorUndoHistory.h"

namespace sw::editor
{
    namespace
    {
        struct HistoryViewModelInternal
        {
            /** @brief 되돌리기 스택 움직임 하나를 게임 단계에 넘깁니다 — 실행은 Ctrl+Z 와 같은 입구다(`EditorUndoEntry` — 편집 잠금 · 열린 편집을 같은 판정으로 거절한다, 감사 E-2). */
            template <typename TMove>
            static void requestMove( const TMove& move )
            {
                EditorCommandRegistry::getInEditorContext().requestHistoryMove( SW_DELEGATE_LAMBDA( Delegate<void()>, move ) );
            }
        };
    } // namespace

    void HistoryViewModel::gather( HistoryViewState& outState ) const
    {
        // 스택이 아니라 게임 단계 끝이 지은 스냅샷을 읽는다(`EditorUndoHistory::publishSnapshot`). 메인 기록은 선택 서비스다 — 종료할 때 결합이 먼저 풀린다(그때 비운다).
        const EditorUndoHistory* const pHistory = editor::findSubsystem<EditorUndoHistory>();
        if ( pHistory == nullptr || pHistory->getSnapshot()._bAvailable == SW_FALSE )
        {
            outState._bAvailable = false;
            outState._listLabel.clear();
            outState._currentIndex = 0;
            outState._bCanUndo     = false;
            outState._bCanRedo     = false;
            return;
        }
        const EditorUndoHistorySnapshot& snapshot = pHistory->getSnapshot();
        outState._bAvailable                      = true;
        outState._listLabel                       = snapshot._listLabel;
        outState._currentIndex                    = snapshot._currentIndex;
        outState._bCanUndo                        = snapshot._bCanUndo != SW_FALSE;
        outState._bCanRedo                        = snapshot._bCanRedo != SW_FALSE;
    }

    uint64 HistoryViewModel::getGatherGeneration() const
    {
        const EditorUndoHistory* const pHistory = editor::findSubsystem<EditorUndoHistory>();
        return pHistory != nullptr ? pHistory->getSnapshot()._generation : 0;
    }

    void HistoryViewModel::undo()
    {
        (void)EditorCommandRegistry::getInEditorContext().request( editorcommand::kEditUndo, EditorCommandTarget::none() ); // Ctrl+Z 와 같은 명령 — 게임 단계가 실행한다
    }

    void HistoryViewModel::redo()
    {
        (void)EditorCommandRegistry::getInEditorContext().request( editorcommand::kEditRedo, EditorCommandTarget::none() );
    }

    void HistoryViewModel::clear()
    {
        HistoryViewModelInternal::requestMove( []()
        { (void)EditorUndoEntry::clear(); } );
    }

    void HistoryViewModel::jumpTo( size_t stepIndex )
    {
        HistoryViewModelInternal::requestMove( [stepIndex]()
        { (void)EditorUndoEntry::jumpTo( stepIndex ); } );
    }
} // namespace sw::editor
