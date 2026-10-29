/**
 * @file AutomationExitCode.h
 * @brief 시나리오 결과(`AutomationOutcome`)를 App 프로세스 종료 코드로 번역합니다. 종료 코드 표의 유일한 정의 자리입니다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Automation/Runner/AutomationOutcome.h"
#include "Engine/EngineFrameHooks.h"

namespace sw
{
    /** @brief `AutomationSession::onFrameEnd` 가 아직 끝나지 않았음을 알리는 값입니다(`kEngineFrameRunning` 그 자체 — 값을 따로 쓰지 않는다). */
    inline constexpr int32 kAutomationExitCodeRunning = kEngineFrameRunning;

    /**
     * @brief 결과의 프로세스 종료 코드입니다. 0 통과, 10 실패, 11 읽기 오류, 12 시간 초과, 13 건너뜀, 15 처리하지 않은 로그(14 는 없앤 "미확정"이라 비워 둔다).
     * @details 숫자는 이 함수 안에만 쓴다. 행렬 실행(`AutomationBackendMatrix`)과 CTest(`Test/Scenario/CMakeLists.txt` 의 SKIP_RETURN_CODE 13)가 이 값을 읽는다.
     */
    constexpr int32 getAutomationExitCode( AutomationOutcome outcome )
    {
        switch ( outcome )
        {
            case AutomationOutcome::Running:
                return kAutomationExitCodeRunning;
            case AutomationOutcome::Passed:
                return 0;
            case AutomationOutcome::Failed:
                return 10;
            case AutomationOutcome::LoadError:
                return 11;
            case AutomationOutcome::TimedOut:
                return 12;
            case AutomationOutcome::Skipped:
                return 13;
            case AutomationOutcome::UnhandledLog:
                return 15;
        }
        return 10; // 열거형에 없는 값은 실패로 센다
    }
} // namespace sw
