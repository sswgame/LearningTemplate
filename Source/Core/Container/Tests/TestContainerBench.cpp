// ContainerBenchTest — std 와 이름이 같은데 복잡도가 다를 수 있는 컨테이너 넷(vector 범위 삽입 · unordered_map reserve · map 범위 구축 · deque)과 deque 키우기(자체 컨테이너 최적화 검토 2026-11-20)를 잰다(감사 C-9, 설계 AC-1).
#include "pch.h"

#include "Core/Container/deque.h"
#include "Core/Container/map.h"
#include "Core/Container/pair.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"
#include "Core/Testing/ScopedAllocationCount.h"
#include "Core/Testing/TestFramework.h"
#include "Core/Time/MonotonicClock.h"

#include <algorithm>

/**
 * @file TestContainerBench.cpp
 * @brief 컨테이너 벤치 넷입니다. 시간 문턱은 두지 않고 값을 로그(`ContainerBenchTest.*`)로 남긴다 — 판정은 통합이 설계 문턱(DesignBacklog "출하 할당기와 컨테이너 복잡도")으로 한다.
 * @details 재는 법: Release `App --test --test-run-disabled --test-filter=ContainerBenchTest.*` 5 번, 로그 값의 중앙값.
 *          할당 수는 할당 관찰자(`Memory::registerAllocationObserver`)로 이 스레드의 할당만 센다 — 배포본에는 관찰자가 없어 0 이다(Release 로 잰다).
 *          케이스는 무거워 평소 실행에서 끈다(`map` 범위 구축은 AC-2 로 붙이고 정렬 · 병합하는 O(n log n), 고치기 전은 O(n²)).
 */
namespace sw
{
    namespace
    {
        /** @brief 결정적 난수(xorshift64)입니다. */
        uint64 nextRandom( uint64& inoutState )
        {
            inoutState ^= inoutState << 13;
            inoutState ^= inoutState >> 7;
            inoutState ^= inoutState << 17;
            return inoutState;
        }

        /** @brief deque 벤치의 64 B 원소입니다. */
        struct Element64Bytes
        {
            uint64 _arrValue[8]{};
        };
        static_assert( sizeof( Element64Bytes ) == 64 );
    } // namespace
} // namespace sw

using namespace sw;

/**
 * @brief [ContainerBenchTest] 1 MB 를 4 KB 꼬리 앞에 범위 삽입하는 시간과 `reserve` + `memcpy` 기준의 비입니다.
 * @details 문턱: 기준의 2 배 이상이면 AC-2(전방 반복자는 한 번 키우고 꼬리를 한 번 민다)로 고친다.
 */
SW_TEST_CASE( ContainerBenchTest, VectorRangeInsert, ._tags = "bench", ._disabledReason = "bench - Release 에서 --test-run-disabled 로 돌린다" )
{
    static constexpr size_t kInsertBytes = 1024 * 1024;
    static constexpr size_t kTailBytes   = 4096;
    vector<uint8>           insertBytes( kInsertBytes );
    for ( size_t index = 0; index < kInsertBytes; ++index )
    {
        insertBytes[index] = static_cast<uint8>( index * 31u );
    }
    vector<uint8> tail( kTailBytes, uint8{ 0xAB } );

    vector<uint8>   target = tail;
    const Stopwatch insertWatch;
    target.insert( target.begin(), insertBytes.begin(), insertBytes.end() );
    const int64 insertNanos = insertWatch.getElapsedNanoseconds();

    vector<uint8>   reference = tail;
    const Stopwatch referenceWatch;
    reference.reserve( kInsertBytes + kTailBytes );
    reference.resize( kInsertBytes + kTailBytes );
    Memory::move( reference.data() + kInsertBytes, reference.data(), kTailBytes );
    Memory::copy( reference.data(), insertBytes.data(), kInsertBytes );
    const int64 referenceNanos = referenceWatch.getElapsedNanoseconds();

    SW_REQUIRE_EQUAL( target.size(), reference.size() );
    SW_EXPECT_TRUE( Memory::compare( target.data(), reference.data(), target.size() ) == 0 );
    SW_LOG_INFO_ID( "ContainerBenchTest.VectorRangeInsert", "vector range insert of %# B before %# B: %# ns, reserve+memcpy %# ns (ratio x%#)", kInsertBytes, kTailBytes,
                    insertNanos, referenceNanos, referenceNanos > 0 ? insertNanos / referenceNanos : 0 );
}

/**
 * @brief [ContainerBenchTest] 10 만 개 `reserve` 뒤 삽입의 할당 수입니다. 문턱: 0 이 아니면 AC-2(값 배열도 예약).
 */
