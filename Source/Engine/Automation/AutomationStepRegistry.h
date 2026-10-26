/**
 * @file AutomationStepRegistry.h
 * @brief 시나리오 단계 종류의 레지스트리를 정의합니다. 엔진 단계(입력 · 단언 · 끝)와 GameFramework(액션 계층 의도), 에디터(ImGui), 플랫폼(창 메시지)의 단계가 모두 여기 등록합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

namespace sw
{
    struct AutomationStep;

    class AutomationStepContext;
    class InputManager;

    /** @brief 단계 하나를 실행합니다. 단언 실패는 `context.recordFailure`, 실행할 수 없으면 false(읽기 오류로 끝난다). 게임 스레드입니다. */
    using AutomationStepFunction = bool ( * )( AutomationStepContext& context, const AutomationStep& step );
    /** @brief 시작 전에 단계의 속성을 검사합니다. 틀렸으면 false 와 이유입니다. */
    using AutomationStepValidateFunction = bool ( * )( const AutomationStep& step, string& outError );
    /**
     * @brief 검사를 통과한 단계를 시작할 때 실행기에 준비합니다(입력 단계는 가상 입력 원천으로 옮기고, 같은 프레임의 다른 단계와의 관계를 봅니다). 틀렸으면 false 와 이유입니다.
     * @details 검사(`_pValidate`)와 같은 때(시작 조건이 참인 프레임) 단계마다 한 번 돈다.
     */
    using AutomationStepPrepareFunction = bool ( * )( AutomationStepContext& context, const AutomationStep& step, string& outError );

    /** @brief 씬 틱 뒤 단계 차례에서 그 단계를 어떻게 할지입니다. */
    enum class AutomationStepHold : uint8
    {
        Run,  ///< 지금 돌립니다.
        Hold, ///< 이 프레임은 붙잡습니다. 그 단계와 뒤 단계는 다음 프레임에 다시 봅니다.
        Skip, ///< 돌리지 않고 넘깁니다(실패는 묻는 함수가 적습니다).
    };
    /** @brief 씬 틱 뒤 단계 차례에 지금 돌릴지 묻습니다(조건 기다리기 · 스크린샷 기다리기). */
    using AutomationStepHoldFunction = AutomationStepHold ( * )( AutomationStepContext& context, const AutomationStep& step, InputManager& input );

    /**
     * @struct AutomationStepRegistration
     * @brief 등록 항목 하나입니다. 문자열과 함수는 등록한 이미지의 정적 저장소에 있습니다. 엔진 단계(`Tap`, `Expect` 등)도 같은 모양으로 등록합니다.
     */
    struct AutomationStepRegistration
    {
        const utf8*                    _pKind;        ///< 요소 이름입니다.
        AutomationStepFunction         _pRun;         ///< nullptr 이면 프레임에 할 일이 없습니다(입력 단계 — 준비에서 가상 입력으로 옮긴다).
        AutomationStepValidateFunction _pValidate;    ///< nullptr 이면 검사하지 않습니다.
        AutomationStepPrepareFunction  _pPrepare;     ///< nullptr 이면 준비하지 않습니다.
        AutomationStepHoldFunction     _pHold;        ///< nullptr 이면 차례가 오면 늘 돌립니다.
        bool                           _bBeforeInput; ///< 참이면 그 프레임의 입력 재생 전에 돕니다(액션 계층 주입). 거짓이면 씬 틱 뒤에 돕니다.
    };
} // namespace sw

namespace sw
{
    /**
     * @struct AutomationStepValidatorRegistration
     * @brief 단계 종류의 검사 함수만 따로 올린 등록 항목입니다(`SW_AUTOMATION_STEP_VALIDATOR`). 단계 등록(`AutomationStepRegistration`)이 같은 함수를 가리키므로 둘이 어긋나지 않는다.
     */
    struct AutomationStepValidatorRegistration
    {
        const utf8*                    _pKind;     ///< 요소 이름입니다(단계 등록의 `_pKind` 와 같은 글).
        AutomationStepValidateFunction _pValidate; ///< 모듈 없이 불러도 되는 순수 검사 함수입니다.
    };
} // namespace sw

namespace sw
{
    /** @brief 지금 막아야 하면 true 입니다(로딩 화면이 게임 입력을 막는 동안, 화면 페이드가 그림을 덮는 동안). 게임 스레드입니다. */
    using AutomationBlockFunction = bool ( * )();

