/**
 * @file PackedValueTraits.h
 * @brief 저장 전용 압축 타입을 실수 성분 묶음(`PackedFloats`)으로 푸는 한 곳의 정의 `PackedValueTraits<T>` 입니다.
 * @details 리플렉션 · 직렬화 · 에디터 인스펙터가 압축 타입을 **값**으로 다루는 길은 모두 이 특성을 지납니다. 글(`0.5,1,0.25`), 인자 변환(`ReflectedValue`),
 *          인스펙터 위젯이 타입마다 따로 분기를 갖지 않고 `toFloats` · `fromFloats` 만 부릅니다. 압축 타입을 `ReflectBuiltins.xxx` 에 더하고 여기에
 *          특수화를 빠뜨리면 글 처리기 등록(`SerializeContext::makeDefault`)이 컴파일되지 않습니다.
 *          - **성분 규칙**: `toFloats` 는 저장된 값을 정확히 나타내는 실수를 성분 순서대로 돌려주고, `fromFloats` 는 그 실수를 다시 같은 비트로 담습니다
 *            (`fromFloats( toFloats( v ) )` 가 비트까지 같습니다. 예외 둘: `snorm` 의 정수 최솟값은 `-최대` 로, `half` NaN 은 페이로드 위쪽 10 비트만 남는 NaN 으로 갑니다).
 *            그래서 글 왕복이 자릿수를 잃지 않습니다. `OctahedralNormal` 의 성분은 방향 xyz 가 아니라 저장된 좌표 (u, v) 입니다 — 방향으로 풀고 다시 담으면
 *            양자화 한 칸 안에서 비트가 흔들릴 수 있어(`OctahedralNormal.h`) 글 왕복이 같은 비트를 지키지 못합니다.
 *            `Radians` 의 성분은 라디안이 아니라 **도**입니다(데이터 · 인스펙터의 단위). 도 `float32` 로 반올림하므로 왕복이 라디안 한 ulp 안에서 흔들릴 수 있고,
 *            `toFloats` 는 같은 라디안으로 돌아오는 도 중 가장 짧은 십진수를 고른다(손으로 쓴 `2.5` 가 다시 저장해도 `2.5` 다).
 *          - **범위**: `kRangeMin` · `kRangeMax` 는 `fromFloats` 가 조용히 자르거나 무한대로 보내지 않는 성분 범위입니다. 읽는 쪽이 범위 밖 입력을 거절할 때 씁니다
 *            (정수 필드가 범위를 넘는 글을 거절하는 것과 같은 규칙). NaN 은 범위 밖입니다.
 *          - 압축 타입이 아닌 `T` 의 `PackedValueTraits<T>` 는 비어 있어 `kIsPackedValue<T>` 가 false 입니다.
 */
#pragma once
#include "Core/Foundation/StdHeaders.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Half.h"
#include "Core/Math/NormalizedInt.h"
#include "Core/Math/OctahedralNormal.h"
#include "Core/Math/RGB10A2.h"
#include "Core/Math/Radians.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    /** @brief 압축 타입 값 하나의 실수 성분입니다. 쓰는 성분은 앞쪽 `kComponentCount` 개이고 나머지는 0 입니다. */
    struct PackedFloats
    {
        static constexpr uint32 kMaxCount = 4;

        float32 _arrValue[kMaxCount]{};
    };
} // namespace sw

namespace sw
{
    /** @brief 압축 타입 @p T 의 성분 규칙입니다. 압축 타입마다 특수화하고, 아닌 타입은 비어 있습니다. */
    template <typename T>
    struct PackedValueTraits
    {
    };
} // namespace sw

namespace sw
{
    template <typename T, typename = void>
    struct IsPackedValue : std::false_type
    {
    };

    template <typename T>
    struct IsPackedValue<T, std::void_t<decltype( PackedValueTraits<T>::kComponentCount )>> : std::true_type
    {
    };

    /** @brief @p T 가 `PackedValueTraits` 특수화를 가진 압축 타입인지입니다. */
    template <typename T>
    inline constexpr bool kIsPackedValue = IsPackedValue<T>::value;
} // namespace sw

namespace sw
{
    /** @brief 성분이 하나인 압축 타입(`half` · `unorm` · `snorm`)의 공통 풀이입니다. 범위는 특수화가 채웁니다. */
    template <typename T>
    struct PackedScalarTraits
    {
        static constexpr uint32 kComponentCount = 1;

