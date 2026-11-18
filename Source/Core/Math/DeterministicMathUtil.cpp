#include "pch.h"

#include "Core/Math/DeterministicMathUtil.h"

#include "Core/Math/BitUtil.h"
#include "Core/Math/MathUtil.h"

#include <array>
#include <cmath>
#include <limits>

namespace sw
{
    namespace
    {
        /**
         * @brief 계산은 전부 float64 의 IEEE 기본 연산이고 마지막에 한 번 float32 로 줄인다 — 중간 오차가 1e-15 아래라 줄인 값이 거의 늘 올바로 반올림된 값이다.
         *        연산 순서를 바꾸면 기록값 테스트(`DeterministicMathTest`)의 비트가 바뀐다.
         */
        struct DeterministicMathUtilInternal
        {
            static constexpr float64 kInvHalfPi = 0.63661977236758134308;
            // π/2 를 두 조각(fdlibm 의 Cody-Waite 상수 pio2_1 · pio2_1t, 비트가 뜻이라 16진 실수로 적는다)으로 뺀다 — 앞 조각은 33 비트라 몫(2^20 미만)과의 곱이 정확하다.
            static constexpr float64 kHalfPiHigh = 0x1.921FB544p+0;
            static constexpr float64 kHalfPiLow  = 0x1.0B4611A626331p-34;
            static constexpr float64 kHalfPi     = MathUtil::kPi64 * 0.5;
            static constexpr float64 kPi         = MathUtil::kPi64;
            // asin 급수의 항 수 — |z| <= 0.5 에서 마지막 항이 0.25^30 배라 float64 오차보다 작다.
            static constexpr int32 kAsinTermCount = 30;

            /** @brief [-π/4, π/4] 의 sin(테일러 급수 15 차까지). */
            static float64 sinKernel( float64 rest )
            {
                const float64 rest2 = rest * rest;
                float64       poly  = -1.0 / 1307674368000.0;
                poly                = poly * rest2 + 1.0 / 6227020800.0;
                poly                = poly * rest2 - 1.0 / 39916800.0;
                poly                = poly * rest2 + 1.0 / 362880.0;
                poly                = poly * rest2 - 1.0 / 5040.0;
                poly                = poly * rest2 + 1.0 / 120.0;
                poly                = poly * rest2 - 1.0 / 6.0;
                return rest + rest * rest2 * poly;
            }

            /** @brief [-π/4, π/4] 의 cos(테일러 급수 16 차까지). */
            static float64 cosKernel( float64 rest )
            {
                const float64 rest2 = rest * rest;
                float64       poly  = 1.0 / 20922789888000.0;
                poly                = poly * rest2 - 1.0 / 87178291200.0;
                poly                = poly * rest2 + 1.0 / 479001600.0;
                poly                = poly * rest2 - 1.0 / 3628800.0;
                poly                = poly * rest2 + 1.0 / 40320.0;
                poly                = poly * rest2 - 1.0 / 720.0;
                poly                = poly * rest2 + 1.0 / 24.0;
                poly                = poly * rest2 - 1.0 / 2.0;
                return 1.0 + rest2 * poly;
            }

            /** @brief 작은 각도 축약(Cody-Waite)이 정확한 |x| 상한입니다 — 몫이 2^20 미만이라 앞 조각(33 비트)과의 곱이 정확하다(2^20 · π/2 ≈ 1.647e6 아래로 둔다). */
            static constexpr float64 kSmallAngleLimit = 1.6e6;
            /** @brief 2/π 의 소수 비트(앞에서부터 32 비트씩, fdlibm `ipio2` 와 같은 수). float32 지수 상한(2^104 · 24 비트 가수)의 축약 창(96 비트)까지 덮는다. */
            static constexpr std::array<uint32, 8> kArrTwoOverPiBits = { 0xA2F9836Eu, 0x4E441529u, 0xFC2757D1u, 0xF534DDC0u, 0xDB629599u, 0x3C439041u, 0xFE5163ABu, 0xDEBBC561u };
            static constexpr int32                 kWordBits         = 32;
            static constexpr int32                 kWordIndexMask    = kWordBits - 1;
            /** @brief float32 비트 배치 — 가수 23 비트, 숨은 1, 가수를 정수로 볼 때의 지수 치우침(127 + 23). */
            static constexpr int32  kFloatMantissaBits = 23;
            static constexpr uint32 kFloatMantissaMask = ( 1u << kFloatMantissaBits ) - 1u;
            static constexpr uint32 kFloatHiddenBit    = 1u << kFloatMantissaBits;
            static constexpr int32  kFloatIntegerBias  = 127 + kFloatMantissaBits;
            /** @brief 축약 결과(아래 96 비트)에서 사분면 2 비트를 뺀 맨 윗 단어의 소수 부분입니다. */
            /** @brief 64 비트 정수 소수를 [0, 1) 로 바꾸는 배율(2^-64)입니다. */
            static constexpr float64 kFractionScale   = 0x1p-64;
            static constexpr uint32  kTopFractionMask = ( 1u << ( kWordBits - 2 ) ) - 1u;

