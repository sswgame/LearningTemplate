/**
 * @file AutomationRunner.h
 * @brief 자동화 시나리오 실행기를 정의합니다. 입력 단계는 가상 입력으로, 단언과 환경 단계는 씬 틱 뒤에 프레임마다 진행하고 결과를 종료 코드로 냅니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Automation/AutomationHangWatch.h"
#include "Engine/Automation/AutomationJudge.h"
#include "Engine/Automation/AutomationOutcome.h"
#include "Engine/Automation/AutomationReportWriter.h"
#include "Engine/Automation/AutomationRunTitle.h"
#include "Engine/Automation/AutomationStepTimeline.h"
#include "Engine/Input/Virtual/VirtualInputScript.h"
#include "Engine/Resource/ResourceID.h"
#include "Engine/Scenario/AutomationScenario.h"
#include "Engine/Scenario/AutomationStepContext.h"
#include "Engine/Scenario/AutomationTimeline.h"

namespace sw
{
    enum class AutomationStepHold : uint8;

    class GameObjectManager;
    class InputManager;

    /**
     * @class AutomationRunner
     * @brief 시나리오 한 편을 프레임마다 진행합니다. `Engine` 가 `-scenario` 를 받으면 하나를 가지고 프레임 앞뒤에서 부릅니다.
     * @details 프레임 0 은 시작 조건이 처음 참인 프레임이다. 그때 단계를 검사하고(레지스트리와 탐침은 모듈이 로드된 뒤라 그때 채워져 있다) 입력 단계를
     *          가상 입력 원천(`VirtualInputScript`)으로 옮겨 `InputManager` 에 붙이므로, 입력 단계의 프레임 번호는 가상 입력 프레임 번호와 같다.
     *          단언과 환경 단계는 그 프레임의 씬 틱 뒤(`onFrameEnd`)에 돈다. 단언 실패는 적어 두고 계속 가며, 끝에서 `Failed` 가 된다.
     *          끝나면 가상 입력을 떼고 `[Scenario]` 요약 줄과 (요청됐으면) JSON 보고를 쓴다. 게임 스레드에서만 쓴다.
     *          판정(비교 · 실패 · 로그 게이트)은 `AutomationJudge`, 진행 상태(프레임 · 단계 순번 · 대기 · 시한)는 `AutomationStepTimeline`, 보고 쓰기는 `AutomationReportWriter` 가 값 멤버로 맡는다 —
     *          실행기는 시작 · 프레임 · 끝맺음을 잇는 얇은 몸통이다.
     */
    class SW_API AutomationRunner final : public AutomationStepContext
    {
    public:
        AutomationRunner();
        ~AutomationRunner() override;

        AutomationRunner( const AutomationRunner& )            = delete;
        AutomationRunner& operator=( const AutomationRunner& ) = delete;

        /**
         * @brief 재현 명령(보고의 `reproduce`)의 재료를 정합니다 — 세션이 게임 · 백엔드 · 에디터 · 시나리오 목록을 채워 시작 전에 넘깁니다.
         * @details 시나리오 인자(`_scenarioArgument`)가 비어 있으면(세션 없이 돌리는 테스트) 보고의 `reproduce` 는 빈 글입니다. 이 시나리오의 `<Launch>` 선언 변수는 보고를 쓸 때 실행기가 더한다.
         */
        void setReproduceInput( AutomationReproduceInput input ) { _reproduceInput = std::move( input ); }
        /**
         * @brief 행 감시 스레드를 켭니다(시작 전에 부릅니다). 프레임이 멈춰 실행기가 끝맺지 못해도 상한 + 유예 뒤에 감시가 보고를 쓰고 종료 코드 12 로 프로세스를 끝냅니다.
         * @details 프로세스를 끝내므로 실제 `App` 실행을 구동하는 세션만 켠다. 손으로 실행기를 돌리는 테스트는 켜지 않는다.
         */
        void armHangWatchdog() { _bHangWatchArmed = SW_TRUE; }
        /**
         * @brief 지금 RHI 백엔드의 이름(`DirectX12` · `Vulkan` …, 창 있는 호스트의 `gv_rhiBackend`)입니다. 엔진 서비스가 없거나 창이 없으면(디바이스를 만들지 않은 호스트) 빈 글입니다.
         * @details 렌더러(`FrameRenderer`)는 이 층보다 위라 읽지 않는다 — 백엔드 교체가 `gv_rhiBackend` 를 갱신하므로 같은 값이다.
         */
        [[nodiscard]] static string findBackendName();

        /** @brief 시나리오 파일(리소스 경로 또는 절대 경로)을 읽습니다. 읽지 못하면 결과가 LoadError 이고 다음 `onFrameEnd` 에서 끝납니다. */
        void startFromPath( const ResourceID& resourceID, string_view reportPath );
        /**
         * @brief XML 글로 시작합니다(테스트). 형식 오류면 false 이고 결과는 LoadError 입니다.
         * @details 이 구성에 없는 단계(Shipping 의 `DevCommand`)가 들었으면 false 이고 결과는 Skipped 입니다(`startFromPath` 도 같다).
         */
        [[nodiscard]] bool startFromText( string_view xmlText, string_view reportPath = {} );

        /** @brief 프레임 시작(입력 `beginFrame` 전)에 부릅니다. 시작 조건을 보고, 시작했으면 가상 입력을 붙이고 입력 전 단계(`_bBeforeInput`)를 돌립니다. */
        void onFrameBegin( InputManager& input );
        /** @brief 프레임 끝(씬 틱과 렌더 제출 뒤)에 부릅니다. 이 프레임의 나머지 단계, 시간 초과, 끝을 확인합니다. 끝났으면 결과를, 아니면 Running 을 반환합니다. */
        AutomationOutcome onFrameEnd( InputManager& input );
        /**
         * @brief 창이 닫혀 루프가 끝났습니다. `ExpectExitWithin` 을 기다리던 중이면 그 시한 안인지로, 아니면 "끝나기 전에 닫혔다" 로 끝냅니다.
         * @return 최종 결과(이미 끝났으면 그 결과)
         */
        AutomationOutcome onWindowClosed( InputManager* pInput );

        /**
         * @brief `<Screenshot>` 이 요청한 경로를 하나 꺼냅니다(Engine 가 다음 렌더 패킷에 싣고, 그 패킷이 그린 이미지가 찍힙니다). 없으면 false 입니다.
         */
        [[nodiscard]] bool takePendingScreenshotPath( string& outPath ) { return _run.takePendingScreenshotPath( outPath ); }
        /** @brief 렌더 스레드가 지금까지 쓴 시나리오 스크린샷 수입니다(Engine 가 `onFrameEnd` 전에 넘긴다). `<ExpectImage>` 는 이것이 오를 때까지 기다린다. */
        void setCompletedScreenshotCount( uint32 completedCount ) { _run._screenshotCompletedCount = completedCount; }
        /** @brief 지표 값 줄들입니다(`darkFraction(0,0,1,0.12) park.ppm = 0.034`). 보고에 적습니다. */
        const vector<string>& getMetricLines() const { return _judge.getMetricLines(); }

        // --- 단계 문맥(`AutomationStepContext`): 단계 함수가 쓰는 좁은 면입니다 ---

        /** @brief 단계 처리기가 실패를 적습니다(시나리오는 계속 진행하고 끝에서 Failed 가 됩니다). */
        void recordFailure( const AutomationStep& step, string_view message ) override;
        /**
         * @brief 단계 차례 함수(`AutomationStepHoldFunction`)가 기다리는 대상이 아직이면(@p bReady 가 false) 시나리오 시계를 한 프레임 붙잡습니다.
         * @details `WaitUntil` 과 같다 — 붙잡은 프레임은 세지 않고 뒤의 입력 이벤트를 한 프레임 민다. 준비되면 붙잡은 프레임 수를 로그로 남긴다.
         * @return 붙잡았으면 Hold, 준비됐거나 @p timeoutFrames 를 다 기다렸으면 Run 입니다(그 단계가 돌며 실패를 적는다).
         */
        AutomationStepHold holdUntilReady( const AutomationStep& step, InputManager& input, bool bReady, uint32 timeoutFrames ) override;
        /** @brief 단계가 시나리오를 끝냅니다. `Pass` · `Fail` · `Skip` 을 종료 코드(`Passed` · `Failed` · `Skipped`)로 번역해 `finish` 를 부릅니다. */
        void endScenario( AutomationStepEnd end, string_view reason ) override;
        /** @brief 이 시나리오의 산출물 폴더(`Saved/Automation/<이름>/`)입니다. */
        const string& getOutputDirectory() const override { return _outputDirectory; }
        /** @brief 상대 경로면 산출물 폴더 아래로 둡니다(단계 처리기가 파일을 쓸 때). */
        string resolveOutputPath( string_view file ) const override;
        /** @brief 지금 시나리오 프레임(시작 전이면 0)입니다. */
        uint32 getFrameIndex() const override { return _run._frameIndex; }
        /** @brief 초를 시나리오 프레임 수로 바꿉니다(고정 프레임 시간으로 반올림). 단계의 `seconds` · `timeoutSeconds` 속성이 씁니다. */
        uint32 convertSecondsToFrames( float32 seconds ) const override;
        /** @brief 활성 씬의 오브젝트 매니저입니다(없으면 nullptr). `setObjectManager` 로 정했으면 그것입니다. */
        GameObjectManager* findActiveObjectManager() const override;
        /** @brief 입력 단계가 이벤트를 넣는 가상 입력 원천입니다. 다른 모듈의 입력 단계(액션 단계)도 준비 함수에서 씁니다. */
        VirtualInputScript& getInputScript() override { return _inputScript; }
        /** @brief 시나리오를 돌리는 입력 관리자입니다(시작 프레임부터, 그 전에는 nullptr). 액션 단계가 입력 맵의 바인딩을 읽습니다. */
        InputManager* getInputManager() const override { return _pInputManager; }

        /** @brief 시나리오를 바로 끝냅니다(`Pass`, `Fail`, `Skipped` 등). 실패가 적혀 있으면 Passed 는 Failed 가 됩니다. 이미 끝났으면 무시합니다. */
        void finish( AutomationOutcome result, string_view reason );

        bool isActive() const { return _result == AutomationOutcome::Running; }
        /** @brief 마지막으로 시작한 실행 단계의 이름입니다(아직 없으면 빈 글). 창 제목과 단계 로그가 씁니다. */
        const string& getCurrentStepKind() const { return _run._currentStepKind; }
        /** @brief 그 단계가 파일에서 몇 번째인지(0 부터)입니다. */
        uint32 getCurrentStepNumber() const { return _run._currentStepNumber; }
        /**
         * @brief 끝맺음(요약 줄, 보고, 가상 입력 떼기)까지 마쳤는지 묻습니다.
         * @details `isActive` 가 false 여도 아직 끝맺지 않았을 수 있다. 시작 프레임(`onFrameBegin`)에서 읽기 오류나 시작 시한으로 끝나면
         *          그 프레임의 `onFrameEnd` 가 끝맺고 결과를 돌려준다. 루프는 이것으로 `onFrameEnd` 를 부를지 정한다.
         */
        bool                      hasEnded() const { return _bEnded == SW_TRUE; }
        AutomationOutcome         getResult() const { return _result; }
        const AutomationScenario& getScenario() const { return _scenario; }
        const vector<string>&     getFailures() const { return _judge.getFailures(); }
        /** @brief 맞았지만 어림값 문턱(`provisional`)이라 미확정인 단언 줄입니다. 비어 있지 않고 실패가 없으면 결과는 `Unconfirmed` 입니다. */
        const vector<string>& getUnconfirmed() const { return _judge.getUnconfirmed(); }
        /** @brief 로그 게이트가 처리하지 않았다고 본 줄입니다. 비어 있지 않으면 실패가 없어도 결과는 `UnhandledLog` 입니다. */
        const vector<string>& getUnhandledLogs() const { return _judge.getUnhandledLogs(); }
        const string&         getFinishReason() const { return _finishReason; }
        /** @brief 활성 씬 대신 @p pManager 를 봅니다(테스트와 도구용이며, 씬 없이 매니저를 손으로 돌릴 때 씁니다). nullptr 이면 활성 씬입니다. */
        void setObjectManager( GameObjectManager* pManager ) { _pObjectManagerOverride = pManager; }

        /** @brief 탐침 값 한 줄을 보고의 `records` 에 남깁니다(`<Record>`). 백엔드끼리 같아야 하는 값이다(AppScenarioTest 가 비교한다). */
        void                  addRecordLine( string line ) { _judge.addRecordLine( std::move( line ) ); }
        const vector<string>& getRecordLines() const { return _judge.getRecordLines(); }

    private:
        /** @brief 엔진 단계(`AutomationRunnerSteps.cpp`, 레지스트리에 등록)는 가상 입력 원천 · 판정 · 진행 상태(스크린샷 대기열 · 종료 시한)를 직접 만진다. */
        friend struct AutomationRunnerEngineSteps;

        /** @brief 이 구성에 없는 단계(Shipping 의 `DevCommand`)가 들었으면 건너뜀(13)으로 끝내고 true 입니다. */
        bool skipIfStepsAreMissingFromThisBuild();
        /** @brief 시나리오가 선언한 사본 폴더(`<Scratch>`)를 새로 만듭니다. 하나라도 실패하면 읽기 오류(LoadError)로 끝내고 false 입니다 — 원본에 쓰며 돌지 않는다. */
        [[nodiscard]] bool prepareScratchFolders();
        /** @brief 시작 조건이 참이 된 프레임에서 단계마다 등록의 검사 · 준비(입력 단계는 가상 입력으로 옮긴다)를 돌리고 로그 수집을 시작합니다. 실패면 false 를 반환하고 이유를 남깁니다. */
        [[nodiscard]] bool prepare( string& outError );
        /**
         * @brief `WaitUntil` 이 아직 시나리오 시계를 붙잡아야 하면 true 입니다(이 프레임은 세지 않고 뒤의 입력 이벤트를 한 프레임 밉니다).
         * @details 조건이 참이거나 `timeoutFrames` 를 넘으면(실패로 적는다) false 이고, 그 단계는 끝난다.
         */
        [[nodiscard]] bool shouldHoldForCondition( const AutomationStep& step, InputManager& input );
        /**
         * @brief 시계 잡기 조건(`SW_AUTOMATION_CLOCK_HOLDER`, 저장 검증 같은 백그라운드 작업)이 참이면 이 프레임을 세지 않고 true 입니다 — 시작 전이면 시작을 미룹니다.
         * @details 그 프레임은 단계를 돌리지 않고 뒤의 입력 이벤트를 한 프레임 민다(`WaitUntil` 과 같다). 벽시계로 `kClockHoldTimeoutSeconds` 를 넘기면 시간 초과로 끝낸다.
         */
        [[nodiscard]] bool holdClockForBackgroundWork( InputManager& input );
        /** @brief 끝 정리를 합니다. 가상 입력과 로그 수집을 떼고 요약 줄과 보고를 씁니다. 한 번만 돕니다. */
        void endRun( InputManager* pInput );
        /** @brief 부하 시나리오(`<Load>`)면 시작 프레임에 측정 창(`gv_profileSeconds`)을 열고 멈춤 그물 시각을 잡습니다. 아니면 아무것도 하지 않습니다. */
        void startLoadWindow();
        /** @brief 벽시계 감시를 시작합니다(시나리오를 읽기 시작할 때 한 번). 시작 시각(단조 시계 · 유닉스 · 지역 달력 글)을 잡습니다. */
        void markWallStart();
        /**
         * @brief 앞 프레임 끝부터 지금까지 걸린 벽시계를 프레임 하나로 `_timeline` 에 더합니다(`onFrameEnd` 가 프레임마다 한 번).
         * @param stepIndexBefore 이 프레임의 `onFrameEnd` 가 시작할 때의 `_nextStepIndex` — 달라졌으면 단계가 돈 프레임이다
         * @details 종류: 시작 전 · 시계 잡기 조건이 붙잡음 · 조건 대기가 붙잡음 · 단계가 돎 · 돌 단계 없이 `<At>` 시점만 기다림(고정 프레임 대기).
         */
        void recordTimelineFrame( size_t stepIndexBefore );
        /**
         * @brief 시작부터 `wallTimeoutSeconds` 가 지났으면 시간 초과(12)로 끝냅니다. 부하 시나리오는 자기 그물이 있어 보지 않습니다.
         * @details 프레임 시한(`timeoutFrames`)은 붙잡은 프레임(`holdUntilReady` · 시계 잡기)을 세지 않아, 붙잡힌 채로는 영영 끝나지 않고 테스트의 프로세스 시한(보고 없는 kill)까지 간다.
         *          이 감시는 프레임이 도는 동안만 본다 — 프레임 자체가 멈춘 행은 행 감시 스레드(`AutomationHangWatchdog`)가 상한 + 유예 뒤에 잡는다.
         */
        void finishIfWallClockExceeded();
        /** @brief 지금 자리 한 줄(`시나리오 · 백엔드 · step N 단계 · f프레임` — 창 제목과 같은 글)입니다. */
        string describeRunPosition() const;
        /** @brief 크래시 보고에 싣는 자리(`Scenario` 키 — 시나리오 · 백엔드 · 단계 · 프레임)를 갱신합니다. 크래시 · 행 뒤 덤프가 어느 시나리오의 어느 단계였는지 알려 준다. */
        void updateCrashContext( string_view note = {} );
        /** @brief 이 실행의 재현 명령입니다 — 세션이 넘긴 재료에 이 시나리오의 `<Launch>` 변수를 더해 짓습니다. 재료에 시나리오가 없으면 빈 글. */
        string makeReproduceCommand() const;
        /**
         * @brief 행 감시 스레드를 띄웁니다. 감시가 켜져 있고(`armHangWatchdog`) 시나리오를 읽었으며 부하 시나리오가 아닐 때만 띄웁니다.
         * @details 부하 시나리오는 측정 창 그물이 따로 있어 제외한다. 스레드는 `endRun` 이 내린다.
         */
        void startHangWatchdog();
        /** @brief 쓰기 입구 게이트를 쥡니다(이미 쥐었으면 아무것도 하지 않습니다). 시나리오가 `scratch/` 밖의 리소스 원본에 쓰면 거절된다. */
        void acquireWriteGate();
        /** @brief 쥔 쓰기 입구 게이트를 놓습니다. 쥐지 않았으면 아무것도 하지 않습니다. */
        void releaseWriteGate();
        /** @brief 감시 스레드가 읽을 스냅샷을 갱신합니다(@p position 은 `describeRunPosition`). 감시가 돌고 있지 않으면 아무것도 하지 않습니다. */
        void publishHangSnapshot( string_view position );

        AutomationScenario       _scenario;
        VirtualInputScript       _inputScript;
        AutomationJudge          _judge;        ///< 판정 상태 · 비교(실패 · 미확정 · 로그 게이트 · 탐침 이력). 시간 · 입력은 모른다
        AutomationStepTimeline   _run;          ///< 진행 상태(프레임 · 단계 순번 · 대기 · 시한 · 스크린샷 대기열). 판정은 읽지 않는다
        AutomationReportWriter   _reportWriter; ///< 보고 경로 · JSON 보고 쓰기
        AutomationTimeline       _wallProfile;  ///< 벽시계가 프레임 종류(고정 대기 · 조건 대기 · 단계 …)별로 어디에 쓰였는지(보고의 `timeline`)
        string                   _outputDirectory;
        string                   _finishReason;
        string                   _startedAt;              ///< 벽시계 시작 지역 시각 글(`2026-10-26 14:03:11`). 얻지 못하면 빈 글
        AutomationReproduceInput _reproduceInput;         ///< 보고의 재현 명령 재료(`setReproduceInput`). 이 시나리오의 `<Launch>` 변수는 보고를 쓸 때 더한다
        AutomationHangPayload    _hangPayload;            ///< 감시 스레드에 넘기는 스냅샷의 게임 스레드 쪽 사본(바뀌지 않는 칸은 시작할 때 한 번 채운다)
        AutomationHangWatchdog   _hangWatchdog;           ///< 프레임이 멈춘 행을 잡는 감시 스레드(`armHangWatchdog` 로 켠 실행만)
        GameObjectManager*       _pObjectManagerOverride; ///< `setObjectManager` 로 정한 매니저입니다(nullptr 이면 활성 씬).
        InputManager*            _pInputManager;          ///< 시작 프레임에 받은 입력 관리자(`onFrameBegin`)
        int64                    _startedUnixMs;          ///< 시작 시각의 유닉스 밀리초(보고용)
        AutomationOutcome        _result;
        uint8                    _bStarted        : 1;
        uint8                    _bEnded          : 1;
        uint8                    _bHangWatchArmed : 1; ///< `armHangWatchdog` 를 불렀다(실제 프로세스를 끝낼 수 있는 감시라 세션만 켠다)
        uint8                    _bWriteGateHeld  : 1; ///< 쓰기 입구 게이트를 쥐고 있다(`AutomationScratch::acquireWriteGate` — `endRun` 이나 소멸자가 놓는다)
        [[maybe_unused]] uint8   _reserved        : 4;
    };
} // namespace sw
