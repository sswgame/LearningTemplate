#include "pch.h"

#include "Engine/TileMap/TileMapXML.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"

#include "Engine/FileSystem/AssetIO.h"
#include "Engine/FileSystem/AssetPaths.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/TileMap/TileSetAsset.h"

namespace sw
{
    SW_LOG_CALLER( "TileMapXML" );

    namespace
    {
        struct TileMapXMLInternal
        {
            /**
             * @brief 0~255 값(높이, 아틀라스, 색)을 읽습니다. 없으면 @p inoutValue 를 둡니다(타일 비주얼의 초기값).
             * @details 범위 밖 값은 자르지 않고 오류다 — 잘라 넣으면 색 "300" 이 44 가, "-1" 이 255 가 되고, 255 로 제한해도 데이터의 실수가 다른 색으로 조용히 돈다.
             */
            [[nodiscard]] static bool readByteAttribute( const XMLNode& node, const utf8* pName, uint8& inoutValue )
            {
                int32 value = inoutValue;
                if ( node.readOptionalAttributeInRange( pName, 0, kMaxByte, value ) == false )
                    return false;
                inoutValue = static_cast<uint8>( value );
                return true;
            }

            static constexpr int32 kMaxByte = 255; ///< 높이 · 아틀라스 · 색 칸이 uint8 이다

            /** @brief 테이블이 레이어 값 순서인지, 본문으로 저장하는 레이어가 하나뿐인지 확인합니다. */
            static constexpr bool isFlagLayerTableValid()
            {
                size_t textLayerCount{ 0 };
                for ( size_t layerIndex = 0; layerIndex < kTileFlagLayerCount; ++layerIndex )
                {
                    if ( kArrTileFlagLayerInfo[layerIndex]._layer != static_cast<TileFlagLayer>( layerIndex ) )
                        return false;
                    if ( kArrTileFlagLayerInfo[layerIndex]._pXMLAttribute == nullptr )
                        ++textLayerCount;
                }
                return textLayerCount <= 1;
            }

            static constexpr string_view kValueListSeparators = " \n\r\t"; ///< 레이어 값 정수 목록의 구분 글자(공백 · 줄바꿈 · 탭)

            /** @brief `<t>` 하나에서 레이어 값을 읽습니다. 본문 레이어는 "0" 으로 시작하면 0 이고, 없으면 레이어 표의 기본값입니다. 깨진 숫자는 false 입니다. */
            [[nodiscard]] static bool readFlag( const XMLNode& tileNode, const TileFlagLayerInfo& info, uint8& outFlag )
            {
                outFlag = info._defaultValue;
                if ( info._pXMLAttribute == nullptr )
                {
                    const utf8* pText = tileNode.getText();
                    if ( pText != nullptr && pText[0] != '\0' )
                        outFlag = pText[0] != '0' ? 1 : 0;
                    return true;
                }
                std::optional<int32> value;
                if ( tileNode.readOptionalAttribute( info._pXMLAttribute, value ) == false )
                    return false;
                if ( value.has_value() )
                    outFlag = *value != 0 ? 1 : 0;
                return true;
            }

            // ------------------------------------------------------------------------------
            // 읽기 단계 — `loadFromXML` 이 위에서 아래로 부른다. false 를 돌려주는 단계는 읽기 오류(로그를 남겼다)라 호출한 쪽이 데이터를 비운다.
            // 깨진 속성값은 읽기를 이어 가며 @p inoutValid 만 내린다(한 번에 모든 오류를 로그로 보인다).
            // ------------------------------------------------------------------------------

