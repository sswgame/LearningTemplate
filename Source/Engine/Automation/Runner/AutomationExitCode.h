/**
 * @file AutomationExitCode.h
 * @brief 시나리오 결과(`AutomationOutcome`)를 App 프로세스 종료 코드로 번역합니다. 종료 코드 표의 유일한 정의 자리입니다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Automation/Runner/AutomationOutcome.h"
#include "Engine/EngineFrameHooks.h"

#include "sw/config/ProcessExitCodes.gen.h"

namespace sw
{
    /** @brief `AutomationSession::onFrameEnd` 가 아직 끝나지 않았음을 알리는 값입니다(`kEngineFrameRunning` 그 자체 — 값을 따로 쓰지 않는다). */
    inline constexpr int32 kAutomationExitCodeRunning = kEngineFrameRunning;

    /**
     * @brief 결과의 프로세스 종료 코드입니다. 숫자는 종료 코드 표(`Config/Engine/ProcessExitCodes.json` → `ProcessExitCode`)에만 있다 —
     *        행렬 실행(`AutomationBackendMatrix`)과 CTest(`SW_EXIT_CODE_Skipped`)도 같은 표를 읽는다.
     */
    constexpr int32 getAutomationExitCode( AutomationOutcome outcome )
    {
        ProcessExitCode exitCode = ProcessExitCode::Failed; // 열거형에 없는 값은 실패로 센다
        switch ( outcome )
        {
            case AutomationOutcome::Running:
                return kAutomationExitCodeRunning;
            case AutomationOutcome::Passed:
                exitCode = ProcessExitCode::Passed;
                break;
            case AutomationOutcome::Failed:
                exitCode = ProcessExitCode::Failed;
                break;
            case AutomationOutcome::LoadError:
                exitCode = ProcessExitCode::LoadError;
                break;
            case AutomationOutcome::TimedOut:
                exitCode = ProcessExitCode::TimedOut;
                break;
            case AutomationOutcome::Skipped:
                exitCode = ProcessExitCode::Skipped;
                break;
            case AutomationOutcome::UnhandledLog:
                exitCode = ProcessExitCode::UnhandledLog;
                break;
        }
        return static_cast<int32>( exitCode );
    }
} // namespace sw
