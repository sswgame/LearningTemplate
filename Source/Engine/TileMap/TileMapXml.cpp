#include "pch.h"

#include "Engine/TileMap/TileMapXml.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"

#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Serialization/Xml/XmlDocument.h"
#include "Engine/TileMap/TileSetAsset.h"

namespace sw
{
    namespace
    {
        struct TileMapXmlInternal
        {
            /**
             * @brief 0~255 칸(높이 · 아틀라스 · 색)을 읽습니다. 범위를 넘으면 0 · 255 로 묶고 경고합니다.
             * @details 그대로 잘라 넣으면 색 "300" 이 44 · "-1" 이 255 가 된다(유니티 `Color32` 는 묶는다).
             */
            static uint8 readByteAttribute( const XmlNode& node, const utf8* pName, int32 fallback )
            {
                const int32 value = node.getAttributeInt( pName, fallback );
                if ( value < 0 || value > 255 )
                {
                    SW_LOG_WARNING( "Tile attribute '%#'=%# is outside 0..255 - clamped", pName, value );
                    return static_cast<uint8>( value < 0 ? 0 : 255 );
                }
                return static_cast<uint8>( value );
            }

            /** @brief 표가 레이어 값 순서이고, 본문으로 저장하는 레이어가 하나뿐인지 봅니다. */
            static constexpr bool isFlagLayerTableValid()
            {
                size_t textLayerCount{ 0 };
                for ( size_t layerIndex = 0; layerIndex < kTileFlagLayerCount; ++layerIndex )
                {
                    if ( kArrTileFlagLayerInfo[layerIndex]._layer != static_cast<TileFlagLayer>( layerIndex ) )
                        return false;
                    if ( kArrTileFlagLayerInfo[layerIndex]._pXmlAttribute == nullptr )
                        ++textLayerCount;
                }
                return textLayerCount <= 1;
            }

            /** @brief `<t>` 하나에서 레이어 값을 읽습니다. 본문 레이어는 "0" 으로 시작하면 0 이고, 없으면 기본값입니다. */
            static uint8 readFlag( const XmlNode& tileNode, const TileFlagLayerInfo& info )
            {
                if ( info._pXmlAttribute == nullptr )
                {
                    const utf8* pText = tileNode.getText();
                    if ( pText == nullptr || pText[0] == '\0' )
                        return info._defaultValue;
                    return pText[0] != '0' ? 1 : 0;
                }
                if ( tileNode.findAttribute( info._pXmlAttribute ) == nullptr )
                    return info._defaultValue;
                return tileNode.getAttributeInt( info._pXmlAttribute, 0 ) != 0 ? 1 : 0;
            }
        };

        static_assert( TileMapXmlInternal::isFlagLayerTableValid(), "kArrTileFlagLayerInfo must be ordered by TileFlagLayer and have at most one text layer" );
    } // namespace

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

        XmlNode root = doc.getRoot( "TileMap" );
        if ( root.isValid() == false )
        {
            SW_LOG_ERROR( "Missing <TileMap>" );
            return false;
        }

