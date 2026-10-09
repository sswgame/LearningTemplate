/**
 * @file AutomationRunner.h
 * @brief 자동화 시나리오 실행기 — 입력 단계는 가상 입력으로, 단언 · 환경 단계는 씬 틱 뒤에 프레임마다 진행하고 결과를 종료 코드로 냅니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"

#include "Engine/Automation/AutomationScenario.h"
#include "Engine/Input/Virtual/VirtualInputScript.h"

namespace sw
{
    struct LogEntry;

    class GameObjectManager;
    class InputManager;

    /** @brief 시나리오가 끝난 까닭 = App 종료 코드입니다. */
    enum class AutomationResult : int32
    {
        Running   = -1,
        Passed    = 0,
        Failed    = 10,
        LoadError = 11, ///< 파일 · 형식 · 모르는 단계 · 모르는 속성 · 모르는 탐침 — 조용히 버리지 않는다
        TimedOut  = 12,
        Skipped   = 13, ///< 이 기계에서 돌 수 없다(전경 창을 못 얻음 등) — CTest 는 건너뜀으로 본다
    };
} // namespace sw

namespace sw
{
    /** @struct AutomationLogLine @brief 시나리오 동안 남은 로그 한 줄(`ExpectLog` 가 센다)입니다. */
    struct AutomationLogLine
    {
        string _message{};
        uint32 _frameIndex{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @class AutomationRunner
     * @brief 시나리오 한 편을 프레임마다 진행합니다. `EngineLoop` 가 `-scenario` 를 받으면 하나 들고 프레임 앞뒤에서 부릅니다.
     * @details 프레임 0 = 시작 조건이 처음 참인 프레임입니다. 그때 단계를 검사하고(등록표 · 탐침은 모듈이 올라온 뒤라 그때 차 있다) 입력 단계를
     *          가상 입력 원천(`VirtualInputScript`)으로 옮겨 `InputManager` 에 붙이므로, 입력 단계의 프레임 번호 = 가상 입력 프레임 번호입니다.
     *          단언 · 환경 단계는 그 프레임의 씬 틱 뒤(`onFrameEnd`)에 돕니다. 단언 실패는 적어 두고 계속 가며, 끝에서 `Failed` 가 됩니다.
     *          끝나면 가상 입력을 떼고 `[Scenario]` 요약 줄과 (요청됐으면) JSON 보고를 씁니다. 게임 스레드에서만 씁니다.
     */
    class SW_API AutomationRunner
    {
    public:
        AutomationRunner();
        ~AutomationRunner();

        AutomationRunner( const AutomationRunner& )            = delete;
        AutomationRunner& operator=( const AutomationRunner& ) = delete;

        /** @brief 시나리오 파일(리소스 경로 또는 절대 경로)을 읽습니다. 읽지 못하면 결과가 LoadError 이고 다음 `onFrameEnd` 에서 끝납니다. */
        void startFromPath( string_view scenarioPath, string_view reportPath );
        /** @brief XML 글로 시작합니다(시험). 형식 오류면 false 이고 결과는 LoadError 입니다. */
        [[nodiscard]] bool startFromText( string_view xmlText, string_view reportPath = {} );

        /** @brief 프레임 시작(입력 `beginFrame` **전**) — 시작 조건을 보고, 시작했으면 가상 입력을 붙이고 입력 전 단계(`_bBeforeInput`)를 돌린다. */
        void onFrameBegin( InputManager& input );
        /** @brief 프레임 끝(씬 틱 · 렌더 제출 뒤) — 이 프레임의 나머지 단계 · 시간 초과 · 끝을 본다. 끝났으면 결과(아니면 Running)입니다. */
        AutomationResult onFrameEnd( InputManager& input );
        /**
         * @brief 창이 닫혀 루프가 끝났습니다. `ExpectExitWithin` 을 기다리던 중이면 그 시한 안인지로, 아니면 "끝나기 전에 닫혔다" 로 끝냅니다.
         * @return 최종 결과(이미 끝났으면 그 결과)
         */
        AutomationResult onWindowClosed( InputManager* pInput );

        /**
         * @brief `<Screenshot>` 이 요청한 경로를 하나 꺼냅니다(EngineLoop 가 다음 렌더 패킷에 싣는다 — 그 패킷이 그린 그림이 찍힌다). 없으면 false 입니다.
         */
        [[nodiscard]] bool takePendingScreenshotPath( string& outPath );
        /** @brief 렌더 스레드가 지금까지 쓴 시나리오 스크린샷 수입니다(EngineLoop 가 `onFrameEnd` 전에 넘긴다). `<ExpectImage>` 는 이것이 오를 때까지 기다린다. */
        void setCompletedScreenshotCount( uint32 completedCount ) { _screenshotCompletedCount = completedCount; }
        /** @brief 지표 값 줄들(`darkFraction(0,0,1,0.12) park.ppm = 0.034`) — 보고에 적는다. */
        const vector<string>& getMetricLines() const { return _listMetricLine; }

        /** @brief 단계 처리기가 실패를 적습니다(시나리오는 계속 — 끝에서 Failed). */
        void recordFailure( const AutomationStep& step, string_view message );
        /** @brief 시나리오를 바로 끝냅니다(`Pass` · `Fail` · `Skipped` …). 실패가 적혀 있으면 Passed 는 Failed 가 됩니다. 이미 끝났으면 무시합니다. */
        void finish( AutomationResult result, string_view reason );

        bool isActive() const { return _result == AutomationResult::Running; }
        /**
         * @brief 끝맺음(요약 줄 · 보고 · 가상 입력 떼기)까지 마쳤는지 묻습니다.
         * @details `isActive` 가 false 여도 아직 끝맺지 않았을 수 있다 — 시작 프레임(`onFrameBegin`)에서 읽기 오류 · 시작 시한으로 끝나면
         *          그 프레임의 `onFrameEnd` 가 끝맺고 결과를 돌려준다. 루프는 이것으로 `onFrameEnd` 를 부를지 정한다.
         */
        bool             hasEnded() const { return _bEnded == SW_TRUE; }
        AutomationResult getResult() const { return _result; }
        /** @brief 지금 시나리오 프레임(시작 전이면 0)입니다. */
        uint32                    getFrameIndex() const { return _frameIndex; }
        const AutomationScenario& getScenario() const { return _scenario; }
        const vector<string>&     getFailures() const { return _listFailure; }
        const string&             getFinishReason() const { return _finishReason; }
        /** @brief 이 시나리오의 산출물 폴더(`Saved/Automation/<이름>/`)입니다. */
        const string& getOutputDirectory() const { return _outputDirectory; }
        /** @brief 활성 씬의 오브젝트 매니저입니다(없으면 nullptr). `setObjectManager` 로 정했으면 그것입니다. */
        GameObjectManager* findActiveObjectManager() const;
        /** @brief 활성 씬 대신 @p pManager 를 봅니다(시험 · 도구 — 씬 없이 매니저를 손으로 돌릴 때). nullptr 이면 활성 씬입니다. */
        void setObjectManager( GameObjectManager* pManager ) { _pObjectManagerOverride = pManager; }

        /** @brief 결과 이름(`PASS` · `FAIL` · `LOAD ERROR` · `TIMEOUT` · `SKIP`)입니다. */
        static const utf8* getResultName( AutomationResult result );

    private:
        /** @brief 시작 조건이 참이 된 프레임 — 단계를 검사하고 입력 단계를 옮기고 로그를 듣기 시작한다. 실패면 false 와 이유. */
        [[nodiscard]] bool prepare( string& outError );
        [[nodiscard]] bool validateEngineStep( const AutomationStep& step, string& outError ) const;
        [[nodiscard]] bool compileInputStep( const AutomationStep& step, string& outError );
        /** @brief 엔진 단계를 돌린다. 돌릴 수 없으면(형식) false 입니다. */
        [[nodiscard]] bool runEngineStep( const AutomationStep& step );
        void               runExpect( const AutomationStep& step );
        void               runExpectLog( const AutomationStep& step );
        void               runExpectImage( const AutomationStep& step );
        /** @brief 상대 경로면 산출물 폴더 아래로 둡니다. */
        string resolveOutputPath( string_view file ) const;
        void   onLogWritten( const LogEntry& entry );
        /** @brief 끝 정리 — 가상 입력 · 로그 듣기를 떼고 요약 줄 · 보고를 쓴다. 한 번만 돈다. */
        void endRun( InputManager* pInput );
        void writeReport() const;

        AutomationScenario        _scenario;
        VirtualInputScript        _inputScript;
        vector<string>            _listFailure;           ///< "frame 40 <Expect> #7: Shooter3D.WeaponIndex == 1, got 2"
        vector<AutomationLogLine> _listLogLine;           ///< 시나리오 동안 남은 로그(상한 `kMaxLogLine`)
        vector<string>            _listPendingScreenshot; ///< 아직 렌더 패킷에 싣지 않은 스크린샷 경로
        vector<string>            _listMetricLine;        ///< 보고에 적는 지표 값
        string                    _reportPath;
        string                    _outputDirectory;
        string                    _finishReason;
        mutex                     _logMutex; ///< `_listLogLine` — 로그는 아무 스레드에서나 온다
        DelegateHandle            _logListenerHandle;
        GameObjectManager*        _pObjectManagerOverride; ///< `setObjectManager` — nullptr 이면 활성 씬
        float64                   _exitDeadlineSeconds;    ///< `ExpectExitWithin` 이 정한 벽시계 시한(단조 시계 초). 0 이면 기다리지 않는다
        size_t                    _nextStepIndex;
        atomic<uint32>            _logFrameIndex; ///< 로그 줄에 적을 프레임(로그 스레드가 읽는다)
        uint32                    _frameIndex;
        uint32                    _waitFrameCount; ///< 시작 조건을 기다린 프레임
        uint32                    _screenshotRequestedCount;
        uint32                    _screenshotCompletedCount;
        uint32                    _imageWaitFrameCount; ///< `<ExpectImage>` 가 스크린샷을 기다린 프레임
        AutomationResult          _result;
        uint8                     _bStarted      : 1;
        uint8                     _bEnded        : 1;
        uint8                     _bListeningLog : 1;
        [[maybe_unused]] uint8    _reserved      : 5;
    };
} // namespace sw
