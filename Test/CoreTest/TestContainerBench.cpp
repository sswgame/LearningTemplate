/**
 * @file TestContainerBench.cpp
 * @brief Core 컨테이너 마이크로벤치 — 해시맵 조회 · 문자열 키 조회 · intern 적중 · 벡터 한 칸씩 늘리기.
 * @details 숫자를 **찍기만** 하고 판정하지 않는다(기계마다 다르다). 판정은 정합성만 본다(넣은 키는 전부 찾고,
 *          넣지 않은 키는 못 찾는다). 회귀의 근거는 `docs/06_Backlog.md` 에 Release 로 잰 표로 남긴다 — 이 케이스는
 *          그 표를 같은 코드로 다시 만들기 위한 자리다.
 *
 *          재는 것:
 *          - `unordered_map<uint64, uint32>` 적중 · 빗나감 조회(버킷 수가 2 의 거듭제곱인 판과 `reserve` 로 잡은 판).
 *          - `unordered_set<uint64>` 적중 조회.
 *          - `unordered_map<string, uint32>` 를 `string_view` 로 조회(이종 조회).
 *          - 이미 intern 된 이름으로 `hashed_string` 만들기(샤드 락 + 맵 조회).
 *          - `vector::resize( size() + 1 )` 를 되풀이하기 — 재할당이 기하급수로 줄어드는가.
 *          - `SlotHandleTable::get` 락 없는 조회(흩어진 순서).
 *
 * @note Release 로 읽는다. Debug 의 컨테이너 레이스 탐지기가 숫자를 다른 것으로 만든다.
 */
#include "pch.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Container/SlotHandle.h"
#include "Core/Container/SlotHandleTable.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/unordered_set.h"
#include "Core/Container/vector.h"
#include "Core/String/StringUtil.h"
#include "Core/String/hashed_string.h"

#include "TestFramework/TestFramework.h"

SW_LOG_CALLER( "ContainerBench" );

namespace
{
    /** @brief 조회 한 벌을 몇 번 되풀이해 가장 빠른 판을 고르는가 — 첫 판의 캐시 · 페이지 비용을 걸러낸다. */
    constexpr uint32 kRoundCount = 5;

    /** @brief 벤치 본문이 남기는 값 — 최적화기가 조회를 지우지 못하게 한다. */
    volatile uint64 s_benchSink = 0;

    /** @brief splitmix64 — 키를 고르게 흩는다(연속 정수 키는 해시가 약해도 좋아 보인다). */
    uint64 mixKey( uint64 value )
    {
        value += 0x9E3779B97F4A7C15ull;
        value = ( value ^ ( value >> 30 ) ) * 0xBF58476D1CE4E5B9ull;
        value = ( value ^ ( value >> 27 ) ) * 0x94D049BB133111EBull;
        return value ^ ( value >> 31 );
    }

    /** @brief @p pBody 를 kRoundCount 번 돌려 가장 짧은 판의 연산당 ns 를 10 배 정수로 돌려줍니다(소수 한 자리). */
    template <typename BodyFn>
    int64 bestDeciNanosPerOp( uint64 opCount, BodyFn&& body )
    {
        int64 bestNanos = std::numeric_limits<int64>::max();
        for ( uint32 round = 0; round < kRoundCount; ++round )
        {
            const auto start = std::chrono::steady_clock::now();
            body();
            const int64 nanos = std::chrono::duration_cast<std::chrono::nanoseconds>( std::chrono::steady_clock::now() - start ).count();
            bestNanos         = std::min( bestNanos, nanos );
        }
        return ( bestNanos * 10 ) / static_cast<int64>( opCount == 0 ? 1 : opCount );
    }

    /** @brief `[Bench] 이름  x.y ns/op` 한 줄. Shipping 은 Info 로그가 컴파일에서 빠진다. */
    void logDeciNanos( [[maybe_unused]] const utf8* pLabel, [[maybe_unused]] int64 deciNanos )
    {
        SW_LOG_INFO( "[Bench] %#  %#.%# ns/op", pLabel, deciNanos / 10, deciNanos % 10 );
    }
} // namespace

/**
 * @brief [ContainerBenchTest] 정수 키 해시맵 · 해시셋 조회 — 적중과 빗나감, 2 의 거듭제곱 버킷과 `reserve` 로 잡은 버킷
 */
