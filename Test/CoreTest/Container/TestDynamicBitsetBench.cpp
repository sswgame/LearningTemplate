/**
 * @file TestDynamicBitsetBench.cpp
 * @brief `DynamicBitset` 연산별 마이크로벤치 — 비트 수 8 · 64 · 1024 · 65536, 비교 기준 `std::bitset` · `vector<uint64>` 수작업 루프.
 * @details 숫자를 **찍기만** 하고 판정하지 않는다(기계마다 다르다). 판정은 비교 기준과 같은 답을 내는지만 본다. 회귀의 근거는
 *          `docs/08_Verification.md` 에 Release 로 측정한 표로 남긴다 — 이 케이스는 그 표를 같은 코드로 다시 만들기 위한 자리다.
 *
 *          줄마다 판(round) 15 개의 연산당 ns 의 p50 · p99 를 찍는다. 비트 하나씩 도는 연산(`set` · `test` · `push_back` · `parse` ·
 *          `to_string`)은 비트당 ns, 나머지는 비트셋 한 번당 ns 다. `&` · `|` · `^` · `~` · `<<` · `>>` 는 사본을 돌려주므로 할당을 포함한다
 *          (`&=` 줄이 할당 없는 몫).
 *
 * @note Release 로 읽는다. Debug 는 단언과 컨테이너 검사가 숫자를 다른 것으로 만든다.
 */
#include "pch.h"

#include "Core/Common/HashUtil.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Container/DynamicBitset.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Time/MonotonicClock.h"

#include "TestFramework/Harness/TestBench.h"
#include "TestFramework/TestFramework.h"

#include <bitset>
#include <string>

SW_TEST_LOAD_SUITE( DynamicBitsetBenchTest, "DynamicBitset micro-benchmarks - per operation at 8 / 64 / 1024 / 65536 bits against std::bitset" );
SW_TEST_TIME_BUDGET( DynamicBitsetBenchTest, 5000, "Release 케이스 최대 1.3 s(2026-10-17 Bits8 — 사본 연산마다 할당, 세 번 중 최대)의 네 배 — CI 러너 여유" );

SW_LOG_CALLER( "DynamicBitsetBench" );

namespace
{
    /** @brief 벤치 본문이 남기는 값 — 최적화기가 연산을 지우지 못하게 한다. */
    volatile uint64 s_bitsetBenchSink = 0;

    struct DynamicBitsetBenchInternal
    {
#if defined( SW_DEBUG )
        // Debug 는 정확성만 본다(숫자는 Release 로 읽는다). Release 크기로는 케이스 넷이 80 초 걸린다.
        static constexpr uint32 kRoundCount   = 2;
        static constexpr uint32 kBitsPerRound = 1u << 16;
#else
        /** @brief 한 줄을 몇 판 재는가 — p50 · p99 를 낼 표본 수. */
        static constexpr uint32 kRoundCount = 15;
        /** @brief 한 판에 지나는 비트 수 — 비트셋이 작으면 그만큼 여러 번 되풀이해 판 길이를 맞춘다. */
        static constexpr uint32 kBitsPerRound = 1u << 20;
#endif
        /** @brief 시프트 양 — 블록 안 비트 시프트 경로(크기 8 에서도 의미가 있는 값). */
        static constexpr uint32 kShift = 3;

        /** @brief 값이 메모리에 있어야 하게 만든다 — 인라인되는 `std::bitset` 연산이 통째로 지워지지 않게 한다. */
        template <typename T>
        static void keepValue( const T& value )
        {
#if defined( SW_COMPILER_CLANG ) || defined( SW_COMPILER_GCC )
            asm volatile( "" : : "r"( &value ) : "memory" );
#else
            s_bitsetBenchSink = s_bitsetBenchSink + *reinterpret_cast<const volatile uint8*>( &value );
#endif
        }