        [[nodiscard]] static constexpr PackedFloats toFloats( const T& value ) noexcept
        {
            PackedFloats result{};
            result._arrValue[0] = value.toFloat();
            return result;
        }
        [[nodiscard]] static constexpr T fromFloats( const PackedFloats& floats ) noexcept { return T::fromFloat( floats._arrValue[0] ); }
    };

    template <>
    struct PackedValueTraits<half> : PackedScalarTraits<half>
    {
        static constexpr float32 kRangeMin  = -half::kMaxValue;
        static constexpr float32 kRangeMax  = half::kMaxValue;
        static constexpr float32 kDragSpeed = 0.01f;
    };

    template <typename TStorage>
    struct PackedValueTraits<unorm<TStorage>> : PackedScalarTraits<unorm<TStorage>>
    {
        static constexpr float32 kRangeMin  = 0.0f;
        static constexpr float32 kRangeMax  = 1.0f;
        static constexpr float32 kDragSpeed = 0.005f;
    };

    template <typename TStorage>
    struct PackedValueTraits<snorm<TStorage>> : PackedScalarTraits<snorm<TStorage>>
    {
        static constexpr float32 kRangeMin  = -1.0f;
        static constexpr float32 kRangeMax  = 1.0f;
        static constexpr float32 kDragSpeed = 0.005f;
    };
} // namespace sw

namespace sw
{
    /** @brief 정규화 정수 묶음(`unorm8x4` · `snorm16x2` …)입니다. 성분은 메모리 순서이고 범위는 성분 하나의 범위입니다. */
    template <typename TScalar, uint32 kCount>
    struct PackedValueTraits<NormalizedVector<TScalar, kCount>>
    {
        static_assert( kCount <= PackedFloats::kMaxCount, "PackedFloats 가 담는 성분 수를 넘는다" );

        using Value = NormalizedVector<TScalar, kCount>;

        static constexpr uint32  kComponentCount = kCount;
        static constexpr float32 kRangeMin       = PackedValueTraits<TScalar>::kRangeMin;
        static constexpr float32 kRangeMax       = PackedValueTraits<TScalar>::kRangeMax;
        static constexpr float32 kDragSpeed      = PackedValueTraits<TScalar>::kDragSpeed;

        [[nodiscard]] static constexpr PackedFloats toFloats( const Value& value ) noexcept
        {
            PackedFloats result{};
            for ( uint32 index = 0; index < kCount; ++index )
            {
                result._arrValue[index] = value._arrValue[index].toFloat();
            }
            return result;
        }
        [[nodiscard]] static constexpr Value fromFloats( const PackedFloats& floats ) noexcept
        {
            Value result{};
            for ( uint32 index = 0; index < kCount; ++index )
            {
                result._arrValue[index] = TScalar::fromFloat( floats._arrValue[index] );
            }
            return result;
        }
    };

    template <>
    struct PackedValueTraits<half2>
    {
        static constexpr uint32  kComponentCount = 2;
        static constexpr float32 kRangeMin       = PackedValueTraits<half>::kRangeMin;
        static constexpr float32 kRangeMax       = PackedValueTraits<half>::kRangeMax;
        static constexpr float32 kDragSpeed      = PackedValueTraits<half>::kDragSpeed;

        [[nodiscard]] static constexpr PackedFloats toFloats( const half2& value ) noexcept
        {
            PackedFloats result{};
            result._arrValue[0] = value._x.toFloat();
            result._arrValue[1] = value._y.toFloat();
            return result;
        }
        [[nodiscard]] static constexpr half2 fromFloats( const PackedFloats& floats ) noexcept
        {
            return half2{ half::fromFloat( floats._arrValue[0] ), half::fromFloat( floats._arrValue[1] ) };
        }
    };

    template <>
    struct PackedValueTraits<half3>
    {
        static constexpr uint32  kComponentCount = 3;
        static constexpr float32 kRangeMin       = PackedValueTraits<half>::kRangeMin;
        static constexpr float32 kRangeMax       = PackedValueTraits<half>::kRangeMax;
        static constexpr float32 kDragSpeed      = PackedValueTraits<half>::kDragSpeed;

        [[nodiscard]] static constexpr PackedFloats toFloats( const half3& value ) noexcept
        {
            PackedFloats result{};
            result._arrValue[0] = value._x.toFloat();
            result._arrValue[1] = value._y.toFloat();
            result._arrValue[2] = value._z.toFloat();
            return result;
        }
        [[nodiscard]] static constexpr half3 fromFloats( const PackedFloats& floats ) noexcept
        {
            return half3{ half::fromFloat( floats._arrValue[0] ), half::fromFloat( floats._arrValue[1] ), half::fromFloat( floats._arrValue[2] ) };
        }
    };

