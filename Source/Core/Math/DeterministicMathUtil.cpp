#include "pch.h"

#include "Core/Math/DeterministicMathUtil.h"

#include "Core/Math/MathUtil.h"

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

            /**
             * @brief 사분면(0~3)과 [-π/4, π/4] 의 나머지로 나눕니다.
             * @details shortcut: |x| 가 2^20 · π/2(약 1.6e6)를 넘으면 앞 조각 곱이 정확하지 않아 정확도가 떨어진다(결정성은 그대로). 시뮬레이션 각도는 그보다 훨씬 작다 —
             *          큰 각도가 필요해지면 Payne-Hanek 축약으로 바꾼다.
             */
            static int32 reduce( float64 x, float64& outRest )
            {
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