            /** @brief 2/π 의 소수 비트 @p start 번째(1 부터, 2^-start 자리)부터 32 비트입니다. 0 이하 자리는 0 이다(2/π < 1). */
            static uint32 readTwoOverPiWord( int32 start )
            {
                uint64 word = 0;
                for ( int32 bit = 0; bit < kWordBits; ++bit )
                {
                    const int32  index = start + bit - 1; // 0 부터 센 비트 번호
                    const uint32 value = index < 0 ? 0u : ( kArrTwoOverPiBits[index >> 5] >> ( kWordIndexMask - ( index & kWordIndexMask ) ) ) & 1u;
                    word               = ( word << 1 ) | value;
                }
                return static_cast<uint32>( word );
            }

            /**
             * @brief Payne-Hanek 축약 — |x| = m · 2^e(m 은 24 비트 정수)에 2/π 의 필요한 96 비트 창만 정수로 곱해 x · 2/π 의 mod 4 를 얻습니다.
             * @details 창보다 앞 비트(자리 e-2 이하)는 4 의 배수만 보태 버리고, 창 뒤 비트는 2^-70 아래라 float32 결과에 닿지 않는다. 정수 연산과 한 번의 float64 변환이라
             *          결정적이다. 큰 각도에서 Cody-Waite 의 앞 조각 곱이 정확하지 않아 값이 틀어지던 것을 대신한다(OS libm 도 같은 방법이다).
             */
            static int32 reduceLarge( float64 magnitude, float64& outRest )
            {
                const uint32 bits     = BitUtil::floatToBits( static_cast<float32>( magnitude ) );
                const int32  exponent = static_cast<int32>( ( bits >> kFloatMantissaBits ) & 0xFFu ) - kFloatIntegerBias; // 정규 수만 온다(큰 값)
                const uint64 mantissa = ( bits & kFloatMantissaMask ) | kFloatHiddenBit;
                const int32  start    = exponent - 1;
                // 창 W = w2·2^64 + w1·2^32 + w0, 값은 m · W · 2^-94 이고 mod 4 는 곱의 아래 96 비트다.
                const uint64 product0 = mantissa * readTwoOverPiWord( start + 2 * kWordBits );
                const uint64 product1 = mantissa * readTwoOverPiWord( start + kWordBits ) + ( product0 >> 32 );
                const uint64 product2 = mantissa * readTwoOverPiWord( start ) + ( product1 >> 32 );
                const uint32 top      = static_cast<uint32>( product2 ); // 비트 95..64
                // 비트 93..30 을 64 비트 소수로 모은다(맨 윗 비트가 0.5 자리).
                const uint64 fraction = ( static_cast<uint64>( top & kTopFractionMask ) << 34 ) | ( ( product1 & 0xFFFFFFFFu ) << 2 ) | ( ( product0 & 0xFFFFFFFFu ) >> 30 );
                int32        quadrant = static_cast<int32>( top >> 30 );
                float64      part     = 0.0;
                if ( ( fraction >> 63 ) != 0 ) // 0.5 이상이면 다음 사분면에서 음의 나머지
                {
                    quadrant = ( quadrant + 1 ) & 3;
                    part     = -static_cast<float64>( 0 - fraction );
                }
                else
                {
                    part = static_cast<float64>( fraction );
                }
                outRest = part * kFractionScale * kHalfPi;
                return quadrant;
            }

            /** @brief 사분면(0~3)과 [-π/4, π/4] 의 나머지로 나눕니다. 작은 각도는 Cody-Waite, 큰 각도는 Payne-Hanek 입니다. */
            static int32 reduce( float64 x, float64& outRest )
            {
                if ( std::fabs( x ) > kSmallAngleLimit )
                {
                    float64     rest     = 0.0;
                    const int32 quadrant = reduceLarge( std::fabs( x ), rest );
                    outRest              = x < 0.0 ? -rest : rest;
                    return x < 0.0 ? ( 4 - quadrant ) & 3 : quadrant;
                }
                const float64 quotient = std::floor( x * kInvHalfPi + 0.5 );
                outRest                = ( x - quotient * kHalfPiHigh ) - quotient * kHalfPiLow;
                return static_cast<int32>( quotient - 4.0 * std::floor( quotient * 0.25 ) );
            }

