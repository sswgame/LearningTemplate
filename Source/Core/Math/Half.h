/**
 * @file Half.h
 * @brief 16 비트 부동소수점 `half`(IEEE 754 binary16)와 묶음 `half2` · `half3` · `half4` 입니다 — **저장 전용** 압축 타입입니다.
 * @details 정점 · 인스턴스 · 파일에 담는 크기를 줄이는 것이 목적이라 **산술 연산자를 일부러 주지 않습니다**. 계산은 `toFloat` 로 `float32` 로 풀어서 하고,
 *          결과를 `fromFloat` 로 다시 담습니다. 암시적 변환도 없어서 `half + half` 나 `float32 x = h` 는 컴파일되지 않습니다.
 *          - 변환은 비트 연산만 씁니다(F16C · NEON · 컴파일러 `_Float16` 에 기대지 않음). 그래서 어느 플랫폼 · 구성에서도 같은 입력이 같은 비트가 되고 `constexpr` 입니다.
 *          - `fromFloat` 는 가장 가까운 값으로 반올림하되 정확히 가운데이면 짝수 쪽으로 갑니다(IEEE 기본). 65520 이상은 무한대, 2^-25 이하는 0(부호 유지),
 *            그 사이의 작은 값은 비정규수가 됩니다. NaN 은 NaN(조용한 NaN, 위쪽 10 비트 페이로드 유지)으로 갑니다.
 *          - **정밀도**: 가수 10 비트라 정규수의 상대 오차는 2^-11(약 0.05%)입니다. 정수는 2048 까지만 정확하고, 65504 가 최대입니다.
 *            **월드 좌표 · 누적 시간 · 큰 범위 값에는 쓰지 않습니다** — 1024 근처에서 이미 단위 간격이 1 이라 움직임이 계단져 보입니다.
 *            쓰기 좋은 값은 UV · 색 · 0 근처의 작은 오프셋 · 속도 · 가중치입니다. 위치는 경계 정규화 `snorm16`, 법선은 `OctahedralNormal` 을 먼저 봅니다.
 *          - 크기 · 정렬은 아래 `static_assert` 가 못 박습니다(CPU · GPU 구조체 미러에 쓸 수 있도록 패딩이 없다). 셰이더 쪽은 `R16_FLOAT` · `R16G16_FLOAT` ·
 *            `R16G16B16A16_FLOAT` 정점 · 텍스처 형식이 하드웨어로 풉니다. 상용 엔진의 대응: 언리얼 `FFloat16` · `FVector2DHalf`, 유니티 `half`.
 */
