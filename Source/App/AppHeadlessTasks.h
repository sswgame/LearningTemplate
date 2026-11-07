/**
 * @file AppHeadlessTasks.h
 * @brief App 이 맡는 헤드리스 작업 표입니다. 엔진의 `Headless` 단계가 하지 못하는 일(에디터 모듈 작업, 크래시 보고)만 여기에 둡니다.
 */
#pragma once
#include "Core/Foundation/Types.h"

namespace sw
{
    class CommandLineManager;
    class Engine;

    /**
     * @struct AppHeadlessTasks
     * @brief 창과 RHI 없이 시작한 App 이 인자에 따라 차례로 실행하는 작업들입니다. 새 작업은 인자를 `ArgumentList.xxx` 에 더하고 `AppHeadlessTasks.cpp` 의 표에 한 줄을 더합니다.
     * @note 에디터 모듈 작업(로컬라이제이션, 임포트)의 인자는 엔진의 `Headless` 단계도 알아야 합니다. 창과 RHI 없이 시작한 뒤 여기로 넘깁니다.
     * @details 종료 코드 규칙은 다음과 같습니다. 실패한 작업이 하나라도 있으면 `App::initialize` 가 false 를 반환하고(종료 코드가 0 이 아님),
     *          `Finished` 를 반환한 작업은 뒤의 항목을 보지 않고 성공으로 끝냅니다.
     */
    struct AppHeadlessTasks
    {
        /** @brief 작업 하나의 결과입니다. */
        enum class TaskResult : uint8
        {
            NotRequested, ///< 인자가 없어 실행하지 않았다.
            Succeeded,    ///< 실행했고 성공했다. 다음 항목으로 넘어간다.
            Failed,       ///< 실행했고 실패했다. 다음 항목도 실행하고, 마지막에 실패로 반환한다.
            Finished,     ///< 이 작업만 하고 끝낸다(성공). 뒤의 항목은 보지 않는다.
        };
        using TaskFunction = TaskResult ( * )( const CommandLineManager& commandLine );
        /** @brief 호스트 서비스가 선 뒤에 도는 작업입니다. 게임 타입 · 렌더 스레드가 있어 엔진을 받습니다. */
        using AfterHostServicesTask = TaskResult ( * )( const CommandLineManager& commandLine, Engine& hostEngine );

        /** @brief App 의 작업 표를 차례로 실행합니다. 실패한 작업이 하나라도 있으면 false 를 반환합니다(명령줄이 없으면 아무것도 실행하지 않고 true). */
        [[nodiscard]] static bool runAll( const CommandLineManager* pCommandLine );

        /**
         * @brief 호스트 서비스(게임 · 에디터 인스턴스)가 선 뒤 한 번 하고 끝나는 작업의 표를 위에서부터 봅니다. 처음으로 실행한 작업의 결과를 돌려주고 끝냅니다.
         * @details 창과 RHI 없이 시작하는 위 표와 달리 이 작업들은 일반 기동(창 · 렌더 스레드 · 모듈)을 모두 거친 뒤에 돈다 — `--render-portraits` 는 게임 컴포넌트 타입과 디바이스가 있어야 그린다.
         * @return 실행한 작업이 없으면 `NotRequested` 입니다(기동을 이어 간다). 그 밖이면 App 은 메인 루프에 들어가지 않고 끝낸다.
         */
        [[nodiscard]] static TaskResult runAfterHostServices( const CommandLineManager& commandLine, Engine& hostEngine );

        /**
         * @brief 작업 @p pArrTask 를 위에서부터 차례로 실행합니다. 결과 규칙은 `TaskResult` 와 같습니다.
         * @details 표와 떼어 둔 것은 규칙(실패해도 계속, `Finished` 에서 멈춤)을 모듈 없이 테스트하기 위해서입니다(`App/Tests/TestAppHeadlessTasks.cpp`).
         * @return 실패한 작업이 없으면 true 를 반환합니다.
         */
        [[nodiscard]] static bool runTable( const TaskFunction* pArrTask, uint32 taskCount, const CommandLineManager& commandLine )
        {
            bool bSucceeded = true;
            for ( uint32 taskIndex = 0; taskIndex < taskCount; ++taskIndex )
            {
                const TaskResult result = pArrTask[taskIndex]( commandLine );
                if ( result == TaskResult::Finished )
                    break;
                if ( result == TaskResult::Failed )
                    bSucceeded = false;
            }
            return bSucceeded;
        }
    };
} // namespace sw