        root.takeChildText( "name", _name );
        _width  = root.getChildInt( "width", 0 );
        _height = root.getChildInt( "height", 0 );
        root.takeChildText( "scene", _scenePath );
        root.takeChildText( "role", _role );
        XmlNode spawn = root.findChild( "spawn" );
        if ( spawn.isValid() )
        {
            _spawnX = spawn.getAttributeInt( "x", _spawnX );
            _spawnY = spawn.getAttributeInt( "y", _spawnY );
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

        (void)resetTiles( _width, _height ); // 크기는 바로 위에서 확인했다
        const size_t count = static_cast<size_t>( _width ) * static_cast<size_t>( _height );

        const vector<uint8>& listWalkable    = getFlagLayer( TileFlagLayer::Walkable );
        const vector<uint8>& listEncounter   = getFlagLayer( TileFlagLayer::Encounter );
        const vector<uint8>& listPassThrough = getFlagLayer( TileFlagLayer::PassThrough );

        XmlNode tiles = root.findChild( "tiles" );
        if ( tiles.isValid() )
        {
            int32 index{ 0 };
            for ( XmlNode tileNode = tiles.findChild( "t" ); tileNode && index < static_cast<int32>( count );
                  tileNode         = tileNode.findNextSibling( "t" ), ++index )
            {
                const size_t elementIndex = static_cast<size_t>( index );
                for ( const TileFlagLayerInfo& info : kArrTileFlagLayerInfo )
                {
                    getFlagLayer( info._layer )[elementIndex] = TileMapXmlInternal::readFlag( tileNode, info );
                }

                // 높이 · 틴트가 없는 맵은 레이어 값으로 보기를 만든다.
                Visual tileVisual{};
                if ( tileNode.findAttribute( "h" ) != nullptr )
                    tileVisual._height = TileMapXmlInternal::readByteAttribute( tileNode, "h", 0 );
                else
                    tileVisual._height = listEncounter[elementIndex] != 0 ? 2 : ( listWalkable[elementIndex] != 0 ? 1 : 0 );

                if ( tileNode.findAttribute( "atlas" ) != nullptr )
                    tileVisual._atlasId = TileMapXmlInternal::readByteAttribute( tileNode, "atlas", 0 );

                const bool bHasTint = tileNode.findAttribute( "tr" ) != nullptr || tileNode.findAttribute( "tg" ) != nullptr || tileNode.findAttribute( "tb" ) != nullptr;
                if ( bHasTint )
                {
                    tileVisual._tintR = TileMapXmlInternal::readByteAttribute( tileNode, "tr", 255 );
                    tileVisual._tintG = TileMapXmlInternal::readByteAttribute( tileNode, "tg", 255 );
                    tileVisual._tintB = TileMapXmlInternal::readByteAttribute( tileNode, "tb", 255 );
                }
                else if ( listEncounter[elementIndex] != 0 )
                {
                    tileVisual._tintR = 120;
                    tileVisual._tintG = 190;
                    tileVisual._tintB = 90;
                }
                else if ( listWalkable[elementIndex] == 0 )
                {
                    tileVisual._tintR = 80;
                    tileVisual._tintG = 80;
                    tileVisual._tintB = 90;
                }
                else if ( listPassThrough[elementIndex] != 0 )
                {
                    tileVisual._tintR = 160;
                    tileVisual._tintG = 170;
                    tileVisual._tintB = 200;
                }
                _listVisual[elementIndex] = tileVisual;
            }
        }

        XmlNode warps = root.findChild( "warps" );
        if ( warps.isValid() )
        {
            for ( XmlNode warpNode = warps.findChild( "warp" ); warpNode; warpNode = warpNode.findNextSibling( "warp" ) )
            {
                Warp warp{};
                warp._tileX      = warpNode.getAttributeInt( "x", 0 );
                warp._tileY      = warpNode.getAttributeInt( "y", 0 );
                const utf8* pMap = warpNode.findAttribute( "map" );
                if ( pMap != nullptr )
                    warp._targetMap = pMap;
                warp._targetTileX = warpNode.getAttributeInt( "tx", 0 );
                warp._targetTileY = warpNode.getAttributeInt( "ty", 0 );
                const utf8* pPair = warpNode.findAttribute( "pair" );
                if ( pPair != nullptr )
                    warp._pairId = pPair;
                _listWarp.push_back( std::move( warp ) );
            }
        }

        // 타일 레이어 — 팔레트(브러시 이름)와 칸마다 팔레트 번호. 칸 수가 맞지 않거나 팔레트 밖 번호는 읽기 오류다.
        XmlNode tileLayer = root.findChild( "tileLayer" );
        if ( tileLayer.isValid() )
        {
            _tileSetPath    = string( tileLayer.getAttributeText( "tileSet" ) );
            XmlNode palette = tileLayer.findChild( "palette" );
            for ( XmlNode entry = palette.isValid() ? palette.findChild( "b" ) : XmlNode{}; entry; entry = entry.findNextSibling( "b" ) )
            {
                _listPaletteName.push_back( string( entry.getAttributeText( "name" ) ) );
            }
            const utf8*       pCells = tileLayer.findChildText( "cells" );
            const string_view cells  = ( pCells != nullptr ) ? string_view( pCells ) : string_view{};
            _listTileCell.reserve( count );
            size_t cursor = 0;
            while ( cursor < cells.size() )
            {
                while ( cursor < cells.size() && ( cells[cursor] == ' ' || cells[cursor] == '\n' || cells[cursor] == '\r' || cells[cursor] == '\t' ) )
                {
                    ++cursor;
                }
                const size_t start = cursor;
                while ( cursor < cells.size() && cells[cursor] >= '0' && cells[cursor] <= '9' )
                {
                    ++cursor;
                }
                if ( cursor == start )
                {
                    if ( cursor < cells.size() )
                    {
                        SW_LOG_ERROR( "TileMap '%#': <cells> holds something that is not a number", _name );
                        *this = {};
                        return false;
                    }
                    break;
                }
                int32 value = 0;
                if ( StringUtil::parseInt( cells.substr( start, cursor - start ), value ) == false || value < 0 ||
                     static_cast<size_t>( value ) > _listPaletteName.size() )
                {
                    SW_LOG_ERROR( "TileMap '%#': tile cell %# is outside the palette (%# names)", _name, value, static_cast<uint32>( _listPaletteName.size() ) );
                    *this = {};
                    return false;
                }
                _listTileCell.push_back( static_cast<uint16>( value ) );
            }
            if ( _listTileCell.size() != count )
            {
                SW_LOG_ERROR( "TileMap '%#': <cells> has %# values, the map has %# cells", _name, static_cast<uint32>( _listTileCell.size() ),
                              static_cast<uint32>( count ) );
                *this = {};
                return false;
            }
        }

        SW_LOG_INFO( "Loaded '%#' (%#×%#) scene=%# role=%#", _name, _width, _height, _scenePath, _role );
        return true;
    }

