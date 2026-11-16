/**
 * @file AutomationSession.h
 * @brief `-scenario=a,b,c` 한 번의 실행을 소유합니다. 시나리오 순서(`AutomationScenarioChain`)와 지금 단계의 실행기(`AutomationRunner`)를 들고, 단계가 끝나면 다음 단계를 시작하고, 끝나면 종료 코드를 냅니다.
 */
#pragma once
#include "Core/Container/pair.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Subsystem/ISubsystem.h"

#include "DevTools/Client/Automation/Runner/AutomationRunTitle.h"
#include "DevTools/Client/Automation/Scenario/ScenarioRecorder.h"
#include "DevTools/DevToolsAPI.h"

#include "Engine/Automation/AutomationResult.h"
#include "Engine/Automation/AutomationRuntime.h"
#include "Engine/EngineFrameHooks.h"

namespace sw
{
    class AutomationLogPolicy;
    class AutomationRunner;
    class AutomationScenarioChain;
    class IEngineRenderDriver;
    class ScenarioPlayer;

    /**
     * @class AutomationSession
     * @brief 엔진 루프에 프레임 훅(`EngineFrameHooks`, `makeFrameHooks`)으로 붙는 시나리오 구동기입니다. 루프는 관찰자 목록만 알고, 세션 · 실행기 · 체인 · 단계 전환은 모릅니다.
     * @details 개발 도구 이미지의 모듈 서브시스템으로 엔진 범위에 스스로 서고 `IAutomationRuntime` 을 공개합니다(`SW_MODULE_SUBSYSTEM( Engine, … )`) — 이 폴더를 짓지 않는
     *          구성은 등록이 없고, 호스트는 실행기를 모른다. 런처가 엔진이 선 뒤에 `IAutomationRuntime::start` 로 시작하면 `Engine::getFrameHooks().add( makeFrameHooks() )` 로 붙습니다.
     *          엔진 범위의 자식이라 기동 단계보다 먼저 내려간다 — 실행기가 빌려 쓰는 엔진 서비스(로그 리스너 · 입력)가 사라지기 전에 `shutdownSubsystem` 이 떼고 비웁니다. 한 단계가 끝나면(통과해야 다음으로 간다 — `AutomationScenarioChain`) 같은 프로세스에서 다음 시나리오의 실행기를 새로 만든다. 그 단계가 시작할 때
     *          - 프로세스의 **작업 폴더**가 처음과 같은지 보고(파일 대화상자 같은 것이 옮긴다) 다르면 경고하고 되돌린다,
     *          - 결정적 실행 정책의 프레임 고정 시간(`DeterministicRunPolicy::_fixedFrameDelta`)을 그 시나리오의 `fixedDelta` 로 맞추고,
     *          - 렌더 스레드가 지금까지 쓴 시나리오 스크린샷 수를 기준선으로 잡는다(실행기는 자기 단계가 요청한 것만 기다린다 — 렌더 스레드의 수는 프로세스 전체로 누적된다),
     *          - 시작할 때 모은 재현 명령 재료(게임 · 백엔드 · 에디터 · `-scenario` 목록)를 단계 실행기에 넘긴다(보고의 `reproduce`).
     *          **재생**(`ScenarioPlayback::play` — 데모 · 어트랙트 · 시퀀서)은 세션이 하지 않는다. 별도 객체 `ScenarioPlayer` 가 자기 실행기 · 프레임 훅으로 돌리고,
     *          세션의 바깥 창구(`Runtime::start`)는 기본 재생기 하나를 만들어 호스트 범위에 `IScenarioPlayer` 로 걸기만 한다(언리얼 Automation 세션과 Level Sequence Player 의 나눔).
     *          게임 스레드에서만 쓴다.
     */
    class SW_DEVTOOLS_API AutomationSession : public ISubsystem
    {
    public:
        /** @brief 서브시스템 이름입니다(같은 범위의 다른 서브시스템이 의존 칸에 적는다). */
        static constexpr const utf8*               kSubsystemName    = "Automation";
        static constexpr SubsystemModuleVisibility kModuleVisibility = SubsystemModuleVisibility::HostModules; ///< 에디터 콘솔이 시나리오가 선언한 로그를 묻는다(`getLogPolicy`)

        AutomationSession();
        ~AutomationSession() override;
        AutomationSession( const AutomationSession& )            = delete;
        AutomationSession& operator=( const AutomationSession& ) = delete;

