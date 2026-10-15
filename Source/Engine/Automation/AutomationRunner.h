/**
 * @file AutomationRunner.h
 * @brief 자동화 시나리오 실행기를 정의합니다. 입력 단계는 가상 입력으로, 단언과 환경 단계는 씬 틱 뒤에 프레임마다 진행하고 결과를 종료 코드로 냅니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Automation/AutomationLogCapture.h"
#include "Engine/Automation/AutomationProbe.h"
#include "Engine/Automation/AutomationReportWriter.h"
#include "Engine/Automation/AutomationScenario.h"
#include "Engine/Input/Virtual/VirtualInputScript.h"

namespace sw
{
    class GameObjectManager;
    class InputManager;

    /** @brief 시나리오가 끝난 까닭 = App 종료 코드입니다. */
    enum class AutomationResult : int32
    {
        Running   = -1,
        Passed    = 0,
        Failed    = 10,
        LoadError = 11, ///< 파일, 형식, 모르는 단계, 모르는 속성, 모르는 탐침이 있습니다. 조용히 버리지 않습니다.
        TimedOut  = 12,
        Skipped   = 13, ///< 이 기계에서 돌 수 없습니다(전경 창을 얻지 못한 경우 등). CTest 는 건너뜀으로 봅니다.
    };
} // namespace sw

namespace sw
{
    /**
     * @class AutomationRunner
     * @brief 시나리오 한 편을 프레임마다 진행합니다. `Engine` 가 `-scenario` 를 받으면 하나를 가지고 프레임 앞뒤에서 부릅니다.
     * @details 프레임 0 은 시작 조건이 처음 참인 프레임이다. 그때 단계를 검사하고(레지스트리와 탐침은 모듈이 로드된 뒤라 그때 채워져 있다) 입력 단계를
     *          가상 입력 원천(`VirtualInputScript`)으로 옮겨 `InputManager` 에 붙이므로, 입력 단계의 프레임 번호는 가상 입력 프레임 번호와 같다.
     *          단언과 환경 단계는 그 프레임의 씬 틱 뒤(`onFrameEnd`)에 돈다. 단언 실패는 적어 두고 계속 가며, 끝에서 `Failed` 가 된다.
     *          끝나면 가상 입력을 떼고 `[Scenario]` 요약 줄과 (요청됐으면) JSON 보고를 쓴다. 게임 스레드에서만 쓴다.
     *          로그 수집은 `AutomationLogCapture`, 보고 경로 · 지표 줄 · 보고 쓰기는 `AutomationReportWriter` 가 값 멤버로 맡는다.
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
        /**
         * @brief XML 글로 시작합니다(테스트). 형식 오류면 false 이고 결과는 LoadError 입니다.
         * @details 이 구성에 없는 단계(Shipping 의 `DevCommand`)가 들었으면 false 이고 결과는 Skipped 입니다(`startFromPath` 도 같다).
         */
        [[nodiscard]] bool startFromText( string_view xmlText, string_view reportPath = {} );

        /** @brief 프레임 시작(입력 `beginFrame` 전)에 부릅니다. 시작 조건을 보고, 시작했으면 가상 입력을 붙이고 입력 전 단계(`_bBeforeInput`)를 돌립니다. */
        void onFrameBegin( InputManager& input );
        /** @brief 프레임 끝(씬 틱과 렌더 제출 뒤)에 부릅니다. 이 프레임의 나머지 단계, 시간 초과, 끝을 확인합니다. 끝났으면 결과를, 아니면 Running 을 반환합니다. */
        AutomationResult onFrameEnd( InputManager& input );
        /**
         * @brief 창이 닫혀 루프가 끝났습니다. `ExpectExitWithin` 을 기다리던 중이면 그 시한 안인지로, 아니면 "끝나기 전에 닫혔다" 로 끝냅니다.
         * @return 최종 결과(이미 끝났으면 그 결과)
         */
        AutomationResult onWindowClosed( InputManager* pInput );

        /**
         * @brief `<Screenshot>` 이 요청한 경로를 하나 꺼냅니다(Engine 가 다음 렌더 패킷에 싣고, 그 패킷이 그린 이미지가 찍힙니다). 없으면 false 입니다.
         */
        [[nodiscard]] bool takePendingScreenshotPath( string& outPath );
        /** @brief 렌더 스레드가 지금까지 쓴 시나리오 스크린샷 수입니다(Engine 가 `onFrameEnd` 전에 넘긴다). `<ExpectImage>` 는 이것이 오를 때까지 기다린다. */
        void setCompletedScreenshotCount( uint32 completedCount ) { _screenshotCompletedCount = completedCount; }
        /** @brief 지표 값 줄들입니다(`darkFraction(0,0,1,0.12) park.ppm = 0.034`). 보고에 적습니다. */
        const vector<string>& getMetricLines() const { return _reportWriter.getMetricLines(); }