    bool TileMapXmlData::resetTiles( int32 width, int32 height )
    {
        if ( isSizeSupported( width, height ) == false )
            return false;

        _width             = width;
        _height            = height;
        const size_t count = static_cast<size_t>( width ) * static_cast<size_t>( height );
        for ( const TileFlagLayerInfo& info : kArrTileFlagLayerInfo )
        {
            getFlagLayer( info._layer ).assign( count, info._defaultValue );
        }
        _listVisual.assign( count, Visual{} );
        _listWarp.clear();
        if ( _tileSetPath.empty() == false || _listTileCell.empty() == false )
            _listTileCell.assign( count, 0 );
        return true;
    }

    string_view TileMapXmlData::getTileBrushName( int32 x, int32 y ) const
    {
        if ( x < 0 || y < 0 || x >= _width || y >= _height )
            return {};
        const size_t cellIndex = static_cast<size_t>( y ) * static_cast<size_t>( _width ) + static_cast<size_t>( x );
        if ( cellIndex >= _listTileCell.size() )
            return {};
        const uint16 value = _listTileCell[cellIndex];
        if ( value == 0 || value > _listPaletteName.size() )
            return {};
        return _listPaletteName[value - 1u];
    }

    bool TileMapXmlData::mapTileCells( const TileSetAsset& tileSet, vector<uint16>& outListBrushIndex ) const
    {
        vector<int32> listPaletteToBrush( _listPaletteName.size(), -1 );
        bool          bAllKnown = true;
        for ( size_t paletteIndex = 0; paletteIndex < _listPaletteName.size(); ++paletteIndex )
        {
            listPaletteToBrush[paletteIndex] = tileSet.findBrush( hashed_string( string_view( _listPaletteName[paletteIndex] ) ) );
            bAllKnown                        = bAllKnown && listPaletteToBrush[paletteIndex] >= 0;
        }
        const size_t count = static_cast<size_t>( _width ) * static_cast<size_t>( _height );
        outListBrushIndex.assign( count, 0 );
        for ( size_t cellIndex = 0; cellIndex < count && cellIndex < _listTileCell.size(); ++cellIndex )
        {
            const uint16 paletteValue = _listTileCell[cellIndex];
            if ( paletteValue == 0 || paletteValue > listPaletteToBrush.size() )
                continue;
            const int32 brush = listPaletteToBrush[paletteValue - 1u];
            if ( brush >= 0 )
                outListBrushIndex[cellIndex] = static_cast<uint16>( brush + 1 );
        }
        return bAllKnown;
    }

