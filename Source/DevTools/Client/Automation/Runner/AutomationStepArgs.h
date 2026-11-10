/**
 * @file AutomationStepArgs.h
 * @brief 판정 단계(`Expect` · `WaitUntil` · `ExpectLog` · `ExpectImage` …)의 인자 읽기와 값 비교입니다.
 * @details 엔진 단계의 검사 함수(`AutomationRunnerSteps.cpp`)와 실행기의 판정(`AutomationJudge.cpp`)이 같은 읽기 하나를 씁니다 —
 *          검사가 읽어 본 뒤 실행이 결과를 버리며 다시 읽던 것을 없앱니다. 기본값은 구조체의 초기값 하나입니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"

#include "DevTools/Client/Automation/Runner/AutomationGoldenUtil.h"
#include "DevTools/Client/Automation/Runner/AutomationImage.h"

#include "Engine/Automation/Scenario/AutomationScenario.h"
#include "Engine/Automation/Scenario/AutomationTimeLimits.h"
#include "Engine/Automation/Watch/AutomationWatch.h"

namespace sw
{
    struct GlobalVariableInfo;

    /**
     * @struct AutomationStepArgsUtil
     * @brief 단계 인자 읽기(`read*` · `parse*`)와 비교(`matchesNumber` · `compareVariable`)입니다. 형식이 틀리면 그 이유(위치는 부르는 쪽이 붙인다)이고 값은 그대로입니다.
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

        /**
         * @brief `WaitUntil` 의 시한입니다. 셋 중 하나다: `timeoutFrames`(시나리오 프레임), `timeoutSeconds`(게임 초 — 고정 델타로 프레임이 된다),
         *        `timeoutWallSeconds`(벽시계 초 — 게임 시간과 무관한 바깥 일: 자식 프로세스 종료 · 파일 · GPU).
         */
        struct WaitTimeoutArgs
        {
            uint32  _timeoutFrames{ AutomationTimeLimits::kDefaultWaitTimeoutFrames };
            float32 _timeoutSeconds{ 0.0f };
            bool    _bSeconds{ false }; ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
            float32 _timeoutWallSeconds{ 0.0f };
            bool    _bWallSeconds{ false }; ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
        };

        /** @brief `ExpectImage` 의 인자입니다(`file` · `golden` · `region` · 상한 덮기 `tolerance` · `maxMeanDistance` · `maxRegionPixels`). */
        struct ExpectImageArgs
        {
            AutomationImageRegion _region{};
            string                _golden{};               ///< 골든 이름(`<시나리오 폴더>/golden/<시나리오>.<이름>.<백엔드>.ppm`)
            optional<float64>     _maxDifferingFraction{}; ///< `tolerance` — 적지 않으면 정책의 기본
            optional<float64>     _maxMeanDistance{};      ///< `maxMeanDistance`
            optional<uint32>      _maxDeepRegionPixels{};  ///< `maxRegionPixels`

            /** @brief 정책의 기본 @p defaults 에 이 단계가 적은 상한을 덮은 값입니다. */
            AutomationGoldenLimits resolveLimits( const AutomationGoldenLimits& defaults ) const
            {
                AutomationGoldenLimits limits = defaults;
                if ( _maxDifferingFraction.has_value() )
                    limits._maxDifferingFraction = *_maxDifferingFraction;
                if ( _maxMeanDistance.has_value() )
                    limits._maxMeanDistance = *_maxMeanDistance;
                if ( _maxDeepRegionPixels.has_value() )
                    limits._maxDeepRegionPixels = *_maxDeepRegionPixels;
                return limits;
            }
        };

        static constexpr float32 kWatchNearTolerance = 1.0e-4f; ///< 감시 값 `near` 의 기본 허용 폭

        /** @brief 있으면 수로 읽습니다. 없으면 @p inoutValue 를 둡니다(기본값은 인자 구조체의 초기값). 형식이 틀리면 이유이고 값은 그대로입니다. */
        [[nodiscard]] static expected<void> readFloat( const AutomationStep& step, string_view name, float32& inoutValue );
        /** @brief 있으면 음이 아닌 정수로 읽습니다. 없으면 @p inoutValue 를 둡니다. */
        [[nodiscard]] static expected<void> readUint( const AutomationStep& step, string_view name, uint32& inoutValue );

        /** @brief 수 비교 하나를 읽습니다. `equals` · `near` · `atLeast` · `atMost` 중 적힌 것이 없으면 오류입니다(정확히 하나인지는 단계의 검사 함수가 봅니다). */
        [[nodiscard]] static expected<NumberComparison> parseNumberComparison( const AutomationStep& step, float32 nearTolerance );
        /** @brief @p comparison 대로 @p value 를 비교합니다. 실패 메시지의 규칙 글을 @p outRule 에 씁니다. */
        static bool                                    matchesNumber( const NumberComparison& comparison, float64 value, string& outRule );
        [[nodiscard]] static expected<WaitTimeoutArgs> parseWaitTimeout( const AutomationStep& step );
        [[nodiscard]] static expected<ExpectImageArgs> parseExpectImage( const AutomationStep& step );

        /**
         * @brief 변수의 지금 값이 @p expected 와 같은지 @p outMatched 에 적습니다. 실수는 @p tolerance 안이면 같습니다.
         * @return @p expected 를 변수의 타입으로 읽지 못하면 false 입니다(enum 은 숫자만 받는다).
         */
        [[nodiscard]] static bool compareVariable( const GlobalVariableInfo& info, string_view expected, float32 tolerance, bool& outMatched );

        /** @brief 감시 값 값의 글입니다(수는 끝의 0 을 정리한다). */
        static string makeValueText( const AutomationWatchValue& value );
        /** @brief 수를 글로 씁니다(끝의 0 을 정리한다 — 1.000000 은 1). */
        static string formatNumber( float64 value );
    };
} // namespace sw
