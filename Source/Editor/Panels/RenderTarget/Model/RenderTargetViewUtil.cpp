#include "pch.h"

#include "Editor/Panels/RenderTarget/Model/RenderTargetViewUtil.h"

#include "Core/Math/Half.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"

namespace sw::editor
{
    namespace
    {
        struct RenderTargetViewUtilInternal
        {
            static float32 readFloat( const uint8* pByte )
            {
                float32 value = 0.0f;
                Memory::copy( &value, pByte, sizeof( value ) );
                return value;
            }

            static uint8 toUnorm8( float32 value )
            {
                // NaN 은 saturate 를 그대로 지나가므로 먼저 0 으로 둔다.
                if ( MathUtil::isFinite( value ) == false )
                    return value > 0.0f ? 255 : 0;
                return static_cast<uint8>( MathUtil::round( MathUtil::saturate( value ) * 255.0f ) );
            }

            static float32 selectChannel( const float4& value, RenderTargetChannel channel )
            {
                switch ( channel )
                {
                    case RenderTargetChannel::Green:
                        return value._y;
                    case RenderTargetChannel::Blue:
                        return value._z;
                    case RenderTargetChannel::Alpha:
                        return value._w;
                    case RenderTargetChannel::Red:
                    case RenderTargetChannel::RGBA:
                    case RenderTargetChannel::Count:
                        return value._x;
                }
            }

            /** @brief 지운 깊이(배경)인지 봅니다. 엔진은 깊이를 1 로 지운다(파이프라인 XML 의 깊이 첨부 `_clearColor`). */
            static bool isClearedDepth( float32 depth ) { return depth >= 1.0f; }
        };
    } // namespace

    bool RenderTargetViewUtil::isDecodableFormat( RHIFormat format, bool bDepth )
    {
        if ( bDepth )
            return true;
        switch ( format )
        {
            case RHIFormat::R8G8B8A8_UNORM:
            case RHIFormat::B8G8R8A8_UNORM:
            case RHIFormat::R16G16B16A16_FLOAT:
            case RHIFormat::R32G32B32_FLOAT:
            case RHIFormat::R32G32_FLOAT:
            case RHIFormat::R32_FLOAT:
            case RHIFormat::R8_UNORM:
                return true;
            case RHIFormat::D24_UNORM_S8_UINT: // 깊이는 bDepth 로 온다 — 깊이 배치가 아닌 D24 바이트는 풀지 않는다
            case RHIFormat::Unknown:
            case RHIFormat::BC1_UNORM:
            case RHIFormat::BC2_UNORM:
            case RHIFormat::BC3_UNORM:
            case RHIFormat::BC4_UNORM:
            case RHIFormat::BC5_UNORM:
            case RHIFormat::BC7_UNORM:
            case RHIFormat::BC6H_UF16:
                return false;
        }
    }

    uint32 RenderTargetViewUtil::getChannelCount( RHIFormat format, bool bDepth )
    {
        if ( bDepth )
            return 1;
        switch ( format )
        {
            case RHIFormat::R8G8B8A8_UNORM:
            case RHIFormat::B8G8R8A8_UNORM:
            case RHIFormat::R16G16B16A16_FLOAT:
                return 4;
            case RHIFormat::R32G32B32_FLOAT:
                return 3;
            case RHIFormat::R32G32_FLOAT:
                return 2;
            case RHIFormat::R32_FLOAT:
            case RHIFormat::R8_UNORM:
                return 1;
            case RHIFormat::D24_UNORM_S8_UINT:
            case RHIFormat::Unknown:
            case RHIFormat::BC1_UNORM:
            case RHIFormat::BC2_UNORM:
            case RHIFormat::BC3_UNORM:
            case RHIFormat::BC4_UNORM:
            case RHIFormat::BC5_UNORM:
            case RHIFormat::BC7_UNORM:
            case RHIFormat::BC6H_UF16:
                return 0;
        }
    }

