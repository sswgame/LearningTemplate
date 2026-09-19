#include "pch.h"

#include "Engine/Utility/Xml/TileMapXml.h"

#include "Core/File/FileUtil.h"

#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Utility/Xml/XmlDocument.h"

namespace sw
{
    SW_LOG_CALLER( "TileMapXml" );

    bool TileMapXmlData::load( string_view path )
    {
        if ( path.empty() )
            return false;

        string text;
        string absPath;
        if ( ResourceUtil::readTextResource( path, text, &absPath ) == false && FileUtil::readTextFile( path, text ) == false )
        {
            SW_LOG_ERROR( "TileMap file not found: %#", path );
            return false;
        }

        if ( loadFromXml( text ) == false )
            return false;
        _sourcePath = path;
        return true;
    }

    bool TileMapXmlData::loadFromXml( string_view xml )
    {
        *this = {};

        XmlDocument doc;
        if ( doc.parse( xml ) == false )
            return false;

        XmlNode root = doc.root( "TileMap" );
        if ( root.isValid() == false )
        {
            SW_LOG_ERROR( "Missing <TileMap>" );
            return false;
        }

        root.takeChildText( "name", _name );
        _width  = root.childInt( "width", 0 );
        _height = root.childInt( "height", 0 );
        root.takeChildText( "scene", _scenePath );
        root.takeChildText( "role", _role );
        XmlNode spawn = root.child( "spawn" );
        if ( spawn.isValid() )
        {
            _spawnX = spawn.attributeInt( "x", _spawnX );
            _spawnY = spawn.attributeInt( "y", _spawnY );
        }

        if ( _width <= 0 || _height <= 0 )
        {
            _width  = 8;
            _height = 8;
        }

        // **파일이 말한 크기를 그대로 잡지 않는다.** 바로 아래에서 `_width x _height` 칸짜리
        // 배열 넷을 잡으므로, 손상되거나 손으로 잘못 적은 맵 하나가 수십억 칸 요청이 된다.
        if ( isSizeSupported( _width, _height ) == false )
        {
            SW_LOG_ERROR( "TileMap size %#x%# is beyond the supported tile count (%#)", _width, _height, kMaxTileCount );
            *this = {};
            return false;
        }

        const size_t count = static_cast<size_t>( _width ) * static_cast<size_t>( _height );
        _listWalkable.assign( count, 1 );
        _listEncounter.assign( count, 0 );
        _listPassThrough.assign( count, 0 );
        _listVisual.assign( count, Visual{} );

        XmlNode tiles = root.child( "tiles" );
        if ( tiles.isValid() )
        {
            int32 index{ 0 };
            for ( XmlNode tileNode = tiles.child( "t" ); tileNode && index < static_cast<int32>( count );
                  tileNode         = tileNode.next( "t" ), ++index )
            {
                const utf8*  pText             = tileNode.text();
                const size_t elementIndex      = static_cast<size_t>( index );
                _listWalkable[elementIndex]    = ( pText == nullptr || pText[0] != '0' ) ? 1 : 0;
                _listEncounter[elementIndex]   = tileNode.attributeInt( "enc", 0 ) != 0 ? 1 : 0;
                _listPassThrough[elementIndex] = tileNode.attributeInt( "pt", 0 ) != 0 ? 1 : 0;

                Visual tileVisual{};
                if ( tileNode.attribute( "h" ) != nullptr )
                    tileVisual._height = static_cast<uint8>( tileNode.attributeInt( "h", 0 ) );
                else
                    tileVisual._height = _listEncounter[elementIndex] != 0 ? 2 : ( _listWalkable[elementIndex] != 0 ? 1 : 0 );

                if ( tileNode.attribute( "atlas" ) != nullptr )
                    tileVisual._atlasId = static_cast<uint8>( tileNode.attributeInt( "atlas", 0 ) );

                const bool bHasTint = tileNode.attribute( "tr" ) != nullptr || tileNode.attribute( "tg" ) != nullptr || tileNode.attribute( "tb" ) != nullptr;
                if ( bHasTint )
                {
                    tileVisual._tintR = static_cast<uint8>( tileNode.attributeInt( "tr", 255 ) );
                    tileVisual._tintG = static_cast<uint8>( tileNode.attributeInt( "tg", 255 ) );
                    tileVisual._tintB = static_cast<uint8>( tileNode.attributeInt( "tb", 255 ) );
                }
                else if ( _listEncounter[elementIndex] != 0 )
                {
                    tileVisual._tintR = 120;
                    tileVisual._tintG = 190;
                    tileVisual._tintB = 90;
                }
                else if ( _listWalkable[elementIndex] == 0 )
                {
                    tileVisual._tintR = 80;
                    tileVisual._tintG = 80;
                    tileVisual._tintB = 90;
                }
                else if ( _listPassThrough[elementIndex] != 0 )
                {
                    tileVisual._tintR = 160;
                    tileVisual._tintG = 170;
                    tileVisual._tintB = 200;
                }
                _listVisual[elementIndex] = tileVisual;
            }
        }

        XmlNode warps = root.child( "warps" );
        if ( warps.isValid() )
        {
            for ( XmlNode warpNode = warps.child( "warp" ); warpNode; warpNode = warpNode.next( "warp" ) )
            {
                Warp warp{};
                warp._tileX      = warpNode.attributeInt( "x", 0 );
                warp._tileY      = warpNode.attributeInt( "y", 0 );
                const utf8* pMap = warpNode.attribute( "map" );
                if ( pMap != nullptr )
                    warp._targetMap = pMap;
                warp._targetTileX = warpNode.attributeInt( "tx", 0 );
                warp._targetTileY = warpNode.attributeInt( "ty", 0 );
                const utf8* pPair = warpNode.attribute( "pair" );
                if ( pPair != nullptr )
                    warp._pairId = pPair;
                _listWarp.push_back( std::move( warp ) );
            }
        }

        XmlNode encounters = root.child( "encounters" );
        if ( encounters.isValid() )
        {
            for ( XmlNode encNode = encounters.child( "e" ); encNode; encNode = encNode.next( "e" ) )
            {
                Encounter   entry{};
                const utf8* pId = encNode.attribute( "id" );
                if ( pId != nullptr )
                    entry._speciesId = pId;
                entry._weight = encNode.attributeFloat( "weight", 0.f );
                if ( entry._speciesId.empty() == false )
                    _listEncounterEntry.push_back( std::move( entry ) );
            }
        }

        SW_LOG_INFO( "Loaded '%#' (%#×%#) scene=%# role=%# encounters=%#",
                     _name, _width, _height, _scenePath, _role,
                     static_cast<uint32>( _listEncounterEntry.size() ) );
        return true;
    }

