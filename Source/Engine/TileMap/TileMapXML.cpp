#include "pch.h"

#include "Engine/TileMap/TileMapXML.h"

#include "Core/File/FileUtil.h"
#include "Core/Foundation/Constants.h"
#include "Core/String/StringUtil.h"

#include "Engine/Content/AssetIO.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Serialization/Inherit/ResourceComposer.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLFields.h"
#include "Engine/Serialization/XML/XMLNameCheck.h"
#include "Engine/TileMap/TileSetAsset.h"

namespace sw
{
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

            // 읽기가 보는 이름 전부다. 철자가 틀린 이름(`atlass` · `tx` 대신 `targetX`)이 조용히 기본값이 되지 않게 모르는 이름은 읽기 오류다(MaterialXML 과 같은 규칙).
            static constexpr const utf8* kArrKnownRootChild[]      = { "spawn", "tiles", "warps", "tileLayer", "dataLayers" };
            static constexpr const utf8* kArrKnownTileAttribute[]  = { "h", "atlas", "tr", "tg", "tb" }; ///< 레이어 속성(`enc` · `pt`)은 `isFlagLayerAttribute` 가 받는다
            static constexpr const utf8* kArrKnownTileLayerChild[] = { "palette", "cells" };

            /** @brief 레이어 표가 `<t>` 속성으로 쓰는 이름인지입니다(표 하나가 정본이라 이름을 따로 적지 않는다). */
            static bool isFlagLayerAttribute( const utf8* pName )
            {
                for ( const TileFlagLayerInfo& info : kArrTileFlagLayerInfo )
                {
                    if ( info._pXMLAttribute != nullptr && StringUtil::equals( info._pXMLAttribute, pName ) )
                        return true;
                }
                return false;
            }

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
                optional<int32> value;
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
             * @brief 이름 · 크기 · 장면 · 역할(루트 속성 — `TileMapHeaderParams`) · 시작 위치를 읽습니다.
             * @details 크기는 필수이고 1 이상이다 — 빠지거나 깨진 크기를 다른 크기(옛 8x8)로 바꿔 읽지 않는다. 크기는 지원 범위(`isSizeSupported`) 안이어야 한다:
             *          파일이 말한 크기를 그대로 잡지 않는다 — 바로 뒤에서 `_width x _height` 셀짜리 배열 넷을 잡으므로, 손상되거나 손으로 잘못 적은 맵 하나가 수십억 셀 요청이 된다.
             */
            [[nodiscard]] static bool readHeader( const XMLNode& root, TileMapXMLData& data, bool& inoutValid )
            {
                TileMapHeaderParams header;
                const bool          bHeaderRead = XMLFields::readFields( root, header, "TileMap" );
                if ( header._width <= 0 || header._height <= 0 )
                {
                    SW_LOG_ERROR_ID( "TileMap.InvalidSize", "<TileMap> needs width and height of at least 1" );
                    return false;
                }
                if ( bHeaderRead == false )
                    return false;
                data._name          = std::move( header._name );
                data._width         = header._width;
                data._height        = header._height;
                data._scenePath     = std::move( header._scene );
                data._role          = std::move( header._role );
                const XMLNode spawn = root.findChild( "spawn" );
                if ( spawn.isValid() )
                {
                    TileSpawnParams params{ data._spawnX, data._spawnY };
                    inoutValid = XMLFields::readFields( spawn, params, data._name ) && inoutValid;
                    // 맵 안의 자리인지는 크기를 아는 여기서 본다(스키마 범위는 상수다).
                    const bool bInside = params._x < data._width && params._y < data._height;
                    if ( bInside == false )
                        SW_LOG_ERROR_ID( "TileMap.SpawnOutside", "TileMap '%#': <spawn x=\"%#\" y=\"%#\"> is outside the %#x%# map", data._name, params._x, params._y, data._width, data._height );
                    inoutValid   = bInside && inoutValid;
                    data._spawnX = bInside ? params._x : data._spawnX;
                    data._spawnY = bInside ? params._y : data._spawnY;
                }
                if ( TileMapXMLData::isSizeSupported( data._width, data._height ) == false )
                {
                    SW_LOG_ERROR_ID( "TileMap.InvalidSize", "TileMap size %#x%# is beyond the supported tile count (%#)", data._width, data._height, TileMapXMLData::kMaxTileCount );
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
                        SW_LOG_ERROR_ID( "TileMap.InvalidAttributes", "TileMap tile %# needs all of tr, tg and tb", index );
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
                    inoutValid                = XMLNameCheck::reportUnknownAttributes( tileNode, kArrKnownTileAttribute, data._name, LogLevel::Error, &isFlagLayerAttribute ) && inoutValid;
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
                    TileWarpParams params;
                    if ( XMLFields::readFields( warpNode, params, data._name ) == false )
                    {
                        inoutValid = false;
                        continue;
                    }
                    TileMapXMLData::Warp warp{};
                    warp._tileX       = params._x;
                    warp._tileY       = params._y;
                    warp._targetMap   = std::move( params._map );
                    warp._targetTileX = params._tx;
                    warp._targetTileY = params._ty;
                    warp._pairID      = std::move( params._pair );
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
                            SW_LOG_ERROR_ID( "TileMap.InvalidTileLayer", "TileMap '%#': <cells> holds something that is not a number", data._name );
                            return false;
                        }
                        break;
                    }
                    int32      value                = 0;
                    const bool bInvalidPaletteIndex = StringUtil::parseInt( cells.substr( start, cursor - start ), value ) == false || value < 0 ||
                                                      static_cast<size_t>( value ) > data._listPaletteName.size();
                    if ( bInvalidPaletteIndex )
                    {
                        SW_LOG_ERROR_ID( "TileMap.InvalidTileLayer", "TileMap '%#': tile cell %# is outside the palette (%# names)", data._name, value, static_cast<uint32>( data._listPaletteName.size() ) );
                        return false;
                    }
                    data._listTileCell.push_back( static_cast<uint16>( value ) );
                }
                if ( data._listTileCell.size() != count )
                {
                    SW_LOG_ERROR_ID( "TileMap.InvalidTileLayer", "TileMap '%#': <cells> has %# values, the map has %# cells", data._name, static_cast<uint32>( data._listTileCell.size() ), static_cast<uint32>( count ) );
                    return false;
                }
                return true;
            }

            /** @brief `<tileLayer>` 의 타일셋 경로 · 팔레트(브러시 이름) · 셀마다 팔레트 번호를 읽습니다. 셀 수가 맞지 않거나 팔레트 밖 번호는 읽기 오류(false)입니다. */
            [[nodiscard]] static bool readTileLayer( const XMLNode& tileLayer, TileMapXMLData& data, size_t count )
            {
                TileLayerParams layerParams;
                const bool      bKnownAttributes = XMLFields::readFields( tileLayer, layerParams, data._name );
                if ( ( XMLNameCheck::reportUnknownChildren( tileLayer, kArrKnownTileLayerChild, data._name ) && bKnownAttributes ) == false )
                    return false;
                data._tileSetPath     = std::move( layerParams._tileSet );
                const XMLNode palette = tileLayer.findChild( "palette" );
                for ( XMLNode entry = palette.isValid() ? palette.findChild( "b" ) : XMLNode{}; entry; entry = entry.findNextSibling( "b" ) )
                {
                    TilePaletteEntryParams entryParams;
                    if ( XMLFields::readFields( entry, entryParams, data._name ) == false )
                        return false;
                    data._listPaletteName.push_back( std::move( entryParams._name ) );
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
                    TileDataLayerParams params;
                    const bool          bFieldsRead  = XMLFields::readFields( layerNode, params, data._name ); // 이름 · 기본값 필수는 PROPERTY
                    const bool          bHeaderValid = bFieldsRead && params._name.empty() == false && data.findDataLayer( params._name ) < 0;
                    if ( bHeaderValid == false )
                    {
                        SW_LOG_ERROR_ID( "TileMap.InvalidDataLayer", "TileMap '%#': <dataLayers><layer> needs a unique non-empty name and a default (name '%#')", data._name, params._name );
                        return false;
                    }
                    TileMapXMLData::DataLayer layer;
                    layer._name         = std::move( params._name );
                    layer._defaultValue = params._default;
                    const utf8* pValues = layerNode.getText();
                    const bool  bValuesValid =
                        StringUtil::parseIntList( pValues != nullptr ? string_view( pValues ) : string_view{}, kValueListSeparators, layer._listValue ) && layer._listValue.size() == count;
                    if ( bValuesValid == false )
                    {
                        SW_LOG_ERROR_ID( "TileMap.InvalidDataLayer", "TileMap '%#': data layer '%#' needs %# integer values (one per cell)", data._name, layer._name, static_cast<uint32>( count ) );
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
                    SW_LOG_WARNING_ID( "TileMap.ShortTileArray", "The tile array is shorter than %#x%# (%# cells) - missing cells are written as defaults.", data._width, data._height, static_cast<uint32>( tileCount ) );

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
                    XMLFields::writeFields( warps.appendChild( "warp" ), TileWarpParams{ warp._tileX, warp._tileY, warp._targetMap, warp._targetTileX, warp._targetTileY, warp._pairID } );
                }
            }

            /** @brief 타일 레이어를 씁니다. 타일셋이 있을 때만 쓴다(없는 맵은 이전과 바이트까지 같다). 셀은 한 행씩 줄을 바꿔 적는다(사람이 읽고 비교할 수 있게 하려는 것이다). */
            static void appendTileLayer( const XMLNode& root, const TileMapXMLData& data )
            {
                if ( data._tileSetPath.empty() )
                    return;
                const XMLNode tileLayer = root.appendChild( "tileLayer" );
                XMLFields::writeFields( tileLayer, TileLayerParams{ data._tileSetPath } );
                const XMLNode palette = tileLayer.appendChild( "palette" );
                for ( const string& name : data._listPaletteName )
                {
                    XMLFields::writeFields( palette.appendChild( "b" ), TilePaletteEntryParams{ name } );
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
                    XMLFields::writeFields( layerNode, TileDataLayerParams{ layer._name, layer._defaultValue } );
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
            SW_LOG_ERROR_ID( "TileMap.FileNotFound", "TileMap file not found: %#", path );
            return false;
        }
        // 리소스 id 를 실어 읽는다 — 덮어쓰기 층 · 다시 읽기 기록 · 접속 대조가 타일맵도 본다(글 입구로 넘기면 조용히 빠졌다).
        XMLDocument doc;
        if ( doc.parseResourceText( text, assetID ) == false || loadFromDocument( doc ) == false )
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
            SW_LOG_ERROR_ID( "TileMap.FileNotFound", "TileMap file not found: %#", diskPath );
            return false;
        }

        if ( loadFromXML( text ) == false )
            return false;
        _sourcePath = diskPath;
        return true;
    }

    bool TileMapXMLData::loadFromXML( string_view xml )
    {
        XMLDocument doc;
        if ( doc.parse( xml ) == false )
        {
            *this = {};
            return false;
        }
        return loadFromDocument( doc );
    }

    bool TileMapXMLData::loadFromDocument( const XMLDocument& doc )
    {
        *this = {};

        const XMLNode root = doc.getRoot( "TileMap" );
        if ( root.isValid() == false )
        {
            SW_LOG_ERROR_ID( "TileMap.MissingRoot", "Missing <TileMap>" );
            return false;
        }

        bool bValid = true;
        if ( TileMapXMLInternal::readHeader( root, *this, bValid ) == false )
        {
            *this = {};
            return false;
        }
        if ( XMLNameCheck::reportUnknownChildren( root, TileMapXMLInternal::kArrKnownRootChild, _name ) == false )
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
            SW_LOG_ERROR_ID( "TileMap.InvalidAttributes", "TileMap '%#' has invalid attribute values (listed above)", _name );
            *this = {};
            return false;
        }
        SW_LOG_INFO_ID( "TileMapXML.LoadedSceneRole", "Loaded '%#' (%#×%#) scene=%# role=%#", _name, _width, _height, _scenePath, _role );
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
        return constant::invalid_index::kInt32;
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
        vector<int32> listPaletteToBrush( _listPaletteName.size(), constant::invalid_index::kInt32 );
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

    bool TileMapXMLData::save( const AbsolutePath& absPath ) const
    {

        const string xml = toXML();
        if ( xml.empty() )
            return false;
        const bool bOk = ResourceComposer::saveText( absPath.str(), xml );
        if ( bOk )
            SW_LOG_INFO_ID( "TileMapXML.Saved", "Saved '%#' -> %#", _name, absPath.str() );
        return bOk;
    }

    string TileMapXMLData::toXML() const
    {
        XMLDocument         doc;
        const XMLNode       root = doc.appendRoot( "TileMap" );
        TileMapHeaderParams header;
        header._name   = _name.empty() ? string( "Untitled" ) : _name;
        header._width  = _width;
        header._height = _height;
        header._scene  = _scenePath;
        header._role   = _role;
        XMLFields::writeFields( root, header );

        XMLFields::writeEveryField( root.appendChild( "spawn" ), TileSpawnParams{ _spawnX, _spawnY } );

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