        SubsystemInitResult initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        /** @brief 지금 단계의 실행기와 체인을 비웁니다(엔진 서비스를 빌려 쓰는 것이 엔진보다 먼저 사라지게). 시작하지 않았으면 아무것도 하지 않습니다. */
        void        shutdownSubsystem() override;
        const utf8* getSubsystemName() const override { return kSubsystemName; }
        /** @brief 모듈 서브시스템 등록이 `IAutomationRuntime` 으로 공개하는 부분입니다. */
        IAutomationRuntime& getPublishedPart() { return _runtime; }

        /**
         * @brief 시나리오 목록(`a,b,c` 또는 하나)으로 시작하고 첫 단계를 엽니다.
         * @param scenarioArgument `-scenario=` 값(쉼표로 가른다)
         * @param reportPath `-scenario-report=` 값(비면 보고 파일을 쓰지 않는다. 단계가 여럿이면 단계마다 번호가 붙는다)
         * @param bKeepGoing `-scenario-keep-going` — 실패한 단계 뒤에도 이어 돌린다
         * @param completedScreenshotCount 지금까지 렌더 스레드가 쓴 시나리오 스크린샷 수
         * @return 돌릴 시나리오가 하나라도 있으면 true 입니다(없으면 아무것도 하지 않고 체인도 남기지 않는다).
         */
        [[nodiscard]] bool begin( string_view scenarioArgument, string_view reportPath, bool bKeepGoing );
        /** @brief 지금 시나리오가 그린 프레임을 화면에 내보낼지 정합니다 — 무인 실행은 시나리오가 `present="true"` 일 때만 내보낸다. 렌더 스레드가 없으면(헤드리스) 할 일이 없다. */
        void applyPresentPolicy();
        /**
         * @brief 플레이를 시나리오로 녹화합니다(`-scenario-record=<Saved/Automation/...scenario.xml>`). 활성 씬이 플레이를 시작한 첫 프레임부터 입력을 적고,
         *        시나리오가 끝나거나 창이 닫힐 때 파일을 씁니다.
         * @param watchList `-scenario-record-watches=` 값 — 쉼표로 가른 검사점(`감시 값`, `감시 값@대상`, `감시 값@대상@키`). 끝 프레임의 값이 `<Expect>` 로 실려 녹화가 스스로를 검증한다.
         * @details 시나리오와 함께 돌리면(`-scenario`) 시나리오가 낸 가상 입력도 적는다 — 시나리오 → 녹화 → 재생의 왕복으로 결정적 재생을 확인한다. 시작 조건은 시나리오의 `ScenePlaying` 과 같은 프레임이다.
         * @return 경로가 `Saved/Automation/` 아래의 `.scenario.xml` 이 아니면 false 입니다.
         */
        [[nodiscard]] bool beginRecording( string_view outputPath, string_view watchList );
        /**
         * @brief 지금 단계 시나리오의 로그 선언 · 허용(`<ExpectLog>` · `<AllowLog>`)입니다. 세션 수명이고 안에서 잠가 아무 스레드에서나 읽는다 — 에디터 콘솔이 받는 순간
         *        `classify` 로 묻고 선언한 줄을 흐리게 그린다. 단계의 판정(`AutomationEvaluator`)은 같은 객체를 빌려 게이트를 판정한다. 받는 쪽은 이 세션보다 오래 들어도 된다.
         */
        shared_ptr<const AutomationLogPolicy> getLogPolicy() const { return _pLogPolicy; }
        /** @brief 시나리오나 녹화가 돌고 있으면 true 입니다(무인 실행 페이싱 · 재생기의 겹침 거절이 본다). */
        bool isBusy() const { return _pRunner != nullptr || _recordPath.empty() == false; }

        /** @brief 이 세션의 네 훅을 묶습니다(`EngineFrameHookList::add` 에 넘긴다). */
        EngineFrameHooks makeFrameHooks();
        /** @brief 프레임 시작(입력 `beginFrame` 전)입니다. 지금 단계의 시작 조건 · 가상 입력 붙이기 · 입력 전 단계를 돌립니다. 입력이 없으면 아무것도 하지 않습니다. */
        void onFrameBegin( const EngineFrameContext& context );
        /**
         * @brief 프레임 끝(씬 틱과 렌더 제출 뒤, 입력 프레임을 닫기 전)입니다. 문맥의 스크린샷 수(프로세스 전체)에서 단계 기준선을 빼 실행기에 넘깁니다.
         * @return 체인이 끝났으면 프로세스 종료 코드, 아니면 `kEngineFrameRunning` 입니다(입력이 없어도 `kEngineFrameRunning`).
         */
        [[nodiscard]] EngineFrameExit onFrameEnd( const EngineFrameContext& context );
        /** @brief 씬 틱 뒤 — `<Screenshot>` 이 요청한 경로를 이번 프레임의 패킷에 맡깁니다(`ScreenshotRequestQueue::putForNextPacket`, 앞 것이 남았으면 다음 프레임). */
        void onAfterSceneTick( const EngineFrameContext& context );
        /**
         * @brief 창이 닫혀 루프가 끝났습니다. 지금 단계를 그 결과로 끝맺고, 돌릴 단계가 남았으면 실패로 봅니다(`CloseWindow` 는 마지막 단계에만 둔다).
         * @return 프로세스 종료 코드, 시작한 단계가 없으면 `kEngineFrameRunning` 입니다.
         */
        [[nodiscard]] EngineFrameExit onWindowClosed( const EngineFrameContext& context );