        /** @brief 단계 처리기가 실패를 적습니다(시나리오는 계속 진행하고 끝에서 Failed 가 됩니다). */
        void recordFailure( const AutomationStep& step, string_view message );
        /** @brief 시나리오를 바로 끝냅니다(`Pass`, `Fail`, `Skipped` 등). 실패가 적혀 있으면 Passed 는 Failed 가 됩니다. 이미 끝났으면 무시합니다. */
        void finish( AutomationResult result, string_view reason );

        bool isActive() const { return _result == AutomationResult::Running; }
        /**
         * @brief 끝맺음(요약 줄, 보고, 가상 입력 떼기)까지 마쳤는지 묻습니다.
         * @details `isActive` 가 false 여도 아직 끝맺지 않았을 수 있다. 시작 프레임(`onFrameBegin`)에서 읽기 오류나 시작 시한으로 끝나면
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
        /** @brief 상대 경로면 산출물 폴더 아래로 둡니다(단계 처리기가 파일을 쓸 때). */
        string resolveOutputPath( string_view file ) const;
        /** @brief 활성 씬의 오브젝트 매니저입니다(없으면 nullptr). `setObjectManager` 로 정했으면 그것입니다. */
        GameObjectManager* findActiveObjectManager() const;
        /** @brief 활성 씬 대신 @p pManager 를 봅니다(테스트와 도구용이며, 씬 없이 매니저를 손으로 돌릴 때 씁니다). nullptr 이면 활성 씬입니다. */
        void setObjectManager( GameObjectManager* pManager ) { _pObjectManagerOverride = pManager; }

        /** @brief 입력 단계가 이벤트를 넣는 가상 입력 원천입니다. 다른 모듈의 입력 단계(액션 단계)도 준비 함수에서 씁니다. */
        VirtualInputScript& getInputScript() { return _inputScript; }
        /** @brief 시나리오를 돌리는 입력 관리자입니다(시작 프레임부터, 그 전에는 nullptr). 액션 단계가 입력 맵의 바인딩을 읽습니다. */
        InputManager* getInputManager() const { return _pInputManager; }
        /** @brief 초를 시나리오 프레임 수로 바꿉니다(고정 프레임 시간으로 반올림). 단계의 `seconds` · `timeoutSeconds` 속성이 씁니다. */
        uint32 convertSecondsToFrames( float32 seconds ) const;
        /** @brief 탐침 값 한 줄을 보고의 `records` 에 남깁니다(`<Record>`). 백엔드끼리 같아야 하는 값이다(AppScenarioTest 가 견준다). */
        void                  addRecordLine( string line );
        const vector<string>& getRecordLines() const { return _reportWriter.getRecordLines(); }

        /** @brief 결과 이름(`PASS`, `FAIL`, `LOAD ERROR`, `TIMEOUT`, `SKIP`)입니다. */
        static const utf8* getResultName( AutomationResult result );

    private:
        /**
         * @struct ProbeWatch
         * @brief `<ExpectVisited>` 가 보는 탐침 하나(탐침 · 대상 · 키)의 값 이력입니다. 시작 프레임부터 프레임마다 읽어 값이 바뀔 때만 적습니다.
         */
        struct ProbeWatch
        {
            vector<string> _listValue{}; ///< 바뀐 값(끝의 0 을 정리한 글)
            vector<uint32> _listFrame{}; ///< 그 값이 처음 보인 시나리오 프레임
            string         _probe{};
            string         _target{};
            string         _key{};
        };

        /** @brief 단계의 탐침 · 대상 · 키를 읽어 값을 냅니다. 없으면 false 이고 @p outMessage 에 이유를 씁니다. */
        [[nodiscard]] bool readProbeValue( const AutomationStep& step, AutomationProbeValue& outValue, string& outMessage ) const;
        /** @brief 시작할 때 `<ExpectVisited>` 가 보는 탐침마다 이력을 만듭니다. */
        void prepareWatches();
        /** @brief 이력마다 이번 프레임 값을 읽어 바뀌었으면 적습니다(씬 틱 뒤, 단계 전). */
        void sampleWatches();
        void runExpectVisited( const AutomationStep& step );
        void runRecord( const AutomationStep& step );
        /** @brief 엔진 단계(`AutomationRunner.cpp`, 레지스트리에 등록)는 가상 입력 원천 · 스크린샷 대기열 · 종료 시한을 직접 만진다. */
        friend struct AutomationRunnerEngineSteps;

