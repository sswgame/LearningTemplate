#include "pch.h"

#include "Editor/Panels/TileMap/Model/TileMapDocument.h"

#include "Core/Container/VectorUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Editor/Common/Commands/Asset/EditorToolAssetCommands.h"
#include "Editor/Common/Config/EditorToolDefaults.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Panels/TileMap/Model/TileMapPaintUtil.h"

#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"

namespace sw::editor
{
    SW_LOG_CALLER( "TileMapDocument" );

    TileMapDocument::TileMapDocument()
        : _pathBuffer{}
        , _nameBuffer{ "Untitled" }
        , _warpTarget{}
        , _arrEdgePreset{}
        , _status{}
        , _map{}
        , _tileSetBuffer{}
        , _tileSet{}
        , _loadedTileSet{}
        , _listBrushIndex{}
        , _listStrokeCell{}
        , _listCellValue{}
        , _brushIndex{ 0 }
        , _arrTint{ 180.0f / 255.0f, 200.0f / 255.0f, 160.0f / 255.0f }
        , _inputWidth{ 8 }
        , _inputHeight{ 8 }
        , _paintHeight{ 1 }
        , _atlasID{ 0 }
        , _warpTx{ 1 }
        , _warpTy{ 1 }
        , _lastPaintCell{}
        , _rectStartCell{}
        , _dataLayerIndex{ 0 }
        , _paintDataValue{ 1 }
        , _layer{ PaintLayer::Flag }
        , _flagLayer{ TileFlagLayer::Walkable }
        , _tool{ PaintTool::Brush }
        , _bErase{ false }
        , _bStrokeActive{ false }
        , _bRectActive{ false }
    {
        const EditorToolDefaults& editorToolDefaults = editor::getEditorToolDefaults();
        if ( editorToolDefaults._defaultMap.isSet() )
            _pathBuffer = editorToolDefaults._defaultMap.c_str();
        if ( editorToolDefaults._warpMap.isSet() )
            _warpTarget = editorToolDefaults._warpMap.c_str();
        (void)resize( 8, 8 ); // 8x8 은 상한 안이다
    }

    ToolAssetLoadResult TileMapDocument::loadDocument()
    {
        // 경로가 없으면 새 맵(기본 8x8)이다. 경로 필드는 첫 로드 때 열린 문서로, Load 단추 때는 적어 넣은 경로로 채워져 있다.
        if ( _pathBuffer.empty() )
            return ToolAssetLoadResult::Missing;
        return loadXML( _pathBuffer.c_str() );
    }

    ToolAssetLoadResult TileMapDocument::loadXML( string_view assetRelativePath )
    {
        TileMapXMLData            data;
        const ToolAssetLoadResult result = EditorToolAssetCommands::loadTileMap( assetRelativePath, data, _status );
        if ( result != ToolAssetLoadResult::Loaded )
            return result;

        applyMapData( data );
        _pathBuffer = string( assetRelativePath ).c_str();
        return ToolAssetLoadResult::Loaded;
    }

    bool TileMapDocument::saveXML() const
    {
        return EditorToolAssetCommands::saveTileMap( _pathBuffer.c_str(), captureMapData() );
    }

    string TileMapDocument::captureText() const
    {
        return captureMapData().toXML();
    }

    void TileMapDocument::applyText( string_view text )
    {
        TileMapXMLData restored;
        if ( text.empty() == false && restored.loadFromXML( text ) == false )
            SW_LOG_WARNING( "Tile map undo snapshot could not be read - showing an empty map" );
        applyMapData( restored );
    }

    bool TileMapDocument::applyInputSize()
    {
        return resize( MathUtil::max( 1, _inputWidth ), MathUtil::max( 1, _inputHeight ) );
    }

