#include "pch.h"

#include "Editor/Panels/TileMapPanel.h"

#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"

#include "Editor/Common/Commands/EditorToolAssetCommands.h"
#include "Editor/Common/Config/EditorToolDefaults.h"
#include "Editor/Common/Gui/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Panels/EditorPanelManager.h"
#include "Editor/Panels/TileMapPaintUtil.h"

#include "Engine/Resource/ResourceUtil.h"

#include <imgui.h>

namespace sw::editor
{
    SW_LOG_CALLER( "TileMapPanel" );
    SW_EDITOR_PANEL( TileMapPanel, "tile_map", EditorPanelCategory::Tool, 1500 );

    TileMapPanel::TileMapPanel()
        : EditorDocumentPanel{ EditorAssetType::TileMap, false }
        , _pathBuffer{}
        , _nameBuffer{ "Untitled" }
        , _edgeTargetN{}
        , _edgeTargetE{}
        , _edgeTargetS{}
        , _edgeTargetW{}
        , _warpTarget{}
        , _status{}
        , _map{}
        , _tileSetBuffer{}
        , _tileSet{}
        , _loadedTileSet{}
        , _listBrushIndex{}
        , _listStrokeCell{}
        , _brushIndex{ 0 }
        , _arrEdgeTx{ 1, 1, 1, 1 }
        , _arrEdgeTy{ 1, 1, 1, 1 }
        , _arrTint{ 180.0f / 255.0f, 200.0f / 255.0f, 160.0f / 255.0f }
        , _inputWidth{ 8 }
        , _inputHeight{ 8 }
        , _paintHeight{ 1 }
        , _atlasId{ 0 }
        , _warpTx{ 1 }
        , _warpTy{ 1 }
        , _lastPaintCell{}
        , _layer{ PaintLayer::Flag }
        , _flagLayer{ TileFlagLayer::Walkable }
        , _bErase{ false }
        , _bStrokeActive{ false }
    {
        const EditorToolDefaults& editorToolDefaults = editor::getEditorToolDefaults();
        if ( editorToolDefaults._defaultMap.empty() == false )
            _pathBuffer = editorToolDefaults._defaultMap.c_str();
        if ( editorToolDefaults._warpMap.empty() == false )
            _warpTarget = editorToolDefaults._warpMap.c_str();
        resize( 8, 8 );
    }

