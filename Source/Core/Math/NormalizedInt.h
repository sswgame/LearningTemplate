/**
 * @file NormalizedInt.h
 * @brief 정수 하나에 [0, 1] 또는 [-1, 1] 실수를 담는 저장 전용 타입 `unorm8` · `unorm16` · `snorm8` · `snorm16` 과 묶음 `unorm8x4` · `snorm8x4` · `unorm16x2` ·
 *        `snorm16x2` · `snorm16x4` 입니다.
 * @details 산술 연산자도 암시적 변환도 없습니다 — 계산은 `toFloat` 로 `float32` 로 풀어서 하고 결과를 `fromFloat` 로 담습니다(`Half.h` 와 같은 규칙).
 *          - **unorm**: 0..최대 정수가 0..1 입니다(D3D `UNORM` · GL `normalized` 정수 속성). 값은 0..1 로 잘리고 NaN 은 0 입니다. 단계는 1/255 · 1/65535 이고
 *            왕복 오차는 반 단계 이하입니다. 색(`unorm8x4`), 0..1 에 갇힌 UV · 가중치에 맞습니다.
 *          - **snorm**: -최대..최대 정수가 -1..1 이고 0 이 정확히 0 입니다(D3D `SNORM` · GL 과 같은 규칙). 정수 최솟값(-128 · -32768)은 -1 로 풀어서 -최대와 같으므로
 *            `fromFloat` 는 그 값을 만들지 않습니다. 값은 -1..1 로 잘리고 NaN 은 0 입니다. 위치(경계 정규화) · 노멀 · 탄젠트에 맞습니다.
 *          - 실수 → 정수는 가장 가까운 값으로 반올림하고 가운데는 0 에서 먼 쪽입니다. 곱셈과 덧셈을 문장 둘로 나눠서 컴파일러의 FMA 융합이 결과 비트를
 *            바꾸지 못하게 합니다(같은 입력은 어느 플랫폼에서나 같은 비트).
 *          - 크기 · 정렬은 `static_assert` 가 못 박습니다(패딩 없음 — CPU · GPU 구조체 미러용). 묶음의 정렬은 성분 하나의 정렬이라 `unorm8x4` 는 1 바이트
 *            정렬이고, GPU 가 4 바이트 정렬을 요구하는 자리는 쓰는 구조체가 맞춥니다.
 *          - 어디에 쓰는지, 무엇을 쓰면 안 되는지는 `docs/plans/PackedTypes.md` 의 규칙을 따릅니다. 상용 엔진의 대응: 언리얼 `FColor` · `FPackedNormal`, 유니티 `UNorm8` ·
 *            `SNorm16` 정점 속성 형식.
 */
#pragma once
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    /**
     * @struct NormalizedMath
     * @brief 정규화 정수의 양자화 한 걸음입니다. `unorm` · `snorm` · `RGB10A2` 가 같은 반올림을 쓰도록 한 곳에 둡니다.
     */
    struct NormalizedMath
    {
        /** @brief [0, 1] 을 0..@p maxValue 로 양자화합니다. 범위 밖은 자르고 NaN 은 0 입니다. */
        [[nodiscard]] static constexpr uint32 quantizeUnorm( const float32 value, const uint32 maxValue ) noexcept
        {
            if ( value >= 1.0f )
                return maxValue;
            if ( value > 0.0f )
            {
                const float32 scaled  = value * static_cast<float32>( maxValue );
                const float32 rounded = scaled + 0.5f;
                return static_cast<uint32>( rounded );
            }
            return 0u;
        }

        /** @brief [-1, 1] 을 -@p maxValue..@p maxValue 로 양자화합니다. 범위 밖은 자르고 NaN 은 0 입니다. */
        [[nodiscard]] static constexpr int32 quantizeSnorm( const float32 value, const int32 maxValue ) noexcept
        {
            if ( value >= 1.0f )
                return maxValue;
            if ( value <= -1.0f )
                return -maxValue;
            if ( value > 0.0f )
                return static_cast<int32>( quantizeUnorm( value, static_cast<uint32>( maxValue ) ) );
            if ( value < 0.0f )
                return -static_cast<int32>( quantizeUnorm( -value, static_cast<uint32>( maxValue ) ) );
            return 0;
        }
    };
} // namespace sw

namespace sw
{
    /**
     * @struct unorm
     * @brief 부호 없는 정수 @p TStorage 에 [0, 1] 을 담는 값입니다(`unorm8` · `unorm16`).
     */
    template <typename TStorage>
    struct unorm final
    {
        static_assert( std::is_unsigned_v<TStorage>, "unorm 의 저장 정수는 부호 없는 정수다" );

