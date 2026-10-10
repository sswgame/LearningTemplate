/**
 * @file TestFixedStringBench.cpp
 * @brief `fixed_string` 마이크로벤치 — 객체 크기, `size()`, 대입, 비교, `clear()`.
 * @details 숫자를 **찍기만** 하고 판정하지 않는다(기계마다 다르다). 판정은 정합성만 본다(잰 길이 합이 넣은 길이 합과 같다).
 *          회귀의 근거는 `docs/09_Decisions.md` 에 Release 로 잰 표로 남긴다 — 이 케이스는 그 표를 같은 코드로 다시 만들기 위한 자리다.
 *
 *          재는 것:
 *          - `sizeof` — `fixed_string<15/31/63/160>`, `fixed_wstring<15>`, 이 타입을 배열로 드는 `CrashBreadcrumbStore` · `CrashContextStore`.
 *          - 길이가 제각각인 문자열 256 개를 돌며 `size()` 를 부르는 비용.
 *          - C 문자열 대입, 같은 용량 복사 대입, 같은 내용 비교(`==`).
 *          - `clear()` 뒤 짧은 글 붙이기(`fixed_string<64>` 와 `fixed_string<8192>`).
 *
 * @note Release 로 읽는다. Debug 의 컨테이너 레이스 탐지기가 숫자를 다른 것으로 만든다.
 */
#include "pch.h"

#include "Core/Diagnostics/CrashContext.h"
#include "Core/String/fixed_string.h"

#include "TestFramework/TestBench.h"
#include "TestFramework/TestFramework.h"

SW_LOG_CALLER( "FixedStringBench" );

namespace
{
    /** @brief 한 벌을 몇 번 되풀이해 가장 빠른 판을 고르는가 — 첫 판의 캐시 · 페이지 비용을 걸러낸다. */
    constexpr uint32 kBenchRoundCount = 5;
    /** @brief 문자열 수 — 길이가 제각각이라 컴파일러가 `size()` 를 루프 밖으로 들어 올리지 못한다. */
    constexpr uint32 kBenchStringCount = 256;
    /** @brief 한 판에서 문자열 묶음을 몇 번 도는가. */
    constexpr uint32 kBenchRepeatCount = 1000;

    /** @brief 벤치 본문이 남기는 값 — 최적화기가 연산을 지우지 못하게 한다. */
    volatile uint64 s_fixedStringBenchSink = 0;

    using BenchString = sw::fixed_string<64>;

    /** @brief `index` 번째 문자열(길이 8..47)을 `pOutText` 에 씁니다. 길이를 돌려줍니다. */
    uint32 makeSampleText( uint32 index, utf8* pOutText )
    {
        const uint32 length = 8 + ( index * 7 ) % 40;
        for ( uint32 charIndex = 0; charIndex < length; ++charIndex )
        {
            pOutText[charIndex] = static_cast<utf8>( 'a' + ( index + charIndex ) % 26 );
        }
        pOutText[length] = '\0';
        return length;
    }
} // namespace

/**
 * @brief [FixedStringBenchTest] 객체 크기 — 용량별 `sizeof` 와 이 타입을 배열로 드는 저장소의 크기
 */
SW_TEST_CASE( FixedStringBenchTest, ObjectSize )
{
    SW_LOG_INFO( "[Bench] sizeof fixed_string<15> %#  <31> %#  <63> %#  <160> %#  fixed_wstring<15> %#", static_cast<uint32>( sizeof( sw::fixed_string<15> ) ),
                 static_cast<uint32>( sizeof( sw::fixed_string<31> ) ), static_cast<uint32>( sizeof( sw::fixed_string<63> ) ),
                 static_cast<uint32>( sizeof( sw::fixed_string<160> ) ), static_cast<uint32>( sizeof( sw::fixed_wstring<15> ) ) );
    SW_LOG_INFO( "[Bench] sizeof CrashBreadcrumbStore %#  CrashContextStore %#", static_cast<uint32>( sizeof( sw::CrashBreadcrumbStore ) ),
                 static_cast<uint32>( sizeof( sw::CrashContextStore ) ) );
    SW_EXPECT_TRUE( sizeof( sw::fixed_string<15> ) >= 16 );
}

/**
 * @brief [FixedStringBenchTest] `size()` · 대입 · 비교 · `clear()` 의 연산당 시간
 */