SW_TEST_CASE( ContainerBenchTest, UnorderedMapReservedInsertAllocations, ._tags = "bench", ._disabledReason = "bench - Release 에서 --test-run-disabled 로 돌린다" )
{
    static constexpr uint32       kCount = 100000;
    unordered_map<uint32, uint64> mapValue;
    mapValue.reserve( kCount );
    uint64 allocationCount = 0;
    {
        const test::ScopedAllocationCount counter;
        for ( uint32 key = 0; key < kCount; ++key )
        {
            mapValue.emplace( key, static_cast<uint64>( key ) * 3u );
        }
        allocationCount = counter.getCount();
    }
    SW_EXPECT_EQUAL( mapValue.size(), static_cast<size_t>( kCount ) );
    SW_LOG_INFO_ID( "ContainerBenchTest.UnorderedMapReservedInsertAllocations", "unordered_map: %# inserts after reserve(%#) allocated %# times", kCount, kCount,
                    allocationCount );
}

/**
 * @brief [ContainerBenchTest] 정렬 안 된 10 만 키의 `map` 범위 구축 시간과 `stable_sort` 기준의 비입니다. 문턱: 2 배 이상이면 AC-2(붙이고 정렬, 겹친 키는 앞 것).
 */
SW_TEST_CASE( ContainerBenchTest, MapRangeBuild, ._tags = "bench", ._disabledReason = "bench - Release 에서 --test-run-disabled 로 돌린다", ._timeoutSeconds = 600 )
{
    static constexpr uint32      kCount = 100000;
    uint64                       state  = 0x9E3779B97F4A7C15ull;
    vector<pair<uint32, uint32>> listPair;
    listPair.reserve( kCount );
    for ( uint32 index = 0; index < kCount; ++index )
    {
        listPair.emplace_back( static_cast<uint32>( nextRandom( state ) ), index );
    }

    const Stopwatch           buildWatch;
    const map<uint32, uint32> mapValue( listPair.begin(), listPair.end() );
    const int64               buildNanos = buildWatch.getElapsedNanoseconds();

    vector<pair<uint32, uint32>> sorted = listPair;
    const Stopwatch              sortWatch;
    std::stable_sort( sorted.begin(), sorted.end(), []( const auto& lhs, const auto& rhs )
    { return lhs.first < rhs.first; } );
    const int64 sortNanos = sortWatch.getElapsedNanoseconds();

    SW_EXPECT_TRUE( mapValue.size() <= static_cast<size_t>( kCount ) );
    SW_EXPECT_TRUE( mapValue.empty() == false );
    SW_LOG_INFO_ID( "ContainerBenchTest.MapRangeBuild", "map range build of %# unsorted keys: %# ns, stable_sort %# ns (ratio x%#)", kCount, buildNanos, sortNanos,
                    sortNanos > 0 ? buildNanos / sortNanos : 0 );
}

/**
 * @brief [ContainerBenchTest] 64 B 원소 1 만 번 `push_back` + `pop_front` 의 할당 수와 시간입니다. 문턱: 원소당 할당 0.5 번 이상이면 AC-3(링 버퍼 + `ChunkedArray`).
 * @details Windows · Linux 둘 다 잰다 — 옛 `std::deque` 래퍼는 MSVC STL 이 8 B 넘는 원소마다 블록 하나를 잡았다. 링(감사 C-9) 뒤로는 첫 키우기 몇 번(8 → 128)뿐이라 원소당 0 에 가깝다.
 */
SW_TEST_CASE( ContainerBenchTest, DequeChurn, ._tags = "bench", ._disabledReason = "bench - Release 에서 --test-run-disabled 로 돌린다" )
{
    static constexpr uint32 kCount  = 10000;
    static constexpr uint32 kWindow = 64; // 줄에 늘 남아 있는 원소 수(FIFO 줄 자리)
    deque<Element64Bytes>   queue;
    uint64                  allocationCount = 0;
    int64                   churnNanos      = 0;
    uint64                  checksum        = 0;
    {
        const test::ScopedAllocationCount counter;
        const Stopwatch                   watch;
        for ( uint32 index = 0; index < kCount; ++index )
        {
            Element64Bytes payload;
            payload._arrValue[0] = index;
            queue.push_back( payload );
            if ( queue.size() > kWindow )
            {
                checksum += queue.front()._arrValue[0];
                queue.pop_front();
            }
        }
        churnNanos      = watch.getElapsedNanoseconds();
        allocationCount = counter.getCount();
    }
    SW_EXPECT_EQUAL( queue.size(), static_cast<size_t>( kWindow ) );
    SW_EXPECT_EQUAL( checksum, static_cast<uint64>( kCount - kWindow ) * ( kCount - kWindow - 1 ) / 2 );
    SW_LOG_INFO_ID( "ContainerBenchTest.DequeChurn", "deque<64 B>: %# push_back+pop_front allocated %# times (%# per mille per element), %# ns", kCount, allocationCount,
                    allocationCount * 1000 / kCount, churnNanos );
}

