#include "pch.h"

#include "Editor/Panels/SourceControl/Model/SourceControlChangesViewModel.h"

#include "Editor/Common/SourceControl/EditorSourceControl.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"

namespace sw::editor
{
    EditorSourceControl& SourceControlChangesViewModel::getSourceControl()
    {
        return editor::getSubsystem<EditorContext>().getSubsystem<EditorSourceControl>();
    }

    EditorSourceControl* SourceControlChangesViewModel::findSourceControl()
    {
        EditorContext* pContext = editor::findSubsystem<EditorContext>();
        return pContext != nullptr ? &pContext->getSubsystem<EditorSourceControl>() : nullptr;
    }
} // namespace sw::editor
