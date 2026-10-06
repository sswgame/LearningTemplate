/**
 * @file AutomationStepRegistry.h
 * @brief 엔진 밖 단계 종류의 등록표 — GameFramework(행동 층 의도) · 에디터(ImGui) · 플랫폼(창 메시지)이 시나리오 단계를 더합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

namespace sw
{
    struct AutomationStep;

    class AutomationRunner;

    /** @brief 단계 하나를 실행합니다. 단언 실패는 `runner.recordFailure`, 실행할 수 없으면 false(읽기 오류로 끝난다). 게임 스레드입니다. */
    using AutomationStepFunction = bool ( * )( AutomationRunner& runner, const AutomationStep& step );
    /** @brief 시작 전에 단계의 속성을 검사합니다. 틀렸으면 false 와 이유입니다. */
    using AutomationStepValidateFunction = bool ( * )( const AutomationStep& step, string& outError );

    /** @struct AutomationStepRegistration @brief 등록 줄 하나 — 문자열 · 함수는 등록한 이미지의 정적 저장소에 있습니다. */
    struct AutomationStepRegistration
    {
        const utf8*                    _pKind; ///< 엘리먼트 이름
        AutomationStepFunction         _pRun;
        AutomationStepValidateFunction _pValidate;    ///< nullptr 이면 검사하지 않는다
        bool                           _bBeforeInput; ///< 참이면 그 프레임의 입력 재생 **전**에 돈다(행동 층 주입). 거짓이면 씬 틱 뒤
    };
} // namespace sw

namespace sw
{
    /**
     * @class AutomationStepRegistry
     * @brief 단계 종류 등록표입니다. 엔진 단계(`Tap` · `Expect` …)와 같은 이름은 거절합니다. 모듈을 내리면 그 모듈의 단계도 빠집니다.
     */
    class SW_API AutomationStepRegistry
    {
    public:
        /** @brief 등록합니다. 같은 이름이 이미 있거나 엔진 단계 이름이면 false 이고 등록하지 않습니다. */
        static bool registerStep( const AutomationStepRegistration* pRegistration );
        static void unregisterStep( const AutomationStepRegistration* pRegistration );
        /** @brief 이름으로 찾습니다. 없으면 nullptr 입니다. */
        static const AutomationStepRegistration* find( string_view kind );
        /** @brief 엔진이 직접 처리하는 단계 이름(`Tap` · `Expect` · `Pass` …)이면 true 입니다. 등록표는 이 이름을 받지 않습니다. */
        static bool isEngineStepKind( string_view kind );
    };
} // namespace sw

namespace sw
{
    /** @brief `SW_AUTOMATION_STEP` 가 두는 정적 등록자입니다. */
    struct SW_API AutomationStepRegistrar
    {
        explicit AutomationStepRegistrar( const AutomationStepRegistration* pRegistration );
        ~AutomationStepRegistrar();
        AutomationStepRegistrar( const AutomationStepRegistrar& )            = delete;
        AutomationStepRegistrar& operator=( const AutomationStepRegistrar& ) = delete;

        const AutomationStepRegistration* _pRegistration;
    };
} // namespace sw

/** @brief 단계 종류를 등록합니다(.cpp 의 파일 범위, `namespace sw` 안). */
#define SW_AUTOMATION_STEP( id, pKind, pRun, pValidate, bBeforeInput )                                            \
    static const ::sw::AutomationStepRegistration sw_automationStep_##id{ pKind, pRun, pValidate, bBeforeInput }; \
    static const ::sw::AutomationStepRegistrar    sw_automationStepRegistrar_##id { &sw_automationStep_##id }
