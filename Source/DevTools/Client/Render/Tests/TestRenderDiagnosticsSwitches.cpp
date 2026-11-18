#include "pch.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Testing/TestFramework.h"
#include "Core/Testing/ThreadTestAccess.h"

#include "Engine/Graphics/Debug/RenderDiagnostics.h"

// 렌더 진단 정책의 스레드 모델 — 개발 변수는 게임 스레드의 `publish` 가 떠 원자 사본으로 내보내고, 다른 스레드는 사본만 읽는다.

namespace sw
{
    SW_EXTERN_TEST_GLOBAL_VARIABLE( int32, gv_fogDebugSlice ); // `RenderDiagnosticsSwitches.cpp` 가 정의한다
} // namespace sw

using namespace sw;

/**
 * @brief [RenderDiagnosticsTest] 개발 변수를 바꿔도 `publish` 전에는 사본이 그대로고, `publish` 뒤에는 게임 스레드와 다른 스레드가 같은 새 값을 읽는다
 */
SW_TEST_CASE( RenderDiagnosticsTest, OtherThreadsReadOnlyThePublishedCopy )
{
    const int32 previous = gv_fogDebugSlice;
    RenderDiagnostics::publish();
    const uint8 before = RenderDiagnostics::get()._fogDebugSlice;
    const uint8 next   = before == 3 ? uint8{ 4 } : uint8{ 3 };

    gv_fogDebugSlice = next;
    SW_EXPECT_EQUAL( before, RenderDiagnostics::get()._fogDebugSlice ); // 내보내기 전 — 렌더 쪽은 지난 사본을 본다

    RenderDiagnostics::publish();
    SW_EXPECT_EQUAL( next, RenderDiagnostics::get()._fogDebugSlice );

    uint8  seenOnOtherThread = 0;
    thread other( ThreadTestAccess::getKey(), [&seenOnOtherThread]()
    {
        seenOnOtherThread = RenderDiagnostics::get()._fogDebugSlice;
    } );
    other.join();
    SW_EXPECT_EQUAL( next, seenOnOtherThread );

    gv_fogDebugSlice = previous;
    RenderDiagnostics::publish();
}