    bool TileMapXmlData::save( string_view path ) const
    {
        string absPath = ResourceUtil::getResourcePath( path );
        if ( absPath.empty() )
            absPath = path;

        const string xml = toXml();
        if ( xml.empty() )
            return false;
        const bool bOk = FileUtil::writeTextFile( absPath, xml );
        if ( bOk )
            SW_LOG_INFO( "Saved '%#' -> %#", _name, absPath );
        return bOk;
    }

    string TileMapXmlData::toXml() const
    {
        XmlDocument doc;
        XmlNode     root = doc.appendRoot( "TileMap" );
        root.appendChild( "name", _name.empty() ? string_view{ "Untitled" } : string_view{ _name } );
        root.appendChild( "width", _width );
        root.appendChild( "height", _height );
        if ( _scenePath.empty() == false )
            root.appendChild( "scene", _scenePath );
        if ( _role.empty() == false )
            root.appendChild( "role", _role );

        XmlNode spawn = root.appendChild( "spawn" );
        spawn.appendAttribute( "x", _spawnX );
        spawn.appendAttribute( "y", _spawnY );

        XmlNode      tiles = root.appendChild( "tiles" );
        const size_t count = static_cast<size_t>( _width ) * static_cast<size_t>( _height );

        // **네 배열이 `_width × _height` 와 같다는 보장은 이 구조체에 없다.** 필드가 전부 공개라
        // 크기만 바꾸고 칸을 안 늘린 채로 저장할 수 있고, 그러면 여기서 남의 메모리를 읽어 파일에
        // 적는다(Debug 는 vector 단언에서 죽고, 배포본은 조용히 쓰레기를 쓴다). 모자란 칸은
        // **읽기 쪽 기본값**으로 적는다 — `loadFromXml` 이 `<t>` 가 없을 때 넣는 값과 같아서
        // 왕복이 어긋나지 않는다(통행 가능 · 조우 없음 · 기본 틴트).
        size_t tileCount = MathUtil::min( count, _listWalkable.size() );
        tileCount        = MathUtil::min( tileCount, _listEncounter.size() );
        tileCount        = MathUtil::min( tileCount, _listPassThrough.size() );
        tileCount        = MathUtil::min( tileCount, _listVisual.size() );
        if ( tileCount < count )
        {
            SW_LOG_WARNING( "타일 배열이 %#×%# 보다 짧습니다(%# 칸) — 모자란 칸은 기본값으로 적습니다.",
                            _width, _height, static_cast<uint32>( tileCount ) );
        }

        const Visual defaultVisual{};
        for ( size_t tileIndex = 0; tileIndex < count; ++tileIndex )
        {
            const bool    bHasTile   = tileIndex < tileCount;
            XmlNode       tileNode   = tiles.appendChild( "t" );
            const Visual& tileVisual = bHasTile ? _listVisual[tileIndex] : defaultVisual;
            tileNode.appendAttribute( "h", static_cast<int32>( tileVisual._height ) );
            if ( bHasTile && _listEncounter[tileIndex] != 0 )
                tileNode.appendAttribute( "enc", 1 );
            if ( bHasTile && _listPassThrough[tileIndex] != 0 )
                tileNode.appendAttribute( "pt", 1 );
            if ( tileVisual._atlasId != 0 )
                tileNode.appendAttribute( "atlas", static_cast<int32>( tileVisual._atlasId ) );
            tileNode.appendAttribute( "tr", static_cast<int32>( tileVisual._tintR ) );
            tileNode.appendAttribute( "tg", static_cast<int32>( tileVisual._tintG ) );
            tileNode.appendAttribute( "tb", static_cast<int32>( tileVisual._tintB ) );
            tileNode.setValue( ( bHasTile == false || _listWalkable[tileIndex] != 0 ) ? "1" : "0" );
        }

        XmlNode warps = root.appendChild( "warps" );
        for ( const Warp& warp : _listWarp )
        {
            XmlNode warpNode = warps.appendChild( "warp" );
            warpNode.appendAttribute( "x", warp._tileX );
            warpNode.appendAttribute( "y", warp._tileY );
            warpNode.appendAttribute( "map", warp._targetMap );
            warpNode.appendAttribute( "tx", warp._targetTileX );
            warpNode.appendAttribute( "ty", warp._targetTileY );
            if ( warp._pairId.empty() == false )
                warpNode.appendAttribute( "pair", warp._pairId );
        }

        if ( _listEncounterEntry.empty() == false )
        {
            XmlNode encounters = root.appendChild( "encounters" );
            for ( const Encounter& entry : _listEncounterEntry )
            {
                XmlNode encNode = encounters.appendChild( "e" );
                encNode.appendAttribute( "id", entry._speciesId );
                encNode.appendAttribute( "weight", entry._weight );
            }
        }

        return doc.saveToString();
    }
} // namespace sw