    bool TileMapDocument::applyToolInput( const TileMapToolInput& input )
    {
        const int2& hoverCell = input._hoverCell;
        bool        bEdited   = false;
        switch ( _tool )
        {
            case PaintTool::Brush:
            {
                if ( input._bPainting )
                {
                    // 한 프레임에 마우스가 여러 셀을 지나가도 끊기지 않게 지난 프레임에 칠한 셀에서 이번 셀까지 선분으로 칠한다.
                    TileMapPaintUtil::collectLineCells( _bStrokeActive ? _lastPaintCell : hoverCell, hoverCell, _listStrokeCell );
                    bEdited        = paintStrokeCells();
                    _lastPaintCell = hoverCell;
                }
                _bStrokeActive = input._bPainting;
                break;
            }
            case PaintTool::Rect:
            {
                if ( input._bClicked )
                {
                    _rectStartCell = hoverCell;
                    _bRectActive   = true;
                }
                if ( _bRectActive && input._bButtonDown == false )
                {
                    _bRectActive = false;
                    TileMapPaintUtil::collectRectCells( _rectStartCell, hoverCell, _map._width, _map._height, _listStrokeCell );
                    bEdited = paintStrokeCells();
                }
                break;
            }
            case PaintTool::Fill:
            {
                if ( input._bClicked && isInBounds( hoverCell._x, hoverCell._y ) )
                {
                    collectCellValues( _listCellValue );
                    TileMapPaintUtil::collectFloodFillCells( _listCellValue, _map._width, _map._height, hoverCell, _listStrokeCell );
                    bEdited = paintStrokeCells();
                }
                break;
            }
            case PaintTool::Picker:
            {
                if ( input._bClicked && isInBounds( hoverCell._x, hoverCell._y ) )
                {
                    pickCell( hoverCell._x, hoverCell._y );
                    _tool = PaintTool::Brush; // 유니티 Tile Palette 처럼 집은 뒤에는 칠한다
                }
                break;
            }
        }
        return bEdited;
    }

    bool TileMapDocument::paintStrokeCells()
    {
        bool bEdited = false;
        for ( const int2& strokeCell : _listStrokeCell )
        {
            if ( paintCell( strokeCell._x, strokeCell._y ) )
                bEdited = true;
        }
        return bEdited;
    }

    void TileMapDocument::collectCellValues( vector<uint64>& outListValue )
    {
        const size_t cellCount = static_cast<size_t>( _map._width ) * static_cast<size_t>( _map._height );
        outListValue.assign( cellCount, 0 );
        switch ( _layer )
        {
            case PaintLayer::Visual:
            {
                for ( size_t cellIndex = 0; cellIndex < cellCount; ++cellIndex )
                {
                    const TileMapXMLData::Visual& visual = _map._listVisual[cellIndex];
                    outListValue[cellIndex]              = ( static_cast<uint64>( visual._height ) << 32 ) | ( static_cast<uint64>( visual._atlasID ) << 24 ) |
                                              ( static_cast<uint64>( visual._tintR ) << 16 ) | ( static_cast<uint64>( visual._tintG ) << 8 ) | visual._tintB;
                }
                break;
            }
            case PaintLayer::Warp:
            {
                for ( const TileMapXMLData::Warp& warp : _map._listWarp )
                {
                    if ( isInBounds( warp._tileX, warp._tileY ) )
                        outListValue[indexOf( warp._tileX, warp._tileY )] = 1;
                }
                break;
            }
            case PaintLayer::Tile:
            {
                (void)_map.mapTileCells( _tileSet, _listBrushIndex ); // 모르는 브러시는 빈 셀(0)이다
                for ( size_t cellIndex = 0; cellIndex < cellCount && cellIndex < _listBrushIndex.size(); ++cellIndex )
                {
                    outListValue[cellIndex] = _listBrushIndex[cellIndex];
                }
                break;
            }
            case PaintLayer::Flag:
            {
                const auto& listFlag = _map.getFlagLayer( _flagLayer );
                for ( size_t cellIndex = 0; cellIndex < cellCount; ++cellIndex )
                {
                    outListValue[cellIndex] = listFlag[cellIndex];
                }
                break;
            }
            case PaintLayer::Data:
            {
                if ( _dataLayerIndex < 0 || static_cast<size_t>( _dataLayerIndex ) >= _map._listDataLayer.size() )
                    break;
                const vector<int32>& listValue = _map._listDataLayer[static_cast<size_t>( _dataLayerIndex )]._listValue;
                for ( size_t cellIndex = 0; cellIndex < cellCount && cellIndex < listValue.size(); ++cellIndex )
                {
                    outListValue[cellIndex] = static_cast<uint64>( static_cast<uint32>( listValue[cellIndex] ) );
                }
                break;
            }
        }
    }

