/**
 * @file PackUtil.h
 * @brief 바이트 · 비트 패킹: 32 비트 단어 하나와 실수 묶음 사이를 오가는 static 도우미 `PackUtil` 입니다 — GPU 구조체 · 버퍼 단어를 CPU 에서 만들고 읽을 때 씁니다.
 * @details **짝**: 셰이더 `Content/engine/shaders/pack.hlsli` 의 `PackUtil` 과 같은 이름 · 같은 인자 순서 · 같은 비트 배치입니다. 한쪽을 고치면 다른 쪽도 고칩니다.
 *          단어 안 순서는 모두 **x(첫 성분)가 아래 비트**입니다(DXGI R8G8B8A8 · R16G16 · R10G10B10A2 · R9G9B9E5 와 같은 배치, 리틀 엔디언 메모리의 바이트 순서).
 *          성분 하나의 양자화는 저장 타입(`unorm` · `snorm` · `half` · `RGB10A2`)이 정본이고, 이 도우미는 그것을 단어로 묶습니다 — 규칙이 한 곳에만 있습니다.
 *          상용 엔진의 대응: 언리얼 `BitPacking.ush` · `PackUnitVector` 와 C++ `FPackedNormal` 짝, DirectXMath `XMConvertFloatToHalf`.
 *          - **unorm**(8 · 16 · RGB10A2): [0, 1] 로 자르고 가장 가까운 단계, 가운데는 위로(0 에서 먼 쪽). NaN 은 0, 음의 0 은 0.
 *          - **snorm**(8 · 16): [-1, 1] 로 자르고 가장 가까운 단계, 가운데는 0 에서 먼 쪽. NaN 은 0, 음의 0 은 0. 정수 최솟값은 만들지 않고 풀면 -1.
 *          - **half**: IEEE 754 binary16, 가장 가까운 값이고 가운데는 짝수 쪽. 65520 이상은 무한대, 2^-25 이하는 0(부호 유지 — 음의 0 은 0x8000),
 *            그 사이는 비정규수, NaN 은 조용한 NaN(`Half.h`). 셰이더도 f32tof16 이 아니라 같은 비트 연산이라 비트까지 같습니다.
 *          - **RGB9E5**: 음수는 0, 65408 위는 65408 로 자릅니다. NaN 입력은 정의하지 않습니다(부르는 쪽이 거릅니다). 담기는 CPU 만, 풀기는 양쪽입니다.
 *          - 담기(실수 → 정수)는 같은 입력이면 셰이더와 비트까지 같고, 풀기(정수 → 실수)는 같은 식(정수 / 최댓값)이지만 GPU 나눗셈은 2.5 ulp 까지 허용되어
 *            ulp 안에서 다를 수 있습니다.
 *          - `extractBits` · `insertBits` 의 count 는 1..32, offset + count <= 32 입니다(셰이더에서 시프트 32 를 피하는 꼴과 같게).
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/Half.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/NormalizedInt.h"
#include "Core/Math/RGB10A2.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    /**
     * @struct PackUtil
     * @brief 32 비트 단어 하나와 실수 묶음 사이의 패킹입니다(셰이더 `pack.hlsli` 의 `PackUtil` 짝).
     */
    struct PackUtil
    {
        /** @brief @p value 의 @p offset 비트부터 @p count 비트를 꺼냅니다. */
        [[nodiscard]] static constexpr uint32 extractBits( const uint32 value, const uint32 offset, const uint32 count ) noexcept
        {
            return ( value >> offset ) & computeFieldMask( count );
        }
        /** @brief @p value 의 @p offset 비트부터 @p count 비트를 @p field 로 바꿉니다(넘치는 위쪽 비트는 버립니다). */
        [[nodiscard]] static constexpr uint32 insertBits( const uint32 value, const uint32 field, const uint32 offset, const uint32 count ) noexcept
        {
            const uint32 mask = computeFieldMask( count ) << offset;
            return ( value & ~mask ) | ( ( field << offset ) & mask );
        }

        /** @brief 네 성분을 unorm8 넷으로 묶습니다(x 가 아래 바이트 — R8G8B8A8_UNORM). */
        [[nodiscard]] static constexpr uint32 packUnorm8x4( const float4& value ) noexcept
        {
            return NormalizedMath::quantizeUnorm( value._x, unorm8::kMaxRaw ) | ( NormalizedMath::quantizeUnorm( value._y, unorm8::kMaxRaw ) << 8 ) |
                   ( NormalizedMath::quantizeUnorm( value._z, unorm8::kMaxRaw ) << 16 ) | ( NormalizedMath::quantizeUnorm( value._w, unorm8::kMaxRaw ) << 24 );
        }
        /** @brief `packUnorm8x4` 의 역입니다. */
        [[nodiscard]] static constexpr float4 unpackUnorm8x4( const uint32 packed ) noexcept
        {
            return float4{ toUnorm8( packed ), toUnorm8( packed >> 8 ), toUnorm8( packed >> 16 ), toUnorm8( packed >> 24 ) };
        }

        /** @brief 네 성분을 snorm8 넷으로 묶습니다(x 가 아래 바이트 — R8G8B8A8_SNORM). */
        [[nodiscard]] static constexpr uint32 packSnorm8x4( const float4& value ) noexcept
        {
            return toSnorm8Bits( value._x ) | ( toSnorm8Bits( value._y ) << 8 ) | ( toSnorm8Bits( value._z ) << 16 ) | ( toSnorm8Bits( value._w ) << 24 );
        }
        /** @brief `packSnorm8x4` 의 역입니다. */
        [[nodiscard]] static constexpr float4 unpackSnorm8x4( const uint32 packed ) noexcept
        {
            return float4{ toSnorm8( packed ), toSnorm8( packed >> 8 ), toSnorm8( packed >> 16 ), toSnorm8( packed >> 24 ) };
        }

        /** @brief 두 성분을 unorm16 둘로 묶습니다(x 가 아래 16 비트 — R16G16_UNORM). */
        [[nodiscard]] static constexpr uint32 packUnorm16x2( const float2& value ) noexcept
        {
            return NormalizedMath::quantizeUnorm( value._x, unorm16::kMaxRaw ) | ( NormalizedMath::quantizeUnorm( value._y, unorm16::kMaxRaw ) << 16 );
        }
        /** @brief `packUnorm16x2` 의 역입니다. */
        [[nodiscard]] static constexpr float2 unpackUnorm16x2( const uint32 packed ) noexcept
        {
            return float2{ unorm16::fromRaw( static_cast<uint16>( packed ) ).toFloat(), unorm16::fromRaw( static_cast<uint16>( packed >> 16 ) ).toFloat() };
        }

        /** @brief 두 성분을 snorm16 둘로 묶습니다(x 가 아래 16 비트 — R16G16_SNORM). */
        [[nodiscard]] static constexpr uint32 packSnorm16x2( const float2& value ) noexcept
        {
            return toSnorm16Bits( value._x ) | ( toSnorm16Bits( value._y ) << 16 );
        }
        /** @brief `packSnorm16x2` 의 역입니다. */
        [[nodiscard]] static constexpr float2 unpackSnorm16x2( const uint32 packed ) noexcept
        {
            return float2{ snorm16::fromRaw( static_cast<int16>( static_cast<uint16>( packed ) ) ).toFloat(),
                           snorm16::fromRaw( static_cast<int16>( static_cast<uint16>( packed >> 16 ) ) ).toFloat() };
        }

        /** @brief RGB 10 비트 셋 + A 2 비트로 묶습니다(R 이 아래 비트 — R10G10B10A2_UNORM). */
        [[nodiscard]] static constexpr uint32 packRGB10A2( const float4& value ) noexcept { return RGB10A2::fromFloat4( value ).getPacked(); }
        /** @brief `packRGB10A2` 의 역입니다. */
        [[nodiscard]] static constexpr float4 unpackRGB10A2( const uint32 packed ) noexcept { return RGB10A2::fromPacked( packed ).toFloat4(); }

        /** @brief float32 → half 비트(아래 16 비트)입니다. 셰이더 `PackUtil::packHalf` 의 짝입니다. */
        [[nodiscard]] static constexpr uint32 packHalf( const float32 value ) noexcept { return half::fromFloat( value ).getBits(); }
        /** @brief half 비트(아래 16 비트) → float32 입니다. 위쪽 16 비트는 무시합니다. */
        [[nodiscard]] static constexpr float32 unpackHalf( const uint32 bits ) noexcept { return half::fromBits( static_cast<uint16>( bits ) ).toFloat(); }
        /** @brief 두 성분을 half 둘로 묶습니다(x 가 아래 16 비트 — R16G16_FLOAT). */
        [[nodiscard]] static constexpr uint32 packHalf2x16( const float2& value ) noexcept { return packHalf( value._x ) | ( packHalf( value._y ) << 16 ); }
        /** @brief `packHalf2x16` 의 역입니다. */
        [[nodiscard]] static constexpr float2 unpackHalf2x16( const uint32 packed ) noexcept { return float2{ unpackHalf( packed ), unpackHalf( packed >> 16 ) }; }

        /** @brief RGB(음수 아님)를 공유 지수 32 비트(R9G9B9E5_SHAREDEXP — 가수 9 비트 셋, 위 5 비트 지수 · 편향 15)로 접습니다. 음수는 0, 65408 위는 65408 입니다. */
        [[nodiscard]] static uint32 packRGB9E5( const float3& value ) noexcept
        {
            const float32 red        = MathUtil::clamp( value._x, 0.0f, kRGB9E5MaxValue );
            const float32 green      = MathUtil::clamp( value._y, 0.0f, kRGB9E5MaxValue );
            const float32 blue       = MathUtil::clamp( value._z, 0.0f, kRGB9E5MaxValue );
            const float32 maxChannel = MathUtil::max( red, MathUtil::max( green, blue ) );
            if ( maxChannel <= 0.0f )
                return 0u;
            int32 sharedExponent = MathUtil::max( -kRGB9E5ExponentBias - 1, static_cast<int32>( MathUtil::floor( MathUtil::log2( maxChannel ) ) ) ) + 1 + kRGB9E5ExponentBias;
            if ( MathUtil::floor( maxChannel / computeRGB9E5Scale( sharedExponent ) + 0.5f ) > static_cast<float32>( kRGB9E5MantissaMask ) )
                ++sharedExponent;
            const float32 scale = computeRGB9E5Scale( sharedExponent );
            return quantizeRGB9E5Mantissa( red, scale ) | ( quantizeRGB9E5Mantissa( green, scale ) << kRGB9E5MantissaBits ) |
                   ( quantizeRGB9E5Mantissa( blue, scale ) << ( kRGB9E5MantissaBits * 2u ) ) | ( static_cast<uint32>( sharedExponent ) << kRGB9E5ExponentShift );
        }
        /** @brief `packRGB9E5` 의 역입니다(셰이더 `PackUtil::unpackRGB9E5` 와 같은 식). */
        [[nodiscard]] static float3 unpackRGB9E5( const uint32 packed ) noexcept
        {
            const float32 scale = computeRGB9E5Scale( static_cast<int32>( packed >> kRGB9E5ExponentShift ) );
            return float3{ static_cast<float32>( extractBits( packed, 0u, kRGB9E5MantissaBits ) ) * scale,
                           static_cast<float32>( extractBits( packed, kRGB9E5MantissaBits, kRGB9E5MantissaBits ) ) * scale,
                           static_cast<float32>( extractBits( packed, kRGB9E5MantissaBits * 2u, kRGB9E5MantissaBits ) ) * scale };
        }

    private:
        static constexpr uint32  kRGB9E5MantissaBits  = 9u;
        static constexpr uint32  kRGB9E5MantissaMask  = ( 1u << kRGB9E5MantissaBits ) - 1u;
        static constexpr uint32  kRGB9E5ExponentShift = kRGB9E5MantissaBits * 3u;
        static constexpr int32   kRGB9E5ExponentBias  = 15;
        static constexpr float32 kRGB9E5MaxValue      = 65408.0f; ///< (2^9 - 1) / 2^9 × 2^(31 - 15)

        static constexpr uint32 kWordBits = 32u;

        /** @brief 아래 @p count 비트가 켜진 마스크입니다(count 32 도 시프트 32 없이 — 셰이더와 같은 꼴). */
        [[nodiscard]] static constexpr uint32 computeFieldMask( const uint32 count ) noexcept { return 0xFFFFFFFFu >> ( kWordBits - count ); }
        /** @brief 아래 바이트 하나를 unorm8 로 풉니다. */
        [[nodiscard]] static constexpr float32 toUnorm8( const uint32 packed ) noexcept { return unorm8::fromRaw( static_cast<uint8>( packed ) ).toFloat(); }
        /** @brief 아래 바이트 하나를 snorm8 로 풉니다. */
        [[nodiscard]] static constexpr float32 toSnorm8( const uint32 packed ) noexcept
        {
            return snorm8::fromRaw( static_cast<int8>( static_cast<uint8>( packed ) ) ).toFloat();
        }
        /** @brief snorm8 양자화 정수를 2 의 보수 그대로 아래 8 비트에 놓습니다. */
        [[nodiscard]] static constexpr uint32 toSnorm8Bits( const float32 value ) noexcept
        {
            return static_cast<uint8>( snorm8::fromFloat( value ).getRaw() );
        }
        /** @brief snorm16 양자화 정수를 2 의 보수 그대로 아래 16 비트에 놓습니다. */
        [[nodiscard]] static constexpr uint32 toSnorm16Bits( const float32 value ) noexcept
        {
            return static_cast<uint16>( snorm16::fromFloat( value ).getRaw() );
        }
        /** @brief 공유 지수 @p sharedExponent 의 가수 한 눈금입니다(2^(지수 - 치우침 - 가수 비트)). */
        [[nodiscard]] static float32 computeRGB9E5Scale( const int32 sharedExponent ) noexcept
        {
            return MathUtil::pow( 2.0f, static_cast<float32>( sharedExponent - kRGB9E5ExponentBias - static_cast<int32>( kRGB9E5MantissaBits ) ) );
        }
        /** @brief 채널 하나를 눈금 @p scale 의 가수로 둥글립니다(마스크 위는 자릅니다). */
        [[nodiscard]] static uint32 quantizeRGB9E5Mantissa( const float32 value, const float32 scale ) noexcept
        {
            return MathUtil::min( static_cast<uint32>( MathUtil::floor( value / scale + 0.5f ) ), kRGB9E5MantissaMask );
        }
    };
} // namespace sw