    template <>
    struct PackedValueTraits<half4>
    {
        static constexpr uint32  kComponentCount = 4;
        static constexpr float32 kRangeMin       = PackedValueTraits<half>::kRangeMin;
        static constexpr float32 kRangeMax       = PackedValueTraits<half>::kRangeMax;
        static constexpr float32 kDragSpeed      = PackedValueTraits<half>::kDragSpeed;

        [[nodiscard]] static constexpr PackedFloats toFloats( const half4& value ) noexcept
        {
            PackedFloats result{};
            result._arrValue[0] = value._x.toFloat();
            result._arrValue[1] = value._y.toFloat();
            result._arrValue[2] = value._z.toFloat();
            result._arrValue[3] = value._w.toFloat();
            return result;
        }
        [[nodiscard]] static constexpr half4 fromFloats( const PackedFloats& floats ) noexcept
        {
            return half4{ half::fromFloat( floats._arrValue[0] ), half::fromFloat( floats._arrValue[1] ), half::fromFloat( floats._arrValue[2] ),
                          half::fromFloat( floats._arrValue[3] ) };
        }
    };

    /** @brief 옥타헤드럴 좌표 (u, v) 입니다 — 방향 xyz 가 아니라 저장된 값 그대로라 글 왕복이 같은 비트를 지킵니다. */
    template <>
    struct PackedValueTraits<OctahedralNormal>
    {
        static constexpr uint32  kComponentCount = 2;
        static constexpr float32 kRangeMin       = -1.0f;
        static constexpr float32 kRangeMax       = 1.0f;
        static constexpr float32 kDragSpeed      = 0.005f;

        [[nodiscard]] static constexpr PackedFloats toFloats( const OctahedralNormal& value ) noexcept
        {
            PackedFloats result{};
            result._arrValue[0] = value._u.toFloat();
            result._arrValue[1] = value._v.toFloat();
            return result;
        }
        [[nodiscard]] static constexpr OctahedralNormal fromFloats( const PackedFloats& floats ) noexcept
        {
            return OctahedralNormal{ snorm16::fromFloat( floats._arrValue[0] ), snorm16::fromFloat( floats._arrValue[1] ) };
        }
    };

    /** @brief R, G, B, A 의 0..1 실수입니다(알파는 네 단계: 0, 1/3, 2/3, 1). */
    template <>
    struct PackedValueTraits<RGB10A2>
    {
        static constexpr uint32  kComponentCount = 4;
        static constexpr float32 kRangeMin       = 0.0f;
        static constexpr float32 kRangeMax       = 1.0f;
        static constexpr float32 kDragSpeed      = 0.005f;

        [[nodiscard]] static constexpr PackedFloats toFloats( const RGB10A2& value ) noexcept
        {
            const float4 rgba = value.toFloat4();
            PackedFloats result{};
            result._arrValue[0] = rgba._x;
            result._arrValue[1] = rgba._y;
            result._arrValue[2] = rgba._z;
            result._arrValue[3] = rgba._w;
            return result;
        }
        [[nodiscard]] static constexpr RGB10A2 fromFloats( const PackedFloats& floats ) noexcept
        {
            return RGB10A2::fromFloat4( float4{ floats._arrValue[0], floats._arrValue[1], floats._arrValue[2], floats._arrValue[3] } );
        }
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 라디안 각의 바깥 표현은 도 하나입니다(`Radians.h`). 범위는 데이터 오타(라디안을 도 칸에 적음 등)가 아니라 float32 를 지키는 넉넉한 경계입니다.
     * @details `toFloats` 는 1 자리부터 `kMaxDecimalDigits` 자리까지 늘려 가며 같은 라디안으로 돌아오는 첫 십진수를 고른다 — 단순히 float32 로 자르면 `2.5` 도가 `2.4999998` 로 저장됐다.
     */
    template <>
    struct PackedValueTraits<Radians>
    {
        static constexpr uint32  kComponentCount = 1;
        static constexpr float32 kRangeMin       = -1.0e6f;
        static constexpr float32 kRangeMax       = 1.0e6f;
        static constexpr float32 kDragSpeed      = 0.5f;
        /** @brief 도 성분을 고를 때 늘려 보는 유효 숫자의 끝입니다 — float32 를 정확히 나타내는 십진 자릿수(`FLT_DECIMAL_DIG`)다. */
        static constexpr int32   kMaxDecimalDigits = 9;
        static constexpr float64 kDecimalBase      = 10.0;

