/**
 * @file AutomationRuntime.h
 * @brief 자동화 실행기(`Automation/Runner`)가 스스로 공개하는 창구 `IAutomationRuntime` 입니다.
 * @details 실행기 세션이 엔진 이미지의 모듈 서브시스템으로 엔진 범위에 서며 이 인터페이스를 공개한다(`SW_MODULE_SUBSYSTEM`). 호스트(App)와 헤드리스 작업은
 *          `engine::findSubsystem<IAutomationRuntime>()` 으로 찾기만 한다 — 실행기를 짓지 않은 구성(`SW_ENABLE_AUTOMATION=OFF`)은 등록이 없어 nullptr 이고,
 *          그 실행이 `-scenario` 를 받았으면 찾는 쪽이 기동 오류로 멈춘다. 빈 구현 파일을 두지 않는다(`docs/agents/BuildConfigSwitch.md`).
 */
#pragma once
#include "Core/Container/string.h"

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
         * @brief `-scenario` · `-scenario-record` 를 읽어 세션을 시작하고, 재생기를 엔진 프레임 관찰자로 붙여 @p host 에 `IScenarioPlayer` 로 겁니다.
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
    };
} // namespace sw