    void TileMapPanel::drawContent()
    {
        updateFocusedDocument();
        if ( isDocumentLoaded() == false )
            _pathBuffer = getLoadedAssetPath().c_str();
        ensureDocumentLoaded();

        drawTileMapFileControls();
        ImGui::Checkbox( "Erase", &_bErase );
        ImGui::Separator();

        drawLayerControls();
        ImGui::Text( "Grid %dx%d - click to paint", _map._width, _map._height );

        constexpr float32 cell   = 18.0f;
        ImDrawList*       pDl    = ImGui::GetWindowDrawList();
        const ImVec2      origin = ImGui::GetCursorScreenPos();

        if ( _layer == PaintLayer::Tile )
            (void)_map.mapTileCells( _tileSet, _listBrushIndex ); // 모르는 브러시는 빈 칸으로 보인다 — 읽을 때 이미 알렸다

        unordered_set<uint64> uniqueWarpCells;
        if ( _layer == PaintLayer::Warp )
        {
            uniqueWarpCells.reserve( _map._listWarp.size() );
            for ( const TileMapXMLData::Warp& warp : _map._listWarp )
            {
                uniqueWarpCells.insert( ( static_cast<uint64>( static_cast<uint32>( warp._tileY ) ) << 32 ) |
                                        static_cast<uint32>( warp._tileX ) );
            }
        }

        for ( int32 tileY = 0; tileY < _map._height; ++tileY )
        {
            for ( int32 tileX = 0; tileX < _map._width; ++tileX )
            {
                const size_t tileIndex = indexOf( tileX, tileY );
                ImU32        color     = IM_COL32( 60, 60, 70, 255 );
                switch ( _layer )
                {
                    case PaintLayer::Visual:
                    {
                        const TileMapXMLData::Visual& tileVisual = _map._listVisual[tileIndex];
                        color                                    = IM_COL32( tileVisual._tintR, tileVisual._tintG, tileVisual._tintB, 255 );
                        break;
                    }
                    case PaintLayer::Warp:
                    {
                        const uint64 key = ( static_cast<uint64>( static_cast<uint32>( tileY ) ) << 32 ) | static_cast<uint32>( tileX );
                        color            = uniqueWarpCells.count( key ) ? IM_COL32( 200, 120, 80, 255 ) : IM_COL32( 50, 50, 55, 255 );
                        break;
                    }
                    case PaintLayer::Tile:
                    {
                        break; // 아래에서 칸마다 그린다(브러시 색 + 규칙이 고른 칸 번호)
                    }
                    case PaintLayer::Flag:
                    {
                        const TileFlagLayerInfo& info = kArrTileFlagLayerInfo[static_cast<size_t>( _flagLayer )];
                        const uint32             rgb  = _map.getFlagLayer( _flagLayer )[tileIndex] != 0 ? info._onColorRgb : info._offColorRgb;
                        color                         = IM_COL32( ( rgb >> 16 ) & 0xFFu, ( rgb >> 8 ) & 0xFFu, rgb & 0xFFu, 255 );
                        break;
                    }
                }

                const float32 fx = static_cast<float32>( tileX );
                const float32 fy = static_cast<float32>( tileY );
                const ImVec2  p0( origin.x + fx * cell, origin.y + fy * cell );
                const ImVec2  p1( p0.x + cell - 1.0f, p0.y + cell - 1.0f );
                pDl->AddRectFilled( p0, p1, color );
                if ( _layer == PaintLayer::Tile )
                    drawTileLayerCell( pDl, p0, p1, tileX, tileY );
                pDl->AddRect( p0, p1, IM_COL32( 20, 20, 24, 255 ) );
            }
        }

        ImGui::InvisibleButton( "##tilegrid",
                                ImVec2( static_cast<float32>( _map._width ) * cell, static_cast<float32>( _map._height ) * cell ) );
        const bool bPainting = ImGui::IsItemHovered() && ImGui::IsMouseDown( ImGuiMouseButton_Left );
        if ( bPainting )
        {
            const ImVec2 mouse = ImGui::GetMousePos();
            const int2   cellNow{ static_cast<int32>( ( mouse.x - origin.x ) / cell ), static_cast<int32>( ( mouse.y - origin.y ) / cell ) };
            // 한 프레임에 마우스가 여러 칸을 지나가도 끊기지 않게 지난 프레임에 칠한 칸에서 이번 칸까지 선분으로 칠한다.
            TileMapPaintUtil::collectLineCells( _bStrokeActive ? _lastPaintCell : cellNow, cellNow, _listStrokeCell );
            for ( const int2& strokeCell : _listStrokeCell )
            {
                paintCell( strokeCell._x, strokeCell._y );
            }
            _lastPaintCell = cellNow;
        }
        _bStrokeActive = bPainting;

        EditorWidgets::drawPanelStatus( _status.c_str() );
    }

    void TileMapPanel::drawTileMapFileControls()
    {
        EditorContext* pContext = EditorContext::get();
        if ( pContext == nullptr )
            return;

        const string& focused = pContext->getWorkspace().getFocusedAssetPath();
        if ( focused.empty() == false )
            ImGui::TextDisabled( "Focused: %s", focused.c_str() );

        ImGui::InputText( "Path", _pathBuffer.data(), _pathBuffer.capacity() );
        ImGui::InputText( "Name", _nameBuffer.data(), _nameBuffer.capacity() );
        if ( ImGui::IsItemDeactivatedAfterEdit() )
            notifyDocumentEdited( "Edit Tile Map Name", "tilemap-name" );
        ImGui::InputInt( "Width", &_inputWidth );
        ImGui::SameLine();
        ImGui::InputInt( "Height", &_inputHeight );
        if ( ImGui::Button( "Apply Size" ) )
        {
            resize( MathUtil::max( 1, _inputWidth ), MathUtil::max( 1, _inputHeight ) );
            notifyDocumentEdited( "Resize Tile Map" );
        }

        ImGui::SameLine();
        if ( ImGui::Button( "Load" ) )
        {
            // 적어 넣은 경로에 파일이 없으면 지금 맵을 그대로 둔다(새 문서로 표시하면 편집 중인 맵이 저장 안 한 채 깨끗해진다).
            if ( ResourceUtil::hasResource( _pathBuffer.c_str() ) )
                reloadDocument();
            else
                SW_LOG_WARNING( "No tile map at '%#'", _pathBuffer.c_str() );
        }
        ImGui::SameLine();
        if ( ImGui::Button( "Save" ) )
        {
            if ( saveXML( _pathBuffer.c_str() ) )
            {
                _status = string( "Saved " ) + _pathBuffer.c_str();
                clearDocumentDirty();
            }
            else
                _status = "Save failed";
        }
    }

