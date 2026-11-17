/**
 * @file AutomationStepRegistry.h
 * @brief 시나리오 단계 종류의 레지스트리를 정의합니다. 엔진 단계(입력 · 단언 · 끝)와 GameFramework(액션 계층 의도), 에디터(ImGui), 플랫폼(창 메시지)의 단계가 모두 여기 등록합니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Registry/RegistrationList.h"

#include "Engine/Automation/Scenario/AutomationReadiness.h"

namespace sw
{
    struct AutomationStep;

    class AutomationStepContext;
    class InputManager;

    /** @brief 단계 하나를 실행합니다. 단언 실패는 `context.recordFailure`, 실행할 수 없으면 false(읽기 오류로 끝난다). 게임 스레드입니다. */
    using AutomationStepFunction = bool ( * )( AutomationStepContext& context, const AutomationStep& step );
    /** @brief 시작 전에 단계의 속성을 검사합니다. 틀렸으면 그 이유(위치는 부르는 쪽이 `AutomationStep::describeError` 로 붙인다)입니다. */
    using AutomationStepValidateFunction = expected<void> ( * )( const AutomationStep& step );
    /**
     * @brief 검사를 통과한 단계를 시작할 때 실행기에 준비합니다(입력 단계는 가상 입력 원천으로 옮기고, 같은 프레임의 다른 단계와의 관계를 봅니다). 틀렸으면 그 이유입니다.
     * @details 검사(`_pValidate`)와 같은 때(시작 조건이 참인 프레임) 단계마다 한 번 돈다.
     */
    using AutomationStepPrepareFunction = expected<void> ( * )( AutomationStepContext& context, const AutomationStep& step );

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
     * @struct AutomationStepOptions
     * @brief 단계 등록의 선택 항목입니다. 없으면 씬 틱 뒤에 돌고 준비 · 차례 함수와 벽시계 몫이 없습니다. 조합마다 매크로를 두지 않으려는 자리입니다(C++17 이라 지정 초기화 대신 잇는 함수).
     */
    struct AutomationStepOptions
    {
        AutomationStepPrepareFunction _pPrepare{ nullptr };
        AutomationStepHoldFunction    _pHold{ nullptr };
        float32                       _wallSeconds{ 0.0f };
        bool                          _bBeforeInput{ false }; ///< NOLINT(sw-bool-flag): 집합체다 — C++17 비트필드는 기본값을 둘 수 없다
        bool                          _bUsesDesktop{ false }; ///< NOLINT(sw-bool-flag): 집합체다 — C++17 비트필드는 기본값을 둘 수 없다

        /** @brief 검사를 지난 단계를 시작할 때 준비합니다(`AutomationStepRegistration::_pPrepare`). */
        [[nodiscard]] constexpr AutomationStepOptions withPrepare( AutomationStepPrepareFunction pPrepare ) const
        {
            AutomationStepOptions options = *this;
            options._pPrepare             = pPrepare;
            return options;
        }
        /** @brief 씬 틱 뒤 차례에 지금 돌릴지 묻습니다(`AutomationStepRegistration::_pHold`). */
        [[nodiscard]] constexpr AutomationStepOptions withHold( AutomationStepHoldFunction pHold ) const
        {
            AutomationStepOptions options = *this;
            options._pHold                = pHold;
            return options;
        }
        /** @brief 그 프레임의 입력 재생 전에 돕니다(`AutomationStepRegistration::_bBeforeInput`). */
        [[nodiscard]] constexpr AutomationStepOptions withBeforeInput() const
        {
            AutomationStepOptions options = *this;
            options._bBeforeInput         = true;
            return options;
        }
        /** @brief 데스크톱 전역 상태를 읽거나 씁니다(`AutomationStepRegistration::_bUsesDesktop`). */
        [[nodiscard]] constexpr AutomationStepOptions withDesktop() const
        {
            AutomationStepOptions options = *this;
            options._bUsesDesktop         = true;
            return options;
        }
        /** @brief 단계 하나가 시나리오 벽시계 상한에 @p wallSeconds 초를 더합니다(`AutomationStepRegistration::_wallSeconds`). */
        [[nodiscard]] constexpr AutomationStepOptions withWallBudget( float32 wallSeconds ) const
        {
            AutomationStepOptions options = *this;
            options._wallSeconds          = wallSeconds;
            return options;
        }
    };

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
        bool                           _bBeforeInput; ///< 참이면 그 프레임의 입력 재생 전에 돕니다(액션 계층 주입). 거짓이면 씬 틱 뒤에 돕니다. NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
        /**
         * @brief 참이면 데스크톱 전역 상태(OS 커서 클립 · 전경 창 · 화면)를 읽거나 쓰는 단계입니다(`AutomationStepOptions::withDesktop`).
         * @details 이런 단계는 겹쳐 도는 다른 프로세스의 창과 서로 방해하므로 혼합 입력(`input="mixed"`) 시나리오에서만 쓸 수 있고, 그 시나리오가 `<타깃>_Desktop` 에서 혼자 돈다
         *          (`AutomationStepValidation::validateDesktopSteps`).
         */
        bool _bUsesDesktop{ false }; ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
        /**
         * @brief 이 단계 하나가 시나리오의 벽시계 상한에 더하는 초입니다(`AutomationStepOptions::withWallBudget`). 0 이면 더하지 않는다.
         * @details 단계 하나가 몇 초씩 드는 종류(백엔드 교체)가 쓴다 — 상한이 그 수에 맞게 는다(`AutomationRunner::computeWallLimitSeconds`).
         */
        float32 _wallSeconds{ 0.0f };

        /** @brief `SW_AUTOMATION_STEP` 의 몸통입니다. */
        [[nodiscard]] static AutomationStepRegistration make( const utf8* pKind, AutomationStepFunction pRun, AutomationStepValidateFunction pValidate, const AutomationStepOptions& options )
        {
            return AutomationStepRegistration{ pKind, pRun, pValidate, options._pPrepare, options._pHold, options._bBeforeInput, options._bUsesDesktop, options._wallSeconds };
        }

        /**
         * @brief 실행 없이 시계만 붙잡는 단계(`WaitUntil` · `EditorWaitStable`)면 true 입니다.
         * @details 이런 단계는 제 프레임을 혼자 갖는다 — 붙잡힌 프레임에 같이 적힌 다른 단계(입력 · 입력 전 단계)가 다시 돌지 않게(`AutomationScenario` 의 프레임 배정),
         *          그리고 같은 프레임의 단계 순서 검사(`validateStepPhaseOrder`)에서 빠진다. 이름이 아니라 등록 모양으로 가린다 — 기다리는 단계를 더해도 규칙이 따라온다.
         */
        [[nodiscard]] bool isWaitOnly() const { return _pRun == nullptr && _pHold != nullptr; }
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
    class JSONValue;

    /**
     * @brief 조건의 문턱을 `Config/Dev/ScenarioRun.json` 의 `_conditions.<조건 이름>` 에서 읽어 조건을 등록한 기능이 제 자리에 둡니다. 칸이 없거나 틀리면 그 칸이 대상인 오류입니다.
     * @details 공용 구조체에 칸을 더하지 않는다 — 기능마다 칸을 더하던 닫힌 집합체(배포 엔진 설정의 사진 정착 문턱 칸)가 세 갈래의 충돌 자리였다(DesignBacklog 실패 9).
     */
    using AutomationConditionConfigureFunction = expected<void> ( * )( const JSONValue& config );

    /** @brief 차단 조건이 무엇을 미루는지입니다. 넓은 쪽이 좁은 쪽을 포함한다 — 시작을 미루는 조건은 캡처도 미룬다. */
    enum class AutomationBlockScope : uint8
    {
        Finish,       ///< `<Pass/>` 만 미룹니다 - 단계는 막지 않는 뒤의 일(편집기 UI 캡처의 파일 쓰기)이 시나리오 끝 전에 마치게 한다. 넓은 범위에 들지 않는다(같은 범위만 본다).
        RenderStable, ///< 사진 앞 렌더 정착(`<Screenshot stability="render">` · `<WaitRenderStable/>`)만 기다립니다 — TAA 기록 · 노출 적응. 넓은 범위에 들지 않는다(같은 범위만 본다).
        Capture,      ///< 화면 캡처만 미룹니다(화면 페이드 — 입력은 막지 않으므로 시작은 미루지 않는다).
        Start,        ///< `ScenePlaying` 시작과 캡처를 미룹니다(로딩 화면).
        Clock,        ///< 시작 · 캡처를 미루고 도는 시나리오의 시계도 멈춥니다(`SW_AUTOMATION_CLOCK_HOLDER`).
    };

    /**
     * @struct AutomationBlocker
     * @brief 시나리오 시작 · 시계 · 화면 캡처를 미루는 조건 하나입니다. 실행기보다 위 계층(UI 의 로딩 화면, 게임 프레임워크의 화면 페이드, 렌더러의 TAA 기록)이 아래로 등록하며,
     *        실행기는 그 계층을 include 하지 않습니다.
     * @details 조건은 세 값(`AutomationReadiness`)을 낸다 — 해당 없음은 막지 않고, 진행 중은 진척 수와 무엇을 기다리는지를 낸다(`bool` 조건은 등록할 수 없다).
     *          `Clock` 은 결과를 로그로 늦게 내는 백그라운드 작업(저장 검증)이 쓴다. 시계가 그 작업보다 앞서 가면 뒤 단계(임시 폴더 지우기)가 작업이 읽을 파일을 바꾸고,
     *          결과 줄이 다음 시나리오 창에 떨어진다. 언리얼 자동화가 셰이더 · 에셋 컴파일을 기다린 뒤 다음 명령을 도는 것과 같다.
     *          `Capture` 는 그림을 덮는 것(페이드)이다 — 캡처가 자리 잡은 화면을 찍게 한다(`AutomationCaptureUtil::isReadyForCapture`).
     *          `RenderStable` 은 그 상태를 만든 기능이 제 폴더에서 등록한다(TAA 기록은 `Renderer/Frame/View/RenderStability.cpp`) — 실행기는 조건 이름만 안다.
     */
    struct AutomationBlocker
    {
        const utf8*                          _pName; ///< 로그 · 중복 판정 · `ScenarioRun.json` 의 `_conditions` 칸 이름입니다.
        AutomationReadinessFunction          _pReadiness;
        AutomationBlockScope                 _scope;
        AutomationConditionConfigureFunction _pConfigure{ nullptr }; ///< 문턱을 읽는 함수(없으면 문턱이 없는 조건)
    };

    /** @brief 지금 기다리는 조건 하나(이름 · 무엇 · 진척)입니다. 기다리는 것이 없으면 이름이 nullptr 입니다. */
    struct AutomationWaitingCondition
    {
        const utf8* _pName{ nullptr };
        const utf8* _pWhat{ nullptr };
        uint64      _progress{ 0 };

        [[nodiscard]] bool isWaiting() const { return _pName != nullptr; }
    };
} // namespace sw