    bool RenderTargetViewUtil::readPixel( const vector<uint8>& byte, const RHITextureMipSpan& layout, RHIFormat format, bool bDepth, uint32 x, uint32 y,
                                          float4& outValue )
    {
        const bool bTexelUnreadable = isDecodableFormat( format, bDepth ) == false || x >= layout._width || y >= layout._height;
        if ( bTexelUnreadable )
            return false;
        const RHIFormat sourceFormat  = bDepth ? kRHIDepthReadbackFormat : format;
        const uint32    bytesPerPixel = getRHIFormatBytesPerPixel( sourceFormat );
        const size_t    offset        = static_cast<size_t>( y ) * layout._rowBytes + static_cast<size_t>( x ) * bytesPerPixel;
        if ( bytesPerPixel == 0 || offset + bytesPerPixel > byte.size() )
            return false;

        const uint8* pPixel = byte.data() + offset;
        outValue            = float4{ 0.0f, 0.0f, 0.0f, 1.0f };
        switch ( sourceFormat )
        {
            case RHIFormat::R8G8B8A8_UNORM:
            {
                outValue = float4{ pPixel[0] / 255.0f, pPixel[1] / 255.0f, pPixel[2] / 255.0f, pPixel[3] / 255.0f };
                break;
            }
            case RHIFormat::B8G8R8A8_UNORM:
            {
                outValue = float4{ pPixel[2] / 255.0f, pPixel[1] / 255.0f, pPixel[0] / 255.0f, pPixel[3] / 255.0f };
                break;
            }
            case RHIFormat::R16G16B16A16_FLOAT:
            {
                uint16 arrHalf[4]{};
                Memory::copy( arrHalf, pPixel, sizeof( arrHalf ) );
                outValue = float4{ half::fromBits( arrHalf[0] ).toFloat(), half::fromBits( arrHalf[1] ).toFloat(), half::fromBits( arrHalf[2] ).toFloat(),
                                   half::fromBits( arrHalf[3] ).toFloat() };
                break;
            }
            case RHIFormat::R32G32B32_FLOAT:
            {
                outValue._x = RenderTargetViewUtilInternal::readFloat( pPixel );
                outValue._y = RenderTargetViewUtilInternal::readFloat( pPixel + 4 );
                outValue._z = RenderTargetViewUtilInternal::readFloat( pPixel + 8 );
                break;
            }
            case RHIFormat::R32G32_FLOAT:
            {
                outValue._x = RenderTargetViewUtilInternal::readFloat( pPixel );
                outValue._y = RenderTargetViewUtilInternal::readFloat( pPixel + 4 );
                break;
            }
            case RHIFormat::R32_FLOAT:
            {
                outValue._x = RenderTargetViewUtilInternal::readFloat( pPixel );
                break;
            }
            case RHIFormat::R8_UNORM:
            {
                outValue._x = pPixel[0] / 255.0f;
                break;
            }
            case RHIFormat::D24_UNORM_S8_UINT:
            case RHIFormat::Unknown:
            case RHIFormat::BC1_UNORM:
            case RHIFormat::BC2_UNORM:
            case RHIFormat::BC3_UNORM:
            case RHIFormat::BC4_UNORM:
            case RHIFormat::BC5_UNORM:
            case RHIFormat::BC7_UNORM:
            case RHIFormat::BC6H_UF16:
            {
                return false;
            }
        }
        return true;
    }

    float32 RenderTargetViewUtil::linearizeDepth( float32 depth, float32 nearPlane, float32 farPlane )
    {
        if ( nearPlane <= 0.0f || farPlane <= nearPlane )
            return depth;
        // 투영 깊이 d = f(z - n) / (z(f - n)) 를 z 에 대해 푼 것이다. d = 0 이면 n, d = 1 이면 f.
        const float32 denominator = farPlane - depth * ( farPlane - nearPlane );
        if ( denominator <= 0.0f )
            return farPlane;
        return nearPlane * farPlane / denominator;
    }