    void TileMapPanel::drawLayerControls()
    {
        // 목록은 Visual · Warp 다음에 플래그 레이어 표 순서다. 순번 ↔ 레이어는 getPaintLayerIndex / selectPaintLayer 가 정한다.
        const utf8* arrLayerName[kFixedPaintLayerCount + kTileFlagLayerCount] = { "Visual", "Warp", "Tile" };
        for ( size_t flagIndex = 0; flagIndex < kTileFlagLayerCount; ++flagIndex )
        {
            arrLayerName[kFixedPaintLayerCount + flagIndex] = kArrTileFlagLayerInfo[flagIndex]._pName;
        }
        int32 layerIndex = getPaintLayerIndex();
        if ( ImGui::Combo( "Layer", &layerIndex, arrLayerName, static_cast<int32>( SW_COUNT_OF( arrLayerName ) ) ) )
            selectPaintLayer( layerIndex );

        if ( _layer == PaintLayer::Visual )
        {
            ImGui::InputInt( "Height", &_paintHeight );
            ImGui::InputInt( "Atlas Id", &_atlasId );
            ImGui::ColorEdit3( "Tint", _arrTint );
        }
        else if ( _layer == PaintLayer::Warp )
        {
            ImGui::InputText( "Warp Target", _warpTarget.data(), _warpTarget.capacity() );
            ImGui::InputInt( "Target TX", &_warpTx );
            ImGui::InputInt( "Target TY", &_warpTy );
        }
        else if ( _layer == PaintLayer::Tile )
        {
            ImGui::InputText( "Tile Set", _tileSetBuffer.data(), _tileSetBuffer.capacity() );
            ImGui::SameLine();
            if ( ImGui::Button( "Use" ) )
            {
                _map._tileSetPath = _tileSetBuffer.c_str();
                refreshTileSet();
                notifyDocumentEdited( "Set Tile Set", "tilemap-tileset" );
            }
            const vector<TileBrush>& listBrush = _tileSet.getBrushes();
            if ( listBrush.empty() )
                ImGui::TextDisabled( "No tile set loaded" );
            for ( int32 brushIndex = 0; brushIndex < static_cast<int32>( listBrush.size() ); ++brushIndex )
            {
                const TileBrush& brush = listBrush[static_cast<size_t>( brushIndex )];
                const string     label = string( brush._name.c_str() ) + ( brush.isRuleTile() ? " (rule)" : "" ) + ( brush._defaultVisual.isAnimated() ? " (animated)" : "" );
                if ( ImGui::RadioButton( label.c_str(), _brushIndex == brushIndex ) )
                    _brushIndex = brushIndex;
            }
        }

        ImGui::Separator();
        ImGui::TextUnformatted( "Edge Warp Presets" );
        const utf8*                            edges[] = { "N", "E", "S", "W" };
        fixed_string<constant::kMaxBuffer128>* bufs[]  = { &_edgeTargetN, &_edgeTargetE, &_edgeTargetS, &_edgeTargetW };
        for ( int32 edgeIndex = 0; edgeIndex < 4; ++edgeIndex )
        {
            ImGui::PushID( edgeIndex );
            ImGui::SetNextItemWidth( 180.0f * EditorThemeUtil::getDpiScale() );
            ImGui::InputText( edges[edgeIndex], bufs[edgeIndex]->data(), bufs[edgeIndex]->capacity() );
            ImGui::SameLine();
            ImGui::SetNextItemWidth( 50.0f * EditorThemeUtil::getDpiScale() );
            ImGui::InputInt( "##tx", &_arrEdgeTx[edgeIndex] );
            ImGui::SameLine();
            ImGui::SetNextItemWidth( 50.0f * EditorThemeUtil::getDpiScale() );
            ImGui::InputInt( "##ty", &_arrEdgeTy[edgeIndex] );
            ImGui::SameLine();
            if ( ImGui::Button( "Apply" ) )
                paintEdgeWarp( edgeIndex );
            ImGui::PopID();
        }

        ImGui::Separator();
    }

