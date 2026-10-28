/**
 * @file RGB10A2.h
 * @brief RGB 10 비트 + 알파 2 비트를 `uint32` 하나에 담는 저장 전용 타입 `RGB10A2` 입니다(DXGI `R10G10B10A2_UNORM` 과 같은 배치).
 * @details 산술 연산자도 암시적 변환도 없습니다. 계산은 `toFloat4` 로 풀어서 하고 결과를 `fromFloat4` 로 담습니다(`Half.h` 와 같은 규칙).
 *          - **배치**: 비트 0..9 = R, 10..19 = G, 20..29 = B, 30..31 = A. 값은 `uint32` 정수 하나이므로 엔디언과 무관하게 같고, 리틀 엔디언 메모리에서는
 *            DXGI `R10G10B10A2_UNORM` 텍스처 · 정점 속성과 바이트가 같습니다.
 *          - **unorm**: 값은 0..1 로 잘리고 NaN 은 0 입니다. 색 채널의 단계는 1/1023 이라 `unorm8` 보다 4 배 곱고, 알파는 0, 1/3, 2/3, 1 네 단계뿐입니다.
 *            HDR(1 초과) 값은 담지 못하므로 HDR 색에는 `half4` 를 씁니다.
 *          - 법선에는 `OctahedralNormal` 이 같은 4 바이트에서 오차가 더 작습니다. 이 타입은 알파가 필요 없는 색 · 마스크 · 0..1 가중치 묶음에 맞습니다.
 *          - 크기 · 정렬은 `static_assert` 가 못 박습니다(패딩 없음). 상용 엔진의 대응: 언리얼 `FPackedRGB10A2N`(DXGI `R10G10B10A2` · Vulkan `A2B10G10R10` 형식).
 */
#pragma once
#include "Core/Foundation/StdHeaders.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/NormalizedInt.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    /**
     * @struct RGB10A2
     * @brief R · G · B 각 10 비트와 A 2 비트의 unorm 묶음입니다.
     */
    struct RGB10A2 final
    {
        /** @brief 색 채널 하나의 1.0 정수(1023)입니다. */
        static constexpr uint32 kColorMaxRaw = 1023u;
        /** @brief 알파의 1.0 정수(3)입니다. */
        static constexpr uint32 kAlphaMaxRaw = 3u;

        uint32 _packed{ 0 };

        /** @brief 담긴 `uint32` 를 그대로 감쌉니다(파일 · GPU 에서 읽은 값). */
        [[nodiscard]] static constexpr RGB10A2 fromPacked( const uint32 packed ) noexcept
        {
            RGB10A2 result{};
            result._packed = packed;
            return result;
        }

        /** @brief 정수 채널(R, G, B 는 0..1023, A 는 0..3)로 만듭니다. 범위를 넘은 정수는 비트를 침범하지 않게 자릅니다. */
        [[nodiscard]] static constexpr RGB10A2 fromRaw( const uint32 red, const uint32 green, const uint32 blue, const uint32 alpha ) noexcept
        {
            const uint32 redBits   = red & kColorMaxRaw;
            const uint32 greenBits = green & kColorMaxRaw;
            const uint32 blueBits  = blue & kColorMaxRaw;
            const uint32 alphaBits = alpha & kAlphaMaxRaw;
            return fromPacked( redBits | ( greenBits << kGreenShift ) | ( blueBits << kBlueShift ) | ( alphaBits << kAlphaShift ) );
        }

        /** @brief 실수 채널(x = R, y = G, z = B, w = A)을 0..1 로 잘라 가장 가까운 단계로 담습니다. */
        [[nodiscard]] static constexpr RGB10A2 fromFloat4( const float4& rgba ) noexcept
        {
            return fromRaw( NormalizedMath::quantizeUnorm( rgba._x, kColorMaxRaw ), NormalizedMath::quantizeUnorm( rgba._y, kColorMaxRaw ),
                            NormalizedMath::quantizeUnorm( rgba._z, kColorMaxRaw ), NormalizedMath::quantizeUnorm( rgba._w, kAlphaMaxRaw ) );
        }

        /** @brief 0..1 실수 채널(x = R, y = G, z = B, w = A)로 풉니다. 최대 정수는 정확히 1.0 입니다. */
        [[nodiscard]] constexpr float4 toFloat4() const noexcept
        {
            return float4{ static_cast<float32>( getRed() ) / static_cast<float32>( kColorMaxRaw ), static_cast<float32>( getGreen() ) / static_cast<float32>( kColorMaxRaw ),
                           static_cast<float32>( getBlue() ) / static_cast<float32>( kColorMaxRaw ), static_cast<float32>( getAlpha() ) / static_cast<float32>( kAlphaMaxRaw ) };
        }

        /** @brief 담긴 `uint32` 입니다. */
        [[nodiscard]] constexpr uint32 getPacked() const noexcept { return _packed; }
        /** @brief R 정수(0..1023)입니다. */
        [[nodiscard]] constexpr uint32 getRed() const noexcept { return _packed & kColorMaxRaw; }
        /** @brief G 정수(0..1023)입니다. */
        [[nodiscard]] constexpr uint32 getGreen() const noexcept { return ( _packed >> kGreenShift ) & kColorMaxRaw; }
        /** @brief B 정수(0..1023)입니다. */
        [[nodiscard]] constexpr uint32 getBlue() const noexcept { return ( _packed >> kBlueShift ) & kColorMaxRaw; }
        /** @brief A 정수(0..3)입니다. */
        [[nodiscard]] constexpr uint32 getAlpha() const noexcept { return ( _packed >> kAlphaShift ) & kAlphaMaxRaw; }

    private:
        static constexpr uint32 kGreenShift = 10u;
        static constexpr uint32 kBlueShift  = 20u;
        static constexpr uint32 kAlphaShift = 30u;
    };

    // CPU · GPU 구조체 미러에 쓰이므로 크기 · 정렬을 못 박는다(`R10G10B10A2_UNORM` 과 같은 4 바이트, 패딩 없음).
    static_assert( sizeof( RGB10A2 ) == 4 && alignof( RGB10A2 ) == 4, "RGB10A2 는 uint32 하나, 4 바이트다" );
    static_assert( std::is_trivially_copyable_v<RGB10A2> && std::is_standard_layout_v<RGB10A2>, "RGB10A2 는 비트 복사가 가능해야 한다" );
    static_assert( RGB10A2::fromRaw( 1u, 2u, 3u, 3u ).getPacked() == 0xC0300801u, "R 은 낮은 비트, A 는 높은 두 비트다" );
} // namespace sw
