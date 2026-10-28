#include "pch.h"

#include "Editor/Panels/TileMap/TileMapPanel.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Math/MathUtil.h"

#include "Editor/Common/Commands/Asset/EditorToolAssetCommands.h"
#include "Editor/Common/Config/EditorToolDefaults.h"
#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/EditorColorUtil.h"
#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/GUI/EditorTextExtentImGui.h"
#include "Editor/Common/GUI/EditorThemeUtil.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Panels/TileMap/Model/TileMapPaintUtil.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"
#include "Engine/Probe/AutomationProbe.h"
#include "Engine/Resource/ResourceIO.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct TileMapPanelInternal
        {
            static constexpr Color kCanvasBg{ Color::fromBytes( 28, 28, 32, 255 ) };
            static constexpr Color kEmptyCell{ Color::fromBytes( 60, 60, 70, 255 ) };
            static constexpr Color kWarpCell{ Color::fromBytes( 200, 120, 80, 255 ) };
            static constexpr Color kNoWarpCell{ Color::fromBytes( 50, 50, 55, 255 ) };
            static constexpr Color kCellBorder{ Color::fromBytes( 20, 20, 24, 255 ) };

            static constexpr Color kBrushPreview{ Color::fromBytes( 255, 255, 255 ) };
            static constexpr Color kBrushPreviewErase{ Color::fromBytes( 255, 90, 90 ) };
            static constexpr Color kCellNumberText{ Color::fromBytes( 255, 255, 255 ) };
            /** @brief 플래그 층의 `0xRRGGBB` 색을 불투명 `0xAARRGGBB` 로 만드는 알파 바이트입니다. */
            static constexpr uint32 kOpaqueAlphaMask = 0xFF000000u;
            /** @brief 확대 1 의 셀 한 변(96 DPI px)입니다. */
            static constexpr float32 kBaseCellSize = 18.0f;
            /** @brief 확대 범위입니다. */
            static constexpr float32 kMinZoom = 0.25f;
            static constexpr float32 kMaxZoom = 4.0f;
            /** @brief 캔버스 왼쪽 위와 격자 사이의 처음 여백(px)입니다. */
            static constexpr float32 kCanvasMargin = 4.0f;

            [[nodiscard]] static bool readFlagCells( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                EditorContext* pContext = editor::findSubsystem<EditorContext>();
                if ( pContext == nullptr )
                    return false;
                const TileMapPanel* pPanel = pContext->getSubsystem<EditorPanelManager>().findPanel<TileMapPanel>();
                if ( pPanel == nullptr )
                    return false;
                outValue = static_cast<float64>( pPanel->countSelectedFlagCells() );
                return true;
            }

            [[nodiscard]] static bool readDataLayerCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                EditorContext* pContext = editor::findSubsystem<EditorContext>();
                if ( pContext == nullptr )
                    return false;
                const TileMapPanel* pPanel = pContext->getSubsystem<EditorPanelManager>().findPanel<TileMapPanel>();
                if ( pPanel == nullptr )
                    return false;
                outValue = static_cast<float64>( pPanel->getDataLayerCount() );
                return true;
            }

            [[nodiscard]] static bool readDataCells( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                EditorContext* pContext = editor::findSubsystem<EditorContext>();
                if ( pContext == nullptr )
                    return false;
                const TileMapPanel* pPanel = pContext->getSubsystem<EditorPanelManager>().findPanel<TileMapPanel>();
                if ( pPanel == nullptr )
                    return false;
                outValue = static_cast<float64>( pPanel->countDataCells() );
                return true;
            }

            [[nodiscard]] static bool readPaintTool( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                EditorContext* pContext = editor::findSubsystem<EditorContext>();
                if ( pContext == nullptr )
                    return false;
                const TileMapPanel* pPanel = pContext->getSubsystem<EditorPanelManager>().findPanel<TileMapPanel>();
                if ( pPanel == nullptr )
                    return false;
                outValue = static_cast<float64>( pPanel->getPaintToolIndex() );
                return true;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_LOG_CALLER( "TileMapPanel" );
    SW_EDITOR_PANEL( TileMapPanel, TileMapPanel::kPanelID, EditorPanelCategory::Tool, 1500 );
    SW_AUTOMATION_PROBE( editorTileMapFlagCells, "Editor.TileMapFlagCells", "Cells set in the Tile Map Tool's selected flag layer (0 on other layers)",
                         &TileMapPanelInternal::readFlagCells );
    SW_AUTOMATION_PROBE( editorTileMapDataLayers, "Editor.TileMapDataLayers", "Data layers of the Tile Map Tool's map", &TileMapPanelInternal::readDataLayerCount );
    SW_AUTOMATION_PROBE( editorTileMapDataCells, "Editor.TileMapDataCells", "Cells that differ from the layer default, summed over all data layers of the Tile Map Tool's map",
                         &TileMapPanelInternal::readDataCells );
    SW_AUTOMATION_PROBE( editorTileMapTool, "Editor.TileMapTool", "Tile Map Tool paint tool: 0 Brush, 1 Rect, 2 Fill, 3 Picker", &TileMapPanelInternal::readPaintTool );

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
        , _listCellValue{}
        , _atlasThumbnail{}
        , _canvasPan{ TileMapPanelInternal::kCanvasMargin, TileMapPanelInternal::kCanvasMargin }
        , _canvasZoom{ 1.0f }
        , _brushIndex{ 0 }
        , _arrEdgeTx{ 1, 1, 1, 1 }
        , _arrEdgeTy{ 1, 1, 1, 1 }
        , _arrTint{ 180.0f / 255.0f, 200.0f / 255.0f, 160.0f / 255.0f }
        , _inputWidth{ 8 }
        , _inputHeight{ 8 }
        , _paintHeight{ 1 }
        , _atlasID{ 0 }
        , _warpTx{ 1 }
        , _warpTy{ 1 }
        , _lastPaintCell{}
        , _rectStartCell{}
        , _newDataLayerName{}
        , _newDataLayerDefault{ 0 }
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
        resize( 8, 8 );
    }

    void TileMapPanel::drawDocumentContent()
    {
        updateFocusedDocument();
        if ( isDocumentLoaded() == false )
            _pathBuffer = getLoadedAssetPath().c_str();
        ensureDocumentLoaded();
        drawDocumentOpenBar( "tileMap" );
        _atlasThumbnail.update();

        // 왼쪽은 파일 · 레이어 · 팔레트, 오른쪽은 캔버스다(유니티 Tile Palette 와 씬 뷰를 한 창에).
        const float32 statusHeight = ImGui::GetFrameHeightWithSpacing();
        const float32 sideWidth    = measureExtent( EditorTextExtent::ofText( 40.0f, 0.0f ) ).x;
        if ( ImGui::BeginChild( "##tileMapSide", ImVec2( sideWidth, -statusHeight ), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX ) )
        {
            drawTileMapFileControls();
            ImGui::Separator();
            drawLayerControls();
        }
        ImGui::EndChild();
        ImGui::SameLine();
        if ( ImGui::BeginChild( "##tileMapCanvasArea", ImVec2( 0.0f, -statusHeight ) ) )
        {
            drawToolBar();
            drawCanvas();
        }
        ImGui::EndChild();

        EditorWidgets::drawPanelStatus( _status.c_str() );
    }

    void TileMapPanel::drawToolBar()
    {
        struct ToolButton
        {
            const utf8* _pLabel;
            const utf8* _pMark;
            const utf8* _pTooltip;
            PaintTool   _tool;
        };
        // 툴팁은 지금 문화권의 글이라 상수 테이블에 두지 않는다.
        const ToolButton arrToolButton[] = {
            { "Brush",  "tileMap.tool.brush", SW_LOCKEY( "EditorTileMap",  "BrushTip" ),  PaintTool::Brush},
            {  "Rect",   "tileMap.tool.rect", SW_LOCKEY( "EditorTileMap",   "RectTip" ),   PaintTool::Rect},
            {  "Fill",   "tileMap.tool.fill", SW_LOCKEY( "EditorTileMap",   "FillTip" ),   PaintTool::Fill},
            {"Picker", "tileMap.tool.picker", SW_LOCKEY( "EditorTileMap", "PickerTip" ), PaintTool::Picker},
        };
        for ( const ToolButton& button : arrToolButton )
        {
            if ( ImGui::RadioButton( button._pLabel, _tool == button._tool ) )
                _tool = button._tool;
            EditorSelfTestMarks::note( button._pMark );
            EditorWidgets::drawTooltip( button._pTooltip );
            ImGui::SameLine();
        }
        ImGui::Checkbox( "Erase", &_bErase );
        EditorSelfTestMarks::note( "tileMap.erase" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileMap", "EraseTip" ) );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Reset View" ) );
        if ( ImGui::SmallButton( "Reset View" ) )
        {
            _canvasPan  = float2{ TileMapPanelInternal::kCanvasMargin, TileMapPanelInternal::kCanvasMargin };
            _canvasZoom = 1.0f;
        }
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileMap", "ResetViewTip" ) );
        ImGui::SameLine();
        ImGui::TextDisabled( "%dx%d  %.0f%%", _map._width, _map._height, static_cast<float64>( _canvasZoom * 100.0f ) );

        // 유니티 Tile Palette 의 단축키 — `Editor.Panel.tile_map` 레이어의 액션(이 창이 포커스이고 글을 치는 중이 아닐 때만 발화한다).
        if ( EditorInputGUI::wasActionTriggered( "tileMap.brush" ) )
            _tool = PaintTool::Brush;
        if ( EditorInputGUI::wasActionTriggered( "tileMap.rect" ) )
            _tool = PaintTool::Rect;
        if ( EditorInputGUI::wasActionTriggered( "tileMap.fill" ) )
            _tool = PaintTool::Fill;
        if ( EditorInputGUI::wasActionTriggered( "tileMap.picker" ) )
            _tool = PaintTool::Picker;
    }

    void TileMapPanel::drawCanvas()
    {
        const ImVec2 canvasMin  = ImGui::GetCursorScreenPos();
        ImVec2       canvasSize = ImGui::GetContentRegionAvail();
        canvasSize.x            = MathUtil::max( canvasSize.x, 64.0f );
        canvasSize.y            = MathUtil::max( canvasSize.y, 64.0f );
        ImGui::InvisibleButton( "##tilegrid", canvasSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle | ImGuiButtonFlags_MouseButtonRight );
        EditorSelfTestMarks::note( "tileMap.canvas" );
        const bool bHovered = ImGui::IsItemHovered();
        const bool bActive  = ImGui::IsItemActive();

        updateCanvasView( canvasMin, bHovered, bActive );

        const float32 cell = TileMapPanelInternal::kBaseCellSize * EditorThemeUtil::getDpiScale() * _canvasZoom;
        const ImVec2  origin( canvasMin.x + _canvasPan._x, canvasMin.y + _canvasPan._y );
        const ImVec2  canvasMax( canvasMin.x + canvasSize.x, canvasMin.y + canvasSize.y );
        ImDrawList*   pDl = ImGui::GetWindowDrawList();
        pDl->PushClipRect( canvasMin, canvasMax, true );
        pDl->AddRectFilled( canvasMin, canvasMax, EditorColorUtil::toImU32( TileMapPanelInternal::kCanvasBg ) );

        drawGridCells( pDl, origin, cell );

        const int2 hoverCell = TileMapPaintUtil::findCellAt( EditorInputGUI::readPointer()._position._x - origin.x, EditorInputGUI::readPointer()._position._y - origin.y, cell );
        drawBrushPreview( pDl, origin, cell, hoverCell, bHovered );
        pDl->PopClipRect();

        applyToolInput( hoverCell, bHovered );
    }

    void TileMapPanel::updateCanvasView( const ImVec2& canvasMin, bool bHovered, bool bActive )
    {
        // 휠은 커서 아래 위치를 붙든 채 확대하고, 가운데 · 오른쪽 끌기는 이동한다(유니티 씬 뷰 2D).
        const EditorPointerState pointer = EditorInputGUI::readPointer();
        if ( bHovered && pointer._wheel != 0.0f )
        {
            const float32 oldZoom = _canvasZoom;
            _canvasZoom           = MathUtil::clamp( _canvasZoom * ( 1.0f + 0.1f * pointer._wheel ), TileMapPanelInternal::kMinZoom, TileMapPanelInternal::kMaxZoom );
            const float32 mouseX  = pointer._position._x - canvasMin.x;
            const float32 mouseY  = pointer._position._y - canvasMin.y;
            const float32 ratio   = _canvasZoom / oldZoom;
            _canvasPan._x         = mouseX - ( mouseX - _canvasPan._x ) * ratio;
            _canvasPan._y         = mouseY - ( mouseY - _canvasPan._y ) * ratio;
        }
        // 화면 이동은 끌기 문턱(입력 맵의 `dragThreshold`)을 넘은 뒤부터다 — 문턱 0 이면 오른쪽 클릭(메뉴)도 한 픽셀 끌기가 됐다(R1).
        if ( bActive && ( pointer.isDragging( EditorMouseButton::Middle ) || pointer.isDragging( EditorMouseButton::Right ) ) )
        {
            _canvasPan._x += pointer._delta._x;
            _canvasPan._y += pointer._delta._y;
        }
    }

    void TileMapPanel::drawGridCells( ImDrawList* pDl, const ImVec2& origin, float32 cell )
    {
        void* pAtlasTexture{ nullptr };
        if ( _layer == PaintLayer::Tile )
        {
            (void)_map.mapTileCells( _tileSet, _listBrushIndex ); // 모르는 브러시는 빈 셀로 보인다 — 읽을 때 이미 알렸다
            if ( _tileSet.getAtlasPath().empty() == false )
                pAtlasTexture = _atlasThumbnail.findTexture( _tileSet.getAtlasPath() );
        }

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
                ImU32        color     = EditorColorUtil::toImU32( TileMapPanelInternal::kEmptyCell );
                switch ( _layer )
                {
                    case PaintLayer::Visual:
                    {
                        const TileMapXMLData::Visual& tileVisual = _map._listVisual[tileIndex];
                        color                                    = EditorColorUtil::toImU32( Color::fromBytes( tileVisual._tintR, tileVisual._tintG, tileVisual._tintB ) );
                        break;
                    }
                    case PaintLayer::Warp:
                    {
                        const uint64 key = ( static_cast<uint64>( static_cast<uint32>( tileY ) ) << 32 ) | static_cast<uint32>( tileX );
                        color            = uniqueWarpCells.count( key ) ? EditorColorUtil::toImU32( TileMapPanelInternal::kWarpCell ) : EditorColorUtil::toImU32( TileMapPanelInternal::kNoWarpCell );
                        break;
                    }
                    case PaintLayer::Tile:
                    {
                        break; // 아래에서 셀마다 그린다(아틀라스 이미지, 없으면 브러시 색 + 규칙이 고른 셀 번호)
                    }
                    case PaintLayer::Flag:
                    {
                        const TileFlagLayerInfo& info = kArrTileFlagLayerInfo[static_cast<size_t>( _flagLayer )];
                        const uint32             rgb  = _map.getFlagLayer( _flagLayer )[tileIndex] != 0 ? info._onColorRgb : info._offColorRgb;
                        color                         = EditorColorUtil::toImU32( Color::fromArgb( TileMapPanelInternal::kOpaqueAlphaMask | rgb ) );
                        break;
                    }
                    case PaintLayer::Data:
                    {
                        // 기본값인 칸은 빈 칸 색, 기본값과 다른 칸은 데이터 색이다(값은 스포이트로 읽는다).
                        int32 value = 0;
                        if ( _map.getCellData( _dataLayerIndex, tileX, tileY, value ) && value != _map._listDataLayer[static_cast<size_t>( _dataLayerIndex )]._defaultValue )
                            color = EditorColorUtil::toImU32( EditorThemeUtil::getAccentColor() );
                        break;
                    }
                }

                const ImVec2 p0( origin.x + static_cast<float32>( tileX ) * cell, origin.y + static_cast<float32>( tileY ) * cell );
                const ImVec2 p1( p0.x + cell - 1.0f, p0.y + cell - 1.0f );
                pDl->AddRectFilled( p0, p1, color );
                if ( _layer == PaintLayer::Tile )
                    drawTileLayerCell( pDl, p0, p1, tileX, tileY, pAtlasTexture );
                pDl->AddRect( p0, p1, EditorColorUtil::toImU32( TileMapPanelInternal::kCellBorder ) );
            }
        }
    }

    void TileMapPanel::drawBrushPreview( ImDrawList* pDl, const ImVec2& origin, float32 cell, const int2& hoverCell, bool bHovered )
    {
        // 브러시 미리보기 — 커서 아래 셀, Rect 도구로 끄는 중이면 그 사각형(지우기면 빨강).
        const ImU32 previewColor = EditorColorUtil::toImU32( _bErase ? TileMapPanelInternal::kBrushPreviewErase : TileMapPanelInternal::kBrushPreview );
        if ( _bRectActive )
        {
            const float32 minX = static_cast<float32>( MathUtil::min( _rectStartCell._x, hoverCell._x ) );
            const float32 minY = static_cast<float32>( MathUtil::min( _rectStartCell._y, hoverCell._y ) );
            const float32 maxX = static_cast<float32>( MathUtil::max( _rectStartCell._x, hoverCell._x ) + 1 );
            const float32 maxY = static_cast<float32>( MathUtil::max( _rectStartCell._y, hoverCell._y ) + 1 );
            pDl->AddRect( ImVec2( origin.x + minX * cell, origin.y + minY * cell ), ImVec2( origin.x + maxX * cell, origin.y + maxY * cell ), previewColor, 0.0f, 0,
                          2.0f );
        }
        else if ( bHovered && isInBounds( hoverCell._x, hoverCell._y ) )
        {
            const ImVec2 p0( origin.x + static_cast<float32>( hoverCell._x ) * cell, origin.y + static_cast<float32>( hoverCell._y ) * cell );
            pDl->AddRect( p0, ImVec2( p0.x + cell, p0.y + cell ), previewColor, 0.0f, 0, 2.0f );
        }
    }
    void TileMapPanel::applyToolInput( const int2& hoverCell, bool bHovered )
    {
        const bool bClicked = bHovered && EditorInputGUI::readPointer().wasPressed( EditorMouseButton::Left );
        switch ( _tool )
        {
            case PaintTool::Brush:
            {
                const bool bPainting = ImGui::IsItemActive() && ImGui::IsMouseDown( ImGuiMouseButton_Left );
                if ( bPainting )
                {
                    // 한 프레임에 마우스가 여러 셀을 지나가도 끊기지 않게 지난 프레임에 칠한 셀에서 이번 셀까지 선분으로 칠한다.
                    TileMapPaintUtil::collectLineCells( _bStrokeActive ? _lastPaintCell : hoverCell, hoverCell, _listStrokeCell );
                    for ( const int2& strokeCell : _listStrokeCell )
                    {
                        paintCell( strokeCell._x, strokeCell._y );
                    }
                    _lastPaintCell = hoverCell;
                }
                _bStrokeActive = bPainting;
                break;
            }
            case PaintTool::Rect:
            {
                if ( bClicked )
                {
                    _rectStartCell = hoverCell;
                    _bRectActive   = true;
                }
                if ( _bRectActive && ImGui::IsMouseDown( ImGuiMouseButton_Left ) == false )
                {
                    _bRectActive = false;
                    TileMapPaintUtil::collectRectCells( _rectStartCell, hoverCell, _map._width, _map._height, _listStrokeCell );
                    for ( const int2& rectCell : _listStrokeCell )
                    {
                        paintCell( rectCell._x, rectCell._y );
                    }
                }
                break;
            }
            case PaintTool::Fill:
            {
                if ( bClicked && isInBounds( hoverCell._x, hoverCell._y ) )
                {
                    collectCellValues( _listCellValue );
                    TileMapPaintUtil::collectFloodFillCells( _listCellValue, _map._width, _map._height, hoverCell, _listStrokeCell );
                    for ( const int2& fillCell : _listStrokeCell )
                    {
                        paintCell( fillCell._x, fillCell._y );
                    }
                }
                break;
            }
            case PaintTool::Picker:
            {
                if ( bClicked && isInBounds( hoverCell._x, hoverCell._y ) )
                {
                    pickCell( hoverCell._x, hoverCell._y );
                    _tool = PaintTool::Brush; // 유니티 Tile Palette 처럼 집은 뒤에는 칠한다
                }
                break;
            }
        }
    }

    void TileMapPanel::collectCellValues( vector<uint64>& outListValue )
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

    void TileMapPanel::pickCell( int32 x, int32 y )
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

    void TileMapPanel::drawDataLayerControls()
    {
        if ( _layer == PaintLayer::Data )
        {
            ImGui::InputInt( "Value", &_paintDataValue );
            EditorSelfTestMarks::note( "tileMap.dataLayer.value" );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileMap", "DataValueTip" ) );
        }
        ImGui::Separator();
        ImGui::TextUnformatted( "Data Layers" );
        ImGui::InputText( "Name##dataLayer", _newDataLayerName.data(), _newDataLayerName.capacity() );
        EditorSelfTestMarks::note( "tileMap.dataLayer.name" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileMap", "DataLayerNameTip" ) );
        ImGui::InputInt( "Default##dataLayer", &_newDataLayerDefault );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileMap", "DataLayerDefaultTip" ) );
        const bool bAddDataLayer = ImGui::Button( "Add Data Layer" );
        EditorSelfTestMarks::note( "tileMap.dataLayer.add" );
        if ( bAddDataLayer )
        {
            // 빈 이름 · 겹치는 이름은 맵이 거절한다 — 읽기도 같은 규칙으로 거절하므로 저장할 수 없는 맵을 만들지 않는다.
            if ( _map.addDataLayer( _newDataLayerName.c_str(), _newDataLayerDefault ) )
            {
                _layer          = PaintLayer::Data;
                _dataLayerIndex = static_cast<int32>( _map._listDataLayer.size() ) - 1;
                _newDataLayerName.clear();
                notifyDocumentEdited( "Add Tile Map Data Layer", "tilemap-datalayer" );
            }
            else
            {
                _status = SW_LOCKEY( "EditorTileMap", "DataLayerRejectedStatus" );
            }
        }
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileMap", "AddDataLayerTip" ) );
    }

    uint32 TileMapPanel::countSelectedFlagCells() const
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

    uint32 TileMapPanel::countDataCells() const
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

    void TileMapPanel::shutdown( IRHIDevice* /*pRHIDevice*/ )
    {
        _atlasThumbnail.clear();
    }

    void TileMapPanel::drawTileMapFileControls()
    {
        EditorContext& editorContext = editor::getSubsystem<EditorContext>();

        const string& focused = editorContext.getSubsystem<EditorWorkspace>().getFocusedAssetPath();
        if ( focused.empty() == false )
            ImGui::TextDisabled( "Focused: %s", focused.c_str() );

        ImGui::InputText( "Path", _pathBuffer.data(), _pathBuffer.capacity() );
        EditorSelfTestMarks::note( "tileMap.path" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileMap", "PathTip" ) );
        ImGui::InputText( "Name", _nameBuffer.data(), _nameBuffer.capacity() );
        if ( ImGui::IsItemDeactivatedAfterEdit() )
            notifyDocumentEdited( "Edit Tile Map Name", "tilemap-name" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileMap", "NameTip" ) );
        ImGui::InputInt( "Width", &_inputWidth );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileMap", "WidthTip" ) );
        ImGui::SameLine();
        ImGui::InputInt( "Height", &_inputHeight );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileMap", "HeightTip" ) );
        if ( ImGui::Button( "Apply Size" ) )
        {
            resize( MathUtil::max( 1, _inputWidth ), MathUtil::max( 1, _inputHeight ) );
            notifyDocumentEdited( "Resize Tile Map" );
        }
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileMap", "ApplySizeTip" ) );

        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Load" ) );
        const bool bLoad = ImGui::Button( "Load" );
        EditorSelfTestMarks::note( "tileMap.load" );
        if ( bLoad )
        {
            // 적어 넣은 경로에 파일이 없으면 지금 맵을 그대로 둔다(새 문서로 표시하면 편집 중인 맵이 저장 안 한 채 깨끗해진다).
            if ( ResourceIO::hasResource( ResourceID{ _pathBuffer.c_str() } ) )
                reloadDocument();
            else
                SW_LOG_WARNING( "No tile map at '%#'", _pathBuffer.c_str() );
        }
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileMap", "LoadTip" ) );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Save" ) );
        const bool bSave = ImGui::Button( "Save" );
        EditorSelfTestMarks::note( "tileMap.save" );
        if ( bSave )
        {
            if ( saveXML( _pathBuffer.c_str() ) )
            {
                _status = SW_LOCKEYFORMAT( "EditorTileMap", "SavedStatus", TextArgumentList().addText( "path", _pathBuffer.c_str() ) );
                clearDocumentDirty();
            }
            else
                _status = SW_LOCKEY( "EditorTileMap", "SaveFailedStatus" );
        }
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileMap", "SaveTip" ) );
    }

    void TileMapPanel::drawLayerControls()
    {
        // 목록은 Visual · Warp · Tile 다음에 플래그 레이어 테이블 순서, 그 뒤에 맵의 데이터 레이어 순서다. 순번 ↔ 레이어는 getPaintLayerIndex / selectPaintLayer 가 정한다.
        vector<const utf8*> listLayerName = { "Visual", "Warp", "Tile" };
        for ( const TileFlagLayerInfo& info : kArrTileFlagLayerInfo )
        {
            listLayerName.push_back( info._pName );
        }
        for ( const TileMapXMLData::DataLayer& dataLayer : _map._listDataLayer )
        {
            listLayerName.push_back( dataLayer._name.c_str() );
        }
        int32 layerIndex = getPaintLayerIndex();
        if ( ImGui::Combo( "Layer", &layerIndex, listLayerName.data(), static_cast<int32>( listLayerName.size() ) ) )
            selectPaintLayer( layerIndex );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileMap", "LayerTip" ) );

        if ( _layer == PaintLayer::Visual )
        {
            ImGui::InputInt( "Height", &_paintHeight );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileMap", "PaintHeightTip" ) );
            ImGui::InputInt( "Atlas ID", &_atlasID );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileMap", "AtlasIdTip" ) );
            ImGui::ColorEdit3( "Tint", _arrTint );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileMap", "TintTip" ) );
        }
        else if ( _layer == PaintLayer::Warp )
        {
            ImGui::InputText( "Warp Target", _warpTarget.data(), _warpTarget.capacity() );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileMap", "WarpTargetTip" ) );
            ImGui::InputInt( "Target TX", &_warpTx );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileMap", "WarpTargetXTip" ) );
            ImGui::InputInt( "Target TY", &_warpTy );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileMap", "WarpTargetYTip" ) );
        }
        else if ( _layer == PaintLayer::Tile )
        {
            ImGui::InputText( "Tile Set", _tileSetBuffer.data(), _tileSetBuffer.capacity() );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileMap", "TileSetTip" ) );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Use" ) );
            if ( ImGui::Button( "Use" ) )
            {
                _map._tileSetPath = _tileSetBuffer.c_str();
                refreshTileSet();
                notifyDocumentEdited( "Set Tile Set", "tilemap-tileset" );
            }
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileMap", "UseTileSetTip" ) );
            drawTilePalette();
        }
        drawDataLayerControls();

        ImGui::Separator();
        ImGui::TextUnformatted( "Edge Warp Presets" );
        const utf8*                            edges[] = { "N", "E", "S", "W" };
        fixed_string<constant::kMaxBuffer128>* bufs[]  = { &_edgeTargetN, &_edgeTargetE, &_edgeTargetS, &_edgeTargetW };
        for ( int32 edgeIndex = 0; edgeIndex < 4; ++edgeIndex )
        {
            ImGui::PushID( edgeIndex );
            EditorChrome::setNextItemWidthColumns( 24.0f );
            ImGui::InputText( edges[edgeIndex], bufs[edgeIndex]->data(), bufs[edgeIndex]->capacity() );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileMap", "EdgeTargetTip" ) );
            ImGui::SameLine();
            EditorChrome::setNextItemWidthColumns( 7.0f );
            ImGui::InputInt( "##tx", &_arrEdgeTx[edgeIndex] );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileMap", "EdgeTargetXTip" ) );
            ImGui::SameLine();
            EditorChrome::setNextItemWidthColumns( 7.0f );
            ImGui::InputInt( "##ty", &_arrEdgeTy[edgeIndex] );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileMap", "EdgeTargetYTip" ) );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Apply" ) );
            if ( ImGui::Button( "Apply" ) )
                paintEdgeWarp( edgeIndex );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileMap", "ApplyEdgeTip" ) );
            ImGui::PopID();
        }

        ImGui::Separator();
    }

    void TileMapPanel::resize( int32 width, int32 height )
    {
        // 크기는 Width/Height 필드에서 그대로 온다. 상한과 그 이유는 `TileMapXMLData::kMaxTileCount` 에 있다.
        if ( _map.resetTiles( width, height ) == false )
        {
            _status = SW_LOCKEY( "EditorTileMap", "SizeOutOfRange" );
            SW_LOG_WARNING( "TileMap resize %#x%# is beyond the supported tile count (%#)",
                            width, height, TileMapXMLData::kMaxTileCount );
            return;
        }
        _inputWidth  = width;
        _inputHeight = height;
    }

    ToolAssetLoadResult TileMapPanel::loadDocument()
    {
        // 경로가 없으면 새 맵(기본 8x8)이다. 경로 필드는 첫 로드 때 열린 문서로, Load 단추 때는 적어 넣은 경로로 채워져 있다.
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
        // 새 맵에 고른 데이터 레이어가 없으면 플래그 레이어로 돌아간다(옛 번호로 다른 레이어를 칠하지 않는다).
        const bool bDataLayerGone = _layer == PaintLayer::Data && ( _dataLayerIndex < 0 || static_cast<size_t>( _dataLayerIndex ) >= _map._listDataLayer.size() );
        if ( bDataLayerGone )
        {
            _layer          = PaintLayer::Flag;
            _dataLayerIndex = 0;
        }
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
        if ( _map._tileSetPath.empty() == false && _tileSet.loadFromResource( ResourceID{ _map._tileSetPath } ) == false )
            _status = SW_LOCKEYFORMAT( "EditorTileMap", "TileSetReadFailed", TextArgumentList().addText( "path", _map._tileSetPath ) );
    }

    void TileMapPanel::drawTilePalette()
    {
        const vector<TileBrush>& listBrush = _tileSet.getBrushes();
        if ( listBrush.empty() )
        {
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EditorTileMap", "NoTileSet" ) );
            return;
        }
        // 브러시마다 아틀라스 이미지의 그 셀을 보인다(유니티 Tile Palette). 이미지를 아직 못 읽었으면 이름만.
        void*         pAtlasTexture = _tileSet.getAtlasPath().empty() ? nullptr : _atlasThumbnail.findTexture( _tileSet.getAtlasPath() );
        const float32 swatchSide    = 32.0f * EditorThemeUtil::getDpiScale();
        for ( int32 brushIndex = 0; brushIndex < static_cast<int32>( listBrush.size() ); ++brushIndex )
        {
            const TileBrush& brush = listBrush[static_cast<size_t>( brushIndex )];
            ImGui::PushID( brushIndex );
            if ( pAtlasTexture != nullptr && brush._defaultVisual._listFrameCell.empty() == false )
            {
                const float4 uvRect = _tileSet.computeCellUvRect( brush._defaultVisual._listFrameCell.front() );
                ImGui::Image( reinterpret_cast<ImTextureID>( pAtlasTexture ), ImVec2( swatchSide, swatchSide ), ImVec2( uvRect._x, uvRect._y ),
                              ImVec2( uvRect._x + uvRect._z, uvRect._y + uvRect._w ) );
                ImGui::SameLine();
            }
            const string label = string( brush._name.c_str() ) + ( brush.isRuleTile() ? " (rule)" : "" ) + ( brush._defaultVisual.isAnimated() ? " (animated)" : "" );
            if ( ImGui::RadioButton( label.c_str(), _brushIndex == brushIndex ) )
                _brushIndex = brushIndex;
            ImGui::PopID();
        }
    }

    void TileMapPanel::drawTileLayerCell( ImDrawList* pDrawList, const ImVec2& cellMin, const ImVec2& cellMax, int32 x, int32 y, void* pAtlasTexture ) const
    {
        const TileVisual* pVisual = _tileSet.resolveVisual( _listBrushIndex, _map._width, _map._height, x, y );
        if ( pVisual == nullptr )
            return;
        if ( pAtlasTexture != nullptr )
        {
            // 규칙이 고른 셀의 이미지 — 이웃을 칠하면 이미지가 따라 바뀌는 것이 규칙 타일이 일하는 모습이다.
            const float4 uvRect = _tileSet.computeCellUvRect( pVisual->computeCellAt( 0.0f ) );
            pDrawList->AddImage( reinterpret_cast<ImTextureID>( pAtlasTexture ), cellMin, cellMax, ImVec2( uvRect._x, uvRect._y ),
                                 ImVec2( uvRect._x + uvRect._z, uvRect._y + uvRect._w ) );
            return;
        }
        // 브러시마다 다른 색(이름 해시) + 규칙이 고른 아틀라스 셀 번호 — 이웃을 칠하면 번호가 바뀌는 것이 규칙 타일이 일하는 모습이다.
        const uint16 brushValue = _listBrushIndex[indexOf( x, y )];
        const uint32 hash       = static_cast<uint32>( _tileSet.getBrushes()[brushValue - 1u]._name.getHash() );
        const ImU32  color      = EditorColorUtil::toImU32( Color::fromBytes( static_cast<uint8>( 80 + ( hash & 0x7Fu ) ), static_cast<uint8>( 80 + ( ( hash >> 8 ) & 0x7Fu ) ), static_cast<uint8>( 80 + ( ( hash >> 16 ) & 0x7Fu ) ) ) );
        pDrawList->AddRectFilled( cellMin, cellMax, color );
        const string number = to_string( pVisual->computeCellAt( 0.0f ) );
        pDrawList->AddText( ImVec2( cellMin.x + 2.0f, cellMin.y + 1.0f ), EditorColorUtil::toImU32( TileMapPanelInternal::kCellNumberText ), number.c_str() );
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
                    return;
                // 지우기는 그 레이어의 기본값으로 되돌린다(0 이 아니다 — 기본값은 맵 데이터가 정한다).
                const int32 value = _bErase ? _map._listDataLayer[static_cast<size_t>( _dataLayerIndex )]._defaultValue : _paintDataValue;
                int32       current{ 0 };
                if ( _map.getCellData( _dataLayerIndex, x, y, current ) && current == value )
                    return; // 끌어 칠할 때 같은 셀을 프레임마다 되돌리기 기록에 넣지 않는다
                (void)_map.setCellData( _dataLayerIndex, x, y, value );
                break;
            }
            case PaintLayer::Tile:
            {
                const vector<TileBrush>& listBrush = _tileSet.getBrushes();
                if ( _bErase == false && ( _brushIndex < 0 || _brushIndex >= static_cast<int32>( listBrush.size() ) ) )
                    return;
                const string_view brushName = _bErase ? string_view{} : string_view( listBrush[static_cast<size_t>( _brushIndex )]._name.c_str() );
                if ( _map.getTileBrushName( x, y ) == brushName )
                    return;                                 // 끌어 칠할 때 같은 셀을 프레임마다 되돌리기 기록에 넣지 않는다
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
        notifyDocumentEdited( "Paint Tile Map", "tilemap-paint" );
    }

    void TileMapPanel::paintEdgeWarp( int32 edge )
    {
        const fixed_string<constant::kMaxBuffer128>* targets[]          = { &_edgeTargetN, &_edgeTargetE, &_edgeTargetS, &_edgeTargetW };
        const bool                                   bInvalidEdgeTarget = edge < 0 || edge > 3 || targets[edge]->empty();
        if ( bInvalidEdgeTarget )
            return;

        auto stamp = [&]( int32 tileX, int32 tileY )
        {
            _map.getFlagLayer( TileFlagLayer::Walkable )[indexOf( tileX, tileY )] = 1;
            VectorUtil::removeIf( _map._listWarp, [tileX, tileY]( const TileMapXMLData::Warp& warp )
            { return warp._tileX == tileX && warp._tileY == tileY; } );
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
        if ( _layer == PaintLayer::Data )
            return kFixedPaintLayerCount + static_cast<int32>( kTileFlagLayerCount ) + _dataLayerIndex;
        return static_cast<int32>( _layer );
    }

    void TileMapPanel::selectPaintLayer( int32 layerIndex )
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
