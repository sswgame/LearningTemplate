#include "pch.h"

#include "Editor/Panels/TileMap/Model/TileMapModel.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringBuilder.h"

#include "Editor/Common/Commands/Asset/EditorToolAssetCommands.h"
#include "Editor/Common/Config/EditorToolDefaults.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Panels/TileMap/Model/TileMapPaintUtil.h"

#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"
#include "Engine/TileMap/TileSetAsset.h"

namespace sw::editor
{
    namespace
    {
        struct TileMapModelInternal
        {
            /** @brief 0..1 틴트를 바이트로 반올림합니다. 내리면 스포이트로 집어 칠할 때마다 값이 한 단계씩 줄어든다. */
            [[nodiscard]] static uint8 toTintByte( float32 value )
            {
                return static_cast<uint8>( MathUtil::round( MathUtil::saturate( value ) * 255.0f ) );
            }

            [[nodiscard]] static bool isSameVisual( const TileMapXMLData::Visual& first, const TileMapXMLData::Visual& second )
            {
                return first._height == second._height && first._atlasID == second._atlasID && first._tintR == second._tintR && first._tintG == second._tintG && first._tintB == second._tintB;
            }
        };
    } // namespace

    TileMapModel::TileMapModel()
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
        , _inputWidth{ kNewMapSide }
        , _inputHeight{ kNewMapSide }
        , _inputSpawnX{ 1 }
        , _inputSpawnY{ 1 }
        , _paintHeight{ 1 }
        , _atlasID{ 0 }
        , _warpTx{ 1 }
        , _warpTy{ 1 }
        , _lastPaintCell{}
        , _dragStartCell{}
        , _dataLayerIndex{ 0 }
        , _paintDataValue{ 1 }
        , _strokeSerial{ 0 }
        , _layer{ PaintLayer::Flag }
        , _flagLayer{ TileFlagLayer::Walkable }
        , _tool{ PaintTool::Brush }
        , _bErase{ false }
        , _bStrokeActive{ SW_FALSE }
        , _bDragActive{ SW_FALSE }
        , _bAwaitRelease{ SW_FALSE }
    {
        const EditorToolDefaults& editorToolDefaults = editor::getEditorToolDefaults();
        if ( editorToolDefaults._defaultMap.isSet() )
            _pathBuffer = editorToolDefaults._defaultMap.c_str();
        if ( editorToolDefaults._warpMap.isSet() )
            _warpTarget = editorToolDefaults._warpMap.c_str();
        (void)resize( kNewMapSide, kNewMapSide ); // 상한 안이다
    }

    ToolAssetLoadResult TileMapModel::loadDocument()
    {
        // 경로가 없으면 새 맵(기본 8x8)이다. 경로 필드는 첫 로드 때 열린 문서로, Load 단추 때는 적어 넣은 경로로 채워져 있다.
        if ( _pathBuffer.empty() )
        {
            startNewMap();
            return ToolAssetLoadResult::Missing;
        }
        return loadXML( _pathBuffer.c_str() );
    }

    ToolAssetLoadResult TileMapModel::loadXML( string_view assetRelativePath )
    {
        TileMapXMLData            data;
        const ToolAssetLoadResult result = EditorToolAssetCommands::loadTileMap( assetRelativePath, data, _status );
        if ( result == ToolAssetLoadResult::Missing )
            startNewMap(); // 새 문서다 - 앞 문서의 내용으로 시작하면 저장할 때 그 내용이 새 파일에 쓰인다
        if ( result != ToolAssetLoadResult::Loaded )
            return result;

        applyMapData( data );
        _pathBuffer = string( assetRelativePath ).c_str();
        return ToolAssetLoadResult::Loaded;
    }

