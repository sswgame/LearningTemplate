// 포커스 창의 되돌리기 스택을 움직이는 입구 하나(편집 잠금 판정)입니다.
#include "pch.h"

#include "Editor/Common/Workspace/Undo/EditorUndoEntry.h"

#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorTransaction.h"
#include "Editor/Common/Workspace/Play/EditorPlaySession.h"
#include "Editor/Common/Workspace/Undo/CommandStack.h"

namespace sw::editor
{
    namespace
    {
        struct EditorUndoEntryInternal
        {
            /** @brief 편집 잠금이 풀려 있으면 포커스 창의 스택, 아니면 nullptr 입니다. */
            static CommandStack* findUnlockedStack()
            {
                if ( EditorPlaySession::areSceneEditsAllowed() == false )
                    return nullptr;
                return getFocusedCommandStack();
            }

            /**
             * @brief 스택을 움직인 뒤 그 알림이 남긴 선택 고칠 일을 메인 선택에 입힙니다. 이 입구는 UI 쪽(단축키 · History 창)이라 선택을 맞추는 것도 여기다 —
             *        기록 · 되돌리기 실행은 선택을 모른다(`EditorSelectionUpdateQueue`). 다음 UI 프레임까지 미루면 그 프레임의 인스펙터 · 기즈모가 지운 오브젝트를 잡는다.
             */
            static bool applySelectionUpdates( bool bMoved )
            {
                EditorObjectEditBindings* const pBindings = findSubsystem<EditorObjectEditBindings>();
                if ( pBindings != nullptr && findSubsystem<EditorContext>() != nullptr )
                    pBindings->applySelectionUpdates( getMainSceneSelection() );
                return bMoved;
            }
        };
    } // namespace

    bool EditorUndoEntry::undo()
    {
        CommandStack* const pStack = EditorUndoEntryInternal::findUnlockedStack();
        return EditorUndoEntryInternal::applySelectionUpdates( pStack != nullptr && pStack->undo() );
    }

    bool EditorUndoEntry::redo()
    {
        CommandStack* const pStack = EditorUndoEntryInternal::findUnlockedStack();
        return EditorUndoEntryInternal::applySelectionUpdates( pStack != nullptr && pStack->redo() );
    }

    bool EditorUndoEntry::jumpTo( size_t stepIndex )
    {
        CommandStack* const pStack = EditorUndoEntryInternal::findUnlockedStack();
        if ( pStack == nullptr || pStack->isEditOpen() )
            return false;
        pStack->jumpTo( stepIndex );
        return EditorUndoEntryInternal::applySelectionUpdates( true );
    }

    bool EditorUndoEntry::clear()
    {
        CommandStack* const pStack = EditorUndoEntryInternal::findUnlockedStack();
        if ( pStack == nullptr || pStack->isEditOpen() )
            return false;
        pStack->clear();
        return true;
    }
} // namespace sw::editor
