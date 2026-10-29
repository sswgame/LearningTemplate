#include "pch.h"

#include "Editor/Common/Task/EditorBackgroundTask.h"

#include "Core/Task/TaskManager.h"

#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorStateStore.h"

namespace sw::editor
{
    void submitEditorBackgroundJob( string_view name, const TaskArgsDelegate& delegate, const TaskArgs& args )
    {
        // 선택 조회(②): 작업 시스템 없이 도는 도구 · 테스트는 호출 스레드에서 바로 실행한다(결과는 같고 비동기만 빠진다).
        TaskManager* pTaskManager = editor::findSubsystem<TaskManager>();
        if ( pTaskManager == nullptr )
        {
            delegate( args );
            return;
        }

        TaskHandle handle = pTaskManager->emplaceTask( name, delegate, args );
        // 스테이지는 에디터 컨텍스트가 만든 상태 저장소에 산다(게임 스레드에서만 낸다). 컨텍스트가 없으면(테스트) 종료에서 기다릴 주인도 없으므로 넣지 않는다.
        if ( EditorStateStore* pStore = editor::findSubsystem<EditorStateStore>() )
            pTaskManager->addToStage( pStore->get<EditorBackgroundWork>()._stage, handle, "EditorBackgroundJobs" );
        handle.submit();
    }

    void requestEditorBackgroundStop()
    {
        if ( EditorStateStore* pStore = editor::findSubsystem<EditorStateStore>() )
            pStore->get<EditorBackgroundWork>()._stop.requestStop();
    }

    StopToken acquireEditorBackgroundStopSignal()
    {
        EditorStateStore* pStore = editor::findSubsystem<EditorStateStore>();
        return pStore != nullptr ? pStore->get<EditorBackgroundWork>()._stop.getToken() : StopToken{};
    }
} // namespace sw::editor
