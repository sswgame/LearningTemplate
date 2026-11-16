#include "pch.h"

#include "Editor/Common/Task/EditorBackgroundTask.h"

#include "Core/String/StringBuilder.h"
#include "Core/Task/TaskManager.h"

#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorStateStore.h"

#include "Engine/Automation/Scenario/AutomationStepRegistry.h"

namespace sw::editor
{
    namespace
    {
        struct EditorBackgroundTaskInternal
        {
            /** @brief 잡 하나를 내고 에디터 전체 스테이지(와 있으면 잡 객체의 스테이지)에 넣습니다. 작업 시스템이 없으면 이 스레드에서 바로 돈다. */
            static void submitJob( string_view name, const TaskArgsDelegate& delegate, const TaskArgs& args, TaskStageHandle* pJobStage )
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
                // 태스크는 스테이지 하나에만 완료를 알린다(`TaskStageHandle::addTask`) — 잡 객체의 스테이지에는 잡 뒤에 도는 빈 태스크를 넣는다.
                // 예전에는 같은 태스크를 두 스테이지에 넣어 뒤 등록이 앞을 덮었고, 전역 스테이지가 영영 끝나지 않아 에디터 정지 대기가 멈췄다.
                if ( pJobStage != nullptr )
                {
                    TaskHandle marker = pTaskManager->emplaceTask( name, TaskDelegate{} );
                    if ( marker.runAfter( handle ) )
                    {
                        pTaskManager->addToStage( *pJobStage, marker, name ); // 제출 전에 넣는다
                        marker.submit();
                    }
                }
                handle.submit();
            }
        };
    } // namespace

    void submitEditorBackgroundJob( string_view name, const TaskArgsDelegate& delegate, const TaskArgs& args )
    {
        EditorBackgroundTaskInternal::submitJob( name, delegate, args, nullptr );
    }

    void submitEditorBackgroundJob( string_view name, const TaskArgsDelegate& delegate, const TaskArgs& args, TaskStageHandle& inoutJobStage )
    {
        EditorBackgroundTaskInternal::submitJob( name, delegate, args, &inoutJobStage );
    }

    void waitEditorBackgroundJob( const TaskStageHandle& jobStage, string_view owner )
    {
        TaskManager* pTaskManager = editor::findSubsystem<TaskManager>(); // 선택: 없으면 제출이 같은 자리에서 돌았다(위) — 기다릴 것이 없다
        if ( pTaskManager != nullptr )
            pTaskManager->waitStage( jobStage, owner );
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
     * @details 시나리오의 정지 판정(`EditorStabilityWait`)과 `<Pass/>` 가 이 잡을 기다린다 — 패널이 "Loading..." 을 그리는 동안 찍은 캡처 · 본 값이 정지 상태가 아니었다(데이터 표 창).
     *          언리얼 Automation 이 캡처 앞에서 비동기 일(`FlushAsyncLoading` · 셰이더 컴파일)을 비우는 것과 같은 자리다. 잡은 한 곳(`submitEditorBackgroundJob`)에서 내므로 하나도 빠지지 않는다.
     */
    static bool isRunningEditorBackgroundJobs()
    {
        EditorStateStore* pStore       = editor::findSubsystem<EditorStateStore>(); // 선택: 편집기를 켜지 않은 실행에는 없다
        TaskManager*      pTaskManager = editor::findSubsystem<TaskManager>();
        return pStore != nullptr && pTaskManager != nullptr && pTaskManager->isStageComplete( pStore->get<EditorBackgroundJob>()._stage ) == false;
    }

    /** @brief 남은 잡 수와 그중 하나의 이름 — 정지 대기가 오래 걸릴 때 무엇이 끝나지 않는지 로그에 남긴다(`AutomationStepRegistry::describeActiveBlockers`). */
    static string describeEditorBackgroundJobs()
    {
        EditorStateStore* pStore       = editor::findSubsystem<EditorStateStore>();
        TaskManager*      pTaskManager = editor::findSubsystem<TaskManager>();
        if ( pStore == nullptr || pTaskManager == nullptr )
            return "no editor";
        const TaskStageHandle&                   stage = pStore->get<EditorBackgroundJob>()._stage;
        StringBuilder<constant::buffer::kMax256> resultBuilder;
        resultBuilder.appendFormat( "%# job(s) left, one is '%#'", pTaskManager->getStagePendingCount( stage ), pTaskManager->describeStageFirstUnfinished( stage ).c_str() );
        return string( resultBuilder.view() );
    }
} // namespace sw::editor

namespace sw
{
    SW_AUTOMATION_FINISH_BLOCKER_DESCRIBED( editorBackgroundJobs, "EditorBackgroundJobs", &editor::isRunningEditorBackgroundJobs, &editor::describeEditorBackgroundJobs );
} // namespace sw
