/**
 * @file TestMemoryCopyBench.cpp
 * @brief `Memory::copy` · `Memory::compare` 한 번의 비용 — 8 · 16 · 64 · 256 · 4096 바이트.
 * @details 숫자를 **찍기만** 한다. Release 로 읽는다. 구조 최적화 B ⑤(헤더 inline — DLL 경계를 넘는 호출을 없애 작은 크기를 내장 복사로)의 전/후를
 *          측정한다(표는 `docs/plans/StructuralOptimization.md`).
 */
#include "pch.h"

#include "Core/Memory/Memory.h"

#include "TestFramework/Harness/TestBench.h"
#include "TestFramework/TestFramework.h"

SW_TEST_LOAD_SUITE( MemoryCopyBenchTest, "Memory::copy and Memory::compare per call at 8 to 4096 bytes" );
SW_TEST_TIME_BUDGET( MemoryCopyBenchTest, 2000, "측정 전 추정 — 첫 Release 실행 뒤 실제 최대의 네 배로 고친다" );

SW_LOG_CALLER( "MemoryCopyBench" );

namespace
{
    struct MemoryCopyBenchInternal
    {
        static constexpr uint64 kCallCount  = 1000000;
        static constexpr uint32 kRoundCount = 5;
        static constexpr size_t kArrSize[]  = { 8, 16, 64, 256, 4096 };
        static constexpr size_t kMaxBytes   = 4096;
    };
} // namespace

/**
 * @brief [MemoryCopyBenchTest] 크기마다 copy · compare 한 번의 ns
 */
SW_TEST_CASE( MemoryCopyBenchTest, CopyAndCompareBySize )
{
    alignas( 16 ) static uint8 s_arrSource[MemoryCopyBenchInternal::kMaxBytes]{};
    alignas( 16 ) static uint8 s_arrDest[MemoryCopyBenchInternal::kMaxBytes]{};
    for ( size_t index = 0; index < MemoryCopyBenchInternal::kMaxBytes; ++index )
    {
        s_arrSource[index] = static_cast<uint8>( index * 31u );
    }

    int64 differenceSum = 0;
    for ( const size_t size : MemoryCopyBenchInternal::kArrSize )
    {
        [[maybe_unused]] const int64 copyDeciNanos    = test::measureBestDeciNanosPerOp( MemoryCopyBenchInternal::kCallCount, MemoryCopyBenchInternal::kRoundCount, [size]()
           {
            for ( uint64 call = 0; call < MemoryCopyBenchInternal::kCallCount; ++call )
            {
                s_arrSource[call & 7] = static_cast<uint8>( call ); // 컴파일러가 반복을 하나로 접지 못하게 원본을 바꾼다
                sw::Memory::copy( s_arrDest, s_arrSource, size );
            }
        } );
        [[maybe_unused]] const int64 compareDeciNanos = test::measureBestDeciNanosPerOp( MemoryCopyBenchInternal::kCallCount, MemoryCopyBenchInternal::kRoundCount, [size, &differenceSum]()
        {
            for ( uint64 call = 0; call < MemoryCopyBenchInternal::kCallCount; ++call )
            {
                s_arrDest[size - 1] = static_cast<uint8>( call );
                differenceSum += sw::Memory::compare( s_arrDest, s_arrSource, size );
            }
        } );
        SW_LOG_INFO( "[Bench] Memory::copy %# B: %#.%# ns, Memory::compare %# B: %#.%# ns", size, copyDeciNanos / 10, copyDeciNanos % 10, size,
                     compareDeciNanos / 10, compareDeciNanos % 10 );
    }
    SW_EXPECT_TRUE( differenceSum != 0 ); // 마지막 바이트를 바꿔 가며 비교하므로 다른 판이 반드시 있다
}