        /** @brief 1.0 을 나타내는 정수입니다(255 · 65535). */
        static constexpr TStorage kMaxRaw = std::numeric_limits<TStorage>::max();

        TStorage _value{ 0 };

        /** @brief 정수 값을 그대로 담습니다(파일 · GPU 에서 읽은 값을 감쌀 때). */
        [[nodiscard]] static constexpr unorm fromRaw( const TStorage raw ) noexcept
        {
            unorm result{};
            result._value = raw;
            return result;
        }
        /** @brief @p value 를 [0, 1] 로 잘라 가장 가까운 단계로 담습니다. NaN 은 0 입니다. */
        [[nodiscard]] static constexpr unorm fromFloat( const float32 value ) noexcept
        {
            return fromRaw( static_cast<TStorage>( NormalizedMath::quantizeUnorm( value, kMaxRaw ) ) );
        }
        /** @brief [0, 1] 실수로 풉니다. 0 → 0.0, 최대 정수 → 정확히 1.0 입니다. */
        [[nodiscard]] constexpr float32 toFloat() const noexcept { return static_cast<float32>( _value ) / static_cast<float32>( kMaxRaw ); }
        /** @brief 정수 값입니다. */
        [[nodiscard]] constexpr TStorage getRaw() const noexcept { return _value; }
    };
} // namespace sw

namespace sw
{
    /**
     * @struct snorm
     * @brief 부호 있는 정수 @p TStorage 에 [-1, 1] 을 담는 값입니다(`snorm8` · `snorm16`). 0 이 정확히 0 입니다.
     */
    template <typename TStorage>
    struct snorm final
    {
        static_assert( std::is_signed_v<TStorage> && std::is_integral_v<TStorage>, "snorm 의 저장 정수는 부호 있는 정수다" );

        /** @brief 1.0 을 나타내는 정수입니다(127 · 32767). */
        static constexpr TStorage kMaxRaw = std::numeric_limits<TStorage>::max();

        TStorage _value{ 0 };

        /** @brief 정수 값을 그대로 담습니다. 정수 최솟값은 풀 때 -1.0 이 됩니다(-최대 와 같은 값). */
        [[nodiscard]] static constexpr snorm fromRaw( const TStorage raw ) noexcept
        {
            snorm result{};
            result._value = raw;
            return result;
        }
        /** @brief @p value 를 [-1, 1] 로 잘라 가장 가까운 단계로 담습니다. NaN 은 0 이고 정수 최솟값은 만들지 않습니다. */
        [[nodiscard]] static constexpr snorm fromFloat( const float32 value ) noexcept
        {
            return fromRaw( static_cast<TStorage>( NormalizedMath::quantizeSnorm( value, static_cast<int32>( kMaxRaw ) ) ) );
        }
        /** @brief [-1, 1] 실수로 풉니다. 최대 정수 → 정확히 1.0, 정수 최솟값 → -1.0 입니다. */
        [[nodiscard]] constexpr float32 toFloat() const noexcept
        {
            const float32 unclamped = static_cast<float32>( _value ) / static_cast<float32>( kMaxRaw );
            return unclamped < -1.0f ? -1.0f : unclamped;
        }
        /** @brief 정수 값입니다. */
        [[nodiscard]] constexpr TStorage getRaw() const noexcept { return _value; }
    };
} // namespace sw

namespace sw
{
    using unorm8  = unorm<uint8>;
    using unorm16 = unorm<uint16>;
    using snorm8  = snorm<int8>;
    using snorm16 = snorm<int16>;

    /**
     * @struct NormalizedVector
     * @brief 같은 정규화 정수 @p kCount 개의 묶음입니다(`unorm8x4` 등). 성분 순서는 x, y, z, w(색이면 r, g, b, a)이고 메모리에 그 순서로 놓입니다.
     */
    template <typename TScalar, uint32 kCount>
    struct NormalizedVector final
    {
        static_assert( 2 <= kCount && kCount <= 4, "묶음은 2 ~ 4 개다" );

        TScalar _arrValue[kCount]{};