        /** @brief @p body 를 한 번 데운 뒤 판마다 재고, 연산당 ns(10 배 정수)의 p50 · p99 를 한 줄로 찍는다. */
        template <typename TBody>
        static void measureRow( const utf8* pLabel, const uint32 bitCount, const uint64 opCount, TBody&& body )
        {
            body();
            sw::vector<int64> listDeci;
            listDeci.reserve( kRoundCount );
            for ( uint32 round = 0; round < kRoundCount; ++round )
            {
                const sw::Stopwatch stopwatch;
                body();
                listDeci.push_back( stopwatch.getElapsedNanoseconds() * 10 / static_cast<int64>( opCount ) );
            }
            [[maybe_unused]] const int64 p50 = test::getPercentile( listDeci, 50 );
            [[maybe_unused]] const int64 p99 = test::getPercentile( listDeci, 99 );
            SW_LOG_INFO( "[Bench] %# bits=%#  p50 %#.%# ns  p99 %#.%# ns", pLabel, bitCount, p50 / 10, p50 % 10, p99 / 10, p99 % 10 );
        }

        /** @brief splitmix64 로 흩은 비트 @p bitCount 개 — 같은 씨앗이면 같은 무늬다. */
        static sw::vector<uint64> makeRandomBlock( const uint32 bitCount, const uint64 seed )
        {
            sw::vector<uint64> listBlock( ( bitCount + 63 ) / 64, 0 );
            for ( uint32 blockIndex = 0; blockIndex < listBlock.size(); ++blockIndex )
            {
                listBlock[blockIndex] = sw::HashUtil::mix64( seed + blockIndex * sw::HashUtil::kGoldenRatio64 );
            }
            const uint32 restBits = bitCount & 63;
            if ( restBits != 0 )
                listBlock.back() &= ( uint64{ 1 } << restBits ) - 1;
            return listBlock;
        }

        /** @brief 블록 무늬로 `DynamicBitset` 을 만든다. */
        static sw::DynamicBitset makeDynamic( const uint32 bitCount, const sw::vector<uint64>& listBlock )
        {
            sw::DynamicBitset value( bitCount );
            for ( uint32 bitIndex = 0; bitIndex < bitCount; ++bitIndex )
            {
                value.set( bitIndex, ( ( listBlock[bitIndex / 64] >> ( bitIndex % 64 ) ) & 1 ) != 0 );
            }
            return value;
        }

        /** @brief 블록 무늬로 `std::bitset` 을 만든다. */
        template <size_t TBitCount>
        static std::bitset<TBitCount> makeStd( const sw::vector<uint64>& listBlock )
        {
            std::bitset<TBitCount> value;
            for ( uint32 bitIndex = 0; bitIndex < TBitCount; ++bitIndex )
            {
                value.set( bitIndex, ( ( listBlock[bitIndex / 64] >> ( bitIndex % 64 ) ) & 1 ) != 0 );
            }
            return value;
        }

