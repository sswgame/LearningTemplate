/**
 * @file AutomationOutcome.h
 * @brief 시나리오 한 편이 끝난 까닭(`AutomationOutcome`)을 정의합니다. 숫자 값은 없습니다 — 프로세스 종료 코드로의 번역은 `AutomationExitCode.h` 가 맡습니다.
 */
#pragma once
#include "Core/Foundation/Types.h"

namespace sw
{
    /**
     * @brief 시나리오가 끝난 까닭입니다. 실행기(`AutomationRunner`)가 돌려주고 체인 · 세션이 읽습니다.
     * @details 정수 값을 일부러 두지 않았다 — 종료 코드는 App 이 밖으로 내는 약속이라 이 열거형이 곧 코드가 되면 실행기의 결과 형식이 종료 코드 표에 묶인다.
     */
    enum class AutomationOutcome : uint8
    {
        Running,
        Passed,
        Failed,
        LoadError, ///< 파일, 형식, 모르는 단계, 모르는 속성, 모르는 감시 값이 있습니다. 조용히 버리지 않습니다.
        TimedOut,
        Skipped,      ///< 이 기계에서 돌 수 없습니다(전경 창을 얻지 못한 경우 등). CTest 는 건너뜀으로 봅니다.
        UnhandledLog, ///< 단언은 모두 맞았지만 허용 목록에도 `<ExpectLog>` 에도 없는 `[Warning]` · `[Error]` 줄이 남았습니다(id 없는 줄 포함). 통과가 아닙니다.
    };

    /** @brief 결과 이름(`PASS`, `FAIL`, `LOAD ERROR`, `TIMEOUT`, `SKIP`, `UNHANDLED LOG`)입니다. 요약 줄과 보고에 적습니다. */
    constexpr const utf8* getAutomationOutcomeName( AutomationOutcome outcome )
    {
        switch ( outcome )
        {
            case AutomationOutcome::Running:
                return "RUNNING";
            case AutomationOutcome::Passed:
                return "PASS";
            case AutomationOutcome::Failed:
                return "FAIL";
            case AutomationOutcome::LoadError:
                return "LOAD ERROR";
            case AutomationOutcome::TimedOut:
                return "TIMEOUT";
            case AutomationOutcome::Skipped:
                return "SKIP";
            case AutomationOutcome::UnhandledLog:
                return "UNHANDLED LOG";
        }
        return "UNKNOWN";
    }
} // namespace sw
