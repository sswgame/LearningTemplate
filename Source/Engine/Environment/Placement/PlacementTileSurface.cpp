#include "pch.h"

#include "Engine/Environment/Placement/PlacementTileSurface.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    PlacementTileSurface::PlacementTileSurface()
        : _listTile{}
        , _tileSize{ 1.0f }
        , _width{ 0 }
        , _height{ 0 }
    {
    }

    bool PlacementTileSurface::setTiles( uint32 width, uint32 height, float32 tileSize, const vector<uint8>& listTile )
    {
        // 크기가 맞지 않거나 타일 크기가 0 이하이면 맞추지 않고 거절한다(예전에는 빈 표면으로 만들거나 `kEpsilon` 으로 올렸다).
        const bool bValid = static_cast<size_t>( width ) * height == listTile.size() && tileSize > 0.0f;
        if ( bValid == false )
        {
            SW_LOG_ERROR_ID( "Placement.TilesRefused", "PlacementTileSurface: refused %# x %# tiles of size %# with %# entries", width, height, tileSize, static_cast<uint32>( listTile.size() ) );
            return false;
        }
        _width    = width;
        _height   = height;
        _tileSize = tileSize;
        _listTile = listTile;
        return true;
    }

    uint8 PlacementTileSurface::getTileAt( const float2& planePosition ) const
    {
        const float32 tileX   = MathUtil::floor( planePosition._x / _tileSize );
        const float32 tileY   = MathUtil::floor( planePosition._y / _tileSize );
        const bool    bInside = 0.0f <= tileX && tileX < static_cast<float32>( _width ) && 0.0f <= tileY && tileY < static_cast<float32>( _height );
        if ( bInside == false )
            return kBlockedTile;
        return _listTile[static_cast<size_t>( tileY ) * _width + static_cast<size_t>( tileX )];
    }

    bool PlacementTileSurface::sampleSurface( const float2& planePosition, PlacementSurfaceSample& outSample ) const
    {
        const uint8 tile             = getTileAt( planePosition );
        outSample                    = PlacementSurfaceSample{};
        outSample._layerWeight._low  = float4{ tile == 0 ? 1.0f : 0.0f, tile == 1 ? 1.0f : 0.0f, tile == 2 ? 1.0f : 0.0f, tile == 3 ? 1.0f : 0.0f };
        outSample._layerWeight._high = float4{ 0.0f, 0.0f, 0.0f, 0.0f };
        outSample._bValid            = tile < 4 ? SW_TRUE : SW_FALSE;
        return tile != kBlockedTile;
    }
} // namespace sw