    private:
        /**
         * @class Runtime
         * @brief 바깥 창구(`IAutomationRuntime`)입니다. 세션이 그 인터페이스를 상속하지 않고 이 멤버를 공개한다(`getPublishedPart`). 구현은 `AutomationSession.cpp`.
         */
        class Runtime final : public IAutomationRuntime
        {
        public:
            explicit Runtime( AutomationSession& owner );
            ~Runtime() override; // 재생기(`ScenarioPlayer`)는 앞 선언뿐이다 — 만들기 · 소멸은 .cpp 에서(예외 정리 길도 완전한 형식을 요구한다)

            [[nodiscard]] bool         start( Engine& engine, SubsystemScope& host, const CommandLineManager& commandLine ) override;
            bool                       isBusy() const override;
            void                       runPerBackend( string_view backends, CommandLineManager& commandLine, HeadlessTaskState& inoutState ) override;
            const AutomationRunStatus* findRunStatus() const override;
            /** @brief 재생기를 호스트 범위에서 풀어 내리고 세션을 엔진 프레임에서 뗍니다(세션 종료가 부른다). 시작하지 않았으면 아무것도 하지 않습니다. */
            void detach();

        private:
            AutomationSession&         _owner;
            SubsystemScope*            _pHost{ nullptr };    ///< 재생기를 건 호스트 범위(`start` 전에는 nullptr)
            Subscription               _frameSubscription{}; ///< 세션을 엔진 프레임 관찰자로 붙인 구독
            unique_ptr<ScenarioPlayer> _pPlayer;             ///< 기본 재생기(`ScenarioPlayback` 이 호스트 범위에서 찾는다) — 세션과 따로 프레임 훅에 붙는다
        };
        /**
         * @brief 체인의 지금 단계 시나리오를 새 실행기로 시작합니다.
         * @details 둘째 단계부터는 앞 단계의 상태를 이어받지 않는다(언리얼 PIE 가 플레이마다 새 GameInstance 를 만들고 CVar 를 되돌리는 것과 같다) — 전역 변수를 세션 시작의
         *          값으로 되돌리고(`restoreStartVariables`), `<Launch>` 값을 건 뒤 게임을 처음부터 다시 세운다(`IGameRestarter`). 새 게임이 기동 때처럼 첫 씬(`gv_firstScene`)을
         *          열거나 벤치를 세우므로 단계는 첫 단계와 같은 길로 선다 — 그래서 CTest 는 시나리오 모두를 한 체인으로 돌린다(`Test/Scenario/CMakeLists.txt`).
         */
        void startStage( uint32 completedScreenshotCount );
        /** @brief 세션 시작 때 찍어 둔 전역 변수 값과 지금 값이 다른 것을 되돌립니다(`_listStartVariable`). */
        void restoreStartVariables();
        /** @brief 주 창을 세션 시작 때의 클라이언트 크기로 되돌립니다(`ResizeWindow` 와 같은 길). 창이 없거나 크기가 같으면 아무것도 하지 않는다. */
        void restoreStartWindowSize();
        /** @brief 찍어 둔 값 하나를 @p value 로 바꿉니다 — 첫 단계가 명령줄로 받은 `<Launch>` 값은 다음 단계로 넘기지 않고 기본값에서 시작한다. */
        void removeStartValue( string_view name, string_view value );
        /** @brief 단계 하나가 끝났을 때 부릅니다(`AutomationResult`). 통과했고 다음 단계가 있으면 그것을 시작하고 `kEngineFrameRunning`, 아니면 체인의 종료 코드(`getAutomationExitCode`)입니다. */
        [[nodiscard]] EngineFrameExit onStageEnded( AutomationResult result, uint32 completedScreenshotCount );
        /** @brief 녹화를 시작할 프레임인지 보고(활성 씬이 플레이 중이고 시작 차단이 없다) 시작합니다. */
        void startRecordingWhenReady();
        /** @brief 녹화를 멈추고 검사점을 읽어 파일을 씁니다. 녹화 중이 아니면 아무것도 하지 않습니다. */
        void finishRecording();
        /** @brief 지금 단계 실행기의 진행 · 판정 상태를 `_runStatus` 에 옮깁니다(실행기가 없으면 아무것도 하지 않는다). 창 제목과 바깥 읽기 창구가 같은 값을 읽는다. */
        void updateRunStatus();
        /** @brief 창 제목에 실행 정보를 적습니다(바뀔 때와 일정 프레임마다만 시스템 호출). 시나리오가 없어도 벤치 인자만 있으면 적는다. */
        void updateRunTitle();
        /**
         * @brief 체인이 끝난 종료 코드를 바로 내지 않고 보이는 실행이면 `gv_scenarioResultHoldFrames` 프레임 뒤에 냅니다 — 지켜보는 사람이 끝 결과(HUD · 창 제목)를 읽을 틈이다.
         * @details 판정 · 보고는 이미 끝났다 — 남기는 프레임은 결과를 바꾸지 않는다. 무인 숨김 실행 · 창 없는 호스트는 바로 낸다.
         */
        [[nodiscard]] EngineFrameExit holdFinalExit( EngineFrameExit exitCode );
        /** @brief 처리한 패킷 사진 수입니다(우편함이 없는 호스트는 0). */
        static uint32 findCompletedScreenshotCount();