    /** @brief 차단 조건이 무엇을 미루는지입니다. 넓은 쪽이 좁은 쪽을 포함한다 — 시작을 미루는 조건은 캡처도 미룬다. */
    enum class AutomationBlockScope : uint8
    {
        Capture, ///< 화면 캡처만 미룹니다(화면 페이드 — 입력은 막지 않으므로 시작은 미루지 않는다).
        Start,   ///< `ScenePlaying` 시작과 캡처를 미룹니다(로딩 화면).
        Clock,   ///< 시작 · 캡처를 미루고 도는 시나리오의 시계도 멈춥니다(`SW_AUTOMATION_CLOCK_HOLDER`).
    };

    /**
     * @struct AutomationBlocker
     * @brief 시나리오 시작 · 시계 · 화면 캡처를 미루는 조건 하나입니다. 실행기보다 위 계층(UI 의 로딩 화면, 게임 프레임워크의 화면 페이드)이 아래로 등록하며,
     *        실행기는 그 계층을 include 하지 않습니다.
     * @details `Clock` 은 결과를 로그로 늦게 내는 백그라운드 작업(저장 검증)이 쓴다. 시계가 그 작업보다 앞서 가면 뒤 단계(임시 폴더 지우기)가 작업이 읽을 파일을 바꾸고,
     *          결과 줄이 다음 시나리오 창에 떨어진다. 언리얼 자동화가 셰이더 · 에셋 컴파일을 기다린 뒤 다음 명령을 도는 것과 같다.
     *          `Capture` 는 그림을 덮는 것(페이드)이다 — 캡처가 자리 잡은 화면을 찍게 한다(`AutomationCaptureUtil::isReadyForCapture`).
     */
    struct AutomationBlocker
    {
        const utf8*             _pName; ///< 로그와 중복 판정에 쓰는 이름입니다.
        AutomationBlockFunction _pIsBlocking;
        AutomationBlockScope    _scope;
    };
} // namespace sw

namespace sw
{
    /**
     * @class AutomationStepRegistry
     * @brief 단계 종류 레지스트리입니다. 엔진 단계(`Tap`, `Expect` 등, `AutomationRunner.cpp`)도 여기 등록하므로 그 이름과 같은 등록은 중복으로 거절합니다.
     *        모듈을 언로드하면 그 모듈의 단계도 빠지고, 핫 리로드의 교체 이미지는 같은 이름을 대체합니다(`RegistrationReloadScope`).
     */
    class SW_API AutomationStepRegistry
    {
    public:
        /** @brief 등록합니다. 같은 이름이 이미 있거나 실행 · 준비 함수가 둘 다 없으면 false 이고 등록하지 않습니다. */
        [[nodiscard]] static bool registerStep( const AutomationStepRegistration* pRegistration );
        static void               unregisterStep( const AutomationStepRegistration* pRegistration );
        /** @brief 이름으로 찾습니다. 없으면 nullptr 입니다. */
        static const AutomationStepRegistration* find( string_view kind );

        /** @brief 검사 함수만 올립니다. 이름이 비었거나 함수가 없거나 같은 이름이 이미 있으면 false 이고 올리지 않습니다. 단계 등록(`registerStep`)과 별개입니다. */
        [[nodiscard]] static bool registerValidator( const AutomationStepValidatorRegistration* pRegistration );
        static void               unregisterValidator( const AutomationStepValidatorRegistration* pRegistration );
        /** @brief 검사 함수만 올린 등록을 이름으로 찾습니다. 없으면 nullptr 입니다(올라온 단계의 검사 함수는 `AutomationStepValidation::findValidator` 가 함께 봅니다). */
        static const AutomationStepValidatorRegistration* findValidator( string_view kind );

