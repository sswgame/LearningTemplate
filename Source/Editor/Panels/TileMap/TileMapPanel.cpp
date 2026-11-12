#include "pch.h"

#include "Editor/Panels/TileMap/TileMapPanel.h"

#include "Core/Container/unordered_set.h"
#include "Core/Math/MathUtil.h"

#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct TileMapPanelInternal
        {
            /** @brief 플래그 층의 `0xRRGGBB` 색을 불투명 `0xAARRGGBB` 로 만드는 알파 바이트입니다. */
            static constexpr uint32 kOpaqueAlphaMask = 0xFF000000u;

            [[nodiscard]] static bool readFlagCells( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const EditorPanelManager* pPanels = EditorPanelManager::findInEditorContext();
                if ( pPanels == nullptr )
                    return false;
                const TileMapPanel* pPanel = pPanels->findPanel<TileMapPanel>();
                if ( pPanel == nullptr )
                    return false;
                outValue = static_cast<float64>( pPanel->countSelectedFlagCells() );
                return true;
            }

            [[nodiscard]] static bool readDataLayerCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const EditorPanelManager* pPanels = EditorPanelManager::findInEditorContext();
                if ( pPanels == nullptr )
                    return false;
                const TileMapPanel* pPanel = pPanels->findPanel<TileMapPanel>();
                if ( pPanel == nullptr )
                    return false;
                outValue = static_cast<float64>( pPanel->getDataLayerCount() );
                return true;
            }

            [[nodiscard]] static bool readDataCells( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const EditorPanelManager* pPanels = EditorPanelManager::findInEditorContext();
                if ( pPanels == nullptr )
                    return false;
                const TileMapPanel* pPanel = pPanels->findPanel<TileMapPanel>();
                if ( pPanel == nullptr )
                    return false;
                outValue = static_cast<float64>( pPanel->countDataCells() );
                return true;
            }

            [[nodiscard]] static bool readPaintTool( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const EditorPanelManager* pPanels = EditorPanelManager::findInEditorContext();
                if ( pPanels == nullptr )
                    return false;
                const TileMapPanel* pPanel = pPanels->findPanel<TileMapPanel>();
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
    SW_EDITOR_PANEL( TileMapPanel, TileMapPanel::kPanelID, EditorPanelCategory::Tool, 1500 );
    SW_AUTOMATION_WATCH( editorTileMapFlagCells, "Editor.TileMapFlagCells", "Cells set in the Tile Map Tool's selected flag layer (0 on other layers)",
                         &TileMapPanelInternal::readFlagCells );
    SW_AUTOMATION_WATCH( editorTileMapDataLayers, "Editor.TileMapDataLayers", "Data layers of the Tile Map Tool's map", &TileMapPanelInternal::readDataLayerCount );
    SW_AUTOMATION_WATCH( editorTileMapDataCells, "Editor.TileMapDataCells", "Cells that differ from the layer default, summed over all data layers of the Tile Map Tool's map",
                         &TileMapPanelInternal::readDataCells );
    SW_AUTOMATION_WATCH( editorTileMapTool, "Editor.TileMapTool", "Tile Map Tool paint tool: 0 Brush, 1 Rect, 2 Fill, 3 Picker, 4 Line", &TileMapPanelInternal::readPaintTool );

    TileMapPanel::TileMapPanel()
        : EditorDocumentPanel{ EditorAssetType{ TileMapXMLData::kAssetType }, false }
        , _document{}
        , _atlasThumbnail{}
        , _canvasView{}
        , _hoverText{}
        , _newDataLayerName{}
        , _newDataLayerDefault{ 0 }
    {
    }

    void TileMapPanel::drawDocumentContent()
    {
        updateFocusedDocument();
        if ( isDocumentLoaded() == false )
            _document.editPath() = getLoadedAssetPath().c_str();
        ensureDocumentLoaded();
        drawDocumentOpenBar( "tileMap" );
        _atlasThumbnail.update();

        // 왼쪽은 파일 · 레이어 · 팔레트, 오른쪽은 캔버스다(유니티 Tile Palette 와 씬 뷰를 한 창에).
        const float32 statusHeight = ImGui::GetFrameHeightWithSpacing();
        const float32 sideWidth    = EditorChrome::measureTextExtent( EditorTextExtent::ofText( 40.0f, 0.0f ) ).x;
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

        EditorWidgets::drawPanelStatus( _document.getStatus().c_str() );
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
            {  "Line",   "tileMap.tool.line", SW_LOCKEY( "EditorTileMap",   "LineTip" ),   PaintTool::Line},
        };
        for ( const ToolButton& button : arrToolButton )
        {
            if ( &button != arrToolButton )
                EditorChrome::sameLineIfFits( EditorChrome::measureCheckbox( button._pLabel ) );
            if ( ImGui::RadioButton( button._pLabel, _document.getTool() == button._tool ) )
                _document.setTool( button._tool );
            EditorSelfTestMarks::record( button._pMark );
            EditorWidgets::drawTooltip( button._pTooltip );
        }
        EditorChrome::sameLineIfFits( EditorChrome::measureCheckbox( SW_LOCKEY( "EdLabel", "Erase" ) ) );
        ImGui::Checkbox( SW_LOCKEY( "EdLabel", "Erase" ), &_document.editErase() );
        EditorSelfTestMarks::record( "tileMap.erase" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileMap", "EraseTip" ) );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "ResetView" ) ) );
        if ( ImGui::SmallButton( SW_LOCKEY( "EdText", "ResetView" ) ) )
            _canvasView.reset();
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileMap", "ResetViewTip" ) );
        ImGui::SameLine();
        ImGui::TextDisabled( "%dx%d  %.0f%%  %s", _document.getMap()._width, _document.getMap()._height, static_cast<float64>( _canvasView.getZoom() * 100.0f ), _hoverText.c_str() );

        // 유니티 Tile Palette 의 단축키 — `Editor.Panel.tile_map` 레이어의 액션(이 창이 포커스이고 글을 치는 중이 아닐 때만 발화한다).
        if ( EditorInputGUI::wasActionTriggered( "tileMap.brush" ) )
            _document.setTool( PaintTool::Brush );
        if ( EditorInputGUI::wasActionTriggered( "tileMap.rect" ) )
            _document.setTool( PaintTool::Rect );
        if ( EditorInputGUI::wasActionTriggered( "tileMap.fill" ) )
            _document.setTool( PaintTool::Fill );
        if ( EditorInputGUI::wasActionTriggered( "tileMap.picker" ) )
            _document.setTool( PaintTool::Picker );
        if ( EditorInputGUI::wasActionTriggered( "tileMap.line" ) )
            _document.setTool( PaintTool::Line );
    }

    void TileMapPanel::drawCanvas()
    {
        const ImVec2 canvasMin  = ImGui::GetCursorScreenPos();
        ImVec2       canvasSize = ImGui::GetContentRegionAvail();
        canvasSize.x            = MathUtil::max( canvasSize.x, 64.0f );
        canvasSize.y            = MathUtil::max( canvasSize.y, 64.0f );
        ImGui::InvisibleButton( "##tilegrid", canvasSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle | ImGuiButtonFlags_MouseButtonRight );
        EditorSelfTestMarks::record( "tileMap.canvas" );
        const bool bHovered = ImGui::IsItemHovered();
        const bool bActive  = ImGui::IsItemActive();

        updateCanvasView( canvasMin, bHovered, bActive );

        const float32 cell = _canvasView.computeCellSize( EditorThemeUtil::getDPIScale() );
        const float2& pan  = _canvasView.getPan();
        const ImVec2  origin( canvasMin.x + pan._x, canvasMin.y + pan._y );
        const ImVec2  canvasMax( canvasMin.x + canvasSize.x, canvasMin.y + canvasSize.y );
        ImDrawList*   pDl = ImGui::GetWindowDrawList();
        pDl->PushClipRect( canvasMin, canvasMax, true );
        pDl->AddRectFilled( canvasMin, canvasMax, EditorColorUtil::toImU32( SW_THEME_COLOR( "Base", "DockingEmptyBg" ) ) );

        const TileMapXMLData& map = _document.getMap();
        drawGridCells( pDl, origin, cell, TileMapPaintUtil::computeVisibleCells( pan, float2{ canvasSize.x, canvasSize.y }, cell, map._width, map._height ) );

        // 스폰 자리 — 어느 층을 칠하든 보인다(유니티 씬 뷰의 플레이어 시작 표시).
        const ImVec2 spawnMin( origin.x + static_cast<float32>( map._spawnX ) * cell, origin.y + static_cast<float32>( map._spawnY ) * cell );
        pDl->AddRect( spawnMin, ImVec2( spawnMin.x + cell, spawnMin.y + cell ), EditorColorUtil::toImU32( SW_THEME_COLOR( "Viewport", "Marker" ) ), 0.0f, 0, 2.0f );

        const int2 hoverCell = TileMapPaintUtil::findCellAt( EditorInputGUI::readPointer()._position._x - origin.x, EditorInputGUI::readPointer()._position._y - origin.y, cell );
        drawBrushPreview( pDl, origin, cell, hoverCell, bHovered );
        pDl->PopClipRect();
        _hoverText = bHovered ? _document.describeCell( hoverCell._x, hoverCell._y ) : string{};

        applyToolInput( hoverCell, bHovered );
    }

    void TileMapPanel::updateCanvasView( const ImVec2& canvasMin, bool bHovered, bool bActive )
    {
        // 휠은 커서 아래 위치를 붙든 채 확대하고, 가운데 · 오른쪽 끌기는 이동한다(유니티 씬 뷰 2D).
        const EditorPointerState pointer = EditorInputGUI::readPointer();
        if ( bHovered && pointer._wheel != 0.0f )
            _canvasView.applyWheel( pointer._wheel, float2{ pointer._position._x - canvasMin.x, pointer._position._y - canvasMin.y } );
        // 화면 이동은 끌기 문턱(입력 맵의 `dragThreshold`)을 넘은 뒤부터다 — 문턱 0 이면 오른쪽 클릭(메뉴)도 한 픽셀 끌기가 됐다(R1).
        if ( bActive && ( pointer.isDragging( EditorMouseButton::Middle ) || pointer.isDragging( EditorMouseButton::Right ) ) )
            _canvasView.applyDrag( float2{ pointer._delta._x, pointer._delta._y } );
    }

    void TileMapPanel::drawGridCells( ImDrawList* pDl, const ImVec2& origin, float32 cell, const TileCellRange& range )
    {
        const ImU32           emptyColor  = EditorColorUtil::toImU32( SW_THEME_COLOR( "Base", "FrameBg" ) );
        const ImU32           dataColor   = EditorColorUtil::toImU32( SW_THEME_COLOR( "Base", "Accent" ) ); // 강조색은 데이터에 쓴다
        const ImU32           borderColor = EditorColorUtil::toImU32( SW_THEME_COLOR( "Base", "Border" ) );
        const TileMapXMLData& map         = _document.getMap();
        const PaintLayer      layer       = _document.getPaintLayer();
        const TileFlagLayer   flagLayer   = _document.getFlagLayer();
        void*                 pAtlasTexture{ nullptr };
        if ( layer == PaintLayer::Tile )
        {
            (void)_document.refreshBrushIndices();
            const TileSetAsset& tileSet = _document.getTileSet();
            if ( tileSet.getAtlasPath().empty() == false )
                pAtlasTexture = _atlasThumbnail.findTexture( tileSet.getAtlasPath() );
        }

        unordered_set<uint64> uniqueWarpCells;
        if ( layer == PaintLayer::Warp )
        {
            uniqueWarpCells.reserve( map._listWarp.size() );
            for ( const TileMapXMLData::Warp& warp : map._listWarp )
            {
                uniqueWarpCells.insert( ( static_cast<uint64>( static_cast<uint32>( warp._tileY ) ) << 32 ) |
                                        static_cast<uint32>( warp._tileX ) );
            }
        }

        for ( int32 tileY = range._min._y; tileY < range._max._y; ++tileY )
        {
            for ( int32 tileX = range._min._x; tileX < range._max._x; ++tileX )
            {
                const size_t tileIndex = _document.getIndexOf( tileX, tileY );
                ImU32        color     = emptyColor;
                switch ( layer )
                {
                    case PaintLayer::Visual:
                    {
                        const TileMapXMLData::Visual& tileVisual = map._listVisual[tileIndex];
                        color                                    = EditorColorUtil::toImU32( Color::fromBytes( tileVisual._tintR, tileVisual._tintG, tileVisual._tintB ) );
                        break;
                    }
                    case PaintLayer::Warp:
                    {
                        const uint64 key = ( static_cast<uint64>( static_cast<uint32>( tileY ) ) << 32 ) | static_cast<uint32>( tileX );
                        color            = uniqueWarpCells.count( key ) ? dataColor : emptyColor;
                        break;
                    }
                    case PaintLayer::Tile:
                    {
                        break; // 아래에서 셀마다 그린다(아틀라스 이미지, 없으면 브러시 색 + 규칙이 고른 셀 번호)
                    }
                    case PaintLayer::Flag:
                    {
                        const TileFlagLayerInfo& info = kArrTileFlagLayerInfo[static_cast<size_t>( flagLayer )];
                        const uint32             rgb  = map.getFlagLayer( flagLayer )[tileIndex] != 0 ? info._onColorRGB : info._offColorRGB;
                        color                         = EditorColorUtil::toImU32( Color::fromArgb( TileMapPanelInternal::kOpaqueAlphaMask | rgb ) );
                        break;
                    }
                    case PaintLayer::Data:
                    {
                        // 기본값인 칸은 빈 칸 색, 기본값과 다른 칸은 데이터 색이다(값은 스포이트로 읽는다).
                        const int32 dataLayerIndex = _document.getDataLayerIndex();
                        int32       value          = 0;
                        if ( map.getCellData( dataLayerIndex, tileX, tileY, value ) && value != map._listDataLayer[static_cast<size_t>( dataLayerIndex )]._defaultValue )
                            color = dataColor;
                        break;
                    }
                }

                const ImVec2 p0( origin.x + static_cast<float32>( tileX ) * cell, origin.y + static_cast<float32>( tileY ) * cell );
                const ImVec2 p1( p0.x + cell - 1.0f, p0.y + cell - 1.0f );
                pDl->AddRectFilled( p0, p1, color );
                if ( layer == PaintLayer::Tile )
                    drawTileLayerCell( pDl, p0, p1, tileX, tileY, pAtlasTexture );
                pDl->AddRect( p0, p1, borderColor );
            }
        }
    }

    void TileMapPanel::drawBrushPreview( ImDrawList* pDl, const ImVec2& origin, float32 cell, const int2& hoverCell, bool bHovered )
    {
        // 브러시 미리보기 — 커서 아래 셀, Rect · Line 도구로 끄는 중이면 그 모양(지우기면 빨강).
        const ImU32 previewColor = EditorColorUtil::toImU32( _document.isErase() ? SW_THEME_COLOR( "Signal", "Error" ) : SW_THEME_COLOR( "Base", "Highlight" ) );
        int2        dragStartCell{};
        const bool  bDragging = _document.readDragStart( dragStartCell );
        if ( bDragging && _document.getTool() == PaintTool::Line )
        {
            vector<int2> listLineCell;
            TileMapPaintUtil::collectLineCells( dragStartCell, hoverCell, listLineCell );
            for ( const int2& lineCell : listLineCell )
            {
                const ImVec2 p0( origin.x + static_cast<float32>( lineCell._x ) * cell, origin.y + static_cast<float32>( lineCell._y ) * cell );
                pDl->AddRect( p0, ImVec2( p0.x + cell, p0.y + cell ), previewColor, 0.0f, 0, 2.0f );
            }
        }
        else if ( bDragging )
        {
            const float32 minX = static_cast<float32>( MathUtil::min( dragStartCell._x, hoverCell._x ) );
            const float32 minY = static_cast<float32>( MathUtil::min( dragStartCell._y, hoverCell._y ) );
            const float32 maxX = static_cast<float32>( MathUtil::max( dragStartCell._x, hoverCell._x ) + 1 );
            const float32 maxY = static_cast<float32>( MathUtil::max( dragStartCell._y, hoverCell._y ) + 1 );
            pDl->AddRect( ImVec2( origin.x + minX * cell, origin.y + minY * cell ), ImVec2( origin.x + maxX * cell, origin.y + maxY * cell ), previewColor, 0.0f, 0,
                          2.0f );
        }
        else if ( bHovered && _document.isInBounds( hoverCell._x, hoverCell._y ) )
        {
            const ImVec2 p0( origin.x + static_cast<float32>( hoverCell._x ) * cell, origin.y + static_cast<float32>( hoverCell._y ) * cell );
            pDl->AddRect( p0, ImVec2( p0.x + cell, p0.y + cell ), previewColor, 0.0f, 0, 2.0f );
        }
    }

    void TileMapPanel::applyToolInput( const int2& hoverCell, bool bHovered )
    {
        TileMapToolInput input{};
        input._hoverCell   = hoverCell;
        input._bClicked    = bHovered && EditorInputGUI::readPointer().wasPressed( EditorMouseButton::Left );
        input._bButtonDown = ImGui::IsMouseDown( ImGuiMouseButton_Left );
        input._bPainting   = ImGui::IsItemActive() && input._bButtonDown;
        // 획 하나(캔버스를 누른 동안)는 상호작용 구간 하나라 되돌리기 한 단계다 — 다음 획은 새 구간이다(감사 P0-1).
        if ( _document.applyToolInput( input ) )
            notifyDocumentEdited( "Paint Tile Map" );
    }

    void TileMapPanel::drawDataLayerControls()
    {
        if ( _document.getPaintLayer() == PaintLayer::Data )
        {
            EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdField", "Value" ) );
            EditorWidgets::inputInt( SW_WIDGET_ID( "##Value" ), &_document.editPaintDataValue(), 1, EditorWidgets::kImGuiDefaultIntStepFast, 0, "tileMap.dataLayer.value" );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileMap", "DataValueTip" ) );
            const bool bRemoveDataLayer = ImGui::Button( SW_LOCKEY( "EdVerb", "Remove" ) );
            EditorSelfTestMarks::record( "tileMap.dataLayer.remove" );
            if ( bRemoveDataLayer && _document.removeSelectedDataLayer() )
                notifyDocumentEdited( "Remove Tile Map Data Layer" );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileMap", "RemoveDataLayerTip" ) );
        }
        ImGui::Separator();
        ImGui::TextUnformatted( SW_LOCKEY( "EdText", "DataLayers" ) );
        EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdField", "NameAlt" ) );
        EditorWidgets::inputText( SW_WIDGET_ID( "##dataLayerName" ), _newDataLayerName.data(), _newDataLayerName.capacity() );
        EditorSelfTestMarks::record( "tileMap.dataLayer.name" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileMap", "DataLayerNameTip" ) );
        EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdField", "Default" ) );
        EditorWidgets::inputInt( SW_WIDGET_ID( "##dataLayerDefault" ), &_newDataLayerDefault );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileMap", "DataLayerDefaultTip" ) );
        const bool bAddDataLayer = ImGui::Button( SW_LOCKEY( "EdText", "AddDataLayer" ) );
        EditorSelfTestMarks::record( "tileMap.dataLayer.add" );
        if ( bAddDataLayer && _document.addDataLayer( _newDataLayerName.c_str(), _newDataLayerDefault ) )
        {
            _newDataLayerName.clear();
            notifyDocumentEdited( "Add Tile Map Data Layer" );
        }
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileMap", "AddDataLayerTip" ) );
    }

    void TileMapPanel::shutdown()
    {
        _atlasThumbnail.clear();
    }

    void TileMapPanel::drawTileMapFileControls()
    {
        const string& focused = TileMapModel::getFocusedAssetPath();
        if ( focused.empty() == false )
            ImGui::TextDisabled( "%s", SW_LOCKEYFORMAT( "EdFormat", "Focused", TextArgumentList().addText( "path", focused ) ).c_str() );

        fixed_string<constant::buffer::kMax256>& path = _document.editPath();
        EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdField", "Path" ) );
        (void)EditorWidgets::drawAssetPathField( "##Path", path, "tile map", ".tilemap.xml", "tileMap.path" ); // 콘텐츠 브라우저에서 끌어 놓을 수 있다
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileMap", "PathTip" ) );
        fixed_string<constant::buffer::kMax128>& name = _document.editName();
        EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdField", "NameAlt" ) );
        EditorWidgets::inputText( SW_WIDGET_ID( "##Name" ), name.data(), name.capacity() );
        if ( ImGui::IsItemDeactivatedAfterEdit() )
            notifyDocumentEdited( "Edit Tile Map Name" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileMap", "NameTip" ) );
        // 이름표는 왼쪽 열(인스펙터와 같다) — ImGui 기본(오른쪽 이름표)은 좁은 왼쪽 칸에서 Height · 가장자리 칸을 잘랐다(uiaudit 21).
        EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdField", "Width" ) );
        EditorWidgets::inputInt( SW_WIDGET_ID( "##Width" ), &_document.editInputWidth() );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileMap", "WidthTip" ) );
        EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdField", "Height" ) );
        EditorWidgets::inputInt( SW_WIDGET_ID( "##Height" ), &_document.editInputHeight() );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileMap", "HeightTip" ) );
        if ( ImGui::Button( SW_LOCKEY( "EdText", "ApplySize" ) ) && _document.applyInputSize() )
            notifyDocumentEdited( "Resize Tile Map" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileMap", "ApplySizeTip" ) );
        EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdField", "SpawnX" ) );
        EditorWidgets::inputInt( SW_WIDGET_ID( "##SpawnX" ), &_document.editInputSpawnX() );
        if ( ImGui::IsItemDeactivatedAfterEdit() && _document.applyInputSpawn() )
            notifyDocumentEdited( "Set Tile Map Spawn" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileMap", "SpawnXTip" ) );
        EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdField", "SpawnY" ) );
        EditorWidgets::inputInt( SW_WIDGET_ID( "##SpawnY" ), &_document.editInputSpawnY() );
        if ( ImGui::IsItemDeactivatedAfterEdit() && _document.applyInputSpawn() )
            notifyDocumentEdited( "Set Tile Map Spawn" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileMap", "SpawnYTip" ) );

        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdVerb", "Load" ) ) );
        const bool bLoad = ImGui::Button( SW_LOCKEY( "EdVerb", "Load" ) );
        EditorSelfTestMarks::record( "tileMap.load" );
        if ( bLoad )
        {
            // 적어 넣은 경로에 파일이 없으면 지금 맵을 그대로 둔다(새 문서로 표시하면 편집 중인 맵이 저장 안 한 채 깨끗해진다).
            // 읽기(`EditorToolAssetCommands::loadTileMap`)와 같은 해석으로 묻는다 — 입력란은 리소스 경로와 디스크 경로를 모두 받는다.
            // 다른 문서는 열기 경로로 넘긴다 — 미저장 확인 · 최근 목록 · 되돌리기의 문서 경로가 이 파일을 가리킨다(곧바로 읽으면 열린 문서는 옛 경로로 남았다).
            if ( TileMapModel::hasDocumentFile( path.c_str() ) == false )
                SW_LOG_WARNING_ID( "TileMapPanel.NoTileMap", "No tile map at '%#'", path.c_str() );
            else if ( string_view( path.c_str() ) == string_view( getLoadedAssetPath() ) )
                reloadDocument();
            else
                requestOpenDocument( path.c_str() );
        }
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileMap", "LoadTip" ) );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdVerb", "Save" ) ) );
        const bool bSave = ImGui::Button( SW_LOCKEY( "EdVerb", "Save" ) );
        EditorSelfTestMarks::record( "tileMap.save" );
        // 저장 경로 하나를 지난다 — 단추만 따로 쓰면 못 읽은 문서(Malformed)를 덮어쓰고 핫 리로드를 건너뛰었다.
        if ( bSave )
            (void)saveDocumentAndClearDirty(); // 실패하면 dirty 가 남고 이유는 상태 줄에 있다
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileMap", "SaveTip" ) );
    }

    void TileMapPanel::drawLayerControls()
    {
        vector<const utf8*> listLayerName;
        _document.collectPaintLayerNames( listLayerName );
        int32 layerIndex = _document.getPaintLayerIndex();
        EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdField", "Layer" ) );
        if ( EditorWidgets::combo( SW_WIDGET_ID( "##Layer" ), &layerIndex, listLayerName.data(), static_cast<int32>( listLayerName.size() ) ) )
            _document.selectPaintLayer( layerIndex );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileMap", "LayerTip" ) );

        const PaintLayer layer = _document.getPaintLayer();
        if ( layer == PaintLayer::Visual )
        {
            EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdField", "Height" ) );
            EditorWidgets::inputInt( SW_WIDGET_ID( "##PaintHeight" ), &_document.editPaintHeight() );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileMap", "PaintHeightTip" ) );
            EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdField", "AtlasID" ) );
            EditorWidgets::inputInt( SW_WIDGET_ID( "##AtlasID" ), &_document.editAtlasID() );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileMap", "AtlasIDTip" ) );
            EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdField", "Tint" ) );
            EditorWidgets::colorEdit3( SW_WIDGET_ID( "##Tint" ), _document.editTint() );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileMap", "TintTip" ) );
        }
        else if ( layer == PaintLayer::Warp )
        {
            fixed_string<constant::buffer::kMax128>& warpTarget = _document.editWarpTarget();
            EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdField", "WarpTarget" ) );
            (void)EditorWidgets::drawAssetPathField( "##WarpTarget", warpTarget, "tile map", ".tilemap.xml", "tileMap.warpTarget" ); // 콘텐츠 브라우저에서 끌어 놓을 수 있다. 비우면 같은 맵이다
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileMap", "WarpTargetTip" ) );
            EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdField", "TargetX" ) );
            EditorWidgets::inputInt( SW_WIDGET_ID( "##WarpTileX" ), &_document.editWarpTileX() );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileMap", "WarpTargetXTip" ) );
            EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdField", "TargetY" ) );
            EditorWidgets::inputInt( SW_WIDGET_ID( "##WarpTileY" ), &_document.editWarpTileY() );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileMap", "WarpTargetYTip" ) );
        }
        else if ( layer == PaintLayer::Tile )
        {
            fixed_string<constant::buffer::kMax256>& tileSetInput = _document.editTileSetInput();
            EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdField", "TileSet" ) );
            ImGui::SetNextItemWidth( ImGui::GetContentRegionAvail().x - EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "Use" ) ) - ImGui::GetStyle().ItemSpacing.x );
            (void)EditorWidgets::drawAssetPathField( "##TileSet", tileSetInput, "tile set", ".tileset.xml", "tileMap.tileSet" ); // 콘텐츠 브라우저에서 끌어 놓을 수 있다
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileMap", "TileSetTip" ) );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "Use" ) ) );
            if ( ImGui::Button( SW_LOCKEY( "EdText", "Use" ) ) && _document.useTileSetInput() )
                notifyDocumentEdited( "Set Tile Set" );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileMap", "UseTileSetTip" ) );
            drawTilePalette();
        }
        drawDataLayerControls();

        ImGui::Separator();
        ImGui::TextUnformatted( SW_LOCKEY( "EdText", "EdgeWarpPresets" ) );
        const utf8* edges[] = { "N", "E", "S", "W" };
        for ( int32 edgeIndex = 0; edgeIndex < TileMapModel::kEdgeCount; ++edgeIndex )
        {
            TileMapModel::EdgeWarpPreset& preset = _document.editEdgePreset( edgeIndex );
            ImGui::PushID( edgeIndex );
            // 한 줄: 가장자리 이름 · 대상 맵(남은 폭) · 칸 X · 칸 Y(단추 없는 숫자 칸) · 적용 — -/+ 단추가 붙은 숫자 칸 둘이 좁은 칸에서 몇 픽셀로 눌렸다.
            const ImGuiStyle& style       = ImGui::GetStyle();
            const float32     numberWidth = EditorChrome::measureColumns( 4.0f );
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled( "%s", edges[edgeIndex] );
            ImGui::SameLine( EditorChrome::measureColumns( 2.0f ) + ImGui::GetStyle().ItemSpacing.x ); // 글자 폭(N · W)과 무관하게 칸 시작을 맞춘다
            ImGui::SetNextItemWidth( ImGui::GetContentRegionAvail().x - 2.0f * ( numberWidth + style.ItemSpacing.x ) - EditorChrome::measureSmallButton( SW_LOCKEY( "EdVerb", "Apply" ) ) - style.ItemSpacing.x );
            EditorWidgets::inputText( SW_WIDGET_ID( "##target" ), preset._target.data(), preset._target.capacity() );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileMap", "EdgeTargetTip" ) );
            ImGui::SameLine();
            ImGui::SetNextItemWidth( numberWidth );
            EditorWidgets::inputInt( SW_WIDGET_ID( "##tx" ), &preset._tileX, 0, 0 );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileMap", "EdgeTargetXTip" ) );
            ImGui::SameLine();
            ImGui::SetNextItemWidth( numberWidth );
            EditorWidgets::inputInt( SW_WIDGET_ID( "##ty" ), &preset._tileY, 0, 0 );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileMap", "EdgeTargetYTip" ) );
            ImGui::SameLine();
            if ( ImGui::Button( SW_LOCKEY( "EdVerb", "Apply" ) ) && _document.paintEdgeWarp( edgeIndex ) )
                notifyDocumentEdited( "Paint Tile Map Edge" );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileMap", "ApplyEdgeTip" ) );
            ImGui::PopID();
        }

        ImGui::Separator();
    }

    bool TileMapPanel::saveDocument()
    {
        if ( _document.saveDocument() == false )
            return false;
        syncDocumentUndoBaseline();
        // 다른 경로로 저장했으면 그 파일이 열린 문서다 — 핫 리로드 · 최근 목록 · 되돌리기의 문서 경로가 옛 파일을 가리키지 않게 한다.
        if ( string_view( _document.getPath() ) != string_view( getLoadedAssetPath() ) )
            requestOpenDocument( _document.getPath() );
        return true;
    }

    void TileMapPanel::collectFilesToSave( vector<string>& outListPath ) const
    {
        appendFileToSave( _document.getPath(), outListPath );
    }

    void TileMapPanel::drawTilePalette()
    {
        const TileSetAsset&      tileSet   = _document.getTileSet();
        const vector<TileBrush>& listBrush = tileSet.getBrushes();
        if ( listBrush.empty() )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorTileMap", "NoTileSet" ) );
            return;
        }
        // 브러시마다 아틀라스 이미지의 그 셀을 보인다(유니티 Tile Palette). 이미지를 아직 못 읽었으면 이름만.
        void*         pAtlasTexture   = tileSet.getAtlasPath().empty() ? nullptr : _atlasThumbnail.findTexture( tileSet.getAtlasPath() );
        const float32 swatchSide      = 32.0f * EditorThemeUtil::getDPIScale();
        int32&        selectedBrushID = _document.editBrushIndex();
        for ( int32 brushIndex = 0; brushIndex < static_cast<int32>( listBrush.size() ); ++brushIndex )
        {
            const TileBrush& brush = listBrush[static_cast<size_t>( brushIndex )];
            ImGui::PushID( brushIndex );
            if ( pAtlasTexture != nullptr && brush._defaultVisual._listFrameCell.empty() == false )
            {
                const float4 uvRect = tileSet.computeCellUVRect( brush._defaultVisual._listFrameCell.front() );
                ImGui::Image( reinterpret_cast<ImTextureID>( pAtlasTexture ), ImVec2( swatchSide, swatchSide ), ImVec2( uvRect._x, uvRect._y ),
                              ImVec2( uvRect._x + uvRect._z, uvRect._y + uvRect._w ) );
                ImGui::SameLine();
            }
            const string label = string( brush._name.c_str() ) + ( brush.isRuleTile() ? " (rule)" : "" ) + ( brush._defaultVisual.isAnimated() ? " (animated)" : "" );
            if ( ImGui::RadioButton( label.c_str(), selectedBrushID == brushIndex ) )
                selectedBrushID = brushIndex;
            ImGui::PopID();
        }
    }

    void TileMapPanel::drawTileLayerCell( ImDrawList* pDrawList, const ImVec2& cellMin, const ImVec2& cellMax, int32 x, int32 y, void* pAtlasTexture ) const
    {
        const TileMapXMLData& map            = _document.getMap();
        const TileSetAsset&   tileSet        = _document.getTileSet();
        const vector<uint16>& listBrushIndex = _document.getBrushIndices();
        const TileVisual*     pVisual        = tileSet.resolveVisual( listBrushIndex, map._width, map._height, x, y );
        const optional<int32> cell           = pVisual != nullptr ? pVisual->computeCellAt( 0.0f ) : nullopt;
        if ( cell.has_value() == false )
            return; // 셀이 없는 모습은 타일셋 창이 문제로 보인다
        if ( pAtlasTexture != nullptr )
        {
            // 규칙이 고른 셀의 이미지 — 이웃을 칠하면 이미지가 따라 바뀌는 것이 규칙 타일이 일하는 모습이다.
            const float4 uvRect = tileSet.computeCellUVRect( *cell );
            pDrawList->AddImage( reinterpret_cast<ImTextureID>( pAtlasTexture ), cellMin, cellMax, ImVec2( uvRect._x, uvRect._y ),
                                 ImVec2( uvRect._x + uvRect._z, uvRect._y + uvRect._w ) );
            return;
        }
        // 브러시마다 다른 색(이름 해시) + 규칙이 고른 아틀라스 셀 번호 — 이웃을 칠하면 번호가 바뀌는 것이 규칙 타일이 일하는 모습이다.
        const uint16 brushValue = listBrushIndex[_document.getIndexOf( x, y )];
        const uint32 hash       = static_cast<uint32>( tileSet.getBrushes()[brushValue - 1u]._name.getHash() );
        const ImU32  color      = EditorColorUtil::toImU32( Color::fromBytes( static_cast<uint8>( 80 + ( hash & 0x7Fu ) ), static_cast<uint8>( 80 + ( ( hash >> 8 ) & 0x7Fu ) ), static_cast<uint8>( 80 + ( ( hash >> 16 ) & 0x7Fu ) ) ) );
        pDrawList->AddRectFilled( cellMin, cellMax, color );
        const string number = to_string( *cell );
        pDrawList->AddText( ImVec2( cellMin.x + 2.0f, cellMin.y + 1.0f ), EditorColorUtil::toImU32( SW_THEME_COLOR( "Base", "Text" ) ), number.c_str() );
    }
} // namespace sw::editor
