#include "pch.h"

#include "Core/Math/Color.h"

#include "Core/Common/BitUtil.h"
#include "Core/Common/HexUtil.h"
#include "Core/Math/MathUtil.h"

namespace sw
{
    namespace
    {
        struct ColorInternal
        {
            /** @brief 0~1 로 자르고 가장 가까운 바이트로 바꿉니다. */
            static uint32 toByte( const float32 channel ) noexcept
            {
                return static_cast<uint32>( MathUtil::clamp( channel, 0.0f, 1.0f ) * 255.0f + 0.5f );
            }

            /** @brief 바이트 값(0~255)을 0~1 로 바꿉니다. */
            static float32 fromByte( const uint32 byte ) noexcept
            {
                return static_cast<float32>( byte ) / 255.0f;
            }

            static float32 srgbToLinear( const float32 value ) noexcept
            {
                if ( value <= 0.04045f )
                    return value / 12.92f;
                return std::pow( ( value + 0.055f ) / 1.055f, 2.4f );
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

    Color Color::fromRgba( const uint32 packed ) noexcept
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
                bParsed = HexUtil::tryParseDigit( text[channelIndex], high );
                low     = high;
            }
            else
            {
                bParsed = HexUtil::tryParseDigit( text[channelIndex * 2], high ) && HexUtil::tryParseDigit( text[channelIndex * 2 + 1], low );
            }
            if ( bParsed == false )
                return false;
            arrChannel[channelIndex] = high * 16 + low;
        }
        outColor = Color{ ColorInternal::fromByte( arrChannel[0] ), ColorInternal::fromByte( arrChannel[1] ), ColorInternal::fromByte( arrChannel[2] ), ColorInternal::fromByte( arrChannel[3] ) };
        return true;
    }

    uint32 Color::toRgba() const noexcept
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

    void Color::toHex( utf8 ( &outText )[10] ) const noexcept
    {
        outText[0] = '#';
        HexUtil::writeDigits( outText + 1, toRgba(), 8, HexCase::Upper );
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

    Color Color::toSrgb() const noexcept
    {
        return Color{ ColorInternal::linearToSRGB( _r ), ColorInternal::linearToSRGB( _g ), ColorInternal::linearToSRGB( _b ), _a };
    }
} // namespace sw