    void TileMapDocument::pickCell( int32 x, int32 y )
    {
        const size_t tileIndex = indexOf( x, y );
        switch ( _layer )
        {
            case PaintLayer::Visual:
            {
                const TileMapXMLData::Visual& visual = _map._listVisual[tileIndex];
                _paintHeight                         = visual._height;
                _atlasID                             = visual._atlasID;
                _arrTint[0]                          = static_cast<float32>( visual._tintR ) / 255.0f;
                _arrTint[1]                          = static_cast<float32>( visual._tintG ) / 255.0f;
                _arrTint[2]                          = static_cast<float32>( visual._tintB ) / 255.0f;
                _bErase                              = false;
                break;
            }
            case PaintLayer::Warp:
            {
                _bErase = true;
                for ( const TileMapXMLData::Warp& warp : _map._listWarp )
                {
                    if ( warp._tileX != x || warp._tileY != y )
                        continue;
                    _warpTarget = warp._targetMap.c_str();
                    _warpTx     = warp._targetTileX;
                    _warpTy     = warp._targetTileY;
                    _bErase     = false;
                    break;
                }
                break;
            }
            case PaintLayer::Tile:
            {
                const string_view brushName = _map.getTileBrushName( x, y );
                const int32       found     = brushName.empty() ? -1 : _tileSet.findBrush( hashed_string( string( brushName ).c_str() ) );
                _bErase                     = found < 0;
                if ( found >= 0 )
                    _brushIndex = found;
                break;
            }
            case PaintLayer::Flag:
            {
                _bErase = _map.getFlagLayer( _flagLayer )[tileIndex] == 0;
                break;
            }
            case PaintLayer::Data:
            {
                int32 value = 0;
                if ( _map.getCellData( _dataLayerIndex, x, y, value ) )
                {
                    _paintDataValue = value;
                    _bErase         = false;
                }
                break;
            }
        }
    }

    bool TileMapDocument::addDataLayer( string_view name, int32 defaultValue )
    {
        // 빈 이름 · 겹치는 이름은 맵이 거절한다 — 읽기도 같은 규칙으로 거절하므로 저장할 수 없는 맵을 만들지 않는다.
        if ( _map.addDataLayer( name, defaultValue ) == false )
        {
            _status = SW_LOCKEY( "EditorTileMap", "DataLayerRejectedStatus" );
            return false;
        }
        _layer          = PaintLayer::Data;
        _dataLayerIndex = static_cast<int32>( _map._listDataLayer.size() ) - 1;
        return true;
    }

    bool TileMapDocument::useTileSetInput()
    {
        _map._tileSetPath = _tileSetBuffer.c_str();
        refreshTileSet();
        return true;
    }

    uint32 TileMapDocument::countSelectedFlagCells() const
    {
        if ( _layer != PaintLayer::Flag )
            return 0;
        uint32 count{ 0 };
        for ( const auto value : _map.getFlagLayer( _flagLayer ) )
        {
            if ( value != 0 )
                ++count;
        }
        return count;
    }

    uint32 TileMapDocument::countDataCells() const
    {
        uint32 count{ 0 };
        for ( const TileMapXMLData::DataLayer& dataLayer : _map._listDataLayer )
        {
            for ( const int32 value : dataLayer._listValue )
            {
                if ( value != dataLayer._defaultValue )
                    ++count;
            }
        }
        return count;
    }

    bool TileMapDocument::resize( int32 width, int32 height )
    {
        // 크기는 Width/Height 필드에서 그대로 온다. 상한과 그 이유는 `TileMapXMLData::kMaxTileCount` 에 있다.
        if ( _map.resetTiles( width, height ) == false )
        {
            _status = SW_LOCKEY( "EditorTileMap", "SizeOutOfRange" );
            SW_LOG_WARNING( "TileMap resize %#x%# is beyond the supported tile count (%#)",
                            width, height, TileMapXMLData::kMaxTileCount );
            return false;
        }
        _inputWidth  = width;
        _inputHeight = height;
        return true;
    }

    TileMapXMLData TileMapDocument::captureMapData() const
    {
        TileMapXMLData data = _map;
        data._name          = _nameBuffer.c_str();
        return data;
    }

    void TileMapDocument::applyMapData( const TileMapXMLData& data )
    {
        _map         = data;
        _nameBuffer  = data._name.c_str();
        _inputWidth  = data._width;
        _inputHeight = data._height;
        // 새 맵에 고른 데이터 레이어가 없으면 플래그 레이어로 돌아간다(옛 번호로 다른 레이어를 칠하지 않는다).
        const bool bDataLayerGone = _layer == PaintLayer::Data && ( _dataLayerIndex < 0 || static_cast<size_t>( _dataLayerIndex ) >= _map._listDataLayer.size() );
        if ( bDataLayerGone )
        {
            _layer          = PaintLayer::Flag;
            _dataLayerIndex = 0;
        }
        refreshTileSet();
    }

