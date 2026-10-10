#include "pch.h"

#include "GameFramework/Kits/Feature/World/Overworld/TileMap.h"

namespace sw
{
    SW_LOG_CALLER( "TileMap" );

    TileMap::TileMap()
        : _data{}
        , _mapWarpIndex{}
    {
    }

    bool TileMap::loadFromXML( string_view assetRelativePath )
    {
        clear();
        _data._sourcePath = assetRelativePath;

        TileMapXMLData xmlData{};
        if ( xmlData.load( assetRelativePath ) == false )
            return false;

        _data = std::move( xmlData );
        rebuildWarpIndex();
        return true;
    }

    bool TileMap::saveToXML( string_view assetRelativePath ) const
    {
        return _data.save( assetRelativePath );
    }

    void TileMap::clear()
    {
        _data = TileMapXMLData{};
        rebuildWarpIndex();
    }

    void TileMap::resize( int32 width, int32 height )
    {
        // 크기 상한은 `TileMapXMLData` 가 기준이다. 로더 · 에디터(`TileMapPanel::resize`)가 같은 것을 본다.
        if ( _data.resetTiles( width, height ) == false )
        {
            SW_LOG_WARNING( "TileMap resize %#x%# is beyond the supported tile count (%#)",
                            width, height, TileMapXMLData::kMaxTileCount );
            return;
        }
        rebuildWarpIndex();
    }

    bool TileMap::isFlagSet( TileFlagLayer layer, int32 x, int32 y ) const
    {
        if ( isInBounds( x, y ) == false )
            return false;
        return _data.getFlagLayer( layer )[indexOf( x, y )] != 0;
    }

    bool TileMap::isWalkable( int32 x, int32 y ) const
    {
        return isFlagSet( TileFlagLayer::Walkable, x, y );
    }

    bool TileMap::isEncounterTile( int32 x, int32 y ) const
    {
        return isFlagSet( TileFlagLayer::Encounter, x, y );
    }

    bool TileMap::isPassThrough( int32 x, int32 y ) const
    {
        return isFlagSet( TileFlagLayer::PassThrough, x, y );
    }

    bool TileMap::isSolid( int32 x, int32 y ) const
    {
        return isWalkable( x, y ) == false;
    }

    void TileMap::rebuildWarpIndex()
    {
        _mapWarpIndex.clear();
        for ( size_t idx = 0; idx < _data._listWarp.size(); ++idx )
        {
            _mapWarpIndex[getWarpKey( _data._listWarp[idx]._tileX, _data._listWarp[idx]._tileY )] = idx;
        }
    }

    const TileWarp* TileMap::findWarp( int32 x, int32 y ) const
    {
        auto it = _mapWarpIndex.find( getWarpKey( x, y ) );
        if ( it != _mapWarpIndex.end() && it->second < _data._listWarp.size() )
            return &_data._listWarp[it->second];
        return nullptr;
    }

    OverworldTileVisual TileMap::getTileVisual( int32 x, int32 y ) const
    {
        if ( isInBounds( x, y ) == false )
            return {};
        return _data._listVisual[indexOf( x, y )];
    }

    void TileMap::setFlag( TileFlagLayer layer, int32 x, int32 y, bool bSet )
    {
        if ( isInBounds( x, y ) )
            _data.getFlagLayer( layer )[indexOf( x, y )] = bSet ? 1 : 0;
    }

    void TileMap::setWalkable( int32 x, int32 y, bool bWalkable )
    {
        setFlag( TileFlagLayer::Walkable, x, y, bWalkable );
    }

    void TileMap::setEncounter( int32 x, int32 y, bool bEncounter )
    {
        setFlag( TileFlagLayer::Encounter, x, y, bEncounter );
    }

    void TileMap::setPassThrough( int32 x, int32 y, bool bPassThrough )
    {
        setFlag( TileFlagLayer::PassThrough, x, y, bPassThrough );
    }

    void TileMap::setTileVisual( int32 x, int32 y, const OverworldTileVisual& visual )
    {
        if ( isInBounds( x, y ) )
            _data._listVisual[indexOf( x, y )] = visual;
    }

    void TileMap::setOrUpdateWarp( const TileWarp& warp )
    {
        auto it = _mapWarpIndex.find( getWarpKey( warp._tileX, warp._tileY ) );
        if ( it != _mapWarpIndex.end() && it->second < _data._listWarp.size() )
        {
            _data._listWarp[it->second] = warp;
            return;
        }
        _mapWarpIndex[getWarpKey( warp._tileX, warp._tileY )] = _data._listWarp.size();
        _data._listWarp.push_back( warp );
    }

    void TileMap::removeWarp( int32 x, int32 y )
    {
        _data._listWarp.erase( std::remove_if( _data._listWarp.begin(), _data._listWarp.end(),
                                               [x, y]( const TileWarp& warp )
        { return warp._tileX == x && warp._tileY == y; } ),
                               _data._listWarp.end() );
        rebuildWarpIndex();
    }

    void TileMap::paintEdgeWarpPreset( int32 edge, string_view targetMap, int32 tx, int32 ty )
    {
        if ( _data._width <= 0 || _data._height <= 0 || targetMap.empty() )
            return;

        auto stamp = [&]( int32 tileX, int32 tileY )
        {
            setWalkable( tileX, tileY, true );
            TileWarp warp{};
            warp._tileX       = tileX;
            warp._tileY       = tileY;
            warp._targetMap   = targetMap;
            warp._targetTileX = tx;
            warp._targetTileY = ty;
            setOrUpdateWarp( warp );
        };

        switch ( edge )
        {
            case 0: // N
            {
                for ( int32 tileX = 0; tileX < _data._width; ++tileX )
                {
                    stamp( tileX, 0 );
                }
                break;
            }
            case 1: // E
            {
                for ( int32 tileY = 0; tileY < _data._height; ++tileY )
                {
                    stamp( _data._width - 1, tileY );
                }
                break;
            }
            case 2: // S
            {
                for ( int32 tileX = 0; tileX < _data._width; ++tileX )
                {
                    stamp( tileX, _data._height - 1 );
                }
                break;
            }
            case 3: // W
            {
                for ( int32 tileY = 0; tileY < _data._height; ++tileY )
                {
                    stamp( 0, tileY );
                }
                break;
            }
            default:
            {
                break;
            }
        }
    }

    void TileMap::debugLogTileHd2d( int32 x, int32 y ) const
    {
        [[maybe_unused]] const OverworldTileVisual tileVisual = getTileVisual( x, y );
        SW_LOG_TRACE( "tile (%#,%#) h=%# tint=(%#,%#,%#) flags walk=%# enc=%# pt=%#",
                      x, y, tileVisual._height, tileVisual._tintR, tileVisual._tintG, tileVisual._tintB,
                      isWalkable( x, y ) ? 1 : 0, isEncounterTile( x, y ) ? 1 : 0, isPassThrough( x, y ) ? 1 : 0 );
    }

    bool TileMap::isInBounds( int32 x, int32 y ) const
    {
        return 0 <= x && x < _data._width && 0 <= y && y < _data._height;
    }
} // namespace sw
