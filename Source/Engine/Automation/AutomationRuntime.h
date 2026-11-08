/**
 * @file AutomationRuntime.h
 * @brief 자동화 실행기(개발 도구 `DevTools/Client/Automation`)가 스스로 공개하는 창구 `IAutomationRuntime` 입니다.
 * @details 실행기는 엔진 밖 개발 도구에 있다 — 언리얼 Automation 컨트롤러 · Gauntlet 이 Developer 모듈이고 엔진은 테스트 등록 틀만 드는 것과 같은 나눔이다.
 *          실행기 세션이 개발 도구 이미지의 모듈 서브시스템으로 엔진 범위에 서며 이 인터페이스를 공개한다(`SW_MODULE_SUBSYSTEM`). 호스트(App)와 헤드리스 작업은
 *          `engine::findSubsystem<IAutomationRuntime>()` 으로 찾기만 한다 — 실행기를 짓지 않은 구성(Shipping — 개발 도구가 없다 · 전용 서버 — `DevTools/Client` 를 짓지 않는다)은 등록이 없어 nullptr 이고,
 *          그 실행이 `-scenario` 를 받았으면 찾는 쪽이 기동 오류로 멈춘다. 빈 구현 파일을 두지 않는다(`docs/agents/BuildConfigSwitch.md`).
 */
#pragma once
#include "Core/Container/string.h"

#include "Engine/Automation/AutomationResult.h"

namespace sw
{
    /**
     * @struct AutomationRunStatus
     * @brief 지금 도는 시나리오의 진행 · 판정 상태입니다(세션이 프레임 끝마다 채운다). 창 제목과 바깥 읽기 창구(`IAutomationRuntime::findRunStatus`)가 같은 값을 읽는다.
     * @details 실행기는 누가 읽는지 모른다 — 개발 도구의 디버그 HUD 가 이것을 읽어 "Scenario" 섹션을 그린다(의존 방향 DevTools → Engine).
     *          언리얼 Automation 의 세션 프런트엔드 · Gauntlet 이 테스트 상태를 화면 · 로그로 내는 것과 같다 — 다만 실행기 쪽은 상태만 내고 그리기는 모른다.
     */
    struct AutomationRunStatus
    {
        string           _scenarioName;                                     ///< 돌고 있는 시나리오 이름
        string           _stepKind;                                         ///< 마지막으로 시작한 단계 이름(아직 없으면 빈 글)
        string           _firstFailure;                                     ///< 적힌 첫 실패 줄(없으면 빈 글)
        string           _previousStageName;                                ///< 체인에서 바로 앞에 끝난 단계 이름(첫 단계면 빈 글)
        uint32           _stepNumber{ 0 };                                  ///< 그 단계가 파일에서 몇 번째인지(0 부터)
        uint32           _frame{ 0 };                                       ///< 시나리오 프레임
        uint32           _stageIndex{ 0 };                                  ///< 체인의 몇 번째 단계인지(0 부터)
        uint32           _stageCount{ 0 };                                  ///< 체인의 단계 수(하나면 1)
        uint32           _failureCount{ 0 };                                ///< 적힌 실패 수
        uint32           _warningErrorCount{ 0 };                           ///< 진행 중이면 모은 Warning · Error 줄 수(허용 판정 전), 끝났으면 로그 게이트가 처리하지 않은 줄 수
        AutomationResult _result{ AutomationResult::Running };              ///< 이 단계의 결과(도는 중이면 Running)
        AutomationResult _previousStageResult{ AutomationResult::Running }; ///< 바로 앞 단계의 결과(첫 단계면 Running)
    };
} // namespace sw

namespace sw
{
    class CommandLineManager;
    class Engine;
    class SubsystemScope;
    struct HeadlessTaskState;

    /**
     * @class IAutomationRuntime
     * @brief 자동화 실행기의 바깥 창구입니다 — 시나리오 시작, 무인 실행 여부, 백엔드마다 실행 작업.
     */
    class IAutomationRuntime
    {
    public:
        virtual ~IAutomationRuntime() = default;

        /**
         * @brief `-scenario` · `-scenario-record` 를 읽어 세션을 시작해 엔진 프레임 관찰자로 붙이고, 기본 재생기(`ScenarioPlayer` — 세션과 따로 선 객체)를 만들어 @p host 에 `IScenarioPlayer` 로 겁니다.
         * @details 엔진 기동이 끝난 뒤, 모듈을 올리기 전에 부른다(첫 단계의 시작 조건이 모듈 · 씬이 설 때까지 기다린다). 떼기는 세션이 엔진과 함께 내려갈 때 스스로 한다.
         *          게임 · 콘솔의 재생(`ScenarioPlayback::play`)은 기동 뒤 아무 때나 시작하므로 시나리오가 없어도 붙인다.
         * @return 기동을 이어 가도 되면 true 입니다.
         */
        [[nodiscard]] virtual bool start( Engine& engine, SubsystemScope& host, const CommandLineManager& commandLine ) = 0;
        /** @brief 무인 시나리오를 돌리는 중인가입니다(재생은 사람이 보는 화면이라 세지 않는다). 프레임 페이싱이 읽는다. */
        virtual bool isBusy() const = 0;
        /**
         * @brief `-scenario-backends=<목록>` 작업 — 시나리오를 백엔드마다 자식 App 으로 동시에 돌리고 모읍니다(`AutomationPerBackend`). 결과는 @p inoutState 에 적습니다.
         * @param backends `-scenario-backends` 값(비어 있지 않다)
         */
        virtual void runPerBackend( string_view backends, CommandLineManager& commandLine, HeadlessTaskState& inoutState ) = 0;
        /**
         * @brief 지금 시나리오의 진행 · 판정 상태입니다. 시나리오가 없으면 nullptr 입니다(끝난 뒤 결과를 남기는 프레임 동안은 끝난 상태를 준다).
         * @details 포인터는 이 프레임 안에서만 쓴다(세션이 프레임 끝마다 다시 쓴다). 게임 스레드에서만 읽는다.
         */
        virtual const AutomationRunStatus* findRunStatus() const = 0;
    };
} // namespace sw