    bool TileMapModel::saveDocument()
    {
        if ( EditorToolAssetCommands::saveTileMap( _pathBuffer.c_str(), captureMapData() ) == false )
        {
            _status = SW_LOCKEY( "EditorTileMap", "SaveFailedStatus" );
            return false;
        }
        _status = SW_LOCKEYFORMAT( "EditorTileMap", "SavedStatus", TextArgumentList().addText( "path", _pathBuffer.c_str() ) );
        return true;
    }

    string TileMapModel::captureText() const
    {
        return captureMapData().toXML();
    }

    void TileMapModel::applyText( string_view text )
    {
        TileMapXMLData restored;
        if ( text.empty() == false && restored.loadFromXML( text ) == false )
            SW_LOG_WARNING_ID( "TileMapModel.TileMapUndoSnapshot", "Tile map undo snapshot could not be read - showing an empty map" );
        applyMapData( restored );
    }

    bool TileMapModel::applyInputSize()
    {
        return resize( MathUtil::max( 1, _inputWidth ), MathUtil::max( 1, _inputHeight ) );
    }

    bool TileMapModel::applyInputSpawn()
    {
        if ( isInBounds( _inputSpawnX, _inputSpawnY ) == false )
        {
            _inputSpawnX = _map._spawnX;
            _inputSpawnY = _map._spawnY;
            _status      = SW_LOCKEY( "EditorTileMap", "SpawnOutsideStatus" );
            return false;
        }
        const bool bEdited = _inputSpawnX != _map._spawnX || _inputSpawnY != _map._spawnY;
        _map._spawnX       = _inputSpawnX;
        _map._spawnY       = _inputSpawnY;
        return bEdited;
    }