    void TileMapDocument::refreshTileSet()
    {
        if ( _map._tileSetPath == _loadedTileSet )
            return;
        _loadedTileSet = _map._tileSetPath;
        _tileSetBuffer = _map._tileSetPath.c_str();
        _tileSet       = TileSetAsset{};
        _brushIndex    = 0;
        if ( _map._tileSetPath.empty() == false && _tileSet.loadFromAsset( AssetID{ _map._tileSetPath } ) == false )
            _status = SW_LOCKEYFORMAT( "EditorTileMap", "TileSetReadFailed", TextArgumentList().addText( "path", _map._tileSetPath ) );
    }

    const vector<uint16>& TileMapDocument::refreshBrushIndices()
    {
        (void)_map.mapTileCells( _tileSet, _listBrushIndex ); // 모르는 브러시는 빈 셀로 보인다 — 읽을 때 이미 알렸다
        return _listBrushIndex;
    }

    bool TileMapDocument::paintCell( int32 x, int32 y )
    {
        if ( isInBounds( x, y ) == false )
            return false;

        const size_t tileIndex = indexOf( x, y );
        switch ( _layer )
        {
            case PaintLayer::Visual:
            {
                if ( _bErase == false )
                {
                    TileMapXMLData::Visual& tileVisual = _map._listVisual[tileIndex];
                    tileVisual._height                 = static_cast<uint8>( _paintHeight );
                    tileVisual._atlasID                = static_cast<uint8>( _atlasID );
                    tileVisual._tintR                  = static_cast<uint8>( MathUtil::clamp( _arrTint[0] * 255.0f, 0.0f, 255.0f ) );
                    tileVisual._tintG                  = static_cast<uint8>( MathUtil::clamp( _arrTint[1] * 255.0f, 0.0f, 255.0f ) );
                    tileVisual._tintB                  = static_cast<uint8>( MathUtil::clamp( _arrTint[2] * 255.0f, 0.0f, 255.0f ) );
                }
                break;
            }
            case PaintLayer::Flag:
            {
                _map.getFlagLayer( _flagLayer )[tileIndex] = _bErase ? 0 : 1;
                break;
            }
            case PaintLayer::Data:
            {
                if ( _dataLayerIndex < 0 || static_cast<size_t>( _dataLayerIndex ) >= _map._listDataLayer.size() )
                    return false;
                // 지우기는 그 레이어의 기본값으로 되돌린다(0 이 아니다 — 기본값은 맵 데이터가 정한다).
                const int32 value = _bErase ? _map._listDataLayer[static_cast<size_t>( _dataLayerIndex )]._defaultValue : _paintDataValue;
                int32       current{ 0 };
                if ( _map.getCellData( _dataLayerIndex, x, y, current ) && current == value )
                    return false; // 끌어 칠할 때 같은 셀을 프레임마다 되돌리기 기록에 넣지 않는다
                (void)_map.setCellData( _dataLayerIndex, x, y, value );
                break;
            }
            case PaintLayer::Tile:
            {
                const vector<TileBrush>& listBrush = _tileSet.getBrushes();
                if ( _bErase == false && ( _brushIndex < 0 || _brushIndex >= static_cast<int32>( listBrush.size() ) ) )
                    return false;
                const string_view brushName = _bErase ? string_view{} : string_view( listBrush[static_cast<size_t>( _brushIndex )]._name.c_str() );
                if ( _map.getTileBrushName( x, y ) == brushName )
                    return false;                           // 끌어 칠할 때 같은 셀을 프레임마다 되돌리기 기록에 넣지 않는다
                (void)_map.setTileBrush( x, y, brushName ); // 범위는 위에서 봤다
                break;
            }
            case PaintLayer::Warp:
            {
                VectorUtil::removeIf( _map._listWarp, [x, y]( const TileMapXMLData::Warp& warp )
                { return warp._tileX == x && warp._tileY == y; } );
                if ( _bErase == false && _warpTarget.empty() == false )
                {
                    TileMapXMLData::Warp warpItem{};
                    warpItem._tileX       = x;
                    warpItem._tileY       = y;
                    warpItem._targetMap   = _warpTarget.c_str();
                    warpItem._targetTileX = _warpTx;
                    warpItem._targetTileY = _warpTy;
                    _map._listWarp.push_back( std::move( warpItem ) );
                    _map.getFlagLayer( TileFlagLayer::Walkable )[tileIndex] = 1;
                }
                break;
            }
        }
        return true;
    }