            /**
             * @brief 이름 · 크기 · 장면 · 역할 · 시작 위치를 읽습니다.
             * @details 크기는 필수이고 1 이상이다 — 빠지거나 깨진 크기를 다른 크기(옛 8x8)로 바꿔 읽지 않는다. 크기는 지원 범위(`isSizeSupported`) 안이어야 한다:
             *          파일이 말한 크기를 그대로 잡지 않는다 — 바로 뒤에서 `_width x _height` 셀짜리 배열 넷을 잡으므로, 손상되거나 손으로 잘못 적은 맵 하나가 수십억 셀 요청이 된다.
             */
            [[nodiscard]] static bool readHeader( const XMLNode& root, TileMapXMLData& data, bool& inoutValid )
            {
                root.takeChildText( "name", data._name );
                const std::optional<int32> width      = root.readRequiredChild<int32>( "width" );
                const std::optional<int32> height     = root.readRequiredChild<int32>( "height" );
                const bool                 bValidSize = width.has_value() && height.has_value() && *width > 0 && *height > 0;
                if ( bValidSize == false )
                {
                    SW_LOG_ERROR( "<TileMap> needs <width> and <height> of at least 1" );
                    return false;
                }
                data._width  = *width;
                data._height = *height;
                root.takeChildText( "scene", data._scenePath );
                root.takeChildText( "role", data._role );
                const XMLNode spawn = root.findChild( "spawn" );
                if ( spawn.isValid() )
                {
                    inoutValid = spawn.readOptionalAttributeInRange( "x", 0, data._width - 1, data._spawnX ) && inoutValid;
                    inoutValid = spawn.readOptionalAttributeInRange( "y", 0, data._height - 1, data._spawnY ) && inoutValid;
                }
                if ( TileMapXMLData::isSizeSupported( data._width, data._height ) == false )
                {
                    SW_LOG_ERROR( "TileMap size %#x%# is beyond the supported tile count (%#)", data._width, data._height, TileMapXMLData::kMaxTileCount );
                    return false;
                }
                return true;
            }

            /** @brief `<t>` 하나에서 타일 비주얼(높이 · 아틀라스 · 틴트)을 읽습니다. 높이와 틴트가 없는 타일은 레이어 값으로 비주얼을 만든다(그 타일의 데이터에서 온 값). */
            static void readTileVisual( const XMLNode& tileNode, int32 index, bool bEncounter, bool bWalkable, bool bPassThrough, TileMapXMLData::Visual& outVisual, bool& inoutValid )
            {
                outVisual._height = bEncounter ? 2 : ( bWalkable ? 1 : 0 );
                inoutValid        = readByteAttribute( tileNode, "h", outVisual._height ) && inoutValid;
                inoutValid        = readByteAttribute( tileNode, "atlas", outVisual._atlasID ) && inoutValid;

                const bool bHasTint = tileNode.findAttribute( "tr" ) != nullptr || tileNode.findAttribute( "tg" ) != nullptr || tileNode.findAttribute( "tb" ) != nullptr;
                if ( bHasTint )
                {
                    // 틴트는 세 성분을 함께 적는다 — 빠진 성분을 어떤 값으로도 메우지 않는다.
                    const bool bFullTint = tileNode.findAttribute( "tr" ) != nullptr && tileNode.findAttribute( "tg" ) != nullptr && tileNode.findAttribute( "tb" ) != nullptr;
                    if ( bFullTint == false )
                    {
                        SW_LOG_ERROR( "TileMap tile %# needs all of tr, tg and tb", index );
                        inoutValid = false;
                    }
                    inoutValid = readByteAttribute( tileNode, "tr", outVisual._tintR ) && inoutValid;
                    inoutValid = readByteAttribute( tileNode, "tg", outVisual._tintG ) && inoutValid;
                    inoutValid = readByteAttribute( tileNode, "tb", outVisual._tintB ) && inoutValid;
                }
                else if ( bEncounter )
                {
                    outVisual._tintR = 120;
                    outVisual._tintG = 190;
                    outVisual._tintB = 90;
                }
                else if ( bWalkable == false )
                {
                    outVisual._tintR = 80;
                    outVisual._tintG = 80;
                    outVisual._tintB = 90;
                }
                else if ( bPassThrough )
                {
                    outVisual._tintR = 160;
                    outVisual._tintG = 170;
                    outVisual._tintB = 200;
                }
            }

