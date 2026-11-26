#include "pch.h"

#include "Core/Log/Logger.h"
#include "Core/Log/ScopedDeferredLog.h"
#include "Core/TestFramework/TestFramework.h"

#include "DevTools/Client/Automation/Runner/AutomationLogCapture.h"

using namespace sw;

// 줄의 프레임은 받은 순간이 아니라 남긴 순간의 것이다 — 미룬 줄이 다음 프레임의 `<ExpectLog>` 를 맞추지 않는다.
SW_TEST_CASE( AutomationLogCaptureTest, DeferredLineCountsForTheFrameItWasLoggedIn )
{
    AutomationLogCapture capture;
    capture.start();
    capture.setFrameIndex( 1 );
    {
        const ScopedDeferredLog deferredLog;
        SW_LOG_INFO_ID( "Test.AutomationDeferredFrame", "deferred" );
        capture.setFrameIndex( 2 );
    }
    SW_LOG_INFO_ID( "Test.AutomationDeferredFrame", "direct" );
    capture.stop();

    SW_EXPECT_EQUAL( 2u, capture.countLinesWithID( "Test.AutomationDeferredFrame", "", 1 ) );
    SW_EXPECT_EQUAL( 1u, capture.countLinesWithID( "Test.AutomationDeferredFrame", "", 2 ) );
    SW_EXPECT_EQUAL( 1u, capture.countLinesWithID( "Test.AutomationDeferredFrame", "direct", 2 ) );
}
