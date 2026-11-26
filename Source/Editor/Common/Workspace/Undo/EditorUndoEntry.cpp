// 포커스 창의 되돌리기 스택을 움직이는 입구 하나(편집 잠금 판정)입니다.
#include "pch.h"

#include "Editor/Common/Workspace/Undo/EditorUndoEntry.h"

#include "Editor/Common/Workspace/EditorSubsystems.h"
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
        };
    } // namespace

    bool EditorUndoEntry::undo()
    {
        CommandStack* const pStack = EditorUndoEntryInternal::findUnlockedStack();
        return pStack != nullptr && pStack->undo();
    }

    bool EditorUndoEntry::redo()
    {
        CommandStack* const pStack = EditorUndoEntryInternal::findUnlockedStack();
        return pStack != nullptr && pStack->redo();
    }

    bool EditorUndoEntry::jumpTo( size_t stepIndex )
    {
        CommandStack* const pStack = EditorUndoEntryInternal::findUnlockedStack();
        if ( pStack == nullptr || pStack->isEditOpen() )
            return false;
        pStack->jumpTo( stepIndex );
        return true;
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