            /** @brief `<tiles>` 의 `<t>` 를 셀 수(@p count)까지 읽어 플래그 레이어와 비주얼을 채웁니다. */
            static void readTiles( const XMLNode& tiles, TileMapXMLData& data, size_t count, bool& inoutValid )
            {
                int32 index{ 0 };
                for ( XMLNode tileNode = tiles.findChild( "t" ); tileNode && index < static_cast<int32>( count ); tileNode = tileNode.findNextSibling( "t" ), ++index )
                {
                    const size_t elementIndex = static_cast<size_t>( index );
                    for ( const TileFlagLayerInfo& info : kArrTileFlagLayerInfo )
                    {
                        inoutValid = readFlag( tileNode, info, data.getFlagLayer( info._layer )[elementIndex] ) && inoutValid;
                    }
                    const bool bEncounter   = data.getFlagLayer( TileFlagLayer::Encounter )[elementIndex] != 0;
                    const bool bWalkable    = data.getFlagLayer( TileFlagLayer::Walkable )[elementIndex] != 0;
                    const bool bPassThrough = data.getFlagLayer( TileFlagLayer::PassThrough )[elementIndex] != 0;

                    TileMapXMLData::Visual tileVisual{};
                    readTileVisual( tileNode, index, bEncounter, bWalkable, bPassThrough, tileVisual, inoutValid );
                    data._listVisual[elementIndex] = tileVisual;
                }
            }

            /** @brief `<warps>` 의 `<warp>` 를 읽습니다. 워프의 자리와 도착 자리는 필수다 — 빠진 좌표를 (0, 0) 으로 두지 않고 그 워프를 건너뛴 채 @p inoutValid 를 내린다. */
            static void readWarps( const XMLNode& warps, TileMapXMLData& data, bool& inoutValid )
            {
                for ( XMLNode warpNode = warps.findChild( "warp" ); warpNode; warpNode = warpNode.findNextSibling( "warp" ) )
                {
                    TileMapXMLData::Warp       warp{};
                    const std::optional<int32> tileX       = warpNode.readRequiredAttribute<int32>( "x" );
                    const std::optional<int32> tileY       = warpNode.readRequiredAttribute<int32>( "y" );
                    const std::optional<int32> targetTileX = warpNode.readRequiredAttribute<int32>( "tx" );
                    const std::optional<int32> targetTileY = warpNode.readRequiredAttribute<int32>( "ty" );
                    const bool                 bComplete   = tileX.has_value() && tileY.has_value() && targetTileX.has_value() && targetTileY.has_value();
                    if ( bComplete == false )
                    {
                        inoutValid = false;
                        continue;
                    }
                    warp._tileX      = *tileX;
                    warp._tileY      = *tileY;
                    const utf8* pMap = warpNode.findAttribute( "map" );
                    if ( pMap != nullptr )
                        warp._targetMap = pMap;
                    warp._targetTileX = *targetTileX;
                    warp._targetTileY = *targetTileY;
                    const utf8* pPair = warpNode.findAttribute( "pair" );
                    if ( pPair != nullptr )
                        warp._pairID = pPair;
                    data._listWarp.push_back( std::move( warp ) );
                }
            }

