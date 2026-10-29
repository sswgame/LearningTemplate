// 결정적 삼각함수 — 기록해 둔 비트와 같아야 한다(구성 · 컴파일러 옵션을 넘어). 기록값은 같은 식을 파이썬 float(IEEE double)으로 따라 계산한 값이고
// 올바로 반올림한 sin · cos · acos 와도 같다. 식을 일부러 바꾸면 새 값을 적고 커밋 메시지에 이유를 적는다.
#include "pch.h"

#include "Core/Math/BitUtil.h"
#include "Core/Math/DeterministicMathUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Testing/TestFramework.h"

#include <limits>

namespace sw
{
    namespace
    {
        struct DeterministicMathTestSample
        {
            float32 _input;
            uint32  _first;  ///< sin 또는 acos 의 비트
            uint32  _second; ///< cos 의 비트(acos 표는 쓰지 않는다)
        };
    } // namespace
} // namespace sw

using namespace sw;

/**
 * @brief [DeterministicMathTest] sin · cos 는 사분면 넷과 큰 각도에서 기록한 비트를 낸다
 */
SW_TEST_CASE( DeterministicMathTest, SinCosMatchRecordedBits )
{
    constexpr DeterministicMathTestSample kArrSample[] = {
        {       0.0f,        0x0u, 0x3f800000u},
        {       0.5f, 0x3ef57744u, 0x3f60a940u},
        {-0.6981317f, 0xbf248dbau, 0x3f441b7du},
        {       1.0f, 0x3f576aa4u, 0x3f0a5140u},
        {       2.5f, 0x3f193578u, 0xbf4d17bfu},
        {      -3.0f, 0xbe1081c3u, 0xbf7d7026u},
        {      10.0f, 0xbf0b44f8u, 0xbf56cd64u},
        {    100.25f, 0xbe8df805u, 0x3f75f639u},
        {   -1234.5f, 0xbe14e299u, 0xbf7d4796u},
    };
    for ( const DeterministicMathTestSample& sample : kArrSample )
    {
        SW_EXPECT_EQUAL( sample._first, BitUtil::floatToBits( DeterministicMathUtil::sin( sample._input ) ) );
        SW_EXPECT_EQUAL( sample._second, BitUtil::floatToBits( DeterministicMathUtil::cos( sample._input ) ) );
    }
    SW_EXPECT_TRUE( MathUtil::isNan( static_cast<float64>( DeterministicMathUtil::sin( std::numeric_limits<float32>::infinity() ) ) ) );
}

/**
 * @brief [DeterministicMathTest] acos 는 양 끝 · 반각 공식 구간 · 가운데에서 기록한 비트를 내고 정의역 밖은 NaN 이다
 */
SW_TEST_CASE( DeterministicMathTest, AcosMatchesRecordedBits )
{
    constexpr DeterministicMathTestSample kArrSample[] = {
        { -1.0f, 0x40490fdbu, 0u},
        {-0.75f, 0x401ace93u, 0u},
        {-0.25f, 0x3fe967aeu, 0u},
        {  0.0f, 0x3fc90fdbu, 0u},
        {  0.3f, 0x3fa20fafu, 0u},
        {  0.9f, 0x3ee6ecfeu, 0u},
        {  1.0f,        0x0u, 0u},
    };
    for ( const DeterministicMathTestSample& sample : kArrSample )
    {
        SW_EXPECT_EQUAL( sample._first, BitUtil::floatToBits( DeterministicMathUtil::acos( sample._input ) ) );
    }
    SW_EXPECT_TRUE( MathUtil::isNan( static_cast<float64>( DeterministicMathUtil::acos( 1.5f ) ) ) );
}

/**
 * @brief [DeterministicMathTest] atan2 는 네 사분면 · 축 위 · 0 의 부호에서 기록한 비트를 낸다
 */
SW_TEST_CASE( DeterministicMathTest, Atan2MatchesRecordedBits )
{
    struct Atan2Sample
    {
        float32 _y;
        float32 _x;
        uint32  _bits;
    };
    constexpr Atan2Sample kArrSample[] = {
        {  0.0f,   1.0f,        0x0u},
        {  1.0f,   0.0f, 0x3fc90fdbu},
        {  1.0f,   1.0f, 0x3f490fdbu},
        { -1.0f,   1.0f, 0xbf490fdbu},
        {  0.5f,  -2.0f, 0x40396220u},
        { -3.0f, -0.25f, 0xbfd3b43au},
        {  0.0f,  -1.0f, 0x40490fdbu},
        { -0.0f,  -1.0f, 0xc0490fdbu},
        {0.001f,   2.0f, 0x3a03126eu},
    };
    for ( const Atan2Sample& sample : kArrSample )
    {
        SW_EXPECT_EQUAL( sample._bits, BitUtil::floatToBits( DeterministicMathUtil::atan2( sample._y, sample._x ) ) );
    }
}