/**
 * @brief [ContainerBenchTest] 예약 없는 `deque<uint64>` 에 100 만 번 `push_back`(세 번에 한 번 `pop_front` — 키울 때 링이 감겨 있다)하는 시간과 같은 수의 `vector<uint64>::push_back` 기준의 비입니다.
 * @details 키우기(`moveToNewBuffer`)가 원소 루프에서 두 토막 `memcpy` 로 바뀐 전후를 잰다(자체 컨테이너 최적화 검토 2026-11-20). 문턱: 바꾼 뒤 시간이 바꾸기 전보다 길면 되돌린다.
 */
SW_TEST_CASE( ContainerBenchTest, DequeGrowBitwise, ._tags = "bench", ._disabledReason = "bench - Release 에서 --test-run-disabled 로 돌린다" )
{
    static constexpr uint32 kCount = 1000000;
    deque<uint64>           queue;
    uint64                  checksum = 0;
    const Stopwatch         dequeWatch;
    for ( uint32 index = 0; index < kCount; ++index )
    {
        queue.push_back( index );
        if ( index % 3 == 2 )
        {
            checksum += queue.front();
            queue.pop_front();
        }
    }
    const int64 dequeNanos = dequeWatch.getElapsedNanoseconds();

    vector<uint64>  reference;
    const Stopwatch referenceWatch;
    for ( uint32 index = 0; index < kCount; ++index )
    {
        reference.push_back( index );
    }
    const int64 referenceNanos = referenceWatch.getElapsedNanoseconds();

    SW_EXPECT_EQUAL( queue.size(), static_cast<size_t>( kCount - kCount / 3 ) );
    SW_EXPECT_EQUAL( queue.front(), static_cast<uint64>( kCount / 3 ) );
    SW_EXPECT_EQUAL( checksum, static_cast<uint64>( kCount / 3 ) * ( kCount / 3 - 1 ) / 2 );
    SW_EXPECT_EQUAL( reference.size(), static_cast<size_t>( kCount ) );
    SW_LOG_INFO_ID( "ContainerBenchTest.DequeGrowBitwise", "deque<uint64>: %# push_back (+1/3 pop_front) without reserve: %# ns, vector push_back %# ns", kCount, dequeNanos,
                    referenceNanos );
}

/**
 * @brief [ContainerBenchTest] `vector<uint32>`(6.5 만 개) 앞에 한 개 넣고 빼기를 2000 번 하는 시간과 `Memory::move` 로 같은 밀기를 하는 기준의 비입니다.
 * @details 후보(자체 컨테이너 최적화 검토 2026-11-20): `insert( pos, count, value )` · `erase` 의 원소 루프를 비트 복사 형식에서 `memmove` 로. 문턱: 기준의 2 배 이상이면 바꾼다.
 */
SW_TEST_CASE( ContainerBenchTest, VectorShiftBitwise, ._tags = "bench", ._disabledReason = "bench - Release 에서 --test-run-disabled 로 돌린다" )
{
    static constexpr size_t kSize  = 65536;
    static constexpr uint32 kRound = 2000;
    vector<uint32>          listValue( kSize, 7u );
    const Stopwatch         shiftWatch;
    for ( uint32 round = 0; round < kRound; ++round )
    {
        listValue.insert( listValue.begin(), size_t( 1 ), round );
        listValue.erase( listValue.begin() );
    }
    const int64 shiftNanos = shiftWatch.getElapsedNanoseconds();

    vector<uint32>  reference( kSize + 1, 7u );
    const Stopwatch referenceWatch;
    for ( uint32 round = 0; round < kRound; ++round )
    {
        Memory::move( reference.data() + 1, reference.data(), kSize * sizeof( uint32 ) );
        reference[0] = round;
        Memory::move( reference.data(), reference.data() + 1, kSize * sizeof( uint32 ) );
    }
    const int64 referenceNanos = referenceWatch.getElapsedNanoseconds();

    SW_EXPECT_EQUAL( listValue.size(), kSize );
    SW_EXPECT_EQUAL( listValue.front(), 7u );
    SW_LOG_INFO_ID( "ContainerBenchTest.VectorShiftBitwise", "vector<uint32> x%#: %# front insert+erase: %# ns, memmove %# ns (ratio x%#)", kSize, kRound, shiftNanos, referenceNanos,
                    referenceNanos > 0 ? shiftNanos / referenceNanos : 0 );
}