SW_TEST_CASE( ContainerBenchTest, IntegerKeyLookup )
{
    constexpr uint32   kKeyCount = 50000;
    sw::vector<uint64> listKey;
    sw::vector<uint64> listMissKey;
    listKey.reserve( kKeyCount );
    listMissKey.reserve( kKeyCount );
    for ( uint32 index = 0; index < kKeyCount; ++index )
    {
        listKey.push_back( mixKey( index ) );
        listMissKey.push_back( mixKey( index + kKeyCount * 4 ) );
    }

    sw::unordered_map<uint64, uint32> mapGrown;
    for ( uint32 index = 0; index < kKeyCount; ++index )
        mapGrown.emplace( listKey[index], index );
    SW_ASSERT_EQUAL( kKeyCount, static_cast<uint32>( mapGrown.size() ) );

    // 버킷 수를 손으로 잡은 판 — 2 의 거듭제곱이 아닌 수를 준다.
    sw::unordered_map<uint64, uint32> mapReserved;
    mapReserved.reserve( 3000 );
    for ( uint32 index = 0; index < 3000; ++index )
        mapReserved.emplace( listKey[index], index );

    sw::unordered_set<uint64> setKey;
    for ( uint32 index = 0; index < kKeyCount; ++index )
        setKey.insert( listKey[index] );

    const uint64* pKey     = listKey.data();
    const uint64* pMissKey = listMissKey.data();

    uint32      wrongCount   = 0;
    const int64 hitDeci      = bestDeciNanosPerOp( kKeyCount, [&]()
         {
        uint64 sum = 0;
        for ( uint32 index = 0; index < kKeyCount; ++index )
        {
            const auto iter = std::as_const( mapGrown ).find( pKey[index] );
            if ( iter == mapGrown.end() || iter->second != index )
                ++wrongCount;
            else
                sum += iter->second;
        }
        s_benchSink = sum;
    } );
    const int64 missDeci     = bestDeciNanosPerOp( kKeyCount, [&]()
        {
        uint64 found = 0;
        for ( uint32 index = 0; index < kKeyCount; ++index )
            found += std::as_const( mapGrown ).find( pMissKey[index] ) != mapGrown.end() ? 1u : 0u;
        wrongCount += static_cast<uint32>( found );
        s_benchSink = found;
    } );
    const int64 reservedDeci = bestDeciNanosPerOp( 3000u * 16u, [&]()
    {
        uint64 sum = 0;
        for ( uint32 repeat = 0; repeat < 16; ++repeat )
        {
            for ( uint32 index = 0; index < 3000; ++index )
            {
                const auto iter = std::as_const( mapReserved ).find( pKey[index] );
                if ( iter == mapReserved.end() || iter->second != index )
                    ++wrongCount;
                else
                    sum += iter->second;
            }
        }
        s_benchSink = sum;
    } );
    const int64 setDeci      = bestDeciNanosPerOp( kKeyCount, [&]()
         {
        uint64 found = 0;
        for ( uint32 index = 0; index < kKeyCount; ++index )
            found += setKey.contains( pKey[index] ) ? 1u : 0u;
        wrongCount += ( found == kKeyCount ) ? 0u : 1u;
        s_benchSink = found;
    } );

    SW_EXPECT_EQUAL( 0u, wrongCount );
    logDeciNanos( "unordered_map<uint64> find hit  (50000, grown)", hitDeci );
    logDeciNanos( "unordered_map<uint64> find miss (50000, grown)", missDeci );
    logDeciNanos( "unordered_map<uint64> find hit  (3000, reserve(3000))", reservedDeci );
    logDeciNanos( "unordered_set<uint64> contains  (50000, grown)", setDeci );
}

/**
 * @brief [ContainerBenchTest] 문자열 키 해시맵을 `string_view` 로 조회 — 키를 만들지 않는 이종 조회
 */
SW_TEST_CASE( ContainerBenchTest, StringKeyLookup )
{
    constexpr uint32       kKeyCount = 4096;
    sw::vector<sw::string> listName;
    listName.reserve( kKeyCount );
    for ( uint32 index = 0; index < kKeyCount; ++index )
        listName.push_back( sw::string( "engine/materials/bench_" ) + sw::to_string( mixKey( index ) % 1000003u ) + "_" + sw::to_string( index ) );

    sw::unordered_map<sw::string, uint32> mapName;
    for ( uint32 index = 0; index < kKeyCount; ++index )
        mapName.emplace( listName[index], index );

    uint32      wrongCount = 0;
    const int64 deci       = bestDeciNanosPerOp( kKeyCount, [&]()
          {
        uint64 sum = 0;
        for ( uint32 index = 0; index < kKeyCount; ++index )
        {
            const auto iter = std::as_const( mapName ).find( sw::string_view( listName[index] ) );
            if ( iter == mapName.end() || iter->second != index )
                ++wrongCount;
            else
                sum += iter->second;
        }
        s_benchSink = sum;
    } );
    SW_EXPECT_EQUAL( 0u, wrongCount );
    logDeciNanos( "unordered_map<string> find(string_view) hit (4096)", deci );
}