            /** @brief |z| <= 0.5 의 asin(테일러 급수, 항 비율로 이어 곱한다). */
            static float64 asinSeries( float64 z )
            {
                const float64 z2   = z * z;
                float64       term = z;
                float64       sum  = z;
                for ( int32 index = 0; index < kAsinTermCount; ++index )
                {
                    const float64 k = static_cast<float64>( index );
                    term            = term * z2 * ( ( 2.0 * k + 1.0 ) * ( 2.0 * k + 1.0 ) ) / ( ( 2.0 * k + 2.0 ) * ( 2.0 * k + 3.0 ) );
                    sum += term;
                }
                return sum;
            }

            /** @brief 0 <= z <= 1 의 asin 입니다. 0.5 를 넘으면 반각 공식 asin(z) = π/2 - 2 asin(sqrt((1 - z) / 2)) 으로 급수 범위에 넣는다. */
            static float64 asinUnit( float64 z )
            {
                if ( z <= 0.5 )
                    return asinSeries( z );
                return kHalfPi - 2.0 * asinSeries( std::sqrt( ( 1.0 - z ) * 0.5 ) );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    float32 DeterministicMathUtil::sin( float32 radian ) noexcept
    {
        using Internal = DeterministicMathUtilInternal;
        if ( std::isfinite( radian ) == false )
            return std::numeric_limits<float32>::quiet_NaN();
        float64       rest     = 0.0;
        const int32   quadrant = Internal::reduce( static_cast<float64>( radian ), rest );
        const float64 value    = ( quadrant & 1 ) == 0 ? Internal::sinKernel( rest ) : Internal::cosKernel( rest );
        return static_cast<float32>( quadrant >= 2 ? -value : value );
    }

    float32 DeterministicMathUtil::cos( float32 radian ) noexcept
    {
        using Internal = DeterministicMathUtilInternal;
        if ( std::isfinite( radian ) == false )
            return std::numeric_limits<float32>::quiet_NaN();
        float64       rest     = 0.0;
        const int32   quadrant = Internal::reduce( static_cast<float64>( radian ), rest );
        const float64 value    = ( quadrant & 1 ) == 0 ? Internal::cosKernel( rest ) : Internal::sinKernel( rest );
        // cos 의 부호는 사분면 1 · 2 에서 음수다.
        return static_cast<float32>( quadrant == 1 || quadrant == 2 ? -value : value );
    }

    float32 DeterministicMathUtil::acos( float32 x ) noexcept
    {
        using Internal       = DeterministicMathUtilInternal;
        const bool bInDomain = x >= -1.0f && x <= 1.0f; // NaN 도 여기서 걸린다
        if ( bInDomain == false )
            return std::numeric_limits<float32>::quiet_NaN();
        const float64 value = static_cast<float64>( x );
        if ( value >= -0.5 && value <= 0.5 )
            return static_cast<float32>( Internal::kHalfPi - Internal::asinSeries( value ) );
        // 끝 쪽은 반각 공식으로 급수 범위(|z| <= 0.5) 안에 넣는다 — acos(x) = 2 asin(sqrt((1 - x) / 2)).
        if ( value > 0.0 )
            return static_cast<float32>( 2.0 * Internal::asinSeries( std::sqrt( ( 1.0 - value ) * 0.5 ) ) );
        return static_cast<float32>( Internal::kPi - 2.0 * Internal::asinSeries( std::sqrt( ( 1.0 + value ) * 0.5 ) ) );
    }

    float32 DeterministicMathUtil::atan2( float32 y, float32 x ) noexcept
    {
        using Internal = DeterministicMathUtilInternal;
        if ( std::isfinite( y ) == false || std::isfinite( x ) == false )
            return std::numeric_limits<float32>::quiet_NaN();
        const float64 absY   = std::fabs( static_cast<float64>( y ) );
        const float64 absX   = std::fabs( static_cast<float64>( x ) );
        const float64 radius = std::sqrt( absX * absX + absY * absY );
        float64       angle  = 0.0;
        if ( radius == 0.0 )
            angle = std::signbit( x ) ? Internal::kPi : 0.0; // libm 과 같은 부호 규칙(±0, -0 → ±π)
        else if ( absY <= absX )
            angle = Internal::asinUnit( absY / radius ); // 비율이 1/√2 이하라 반각 공식의 상쇄가 작다
        else
            angle = Internal::kHalfPi - Internal::asinUnit( absX / radius );
        if ( radius != 0.0 && x < 0.0f )
            angle = Internal::kPi - angle;
        return static_cast<float32>( std::signbit( y ) ? -angle : angle );
    }
} // namespace sw