        [[nodiscard]] static PackedFloats toFloats( const Radians& value ) noexcept
        {
            const float64 degrees = static_cast<float64>( value.radians() ) * Radians::kDegreesPerRadian;
            PackedFloats  result{};
            result._arrValue[0]     = static_cast<float32>( degrees );
            const bool bSearchShort = std::isfinite( degrees ) && degrees != 0.0;
            if ( bSearchShort == false )
                return result;
            const float64 magnitude = std::floor( std::log10( std::fabs( degrees ) ) );
            for ( int32 digits = 1; digits <= kMaxDecimalDigits; ++digits )
            {
                const float64 scale     = std::pow( kDecimalBase, static_cast<float64>( digits - 1 ) - magnitude );
                const float32 candidate = static_cast<float32>( std::round( degrees * scale ) / scale );
                if ( Radians::fromDegrees( candidate ) == value )
                {
                    result._arrValue[0] = candidate;
                    break;
                }
            }
            return result;
        }
        [[nodiscard]] static constexpr Radians fromFloats( const PackedFloats& floats ) noexcept { return Radians::fromDegrees( floats._arrValue[0] ); }
    };

    /**
     * @brief 정수 좌표 `int2` 의 성분은 정수 둘입니다(언리얼 `FIntPoint` 의 X · Y). 범위는 float32 가 정수를 정확히 나타내는 ±(2^24 - 1) 라 왕복이 비트까지 같고,
     *        `kIntegerComponents` 가 소수 성분(`2.5`)을 범위 밖처럼 거절하게 한다(`isPackedFloatsInRange`) — 말없이 반올림하지 않는다.
     */
    template <>
    struct PackedValueTraits<int2>
    {
        static constexpr uint32  kComponentCount    = 2;
        static constexpr float32 kRangeMin          = -16777215.0f; // 2^24 - 1 — 글의 16777217 은 float32 로 읽으면 2^24 가 되므로 2^24 부터 거절해야 잡힌다
        static constexpr float32 kRangeMax          = 16777215.0f;
        static constexpr float32 kDragSpeed         = 1.0f;
        static constexpr bool    kIntegerComponents = true;

        [[nodiscard]] static constexpr PackedFloats toFloats( const int2& value ) noexcept
        {
            PackedFloats result{};
            result._arrValue[0] = static_cast<float32>( value._x );
            result._arrValue[1] = static_cast<float32>( value._y );
            return result;
        }
        [[nodiscard]] static constexpr int2 fromFloats( const PackedFloats& floats ) noexcept
        {
            return int2{ static_cast<int32>( floats._arrValue[0] ), static_cast<int32>( floats._arrValue[1] ) };
        }
    };
} // namespace sw

namespace sw
{
    template <typename T, typename = void>
    struct HasIntegerPackedComponents : std::false_type
    {
    };
    template <typename T>
    struct HasIntegerPackedComponents<T, std::void_t<decltype( PackedValueTraits<T>::kIntegerComponents )>> : std::bool_constant<PackedValueTraits<T>::kIntegerComponents>
    {
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 앞쪽 `kComponentCount` 개 성분이 모두 `T` 의 범위 안(경계 포함)인지입니다. NaN 과 무한대는 범위 밖입니다.
     * @details 읽는 쪽(글 · 인자 변환)이 `fromFloats` 에 넘기기 전에 부릅니다 — `fromFloats` 는 범위 밖을 자르거나(`unorm`) 무한대로 보내므로(`half`),
     *          그대로 두면 "5" 를 받은 `unorm8` 이 말없이 1 이 됩니다.
     */
    template <typename T>
    [[nodiscard]] constexpr bool isPackedFloatsInRange( const PackedFloats& floats ) noexcept
    {
        using Traits = PackedValueTraits<T>;
        for ( uint32 index = 0; index < Traits::kComponentCount; ++index )
        {
            const float32 value = floats._arrValue[index];
            if ( ( Traits::kRangeMin <= value && value <= Traits::kRangeMax ) == false )
                return false;
            if constexpr ( HasIntegerPackedComponents<T>::value )
            {
                if ( static_cast<float32>( static_cast<int32>( value ) ) != value )
                    return false;
            }
        }
        return true;
    }
} // namespace sw