    bool TileMapDocument::paintEdgeWarp( int32 edge )
    {
        const bool bInvalidEdgeTarget = edge < 0 || edge >= kEdgeCount || _arrEdgePreset[edge]._target.empty();
        if ( bInvalidEdgeTarget )
            return false;

        const EdgeWarpPreset& preset = _arrEdgePreset[edge];
        auto                  stamp  = [&]( int32 tileX, int32 tileY )
        {
            _map.getFlagLayer( TileFlagLayer::Walkable )[indexOf( tileX, tileY )] = 1;
            VectorUtil::removeIf( _map._listWarp, [tileX, tileY]( const TileMapXMLData::Warp& warp )
            { return warp._tileX == tileX && warp._tileY == tileY; } );
            TileMapXMLData::Warp warpItem{};
            warpItem._tileX       = tileX;
            warpItem._tileY       = tileY;
            warpItem._targetMap   = preset._target.c_str();
            warpItem._targetTileX = preset._tileX;
            warpItem._targetTileY = preset._tileY;
            _map._listWarp.push_back( std::move( warpItem ) );
        };

        switch ( edge )
        {
            case 0:
            {
                for ( int32 tileX = 0; tileX < _map._width; ++tileX )
                {
                    stamp( tileX, 0 );
                }
                break;
            }
            case 1:
            {
                for ( int32 tileY = 0; tileY < _map._height; ++tileY )
                {
                    stamp( _map._width - 1, tileY );
                }
                break;
            }
            case 2:
            {
                for ( int32 tileX = 0; tileX < _map._width; ++tileX )
                {
                    stamp( tileX, _map._height - 1 );
                }
                break;
            }
            case 3:
            {
                for ( int32 tileY = 0; tileY < _map._height; ++tileY )
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
        return true;
    }

    int32 TileMapDocument::getPaintLayerIndex() const
    {
        if ( _layer == PaintLayer::Flag )
            return kFixedPaintLayerCount + static_cast<int32>( _flagLayer );
        if ( _layer == PaintLayer::Data )
            return kFixedPaintLayerCount + static_cast<int32>( kTileFlagLayerCount ) + _dataLayerIndex;
        return static_cast<int32>( _layer );
    }

    void TileMapDocument::selectPaintLayer( int32 layerIndex )
    {
        if ( layerIndex < kFixedPaintLayerCount )
        {
            _layer = static_cast<PaintLayer>( layerIndex );
            return;
        }
        const int32 flagIndex = layerIndex - kFixedPaintLayerCount;
        if ( flagIndex < static_cast<int32>( kTileFlagLayerCount ) )
        {
            _layer     = PaintLayer::Flag;
            _flagLayer = static_cast<TileFlagLayer>( flagIndex );
            return;
        }
        _layer          = PaintLayer::Data;
        _dataLayerIndex = flagIndex - static_cast<int32>( kTileFlagLayerCount );
    }

    void TileMapDocument::collectPaintLayerNames( vector<const utf8*>& outListName ) const
    {
        // 목록은 Visual · Warp · Tile 다음에 플래그 레이어 테이블 순서, 그 뒤에 맵의 데이터 레이어 순서다. 순번 ↔ 레이어는 getPaintLayerIndex / selectPaintLayer 가 정한다.
        outListName = { "Visual", "Warp", "Tile" };
        for ( const TileFlagLayerInfo& info : kArrTileFlagLayerInfo )
        {
            outListName.push_back( info._pName );
        }
        for ( const TileMapXMLData::DataLayer& dataLayer : _map._listDataLayer )
        {
            outListName.push_back( dataLayer._name.c_str() );
        }
    }

    bool TileMapDocument::readRectStart( int2& outCell ) const
    {
        if ( _bRectActive == false )
            return false;
        outCell = _rectStartCell;
        return true;
    }

    bool TileMapDocument::isInBounds( int32 x, int32 y ) const
    {
        return 0 <= x && x < _map._width && 0 <= y && y < _map._height;
    }

    size_t TileMapDocument::indexOf( int32 x, int32 y ) const
    {
        // 곱셈을 size_t 로 한다(Engine 의 TileMap::indexOf 와 같은 이유).
        return static_cast<size_t>( y ) * static_cast<size_t>( _map._width ) + static_cast<size_t>( x );
    }
} // namespace sw::editor
