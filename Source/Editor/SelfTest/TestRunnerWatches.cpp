/**
 * @file TestRunnerWatchs.cpp
 * @brief Test Runner 의 자동화 감시 값 `Editor.TestRunnerPassCount` 입니다.
 * @details 감시 값 레지스트리(`Engine/Automation/Watch`)가 기능 층보다 아래라 기능이 자기 감시 값을 등록합니다. 등록은 이 .cpp 의 정적 등록자가 하며 대상 id 를 정하는 시나리오용 전역 변수는
 *          `EditorWatchSupport.h` 에 있습니다.
 */
#include "pch.h"

#include "Editor/SelfTest/EditorSelfTest.h"

#include "Engine/Automation/Watch/AutomationWatch.h"

namespace sw::editor
{
    namespace
    {
        /** @brief 마지막(또는 현재) 자체 테스트 실행에서 통과한 테스트 수입니다. Test Runner 창과 명령줄 실행 모두 해당합니다. */
        [[nodiscard]] bool readTestRunnerPassCount( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            uint32 passedCount{ 0 };
            for ( const EditorSelfTestResult& result : EditorSelfTestRunner::getResults() )
            {
                if ( result._bPassed )
                    ++passedCount;
            }
            outValue = static_cast<float64>( passedCount );
            return true;
        }
    } // namespace

    SW_AUTOMATION_WATCH( editorTestRunnerPassCount, "Editor.TestRunnerPassCount", "Editor self tests that passed in the last (or current) self test run",
                         &readTestRunnerPassCount );
} // namespace sw::editor