#pragma once
#include "Core/Foundation/StdHeaders.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/BitUtil.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    /**
     * @struct half
     * @brief IEEE 754 binary16(부호 1 · 지수 5 · 가수 10 비트) 값 하나입니다. 비트 패턴 `_bits` 만 담습니다.
     */
    struct half final
    {
        /** @brief 담을 수 있는 가장 큰 유한값(65504)입니다. */
        static constexpr float32 kMaxValue = 65504.0f;
        /** @brief 가장 작은 정규수(2^-14)입니다. 이보다 작으면 비정규수라 상대 오차가 커집니다. */
        static constexpr float32 kMinNormalValue = 6.103515625e-5f;
        /** @brief 가장 작은 양수(비정규수 2^-24)입니다. */
        static constexpr float32 kMinSubnormalValue = 5.9604644775390625e-8f;

        uint16 _bits{ 0 };

        /** @brief 비트 패턴을 그대로 담은 값입니다(파일 · GPU 에서 읽은 값을 감쌀 때). */
        [[nodiscard]] static constexpr half fromBits( const uint16 bits ) noexcept
        {
            half result{};
            result._bits = bits;
            return result;
        }

        /**
         * @brief @p value 를 가장 가까운 half 로 반올림합니다(가운데는 짝수 쪽). 범위를 넘으면 무한대, 너무 작으면 0, NaN 은 NaN 입니다.
         */
        [[nodiscard]] static constexpr half fromFloat( const float32 value ) noexcept
        {
            const uint32 floatBits    = BitUtil::floatToBits( value );
            const uint32 absoluteBits = floatBits & kFloatAbsoluteMask;
            const uint32 signBits     = ( floatBits >> kSignShift ) & kSignMask;
            uint32       resultBits   = 0;
            if ( absoluteBits > kFloatInfinityBits )
            {
                resultBits = kExponentMask | kQuietNaNBit | ( ( absoluteBits >> kMantissaShift ) & kMantissaMask );
            }
            else if ( absoluteBits >= kFloatOverflowBits )
            {
                resultBits = kExponentMask;
            }
            else if ( absoluteBits >= kFloatMinNormalBits )
            {
                resultBits = shiftRightRoundEven( absoluteBits - kFloatRebiasBits, kMantissaShift );
            }
            else if ( absoluteBits > kFloatUnderflowBits )
            {
                const uint32 mantissa = ( absoluteBits & kFloatMantissaMask ) | kFloatImplicitOne;
                const uint32 exponent = absoluteBits >> kFloatMantissaBits;
                resultBits            = shiftRightRoundEven( mantissa, kSubnormalShiftBase - exponent );
            }
            return fromBits( static_cast<uint16>( signBits | resultBits ) );
        }

        /** @brief float32 로 풉니다. 모든 half 값(비정규수 · 무한대 · NaN 포함)이 정확히 표현됩니다. */
        [[nodiscard]] constexpr float32 toFloat() const noexcept
        {
            const uint32 bits     = _bits;
            const uint32 signBits = ( bits & kSignMask ) << kSignShift;
            const uint32 exponent = ( bits & kExponentMask ) >> kMantissaBits;
            const uint32 mantissa = bits & kMantissaMask;
            uint32       resultBits{ 0 };
            if ( exponent == kExponentAllOnes )
            {
                resultBits = kFloatInfinityBits | ( mantissa << kMantissaShift );
            }
            else if ( exponent != 0 )
            {
                resultBits = ( ( exponent + kExponentRebias ) << kFloatMantissaBits ) | ( mantissa << kMantissaShift );
            }
            else
            {
                // 0 또는 비정규수: 가수 × 2^-24 가 정확히 나온다.
                resultBits = BitUtil::floatToBits( static_cast<float32>( mantissa ) * kMinSubnormalValue );
            }
            return BitUtil::bitsToFloat( signBits | resultBits );
        }

        /**
         * @brief 유한값은 `toFloat` 와 같고, 무한대 · NaN 은 부호 있는 float32 최대값으로 풉니다.
         * @details 무한대 · NaN 이 거르기 · 합산으로 퍼지면 한 픽셀이 화면 전체를 오염시킨다 — 그런 곳(반사 프로브 캡처 읽기)이 쓴다. NaN 의 부호는 부호 비트를 따른다.
         */
        [[nodiscard]] constexpr float32 toFiniteFloat() const noexcept
        {
            if ( isFinite() )
                return toFloat();
            return ( _bits & kSignMask ) != 0u ? -std::numeric_limits<float32>::max() : std::numeric_limits<float32>::max();
        }

        /** @brief 비트 패턴입니다. */
        [[nodiscard]] constexpr uint16 getBits() const noexcept { return _bits; }
        /** @brief NaN 인가(지수가 모두 1 이고 가수가 0 이 아님). */
        [[nodiscard]] constexpr bool isNaN() const noexcept
        {
            const uint32 bits = _bits;
            return ( bits & kExponentMask ) == kExponentMask && ( bits & kMantissaMask ) != 0;
        }
        /** @brief ±무한대인가. */
        [[nodiscard]] constexpr bool isInfinity() const noexcept
        {
            const uint32 bits = _bits;
            return ( bits & ~kSignMask ) == kExponentMask;
        }
        /** @brief 유한한 값인가(NaN 도 무한대도 아님). */
        [[nodiscard]] constexpr bool isFinite() const noexcept
        {
            const uint32 bits = _bits;
            return ( bits & kExponentMask ) != kExponentMask;
        }

    private:
        // half 쪽 비트 배치
        static constexpr uint32 kSignMask        = 0x8000u;
        static constexpr uint32 kExponentMask    = 0x7C00u;
        static constexpr uint32 kMantissaMask    = 0x03FFu;
        static constexpr uint32 kQuietNaNBit     = 0x0200u;
        static constexpr uint32 kMantissaBits    = 10u;
        static constexpr uint32 kExponentAllOnes = 31u;
        static constexpr uint32 kExponentRebias  = 112u; ///< float32 지수 편향 127 - half 지수 편향 15
        // float32 쪽 비트 배치와 경계
        static constexpr uint32 kSignShift          = 16u; ///< float32 부호 비트(31)를 half 부호 비트(15)로 옮기는 거리
        static constexpr uint32 kMantissaShift      = 13u; ///< 가수 23 비트 - 10 비트
        static constexpr uint32 kFloatMantissaBits  = 23u;
        static constexpr uint32 kFloatMantissaMask  = 0x007FFFFFu;
        static constexpr uint32 kFloatImplicitOne   = 0x00800000u;
        static constexpr uint32 kFloatAbsoluteMask  = 0x7FFFFFFFu;
        static constexpr uint32 kFloatInfinityBits  = 0x7F800000u;
        static constexpr uint32 kFloatOverflowBits  = 0x477FF000u; ///< 65520: 65504 와 65536(무한대) 의 한가운데 — 이상이면 짝수 쪽인 무한대로 간다.
        static constexpr uint32 kFloatMinNormalBits = 0x38800000u; ///< 2^-14: 이상이면 half 정규수
        static constexpr uint32 kFloatUnderflowBits = 0x33000000u; ///< 2^-25: 가장 작은 비정규수의 절반 — 이하이면 0 (가운데는 짝수인 0)
        static constexpr uint32 kFloatRebiasBits    = 0x38000000u; ///< kExponentRebias << 23
        static constexpr uint32 kSubnormalShiftBase = 126u;        ///< 비정규수 가수 오른쪽 이동 = 126 - float32 지수

        /** @brief @p value 를 오른쪽으로 @p shift 칸 밀고, 버려진 부분이 절반을 넘으면(가운데면 짝수 쪽으로) 올립니다. 올림이 지수로 넘어가는 것은 의도한 동작입니다. */
        [[nodiscard]] static constexpr uint32 shiftRightRoundEven( const uint32 value, const uint32 shift ) noexcept
        {
            const uint32 truncated = value >> shift;
            const uint32 remainder = value & ( ( 1u << shift ) - 1u );
            const uint32 halfway   = 1u << ( shift - 1u );
            const bool   bTie      = remainder == halfway;
            const bool   bOdd      = ( truncated & 1u ) != 0u;
            const bool   bRoundUp  = remainder > halfway || ( bTie && bOdd );
            return bRoundUp ? truncated + 1u : truncated;
        }
    };
} // namespace sw