    bool TileMapXmlData::setTileBrush( int32 x, int32 y, string_view brushName )
    {
        if ( x < 0 || y < 0 || x >= _width || y >= _height )
            return false;
        const size_t count = static_cast<size_t>( _width ) * static_cast<size_t>( _height );
        if ( _listTileCell.size() != count )
            _listTileCell.assign( count, 0 );
        uint16 value = 0;
        if ( brushName.empty() == false )
        {
            size_t paletteIndex = 0;
            while ( paletteIndex < _listPaletteName.size() && _listPaletteName[paletteIndex] != brushName )
            {
                ++paletteIndex;
            }
            if ( paletteIndex == _listPaletteName.size() )
                _listPaletteName.push_back( string( brushName ) );
            value = static_cast<uint16>( paletteIndex + 1 );
        }
        _listTileCell[static_cast<size_t>( y ) * static_cast<size_t>( _width ) + static_cast<size_t>( x )] = value;
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

        // **네 배열이 `_width × _height` 와 같다는 보장은 이 구조체에 없다.** 필드가 모두 공개라
        // 크기만 바꾸고 칸을 늘리지 않은 채로 저장할 수 있고, 그러면 여기서 남의 메모리를 읽어 파일에
        // 적는다(Debug 는 vector assert 에서 죽고, 배포본은 조용히 쓰레기 값을 쓴다). 모자란 칸은
        // **읽기 쪽 기본값**으로 적는다. `loadFromXml` 이 `<t>` 가 없을 때 넣는 값과 같아서
        // 왕복이 어긋나지 않는다(통행 가능 · 조우 없음 · 기본 틴트).
        size_t tileCount = MathUtil::min( count, _listVisual.size() );
        for ( const vector<uint8>& listFlag : _arrFlagLayer )
        {
            tileCount = MathUtil::min( tileCount, listFlag.size() );
        }
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
            // 레이어는 표 순서로 적는다. 속성은 기본값과 다를 때만 적고, 본문 레이어는 맨 끝 본문이 된다.
            const utf8* pTextFlag = nullptr;
            for ( const TileFlagLayerInfo& info : kArrTileFlagLayerInfo )
            {
                const uint8 value = bHasTile ? getFlagLayer( info._layer )[tileIndex] : info._defaultValue;
                if ( info._pXmlAttribute == nullptr )
                    pTextFlag = value != 0 ? "1" : "0";
                else if ( ( value != 0 ) != ( info._defaultValue != 0 ) )
                    tileNode.appendAttribute( info._pXmlAttribute, value != 0 ? 1 : 0 );
            }
            if ( tileVisual._atlasId != 0 )
                tileNode.appendAttribute( "atlas", static_cast<int32>( tileVisual._atlasId ) );
            tileNode.appendAttribute( "tr", static_cast<int32>( tileVisual._tintR ) );
            tileNode.appendAttribute( "tg", static_cast<int32>( tileVisual._tintG ) );
            tileNode.appendAttribute( "tb", static_cast<int32>( tileVisual._tintB ) );
            if ( pTextFlag != nullptr )
                tileNode.setValue( pTextFlag );
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

        // 타일 레이어는 타일셋이 있을 때만 쓴다 — 없는 맵은 예전과 바이트까지 같다. 칸은 한 행씩 줄을 바꿔 적는다(사람이 읽고 비교할 수 있게).
        if ( _tileSetPath.empty() == false )
        {
            XmlNode tileLayer = root.appendChild( "tileLayer" );
            tileLayer.appendAttribute( "tileSet", _tileSetPath );
            XmlNode palette = tileLayer.appendChild( "palette" );
            for ( const string& name : _listPaletteName )
            {
                palette.appendChild( "b" ).appendAttribute( "name", name );
            }
            string cells = "\n";
            for ( int32 y = 0; y < _height; ++y )
            {
                for ( int32 x = 0; x < _width; ++x )
                {
                    const size_t cellIndex = static_cast<size_t>( y ) * static_cast<size_t>( _width ) + static_cast<size_t>( x );
                    const uint16 value     = ( cellIndex < _listTileCell.size() ) ? _listTileCell[cellIndex] : 0;
                    cells += to_string( static_cast<uint32>( value ) );
                    cells += ( x + 1 < _width ) ? " " : "\n";
                }
            }
            tileLayer.appendChild( "cells", string_view{ cells } );
        }

        return doc.saveToString();
    }
} // namespace sw
