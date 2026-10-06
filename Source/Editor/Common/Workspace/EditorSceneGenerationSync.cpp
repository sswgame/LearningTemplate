#include "pch.h"

#include "Editor/Common/Workspace/EditorSceneGenerationSync.h"

#include "Editor/Common/Workspace/EditorWorkspace.h"

#include "Engine/Utility/CommandStack.h"

namespace sw::editor
{
    bool EditorSceneGenerationSync::apply( EditorWorkspace& workspace, uint64 generation, CommandStack* pCommandStack )
    {
        if ( generation == workspace.getObservedSceneGeneration() )
            return false;
        workspace.setObservedSceneGeneration( generation );
        workspace.clearSceneDirty();
        workspace.clearSelection();
        if ( pCommandStack != nullptr )
            pCommandStack->clear();
        workspace.clearPrefabIsolation();
        return true;
    }
} // namespace sw::editor
