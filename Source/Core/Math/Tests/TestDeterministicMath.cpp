// 결정적 삼각함수 — 기록해 둔 비트와 같아야 한다(구성 · 컴파일러 옵션을 넘어). 기록값은 같은 식을 파이썬 float(IEEE double)으로 따라 계산한 값이고
// 올바로 반올림한 sin · cos · acos 와도 같다. 식을 일부러 바꾸면 새 값을 적고 커밋 메시지에 이유를 적는다.
#include "pch.h"

#include "Core/Math/BitUtil.h"
#include "Core/Math/DeterministicMathUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/ValueNoise.h"
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

/**
 * @brief [DeterministicMathTest] 쿼터니언 곱은 기록한 비트를 낸다 — Math 폴더가 결정성 표시(축약 끔)에서 빠지면 Release 의 FMA 합치기가 끝 비트를 바꾼다(감사 C-14)
 * @details 기록값은 같은 식을 numpy float32 로 연산마다 반올림해 계산했다. 입력은 첫 곱을 FMA 로 합치면 네 성분 모두 비트가 달라지는 값이다.
 */
SW_TEST_CASE( DeterministicMathTest, QuaternionProductMatchesRecordedBits )
{
    const quaternion lhs{ -0.0734f, -0.4116f, 0.2329f, 0.9918f };
    const quaternion rhs{ -0.5976f, 0.5143f, 0.0438f, 0.2413f };
    const quaternion product = lhs * rhs;
    SW_EXPECT_EQUAL( 0xbf3f8b52u, BitUtil::floatToBits( product._x ) );
    SW_EXPECT_EQUAL( 0x3e8cb242u, BitUtil::floatToBits( product._y ) );
    SW_EXPECT_EQUAL( 0xbe3c8008u, BitUtil::floatToBits( product._z ) );
    SW_EXPECT_EQUAL( 0x3ecb3c08u, BitUtil::floatToBits( product._w ) );
}

/**
 * @brief [DeterministicMathTest] 값 노이즈(한 옥타브 · 프랙탈)는 기록한 비트를 낸다 — 지형 · 절차 생성이 서버와 클라이언트에서 같은 값을 내야 한다(감사 C-14)
 * @details 기록값은 numpy float32 로 따라 계산했다. 첫 표본은 보간을 FMA 로 합치면 끝 비트가 하나 달라지는 자리다.
 */
SW_TEST_CASE( DeterministicMathTest, ValueNoiseMatchesRecordedBits )
{
    SW_EXPECT_EQUAL( 0x3ec5ad92u, BitUtil::floatToBits( ValueNoise::sampleValue( 0.37f, -1.73f, 42u ) ) );
    SW_EXPECT_EQUAL( 0x3ed34c1fu, BitUtil::floatToBits( ValueNoise::sampleFractal( 0.37f, -1.73f, 42u, 4 ) ) );
    SW_EXPECT_EQUAL( 0x3f0bab0fu, BitUtil::floatToBits( ValueNoise::sampleFractal( 12.625f, 3.1f, 7u, 5 ) ) );
    SW_EXPECT_EQUAL( 0x3edd02b0u, BitUtil::floatToBits( ValueNoise::sampleFractal( -5.55f, 8.125f, 123456u, 3 ) ) );
}