            /** @brief `<cells>` 본문의 팔레트 번호 목록을 읽어 @p data 의 타일 셀에 채웁니다. 숫자가 아닌 글자나 팔레트 밖 번호, 셀 수가 안 맞는 목록은 false 입니다. */
            [[nodiscard]] static bool readTileCells( string_view cells, TileMapXMLData& data, size_t count )
            {
                data._listTileCell.reserve( count );
                size_t cursor = 0;
                while ( cursor < cells.size() )
                {
                    while ( cursor < cells.size() && ( cells[cursor] == ' ' || cells[cursor] == '\n' || cells[cursor] == '\r' || cells[cursor] == '\t' ) )
                    {
                        ++cursor;
                    }
                    const size_t start = cursor;
                    while ( cursor < cells.size() && StringUtil::isDigitChar( cells[cursor] ) )
                    {
                        ++cursor;
                    }
                    if ( cursor == start )
                    {
                        if ( cursor < cells.size() )
                        {
                            SW_LOG_ERROR( "TileMap '%#': <cells> holds something that is not a number", data._name );
                            return false;
                        }
                        break;
                    }
                    int32      value                = 0;
                    const bool bInvalidPaletteIndex = StringUtil::parseInt( cells.substr( start, cursor - start ), value ) == false || value < 0 ||
                                                      static_cast<size_t>( value ) > data._listPaletteName.size();
                    if ( bInvalidPaletteIndex )
                    {
                        SW_LOG_ERROR( "TileMap '%#': tile cell %# is outside the palette (%# names)", data._name, value, static_cast<uint32>( data._listPaletteName.size() ) );
                        return false;
                    }
                    data._listTileCell.push_back( static_cast<uint16>( value ) );
                }
                if ( data._listTileCell.size() != count )
                {
                    SW_LOG_ERROR( "TileMap '%#': <cells> has %# values, the map has %# cells", data._name, static_cast<uint32>( data._listTileCell.size() ), static_cast<uint32>( count ) );
                    return false;
                }
                return true;
            }

            /** @brief `<tileLayer>` 의 타일셋 경로 · 팔레트(브러시 이름) · 셀마다 팔레트 번호를 읽습니다. 셀 수가 맞지 않거나 팔레트 밖 번호는 읽기 오류(false)입니다. */
            [[nodiscard]] static bool readTileLayer( const XMLNode& tileLayer, TileMapXMLData& data, size_t count )
            {
                data._tileSetPath     = string( tileLayer.getAttributeText( "tileSet" ) );
                const XMLNode palette = tileLayer.findChild( "palette" );
                for ( XMLNode entry = palette.isValid() ? palette.findChild( "b" ) : XMLNode{}; entry; entry = entry.findNextSibling( "b" ) )
                {
                    data._listPaletteName.push_back( string( entry.getAttributeText( "name" ) ) );
                }
                const utf8*       pCells = tileLayer.findChildText( "cells" );
                const string_view cells  = ( pCells != nullptr ) ? string_view( pCells ) : string_view{};
                return readTileCells( cells, data, count );
            }

            /** @brief `<dataLayers>` 의 `<layer>` 를 읽습니다. 이름 · 기본값 필수, 이름은 겹치지 않고, 값 수 = 셀 수. 하나라도 어긋나면 false 입니다(모자란 칸을 기본값으로 메우지 않는다). */
            [[nodiscard]] static bool readDataLayers( const XMLNode& dataLayers, TileMapXMLData& data, size_t count )
            {
                for ( XMLNode layerNode = dataLayers.isValid() ? dataLayers.findChild( "layer" ) : XMLNode{}; layerNode; layerNode = layerNode.findNextSibling( "layer" ) )
                {
                    TileMapXMLData::DataLayer  layer;
                    const std::optional<int32> defaultValue = layerNode.readRequiredAttribute<int32>( "default" );
                    layer._name                             = string( layerNode.getAttributeText( "name" ) );
                    const bool bHeaderValid                 = layer._name.empty() == false && defaultValue.has_value() && data.findDataLayer( layer._name ) < 0;
                    if ( bHeaderValid == false )
                    {
                        SW_LOG_ERROR( "TileMap '%#': <dataLayers><layer> needs a unique non-empty name and a default (name '%#')", data._name, layer._name );
                        return false;
                    }
                    layer._defaultValue = *defaultValue;
                    const utf8* pValues = layerNode.getText();
                    const bool  bValuesValid =
                        StringUtil::parseIntList( pValues != nullptr ? string_view( pValues ) : string_view{}, kValueListSeparators, layer._listValue ) && layer._listValue.size() == count;
                    if ( bValuesValid == false )
                    {
                        SW_LOG_ERROR( "TileMap '%#': data layer '%#' needs %# integer values (one per cell)", data._name, layer._name, static_cast<uint32>( count ) );
                        return false;
                    }
                    data._listDataLayer.push_back( std::move( layer ) );
                }
                return true;
            }