    bool RenderTargetViewUtil::makePreviewImage( const vector<uint8>& byte, const RHITextureMipSpan& layout, RHIFormat format, bool bDepth,
                                                 const RenderTargetViewSettings& settings, RenderTargetPreviewImage& outImage )
    {
        outImage._byte.clear();
        outImage._width              = 0;
        outImage._height             = 0;
        const bool bImageUndecodable = isDecodableFormat( format, bDepth ) == false || layout._width == 0 || layout._height == 0;
        if ( bImageUndecodable )
            return false;
        if ( static_cast<size_t>( layout._rowBytes ) * layout._height > byte.size() )
            return false;

        const bool bLinearize  = bDepth && settings._bLinearizeDepth != SW_FALSE;
        auto       toDepthView = [&settings, bLinearize]( float32 depth )
        { return bLinearize ? linearizeDepth( depth, settings._nearPlane, settings._farPlane ) : depth; };

        // 깊이의 회색 범위: 자동이면 배경을 뺀 최소 ~ 최대, 아니면 선형화했을 때 가까운 면 ~ 먼 면, 원래 깊이면 0 ~ 1.
        float32 low  = bLinearize ? settings._nearPlane : 0.0f;
        float32 high = bLinearize ? settings._farPlane : 1.0f;
        if ( bDepth && settings._bAutoDepthRange != SW_FALSE )
        {
            float32 foundLow  = std::numeric_limits<float32>::max();
            float32 foundHigh = std::numeric_limits<float32>::lowest();
            for ( uint32 y = 0; y < layout._height; ++y )
            {
                for ( uint32 x = 0; x < layout._width; ++x )
                {
                    float4 value{};
                    if ( readPixel( byte, layout, format, bDepth, x, y, value ) == false ||
                         RenderTargetViewUtilInternal::isClearedDepth( value._x ) )
                        continue;
                    const float32 view = toDepthView( value._x );
                    foundLow           = MathUtil::min( foundLow, view );
                    foundHigh          = MathUtil::max( foundHigh, view );
                }
            }
            if ( foundLow <= foundHigh )
            {
                low  = foundLow;
                high = foundHigh;
            }
        }
        outImage._depthLow  = low;
        outImage._depthHigh = high;
        const float32 span  = high - low;

        outImage._width  = layout._width;
        outImage._height = layout._height;
        outImage._byte.resize( static_cast<size_t>( layout._width ) * layout._height * 4 );
        uint8* pOut = outImage._byte.data();
        for ( uint32 y = 0; y < layout._height; ++y )
        {
            for ( uint32 x = 0; x < layout._width; ++x )
            {
                float4 value{};
                (void)readPixel( byte, layout, format, bDepth, x, y, value ); // 크기는 위에서 확인했다
                float4 shown = value;
                if ( bDepth )
                {
                    float32 gray = 1.0f; // 배경은 먼 쪽 = 흰색
                    if ( RenderTargetViewUtilInternal::isClearedDepth( value._x ) == false )
                        gray = span > 0.0f ? ( toDepthView( value._x ) - low ) / span : 0.0f;
                    shown = float4{ gray, gray, gray, 1.0f };
                }
                else if ( settings._channel != RenderTargetChannel::RGBA )
                {
                    const float32 gray = RenderTargetViewUtilInternal::selectChannel( value, settings._channel );
                    shown              = float4{ gray, gray, gray, 1.0f };
                }
                pOut[0] = RenderTargetViewUtilInternal::toUnorm8( shown._x );
                pOut[1] = RenderTargetViewUtilInternal::toUnorm8( shown._y );
                pOut[2] = RenderTargetViewUtilInternal::toUnorm8( shown._z );
                pOut[3] = 255; // 알파는 미리보기를 비치게 하지 않는다 — 알파는 A 채널로 본다
                pOut += 4;
            }
        }
        return true;
    }

    bool RenderTargetViewUtil::mapPreviewToTexel( const float2& localPos, const float2& drawSize, uint32 width, uint32 height, int2& outTexel )
    {
        const bool bInvalidPreviewSize = width == 0 || height == 0 || drawSize._x <= 0.0f || drawSize._y <= 0.0f;
        if ( bInvalidPreviewSize )
            return false;
        const bool bOutsidePreview = localPos._x < 0.0f || localPos._y < 0.0f || localPos._x >= drawSize._x || localPos._y >= drawSize._y;
        if ( bOutsidePreview )
            return false;
        const int32 texelX = static_cast<int32>( localPos._x / drawSize._x * static_cast<float32>( width ) );
        const int32 texelY = static_cast<int32>( localPos._y / drawSize._y * static_cast<float32>( height ) );
        outTexel           = int2{ MathUtil::min( texelX, static_cast<int32>( width ) - 1 ), MathUtil::min( texelY, static_cast<int32>( height ) - 1 ) };
        return true;
    }

    const utf8* RenderTargetViewUtil::getChannelLabel( RenderTargetChannel channel )
    {
        switch ( channel )
        {
            case RenderTargetChannel::RGBA:
                return "RGBA";
            case RenderTargetChannel::Red:
                return "R";
            case RenderTargetChannel::Green:
                return "G";
            case RenderTargetChannel::Blue:
                return "B";
            case RenderTargetChannel::Alpha:
                return "A";
            case RenderTargetChannel::Count:
                return "";
        }
    }
} // namespace sw::editor
