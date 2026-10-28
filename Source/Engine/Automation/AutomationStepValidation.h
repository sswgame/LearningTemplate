/**
 * @file AutomationStepValidation.h
 * @brief 시나리오 단계를 실행기 없이 검사할 때, 검사 함수(`AutomationStepRegistration::_pValidate`)를 찾고 그것이 답할 수 있는 단계인지 가립니다.
 * @details 검사 함수는 순수 함수(단계 → 참 · 거짓과 이유)라 실행기 없이 부를 수 있다. 다만 두 가지가 이 프로세스에 모듈이 올라와 있어야 답이 나온다.
 *          (1) 단계 종류 자체 — 에디터 · App 의 단계는 그 모듈이 올라와야 등록된다. 그래서 검사 함수만 ImGui · 렌더에 기대지 않는 파일에 두고
 *              `SW_AUTOMATION_STEP_VALIDATOR` 로 따로 등록하면(진짜 등록은 같은 함수를 부른다) 모듈 없이도 검사할 수 있다.
 *          (2) 탐침 이름 · 전역 변수 이름 · 개발 명령 이름 — 등록표를 묻는다. 그런 단계를 부르면 "틀렸다" 와 "이 프로세스가 모른다" 가 같은 거짓으로 섞이므로,
 *              부르기 전에 `findUnresolvedDependency` 로 먼저 가른다. 모르는 단계는 통과로 세지 않고 "못 본 단계" 로 따로 센다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

#include "Engine/Automation/AutomationStepRegistry.h"

namespace sw
{
    struct AutomationStep;

    class AutomationScenario;

    /** @brief 단계의 검사가 기대는 이름 가운데, 이 프로세스에 등록돼 있지 않아 답을 낼 수 없는 것의 종류입니다. */
    enum class AutomationStepDependency : uint8
    {
        Resolved,       ///< 모두 등록돼 있습니다. 검사 함수를 불러도 됩니다.
        StepKind,       ///< 단계 종류도, 그 종류의 검사 함수도 이 프로세스에 등록돼 있지 않습니다(그 단계를 등록하는 모듈이 올라오지 않았다).
        Probe,          ///< `probe` 속성의 탐침이 등록돼 있지 않습니다.
        GlobalVariable, ///< `Variable` · `ExpectVariable` 의 `name` 이 등록된 전역 변수가 아닙니다(엔진 서비스가 묶이지 않은 경우 포함).
        DevCommand,     ///< `DevCommand` 의 첫 낱말이 등록된 개발 명령이 아닙니다(Shipping 처럼 개발 명령이 없는 구성 포함).
    };

    /** @brief `findValidator` 의 답입니다. */
    enum class AutomationValidatorLookup : uint8
    {
        Unknown,     ///< 이 단계 종류의 등록도 검사 함수 등록도 없습니다.
        NoValidator, ///< 단계 종류는 있으나 검사 함수가 없습니다(속성 검사를 하지 않는 단계).
        Found,       ///< 검사 함수를 찾았습니다.
    };

} // namespace sw

namespace sw
{
    /** @struct AutomationStepValidation @brief 단계를 실행 없이 검사하기 전의 확인 모음입니다. 검사 함수만 올리는 곳은 `AutomationStepRegistry::registerValidator` 입니다. */
    struct SW_API AutomationStepValidation
    {
        /**
         * @brief @p kind 의 검사 함수를 찾습니다. 올라온 단계 등록(`AutomationStepRegistry`)이 먼저이고, 없으면 검사만 올린 등록입니다.
         * @param outValidate `Found` 일 때만 채웁니다.
         */
        [[nodiscard]] static AutomationValidatorLookup findValidator( string_view kind, AutomationStepValidateFunction& outValidate );

        /**
         * @brief @p step 의 검사가 기대는 이름 가운데 이 프로세스에 등록되지 않은 첫 번째의 종류를 반환합니다. 모두 있으면 `Resolved` 입니다.
         * @param outName 등록되지 않은 이름(단계 종류 · 탐침 · 변수 · 개발 명령)입니다. 모두 있으면 단계 종류를 적습니다.
         * @details 이름을 묻는 검사 함수는 `validateProbeReference` · `findRequiredVariable`(`AutomationRunner.cpp`)과 `validateDevCommand`(`AutomationEnvironmentSteps.cpp`)다.
         *          그 검사에 이름을 묻는 곳이 늘면 여기에도 더한다 — 빠뜨리면 실행기 없는 검사가 "틀렸다" 로 드러나 알려 준다(조용히 통과하지 않는다).
         */
        [[nodiscard]] static AutomationStepDependency findUnresolvedDependency( const AutomationStep& step, string& outName );

        /**
         * @brief 데스크톱 전역 상태를 쓰는 단계(`AutomationStepRegistration::_bUsesDesktop`)나 루트 `requireForeground="true"` 가 혼합 입력(`input="mixed"`)이 아닌 시나리오에 있으면 false 와 이유입니다.
         * @details 그런 단계(OS 커서 클립 · 화면 복사 `CaptureWindow` 등)와 전경 요구(OS 에 전경 창은 하나뿐이다)는 겹쳐 도는 다른 프로세스의 창과 서로 방해해서 `<타깃>_Desktop` 에서 혼자 도는 시나리오(혼합 입력)에서만 쓸 수 있다.
         *          표시를 시나리오 이름이 아니라 단계 등록에 두었으므로 새 데스크톱 단계는 등록 때 한 번 표시하면 모든 시나리오에 걸린다. 이 프로세스에 등록되지 않은 단계는 모르는 것이라 건너뛴다
         *          (그 판단은 `findUnresolvedDependency`).
         */
        [[nodiscard]] static bool validateDesktopSteps( const AutomationScenario& scenario, string& outError );

        /** @brief 못 본 까닭의 종류 이름입니다(보고에 적는다). */
        [[nodiscard]] static const utf8* getDependencyLabel( AutomationStepDependency dependency );
    };
} // namespace sw

namespace sw
{
    /** @brief `SW_AUTOMATION_STEP_VALIDATOR` 가 두는 정적 등록자입니다. 등록이 거절됐으면 내릴 때 빼지 않습니다(앞 등록을 지우지 않게). */
    struct SW_API AutomationStepValidatorRegistrar
    {
        explicit AutomationStepValidatorRegistrar( const AutomationStepValidatorRegistration* pRegistration );
        ~AutomationStepValidatorRegistrar();
        AutomationStepValidatorRegistrar( const AutomationStepValidatorRegistrar& )            = delete;
        AutomationStepValidatorRegistrar& operator=( const AutomationStepValidatorRegistrar& ) = delete;

        const AutomationStepValidatorRegistration* _pRegistration;
    };
} // namespace sw

/**
 * @brief 단계 종류의 검사 함수만 등록합니다(.cpp 의 파일 범위). 단계를 등록하는 모듈(에디터 · App)이 올라오지 않은 프로세스(nogpu 테스트)도 시나리오 데이터를 검사하게 한다.
 * @details 검사 함수는 단계 등록(`SW_AUTOMATION_STEP`)이 받는 함수와 같아야 한다. 검사 함수를 ImGui · 렌더에 기대지 않는 파일에 두면 이 줄도 거기 둔다.
 */
#define SW_AUTOMATION_STEP_VALIDATOR( id, pKind, pValidate )                                                    \
    static const ::sw::AutomationStepValidatorRegistration sw_automationStepValidator_##id{ pKind, pValidate }; \
    static const ::sw::AutomationStepValidatorRegistrar    sw_automationStepValidatorRegistrar_##id             \
    {                                                                                                           \
        &sw_automationStepValidator_##id                                                                        \
    }
