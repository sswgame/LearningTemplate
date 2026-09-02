#include "pch.h"

#include "GameFramework/Kits/Overworld/TileMap.h"

#include "Engine/Utility/Xml/TileMapXml.h"

namespace sw
{
    SW_LOG_CALLER( "TileMap" );

    TileMap::TileMap()
        : _name{}
        , _sourcePath{}
        , _scenePath{}
        , _role{}
        , _width{ 0 }
        , _height{ 0 }
        , _spawnX{ 1 }
        , _spawnY{ 1 }
        , _listWalkable{}
        , _listEncounter{}
        , _listPassThrough{}
        , _listVisual{}
        , _listWarp{}
        , _listEncounterEntry{}
    {
    }

    bool TileMap::loadFromXml( string_view assetRelativePath )
    {
        clear();
        _sourcePath = assetRelativePath;

        TileMapXmlData xmlData{};
        if ( xmlData.load( assetRelativePath ) == false )
            return false;

        _name            = xmlData._name;
        _scenePath       = xmlData._scenePath;
        _role            = xmlData._role;
        _width           = xmlData._width;
        _height          = xmlData._height;
        _spawnX          = xmlData._spawnX;
        _spawnY          = xmlData._spawnY;
        _listWalkable    = std::move( xmlData._listWalkable );
        _listEncounter   = std::move( xmlData._listEncounter );
        _listPassThrough = std::move( xmlData._listPassThrough );
        _listVisual.clear();
        _listVisual.reserve( xmlData._listVisual.size() );
        for ( const TileMapXmlData::Visual& src : xmlData._listVisual )
        {
            TileVisual dst{};
            dst._height  = src._height;
            dst._tintR   = src._tintR;
            dst._tintG   = src._tintG;
            dst._tintB   = src._tintB;
            dst._atlasId = src._atlasId;
            _listVisual.push_back( dst );
        }
        _listWarp.clear();
        _listWarp.reserve( xmlData._listWarp.size() );
        for ( const TileMapXmlData::Warp& src : xmlData._listWarp )
        {
            TileWarp dst{};
            dst._tileX       = src._tileX;
            dst._tileY       = src._tileY;
            dst._targetMap   = src._targetMap;
            dst._targetTileX = src._targetTileX;
            dst._targetTileY = src._targetTileY;
            dst._pairId      = src._pairId;
            _listWarp.push_back( std::move( dst ) );
        }
        _listEncounterEntry.clear();
        _listEncounterEntry.reserve( xmlData._listEncounterEntry.size() );
        for ( const TileMapXmlData::Encounter& src : xmlData._listEncounterEntry )
        {
            TileEncounterEntry dst{};
            dst._speciesId = src._speciesId;
            dst._weight    = src._weight;
            _listEncounterEntry.push_back( std::move( dst ) );
        }
        rebuildWarpIndex();
        return true;
    }

    bool TileMap::saveToXml( string_view assetRelativePath ) const
    {
        TileMapXmlData xmlData{};
        xmlData._name            = _name;
        xmlData._sourcePath      = assetRelativePath;
        xmlData._scenePath       = _scenePath;
        xmlData._role            = _role;
        xmlData._width           = _width;
        xmlData._height          = _height;
        xmlData._spawnX          = _spawnX;
        xmlData._spawnY          = _spawnY;
        xmlData._listWalkable    = _listWalkable;
        xmlData._listEncounter   = _listEncounter;
        xmlData._listPassThrough = _listPassThrough;
        xmlData._listVisual.reserve( _listVisual.size() );
        for ( const TileVisual& src : _listVisual )
        {
            TileMapXmlData::Visual dst{};
            dst._height  = src._height;
            dst._tintR   = src._tintR;
            dst._tintG   = src._tintG;
            dst._tintB   = src._tintB;
            dst._atlasId = src._atlasId;
            xmlData._listVisual.push_back( dst );
        }
        xmlData._listWarp.reserve( _listWarp.size() );
        for ( const TileWarp& src : _listWarp )
        {
            TileMapXmlData::Warp dst{};
            dst._tileX       = src._tileX;
            dst._tileY       = src._tileY;
            dst._targetMap   = src._targetMap;
            dst._targetTileX = src._targetTileX;
            dst._targetTileY = src._targetTileY;
            dst._pairId      = src._pairId;
            xmlData._listWarp.push_back( dst );
        }
        xmlData._listEncounterEntry.reserve( _listEncounterEntry.size() );
        for ( const TileEncounterEntry& src : _listEncounterEntry )
        {
            TileMapXmlData::Encounter dst{};
            dst._speciesId = src._speciesId;
            dst._weight    = src._weight;
            xmlData._listEncounterEntry.push_back( dst );
        }
        return xmlData.save( assetRelativePath );
    }