        /** @brief 실수 @p kCount 개를 성분마다 양자화합니다. */
        [[nodiscard]] static constexpr NormalizedVector fromFloats( const float32 ( &arrValue )[kCount] ) noexcept
        {
            NormalizedVector result{};
            for ( uint32 index = 0; index < kCount; ++index )
            {
                result._arrValue[index] = TScalar::fromFloat( arrValue[index] );
            }
            return result;
        }
        /** @brief 성분마다 실수로 풉니다. */
        constexpr void toFloats( float32 ( &outArrValue )[kCount] ) const noexcept
        {
            for ( uint32 index = 0; index < kCount; ++index )
            {
                outArrValue[index] = _arrValue[index].toFloat();
            }
        }

        /** @brief `float2` 에서 만듭니다(두 개짜리 묶음만). */
        [[nodiscard]] static constexpr NormalizedVector fromFloat2( const float2& value ) noexcept
        {
            static_assert( kCount == 2, "fromFloat2 는 두 개짜리 묶음의 것이다" );
            const float32 arrValue[2]{ value._x, value._y };
            return fromFloats( arrValue );
        }
        /** @brief `float4`(x, y, z, w)에서 만듭니다(네 개짜리 묶음만). */
        [[nodiscard]] static constexpr NormalizedVector fromFloat4( const float4& value ) noexcept
        {
            static_assert( kCount == 4, "fromFloat4 는 네 개짜리 묶음의 것이다" );
            const float32 arrValue[4]{ value._x, value._y, value._z, value._w };
            return fromFloats( arrValue );
        }
        /** @brief `float2` 로 풉니다(두 개짜리 묶음만). */
        [[nodiscard]] constexpr float2 toFloat2() const noexcept
        {
            static_assert( kCount == 2, "toFloat2 는 두 개짜리 묶음의 것이다" );
            return float2{ _arrValue[0].toFloat(), _arrValue[1].toFloat() };
        }
        /** @brief `float4` 로 풉니다(네 개짜리 묶음만). */
        [[nodiscard]] constexpr float4 toFloat4() const noexcept
        {
            static_assert( kCount == 4, "toFloat4 는 네 개짜리 묶음의 것이다" );
            return float4{ _arrValue[0].toFloat(), _arrValue[1].toFloat(), _arrValue[2].toFloat(), _arrValue[3].toFloat() };
        }
    };

    using unorm8x4  = NormalizedVector<unorm8, 4>;
    using snorm8x4  = NormalizedVector<snorm8, 4>;
    using unorm16x2 = NormalizedVector<unorm16, 2>;
    using snorm16x2 = NormalizedVector<snorm16, 2>;
    using snorm16x4 = NormalizedVector<snorm16, 4>;

    // CPU · GPU 구조체 미러에 쓰이므로 크기 · 정렬을 못 박는다(패딩 없음, 비트 복사 가능).
    static_assert( sizeof( unorm8 ) == 1 && alignof( unorm8 ) == 1, "unorm8 은 1 바이트다" );
    static_assert( sizeof( snorm8 ) == 1 && alignof( snorm8 ) == 1, "snorm8 은 1 바이트다" );
    static_assert( sizeof( unorm16 ) == 2 && alignof( unorm16 ) == 2, "unorm16 은 2 바이트다" );
    static_assert( sizeof( snorm16 ) == 2 && alignof( snorm16 ) == 2, "snorm16 은 2 바이트다" );
    static_assert( sizeof( unorm8x4 ) == 4 && alignof( unorm8x4 ) == 1, "unorm8x4 는 4 바이트다" );
    static_assert( sizeof( snorm8x4 ) == 4 && alignof( snorm8x4 ) == 1, "snorm8x4 는 4 바이트다" );
    static_assert( sizeof( unorm16x2 ) == 4 && alignof( unorm16x2 ) == 2, "unorm16x2 는 4 바이트다" );
    static_assert( sizeof( snorm16x2 ) == 4 && alignof( snorm16x2 ) == 2, "snorm16x2 는 4 바이트다" );
    static_assert( sizeof( snorm16x4 ) == 8 && alignof( snorm16x4 ) == 2, "snorm16x4 는 8 바이트다" );
    static_assert( std::is_trivially_copyable_v<unorm8x4> && std::is_standard_layout_v<unorm8x4>, "unorm8x4 는 비트 복사가 가능해야 한다" );
    static_assert( std::is_trivially_copyable_v<snorm16x4> && std::is_standard_layout_v<snorm16x4>, "snorm16x4 는 비트 복사가 가능해야 한다" );
    static_assert( unorm8::fromFloat( 1.0f ).getRaw() == 255 && snorm16::fromFloat( -1.0f ).getRaw() == -32767, "끝값은 정수 끝값에 닿는다" );
} // namespace sw