        /** @brief 한 크기의 모든 연산을 `DynamicBitset` · 비교 기준 순으로 번갈아 잰다. */
        template <uint32 TBitCount>
        static void benchSize()
        {
            constexpr uint32 kRepeat     = kBitsPerRound / TBitCount;
            constexpr uint64 kBitOpCount = static_cast<uint64>( kRepeat ) * TBitCount;

            const sw::vector<uint64> listBlockA = makeRandomBlock( TBitCount, 1 );
            const sw::vector<uint64> listBlockB = makeRandomBlock( TBitCount, 2 );

            const sw::DynamicBitset dynA = makeDynamic( TBitCount, listBlockA );
            const sw::DynamicBitset dynB = makeDynamic( TBitCount, listBlockB );
            const auto              stdA = makeStd<TBitCount>( listBlockA );
            const auto              stdB = makeStd<TBitCount>( listBlockB );

            // any 는 맨 위 비트 하나만 켜 끝까지 훑게, all 은 모두 켜 끝까지 훑게 둔다.
            sw::DynamicBitset dynTop( TBitCount );
            dynTop.set( TBitCount - 1 );
            std::bitset<TBitCount> stdTop;
            stdTop.set( TBitCount - 1 );
            sw::DynamicBitset dynFull( TBitCount );
            dynFull.set();
            std::bitset<TBitCount> stdFull;
            stdFull.set();

            // == 는 같은 값끼리, < 는 비트 0 만 다르게 — 둘 다 끝까지 훑는다.
            const sw::DynamicBitset dynSame = dynA;
            const auto              stdSame = stdA;
            sw::DynamicBitset       dynLess = dynA;
            dynLess.set( 0, false );
            sw::DynamicBitset dynMore = dynA;
            dynMore.set( 0, true );
            sw::vector<uint64> listLess = listBlockA;
            listLess[0] &= ~uint64{ 1 };
            sw::vector<uint64> listMore = listBlockA;
            listMore[0] |= uint64{ 1 };

            const sw::string  textA    = dynA.to_string();
            const std::string stdTextA = stdA.to_string();
            SW_ASSERT_TRUE( sw::string_view( textA ) == sw::string_view( stdTextA.data(), stdTextA.size() ) );
            SW_EXPECT_EQUAL( static_cast<uint32>( stdA.count() ), dynA.count() );
            SW_EXPECT_TRUE( ( dynA & dynB ).to_string() == sw::string( ( stdA & stdB ).to_string().c_str() ) );
            SW_EXPECT_TRUE( ( dynA << kShift ).to_string() == sw::string( ( stdA << kShift ).to_string().c_str() ) );
            SW_EXPECT_TRUE( ( dynA >> kShift ).to_string() == sw::string( ( stdA >> kShift ).to_string().c_str() ) );
            SW_EXPECT_TRUE( dynLess < dynMore );

            // --- 비트셋 한 번당 연산 ---
            measureRow( "count       DynamicBitset", TBitCount, kRepeat, [&]()
            {
                uint64 sum = 0;
                for ( uint32 repeat = 0; repeat < kRepeat; ++repeat )
                {
                    keepValue( dynA );
                    sum += dynA.count();
                }
                s_bitsetBenchSink = sum;
            } );
            measureRow( "count       std::bitset", TBitCount, kRepeat, [&]()
            {
                uint64 sum = 0;
                for ( uint32 repeat = 0; repeat < kRepeat; ++repeat )
                {
                    keepValue( stdA );
                    sum += stdA.count();
                }
                s_bitsetBenchSink = sum;
            } );
            measureRow( "any(top)    DynamicBitset", TBitCount, kRepeat, [&]()
            {
                uint64 sum = 0;
                for ( uint32 repeat = 0; repeat < kRepeat; ++repeat )
                {
                    keepValue( dynTop );
                    sum += dynTop.any() ? 1 : 0;
                }
                s_bitsetBenchSink = sum;
            } );
            measureRow( "any(top)    std::bitset", TBitCount, kRepeat, [&]()
            {
                uint64 sum = 0;
                for ( uint32 repeat = 0; repeat < kRepeat; ++repeat )
                {
                    keepValue( stdTop );
                    sum += stdTop.any() ? 1 : 0;
                }
                s_bitsetBenchSink = sum;
            } );
            measureRow( "all(full)   DynamicBitset", TBitCount, kRepeat, [&]()
            {
                uint64 sum = 0;
                for ( uint32 repeat = 0; repeat < kRepeat; ++repeat )
                {
                    keepValue( dynFull );
                    sum += dynFull.all() ? 1 : 0;
                }
                s_bitsetBenchSink = sum;
            } );
            measureRow( "all(full)   std::bitset", TBitCount, kRepeat, [&]()
            {
                uint64 sum = 0;
                for ( uint32 repeat = 0; repeat < kRepeat; ++repeat )
                {
                    keepValue( stdFull );
                    sum += stdFull.all() ? 1 : 0;
                }
                s_bitsetBenchSink = sum;
            } );
            measureRow( "a & b       DynamicBitset", TBitCount, kRepeat, [&]()
            {
                for ( uint32 repeat = 0; repeat < kRepeat; ++repeat )
                {
                    const sw::DynamicBitset result = dynA & dynB;
                    keepValue( result );
                }
            } );
            measureRow( "a & b       std::bitset", TBitCount, kRepeat, [&]()
            {
                for ( uint32 repeat = 0; repeat < kRepeat; ++repeat )
                {
                    keepValue( stdA );
                    const auto result = stdA & stdB;
                    keepValue( result );
                }
            } );
            measureRow( "a &= b      DynamicBitset", TBitCount, kRepeat, [&]()
            {
                sw::DynamicBitset target = dynA;
                for ( uint32 repeat = 0; repeat < kRepeat; ++repeat )
                {
                    target &= dynB;
                    keepValue( target );
                }
            } );
            measureRow( "a &= b      std::bitset", TBitCount, kRepeat, [&]()
            {
                auto target = stdA;
                for ( uint32 repeat = 0; repeat < kRepeat; ++repeat )
                {
                    target &= stdB;
                    keepValue( target );
                }
            } );
            measureRow( "a | b       DynamicBitset", TBitCount, kRepeat, [&]()
            {
                for ( uint32 repeat = 0; repeat < kRepeat; ++repeat )
                {
                    const sw::DynamicBitset result = dynA | dynB;
                    keepValue( result );
                }
            } );
            measureRow( "a | b       std::bitset", TBitCount, kRepeat, [&]()
            {
                for ( uint32 repeat = 0; repeat < kRepeat; ++repeat )
                {
                    keepValue( stdA );
                    const auto result = stdA | stdB;
                    keepValue( result );
                }
            } );
            measureRow( "a ^ b       DynamicBitset", TBitCount, kRepeat, [&]()
            {
                for ( uint32 repeat = 0; repeat < kRepeat; ++repeat )
                {
                    const sw::DynamicBitset result = dynA ^ dynB;
                    keepValue( result );
                }
            } );
            measureRow( "a ^ b       std::bitset", TBitCount, kRepeat, [&]()
            {
                for ( uint32 repeat = 0; repeat < kRepeat; ++repeat )
                {
                    keepValue( stdA );
                    const auto result = stdA ^ stdB;
                    keepValue( result );
                }
            } );
            measureRow( "~a          DynamicBitset", TBitCount, kRepeat, [&]()
            {
                for ( uint32 repeat = 0; repeat < kRepeat; ++repeat )
                {
                    const sw::DynamicBitset result = ~dynA;
                    keepValue( result );
                }
            } );
            measureRow( "~a          std::bitset", TBitCount, kRepeat, [&]()
            {
                for ( uint32 repeat = 0; repeat < kRepeat; ++repeat )
                {
                    keepValue( stdA );
                    const auto result = ~stdA;
                    keepValue( result );
                }
            } );
            measureRow( "a << 3      DynamicBitset", TBitCount, kRepeat, [&]()
            {
                for ( uint32 repeat = 0; repeat < kRepeat; ++repeat )
                {
                    const sw::DynamicBitset result = dynA << kShift;
                    keepValue( result );
                }
            } );
            measureRow( "a << 3      std::bitset", TBitCount, kRepeat, [&]()
            {
                for ( uint32 repeat = 0; repeat < kRepeat; ++repeat )
                {
                    keepValue( stdA );
                    const auto result = stdA << kShift;
                    keepValue( result );
                }
            } );
            measureRow( "a >> 3      DynamicBitset", TBitCount, kRepeat, [&]()
            {
                for ( uint32 repeat = 0; repeat < kRepeat; ++repeat )
                {
                    const sw::DynamicBitset result = dynA >> kShift;
                    keepValue( result );
                }
            } );
            measureRow( "a >> 3      std::bitset", TBitCount, kRepeat, [&]()
            {
                for ( uint32 repeat = 0; repeat < kRepeat; ++repeat )
                {
                    keepValue( stdA );
                    const auto result = stdA >> kShift;
                    keepValue( result );
                }
            } );
            measureRow( "a == same   DynamicBitset", TBitCount, kRepeat, [&]()
            {
                uint64 sum = 0;
                for ( uint32 repeat = 0; repeat < kRepeat; ++repeat )
                {
                    keepValue( dynA );
                    sum += ( dynA == dynSame ) ? 1 : 0;
                }
                s_bitsetBenchSink = sum;
            } );
            measureRow( "a == same   std::bitset", TBitCount, kRepeat, [&]()
            {
                uint64 sum = 0;
                for ( uint32 repeat = 0; repeat < kRepeat; ++repeat )
                {
                    keepValue( stdA );
                    sum += ( stdA == stdSame ) ? 1 : 0;
                }
                s_bitsetBenchSink = sum;
            } );
            measureRow( "a < b(bit0) DynamicBitset", TBitCount, kRepeat, [&]()
            {
                uint64 sum = 0;
                for ( uint32 repeat = 0; repeat < kRepeat; ++repeat )
                {
                    keepValue( dynLess );
                    sum += ( dynLess < dynMore ) ? 1 : 0;
                }
                s_bitsetBenchSink = sum;
            } );
            measureRow( "a < b(bit0) vector<uint64>", TBitCount, kRepeat, [&]()
            {
                uint64 sum = 0;
                for ( uint32 repeat = 0; repeat < kRepeat; ++repeat )
                {
                    keepValue( listLess );
                    bool bLess = false;
                    for ( size_t blockIndex = listLess.size(); blockIndex > 0; --blockIndex )
                    {
                        if ( listLess[blockIndex - 1] != listMore[blockIndex - 1] )
                        {
                            bLess = listLess[blockIndex - 1] < listMore[blockIndex - 1];
                            break;
                        }
                    }
                    sum += bLess ? 1 : 0;
                }
                s_bitsetBenchSink = sum;
            } );
            measureRow( "resize(0->n,true) DynamicBitset", TBitCount, kRepeat, [&]()
            {
                for ( uint32 repeat = 0; repeat < kRepeat; ++repeat )
                {
                    sw::DynamicBitset value;
                    value.resize( TBitCount, true );
                    keepValue( value );
                }
            } );
            measureRow( "resize(0->n,true) vector<uint64>", TBitCount, kRepeat, [&]()
            {
                for ( uint32 repeat = 0; repeat < kRepeat; ++repeat )
                {
                    sw::vector<uint64> listValue;
                    listValue.resize( ( TBitCount + 63 ) / 64, ~uint64{ 0 } );
                    if constexpr ( ( TBitCount & 63 ) != 0 )
                        listValue.back() &= ( uint64{ 1 } << ( TBitCount & 63 ) ) - 1;
                    keepValue( listValue );
                }
            } );

            // --- 비트당 연산 ---
            measureRow( "set(pos)    DynamicBitset", TBitCount, kBitOpCount, [&]()
            {
                sw::DynamicBitset target( TBitCount );
                for ( uint32 repeat = 0; repeat < kRepeat; ++repeat )
                {
                    for ( uint32 bitIndex = 0; bitIndex < TBitCount; ++bitIndex )
                    {
                        target.set( bitIndex, ( ( bitIndex ^ repeat ) & 1 ) != 0 );
                    }
                    keepValue( target );
                }
            } );
            measureRow( "set(pos)    std::bitset", TBitCount, kBitOpCount, [&]()
            {
                std::bitset<TBitCount> target;
                for ( uint32 repeat = 0; repeat < kRepeat; ++repeat )
                {
                    for ( uint32 bitIndex = 0; bitIndex < TBitCount; ++bitIndex )
                    {
                        target.set( bitIndex, ( ( bitIndex ^ repeat ) & 1 ) != 0 );
                    }
                    keepValue( target );
                }
            } );
            measureRow( "test(pos)   DynamicBitset", TBitCount, kBitOpCount, [&]()
            {
                uint64 sum = 0;
                for ( uint32 repeat = 0; repeat < kRepeat; ++repeat )
                {
                    keepValue( dynA );
                    for ( uint32 bitIndex = 0; bitIndex < TBitCount; ++bitIndex )
                    {
                        sum += dynA.test( bitIndex ) ? 1 : 0;
                    }
                }
                s_bitsetBenchSink = sum;
            } );
            measureRow( "test(pos)   std::bitset", TBitCount, kBitOpCount, [&]()
            {
                uint64 sum = 0;
                for ( uint32 repeat = 0; repeat < kRepeat; ++repeat )
                {
                    keepValue( stdA );
                    for ( uint32 bitIndex = 0; bitIndex < TBitCount; ++bitIndex )
                    {
                        sum += stdA.test( bitIndex ) ? 1 : 0;
                    }
                }
                s_bitsetBenchSink = sum;
            } );
            measureRow( "push_back   DynamicBitset", TBitCount, kBitOpCount, [&]()
            {
                for ( uint32 repeat = 0; repeat < kRepeat; ++repeat )
                {
                    sw::DynamicBitset target;
                    for ( uint32 bitIndex = 0; bitIndex < TBitCount; ++bitIndex )
                    {
                        target.push_back( ( ( bitIndex ^ repeat ) & 1 ) != 0 );
                    }
                    keepValue( target );
                }
            } );
            measureRow( "push_back   vector<uint64>", TBitCount, kBitOpCount, [&]()
            {
                for ( uint32 repeat = 0; repeat < kRepeat; ++repeat )
                {
                    sw::vector<uint64> listValue;
                    for ( uint32 bitIndex = 0; bitIndex < TBitCount; ++bitIndex )
                    {
                        if ( ( bitIndex & 63 ) == 0 )
                            listValue.push_back( 0 );
                        listValue.back() |= static_cast<uint64>( ( bitIndex ^ repeat ) & 1 ) << ( bitIndex & 63 );
                    }
                    keepValue( listValue );
                }
            } );
            measureRow( "parse       DynamicBitset", TBitCount, kBitOpCount, [&]()
            {
                sw::DynamicBitset target;
                for ( uint32 repeat = 0; repeat < kRepeat; ++repeat )
                {
                    if ( sw::DynamicBitset::parse( textA, target ) == false )
                        s_bitsetBenchSink = 0;
                    keepValue( target );
                }
            } );
            measureRow( "parse       std::bitset", TBitCount, kBitOpCount, [&]()
            {
                for ( uint32 repeat = 0; repeat < kRepeat; ++repeat )
                {
                    const std::bitset<TBitCount> target( stdTextA.data(), stdTextA.size() );
                    keepValue( target );
                }
            } );
            measureRow( "to_string   DynamicBitset", TBitCount, kBitOpCount, [&]()
            {
                for ( uint32 repeat = 0; repeat < kRepeat; ++repeat )
                {
                    const sw::string text = dynA.to_string();
                    keepValue( text );
                }
            } );
            measureRow( "to_string   std::bitset", TBitCount, kBitOpCount, [&]()
            {
                for ( uint32 repeat = 0; repeat < kRepeat; ++repeat )
                {
                    keepValue( stdA );
                    const std::string text = stdA.to_string();
                    keepValue( text );
                }
            } );
        }
    };
} // namespace

/**
 * @brief [DynamicBitsetBenchTest] 8 비트 — 직렬화 필드 크기(MaxCount 8 안팎)
 */
SW_TEST_CASE( DynamicBitsetBenchTest, Bits8 )
{
    DynamicBitsetBenchInternal::benchSize<8>();
}

/**
 * @brief [DynamicBitsetBenchTest] 64 비트 — 블록 하나를 꽉 채움
 */
SW_TEST_CASE( DynamicBitsetBenchTest, Bits64 )
{
    DynamicBitsetBenchInternal::benchSize<64>();
}

/**
 * @brief [DynamicBitsetBenchTest] 1024 비트 — 블록 16 개
 */
SW_TEST_CASE( DynamicBitsetBenchTest, Bits1024 )
{
    DynamicBitsetBenchInternal::benchSize<1024>();
}

/**
 * @brief [DynamicBitsetBenchTest] 65536 비트 — 블록 1024 개
 */
SW_TEST_CASE( DynamicBitsetBenchTest, Bits65536 )
{
    DynamicBitsetBenchInternal::benchSize<65536>();
}
