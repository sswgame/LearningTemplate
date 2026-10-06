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

    void PlacementTileSurface::setTiles( uint32 width, uint32 height, float32 tileSize, const vector<uint8>& listTile )
    {
        const bool bSizeMatches = static_cast<size_t>( width ) * height == listTile.size();
        SW_LOG_ASSERT( bSizeMatches, "PlacementTileSurface tile count does not match width x height" );
        _width    = bSizeMatches ? width : 0u;
        _height   = bSizeMatches ? height : 0u;
        _tileSize = MathUtil::max( tileSize, MathUtil::kEpsilon );
        _listTile = bSizeMatches ? listTile : vector<uint8>{};
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
        const uint8 tile       = getTileAt( planePosition );
        outSample              = PlacementSurfaceSample{};
        outSample._layerWeight = float4{ tile == 0 ? 1.0f : 0.0f, tile == 1 ? 1.0f : 0.0f, tile == 2 ? 1.0f : 0.0f, tile == 3 ? 1.0f : 0.0f };
        outSample._bValid      = tile < 4 ? SW_TRUE : SW_FALSE;
        return tile != kBlockedTile;
    }
} // namespace sw
