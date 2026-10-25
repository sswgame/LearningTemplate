#include "pch.h"

#include "Editor/Common/EditorBackgroundTask.h"

#include "Core/Task/TaskHandleTracker.h"
#include "Core/Task/TaskManager.h"

#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorStateStore.h"

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
        // 추적기는 에디터 컨텍스트가 만든 상태 저장소에 산다. 컨텍스트가 없으면(테스트) 종료에서 기다릴 주인도 없으므로 올리지 않는다.
        if ( EditorStateStore* pStore = editor::findSubsystem<EditorStateStore>() )
            pStore->get<TaskHandleTracker>().track( handle );
        handle.submit();
    }
} // namespace sw::editor