    void TileMap::clear()
    {
        _name.clear();
        _sourcePath.clear();
        _scenePath.clear();
        _role.clear();
        _width  = 0;
        _height = 0;
        _spawnX = 1;
        _spawnY = 1;
        _listWalkable.clear();
        _listEncounter.clear();
        _listPassThrough.clear();
        _listVisual.clear();
        _listWarp.clear();
        _listEncounterEntry.clear();
        rebuildWarpIndex();
    }

    void TileMap::resize( int32 width, int32 height )
    {
        if ( width <= 0 || height <= 0 )
            return;
        _width             = width;
        _height            = height;
        const size_t count = static_cast<size_t>( _width * _height );
        _listWalkable.assign( count, 1 );
        _listEncounter.assign( count, 0 );
        _listPassThrough.assign( count, 0 );
        _listVisual.assign( count, TileVisual{} );
        _listWarp.clear();
        _listEncounterEntry.clear();
        rebuildWarpIndex();
    }

    string TileMap::pickEncounterSpeciesId() const
    {
        if ( _listEncounterEntry.empty() )
            return {};

        float32 total{ 0.0f };
        for ( const TileEncounterEntry& entry : _listEncounterEntry )
        {
            total += entry._weight > 0.0f ? entry._weight : 0.0f;
        }
        if ( total <= 0.0f )
            return _listEncounterEntry[0]._speciesId;

        // 누적 가중치 선택. 예전에는 합계를 구해놓고 함수 지역 static 라운드로빈을 써서
        // XML 의 확률 가중치가 통째로 무시됐고, static 이 모든 타일맵/스레드에 공유됐다.
        const float32 pick = MathUtil::getRandomRange( 0.0f, total );

        float32 accumulated{ 0.0f };
        for ( const TileEncounterEntry& entry : _listEncounterEntry )
        {
            const float32 weight = entry._weight > 0.0f ? entry._weight : 0.0f;
            if ( weight <= 0.0f )
                continue;

            accumulated += weight;
            if ( pick <= accumulated )
                return entry._speciesId;
        }

        // 부동소수 오차로 끝까지 못 고른 경우: 가중치가 있는 마지막 항목으로 떨어뜨린다.
        for ( size_t entryIndex = _listEncounterEntry.size(); entryIndex > 0; --entryIndex )
        {
            const TileEncounterEntry& entry = _listEncounterEntry[entryIndex - 1];
            if ( entry._weight > 0.0f )
                return entry._speciesId;
        }
        return _listEncounterEntry[0]._speciesId;
    }

    bool TileMap::isWalkable( int32 x, int32 y ) const
    {
        if ( inBounds( x, y ) == false )
            return false;
        return _listWalkable[indexOf( x, y )] != 0;
    }

    bool TileMap::isEncounterTile( int32 x, int32 y ) const
    {
        if ( inBounds( x, y ) == false )
            return false;
        return _listEncounter[indexOf( x, y )] != 0;
    }

    bool TileMap::isPassThrough( int32 x, int32 y ) const
    {
        if ( inBounds( x, y ) == false )
            return false;
        return _listPassThrough[indexOf( x, y )] != 0;
    }

    bool TileMap::isSolid( int32 x, int32 y ) const
    {
        return isWalkable( x, y ) == false;
    }

    TileFlags TileMap::getFlags( int32 x, int32 y ) const
    {
        TileFlags f = TileFlags::None;
        if ( inBounds( x, y ) == false )
            return TileFlags::Solid;
        if ( _listWalkable[indexOf( x, y )] != 0 )
            f = f | TileFlags::Walkable;
        else
            f = f | TileFlags::Solid;
        if ( _listEncounter[indexOf( x, y )] != 0 )
            f = f | TileFlags::Encounter;
        if ( _listPassThrough[indexOf( x, y )] != 0 )
            f = f | TileFlags::PassThrough;
        if ( findWarp( x, y ) != nullptr )
            f = f | TileFlags::Warp;
        return f;
    }

