#include "pch.h"

#include "Editor/Common/Task/EditorBackgroundTask.h"

#include "Core/Task/TaskManager.h"

#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorStateStore.h"

#include "Engine/Automation/Scenario/AutomationStepRegistry.h"

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
            pTaskManager->addToStage( pStore->get<EditorBackgroundJob>()._stage, handle, "EditorBackgroundJobs" );
        handle.submit();
    }

    void requestEditorBackgroundStop()
    {
        if ( EditorStateStore* pStore = editor::findSubsystem<EditorStateStore>() )
            pStore->get<EditorBackgroundJob>()._stop.requestStop();
    }

    StopToken acquireEditorBackgroundStopSignal()
    {
        EditorStateStore* pStore = editor::findSubsystem<EditorStateStore>();
        return pStore != nullptr ? pStore->get<EditorBackgroundJob>()._stop.getToken() : StopToken{};
    }
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @brief 에디터가 낸 백그라운드 잡(파일 스캔 · 현지화 로드 · 참조 색인)이 아직 돌면 true 입니다.
     * @details 시나리오의 정지 판정(`EditorSettleWait`)과 `<Pass/>` 가 이 잡을 기다린다 — 패널이 "Loading..." 을 그리는 동안 찍은 캡처 · 본 값이 정지 상태가 아니었다(데이터 표 창).
     *          언리얼 Automation 이 캡처 앞에서 비동기 일(`FlushAsyncLoading` · 셰이더 컴파일)을 비우는 것과 같은 자리다. 잡은 한 곳(`submitEditorBackgroundJob`)에서 내므로 하나도 빠지지 않는다.
     */
    static bool isRunningEditorBackgroundJobs()
    {
        EditorStateStore* pStore       = editor::findSubsystem<EditorStateStore>(); // 선택: 편집기를 켜지 않은 실행에는 없다
        TaskManager*      pTaskManager = editor::findSubsystem<TaskManager>();
        return pStore != nullptr && pTaskManager != nullptr && pTaskManager->isStageComplete( pStore->get<EditorBackgroundJob>()._stage ) == false;
    }
} // namespace sw::editor

namespace sw
{
    SW_AUTOMATION_FINISH_BLOCKER( editorBackgroundJobs, "EditorBackgroundJobs", &editor::isRunningEditorBackgroundJobs );
} // namespace sw
