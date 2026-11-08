/**
 * @file AutomationRunnerParams.h
 * @brief 실행기가 맡는 시나리오 단계(입력 · 변수 · 감시 값 · 로그 · 이미지 · 종료)의 인자 스키마입니다(`AutomationStep::readFields`).
 * @details 수 비교(`equals` · `near` · `atLeast` · `atMost`)는 어느 속성을 적었는지가 곧 규칙이라 스키마 칸이 아니라 손으로 읽습니다(`AutomationStepArgsUtil::parseNumberComparison`).
 *          감시 값 참조(`watch` · `target` · `key`)의 뜻 검사도 등록표를 봐야 해서 단계의 검사 함수가 합니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Automation/Scenario/AutomationTimeLimits.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /** @brief 횟수 칸의 "적지 않음" 입니다. */
    inline constexpr uint32 kAutomationNoCount = 0xFFFFFFFFu;

    /** @brief `Press` · `Release` · `Tap` 의 인자입니다. `hold` 는 `Tap` 에만 씁니다. */
    REFLECT()
    struct SW_API AutomationSlotStepParams
    {
        REFLECT_BODY();

        PROPERTY( Required )
        string _slot{};
        PROPERTY()
        uint32 _hold{ 1 };
    };

    /** @brief `MouseDelta` · `Wheel` 의 인자입니다. 없는 축은 0 입니다. */
    REFLECT()
    struct SW_API AutomationAxisPairParams
    {
        REFLECT_BODY();

        PROPERTY()
        float32 _x{ 0.0f };
        PROPERTY()
        float32 _y{ 0.0f };
    };

    /** @brief `MousePosition` 의 인자입니다(클라이언트 영역의 비율). */
    REFLECT()
    struct SW_API AutomationMousePositionParams
    {
        REFLECT_BODY();

        PROPERTY( Required, Min = 0, Max = 1 )
        float32 _x{ 0.0f };
        PROPERTY( Required, Min = 0, Max = 1 )
        float32 _y{ 0.0f };
    };

    /** @brief `Touch` 의 인자입니다(클라이언트 영역의 비율 · 닿아 있는 프레임 수). 손가락 0 이 닿았다 뗀다 — 휴대 기기의 탭을 데스크톱 시나리오에서 흉내 낸다. */
    REFLECT()
    struct SW_API AutomationTouchParams
    {
        REFLECT_BODY();

        PROPERTY( Required, Min = 0, Max = 1 )
        float32 _x{ 0.0f };
        PROPERTY( Required, Min = 0, Max = 1 )
        float32 _y{ 0.0f };
        PROPERTY( Name = "frames", Min = 1, Max = 600 )
        uint32 _holdFrameCount{ 1 };
    };

    /** @brief `GamepadAxis` 의 인자입니다. */
    REFLECT()
    struct SW_API AutomationGamepadAxisParams
    {
        REFLECT_BODY();

        PROPERTY( Required )
        uint32 _axis{ 0 };
        PROPERTY( Required )
        float32 _value{ 0.0f };
        PROPERTY()
        uint32 _pad{ 0 };
    };

    /** @brief `Text` 의 인자입니다. */
    REFLECT()
    struct SW_API AutomationTextStepParams
    {
        REFLECT_BODY();

        PROPERTY( Required )
        string _value{};
    };

    /** @brief `Variable` 의 인자입니다. */
    REFLECT()
    struct SW_API AutomationVariableParams
    {
        REFLECT_BODY();

        PROPERTY( Required )
        string _name{};
        PROPERTY( Required )
        string _value{};
    };

    /** @brief `ExpectVariable` 의 인자입니다. 적지 않으면 정확히 같아야 합니다. */
    REFLECT()
    struct SW_API AutomationExpectVariableParams
    {
        REFLECT_BODY();

        PROPERTY( Required )
        string _name{};
        PROPERTY( Required )
        string _value{};
        PROPERTY( Min = 0 )
        float32 _tolerance{ 0.0f };
    };

    /** @brief `Expect` 의 글 칸입니다. 수 비교 속성은 손으로 읽습니다(파일 머리말). */
    REFLECT()
    struct SW_API AutomationExpectParams
    {
        REFLECT_BODY();

        PROPERTY( Required )
        string _watch{};
        PROPERTY()
        string _target{};
        PROPERTY()
        string _key{};
        PROPERTY( Min = 0 )
        float32 _tolerance{ 0.0f };
        PROPERTY()
        string _is{};
        PROPERTY()
        string _sameAs{};
        PROPERTY()
        string _differsFrom{};
    };

    /** @brief `WaitUntil` 의 시한 칸(`Expect` 칸에 더해진다)입니다. 셋 중 하나만 적습니다. */
    REFLECT()
    struct SW_API AutomationWaitUntilParams : public AutomationExpectParams
    {
        REFLECT_BODY();

        PROPERTY()
        uint32 _timeoutFrames{ 0 };
        PROPERTY( Min = 0 )
        float32 _timeoutSeconds{ 0.0f };
        PROPERTY( Min = 0 )
        float32 _timeoutWallSeconds{ 0.0f };
    };

    /** @brief `ExpectVisited` 의 인자입니다. `is` · `sequence` 중 하나를 적습니다. */
    REFLECT()
    struct SW_API AutomationExpectVisitedParams
    {
        REFLECT_BODY();

        PROPERTY( Required )
        string _watch{};
        PROPERTY()
        string _target{};
        PROPERTY()
        string _key{};
        PROPERTY()
        string _is{};
        PROPERTY()
        string _sequence{};
        PROPERTY()
        uint32 _since{ 0 };
    };

    /** @brief `Record` 의 인자입니다. */
    REFLECT()
    struct SW_API AutomationRecordParams
    {
        REFLECT_BODY();

        PROPERTY( Required )
        string _watch{};
        PROPERTY()
        string _target{};
        PROPERTY()
        string _key{};
    };

    /** @brief `Remember` 의 인자입니다. */
    REFLECT()
    struct SW_API AutomationRememberParams : public AutomationRecordParams
    {
        REFLECT_BODY();

        PROPERTY( Required )
        string _as{};
    };

    /** @brief `ExpectLog` 의 인자입니다. `count` · `atLeast` 중 하나를 적습니다. */
    REFLECT()
    struct SW_API AutomationExpectLogParams
    {
        REFLECT_BODY();

        PROPERTY( Required )
        string _id{};
        PROPERTY()
        string _contains{};
        PROPERTY()
        uint32 _count{ kAutomationNoCount };
        PROPERTY()
        uint32 _atLeast{ kAutomationNoCount };
        PROPERTY()
        uint32 _since{ 0 };
    };

    /** @brief `Screenshot` 의 인자입니다. */
    REFLECT()
    struct SW_API AutomationScreenshotParams
    {
        REFLECT_BODY();

        PROPERTY( Required )
        string _file{};
        PROPERTY()
        bool _bWaitForSettle{ true }; ///< NOLINT(sw-bool-flag): 집합체다
    };

    /** @brief `ExpectImage` 의 인자입니다. */
    REFLECT()
    struct SW_API AutomationExpectImageParams
    {
        REFLECT_BODY();

        PROPERTY( Required )
        string _file{};
        PROPERTY( Required )
        string _golden{};
        PROPERTY()
        string _region{};
        PROPERTY( Min = 0, Max = 1 )
        float32 _tolerance{ 0.01f }; ///< 다른 화소의 비율 상한
    };

    /** @brief `ExpectImagesDiffer` 의 인자입니다 — 값을 바꾼 두 장이 정말 다른지 보는 음성 대조입니다(골든 없이 같은 실행의 두 스크린샷을 견준다). */
    REFLECT()
    struct SW_API AutomationExpectImagesDifferParams
    {
        REFLECT_BODY();

        PROPERTY( Required )
        string _first{};
        PROPERTY( Required )
        string _second{};
        PROPERTY()
        string _region{};
        PROPERTY( Min = 0, Max = 1 )
        float32 _minDiffering{ 0.01f }; ///< 다른 화소의 비율 하한(영역을 줄인 뒤)
        PROPERTY( Min = 0, Max = 1 )
        float32 _maxDiffering{ 1.0f }; ///< 다른 화소의 비율 상한 — 골든 없이 "값을 바꿔도 그림이 깨지지 않는다"(본 LOD 켬 · 끔)를 같은 실행의 두 장으로 단언한다(`minDiffering="0"`)
    };

    /** @brief `CloseWindow` 의 인자입니다. */
    REFLECT()
    struct SW_API AutomationCloseWindowParams
    {
        REFLECT_BODY();

        PROPERTY( Min = 0 )
        float32 _withinSeconds{ AutomationTimeLimits::kDefaultExitWithinSeconds };
    };

    /** @brief `ExpectExitWithin` 의 인자입니다. */
    REFLECT()
    struct SW_API AutomationExpectExitWithinParams
    {
        REFLECT_BODY();

        PROPERTY( Min = 0 )
        float32 _seconds{ AutomationTimeLimits::kDefaultExitWithinSeconds };
    };

    /** @brief `Fail` · `Skip` 의 인자입니다. */
    REFLECT()
    struct SW_API AutomationEndStepParams
    {
        REFLECT_BODY();

        PROPERTY()
        string _reason{};
    };

    /**
     * @brief `SkipUnless` 의 인자입니다. 감시 값(`watch`)이 0 이면 시나리오를 건너뛰고(Skipped), 0 이 아니면 다음 단계로 간다.
     * @details 장치 능력이 없어 그 그림을 그리지 않는다고 **선언된** 백엔드(예: `RHI.MaterialTessellation` — 변위 머티리얼)에서 시나리오가 그 능력을 요구할 때 쓴다.
     *          조용히 통과시키지 않고 보고에 건너뛴 까닭이 남는다. 감시 값을 못 읽으면 실패다.
     */
    REFLECT()
    struct SW_API AutomationSkipUnlessParams
    {
        REFLECT_BODY();

        PROPERTY( Required )
        string _watch{};
        PROPERTY( Required )
        string _reason{};
    };
} // namespace sw
