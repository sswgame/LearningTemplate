#include "pch.h"

#include "Core/Math/DisplayEncoding.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    namespace
    {
        /** @brief SMPTE ST 2084 상수입니다(정의 그대로의 분수). */
        struct DisplayEncodingInternal
        {
            static constexpr float64 kM1 = 2610.0 / 16384.0;
            static constexpr float64 kM2 = 2523.0 / 4096.0 * 128.0;
            static constexpr float64 kC1 = 3424.0 / 4096.0;
            static constexpr float64 kC2 = 2413.0 / 4096.0 * 32.0;
            static constexpr float64 kC3 = 2392.0 / 4096.0 * 32.0;
            /** @brief Rec.709 → BT.2020 원색 행렬(ITU-R BT.2087 표 1, 행 우선). 행마다 합이 1 이라 흰색은 그대로다. */
            static constexpr float32 kArrRec709ToRec2020[3][3] = {
                {0.6274040f, 0.3292820f, 0.0433136f},
                {0.0690970f, 0.9195400f, 0.0113612f},
                {0.0163916f, 0.0880132f, 0.8955950f},
            };
        };
    } // namespace

    float32 DisplayEncoding::encodePQ( const float32 nits ) noexcept
    {
        using Internal          = DisplayEncodingInternal;
        const float64 luminance = static_cast<float64>( MathUtil::clamp( nits, 0.0f, kPQMaxNits ) / kPQMaxNits );
        const float64 powered   = MathUtil::pow( luminance, Internal::kM1 );
        return static_cast<float32>( MathUtil::pow( ( Internal::kC1 + Internal::kC2 * powered ) / ( 1.0 + Internal::kC3 * powered ), Internal::kM2 ) );
    }

    float32 DisplayEncoding::decodePQ( const float32 signal ) noexcept
    {
        using Internal          = DisplayEncodingInternal;
        const float64 powered   = MathUtil::pow( static_cast<float64>( MathUtil::saturate( signal ) ), 1.0 / Internal::kM2 );
        const float64 numerator = MathUtil::max( powered - Internal::kC1, 0.0 );
        const float64 luminance = MathUtil::pow( numerator / ( Internal::kC2 - Internal::kC3 * powered ), 1.0 / Internal::kM1 );
        return static_cast<float32>( luminance * static_cast<float64>( kPQMaxNits ) );
    }

    float3 DisplayEncoding::convertRec709ToRec2020( const float3& linearRec709 ) noexcept
    {
        const auto& matrix = DisplayEncodingInternal::kArrRec709ToRec2020;
        return float3{ matrix[0][0] * linearRec709._x + matrix[0][1] * linearRec709._y + matrix[0][2] * linearRec709._z,
                       matrix[1][0] * linearRec709._x + matrix[1][1] * linearRec709._y + matrix[1][2] * linearRec709._z,
                       matrix[2][0] * linearRec709._x + matrix[2][1] * linearRec709._y + matrix[2][2] * linearRec709._z };
    }
} // namespace sw