namespace sw
{
    /**
     * @class AutomationStepRegistry
     * @brief 단계 종류 레지스트리입니다. 엔진 단계(`Tap`, `Expect` 등, `AutomationRunnerSteps.cpp`)도 여기 등록하므로 그 이름과 같은 등록은 중복으로 거절합니다.
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
        /** @brief 시작을 미루는 조건(`Start` · `Clock`) 중 하나라도 진행 중이면 true 입니다. */
        static bool isStartBlocked();
        /** @brief 지금 화면 캡처를 미루는 조건(`Capture` · `Start` · `Clock`)의 첫 진행 중인 것입니다. */
        static AutomationWaitingCondition findCaptureBlocker();
        /** @brief 지금 시계를 잡고 있는 조건(`Clock`)의 첫 진행 중인 것입니다. */
        static AutomationWaitingCondition findClockHolder();
        /** @brief 지금 시나리오 끝(`<Pass/>`)을 미루는 조건(`Finish` 만)의 첫 진행 중인 것입니다. */
        static AutomationWaitingCondition findFinishBlocker();
        /** @brief 사진 앞 렌더 정착 조건(`RenderStable` 만)의 첫 진행 중인 것입니다. 해당 없음은 막지 않는다. */
        static AutomationWaitingCondition findStabilityCondition();
        /** @brief 지금 진행 중인 조건(정착 조건 `RenderStable` 은 빼고) 모두를 "이름 (무엇, progress N)" 으로 잇습니다. 없으면 "none" 입니다. 오래 기다릴 때의 보고(`AutomationRunner::reportOngoingWait`)가 부른다. */
        static string describeActiveBlockers();
        /**
         * @brief 문턱을 읽는 조건마다 @p conditions(`ScenarioRun.json` 의 `_conditions`)에서 제 이름의 칸을 넘깁니다. 칸이 없거나 조건이 거절하면 그 오류입니다(첫 오류에서 멈춘다).
         * @details 시나리오를 시작할 때마다 부른다(`AutomationRunPolicy::load`) — 뒤에 올라온 모듈의 조건도 문턱을 받는다.
         */
        [[nodiscard]] static expected<void> configureConditions( const JSONValue& conditions );
    };
} // namespace sw