    bool TileMapModel::applyToolInput( const TileMapToolInput& input )
    {
        if ( input._bClicked )
            ++_strokeSerial;
        // 스포이트로 집은 버튼을 놓기 전까지 칠하지 않는다 - 끌면 집은 값으로 지나간 칸을 덮어썼다(유니티 Tile Palette 의 Picker 는 놓을 때 끝난다).
        if ( _bAwaitRelease )
        {
            _bAwaitRelease = input._bButtonDown ? SW_TRUE : SW_FALSE;
            return false;
        }

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
            case PaintTool::Line:
            {
                if ( input._bClicked )
                {
                    _dragStartCell = hoverCell;
                    _bDragActive   = SW_TRUE;
                }
                if ( _bDragActive && input._bButtonDown == false )
                {
                    _bDragActive = SW_FALSE;
                    if ( _tool == PaintTool::Rect )
                        TileMapPaintUtil::collectRectCells( _dragStartCell, hoverCell, _map._width, _map._height, _listStrokeCell );
                    else
                        TileMapPaintUtil::collectLineCells( _dragStartCell, hoverCell, _listStrokeCell );
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
                    _tool          = PaintTool::Brush; // 유니티 Tile Palette 처럼 집은 뒤에는 칠한다
                    _bAwaitRelease = input._bButtonDown ? SW_TRUE : SW_FALSE;
                }
                break;
            }
        }
        return bEdited;
    }

    bool TileMapModel::paintStrokeCells()
    {
        bool bEdited = false;
        for ( const int2& strokeCell : _listStrokeCell )
        {
            if ( paintCell( strokeCell._x, strokeCell._y ) )
                bEdited = true;
        }
        return bEdited;
    }

    void TileMapModel::collectCellValues( vector<uint64>& outListValue )
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
                // 맵의 팔레트 번호로 비교한다 - 타일셋 번호로 옮기면 타일셋에 없는 브러시가 빈 칸과 한 영역이 되어 채우기에 덮였다.
                for ( size_t cellIndex = 0; cellIndex < cellCount && cellIndex < _map._listTileCell.size(); ++cellIndex )
                {
                    outListValue[cellIndex] = _map._listTileCell[cellIndex];
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

    void TileMapModel::pickCell( int32 x, int32 y )
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
                const string_view      brushName = _map.getTileBrushName( x, y );
                const optional<uint32> found     = brushName.empty() ? nullopt : _tileSet.findBrush( hashed_string( string( brushName ).c_str() ) );
                _bErase                          = found.has_value() == false;
                if ( found.has_value() )
                    _brushIndex = static_cast<int32>( *found );
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

    bool TileMapModel::addDataLayer( string_view name, int32 defaultValue )
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

    bool TileMapModel::removeSelectedDataLayer()
    {
        if ( _layer != PaintLayer::Data || _dataLayerIndex < 0 || static_cast<size_t>( _dataLayerIndex ) >= _map._listDataLayer.size() )
            return false;
        _map._listDataLayer.erase( _map._listDataLayer.begin() + _dataLayerIndex );
        _dataLayerIndex = MathUtil::min( _dataLayerIndex, static_cast<int32>( _map._listDataLayer.size() ) - 1 );
        if ( _dataLayerIndex < 0 )
        {
            _layer          = PaintLayer::Flag;
            _dataLayerIndex = 0;
        }
        return true;
    }

    bool TileMapModel::useTileSetInput()
    {
        const string path = _tileSetBuffer.c_str();
        if ( path == _map._tileSetPath )
            return false;
        // 런타임과 같은 공유 캐시의 한 벌로 읽어 본다(쥐지 않는다).
        if ( path.empty() == false && SharedAssetCache<TileSetAsset>::acquire( path ) == nullptr )
        {
            _status = SW_LOCKEYFORMAT( "EditorTileMap", "TileSetReadFailed", TextArgumentList().addText( "path", path ) );
            return false;
        }
        _map._tileSetPath = path;
        refreshTileSet();
        return true;
    }

    uint32 TileMapModel::countSelectedFlagCells() const
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

    uint32 TileMapModel::countDataCells() const
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

    bool TileMapModel::resize( int32 width, int32 height )
    {
        // 크기는 Width/Height 필드에서 그대로 온다. 상한과 그 이유는 `TileMapXMLData::kMaxTileCount` 에 있다.
        if ( TileMapXMLData::isSizeSupported( width, height ) == false )
        {
            _status = SW_LOCKEY( "EditorTileMap", "SizeOutOfRange" );
            SW_LOG_WARNING_ID( "TileMapModel.TileMapResizeXBeyond", "TileMap resize %#x%# is beyond the supported tile count (%#)",
                               width, height, TileMapXMLData::kMaxTileCount );
            return false;
        }
        _inputWidth  = width;
        _inputHeight = height;
        if ( width == _map._width && height == _map._height )
            return false;

        const TileMapXMLData oldMap = _map;
        (void)_map.resetTiles( width, height ); // 크기는 위에서 봤다
        const int32 keptWidth  = MathUtil::min( oldMap._width, width );
        const int32 keptHeight = MathUtil::min( oldMap._height, height );
        for ( int32 cellY = 0; cellY < keptHeight; ++cellY )
        {
            for ( int32 cellX = 0; cellX < keptWidth; ++cellX )
            {
                const size_t oldIndex = static_cast<size_t>( cellY ) * static_cast<size_t>( oldMap._width ) + static_cast<size_t>( cellX );
                const size_t newIndex = indexOf( cellX, cellY );
                for ( const TileFlagLayerInfo& info : kArrTileFlagLayerInfo )
                {
                    _map.getFlagLayer( info._layer )[newIndex] = oldMap.getFlagLayer( info._layer )[oldIndex];
                }
                _map._listVisual[newIndex] = oldMap._listVisual[oldIndex];
                for ( size_t layerIndex = 0; layerIndex < _map._listDataLayer.size(); ++layerIndex )
                {
                    _map._listDataLayer[layerIndex]._listValue[newIndex] = oldMap._listDataLayer[layerIndex]._listValue[oldIndex];
                }
                if ( oldIndex < oldMap._listTileCell.size() )
                    _map._listTileCell[newIndex] = oldMap._listTileCell[oldIndex];
            }
        }
        for ( const TileMapXMLData::Warp& warp : oldMap._listWarp )
        {
            if ( isInBounds( warp._tileX, warp._tileY ) )
                _map._listWarp.push_back( warp );
        }
        // 맵 밖 스폰은 읽기가 거절한다 - 그대로 두면 저장도 되돌리기(스냅숏 읽기)도 실패한다.
        _map._spawnX = MathUtil::min( _map._spawnX, width - 1 );
        _map._spawnY = MathUtil::min( _map._spawnY, height - 1 );
        _inputSpawnX = _map._spawnX;
        _inputSpawnY = _map._spawnY;
        return true;
    }

    void TileMapModel::startNewMap()
    {
        TileMapXMLData newMap;
        (void)newMap.resetTiles( kNewMapSide, kNewMapSide ); // 상한 안이다
        newMap._name = "Untitled";
        applyMapData( newMap );
    }

    TileMapXMLData TileMapModel::captureMapData() const
    {
        TileMapXMLData data = _map;
        data._name          = _nameBuffer.c_str();
        return data;
    }

    void TileMapModel::applyMapData( const TileMapXMLData& data )
    {
        _map         = data;
        _nameBuffer  = data._name.c_str();
        _inputWidth  = data._width;
        _inputHeight = data._height;
        _inputSpawnX = data._spawnX;
        _inputSpawnY = data._spawnY;
        // 새 맵에 고른 데이터 레이어가 없으면 플래그 레이어로 돌아간다(옛 번호로 다른 레이어를 칠하지 않는다).
        const bool bDataLayerGone = _layer == PaintLayer::Data && ( _dataLayerIndex < 0 || static_cast<size_t>( _dataLayerIndex ) >= _map._listDataLayer.size() );
        if ( bDataLayerGone )
        {
            _layer          = PaintLayer::Flag;
            _dataLayerIndex = 0;
        }
        refreshTileSet();
    }

    void TileMapModel::refreshTileSet()
    {
        if ( _map._tileSetPath == _loadedTileSet )
            return;
        _loadedTileSet = _map._tileSetPath;
        _tileSetBuffer = _map._tileSetPath.c_str();
        _tileSet       = TileSetAsset{};
        _brushIndex    = 0;
        if ( _map._tileSetPath.empty() )
            return;
        // 팔레트는 런타임과 같은 공유 캐시의 한 벌을 사본으로 받는다.
        const shared_ptr<const TileSetAsset> pTileSet = SharedAssetCache<TileSetAsset>::acquire( _map._tileSetPath );
        if ( pTileSet != nullptr )
            _tileSet = *pTileSet;
        else
            _status = SW_LOCKEYFORMAT( "EditorTileMap", "TileSetReadFailed", TextArgumentList().addText( "path", _map._tileSetPath ) );
    }

    const vector<uint16>& TileMapModel::refreshBrushIndices()
    {
        (void)_map.mapTileCells( _tileSet, _listBrushIndex ); // 모르는 브러시는 빈 셀로 보인다 — 읽을 때 이미 알렸다
        return _listBrushIndex;
    }

    bool TileMapModel::paintCell( int32 x, int32 y )
    {
        if ( isInBounds( x, y ) == false )
            return false;

        const size_t tileIndex = indexOf( x, y );
        switch ( _layer )
        {
            case PaintLayer::Visual:
            {
                // 지우기는 새 칸의 값으로 되돌린다(데이터 레이어의 지우기와 같다). 칸은 0..255 를 적는다고 알리므로 묶는다 - 그대로 자르면 -1 이 255, 300 이 44 가 됐다.
                TileMapXMLData::Visual visual{};
                if ( _bErase == false )
                {
                    visual._height  = static_cast<uint8>( MathUtil::clamp( _paintHeight, 0, 255 ) );
                    visual._atlasID = static_cast<uint8>( MathUtil::clamp( _atlasID, 0, 255 ) );
                    visual._tintR   = TileMapModelInternal::toTintByte( _arrTint[0] );
                    visual._tintG   = TileMapModelInternal::toTintByte( _arrTint[1] );
                    visual._tintB   = TileMapModelInternal::toTintByte( _arrTint[2] );
                }
                if ( TileMapModelInternal::isSameVisual( _map._listVisual[tileIndex], visual ) )
                    return false;
                _map._listVisual[tileIndex] = visual;
                break;
            }
            case PaintLayer::Flag:
            {
                uint8&      flag  = _map.getFlagLayer( _flagLayer )[tileIndex];
                const uint8 value = _bErase ? 0 : 1;
                if ( flag == value )
                    return false;
                flag = value;
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
                // 빈 대상은 같은 맵 워프다(`TileWarpParams::_map`). 대상이 비었다고 지우면 집어 온 같은 맵 워프를 칠할 때 도리어 지워졌다.
                if ( _bErase == false )
                    return stampWarp( x, y, _warpTarget.c_str(), _warpTx, _warpTy );
                return VectorUtil::removeIf( _map._listWarp, [x, y]( const TileMapXMLData::Warp& warp )
                { return warp._tileX == x && warp._tileY == y; } ) > 0;
            }
        }
        return true;
    }

    bool TileMapModel::paintEdgeWarp( int32 edge )
    {
        const bool bInvalidEdgeTarget = edge < 0 || edge >= kEdgeCount || _arrEdgePreset[edge]._target.empty();
        if ( bInvalidEdgeTarget )
            return false;

        const EdgeWarpPreset& preset                   = _arrEdgePreset[edge];
        const int32           lastX                    = _map._width - 1;
        const int32           lastY                    = _map._height - 1;
        const int2            arrCorner[kEdgeCount][2] = {
            {    int2{ 0, 0 },     int2{ lastX, 0 }},
            {int2{ lastX, 0 }, int2{ lastX, lastY }},
            {int2{ 0, lastY }, int2{ lastX, lastY }},
            {    int2{ 0, 0 },     int2{ 0, lastY }},
        };
        TileMapPaintUtil::collectRectCells( arrCorner[edge][0], arrCorner[edge][1], _map._width, _map._height, _listStrokeCell );
        bool bEdited = false;
        for ( const int2& cell : _listStrokeCell )
        {
            bEdited = stampWarp( cell._x, cell._y, preset._target.c_str(), preset._tileX, preset._tileY ) || bEdited;
        }
        return bEdited;
    }

    bool TileMapModel::stampWarp( int32 x, int32 y, const utf8* pTargetMap, int32 targetX, int32 targetY )
    {
        uint8&     walkable = _map.getFlagLayer( TileFlagLayer::Walkable )[indexOf( x, y )];
        const bool bOpened  = walkable == 0;
        walkable            = 1;
        for ( TileMapXMLData::Warp& warp : _map._listWarp )
        {
            if ( warp._tileX != x || warp._tileY != y )
                continue;
            if ( string_view( warp._targetMap ) == string_view( pTargetMap ) && warp._targetTileX == targetX && warp._targetTileY == targetY )
                return bOpened;
            warp._targetMap   = pTargetMap;
            warp._targetTileX = targetX;
            warp._targetTileY = targetY;
            return true;
        }
        TileMapXMLData::Warp warpItem{};
        warpItem._tileX       = x;
        warpItem._tileY       = y;
        warpItem._targetMap   = pTargetMap;
        warpItem._targetTileX = targetX;
        warpItem._targetTileY = targetY;
        _map._listWarp.push_back( std::move( warpItem ) );
        return true;
    }

    int32 TileMapModel::getPaintLayerIndex() const
    {
        if ( _layer == PaintLayer::Flag )
            return kFixedPaintLayerCount + static_cast<int32>( _flagLayer );
        if ( _layer == PaintLayer::Data )
            return kFixedPaintLayerCount + static_cast<int32>( kTileFlagLayerCount ) + _dataLayerIndex;
        return static_cast<int32>( _layer );
    }

    void TileMapModel::selectPaintLayer( int32 layerIndex )
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

    void TileMapModel::collectPaintLayerNames( vector<const utf8*>& outListName ) const
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

    void TileMapModel::setTool( PaintTool tool )
    {
        if ( tool == _tool )
            return;
        _tool          = tool;
        _bStrokeActive = SW_FALSE;
        _bDragActive   = SW_FALSE;
    }

    bool TileMapModel::readDragStart( int2& outCell ) const
    {
        if ( _bDragActive == false )
            return false;
        outCell = _dragStartCell;
        return true;
    }

    string TileMapModel::describeCell( int32 x, int32 y ) const
    {
        if ( isInBounds( x, y ) == false )
            return {};
        const size_t tileIndex = indexOf( x, y );
        switch ( _layer )
        {
            case PaintLayer::Visual:
            {
                const TileMapXMLData::Visual&            visual = _map._listVisual[tileIndex];
                StringBuilder<constant::buffer::kMax256> resultBuilder;
                resultBuilder.appendFormat( "%#, %#: %# / %#", x, y, static_cast<int32>( visual._height ), static_cast<int32>( visual._atlasID ) );
                return string( resultBuilder.view() );
            }
            case PaintLayer::Warp:
            {
                for ( const TileMapXMLData::Warp& warp : _map._listWarp )
                {
                    if ( warp._tileX == x && warp._tileY == y )
                    {
                        StringBuilder<constant::buffer::kMax256> resultBuilder;
                        resultBuilder.appendFormat( "%#, %#: %# (%#, %#)", x, y, warp._targetMap, warp._targetTileX, warp._targetTileY );
                        return string( resultBuilder.view() );
                    }
                }
                break;
            }
            case PaintLayer::Tile:
            {
                const string_view brushName = _map.getTileBrushName( x, y );
                if ( brushName.empty() == false )
                {
                    StringBuilder<constant::buffer::kMax256> resultBuilder;
                    resultBuilder.appendFormat( "%#, %#: %#", x, y, brushName );
                    return string( resultBuilder.view() );
                }
                break;
            }
            case PaintLayer::Flag:
            {
                StringBuilder<constant::buffer::kMax256> resultBuilder;
                resultBuilder.appendFormat( "%#, %#: %#", x, y, static_cast<int32>( _map.getFlagLayer( _flagLayer )[tileIndex] ) );
                return string( resultBuilder.view() );
            }
            case PaintLayer::Data:
            {
                int32 value = 0;
                if ( _map.getCellData( _dataLayerIndex, x, y, value ) )
                {
                    StringBuilder<constant::buffer::kMax256> resultBuilder;
                    resultBuilder.appendFormat( "%#, %#: %#", x, y, value );
                    return string( resultBuilder.view() );
                }
                break;
            }
        }
        StringBuilder<constant::buffer::kMax256> resultBuilder;
        resultBuilder.appendFormat( "%#, %#", x, y );
        return string( resultBuilder.view() );
    }

    bool TileMapModel::isInBounds( int32 x, int32 y ) const
    {
        return 0 <= x && x < _map._width && 0 <= y && y < _map._height;
    }

    size_t TileMapModel::indexOf( int32 x, int32 y ) const
    {
        // 곱셈을 size_t 로 한다(Engine 의 TileMap::indexOf 와 같은 이유).
        return static_cast<size_t>( y ) * static_cast<size_t>( _map._width ) + static_cast<size_t>( x );
    }

    const string& TileMapModel::getFocusedAssetPath()
    {
        return editor::getSubsystem<EditorContext>().getSubsystem<EditorWorkspace>().getFocusedAssetPath();
    }

    bool TileMapModel::hasDocumentFile( string_view path )
    {
        return EditorToolAssetCommands::hasToolDocument( path );
    }
} // namespace sw::editor
