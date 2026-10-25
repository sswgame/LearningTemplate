/**
 * @file AutomationStepValidation.h
 * @brief 시나리오 단계를 실행기 없이 검사할 때, 검사 함수(`AutomationStepRegistration::_pValidate`)가 답할 수 있는 단계인지 가립니다.
 * @details 검사 함수는 순수 함수(단계 → 참 · 거짓과 이유)라 실행기 없이 부를 수 있다. 다만 일부 검사는 이 프로세스에 모듈이 올라와 있어야 답이 나온다
 *          (탐침 이름 · 전역 변수 이름 · 개발 명령 이름은 등록표를 묻는다). 그런 단계를 부르면 "틀렸다" 와 "이 프로세스가 모른다" 가 같은 거짓으로 섞이므로,
 *          부르기 전에 이 함수로 먼저 가른다. 모르는 단계는 통과로 세지 않고 "못 본 단계" 로 따로 센다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

namespace sw
{
    struct AutomationStep;

    /** @brief 단계의 검사가 기대는 이름 가운데, 이 프로세스에 등록돼 있지 않아 답을 낼 수 없는 것의 종류입니다. */
    enum class AutomationStepDependency : uint8
    {
        Resolved,       ///< 모두 등록돼 있습니다. 검사 함수를 불러도 됩니다.
        StepKind,       ///< 단계 종류가 이 프로세스에 등록돼 있지 않습니다(그 단계를 등록하는 모듈이 올라오지 않았다).
        Probe,          ///< `probe` 속성의 탐침이 등록돼 있지 않습니다.
        GlobalVariable, ///< `Variable` · `ExpectVariable` 의 `name` 이 등록된 전역 변수가 아닙니다(엔진 서비스가 묶이지 않은 경우 포함).
        DevCommand,     ///< `DevCommand` 의 첫 낱말이 등록된 개발 명령이 아닙니다(Shipping 처럼 개발 명령이 없는 구성 포함).
    };

    /** @struct AutomationStepValidation @brief 단계를 실행 없이 검사하기 전의 확인 모음입니다. */
    struct SW_API AutomationStepValidation
    {
        /**
         * @brief @p step 의 검사 함수가 기대는 이름 가운데 이 프로세스에 등록되지 않은 첫 번째의 종류를 반환합니다. 모두 있으면 `Resolved` 입니다.
         * @param outName 등록되지 않은 이름(단계 종류 · 탐침 · 변수 · 개발 명령)입니다. 모두 있으면 단계 종류를 적습니다.
         * @details 이름을 묻는 검사 함수는 `validateProbeReference` · `findRequiredVariable`(`AutomationRunner.cpp`)과 `validateDevCommand`(`AutomationEnvironmentSteps.cpp`)다.
         *          그 검사에 이름을 묻는 곳이 늘면 여기에도 더한다 — 빠뜨리면 실행기 없는 검사가 "틀렸다" 로 드러나 알려 준다(조용히 통과하지 않는다).
         */
        [[nodiscard]] static AutomationStepDependency findUnresolvedDependency( const AutomationStep& step, string& outName );

        /** @brief 못 본 까닭의 종류 이름입니다(보고에 적는다). */
        [[nodiscard]] static const utf8* getDependencyLabel( AutomationStepDependency dependency );
    };
} // namespace sw
