#include "pch.h"

#include "EngineTest/RHITestImage.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/Renderer/Frame/FrameRenderer.h"

#include <cmath>

namespace test
{
    namespace
    {
        /** @brief IEEE 반정밀도 하나를 [0,1] 로 자른 0~255 로 바꿉니다(음수 · 비정상 수 · 무한은 0). */
        uint8 toUnitByteFromHalf( uint16 half )
        {
            const uint32 exponent = ( half >> 10 ) & 0x1Fu;
            const uint32 mantissa = half & 0x3FFu;
            if ( exponent == 0 || exponent == 0x1Fu || ( half & 0x8000u ) != 0 )
                return 0;
            const float32 value = ( 1.0f + static_cast<float32>( mantissa ) / 1024.0f ) * std::ldexp( 1.0f, static_cast<int32>( exponent ) - 15 );
            return static_cast<uint8>( sw::MathUtil::clamp( value, 0.0f, 1.0f ) * 255.0f );
        }
    } // namespace

    bool RHITestImage::readTransient( sw::FrameRenderer& renderer, sw::string_view attachmentName )
    {
        _bytes.clear();
        _layout = {};
        _format = sw::RHIFormat::R8G8B8A8_UNORM;
        if ( renderer.readbackTransient( attachmentName, _bytes, _layout, _format ) )
            return true;

        _bytes.clear();
        _layout = {};
        return false;
    }

    void RHITestImage::assign( sw::vector<uint8> bytes, const sw::RHITextureMipSpan& layout, sw::RHIFormat format )
    {
        _bytes  = std::move( bytes );
        _layout = layout;
        _format = format;
    }

    const uint8* RHITestImage::getRawPixel( uint32 x, uint32 y ) const
    {
        if ( x >= _layout._width || y >= _layout._height )
            return nullptr;
        return _bytes.data() + static_cast<size_t>( y ) * _layout._rowBytes + static_cast<size_t>( x ) * getBytesPerPixel();
    }

    Rgba8 RHITestImage::getPixel( uint32 x, uint32 y ) const
    {
        const uint8* pPixel = getRawPixel( x, y );
        if ( pPixel == nullptr )
            return {};

        if ( _format == sw::RHIFormat::R16G16B16A16_FLOAT )
        {
            const auto readHalf = [pPixel]( uint32 channel ) -> uint16
            {
                return static_cast<uint16>( pPixel[channel * 2] | ( pPixel[channel * 2 + 1] << 8 ) );
            };
            return Rgba8{ toUnitByteFromHalf( readHalf( 0 ) ), toUnitByteFromHalf( readHalf( 1 ) ), toUnitByteFromHalf( readHalf( 2 ) ),
                          toUnitByteFromHalf( readHalf( 3 ) ) };
        }
        if ( _format == sw::RHIFormat::B8G8R8A8_UNORM )
            return Rgba8{ pPixel[2], pPixel[1], pPixel[0], pPixel[3] };
        return Rgba8{ pPixel[0], pPixel[1], pPixel[2], pPixel[3] };
    }

    uint32 RHITestImage::getColorDistance( const Rgba8& lhs, const Rgba8& rhs )
    {
        const auto distance = []( uint8 a, uint8 b ) -> uint32
        {
            return a > b ? static_cast<uint32>( a - b ) : static_cast<uint32>( b - a );
        };
        return distance( lhs._r, rhs._r ) + distance( lhs._g, rhs._g ) + distance( lhs._b, rhs._b );
    }

    bool RHITestImage::isDefaultClearBackground( const Rgba8& pixel )
    {
        return 22 <= pixel._r && pixel._r <= 40 && 28 <= pixel._g && pixel._g <= 48 && 36 <= pixel._b && pixel._b <= 56;
    }
} // namespace test