        /** @brief 이 구성에 없는 단계(Shipping 의 `DevCommand`)가 들었으면 건너뜀(13)으로 끝내고 true 입니다. */
        bool skipIfStepsAreMissingFromThisBuild();
        /** @brief 시작 조건이 참이 된 프레임에서 단계마다 등록의 검사 · 준비(입력 단계는 가상 입력으로 옮긴다)를 돌리고 로그 수집을 시작합니다. 실패면 false 를 반환하고 이유를 남깁니다. */
        [[nodiscard]] bool prepare( string& outError );
        void               runExpect( const AutomationStep& step );
        /**
         * @brief 탐침 단계(`Expect`, `WaitUntil`)의 비교를 봅니다. 탐침에 값이 없으면 false 이고 @p outMessage 에 이유를 씁니다.
         * @details 값이 있으면 @p outMessage 는 `<탐침> <규칙>, got <값>` 이다.
         */
        [[nodiscard]] bool compareProbe( const AutomationStep& step, bool& outMatched, string& outMessage ) const;
        /**
         * @brief `WaitUntil` 이 아직 시나리오 시계를 붙잡아야 하면 true 입니다(이 프레임은 세지 않고 뒤의 입력 이벤트를 한 프레임 밉니다).
         * @details 조건이 참이거나 `timeoutFrames` 를 넘으면(실패로 적는다) false 이고, 그 단계는 끝난다.
         */
        [[nodiscard]] bool shouldHoldForCondition( const AutomationStep& step, InputManager& input );
        void               runExpectLog( const AutomationStep& step );
        void               runExpectImage( const AutomationStep& step );
        /** @brief 끝 정리를 합니다. 가상 입력과 로그 수집을 떼고 요약 줄과 보고를 씁니다. 한 번만 돕니다. */
        void endRun( InputManager* pInput );
        /** @brief 부하 시나리오(`<Load>`)면 시작 프레임에 측정 창(`gv_profileSeconds`)을 열고 멈춤 그물 시각을 잡습니다. 아니면 아무것도 하지 않습니다. */
        void startLoadWindow();

        AutomationScenario     _scenario;
        VirtualInputScript     _inputScript;
        AutomationLogCapture   _logCapture;            ///< 시나리오 동안의 로그 줄(`<ExpectLog>`)
        AutomationReportWriter _reportWriter;          ///< 보고 경로 · 지표 줄 · JSON 보고 쓰기
        vector<string>         _listFailure;           ///< "frame 40 <Expect> #7: Shooter3D.WeaponIndex == 1, got 2"
        vector<string>         _listPendingScreenshot; ///< 아직 렌더 패킷에 싣지 않은 스크린샷 경로입니다.
        vector<ProbeWatch>     _listWatch;             ///< `<ExpectVisited>` 가 보는 탐침 이력
        string                 _outputDirectory;
        string                 _finishReason;
        GameObjectManager*     _pObjectManagerOverride; ///< `setObjectManager` 로 정한 매니저입니다(nullptr 이면 활성 씬).
        InputManager*          _pInputManager;          ///< 시작 프레임에 받은 입력 관리자(`onFrameBegin`)
        float64                _exitDeadlineSeconds;    ///< `ExpectExitWithin` 이 정한 벽시계 시한입니다(단조 시계 초). 0 이면 기다리지 않습니다.
        float64                _loadDeadlineSeconds;    ///< 부하 시나리오(`<Load>`)의 멈춤 그물 — 측정 창이 이때까지 끝내지 못하면 시간 초과다(단조 시계 초). 0 이면 부하 시나리오가 아니다.
        size_t                 _nextStepIndex;
        uint32                 _frameIndex;
        uint32                 _waitFrameCount; ///< 시작 조건을 기다린 프레임 수입니다.
        uint32                 _screenshotRequestedCount;
        uint32                 _screenshotCompletedCount;
        uint32                 _imageWaitFrameCount; ///< `<ExpectImage>` 가 스크린샷을 기다린 프레임 수입니다.
        uint32                 _holdFrameCount;      ///< 지금 `<WaitUntil>` 이 시나리오 시계를 붙잡은 프레임(그동안 `_frameIndex` 는 그대로다)
        AutomationResult       _result;
        uint8                  _bStarted : 1;
        uint8                  _bEnded   : 1;
        [[maybe_unused]] uint8 _reserved : 6;
    };
} // namespace sw
