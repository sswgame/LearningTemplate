#include "pch.h"

#include "Editor/Panels/SourceControl/Model/SourceControlChangesModel.h"

#include "Editor/Common/SourceControl/EditorSourceControl.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"

namespace sw::editor
{
    EditorSourceControl* SourceControlChangesModel::findSourceControl()
    {
        EditorContext* const pContext = editor::findSubsystem<EditorContext>();
        return pContext != nullptr ? &pContext->getSubsystem<EditorSourceControl>() : nullptr;
    }

    EditorSourceControl& SourceControlChangesModel::getSourceControl()
    {
        return editor::getSubsystem<EditorContext>().getSubsystem<EditorSourceControl>();
    }
} // namespace sw::editor