            // ------------------------------------------------------------------------------
            // 쓰기 단계 — `toXML` 이 위에서 아래로 부른다.
            // ------------------------------------------------------------------------------

            /**
             * @brief `<tiles>` 아래에 셀마다 `<t>` 를 씁니다.
             * @details **네 배열이 `_width × _height` 와 같다는 보장은 이 구조체에 없다.** 필드가 모두 공개라 크기만 바꾸고 셀을 늘리지 않은 채로 저장할 수 있고, 그러면 여기서 남의 메모리를 읽어 파일에
             *          적는다(Debug 는 vector assert 에서 죽고, 배포본은 조용히 쓰레기 값을 쓴다). 모자란 셀은 **읽기 쪽 기본값**으로 적는다. `loadFromXML` 이 `<t>` 가 없을 때 넣는 값과 같아서
             *          왕복이 어긋나지 않는다(통행 가능, 조우 없음, 기본 틴트).
             */
            static void appendTiles( const XMLNode& tiles, const TileMapXMLData& data )
            {
                const size_t count     = static_cast<size_t>( data._width ) * static_cast<size_t>( data._height );
                size_t       tileCount = MathUtil::min( count, data._listVisual.size() );
                for ( const vector<uint8>& listFlag : data._arrFlagLayer )
                {
                    tileCount = MathUtil::min( tileCount, listFlag.size() );
                }
                if ( tileCount < count )
                    SW_LOG_WARNING( "The tile array is shorter than %#x%# (%# cells) - missing cells are written as defaults.", data._width, data._height, static_cast<uint32>( tileCount ) );

                const TileMapXMLData::Visual defaultVisual{};
                for ( size_t tileIndex = 0; tileIndex < count; ++tileIndex )
                {
                    const bool                    bHasTile   = tileIndex < tileCount;
                    const XMLNode                 tileNode   = tiles.appendChild( "t" );
                    const TileMapXMLData::Visual& tileVisual = bHasTile ? data._listVisual[tileIndex] : defaultVisual;
                    tileNode.appendAttribute( "h", static_cast<int32>( tileVisual._height ) );
                    // 레이어는 테이블 순서로 적는다. 속성은 기본값과 다를 때만 적고, 본문 레이어는 맨 끝 본문이 된다.
                    const utf8* pTextFlag = nullptr;
                    for ( const TileFlagLayerInfo& info : kArrTileFlagLayerInfo )
                    {
                        const uint8 value = bHasTile ? data.getFlagLayer( info._layer )[tileIndex] : info._defaultValue;
                        if ( info._pXMLAttribute == nullptr )
                            pTextFlag = value != 0 ? "1" : "0";
                        else if ( ( value != 0 ) != ( info._defaultValue != 0 ) )
                            tileNode.appendAttribute( info._pXMLAttribute, value != 0 ? 1 : 0 );
                    }
                    if ( tileVisual._atlasID != 0 )
                        tileNode.appendAttribute( "atlas", static_cast<int32>( tileVisual._atlasID ) );
                    tileNode.appendAttribute( "tr", static_cast<int32>( tileVisual._tintR ) );
                    tileNode.appendAttribute( "tg", static_cast<int32>( tileVisual._tintG ) );
                    tileNode.appendAttribute( "tb", static_cast<int32>( tileVisual._tintB ) );
                    if ( pTextFlag != nullptr )
                        tileNode.setValue( pTextFlag );
                }
            }

