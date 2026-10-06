#include "pch.h"

#include "Core/Common/HashUtil.h"

#include "TestFramework/TestFramework.h"

// splitmix64 · boost 결합 · FNV 는 파일마다 복사돼 있던 식이다 — 모은 함수가 옛 식과 같은 값을 내야 네트워크 비교 · 결정적 시뮬레이션이 그대로다.

/**
 * @brief [HashUtilTest] mix64 는 splitmix64 참고 구현(Vigna)의 마무리와 같은 값을 낸다(씨앗 0 의 첫 두 값)
 */
SW_TEST_CASE( HashUtilTest, Mix64MatchesSplitMixReference )
{
    uint64 state = 0;
    state += sw::HashUtil::kGoldenRatio64;
    SW_EXPECT_EQUAL( uint64{ 0xE220A8397B1DCDAFull }, sw::HashUtil::mix64( state ) );
    state += sw::HashUtil::kGoldenRatio64;
    SW_EXPECT_EQUAL( uint64{ 0x6E789E6AA1B965F4ull }, sw::HashUtil::mix64( state ) );
}

/**
 * @brief [HashUtilTest] combine 은 boost hash_combine 의 64 비트 식과 같다
 */
SW_TEST_CASE( HashUtilTest, CombineMatchesBoostFormula )
{
    const uint64 seed  = 0x0123456789ABCDEFull;
    const uint64 value = 42;
    SW_EXPECT_EQUAL( seed ^ ( value + 0x9E3779B97F4A7C15ull + ( seed << 6 ) + ( seed >> 2 ) ), sw::HashUtil::combine( seed, value ) );
}

/**
 * @brief [HashUtilTest] FNV-1a 기저 · 소수는 명세 값이다(64 비트 기저의 끝 자리까지)
 */
SW_TEST_CASE( HashUtilTest, FnvConstantsAreTheSpecValues )
{
    SW_EXPECT_EQUAL( uint32{ 2166136261u }, sw::HashUtil::kFnvOffset32 );
    SW_EXPECT_EQUAL( uint32{ 16777619u }, sw::HashUtil::kFnvPrime32 );
    SW_EXPECT_EQUAL( uint64{ 14695981039346656037ull }, sw::HashUtil::kFnvOffset64 );
    SW_EXPECT_EQUAL( uint64{ 1099511628211ull }, sw::HashUtil::kFnvPrime64 );
}
