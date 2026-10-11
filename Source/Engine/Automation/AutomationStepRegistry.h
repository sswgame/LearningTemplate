/**
 * @file AutomationStepRegistry.h
 * @brief 엔진 밖 단계 종류의 레지스트리를 정의합니다. GameFramework(액션 계층 의도), 에디터(ImGui), 플랫폼(창 메시지)이 시나리오 단계를 추가합니다.
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

    /** @struct AutomationStepRegistration @brief 등록 항목 하나입니다. 문자열과 함수는 등록한 이미지의 정적 저장소에 있습니다. */
    struct AutomationStepRegistration
    {
        const utf8*                    _pKind; ///< 요소 이름입니다.
        AutomationStepFunction         _pRun;
        AutomationStepValidateFunction _pValidate;    ///< nullptr 이면 검사하지 않습니다.
        bool                           _bBeforeInput; ///< 참이면 그 프레임의 입력 재생 전에 돕니다(액션 계층 주입). 거짓이면 씬 틱 뒤에 돕니다.
    };
} // namespace sw

namespace sw
{
    /** @brief 지금 시나리오 시작을 막아야 하면 true 입니다(로딩 화면이 게임 입력을 막는 동안). 게임 스레드입니다. */
    using AutomationStartBlockFunction = bool ( * )();

    /**
     * @struct AutomationStartBlocker
     * @brief `ScenePlaying` 시작 조건을 미루는 조건 하나입니다. 실행기보다 위 계층(UI 의 로딩 화면)이 아래로 등록하며, 실행기는 그 계층을 include 하지 않습니다.
     */
    struct AutomationStartBlocker
    {
        const utf8*                  _pName; ///< 로그와 중복 판정에 쓰는 이름입니다.
        AutomationStartBlockFunction _pIsBlocking;
    };
} // namespace sw

namespace sw
{
    /**
     * @class AutomationStepRegistry
     * @brief 단계 종류 레지스트리입니다. 엔진 단계(`Tap`, `Expect` 등)와 같은 이름은 거절합니다. 모듈을 언로드하면 그 모듈의 단계도 빠집니다.
     */
    class SW_API AutomationStepRegistry
    {
    public:
        /** @brief 등록합니다. 같은 이름이 이미 있거나 엔진 단계 이름이면 false 이고 등록하지 않습니다. */
        static bool registerStep( const AutomationStepRegistration* pRegistration );
        static void unregisterStep( const AutomationStepRegistration* pRegistration );
        /** @brief 이름으로 찾습니다. 없으면 nullptr 입니다. */
        static const AutomationStepRegistration* find( string_view kind );
        /** @brief 엔진이 직접 처리하는 단계 이름(`Tap`, `Expect`, `Pass` 등)이면 true 입니다. 레지스트리는 이 이름을 받지 않습니다. */
        static bool isEngineStepKind( string_view kind );

        /** @brief 시작 차단 조건을 등록합니다. 같은 이름이 있으면 false 입니다. */
        static bool registerStartBlocker( const AutomationStartBlocker* pBlocker );
        static void unregisterStartBlocker( const AutomationStartBlocker* pBlocker );
        /** @brief 등록된 조건 중 하나라도 시작을 막으면 true 입니다. */
        static bool isStartBlocked();
        /** @brief 언로드된 모듈이 키운 두 레지스트리의 용량을 해제합니다. 엔진 종료 끝(`EngineBootstrap::shutdown`)이 모듈을 모두 언로드한 뒤 부릅니다. */
        static void releaseUnusedStorage();
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

namespace sw
{
    /** @brief `SW_AUTOMATION_START_BLOCKER` 가 두는 정적 등록자입니다. */
    struct SW_API AutomationStartBlockerRegistrar
    {
        explicit AutomationStartBlockerRegistrar( const AutomationStartBlocker* pBlocker );
        ~AutomationStartBlockerRegistrar();
        AutomationStartBlockerRegistrar( const AutomationStartBlockerRegistrar& )            = delete;
        AutomationStartBlockerRegistrar& operator=( const AutomationStartBlockerRegistrar& ) = delete;

        const AutomationStartBlocker* _pBlocker;
    };
} // namespace sw

/** @brief 단계 종류를 등록합니다(.cpp 의 파일 범위, `namespace sw` 안). */
#define SW_AUTOMATION_STEP( id, pKind, pRun, pValidate, bBeforeInput )                                            \
    static const ::sw::AutomationStepRegistration sw_automationStep_##id{ pKind, pRun, pValidate, bBeforeInput }; \
    static const ::sw::AutomationStepRegistrar    sw_automationStepRegistrar_##id { &sw_automationStep_##id }

/** @brief 시작 차단 조건을 등록합니다(.cpp 의 파일 범위, `namespace sw` 안). */
#define SW_AUTOMATION_START_BLOCKER( id, pName, pIsBlocking )                                                \
    static const ::sw::AutomationStartBlocker          sw_automationStartBlocker_##id{ pName, pIsBlocking }; \
    static const ::sw::AutomationStartBlockerRegistrar sw_automationStartBlockerRegistrar_##id { &sw_automationStartBlocker_##id }