            /** @brief `<warps>` 를 씁니다. */
            static void appendWarps( const XMLNode& root, const TileMapXMLData& data )
            {
                const XMLNode warps = root.appendChild( "warps" );
                for ( const TileMapXMLData::Warp& warp : data._listWarp )
                {
                    const XMLNode warpNode = warps.appendChild( "warp" );
                    warpNode.appendAttribute( "x", warp._tileX );
                    warpNode.appendAttribute( "y", warp._tileY );
                    warpNode.appendAttribute( "map", warp._targetMap );
                    warpNode.appendAttribute( "tx", warp._targetTileX );
                    warpNode.appendAttribute( "ty", warp._targetTileY );
                    if ( warp._pairID.empty() == false )
                        warpNode.appendAttribute( "pair", warp._pairID );
                }
            }

            /** @brief 타일 레이어를 씁니다. 타일셋이 있을 때만 쓴다(없는 맵은 이전과 바이트까지 같다). 셀은 한 행씩 줄을 바꿔 적는다(사람이 읽고 비교할 수 있게 하려는 것이다). */
            static void appendTileLayer( const XMLNode& root, const TileMapXMLData& data )
            {
                if ( data._tileSetPath.empty() )
                    return;
                const XMLNode tileLayer = root.appendChild( "tileLayer" );
                tileLayer.appendAttribute( "tileSet", data._tileSetPath );
                const XMLNode palette = tileLayer.appendChild( "palette" );
                for ( const string& name : data._listPaletteName )
                {
                    palette.appendChild( "b" ).appendAttribute( "name", name );
                }
                string cells = "\n";
                for ( int32 y = 0; y < data._height; ++y )
                {
                    for ( int32 x = 0; x < data._width; ++x )
                    {
                        const size_t cellIndex     = static_cast<size_t>( y ) * static_cast<size_t>( data._width ) + static_cast<size_t>( x );
                        const uint16 paletteNumber = ( cellIndex < data._listTileCell.size() ) ? data._listTileCell[cellIndex] : 0;
                        cells += to_string( static_cast<uint32>( paletteNumber ) );
                        cells += ( x + 1 < data._width ) ? " " : "\n";
                    }
                }
                tileLayer.appendChild( "cells", string_view{ cells } );
            }

            /** @brief 데이터 레이어를 씁니다. 있을 때만 쓴다(없는 맵은 이전과 바이트까지 같다). 값은 한 행씩 줄을 바꿔 적고, 배열이 짧으면 모자란 칸은 그 레이어의 기본값이다(`appendTiles` 와 같은 규칙). */
            static void appendDataLayers( const XMLNode& root, const TileMapXMLData& data )
            {
                if ( data._listDataLayer.empty() )
                    return;
                const XMLNode dataLayers = root.appendChild( "dataLayers" );
                for ( const TileMapXMLData::DataLayer& layer : data._listDataLayer )
                {
                    string values = "\n";
                    for ( int32 y = 0; y < data._height; ++y )
                    {
                        for ( int32 x = 0; x < data._width; ++x )
                        {
                            const size_t cellIndex = static_cast<size_t>( y ) * static_cast<size_t>( data._width ) + static_cast<size_t>( x );
                            values += to_string( cellIndex < layer._listValue.size() ? layer._listValue[cellIndex] : layer._defaultValue );
                            values += ( x + 1 < data._width ) ? " " : "\n";
                        }
                    }
                    const XMLNode layerNode = dataLayers.appendChild( "layer", string_view{ values } );
                    layerNode.appendAttribute( "name", layer._name );
                    layerNode.appendAttribute( "default", layer._defaultValue );
                }
            }
        };

        static_assert( TileMapXMLInternal::isFlagLayerTableValid(), "kArrTileFlagLayerInfo must be ordered by TileFlagLayer and have at most one text layer" );
    } // namespace

