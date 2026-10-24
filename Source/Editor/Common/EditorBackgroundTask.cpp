#include "pch.h"

#include "Editor/Common/EditorBackgroundTask.h"

#include "Core/Task/TaskManager.h"

#include "Editor/Common/Workspace/EditorService.h"

namespace sw::editor
{
    void submitEditorBackgroundJob( string_view name, const TaskArgsDelegate& delegate, const TaskArgs& args )
    {
        TaskManager* pTaskManager = editor::findSubsystem<TaskManager>();
        if ( pTaskManager == nullptr )
        {
            delegate( args );
            return;
        }

        TaskHandle handle = pTaskManager->emplaceTask( name, delegate, args );
        handle.submit();
    }
} // namespace sw::editor