    private:
        shared_ptr<AutomationLogPolicy>     _pLogPolicy; ///< 지금 단계의 로그 선언 · 허용(단계 실행기 · 재생기 · 에디터 콘솔이 나눠 든다)
        unique_ptr<AutomationRunner>        _pRunner;
        unique_ptr<AutomationScenarioChain> _pChain;
        ScenarioRecorder                    _recorder;
        vector<string>                      _listRecordWatch;          ///< `-scenario-record-watches` 의 검사점
        string                              _recordPath;               ///< `-scenario-record` 경로(비면 녹화하지 않는다)
        string                              _reportPath;               ///< `-scenario-report` 원본(단계마다 `AutomationScenarioChain::makeStageReportPath`)
        string                              _workingFolder;            ///< 시작할 때의 작업 폴더(단계마다 이 폴더에서 시작한다)
        uint32                              _startWindowWidth;         ///< 시작할 때의 주 창 클라이언트 너비(둘째 단계부터 이 크기에서 시작한다, 창이 없으면 0)
        uint32                              _startWindowHeight;        ///< 시작할 때의 주 창 클라이언트 높이
        vector<pair<string, string>>        _listStartVariable;        ///< 세션 시작 때의 전역 변수(이름, 값 글) — 둘째 단계부터 이 값에서 시작한다
        AutomationReproduceInput            _reproduceInput;           ///< 재현 명령 재료(게임 · 시작 때 백엔드 · 에디터 · 시나리오 목록) — 단계마다 실행기에 넘긴다
        uint32                              _screenshotBaseline;       ///< 단계가 시작할 때 렌더 스레드가 이미 쓴 스크린샷 수
        uint8                               _bUnattended : 1;          ///< `-unattended` — 보는 사람이 없다. 시나리오가 `present="true"` 가 아니면 내보내지 않는다(`applyPresentPolicy`)
        IEngineRenderDriver*                _pRenderDriver{ nullptr }; ///< 사진을 실은 패킷을 단계 앞에서 비운다(`onFrameEnd`). `Runtime::start` 가 건다, 없으면(헤드리스) 비울 것이 없다
        Runtime                             _runtime;                  ///< 바깥 창구(`getPublishedPart`)
        AutomationRunTitleTracker           _runTitle;                 ///< 창 제목의 실행 정보(시나리오 · 백엔드 · 단계 · 프레임)
        AutomationRunStatus                 _runStatus;                ///< 지금 단계의 진행 · 판정 상태(`updateRunStatus`, 바깥 창구 `findRunStatus`)
        EngineFrameExit                     _heldExit;                 ///< 끝 결과를 남기는 동안 쥔 종료 코드(`holdFinalExit`, 없으면 빈 값)
        uint32                              _heldExitFrames;           ///< 종료 코드를 내기까지 남은 프레임
    };
} // namespace sw