/**
 * @brief [ContainerBenchTest] 이미 intern 된 이름으로 `hashed_string` 만들기 — 해시 · 샤드 락 · 맵 조회
 */
SW_TEST_CASE( ContainerBenchTest, HashedStringInternHit )
{
    constexpr uint32       kNameCount   = 64;
    constexpr uint32       kRepeatCount = 256;
    sw::vector<sw::string> listName;
    for ( uint32 index = 0; index < kNameCount; ++index )
        listName.push_back( sw::string( "ContainerBenchName_" ) + sw::to_string( index ) );
    sw::vector<uint32> listExpected;
    for ( const sw::string& name : listName )
        listExpected.push_back( sw::hashed_string( sw::string_view( name ) ).getIndex() );

    uint32      wrongCount = 0;
    const int64 deci       = bestDeciNanosPerOp( kNameCount * kRepeatCount, [&]()
          {
        for ( uint32 repeat = 0; repeat < kRepeatCount; ++repeat )
        {
            for ( uint32 index = 0; index < kNameCount; ++index )
            {
                if ( sw::hashed_string( sw::string_view( listName[index] ) ).getIndex() != listExpected[index] )
                    ++wrongCount;
            }
        }
    } );
    SW_EXPECT_EQUAL( 0u, wrongCount );
    logDeciNanos( "hashed_string(string_view) intern hit (24 chars)", deci );
}

/**
 * @brief [ContainerBenchTest] `vector::resize( size() + 1 )` 되풀이 — 한 칸씩 늘려도 재할당이 기하급수로 줄어야 한다
 */
SW_TEST_CASE( ContainerBenchTest, VectorGrowByResize )
{
    constexpr uint32 kFinalSize = 20000;
    uint32           wrongCount = 0;
    const int64      deci       = bestDeciNanosPerOp( kFinalSize, [&]()
               {
        sw::vector<uint32> listValue;
        for ( uint32 index = 0; index < kFinalSize; ++index )
        {
            listValue.resize( listValue.size() + 1 );
            listValue.back() = index;
        }
        if ( listValue.size() != kFinalSize || listValue.back() != kFinalSize - 1 )
            ++wrongCount;
        s_benchSink = listValue.capacity();
    } );
    SW_EXPECT_EQUAL( 0u, wrongCount );
    logDeciNanos( "vector<uint32>::resize( size() + 1 ) x20000", deci );
}

/**
 * @brief [ContainerBenchTest] `SlotHandleTable::get` — 락 없는 조회. RHI 백엔드가 드로우마다 버퍼 · 텍스처 · 파이프라인을 찾는 길입니다.
 */
SW_TEST_CASE( ContainerBenchTest, SlotHandleTableGet )
{
    constexpr uint32 kSlotCount  = 16384;
    constexpr uint32 kProbeCount = 1u << 18;

    sw::SlotHandleTable<uint64> table;
    sw::vector<sw::SlotHandle>  listHandle;
    listHandle.reserve( kSlotCount );
    for ( uint32 index = 0; index < kSlotCount; ++index )
        listHandle.push_back( table.insert( index ) );

    // 조회 순서를 흩는다. 인덱스 순서대로 읽으면 청크 하나가 캐시에 머물러 실제보다 빨라 보인다.
    sw::vector<sw::SlotHandle> listProbe;
    listProbe.reserve( kProbeCount );
    for ( uint32 index = 0; index < kProbeCount; ++index )
        listProbe.push_back( listHandle[static_cast<uint32>( mixKey( index ) % kSlotCount )] );

    const sw::SlotHandle* pProbe     = listProbe.data();
    uint32                wrongCount = 0;
    const int64           getDeci    = bestDeciNanosPerOp( kProbeCount, [&]()
                 {
        uint64 sum = 0;
        for ( uint32 index = 0; index < kProbeCount; ++index )
        {
            const uint64* pValue = std::as_const( table ).get( pProbe[index] );
            if ( pValue == nullptr || *pValue != pProbe[index].index() )
                ++wrongCount;
            else
                sum += *pValue;
        }
        s_benchSink = sum;
    } );

    SW_EXPECT_EQUAL( 0u, wrongCount );
    logDeciNanos( "SlotHandleTable<uint64>::get hit (16384 slots, scattered)", getDeci );
}
