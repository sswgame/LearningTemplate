#include "pch.h"

#include "DevTools/Profiling/LeakBaselineDetailSwitch.h"

#include "Core/Diagnostics/MemoryProfiler.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"

namespace sw
{
    /**
     * @brief `-gv_leakWindowFullStacks`: 기준선 창을 `FullStacks` 로 엽니다(`MemoryProfiler::captureMemoryLeakBaseline`). 종료 누수 보고가 줄마다 첫 프레임(누가 할당했나)을 적습니다.
     * @details 창 안의 모든 할당이 스택을 잡아 Debug 실행이 느려진다 — 누수 출처를 찾을 때만 켠다. 꺼 두면 `TagTotals`(태그별 바이트 · 개수만)다.
     */
    SW_TEST_GLOBAL_VARIABLE( bool, gv_leakWindowFullStacks, false, "기동 뒤 누수 기준선 창을 FullStacks 로 연다 - 종료 보고가 줄마다 할당한 코드의 첫 프레임을 적는다(Debug 가 느려지므로 누수 출처를 찾을 때만)" );

    namespace
    {
        struct LeakBaselineDetailSwitchInternal
        {
            /** @brief 기준선 질의의 답입니다 — 스위치를 그 자리에서 읽는다. */
            static LeakWindowDetail queryBaselineDetail() { return gv_leakWindowFullStacks ? LeakWindowDetail::FullStacks : LeakWindowDetail::TagTotals; }
        };
    } // namespace

    void LeakBaselineDetailSwitch::install()
    {
        MemoryProfiler::setLeakBaselineDetailQuery( &LeakBaselineDetailSwitchInternal::queryBaselineDetail );
    }

    void LeakBaselineDetailSwitch::uninstall()
    {
        MemoryProfiler::setLeakBaselineDetailQuery( nullptr );
    }
} // namespace sw