namespace sw
{
    /** @brief `SW_AUTOMATION_STEP` 가 두는 정적 등록자입니다(공통 틀 `StaticRegistrar`). */
    SW_DEFINE_STATIC_REGISTRAR( AutomationStepRegistrar, const AutomationStepRegistration*, &AutomationStepRegistry::registerStep, &AutomationStepRegistry::unregisterStep );
} // namespace sw

namespace sw
{
    /** @brief `SW_AUTOMATION_START_BLOCKER` · `SW_AUTOMATION_CLOCK_HOLDER` · `SW_AUTOMATION_CAPTURE_BLOCKER` · `SW_AUTOMATION_STABILITY_CONDITION` 이 두는 정적 등록자입니다(공통 틀 `StaticRegistrar`). */
    SW_DEFINE_STATIC_REGISTRAR( AutomationBlockerRegistrar, const AutomationBlocker*, &AutomationStepRegistry::registerBlocker, &AutomationStepRegistry::unregisterBlocker );
} // namespace sw

#if !defined( SW_SHIPPING )

    /**
     * @brief 단계 종류를 등록합니다(.cpp 의 파일 범위, `namespace sw` 안). 모양이 다른 단계도 이 매크로 하나이고, 다른 점은 @p options 에만 적습니다.
     * @details @p pValidate 는 스키마 구조체만 읽으면 되는 단계라면 `&AutomationStep::validateProperties<TParams>` 입니다(모르는 속성 · 필수 · 범위가 `PROPERTY` 에서 나온다). 의미 검사가 더 있을 때만 함수를 씁니다.
     * @code
     *     SW_AUTOMATION_STEP( controlMount, "Mount", &runMount, &AutomationStep::validateProperties<MountStepParams>, AutomationStepOptions{} );
     *     SW_AUTOMATION_STEP( runnerPress, "Press", nullptr, &validateSlotStep, AutomationStepOptions{}.withPrepare( &prepareSlotStep ) );
     * @endcode
     */
    #define SW_AUTOMATION_STEP( id, pKind, pRun, pValidate, options )                                                                                     \
        static const ::sw::AutomationStepRegistration sw_automationStep_##id = ::sw::AutomationStepRegistration::make( pKind, pRun, pValidate, options ); \
        static const ::sw::AutomationStepRegistrar    sw_automationStepRegistrar_##id                                                                     \
        {                                                                                                                                                 \
            &sw_automationStep_##id                                                                                                                       \
        }

    /** @brief 시작 차단 조건을 등록합니다(.cpp 의 파일 범위, `namespace sw` 안). 진행 중(`AutomationReadiness::waiting`)인 동안 시나리오가 시작하지 않고 화면 캡처도 미뤄집니다. */
    #define SW_AUTOMATION_START_BLOCKER( id, pName, pReadiness ) SW_STATIC_REGISTRATION( automationStartBlocker, ::sw::AutomationBlocker, ::sw::AutomationBlockerRegistrar, id, pName, pReadiness, ::sw::AutomationBlockScope::Start )

    /** @brief 시계 잡기 조건을 등록합니다(.cpp 의 파일 범위, `namespace sw` 안). 진행 중인 동안 시나리오가 시작하지 않고 도는 시나리오의 시계가 멈춥니다(`AutomationBlockScope::Clock`). */
    #define SW_AUTOMATION_CLOCK_HOLDER( id, pName, pReadiness ) SW_STATIC_REGISTRATION( automationClockHolder, ::sw::AutomationBlocker, ::sw::AutomationBlockerRegistrar, id, pName, pReadiness, ::sw::AutomationBlockScope::Clock )

    /** @brief 시나리오 끝(`<Pass/>`)만 미루는 조건을 등록합니다(.cpp 의 파일 범위, `namespace sw` 안). 단계 사이에는 돌게 두고 끝나기 전에만 기다릴 뒤의 일이 씁니다(`AutomationBlockScope::Finish`). */
    #define SW_AUTOMATION_FINISH_BLOCKER( id, pName, pReadiness ) SW_STATIC_REGISTRATION( automationFinishBlocker, ::sw::AutomationBlocker, ::sw::AutomationBlockerRegistrar, id, pName, pReadiness, ::sw::AutomationBlockScope::Finish )

    /** @brief 화면 캡처만 미루는 조건을 등록합니다(.cpp 의 파일 범위, `namespace sw` 안). 그림을 덮지만 입력은 막지 않는 것(화면 페이드)이 씁니다(`AutomationBlockScope::Capture`). */
    #define SW_AUTOMATION_CAPTURE_BLOCKER( id, pName, pReadiness ) SW_STATIC_REGISTRATION( automationCaptureBlocker, ::sw::AutomationBlocker, ::sw::AutomationBlockerRegistrar, id, pName, pReadiness, ::sw::AutomationBlockScope::Capture )

    /**
     * @brief 사진 앞 렌더 정착 조건을 등록합니다(.cpp 의 파일 범위, `namespace sw` 안). 그 상태(TAA 기록 · 노출 적응)를 만든 기능이 제 폴더에서 등록한다(`AutomationBlockScope::RenderStable`).
     * @param pConfigure 문턱을 `ScenarioRun.json` 의 `_conditions.<pName>` 에서 읽는 함수(`AutomationConditionConfigureFunction`) — 칸이 없으면 시나리오가 읽기 오류로 시작하지 않는다
     */
    #define SW_AUTOMATION_STABILITY_CONDITION( id, pName, pReadiness, pConfigure ) SW_STATIC_REGISTRATION( automationStabilityCondition, ::sw::AutomationBlocker, ::sw::AutomationBlockerRegistrar, id, pName, pReadiness, ::sw::AutomationBlockScope::RenderStable, pConfigure )

