/**
 * @file AutomationExitCode.h
 * @brief 시나리오 결과(`AutomationResult`)를 App 프로세스 종료 코드로 번역합니다. 종료 코드 표의 유일한 정의 자리입니다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Log/Logger.h"

#include "Engine/Automation/Runner/AutomationResult.h"
#include "Engine/EngineFrameHooks.h"

#include "sw/config/ProcessExitCodes.gen.h"

namespace sw
{
    /**
     * @brief 결과의 프로세스 종료 코드입니다(아직 도는 중이면 `kEngineFrameRunning` — 빈 값). 숫자는 종료 코드 표(`Config/Engine/ProcessExitCodes.json` → `ProcessExitCode`)에만 있다 —
     *        백엔드마다 실행(`AutomationPerBackend`)과 CTest(`SW_EXIT_CODE_Skipped`)도 같은 표를 읽는다.
     */
    constexpr EngineFrameExit getAutomationExitCode( AutomationResult result )
    {
        ProcessExitCode exitCode = ProcessExitCode::Failed;
        switch ( result )
        {
            case AutomationResult::Running:
                return kEngineFrameRunning;
            case AutomationResult::Passed:
                exitCode = ProcessExitCode::Passed;
                break;
            case AutomationResult::Failed:
                exitCode = ProcessExitCode::Failed;
                break;
            case AutomationResult::LoadError:
                exitCode = ProcessExitCode::LoadError;
                break;
            case AutomationResult::TimedOut:
                exitCode = ProcessExitCode::TimedOut;
                break;
            case AutomationResult::Skipped:
                exitCode = ProcessExitCode::Skipped;
                break;
            case AutomationResult::UnhandledLog:
                exitCode = ProcessExitCode::UnhandledLog;
                break;
        }
        return exitCode;
    }

    /** @brief 끝난 결과 @p result 의 종료 상태(프로세스가 내는 정수)입니다. 자식 프로세스의 종료 코드와 견주는 자리(`AutomationPerBackend`)와 보고가 쓴다. 도는 중(`Running`)은 끝난 결과가 아니라 멈춘다. */
    inline int32 getAutomationExitStatus( AutomationResult result )
    {
        const EngineFrameExit exitCode = getAutomationExitCode( result );
        if ( exitCode.has_value() == false )
            SW_FATAL( "getAutomationExitStatus: a running scenario has no exit status" );
        return static_cast<int32>( *exitCode );
    }
} // namespace sw
