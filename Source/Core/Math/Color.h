/**
 * @file Color.h
 * @brief RGBA 색 값 타입 `Color` 입니다 — 부동소수점(0~1) 4채널과 8비트 패킹(`toRgba` · `toArgb` · `toAbgr`) · 16진 글자 · sRGB 변환을 한곳에 둡니다.
 * @details 엔진 · 에디터 · UI · 디버그 그리기가 `float4` 필드와 손으로 만든 `uint32` 패킹을 따로 들고 있던 자리를 대신합니다.
 *          **저장 값은 입력 그대로입니다**(감마 · 선형 가정을 하지 않는다) — 이 값이 sRGB 인지 선형인지는 쓰는 쪽이 알고, `toLinear` · `toSrgb` 로 바꿉니다.
 *          패킹 이름은 **가장 높은 바이트부터** 채널 순서로 읽습니다: `toRgba` 는 `0xRRGGBBAA`, `toArgb` 는 `0xAARRGGBB`, `toAbgr` 는 `0xAABBGGRR` 입니다.
 *          `toAbgr` 는 리틀엔디안 메모리에서 바이트가 R, G, B, A 순으로 놓이는 값이라 `DXGI_FORMAT_R8G8B8A8_UNORM` 텍스처 · ImGui `IM_COL32` 와 같습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    /**
     * @struct Color
     * @brief 4채널(R · G · B · A) 부동소수점 색입니다. 채널은 보통 0~1 이지만 HDR 값(1 초과)도 담습니다 — 패킹할 때만 0~1 로 자릅니다.
     */
    struct SW_API Color final
    {
        float32 _r{ 1.0f };
        float32 _g{ 1.0f };
        float32 _b{ 1.0f };
        float32 _a{ 1.0f };

        constexpr Color() noexcept = default;
        constexpr Color( const float32 r, const float32 g, const float32 b, const float32 a = 1.0f ) noexcept
            : _r{ r }
            , _g{ g }
            , _b{ b }
            , _a{ a }
        {
        }

        // ------------------------------------------------------------------------------
        // 1) 만들기 — 8비트 패킹 · 바이트 · float4 · 16진 글자
        // ------------------------------------------------------------------------------
        /** @brief `0xRRGGBBAA` 에서 만듭니다. */
        [[nodiscard]] static Color fromRgba( uint32 packed ) noexcept;
        /** @brief `0xAARRGGBB` 에서 만듭니다(D3D 의 `D3DCOLOR` · `DXGI_FORMAT_B8G8R8A8_UNORM` 의 정수 값). */
        [[nodiscard]] static Color fromArgb( uint32 packed ) noexcept;
        /** @brief `0xAABBGGRR` 에서 만듭니다(ImGui `IM_COL32` · `DXGI_FORMAT_R8G8B8A8_UNORM` 의 정수 값). */
        [[nodiscard]] static Color fromAbgr( uint32 packed ) noexcept;
        /** @brief 0~255 바이트 채널에서 만듭니다. */
        [[nodiscard]] static constexpr Color fromBytes( const uint8 r, const uint8 g, const uint8 b, const uint8 a = 255 ) noexcept
        {
            return Color{ static_cast<float32>( r ) * kByteToUnit, static_cast<float32>( g ) * kByteToUnit, static_cast<float32>( b ) * kByteToUnit, static_cast<float32>( a ) * kByteToUnit };
        }
        /** @brief `float4`(x=R, y=G, z=B, w=A)에서 만듭니다. */
        [[nodiscard]] static constexpr Color fromFloat4( const float4& value ) noexcept { return Color{ value._x, value._y, value._z, value._w }; }
        /**
         * @brief `#RGB` · `#RGBA` · `#RRGGBB` · `#RRGGBBAA`(앞의 `#` 는 생략 가능, 대소문자 무관)에서 만듭니다.
         * @return 글자가 형식에 맞지 않으면 false 이고 @p outColor 는 바뀌지 않습니다.
         */
        [[nodiscard]] static bool fromHex( string_view text, Color& outColor ) noexcept;

        // ------------------------------------------------------------------------------
        // 2) 내보내기
        // ------------------------------------------------------------------------------
        /** @brief `0xRRGGBBAA` 로 패킹합니다. 채널은 0~1 로 자르고 가장 가까운 바이트로 반올림합니다. */
        [[nodiscard]] uint32 toRgba() const noexcept;
        /** @brief `0xAARRGGBB` 로 패킹합니다. */
        [[nodiscard]] uint32 toArgb() const noexcept;
        /** @brief `0xAABBGGRR` 로 패킹합니다(ImGui `IM_COL32` 와 같은 값). */
        [[nodiscard]] uint32 toAbgr() const noexcept;
        /** @brief `float4`(x=R, y=G, z=B, w=A)로 바꿉니다. */
        [[nodiscard]] constexpr float4 toFloat4() const noexcept { return float4{ _r, _g, _b, _a }; }
        /** @brief `#RRGGBBAA` 글자를 @p outText 에 씁니다(끝의 NUL 까지 10 칸, `fromHex` 로 되돌릴 수 있다). */
        void toHex( utf8 ( &outText )[10] ) const noexcept;

        // ------------------------------------------------------------------------------
        // 3) 가공 — 알파 · 보간 · 밝기 · 색 공간
        // ------------------------------------------------------------------------------
        /** @brief 알파만 바꾼 색입니다. */
        [[nodiscard]] constexpr Color withAlpha( const float32 alpha ) const noexcept { return Color{ _r, _g, _b, alpha }; }
        /** @brief 알파를 @p factor 배한 색입니다(페이드). */
        [[nodiscard]] constexpr Color scaleAlpha( const float32 factor ) const noexcept { return Color{ _r, _g, _b, _a * factor }; }
        /** @brief RGB 에 알파를 곱한 색입니다(미리 곱한 알파 블렌딩용). */
        [[nodiscard]] constexpr Color premultiply() const noexcept { return Color{ _r * _a, _g * _a, _b * _a, _a }; }
        /** @brief 채널마다 @p from 에서 @p to 로 @p t 만큼 섞은 색입니다(t 는 자르지 않는다). */
        [[nodiscard]] static constexpr Color lerp( const Color& from, const Color& to, const float32 t ) noexcept
        {
            return Color{ from._r + ( to._r - from._r ) * t, from._g + ( to._g - from._g ) * t, from._b + ( to._b - from._b ) * t, from._a + ( to._a - from._a ) * t };
        }
        /** @brief RGB 를 @p factor 배한 색입니다(알파는 그대로). 1 보다 작으면 어둡고 크면 밝습니다. */
        [[nodiscard]] constexpr Color scaleBrightness( const float32 factor ) const noexcept { return Color{ _r * factor, _g * factor, _b * factor, _a }; }
        /** @brief 이 색을 sRGB 값으로 보고 선형 값으로 바꿉니다(알파는 그대로). */
        [[nodiscard]] Color toLinear() const noexcept;
        /** @brief 이 색을 선형 값으로 보고 sRGB 값으로 바꿉니다(알파는 그대로). */
        [[nodiscard]] Color toSrgb() const noexcept;
        /** @brief 지각 밝기(Rec.709 가중치, RGB 가 같은 색 공간일 때)입니다. */
        [[nodiscard]] constexpr float32 getLuminance() const noexcept { return _r * 0.2126f + _g * 0.7152f + _b * 0.0722f; }

        constexpr bool operator==( const Color& other ) const noexcept { return _r == other._r && _g == other._g && _b == other._b && _a == other._a; }
        constexpr bool operator!=( const Color& other ) const noexcept { return !( *this == other ); }

        // ------------------------------------------------------------------------------
        // 4) 이름 있는 색 — 알파 1 입니다
        // ------------------------------------------------------------------------------
        static const Color kWhite;
        static const Color kBlack;
        static const Color kRed;
        static const Color kGreen;
        static const Color kBlue;
        static const Color kYellow;
        static const Color kCyan;
        static const Color kMagenta;
        static const Color kGray;
        static const Color kTransparent; ///< (0, 0, 0, 0)

    private:
        static constexpr float32 kByteToUnit = 1.0f / 255.0f;
    };
} // namespace sw
