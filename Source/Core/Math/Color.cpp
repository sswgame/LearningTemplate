#include "pch.h"

#include "Core/Math/Color.h"

#include "Core/Foundation/HexDigitUtil.h"
#include "Core/Math/BitUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/NormalizedInt.h"

namespace sw
{
    namespace
    {
        struct ColorInternal
        {
            /** @brief 색상환의 구간 수입니다(빨강 → 노랑 → 초록 → 청록 → 파랑 → 자홍). */
            static constexpr float32 kHueSectorCount = 6.0f;
            /** @brief 초록 · 파랑이 가장 큰 채널일 때 색상이 시작하는 구간입니다. */
            static constexpr float32 kGreenSector = 2.0f;
            static constexpr float32 kBlueSector  = 4.0f;

            /** @brief 0~1 로 자르고 가장 가까운 바이트로 바꿉니다. NaN 은 0 입니다 — `clamp` 는 NaN 을 그대로 넘겨 정수 변환이 정의되지 않았다. */
            static uint32 toByte( const float32 channel ) noexcept
            {
                return static_cast<uint32>( unorm8::fromFloat( channel ).getRaw() );
            }

            /** @brief 바이트 값(0~255)을 0~1 로 바꿉니다. */
            static float32 fromByte( const uint32 byte ) noexcept
            {
                return unorm8::fromRaw( static_cast<uint8>( byte ) ).toFloat();
            }

            static float32 srgbToLinear( const float32 value ) noexcept
            {
                if ( value <= 0.04045f )
                    return value / 12.92f;
                return std::pow( ( value + 0.055f ) / 1.055f, 2.4f );
            }

            /**
             * @brief 색상(0~1) · 채도 폭(span — 가장 큰 채널 − 가장 작은 채널) · 밝기 바닥(@p minimum)에서 RGB 를 만듭니다 — HSV 는 폭 = 명도 × 채도, 바닥 = 명도 − 폭이다.
             * @details 색상환은 여섯 구간(빨강 → 노랑 → 초록 → 청록 → 파랑 → 자홍)이고, 구간마다 가장 큰 채널이 폭, 둘째 채널이 구간 안 위치만큼이다.
             */
            static Color makeFromHueSpan( const float32 hue, const float32 span, const float32 minimum, const float32 alpha ) noexcept
            {
                const float32 sector = MathUtil::repeat( hue, 1.0f ) * kHueSectorCount;
                const float32 second = span * ( 1.0f - MathUtil::abs( MathUtil::fmod( sector, 2.0f ) - 1.0f ) );
                float32       red    = 0.0f;
                float32       green  = 0.0f;
                float32       blue   = 0.0f;
                switch ( static_cast<uint32>( sector ) )
                {
                    case 0:
                        red   = span;
                        green = second;
                        break;
                    case 1:
                        red   = second;
                        green = span;
                        break;
                    case 2:
                        green = span;
                        blue  = second;
                        break;
                    case 3:
                        green = second;
                        blue  = span;
                        break;
                    case 4:
                        red  = second;
                        blue = span;
                        break;
                    default:
                        red  = span;
                        blue = second;
                        break;
                }
                return Color{ red + minimum, green + minimum, blue + minimum, alpha };
            }

            /** @brief RGB 의 색상(0~1)입니다. @p maximum · @p span 는 세 채널의 최댓값과 (최댓값 − 최솟값)이고, 폭이 0 이면 0 입니다. */
            static float32 computeHue( const Color& color, const float32 maximum, const float32 span ) noexcept
            {
                if ( span <= 0.0f )
                    return 0.0f;
                float32 sector = 0.0f;
                if ( maximum == color._r )
                    sector = ( color._g - color._b ) / span;
                else if ( maximum == color._g )
                    sector = ( color._b - color._r ) / span + kGreenSector;
                else
                    sector = ( color._r - color._g ) / span + kBlueSector;
                return MathUtil::repeat( sector / kHueSectorCount, 1.0f );
            }

            static float32 linearToSRGB( const float32 value ) noexcept
            {
                if ( value <= 0.0031308f )
                    return value * 12.92f;
                return 1.055f * std::pow( value, 1.0f / 2.4f ) - 0.055f;
            }
        };
    } // namespace

    const Color Color::kWhite{ 1.0f, 1.0f, 1.0f, 1.0f };
    const Color Color::kBlack{ 0.0f, 0.0f, 0.0f, 1.0f };
    const Color Color::kRed{ 1.0f, 0.0f, 0.0f, 1.0f };
    const Color Color::kGreen{ 0.0f, 1.0f, 0.0f, 1.0f };
    const Color Color::kBlue{ 0.0f, 0.0f, 1.0f, 1.0f };
    const Color Color::kYellow{ 1.0f, 1.0f, 0.0f, 1.0f };
    const Color Color::kCyan{ 0.0f, 1.0f, 1.0f, 1.0f };
    const Color Color::kMagenta{ 1.0f, 0.0f, 1.0f, 1.0f };
    const Color Color::kGray{ 0.5f, 0.5f, 0.5f, 1.0f };
    const Color Color::kTransparent{ 0.0f, 0.0f, 0.0f, 0.0f };

    Color Color::fromRGBA( const uint32 packed ) noexcept
    {
        return Color{ ColorInternal::fromByte( BitUtil::getByte( packed, 3 ) ), ColorInternal::fromByte( BitUtil::getByte( packed, 2 ) ), ColorInternal::fromByte( BitUtil::getByte( packed, 1 ) ), ColorInternal::fromByte( BitUtil::getByte( packed, 0 ) ) };
    }

