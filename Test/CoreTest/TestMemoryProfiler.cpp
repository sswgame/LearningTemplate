#include "pch.h"

#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Memory/MemoryProfiler.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

// ------------------------------------------------------------------------------
// 1) MemoryProfilerTest — 할당·해제 바이트
// ------------------------------------------------------------------------------
/**
 * @brief [MemoryProfilerTest] 할당·해제 바이트 추적
 */
SW_TEST_CASE( MemoryProfilerTest, BasicTracking )
{
    MemoryProfiler profiler;
    profiler.initialize();
    profiler.setTrackingEnabled( true );
    profiler.setDetailedTrackingEnabled( true );

    const auto& beforeStats  = profiler.getStats( MemoryTag::Game );
    uint64      initialBytes = beforeStats._currentAllocatedBytes.load();

    void*  dummyPtr = reinterpret_cast<void*>( 0x12345678 );
    uint64 hash     = profiler.recordAllocation( dummyPtr, 1024, MemoryTag::Game );

    const auto& afterStats = profiler.getStats( MemoryTag::Game );
    uint64      afterBytes = afterStats._currentAllocatedBytes.load();

    SW_EXPECT_TRUE( afterBytes > initialBytes );

    auto topStacks = profiler.getTopCallStacks();
    SW_EXPECT_TRUE( !topStacks.empty() );

    profiler.recordFree( dummyPtr, 1024, MemoryTag::Game, hash );

    const auto& finalStats = profiler.getStats( MemoryTag::Game );
    SW_EXPECT_EQUAL( initialBytes, finalStats._currentAllocatedBytes.load() );

    profiler.shutdown();
}

/**
 * @brief [MemoryProfilerTest] 세지 않은 해제가 카운터를 0 아래로 접지 않는지 검증
 * @details 두 카운터(`_currentAllocatedBytes` · `_currentAllocationCount`)는 `uint64` 다.
 *          그냥 `fetch_sub` 하면 0 아래가 **1.8e19** 로 접힌다. 할당은 세지 않았는데 해제만
 *          세는 경우가 실제로 있다 — 에디터의 프로파일러 패널에 **추적 켜기 체크박스**가
 *          있어서, 켜기 전에 잡힌 블록들이 켠 뒤에 풀리면 정확히 그 일이 난다.
 *
 *          같은 함수 안의 콜스택 표는 처음부터 `>= size` 로 막고 있었다 — 위쪽만 빠져 있었다.
 */
SW_TEST_CASE( MemoryProfilerTest, FreeWithoutMatchingAllocationDoesNotWrap )
{
    MemoryProfiler profiler;
    profiler.initialize();

    // 추적을 **끈 채** 할당한다 — 카운터는 올라가지 않는다.
    profiler.setTrackingEnabled( false );
    void* pDummy = reinterpret_cast<void*>( 0x1234'5678 );
    profiler.recordAllocation( pDummy, 4096, MemoryTag::Game );

    const uint64 beforeBytes = profiler.getStats( MemoryTag::Game )._currentAllocatedBytes.load();
    const uint64 beforeCount = profiler.getStats( MemoryTag::Game )._currentAllocationCount.load();

    // 이제 켜고 해제한다 — 세지 않은 것을 빼게 된다.
    profiler.setTrackingEnabled( true );
    profiler.recordFree( pDummy, 4096, MemoryTag::Game, 0 );

    const uint64 afterBytes = profiler.getStats( MemoryTag::Game )._currentAllocatedBytes.load();
    const uint64 afterCount = profiler.getStats( MemoryTag::Game )._currentAllocationCount.load();

    // 고치기 전에는 여기가 1.8e19 였다.
    SW_EXPECT_TRUE( afterBytes <= beforeBytes );
    SW_EXPECT_TRUE( afterCount <= beforeCount );

    profiler.shutdown();
}

/**
 * @brief [MemoryProfilerTest] 할당 횟수 누계는 해제해도 줄지 않는다 — churn 을 세는 근거
 * @details 살아 있는 양은 잡았다 놓은 것을 못 본다. 같은 자리에서 세 번 잡았다 놓으면 누계는 3, 살아 있는 것은 0 이고,
 *          횟수 순 표에는 그 자리가 남지만 살아 있는 바이트 순 표에는 없다.
 */
SW_TEST_CASE( MemoryProfilerTest, TotalAllocationCountSurvivesFrees )
{
    MemoryProfiler profiler;
    profiler.initialize();
    profiler.setTrackingEnabled( true );
    profiler.setDetailedTrackingEnabled( true );

    const uint64 totalBefore = profiler.getTotalAllocationCount();
    void*        pDummy      = reinterpret_cast<void*>( 0x2468'ACE0 );
    for ( uint32 round = 0; round < 3; ++round )
    {
        const uint64 hash = profiler.recordAllocation( pDummy, 256, MemoryTag::Core );
        profiler.recordFree( pDummy, 256, MemoryTag::Core, hash );
    }

    SW_EXPECT_EQUAL( uint64( 3 ), profiler.getTotalAllocationCount() - totalBefore );
    SW_EXPECT_EQUAL( uint64( 0 ), profiler.getStats( MemoryTag::Core )._currentAllocationCount.load() );

    const vector<CallStackAllocInfo> listByChurn = profiler.getTopCallStacks( TopCallStackOrder::TotalCount );
    SW_ASSERT_FALSE( listByChurn.empty() );
    SW_EXPECT_EQUAL( uint64( 3 ), listByChurn.front()._totalCount );
    SW_EXPECT_EQUAL( uint64( 0 ), listByChurn.front()._currentCount );
    SW_EXPECT_TRUE( profiler.getTopCallStacks( TopCallStackOrder::LiveBytes ).empty() );

    profiler.shutdown();
}
