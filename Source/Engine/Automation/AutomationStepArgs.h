/**
 * @file AutomationStepArgs.h
 * @brief 판정 단계(`Expect` · `WaitUntil` · `ExpectLog` · `ExpectImage` …)의 인자 읽기와 값 비교입니다.
 * @details 엔진 단계의 검사 함수(`AutomationRunnerSteps.cpp`)와 실행기의 판정(`AutomationRunnerJudge.cpp`)이 같은 읽기 하나를 씁니다 —
 *          검사가 읽어 본 뒤 실행이 결과를 버리며 다시 읽던 것을 없앱니다. 기본값은 구조체의 초기값 하나입니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

#include "Engine/Automation/AutomationImageMetric.h"
#include "Engine/Automation/AutomationThresholdBasis.h"
#include "Engine/Probe/AutomationProbe.h"
#include "Engine/Scenario/AutomationScenario.h"
#include "Engine/Scenario/AutomationTimeLimits.h"

namespace sw
{
    struct GlobalVariableInfo;

    /**
     * @struct AutomationStepArgsUtil
     * @brief 단계 인자 읽기(`read*` · `parse*`)와 비교(`matchesNumber` · `compareVariable`)입니다. 형식이 틀리면 false 와 이유이고 값은 그대로입니다.
     */
    struct AutomationStepArgsUtil
    {
        /** @brief 수 비교 규칙(`equals` · `near` · `atLeast` · `atMost`)입니다. */
        enum class NumberRule : uint8
        {
            Equals,
            Near,
            AtLeast,
            AtMost,
        };

        /** @brief 수 비교 하나입니다. */
        struct NumberComparison
        {
            string     _text{}; ///< 데이터에 적힌 기대값 글(실패 메시지)
            float32    _expected{ 0.0f };
            float32    _tolerance{ 0.0f }; ///< `near` 의 허용 폭
            NumberRule _rule{ NumberRule::Equals };
        };

        /** @brief `WaitUntil` 의 시한입니다. `timeoutSeconds` 를 적었으면 초, 아니면 프레임입니다. */
        struct WaitTimeoutArgs
        {
            uint32  _timeoutFrames{ AutomationTimeLimits::kDefaultWaitTimeoutFrames };
            float32 _timeoutSeconds{ 0.0f };
            bool    _bSeconds{ false };
        };

        /** @brief `ExpectLog` 의 인자입니다. */
        struct ExpectLogArgs
        {
            uint32 _since{ 0 };
            uint32 _expected{ 0 };
            bool   _bExactCount{ false }; ///< 참이면 `count`(같아야 한다), 거짓이면 `atLeast`
        };

        /** @brief `darkFraction` 의 기본 임계값(영역 중앙값에 곱한다)입니다. */
        static constexpr float32 kDefaultDarkRatio = 0.7f;
        /** @brief `ExpectImage` 의 인자입니다. */
        struct ExpectImageArgs
        {
            NumberComparison          _comparison{};
            AutomationThresholdBasis  _basis{}; ///< 문턱이 잰 값(`measured`)인지 어림값(`provisional`)인지
            AutomationImageRegion     _region{};
            float32                   _ratio{ kDefaultDarkRatio };
            AutomationImageMetricKind _metric{ AutomationImageMetricKind::MeanLuma };
        };

        static constexpr float32 kProbeNearTolerance = 1.0e-4f; ///< 탐침 `near` 의 기본 허용 폭
        static constexpr float32 kImageNearTolerance = 1.0e-3f; ///< 이미지 측정 `near` 의 기본 허용 폭

        /** @brief 있으면 수로 읽습니다. 없으면 @p inoutValue 를 둡니다(기본값은 인자 구조체의 초기값). 형식이 틀리면 false 와 이유이고 값은 그대로입니다. */
        [[nodiscard]] static bool readFloat( const AutomationStep& step, string_view name, float32& inoutValue, string& outError );
        /** @brief 있으면 음이 아닌 정수로 읽습니다. 없으면 @p inoutValue 를 둡니다. */
        [[nodiscard]] static bool readUint( const AutomationStep& step, string_view name, uint32& inoutValue, string& outError );

        /** @brief 수 비교 하나를 읽습니다. `equals` · `near` · `atLeast` · `atMost` 중 적힌 것이 없으면 false 입니다(정확히 하나인지는 단계의 검사 함수가 봅니다). */
        [[nodiscard]] static bool parseNumberComparison( const AutomationStep& step, float32 nearTolerance, NumberComparison& outComparison, string& outError );
        /** @brief @p comparison 대로 @p value 를 비교합니다. 실패 메시지의 규칙 글을 @p outRule 에 씁니다. */
        static bool               matchesNumber( const NumberComparison& comparison, float64 value, string& outRule );
        [[nodiscard]] static bool parseWaitTimeout( const AutomationStep& step, WaitTimeoutArgs& outArgs, string& outError );
        [[nodiscard]] static bool parseExpectLog( const AutomationStep& step, ExpectLogArgs& outArgs, string& outError );
        [[nodiscard]] static bool parseExpectImage( const AutomationStep& step, ExpectImageArgs& outArgs, string& outError );

        /**
         * @brief 변수의 지금 값이 @p expected 와 같은지 @p outMatched 에 적습니다. 실수는 @p tolerance 안이면 같습니다.
         * @return @p expected 를 변수의 타입으로 읽지 못하면 false 입니다(enum 은 숫자만 받는다).
         */
        [[nodiscard]] static bool compareVariable( const GlobalVariableInfo& info, string_view expected, float32 tolerance, bool& outMatched );

        /** @brief 탐침 값의 글입니다(수는 끝의 0 을 정리한다). */
        static string makeValueText( const AutomationProbeValue& value );
        /** @brief 수를 글로 씁니다(끝의 0 을 정리한다 — 1.000000 은 1). */
        static string formatNumber( float64 value );
    };
} // namespace sw