#else

// 자동화를 뺀 배포본: 등록하지 않고 본문 함수만 붙잡는다(`Engine/Automation/Watch/AutomationHooksEnabled.h`).
    #define SW_AUTOMATION_STEP( id, pKind, pRun, pValidate, options )                                                   \
        [[maybe_unused]] static constexpr auto                        sw_automationStepRunUnused_##id      = pRun;      \
        [[maybe_unused]] static constexpr auto                        sw_automationStepValidateUnused_##id = pValidate; \
        [[maybe_unused]] static constexpr ::sw::AutomationStepOptions sw_automationStepOptionsUnused_##id  = options
    #define SW_AUTOMATION_START_BLOCKER( id, pName, pReadiness )   [[maybe_unused]] static constexpr auto sw_automationStartBlockerUnused_##id = pReadiness
    #define SW_AUTOMATION_CLOCK_HOLDER( id, pName, pReadiness )    [[maybe_unused]] static constexpr auto sw_automationClockHolderUnused_##id = pReadiness
    #define SW_AUTOMATION_FINISH_BLOCKER( id, pName, pReadiness )  [[maybe_unused]] static constexpr auto sw_automationFinishBlockerUnused_##id = pReadiness
    #define SW_AUTOMATION_CAPTURE_BLOCKER( id, pName, pReadiness ) [[maybe_unused]] static constexpr auto sw_automationCaptureBlockerUnused_##id = pReadiness
    #define SW_AUTOMATION_STABILITY_CONDITION( id, pName, pReadiness, pConfigure )                               \
        [[maybe_unused]] static constexpr auto sw_automationStabilityConditionUnused_##id          = pReadiness; \
        [[maybe_unused]] static constexpr auto sw_automationStabilityConditionConfigureUnused_##id = pConfigure

#endif // !SW_SHIPPING