    void TileMap::rebuildWarpIndex()
    {
        _mapWarpIndex.clear();
        for ( size_t idx = 0; idx < _listWarp.size(); ++idx )
        {
            _mapWarpIndex[getWarpKey( _listWarp[idx]._tileX, _listWarp[idx]._tileY )] = idx;
        }
    }

    const TileWarp* TileMap::findWarp( int32 x, int32 y ) const
    {
        auto it = _mapWarpIndex.find( getWarpKey( x, y ) );
        if ( it != _mapWarpIndex.end() && it->second < _listWarp.size() )
            return &_listWarp[it->second];
        return nullptr;
    }

    TileVisual TileMap::getTileVisual( int32 x, int32 y ) const
    {
        if ( inBounds( x, y ) == false )
            return {};
        return _listVisual[indexOf( x, y )];
    }

    void TileMap::setWalkable( int32 x, int32 y, bool bWalkable )
    {
        if ( inBounds( x, y ) )
            _listWalkable[indexOf( x, y )] = bWalkable ? 1 : 0;
    }

    void TileMap::setEncounter( int32 x, int32 y, bool bEncounter )
    {
        if ( inBounds( x, y ) )
            _listEncounter[indexOf( x, y )] = bEncounter ? 1 : 0;
    }

    void TileMap::setPassThrough( int32 x, int32 y, bool bPassThrough )
    {
        if ( inBounds( x, y ) )
            _listPassThrough[indexOf( x, y )] = bPassThrough ? 1 : 0;
    }

    void TileMap::setTileVisual( int32 x, int32 y, const TileVisual& visual )
    {
        if ( inBounds( x, y ) )
            _listVisual[indexOf( x, y )] = visual;
    }

    void TileMap::setOrUpdateWarp( const TileWarp& warp )
    {
        auto it = _mapWarpIndex.find( getWarpKey( warp._tileX, warp._tileY ) );
        if ( it != _mapWarpIndex.end() && it->second < _listWarp.size() )
        {
            _listWarp[it->second] = warp;
            return;
        }
        _mapWarpIndex[getWarpKey( warp._tileX, warp._tileY )] = _listWarp.size();
        _listWarp.push_back( warp );
    }

    void TileMap::removeWarp( int32 x, int32 y )
    {
        _listWarp.erase( std::remove_if( _listWarp.begin(), _listWarp.end(),
                                         [x, y]( const TileWarp& warp )
        { return warp._tileX == x && warp._tileY == y; } ),
                         _listWarp.end() );
        rebuildWarpIndex();
    }

    void TileMap::paintEdgeWarpPreset( int32 edge, string_view targetMap, int32 tx, int32 ty )
    {
        if ( _width <= 0 || _height <= 0 || targetMap.empty() )
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
                for ( int32 tileX = 0; tileX < _width; ++tileX )
                {
                    stamp( tileX, 0 );
                }
                break;
            case 1: // E
                for ( int32 tileY = 0; tileY < _height; ++tileY )
                {
                    stamp( _width - 1, tileY );
                }
                break;
            case 2: // S
                for ( int32 tileX = 0; tileX < _width; ++tileX )
                {
                    stamp( tileX, _height - 1 );
                }
                break;
            case 3: // W
                for ( int32 tileY = 0; tileY < _height; ++tileY )
                {
                    stamp( 0, tileY );
                }
                break;
            default:
                break;
        }
    }

    void TileMap::debugLogTileHd2d( int32 x, int32 y ) const
    {
        [[maybe_unused]] const TileVisual tileVisual = getTileVisual( x, y );
        SW_LOG_TRACE( "tile (%#,%#) h=%# tint=(%#,%#,%#) flags walk=%# enc=%# pt=%#",
                      x, y, tileVisual._height, tileVisual._tintR, tileVisual._tintG, tileVisual._tintB,
                      isWalkable( x, y ) ? 1 : 0, isEncounterTile( x, y ) ? 1 : 0, isPassThrough( x, y ) ? 1 : 0 );
    }

    bool TileMap::inBounds( int32 x, int32 y ) const
    {
        return 0 <= x && x < _width && 0 <= y && y < _height;
    }
} // namespace sw
