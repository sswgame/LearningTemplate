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