        /** @brief 차단 조건을 등록합니다. 같은 이름이 있으면 false 입니다. */
        [[nodiscard]] static bool registerBlocker( const AutomationBlocker* pBlocker );
        static void               unregisterBlocker( const AutomationBlocker* pBlocker );
        /** @brief 시작을 미루는 조건(`Start` · `Clock`) 중 하나라도 참이면 true 입니다. */
        static bool isStartBlocked();
        /** @brief 지금 화면 캡처를 미루는 조건(모든 범위)의 이름입니다. 없으면 nullptr 입니다. */
        static const utf8* findCaptureBlocker();
        /** @brief 지금 시계를 잡고 있는 조건(`Clock`)의 이름입니다. 없으면 nullptr 입니다. */
        static const utf8* findClockHolder();
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
    /** @brief `SW_AUTOMATION_START_BLOCKER` · `SW_AUTOMATION_CLOCK_HOLDER` · `SW_AUTOMATION_CAPTURE_BLOCKER` 가 두는 정적 등록자입니다. */
    struct SW_API AutomationBlockerRegistrar
    {
        explicit AutomationBlockerRegistrar( const AutomationBlocker* pBlocker );
        ~AutomationBlockerRegistrar();
        AutomationBlockerRegistrar( const AutomationBlockerRegistrar& )            = delete;
        AutomationBlockerRegistrar& operator=( const AutomationBlockerRegistrar& ) = delete;

        const AutomationBlocker* _pBlocker;
    };
} // namespace sw

/** @brief 단계 종류를 등록합니다(.cpp 의 파일 범위, `namespace sw` 안). */
#define SW_AUTOMATION_STEP( id, pKind, pRun, pValidate, bBeforeInput )                                                              \
    static const ::sw::AutomationStepRegistration sw_automationStep_##id{ pKind, pRun, pValidate, nullptr, nullptr, bBeforeInput }; \
    static const ::sw::AutomationStepRegistrar    sw_automationStepRegistrar_##id                                                   \
    {                                                                                                                               \
        &sw_automationStep_##id                                                                                                     \
    }

/** @brief 준비 · 차례 함수까지 든 단계 종류를 등록합니다(.cpp 의 파일 범위, `namespace sw` 안). 엔진 단계(입력 · 조건 기다리기 · 스크린샷 확인)가 씁니다. */
#define SW_AUTOMATION_STEP_WITH_HOOKS( id, pKind, pRun, pValidate, pPrepare, pHold, bBeforeInput )                                 \
    static const ::sw::AutomationStepRegistration sw_automationStep_##id{ pKind, pRun, pValidate, pPrepare, pHold, bBeforeInput }; \
    static const ::sw::AutomationStepRegistrar    sw_automationStepRegistrar_##id                                                  \
    {                                                                                                                              \
        &sw_automationStep_##id                                                                                                    \
    }

/** @brief 시작 차단 조건을 등록합니다(.cpp 의 파일 범위, `namespace sw` 안). 참인 동안 시나리오가 시작하지 않고 화면 캡처도 미뤄집니다. */
#define SW_AUTOMATION_START_BLOCKER( id, pName, pIsBlocking )                                                                              \
    static const ::sw::AutomationBlocker          sw_automationStartBlocker_##id{ pName, pIsBlocking, ::sw::AutomationBlockScope::Start }; \
    static const ::sw::AutomationBlockerRegistrar sw_automationStartBlockerRegistrar_##id                                                  \
    {                                                                                                                                      \
        &sw_automationStartBlocker_##id                                                                                                    \
    }

/** @brief 시계 잡기 조건을 등록합니다(.cpp 의 파일 범위, `namespace sw` 안). 참인 동안 시나리오가 시작하지 않고 도는 시나리오의 시계가 멈춥니다(`AutomationBlockScope::Clock`). */
#define SW_AUTOMATION_CLOCK_HOLDER( id, pName, pIsHolding )                                                                              \
    static const ::sw::AutomationBlocker          sw_automationClockHolder_##id{ pName, pIsHolding, ::sw::AutomationBlockScope::Clock }; \
    static const ::sw::AutomationBlockerRegistrar sw_automationClockHolderRegistrar_##id                                                 \
    {                                                                                                                                    \
        &sw_automationClockHolder_##id                                                                                                   \
    }

/** @brief 화면 캡처만 미루는 조건을 등록합니다(.cpp 의 파일 범위, `namespace sw` 안). 그림을 덮지만 입력은 막지 않는 것(화면 페이드)이 씁니다(`AutomationBlockScope::Capture`). */
#define SW_AUTOMATION_CAPTURE_BLOCKER( id, pName, pIsBlocking )                                                                                \
    static const ::sw::AutomationBlocker          sw_automationCaptureBlocker_##id{ pName, pIsBlocking, ::sw::AutomationBlockScope::Capture }; \
    static const ::sw::AutomationBlockerRegistrar sw_automationCaptureBlockerRegistrar_##id                                                    \
    {                                                                                                                                          \
        &sw_automationCaptureBlocker_##id                                                                                                      \
    }
