/**
 * @file AutomationResult.h
 * @brief 시나리오 한 편이 끝난 결과(`AutomationResult`)와 판정 값(`AutomationRunResult` — 결과 · 실패 종류 · 까닭 한 줄)을 정의합니다.
 *        숫자 값은 없습니다 — 프로세스 종료 코드로의 번역은 `AutomationExitCode.h` 가 맡습니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Types.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/formatString.h"

#include "Engine/Reflection/EnumName.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @brief 시나리오가 끝난 까닭입니다. 실행기(`AutomationRunner`)가 돌려주고 체인 · 세션이 읽습니다.
     * @details 리플렉션 열거형이다 — 요약 줄 · 보고의 이름은 `getEnumName` 이 준다(`Passed`, `LoadError` …). 행 감시 스레드는 잠금을 타지 않게 시작할 때 복사한 이름을 쓴다(`AutomationHangPayload::_result`).
     *          정수 값을 일부러 두지 않았다 — 종료 코드는 App 이 밖으로 내는 약속이라 이 열거형이 곧 코드가 되면 실행기의 결과 형식이 종료 코드 표에 묶인다.
     */
    ENUM()
    enum class AutomationResult : uint8
    {
        Running,
        Passed,
        Failed,
        LoadError, ///< 파일, 형식, 모르는 단계, 모르는 속성, 모르는 감시 값이 있습니다. 조용히 버리지 않습니다.
        TimedOut,
        Skipped,      ///< 이 기계에서 돌 수 없습니다(전경 창을 얻지 못한 경우 등). CTest 는 건너뜀으로 봅니다.
        UnhandledLog, ///< 단언은 모두 맞았지만 허용 목록에도 `<ExpectLog>` 에도 없는 `[Warning]` · `[Error]` 줄이 남았습니다(id 없는 줄 포함). 통과가 아닙니다.
    };

    /**
     * @brief 진 시나리오의 실패 종류입니다 — 끝 줄 · 보고 · 부모 요약이 "무엇으로 졌는가" 를 이 이름으로 가른다(`chain stage 3/9 Failed (x): GoldenMismatch - …`).
     * @details 결과(`AutomationResult`)는 종료 코드의 단위라 거칠다 — 같은 `Failed` 가 단언 · 골든 · 기준 없음을 함께 담았다. 종류가 결과를 정한다(`AutomationRunResult::failed`).
     *          리플렉션 열거형이다 — 끝 줄 · 보고 · 스크립트가 읽는 이름(`GoldenMismatch`)은 `getEnumName` 이 준다. 행 감시 스레드는 시작할 때 복사한 이름을 쓴다(`AutomationHangPayload::_failureKind`).
     */
    ENUM()
    enum class AutomationFailureKind : uint8
    {
        Assertion,       ///< 단언이 졌다(`<Expect>` · `<Fail/>` · 창이 일찍 닫힘 …)
        WaitTimeout,     ///< 대기가 시한까지 기다렸다(시작 조건 · `<WaitUntil>` · `timeoutFrames` · 벽시계)
        WaitStalled,     ///< 대기의 진척이 정지 시한 동안 그대로였다
        UnhandledLog,    ///< 처리하지 않은 Warning · Error 줄이 남았다
        GoldenMismatch,  ///< 그림이 골든과 다르다
        MissingBaseline, ///< 기준 파일(골든 · 스냅숏)이 없다
        LoadError,       ///< 읽기 · 검사 · 준비가 졌다(시작하지 않았다)
        Hang,            ///< 프레임이 멈췄다(행 감시)
        RepeatedFailure, ///< 같은 실패가 되풀이되어 체인을 멈췄다
    };

    /**
     * @class AutomationRunResult
     * @brief 끝난 시나리오 한 편의 판정 값 하나입니다 — 결과 · 실패 종류 · 까닭 한 줄 · 단계 자리. 실행기가 하나를 정하고, 끝 줄(`Automation.Finished` · `chain stage i/n`) ·
     *        체인 끝 표 · 부모의 백엔드 요약 · 보고(`failure`) · HUD 가 모두 이 값을 읽는다.
     * @details 만드는 길은 셋뿐이다(`passed` · `skipped` · `failed`) — 기본 생성자가 없어 결과와 까닭이 따로 놀 수 없다.
     *          예전에는 결과와 까닭이 따로 든 두 변수라, `<Pass/>` 로 끝맺은 뒤 실패로 바뀌면 까닭이 끝 단계 이름으로 남았다(`Failed X at frame 5: <Pass/>`).
     *          통과로 끝맺었는데 실패 줄이 있으면 `concludeWithFailures` 가 첫 실패 줄을 까닭으로 삼는다. 상용 엔진 비교: Gauntlet 의 `ExitReason` + 요약 머리.
     */
    class AutomationRunResult
    {
    public:
        [[nodiscard]] static AutomationRunResult passed() { return AutomationRunResult{ AutomationResult::Passed, AutomationFailureKind::Assertion, {}, {}, false }; }
        /** @param bByBuild 시나리오가 선언한 빌드 구성(없는 모듈 · 빠진 개발 명령)으로 건너뛰었다 — 환경 탓의 건너뜀과 달리 경고가 아니다 */
        [[nodiscard]] static AutomationRunResult skipped( string_view reason, bool bByBuild )
        {
            return AutomationRunResult{ AutomationResult::Skipped, AutomationFailureKind::Assertion, reason, {}, bByBuild };
        }
        /**
         * @brief 진 판정입니다. 결과는 종류가 정한다 — 읽기 오류는 `LoadError`, 처리하지 않은 로그는 `UnhandledLog`, 시한 · 정지 · 행은 `TimedOut`, 나머지는 `Failed`.
         * @param stepLocation 진 단계의 자리(`AutomationStep::describe`). 단계와 무관하면 빈 글
         */
        [[nodiscard]] static AutomationRunResult failed( AutomationFailureKind kind, string_view reason, string_view stepLocation )
        {
            return AutomationRunResult{ getResultOf( kind ), kind, reason, stepLocation, false };
        }

        /**
         * @brief 통과로 끝맺은 판정에 실패 줄이 있으면 첫 실패 줄(@p firstFailure, 종류 @p firstKind)을 까닭으로 한 실패로 바꿉니다. 실패 줄이 없거나 통과가 아니면 그대로입니다.
         * @details 끝 단계 이름(`<Pass/>`)이 실패의 까닭이 되는 길을 여기서 막는다.
         */
        [[nodiscard]] AutomationRunResult concludeWithFailures( string_view firstFailure, AutomationFailureKind firstKind ) const
        {
            if ( _result != AutomationResult::Passed || firstFailure.empty() )
                return *this;
            return failed( firstKind, firstFailure, {} );
        }

        /**
         * @brief 통과한 판정에 처리하지 않은 로그 줄(@p listUnhandled)이 남았으면 `UnhandledLog` 로 바꿉니다 — 까닭은 첫 줄과 남은 수(`… (+2)`)입니다. 줄이 없거나 통과가 아니면 그대로입니다.
         * @details 이미 진 판정은 바꾸지 않는다 — 원인은 단언 · 시한이고, 남은 줄은 보고의 `unhandledLogs` 에 있다.
         */
        [[nodiscard]] AutomationRunResult concludeWithUnhandledLogs( const vector<string>& listUnhandled ) const
        {
            if ( _result != AutomationResult::Passed || listUnhandled.empty() )
                return *this;
            StringBuilder<constant::buffer::kMax256> reason;
            reason.append( listUnhandled.front() );
            if ( listUnhandled.size() > 1 )
                reason.appendFormat( " (+%#)", listUnhandled.size() - 1 );
            return failed( AutomationFailureKind::UnhandledLog, reason.view(), {} );
        }

        /**
         * @brief 반복 실패 차단(`AutomationScenarioChain` — `ScenarioRun.json` 의 `_repeatedFailureLimit`)의 열쇠 @p key 를 붙인 사본입니다.
         * @details 대기의 포기(시한 · 정지)만 붙인다 — `<단계 종류> <감시 값 · 조건 이름>`(`WaitUntil Scene.Transitioning`, `Screenshot RenderTemporalAccumulation`). 종류는 열쇠에 따로 든다.
         *          같은 대기가 단계마다 시한까지 기다리며 지는 체인(실패 1 — 사진마다 수천 프레임)을 몇 번 만에 멈추게 한다.
         */
        [[nodiscard]] AutomationRunResult withRepeatKey( string_view key ) const
        {
            AutomationRunResult copy = *this;
            copy._repeatKey          = string( key );
            return copy;
        }

        AutomationResult getResult() const { return _result; }
        /** @brief 진 판정이면 true 입니다(통과 · 건너뜀이 아니다). 종류(`getFailureKind`)는 이때만 뜻이 있다. */
        bool                  isFailure() const { return _result != AutomationResult::Passed && _result != AutomationResult::Skipped; }
        AutomationFailureKind getFailureKind() const { return _kind; }
        /** @brief 까닭 한 줄입니다(통과면 빈 글). */
        const string& getReason() const { return _reason; }
        /** @brief 진 단계의 자리입니다(모르면 빈 글). */
        const string& getStepLocation() const { return _stepLocation; }
        /** @brief 반복 실패 차단의 열쇠입니다(대기의 포기가 아니면 빈 글 — `withRepeatKey`). */
        const string& getRepeatKey() const { return _repeatKey; }
        bool          isSkippedByBuild() const { return _bSkippedByBuild == 1; }
        /** @brief 끝 줄에 붙이는 한 줄입니다 — 진 판정은 `<종류> - <까닭>`, 건너뜀은 까닭, 통과는 빈 글. 줄바꿈은 공백으로 바꾼다(끝 줄이 한 줄이게). */
        string describe() const
        {
            string text = _reason;
            if ( isFailure() )
            {
                StringBuilder<constant::buffer::kMax256> failureText;
                failureText.appendFormat( "%# - %#", getEnumName( _kind ), _reason );
                text = string( failureText.view() );
            }
            for ( utf8& character : text )
            {
                if ( character == '\n' || character == '\r' )
                    character = ' ';
            }
            return text;
        }

    private:
        AutomationRunResult( AutomationResult result, AutomationFailureKind kind, string_view reason, string_view stepLocation, bool bSkippedByBuild )
            : _result{ result }
            , _kind{ kind }
            , _bSkippedByBuild{ static_cast<uint8>( bSkippedByBuild ? 1 : 0 ) }
            , _reserved{ 0 }
            , _reason{ reason }
            , _stepLocation{ stepLocation }
            , _repeatKey{}
        {
        }

        /** @brief 종류 @p kind 가 정하는 결과입니다(`failed`). */
        static constexpr AutomationResult getResultOf( AutomationFailureKind kind )
        {
            switch ( kind )
            {
                case AutomationFailureKind::LoadError:
                    return AutomationResult::LoadError;
                case AutomationFailureKind::UnhandledLog:
                    return AutomationResult::UnhandledLog;
                case AutomationFailureKind::WaitTimeout:
                case AutomationFailureKind::WaitStalled:
                case AutomationFailureKind::Hang:
                    return AutomationResult::TimedOut;
                case AutomationFailureKind::Assertion:
                case AutomationFailureKind::GoldenMismatch:
                case AutomationFailureKind::MissingBaseline:
                case AutomationFailureKind::RepeatedFailure:
                    return AutomationResult::Failed;
            }
            return AutomationResult::Failed;
        }

    private:
        AutomationResult       _result;
        AutomationFailureKind  _kind;                ///< 진 판정의 종류(통과 · 건너뜀이면 뜻이 없다)
        uint8                  _bSkippedByBuild : 1; ///< 시나리오가 선언한 빌드 구성으로 건너뛰었다(`skipped` 가 정한다)
        [[maybe_unused]] uint8 _reserved        : 7;
        string                 _reason;
        string                 _stepLocation;
        string                 _repeatKey; ///< 반복 실패 차단의 열쇠(`withRepeatKey`)
    };
} // namespace sw