SW_TEST_CASE( FixedStringBenchTest, SizeAssignCompareClear )
{
    static utf8        s_arrText[kBenchStringCount][64];
    static BenchString s_arrString[kBenchStringCount];
    static BenchString s_arrCopy[kBenchStringCount];
    uint64             expectedLengthSum = 0;
    for ( uint32 index = 0; index < kBenchStringCount; ++index )
    {
        expectedLengthSum += makeSampleText( index, s_arrText[index] );
        s_arrString[index] = s_arrText[index];
        s_arrCopy[index]   = s_arrText[index];
    }

    constexpr uint64 kOpCount  = static_cast<uint64>( kBenchStringCount ) * kBenchRepeatCount;
    uint64           lengthSum = 0;

    const int64 sizeDeci = test::measureBestDeciNanosPerOp( kOpCount, kBenchRoundCount, [&]()
    {
        uint64 sum = 0;
        for ( uint32 repeat = 0; repeat < kBenchRepeatCount; ++repeat )
        {
            for ( uint32 index = 0; index < kBenchStringCount; ++index )
            {
                sum += std::as_const( s_arrString[index] ).size();
            }
        }
        lengthSum              = sum;
        s_fixedStringBenchSink = sum;
    } );

    const int64 assignDeci = test::measureBestDeciNanosPerOp( kOpCount, kBenchRoundCount, [&]()
    {
        uint64 sum = 0;
        for ( uint32 repeat = 0; repeat < kBenchRepeatCount; ++repeat )
        {
            for ( uint32 index = 0; index < kBenchStringCount; ++index )
            {
                BenchString& target = s_arrCopy[( index + repeat ) % kBenchStringCount];
                target              = s_arrText[index];
                sum += static_cast<uint8>( target.data()[0] );
            }
        }
        s_fixedStringBenchSink = sum;
    } );

    const int64 copyDeci = test::measureBestDeciNanosPerOp( kOpCount, kBenchRoundCount, [&]()
    {
        uint64 sum = 0;
        for ( uint32 repeat = 0; repeat < kBenchRepeatCount; ++repeat )
        {
            for ( uint32 index = 0; index < kBenchStringCount; ++index )
            {
                BenchString& target = s_arrCopy[( index + repeat ) % kBenchStringCount];
                target              = s_arrString[index];
                sum += static_cast<uint8>( target.data()[0] );
            }
        }
        s_fixedStringBenchSink = sum;
    } );

    for ( uint32 index = 0; index < kBenchStringCount; ++index )
    {
        s_arrCopy[index] = s_arrString[index];
    }
    uint64      equalCount  = 0;
    const int64 compareDeci = test::measureBestDeciNanosPerOp( kOpCount, kBenchRoundCount, [&]()
    {
        uint64 count = 0;
        for ( uint32 repeat = 0; repeat < kBenchRepeatCount; ++repeat )
        {
            for ( uint32 index = 0; index < kBenchStringCount; ++index )
            {
                count += ( s_arrString[index] == s_arrCopy[index] ) ? 1u : 0u;
            }
        }
        equalCount             = count;
        s_fixedStringBenchSink = count;
    } );

    static sw::fixed_string<8192> s_largeString;
    const int64                   clearSmallDeci = test::measureBestDeciNanosPerOp( kOpCount, kBenchRoundCount, [&]()
                      {
        uint64 sum = 0;
        for ( uint32 repeat = 0; repeat < kBenchRepeatCount; ++repeat )
        {
            for ( uint32 index = 0; index < kBenchStringCount; ++index )
            {
                BenchString& target = s_arrCopy[index];
                target.clear();
                target.append( "ab" );
                sum += static_cast<uint8>( target.data()[1] );
            }
        }
        s_fixedStringBenchSink = sum;
    } );
    const int64                   clearLargeDeci = test::measureBestDeciNanosPerOp( kOpCount, kBenchRoundCount, [&]()
                      {
        uint64 sum = 0;
        for ( uint32 repeat = 0; repeat < kBenchRepeatCount; ++repeat )
        {
            for ( uint32 index = 0; index < kBenchStringCount; ++index )
            {
                s_largeString.clear();
                s_largeString.append( s_arrText[index] );
                sum += static_cast<uint8>( s_largeString.data()[1] );
            }
        }
        s_fixedStringBenchSink = sum;
    } );

    test::logBenchDeciNanos( "fixed_string<64>::size()", sizeDeci );
    test::logBenchDeciNanos( "fixed_string<64> = const utf8*", assignDeci );
    test::logBenchDeciNanos( "fixed_string<64> = fixed_string<64>", copyDeci );
    test::logBenchDeciNanos( "fixed_string<64> == fixed_string<64>", compareDeci );
    test::logBenchDeciNanos( "fixed_string<64> clear + append", clearSmallDeci );
    test::logBenchDeciNanos( "fixed_string<8192> clear + append", clearLargeDeci );

    SW_EXPECT_EQUAL( expectedLengthSum * kBenchRepeatCount, lengthSum );
    SW_EXPECT_EQUAL( kOpCount, equalCount );
}
