// RealtimeSectionTest — 실시간 구간 안의 할당 · 해제 · 잠금만 센다(검사 구성). 구간 밖과 구간을 뗀 뒤는 세지 않는다.
#include "pch.h"

#include "Core/Concurrency/RealtimeSection.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Memory/Memory.h"
#include "Core/Testing/TestFramework.h"

using namespace sw;

/**
 * @brief [RealtimeSectionTest] 구간 안의 힙 할당 · 해제와 잠금 획득을 그 칸에 센다 — 구간 밖은 칸이 그대로다
 * @details 변이: `Memory` 의 `recordAllocation` 을 빼면 할당 단언이, `mutex::changeHeldLockDepth` 의 `recordLock` 을 빼면 잠금 단언이 진다.
 */
SW_TEST_CASE( RealtimeSectionTest, CountsHeapAndLocksOnlyInsideTheSection )
{
    RealtimeViolationCounters counters;
    mutex                     lockForTest;
    {
        unique_ptr<int32> pOutside = make_unique<int32>( 1 );
        ScopedLock<mutex> outsideLock{ lockForTest };
    }
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( counters._allocationCount.get() ) );
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( counters._lockCount.get() ) );

    {
        const RealtimeSection section{ counters };
        {
            unique_ptr<int32> pInside = make_unique<int32>( 2 );
        }
        ScopedLock<mutex> insideLock{ lockForTest };
    }
    SW_EXPECT_TRUE( counters._allocationCount.get() >= 1u );
    SW_EXPECT_TRUE( counters._freeCount.get() >= 1u );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( counters._lockCount.get() ) );

    const uint64 allocationsAfter = counters._allocationCount.get();
    {
        unique_ptr<int32> pAfter = make_unique<int32>( 3 );
    }
    SW_EXPECT_EQUAL( static_cast<uint32>( allocationsAfter ), static_cast<uint32>( counters._allocationCount.get() ) );
}