    bool TileMapXMLData::load( const AssetID& assetID )
    {
        const string_view path = assetID.view();
        if ( path.empty() )
            return false;

        string text;
        string absPath;
        if ( AssetIO::readTextAsset( assetID, text, &absPath ) == false ) // 낱개 파일 허용 정책은 AssetIO 한 곳이 본다
        {
            SW_LOG_ERROR( "TileMap file not found: %#", path );
            return false;
        }

        if ( loadFromXML( text ) == false )
            return false;
        _sourcePath = path;
        return true;
    }

    bool TileMapXMLData::loadFromDiskFile( string_view diskPath )
    {
        if ( diskPath.empty() )
            return false;

        string text;
        if ( AssetIO::readTextFile( diskPath, text ) == false )
        {
            SW_LOG_ERROR( "TileMap file not found: %#", diskPath );
            return false;
        }

        if ( loadFromXML( text ) == false )
            return false;
        _sourcePath = diskPath;
        return true;
    }

    bool TileMapXMLData::loadFromXML( string_view xml )
    {
        *this = {};

        XMLDocument doc;
        if ( doc.parse( xml ) == false )
            return false;

        const XMLNode root = doc.getRoot( "TileMap" );
        if ( root.isValid() == false )
        {
            SW_LOG_ERROR( "Missing <TileMap>" );
            return false;
        }

        bool bValid = true;
        if ( TileMapXMLInternal::readHeader( root, *this, bValid ) == false )
        {
            *this = {};
            return false;
        }

        (void)resetTiles( _width, _height ); // 크기는 위에서 확인했다
        const size_t count = static_cast<size_t>( _width ) * static_cast<size_t>( _height );

        const XMLNode tiles = root.findChild( "tiles" );
        if ( tiles.isValid() )
            TileMapXMLInternal::readTiles( tiles, *this, count, bValid );

        const XMLNode warps = root.findChild( "warps" );
        if ( warps.isValid() )
            TileMapXMLInternal::readWarps( warps, *this, bValid );

        const XMLNode tileLayer = root.findChild( "tileLayer" );
        if ( tileLayer.isValid() && TileMapXMLInternal::readTileLayer( tileLayer, *this, count ) == false )
        {
            *this = {};
            return false;
        }

        if ( TileMapXMLInternal::readDataLayers( root.findChild( "dataLayers" ), *this, count ) == false )
        {
            *this = {};
            return false;
        }

        if ( bValid == false )
        {
            SW_LOG_ERROR( "TileMap '%#' has invalid attribute values (listed above)", _name );
            *this = {};
            return false;
        }
        SW_LOG_INFO( "Loaded '%#' (%#×%#) scene=%# role=%#", _name, _width, _height, _scenePath, _role );
        return true;
    }

