#include "pch.h"

#include "Engine/Renderer/Frame/View/HZBPyramid.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    namespace
    {
        struct HZBPyramidInternal
        {
            /** @brief UV @p uv 를 깊이 한 축 @p depthExtent 의 픽셀로 옮깁니다(0 ~ 깊이 - 1 로 자른다 — hzb.hlsli `HZB::toPixel` 과 같다). */
            static uint32 toPixel( float32 uv, uint32 depthExtent )
            {
                const float32 pixel = MathUtil::clamp( uv, 0.0f, 1.0f ) * static_cast<float32>( depthExtent );
                const uint32  index = static_cast<uint32>( pixel );
                return index >= depthExtent ? depthExtent - 1u : index;
            }
        };
    } // namespace

    optional<HZBScreenRect> HZBScreenRect::projectBox( const float4x4& viewProj, const float3& center, const float3& extent )
    {
        const float4x4& m = viewProj;
        HZBScreenRect   rect{ 1.0f, 1.0f, 0.0f, 0.0f, 1.0f };
        for ( uint32 corner = 0; corner < 8; ++corner )
        {
            const float32 x     = center._x + ( ( corner & 1u ) != 0u ? extent._x : -extent._x );
            const float32 y     = center._y + ( ( corner & 2u ) != 0u ? extent._y : -extent._y );
            const float32 z     = center._z + ( ( corner & 4u ) != 0u ? extent._z : -extent._z );
            const float32 clipW = x * m._14 + y * m._24 + z * m._34 + m._44;
            if ( clipW <= kMinClipW )
                return nullopt;
            const float32 ndcX = ( x * m._11 + y * m._21 + z * m._31 + m._41 ) / clipW;
            const float32 ndcY = ( x * m._12 + y * m._22 + z * m._32 + m._42 ) / clipW;
            const float32 ndcZ = ( x * m._13 + y * m._23 + z * m._33 + m._43 ) / clipW;
            const float32 u    = ndcX * 0.5f + 0.5f;
            const float32 v    = ndcY * -0.5f + 0.5f;
            rect._minU         = MathUtil::min( rect._minU, u );
            rect._minV         = MathUtil::min( rect._minV, v );
            rect._maxU         = MathUtil::max( rect._maxU, u );
            rect._maxV         = MathUtil::max( rect._maxV, v );
            rect._minDepth     = MathUtil::min( rect._minDepth, ndcZ );
        }
        // 가까운 평면을 걸치거나 화면 밖이면 그 깊이가 자리를 모른다.
        const bool bUnknown = rect._minDepth <= 0.0f || rect._maxU <= 0.0f || rect._maxV <= 0.0f || rect._minU >= 1.0f || rect._minV >= 1.0f;
        if ( bUnknown )
            return nullopt;
        rect._minU = MathUtil::clamp( rect._minU, 0.0f, 1.0f );
        rect._minV = MathUtil::clamp( rect._minV, 0.0f, 1.0f );
        rect._maxU = MathUtil::clamp( rect._maxU, 0.0f, 1.0f );
        rect._maxV = MathUtil::clamp( rect._maxV, 0.0f, 1.0f );
        return rect;
    }

    HZBLayout HZBLayout::make( uint32 depthWidth, uint32 depthHeight )
    {
        HZBLayout layout;
        layout._depthWidth  = depthWidth;
        layout._depthHeight = depthHeight;
        if ( depthWidth == 0 || depthHeight == 0 )
            return layout;
        uint32 offset = 0;
        for ( uint32 mip = 0; mip < kMaxMipCount; ++mip )
        {
            Mip& level    = layout._arrMip[mip];
            level._width  = computeMipExtent( depthWidth, mip );
            level._height = computeMipExtent( depthHeight, mip );
            level._offset = offset;
            offset += level._width * level._height;
            layout._mipCount       = mip + 1u;
            const bool bReachedTop = level._width == 1u && level._height == 1u && mip >= kTileMip;
            if ( bReachedTop )
                break;
        }
        layout._texelCount = offset;
        return layout;
    }

    optional<uint32> HZBLayout::selectMip( const HZBScreenRect& rect, uint32 footprintTexels ) const
    {
        if ( _mipCount == 0 || footprintTexels == 0 )
            return nullopt;
        using Internal      = HZBPyramidInternal;
        const uint32 firstX = Internal::toPixel( rect._minU, _depthWidth );
        const uint32 firstY = Internal::toPixel( rect._minV, _depthHeight );
        const uint32 lastX  = Internal::toPixel( rect._maxU, _depthWidth );
        const uint32 lastY  = Internal::toPixel( rect._maxV, _depthHeight );
        for ( uint32 mip = 0; mip < _mipCount; ++mip )
        {
            const uint32 shift = mip + 1u;
            const uint32 spanX = ( lastX >> shift ) - ( firstX >> shift ) + 1u;
            const uint32 spanY = ( lastY >> shift ) - ( firstY >> shift ) + 1u;
            if ( spanX <= footprintTexels && spanY <= footprintTexels )
                return mip;
        }
        return nullopt;
    }
} // namespace sw
