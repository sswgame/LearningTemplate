#include "pch.h"

#include "GameFramework/Kits/Overworld/TileMap.h"

namespace sw
{
    namespace
    {
        /**
         * @brief 플래그 레이어마다 `getFlags` 가 켜는 런타임 비트입니다. `TileFlagLayer` 순서입니다.
         * @details 레이어 표(`kArrTileFlagLayerInfo`)는 Engine 것이라 GameFramework 의 `TileFlags` 를 모릅니다. 그래서 비트만 여기 둡니다.
         */
        constexpr TileFlags kArrTileFlagOfLayer[] = { TileFlags::Walkable, TileFlags::Encounter, TileFlags::PassThrough };
        static_assert( SW_COUNT_OF( kArrTileFlagOfLayer ) == kTileFlagLayerCount, "TileFlagLayer 를 늘렸으면 kArrTileFlagOfLayer 에도 비트를 더할 것" );
    } // namespace

    SW_LOG_CALLER( "TileMap" );

    TileMap::TileMap()
        : _data{}
        , _mapWarpIndex{}
    {
    }

    bool TileMap::loadFromXml( string_view assetRelativePath )
    {
        clear();
        _data._sourcePath = assetRelativePath;

        TileMapXmlData xmlData{};
        if ( xmlData.load( assetRelativePath ) == false )
            return false;

        _data = std::move( xmlData );
        rebuildWarpIndex();
        return true;
    }

    bool TileMap::saveToXml( string_view assetRelativePath ) const
    {
        return _data.save( assetRelativePath );
    }

    void TileMap::clear()
    {
        _data = TileMapXmlData{};
        rebuildWarpIndex();
    }

    void TileMap::resize( int32 width, int32 height )
    {
        // 크기 상한은 `TileMapXmlData` 가 기준이다. 로더 · 에디터(`TileMapPanel::resize`)가 같은 것을 본다.
        if ( _data.resetTiles( width, height ) == false )
        {
            SW_LOG_WARNING( "TileMap resize %#x%# is beyond the supported tile count (%#)",
                            width, height, TileMapXmlData::kMaxTileCount );
            return;
        }
        _data._listEncounterEntry.clear();
        rebuildWarpIndex();
    }

    string TileMap::pickEncounterSpeciesId() const
    {
        if ( _data._listEncounterEntry.empty() )
            return {};

        float32 total{ 0.0f };
        for ( const TileEncounterEntry& entry : _data._listEncounterEntry )
        {
            total += entry._weight > 0.0f ? entry._weight : 0.0f;
        }
        if ( total <= 0.0f )
            return _data._listEncounterEntry[0]._speciesId;

        // 누적 가중치 선택 — XML 의 확률 가중치를 따른다. 함수 지역 static 상태를 두지 말 것(모든 타일맵/스레드가 공유한다).
        const float32 pick = MathUtil::getRandomRange( 0.0f, total );

        float32 accumulated{ 0.0f };
        for ( const TileEncounterEntry& entry : _data._listEncounterEntry )
        {
            const float32 weight = entry._weight > 0.0f ? entry._weight : 0.0f;
            if ( weight <= 0.0f )
                continue;

            accumulated += weight;
            if ( pick <= accumulated )
                return entry._speciesId;
        }

        // 부동소수 오차로 끝까지 못 고른 경우: 가중치가 있는 마지막 항목으로 떨어뜨린다.
        for ( size_t entryIndex = _data._listEncounterEntry.size(); entryIndex > 0; --entryIndex )
        {
            const TileEncounterEntry& entry = _data._listEncounterEntry[entryIndex - 1];
            if ( entry._weight > 0.0f )
                return entry._speciesId;
        }
        return _data._listEncounterEntry[0]._speciesId;
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

    TileFlags TileMap::getFlags( int32 x, int32 y ) const
    {
        if ( isInBounds( x, y ) == false )
            return TileFlags::Solid;
        TileFlags flags = TileFlags::None;
        for ( const TileFlagLayerInfo& info : kArrTileFlagLayerInfo )
        {
            if ( _data.getFlagLayer( info._layer )[indexOf( x, y )] != 0 )
                flags = flags | kArrTileFlagOfLayer[static_cast<size_t>( info._layer )];
        }
        if ( ( flags & TileFlags::Walkable ) == TileFlags::None )
            flags = flags | TileFlags::Solid;
        if ( findWarp( x, y ) != nullptr )
            flags = flags | TileFlags::Warp;
        return flags;
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

    TileVisual TileMap::getTileVisual( int32 x, int32 y ) const
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

    void TileMap::setTileVisual( int32 x, int32 y, const TileVisual& visual )
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
        [[maybe_unused]] const TileVisual tileVisual = getTileVisual( x, y );
        SW_LOG_TRACE( "tile (%#,%#) h=%# tint=(%#,%#,%#) flags walk=%# enc=%# pt=%#",
                      x, y, tileVisual._height, tileVisual._tintR, tileVisual._tintG, tileVisual._tintB,
                      isWalkable( x, y ) ? 1 : 0, isEncounterTile( x, y ) ? 1 : 0, isPassThrough( x, y ) ? 1 : 0 );
    }

    bool TileMap::isInBounds( int32 x, int32 y ) const
    {
        return 0 <= x && x < _data._width && 0 <= y && y < _data._height;
    }
} // namespace sw