/**
 * @brief [ContainerBenchTest] 예약 없는 `vector<string>`(SSO 를 넘는 글 20 만 개) `push_back` 시간과 예약한 경우의 비입니다.
 * @details 후보(자체 컨테이너 최적화 검토 2026-11-20): 키울 때 원소마다 이동 생성 + 소멸 대신, 비트로 옮겨도 되는 형식(trivially relocatable — `string` · `unique_ptr`)은
 *          `memcpy` 로(언리얼 `TArray` 는 모든 형식을 비트로 옮기고, folly `IsRelocatable` 는 형식이 알린다). 문턱: 예약한 경우의 1.5 배 이상이면 형식 표식을 들인다.
 */
SW_TEST_CASE( ContainerBenchTest, VectorGrowString, ._tags = "bench", ._disabledReason = "bench - Release 에서 --test-run-disabled 로 돌린다" )
{
    static constexpr uint32 kCount = 200000;
    const string            text( 40, 'x' ); // SSO(MSVC 15 자 · libstdc++ 15 자) 밖이라 힙 버퍼를 든다
    vector<string>          listGrow;
    const Stopwatch         growWatch;
    for ( uint32 index = 0; index < kCount; ++index )
    {
        listGrow.push_back( text );
    }
    const int64 growNanos = growWatch.getElapsedNanoseconds();

    vector<string>  listReserved;
    const Stopwatch reservedWatch;
    listReserved.reserve( kCount );
    for ( uint32 index = 0; index < kCount; ++index )
    {
        listReserved.push_back( text );
    }
    const int64 reservedNanos = reservedWatch.getElapsedNanoseconds();

    SW_EXPECT_EQUAL( listGrow.size(), static_cast<size_t>( kCount ) );
    SW_EXPECT_EQUAL( listReserved.size(), static_cast<size_t>( kCount ) );
    SW_LOG_INFO_ID( "ContainerBenchTest.VectorGrowString", "vector<string>: %# push_back without reserve %# ns, with reserve %# ns", kCount, growNanos, reservedNanos );
}

/**
 * @brief [ContainerBenchTest] 10 만 키 `unordered_map<uint64, uint64>` 에서 찾기 100 만 번(절반 있음 · 절반 없음)의 원소당 시간입니다.
 * @details 후보(자체 컨테이너 최적화 검토 2026-11-20): 체인(버킷 → 밀집 배열 인덱스, 부하율 1.0)을 열린 주소(abseil `flat_hash_map` 의 SIMD 그룹 탐사 ·
 *          언리얼 `TMap` 은 체인)로. 문턱: 원소당 20 ns 이상이면 설계를 연다(지금 꼴은 태그 비교로 키 비교를 거른다).
 */
SW_TEST_CASE( ContainerBenchTest, UnorderedMapFind, ._tags = "bench", ._disabledReason = "bench - Release 에서 --test-run-disabled 로 돌린다" )
{
    static constexpr uint32       kKeyCount  = 100000;
    static constexpr uint32       kFindCount = 1000000;
    uint64                        state      = 0x9E3779B97F4A7C15ull;
    vector<uint64>                listKey;
    unordered_map<uint64, uint64> mapValue;
    listKey.reserve( kKeyCount );
    mapValue.reserve( kKeyCount );
    for ( uint32 index = 0; index < kKeyCount; ++index )
    {
        const uint64 key = nextRandom( state );
        listKey.push_back( key );
        mapValue.emplace( key, static_cast<uint64>( index ) );
    }

    uint64          hitCount = 0;
    const Stopwatch findWatch;
    for ( uint32 index = 0; index < kFindCount; ++index )
    {
        const uint64 key = ( index & 1u ) == 0 ? listKey[index % kKeyCount] : nextRandom( state ); // 홀수 차례는 거의 늘 없는 키
        hitCount += mapValue.find( key ) != mapValue.end() ? 1u : 0u;
    }
    const int64 findNanos = findWatch.getElapsedNanoseconds();

    SW_EXPECT_TRUE( hitCount >= kFindCount / 2 );
    SW_LOG_INFO_ID( "ContainerBenchTest.UnorderedMapFind", "unordered_map<uint64, uint64> x%#: %# finds (half hit) %# ns (%# ns per find)", kKeyCount, kFindCount, findNanos,
                    findNanos / kFindCount );
}