namespace sw
{
    /** @brief half 두 개(x, y)입니다. 저장 전용 — 계산은 `toFloat2` 로 풀어서 합니다. */
    struct half2 final
    {
        half _x{};
        half _y{};

        [[nodiscard]] static constexpr half2 fromFloat2( const float2& value ) noexcept { return half2{ half::fromFloat( value._x ), half::fromFloat( value._y ) }; }
        [[nodiscard]] constexpr float2       toFloat2() const noexcept { return float2{ _x.toFloat(), _y.toFloat() }; }
    };
} // namespace sw

namespace sw
{
    /** @brief half 세 개(x, y, z)입니다. 6 바이트라 배열 원소 사이에 패딩이 없습니다(정렬은 2). 저장 전용입니다. */
    struct half3 final
    {
        half _x{};
        half _y{};
        half _z{};

        [[nodiscard]] static constexpr half3 fromFloat3( const float3& value ) noexcept
        {
            return half3{ half::fromFloat( value._x ), half::fromFloat( value._y ), half::fromFloat( value._z ) };
        }
        [[nodiscard]] constexpr float3 toFloat3() const noexcept { return float3{ _x.toFloat(), _y.toFloat(), _z.toFloat() }; }
    };
} // namespace sw

namespace sw
{
    /** @brief half 네 개(x, y, z, w)입니다. 저장 전용입니다. */
    struct half4 final
    {
        half _x{};
        half _y{};
        half _z{};
        half _w{};

        [[nodiscard]] static constexpr half4 fromFloat4( const float4& value ) noexcept
        {
            return half4{ half::fromFloat( value._x ), half::fromFloat( value._y ), half::fromFloat( value._z ), half::fromFloat( value._w ) };
        }
        [[nodiscard]] constexpr float4 toFloat4() const noexcept { return float4{ _x.toFloat(), _y.toFloat(), _z.toFloat(), _w.toFloat() }; }
    };

    // CPU · GPU 구조체 미러에 쓰이므로 크기 · 정렬을 못 박는다(패딩 없음, 비트 복사 가능).
    static_assert( sizeof( half ) == 2 && alignof( half ) == 2, "half 는 2 바이트다" );
    static_assert( sizeof( half2 ) == 4 && alignof( half2 ) == 2, "half2 는 4 바이트다" );
    static_assert( sizeof( half3 ) == 6 && alignof( half3 ) == 2, "half3 는 6 바이트다" );
    static_assert( sizeof( half4 ) == 8 && alignof( half4 ) == 2, "half4 는 8 바이트다" );
    static_assert( std::is_trivially_copyable_v<half> && std::is_standard_layout_v<half>, "half 는 비트 복사가 가능해야 한다" );
    static_assert( std::is_trivially_copyable_v<half4> && std::is_standard_layout_v<half4>, "half4 는 비트 복사가 가능해야 한다" );
    static_assert( half::fromFloat( 1.0f ).getBits() == 0x3C00u && half::fromBits( 0x3C00u ).toFloat() == 1.0f, "half 1.0 은 0x3C00 이다" );
} // namespace sw