    Color Color::fromArgb( const uint32 packed ) noexcept
    {
        return Color{ ColorInternal::fromByte( BitUtil::getByte( packed, 2 ) ), ColorInternal::fromByte( BitUtil::getByte( packed, 1 ) ), ColorInternal::fromByte( BitUtil::getByte( packed, 0 ) ), ColorInternal::fromByte( BitUtil::getByte( packed, 3 ) ) };
    }

    Color Color::fromAbgr( const uint32 packed ) noexcept
    {
        return Color{ ColorInternal::fromByte( BitUtil::getByte( packed, 0 ) ), ColorInternal::fromByte( BitUtil::getByte( packed, 1 ) ), ColorInternal::fromByte( BitUtil::getByte( packed, 2 ) ), ColorInternal::fromByte( BitUtil::getByte( packed, 3 ) ) };
    }

    bool Color::fromHex( string_view text, Color& outColor ) noexcept
    {
        if ( text.empty() == false && text.front() == '#' )
            text.remove_prefix( 1 );
        const size_t digitCount            = text.size();
        const bool   bInvalidHexDigitCount = digitCount != 3 && digitCount != 4 && digitCount != 6 && digitCount != 8;
        if ( bInvalidHexDigitCount )
            return false;

        uint32       arrChannel[4]{ 0, 0, 0, 255 };
        const bool   bShort       = digitCount <= 4;
        const size_t channelCount = bShort ? digitCount : digitCount / 2;
        for ( size_t channelIndex = 0; channelIndex < channelCount; ++channelIndex )
        {
            uint32 high    = 0;
            uint32 low     = 0;
            bool   bParsed = false;
            if ( bShort )
            {
                bParsed = HexDigitUtil::tryParseDigit( text[channelIndex], high );
                low     = high;
            }
            else
            {
                bParsed = HexDigitUtil::tryParseDigit( text[channelIndex * 2], high ) && HexDigitUtil::tryParseDigit( text[channelIndex * 2 + 1], low );
            }
            if ( bParsed == false )
                return false;
            arrChannel[channelIndex] = high * 16 + low;
        }
        outColor = Color{ ColorInternal::fromByte( arrChannel[0] ), ColorInternal::fromByte( arrChannel[1] ), ColorInternal::fromByte( arrChannel[2] ), ColorInternal::fromByte( arrChannel[3] ) };
        return true;
    }

    uint32 Color::toRGBA() const noexcept
    {
        return BitUtil::packBytes( ColorInternal::toByte( _r ), ColorInternal::toByte( _g ), ColorInternal::toByte( _b ), ColorInternal::toByte( _a ) );
    }

    uint32 Color::toArgb() const noexcept
    {
        return BitUtil::packBytes( ColorInternal::toByte( _a ), ColorInternal::toByte( _r ), ColorInternal::toByte( _g ), ColorInternal::toByte( _b ) );
    }

    uint32 Color::toAbgr() const noexcept
    {
        return BitUtil::packBytes( ColorInternal::toByte( _a ), ColorInternal::toByte( _b ), ColorInternal::toByte( _g ), ColorInternal::toByte( _r ) );
    }

    Color Color::fromHueSaturationValue( const float32 hue, const float32 saturation, const float32 value, const float32 alpha ) noexcept
    {
        const float32 span = value * saturation;
        return ColorInternal::makeFromHueSpan( hue, span, value - span, alpha );
    }

    float3 Color::toHueSaturationValue() const noexcept
    {
        const float32 maximum = MathUtil::max( _r, MathUtil::max( _g, _b ) );
        const float32 span    = maximum - MathUtil::min( _r, MathUtil::min( _g, _b ) );
        return float3{ ColorInternal::computeHue( *this, maximum, span ), maximum > 0.0f ? span / maximum : 0.0f, maximum };
    }

    bool Color::nearEqual( const Color& other, const float32 tolerance ) const noexcept
    {
        return MathUtil::nearEqual( _r, other._r, tolerance ) && MathUtil::nearEqual( _g, other._g, tolerance ) && MathUtil::nearEqual( _b, other._b, tolerance ) &&
               MathUtil::nearEqual( _a, other._a, tolerance );
    }

    void Color::toHex( utf8 ( &outText )[10] ) const noexcept
    {
        outText[0] = '#';
        HexDigitUtil::writeDigits( outText + 1, toRGBA(), 8, HexCase::Upper );
        outText[9] = '\0';
    }

    Color Color::saturate() const noexcept
    {
        return Color{ MathUtil::clamp( _r, 0.0f, 1.0f ), MathUtil::clamp( _g, 0.0f, 1.0f ), MathUtil::clamp( _b, 0.0f, 1.0f ), MathUtil::clamp( _a, 0.0f, 1.0f ) };
    }

    Color Color::toLinear() const noexcept
    {
        return Color{ ColorInternal::srgbToLinear( _r ), ColorInternal::srgbToLinear( _g ), ColorInternal::srgbToLinear( _b ), _a };
    }

    Color Color::toSRGB() const noexcept
    {
        return Color{ ColorInternal::linearToSRGB( _r ), ColorInternal::linearToSRGB( _g ), ColorInternal::linearToSRGB( _b ), _a };
    }
} // namespace sw