    bool TileMapXMLData::resetTiles( int32 width, int32 height )
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
        for ( DataLayer& layer : _listDataLayer )
        {
            layer._listValue.assign( count, layer._defaultValue );
        }
        _listWarp.clear();
        if ( _tileSetPath.empty() == false || _listTileCell.empty() == false )
            _listTileCell.assign( count, 0 );
        return true;
    }

    int32 TileMapXMLData::findDataLayer( string_view name ) const
    {
        for ( size_t layerIndex = 0; layerIndex < _listDataLayer.size(); ++layerIndex )
        {
            if ( _listDataLayer[layerIndex]._name == name )
                return static_cast<int32>( layerIndex );
        }
        return -1;
    }

    bool TileMapXMLData::addDataLayer( string_view name, int32 defaultValue )
    {
        if ( name.empty() || findDataLayer( name ) >= 0 )
            return false;
        DataLayer layer;
        layer._name         = string( name );
        layer._defaultValue = defaultValue;
        layer._listValue.assign( static_cast<size_t>( MathUtil::max( _width, 0 ) ) * static_cast<size_t>( MathUtil::max( _height, 0 ) ), defaultValue );
        _listDataLayer.push_back( std::move( layer ) );
        return true;
    }

    bool TileMapXMLData::getCellData( int32 layerIndex, int32 x, int32 y, int32& outValue ) const
    {
        const bool bOutside = layerIndex < 0 || static_cast<size_t>( layerIndex ) >= _listDataLayer.size() || x < 0 || y < 0 || x >= _width || y >= _height;
        if ( bOutside )
            return false;
        const vector<int32>& listValue = _listDataLayer[static_cast<size_t>( layerIndex )]._listValue;
        const size_t         cellIndex = static_cast<size_t>( y ) * static_cast<size_t>( _width ) + static_cast<size_t>( x );
        if ( cellIndex >= listValue.size() )
            return false;
        outValue = listValue[cellIndex];
        return true;
    }

    bool TileMapXMLData::setCellData( int32 layerIndex, int32 x, int32 y, int32 value )
    {
        const bool bOutside = layerIndex < 0 || static_cast<size_t>( layerIndex ) >= _listDataLayer.size() || x < 0 || y < 0 || x >= _width || y >= _height;
        if ( bOutside )
            return false;
        vector<int32>& listValue = _listDataLayer[static_cast<size_t>( layerIndex )]._listValue;
        const size_t   cellIndex = static_cast<size_t>( y ) * static_cast<size_t>( _width ) + static_cast<size_t>( x );
        if ( cellIndex >= listValue.size() )
            return false;
        listValue[cellIndex] = value;
        return true;
    }

    string_view TileMapXMLData::getTileBrushName( int32 x, int32 y ) const
    {
        const bool bOutsideMap = x < 0 || y < 0 || x >= _width || y >= _height;
        if ( bOutsideMap )
            return {};
        const size_t cellIndex = static_cast<size_t>( y ) * static_cast<size_t>( _width ) + static_cast<size_t>( x );
        if ( cellIndex >= _listTileCell.size() )
            return {};
        const uint16 paletteNumber = _listTileCell[cellIndex];
        if ( paletteNumber == 0 || paletteNumber > _listPaletteName.size() )
            return {};
        return _listPaletteName[paletteNumber - 1u];
    }

    bool TileMapXMLData::mapTileCells( const TileSetAsset& tileSet, vector<uint16>& outListBrushIndex ) const
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

    bool TileMapXMLData::setTileBrush( int32 x, int32 y, string_view brushName )
    {
        const bool bOutsideTileMap = x < 0 || y < 0 || x >= _width || y >= _height;
        if ( bOutsideTileMap )
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

    bool TileMapXMLData::save( string_view path ) const
    {
        const string absPath = AssetPaths::getWritePath( path );

        const string xml = toXML();
        if ( xml.empty() )
            return false;
        const bool bOk = FileUtil::writeTextFile( absPath, xml );
        if ( bOk )
            SW_LOG_INFO( "Saved '%#' -> %#", _name, absPath );
        return bOk;
    }

    string TileMapXMLData::toXML() const
    {
        XMLDocument   doc;
        const XMLNode root = doc.appendRoot( "TileMap" );
        root.appendChild( "name", _name.empty() ? string_view{ "Untitled" } : string_view{ _name } );
        root.appendChild( "width", _width );
        root.appendChild( "height", _height );
        if ( _scenePath.empty() == false )
            root.appendChild( "scene", _scenePath );
        if ( _role.empty() == false )
            root.appendChild( "role", _role );

        const XMLNode spawn = root.appendChild( "spawn" );
        spawn.appendAttribute( "x", _spawnX );
        spawn.appendAttribute( "y", _spawnY );

        TileMapXMLInternal::appendTiles( root.appendChild( "tiles" ), *this );
        TileMapXMLInternal::appendWarps( root, *this );
        TileMapXMLInternal::appendTileLayer( root, *this );
        TileMapXMLInternal::appendDataLayers( root, *this );

        return doc.saveToString();
    }
} // namespace sw

namespace sw
{
    SW_ASSET_TYPE( TileMapXMLData, TileMapXMLData::kAssetType );
} // namespace sw