    void TileMapPanel::resize( int32 width, int32 height )
    {
        // 크기는 Width/Height 칸에서 그대로 온다. 상한과 그 이유는 `TileMapXMLData::kMaxTileCount` 에 있다.
        if ( _map.resetTiles( width, height ) == false )
        {
            _status = "Size is out of range.";
            SW_LOG_WARNING( "TileMap resize %#x%# is beyond the supported tile count (%#)",
                            width, height, TileMapXMLData::kMaxTileCount );
            return;
        }
        _inputWidth  = width;
        _inputHeight = height;
    }

    ToolAssetLoadResult TileMapPanel::loadDocument()
    {
        // 경로가 없으면 새 맵(기본 8x8)이다. 경로 칸은 첫 로드 때 열린 문서로, Load 단추 때는 적어 넣은 경로로 채워져 있다.
        if ( _pathBuffer.empty() )
            return ToolAssetLoadResult::Missing;
        return loadXML( _pathBuffer.c_str() );
    }

    ToolAssetLoadResult TileMapPanel::loadXML( string_view assetRelativePath )
    {
        TileMapXMLData            data;
        const ToolAssetLoadResult result = EditorToolAssetCommands::loadTileMap( assetRelativePath, data, _status );
        if ( result != ToolAssetLoadResult::Loaded )
            return result;

        applyMapData( data );
        _pathBuffer = string( assetRelativePath ).c_str();
        return ToolAssetLoadResult::Loaded;
    }

    bool TileMapPanel::saveXML( string_view assetRelativePath )
    {
        if ( EditorToolAssetCommands::saveTileMap( assetRelativePath, captureMapData() ) == false )
            return false;
        clearDocumentDirty();
        syncDocumentUndoBaseline();
        return true;
    }

    bool TileMapPanel::saveDocument()
    {
        if ( saveXML( _pathBuffer.c_str() ) == false )
            return false;
        return true;
    }

    TileMapXMLData TileMapPanel::captureMapData() const
    {
        TileMapXMLData data = _map;
        data._name          = _nameBuffer.c_str();
        return data;
    }

    void TileMapPanel::applyMapData( const TileMapXMLData& data )
    {
        _map         = data;
        _nameBuffer  = data._name.c_str();
        _inputWidth  = data._width;
        _inputHeight = data._height;
        refreshTileSet();
    }

    void TileMapPanel::refreshTileSet()
    {
        if ( _map._tileSetPath == _loadedTileSet )
            return;
        _loadedTileSet = _map._tileSetPath;
        _tileSetBuffer = _map._tileSetPath.c_str();
        _tileSet       = TileSetAsset{};
        _brushIndex    = 0;
        if ( _map._tileSetPath.empty() == false && _tileSet.loadFromResource( _map._tileSetPath ) == false )
            _status = string( "Tile set could not be read: " ) + _map._tileSetPath;
    }

    void TileMapPanel::drawTileLayerCell( ImDrawList* pDrawList, const ImVec2& cellMin, const ImVec2& cellMax, int32 x, int32 y ) const
    {
        const TileVisual* pVisual = _tileSet.resolveVisual( _listBrushIndex, _map._width, _map._height, x, y );
        if ( pVisual == nullptr )
            return;
        // 브러시마다 다른 색(이름 해시) + 규칙이 고른 아틀라스 칸 번호 — 이웃을 칠하면 번호가 바뀌는 것이 규칙 타일이 일하는 모습이다.
        const uint16 brushValue = _listBrushIndex[indexOf( x, y )];
        const uint32 hash       = static_cast<uint32>( _tileSet.getBrushes()[brushValue - 1u]._name.getHash() );
        const ImU32  color      = IM_COL32( 80 + ( hash & 0x7Fu ), 80 + ( ( hash >> 8 ) & 0x7Fu ), 80 + ( ( hash >> 16 ) & 0x7Fu ), 255 );
        pDrawList->AddRectFilled( cellMin, cellMax, color );
        const string number = to_string( pVisual->computeCellAt( 0.0f ) );
        pDrawList->AddText( ImVec2( cellMin.x + 2.0f, cellMin.y + 1.0f ), IM_COL32( 255, 255, 255, 255 ), number.c_str() );
    }

    string TileMapPanel::captureDocumentText() const
    {
        return captureMapData().toXML();
    }

    void TileMapPanel::applyDocumentText( string_view text )
    {
        TileMapXMLData restored;
        if ( text.empty() == false && restored.loadFromXML( text ) == false )
            SW_LOG_WARNING( "Tile map undo snapshot could not be read - showing an empty map" );
        applyMapData( restored );
    }

    void TileMapPanel::paintCell( int32 x, int32 y )
    {
        if ( isInBounds( x, y ) == false )
            return;

        const size_t tileIndex = indexOf( x, y );
        switch ( _layer )
        {
            case PaintLayer::Visual:
            {
                if ( _bErase == false )
                {
                    TileMapXMLData::Visual& tileVisual = _map._listVisual[tileIndex];
                    tileVisual._height                 = static_cast<uint8>( _paintHeight );
                    tileVisual._atlasId                = static_cast<uint8>( _atlasId );
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
            case PaintLayer::Tile:
            {
                const vector<TileBrush>& listBrush = _tileSet.getBrushes();
                if ( _bErase == false && ( _brushIndex < 0 || _brushIndex >= static_cast<int32>( listBrush.size() ) ) )
                    return;
                const string_view brushName = _bErase ? string_view{} : string_view( listBrush[static_cast<size_t>( _brushIndex )]._name.c_str() );
                if ( _map.getTileBrushName( x, y ) == brushName )
                    return;                                 // 끌어 칠할 때 같은 칸을 프레임마다 되돌리기 기록에 넣지 않는다
                (void)_map.setTileBrush( x, y, brushName ); // 범위는 위에서 봤다
                break;
            }
            case PaintLayer::Warp:
            {
                _map._listWarp.erase( std::remove_if( _map._listWarp.begin(), _map._listWarp.end(),
                                                      [x, y]( const TileMapXMLData::Warp& warp )
                { return warp._tileX == x && warp._tileY == y; } ),
                                      _map._listWarp.end() );
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
        notifyDocumentEdited( "Paint Tile Map", "tilemap-paint" );
    }

    void TileMapPanel::paintEdgeWarp( int32 edge )
    {
        const fixed_string<constant::kMaxBuffer128>* targets[] = { &_edgeTargetN, &_edgeTargetE, &_edgeTargetS, &_edgeTargetW };
        if ( edge < 0 || edge > 3 || targets[edge]->empty() )
            return;

        auto stamp = [&]( int32 tileX, int32 tileY )
        {
            _map.getFlagLayer( TileFlagLayer::Walkable )[indexOf( tileX, tileY )] = 1;
            _map._listWarp.erase( std::remove_if( _map._listWarp.begin(), _map._listWarp.end(),
                                                  [tileX, tileY]( const TileMapXMLData::Warp& warp )
            { return warp._tileX == tileX && warp._tileY == tileY; } ),
                                  _map._listWarp.end() );
            TileMapXMLData::Warp warpItem{};
            warpItem._tileX       = tileX;
            warpItem._tileY       = tileY;
            warpItem._targetMap   = targets[edge]->c_str();
            warpItem._targetTileX = _arrEdgeTx[edge];
            warpItem._targetTileY = _arrEdgeTy[edge];
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
        notifyDocumentEdited( "Paint Tile Map Edge", "tilemap-edge" );
    }

    int32 TileMapPanel::getPaintLayerIndex() const
    {
        if ( _layer == PaintLayer::Flag )
            return kFixedPaintLayerCount + static_cast<int32>( _flagLayer );
        return static_cast<int32>( _layer );
    }

    void TileMapPanel::selectPaintLayer( int32 layerIndex )
    {
        if ( layerIndex < kFixedPaintLayerCount )
        {
            _layer = static_cast<PaintLayer>( layerIndex );
            return;
        }
        _layer     = PaintLayer::Flag;
        _flagLayer = static_cast<TileFlagLayer>( layerIndex - kFixedPaintLayerCount );
    }

    bool TileMapPanel::isInBounds( int32 x, int32 y ) const
    {
        return 0 <= x && x < _map._width && 0 <= y && y < _map._height;
    }

    size_t TileMapPanel::indexOf( int32 x, int32 y ) const
    {
        // 곱셈을 size_t 로 한다(Engine 의 TileMap::indexOf 와 같은 이유).
        return static_cast<size_t>( y ) * static_cast<size_t>( _map._width ) + static_cast<size_t>( x );
    }
} // namespace sw::editor
