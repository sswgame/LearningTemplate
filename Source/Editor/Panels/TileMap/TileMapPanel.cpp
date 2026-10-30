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
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Panels/TileMap/Model/TileMapPaintUtil.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/FileSystem/AssetPaths.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"

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
    SW_AUTOMATION_WATCH( editorTileMapFlagCells, "Editor.TileMapFlagCells", "Cells set in the Tile Map Tool's selected flag layer (0 on other layers)",
                         &TileMapPanelInternal::readFlagCells );
    SW_AUTOMATION_WATCH( editorTileMapDataLayers, "Editor.TileMapDataLayers", "Data layers of the Tile Map Tool's map", &TileMapPanelInternal::readDataLayerCount );
    SW_AUTOMATION_WATCH( editorTileMapDataCells, "Editor.TileMapDataCells", "Cells that differ from the layer default, summed over all data layers of the Tile Map Tool's map",
                         &TileMapPanelInternal::readDataCells );
    SW_AUTOMATION_WATCH( editorTileMapTool, "Editor.TileMapTool", "Tile Map Tool paint tool: 0 Brush, 1 Rect, 2 Fill, 3 Picker", &TileMapPanelInternal::readPaintTool );

    TileMapPanel::TileMapPanel()
        : EditorDocumentPanel{ EditorAssetType{ TileMapXMLData::kAssetType }, false }
        , _document{}
        , _atlasThumbnail{}
        , _canvasPan{ TileMapPanelInternal::kCanvasMargin, TileMapPanelInternal::kCanvasMargin }
        , _canvasZoom{ 1.0f }
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
        };
        for ( const ToolButton& button : arrToolButton )
        {
            if ( &button != arrToolButton )
                EditorChrome::sameLineIfFits( EditorChrome::measureCheckbox( button._pLabel ) );
            if ( ImGui::RadioButton( button._pLabel, _document.getTool() == button._tool ) )
                _document.setTool( button._tool );
            EditorSelfTestMarks::note( button._pMark );
            EditorWidgets::drawTooltip( button._pTooltip );
        }
        EditorChrome::sameLineIfFits( EditorChrome::measureCheckbox( "Erase" ) );
        ImGui::Checkbox( "Erase", &_document.editErase() );
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
        ImGui::TextDisabled( "%dx%d  %.0f%%", _document.getMap()._width, _document.getMap()._height, static_cast<float64>( _canvasZoom * 100.0f ) );

        // 유니티 Tile Palette 의 단축키 — `Editor.Panel.tile_map` 레이어의 액션(이 창이 포커스이고 글을 치는 중이 아닐 때만 발화한다).
        if ( EditorInputGUI::wasActionTriggered( "tileMap.brush" ) )
            _document.setTool( PaintTool::Brush );
        if ( EditorInputGUI::wasActionTriggered( "tileMap.rect" ) )
            _document.setTool( PaintTool::Rect );
        if ( EditorInputGUI::wasActionTriggered( "tileMap.fill" ) )
            _document.setTool( PaintTool::Fill );
        if ( EditorInputGUI::wasActionTriggered( "tileMap.picker" ) )
            _document.setTool( PaintTool::Picker );
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

        const float32 cell = TileMapPanelInternal::kBaseCellSize * EditorThemeUtil::getDPIScale() * _canvasZoom;
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
        const TileMapXMLData& map       = _document.getMap();
        const PaintLayer      layer     = _document.getPaintLayer();
        const TileFlagLayer   flagLayer = _document.getFlagLayer();
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

        for ( int32 tileY = 0; tileY < map._height; ++tileY )
        {
            for ( int32 tileX = 0; tileX < map._width; ++tileX )
            {
                const size_t tileIndex = _document.indexOf( tileX, tileY );
                ImU32        color     = EditorColorUtil::toImU32( TileMapPanelInternal::kEmptyCell );
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
                        color            = uniqueWarpCells.count( key ) ? EditorColorUtil::toImU32( TileMapPanelInternal::kWarpCell ) : EditorColorUtil::toImU32( TileMapPanelInternal::kNoWarpCell );
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
                            color = EditorColorUtil::toImU32( EditorThemeUtil::getAccentColor() );
                        break;
                    }
                }

                const ImVec2 p0( origin.x + static_cast<float32>( tileX ) * cell, origin.y + static_cast<float32>( tileY ) * cell );
                const ImVec2 p1( p0.x + cell - 1.0f, p0.y + cell - 1.0f );
                pDl->AddRectFilled( p0, p1, color );
                if ( layer == PaintLayer::Tile )
                    drawTileLayerCell( pDl, p0, p1, tileX, tileY, pAtlasTexture );
                pDl->AddRect( p0, p1, EditorColorUtil::toImU32( TileMapPanelInternal::kCellBorder ) );
            }
        }
    }

    void TileMapPanel::drawBrushPreview( ImDrawList* pDl, const ImVec2& origin, float32 cell, const int2& hoverCell, bool bHovered )
    {
        // 브러시 미리보기 — 커서 아래 셀, Rect 도구로 끄는 중이면 그 사각형(지우기면 빨강).
        const ImU32 previewColor = EditorColorUtil::toImU32( _document.isErase() ? TileMapPanelInternal::kBrushPreviewErase : TileMapPanelInternal::kBrushPreview );
        int2        rectStartCell{};
        if ( _document.readRectStart( rectStartCell ) )
        {
            const float32 minX = static_cast<float32>( MathUtil::min( rectStartCell._x, hoverCell._x ) );
            const float32 minY = static_cast<float32>( MathUtil::min( rectStartCell._y, hoverCell._y ) );
            const float32 maxX = static_cast<float32>( MathUtil::max( rectStartCell._x, hoverCell._x ) + 1 );
            const float32 maxY = static_cast<float32>( MathUtil::max( rectStartCell._y, hoverCell._y ) + 1 );
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
        // 한 프레임에 칠한 셀은 기록 하나다 — 같은 묶음 키라 끄는 동안의 프레임도 한 기록으로 합쳐진다.
        if ( _document.applyToolInput( input ) )
            notifyDocumentEdited( "Paint Tile Map", "tilemap-paint" );
    }

    void TileMapPanel::drawDataLayerControls()
    {
        if ( _document.getPaintLayer() == PaintLayer::Data )
        {
            ImGui::InputInt( "Value", &_document.editPaintDataValue() );
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
        if ( bAddDataLayer && _document.addDataLayer( _newDataLayerName.c_str(), _newDataLayerDefault ) )
        {
            _newDataLayerName.clear();
            notifyDocumentEdited( "Add Tile Map Data Layer", "tilemap-datalayer" );
        }
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileMap", "AddDataLayerTip" ) );
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

        fixed_string<constant::kMaxBuffer256>& path = _document.editPath();
        ImGui::InputText( "Path", path.data(), path.capacity() );
        EditorSelfTestMarks::note( "tileMap.path" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileMap", "PathTip" ) );
        fixed_string<constant::kMaxBuffer128>& name = _document.editName();
        ImGui::InputText( "Name", name.data(), name.capacity() );
        if ( ImGui::IsItemDeactivatedAfterEdit() )
            notifyDocumentEdited( "Edit Tile Map Name", "tilemap-name" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileMap", "NameTip" ) );
        ImGui::InputInt( "Width", &_document.editInputWidth() );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileMap", "WidthTip" ) );
        ImGui::SameLine();
        ImGui::InputInt( "Height", &_document.editInputHeight() );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileMap", "HeightTip" ) );
        if ( ImGui::Button( "Apply Size" ) && _document.applyInputSize() )
            notifyDocumentEdited( "Resize Tile Map" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileMap", "ApplySizeTip" ) );

        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Load" ) );
        const bool bLoad = ImGui::Button( "Load" );
        EditorSelfTestMarks::note( "tileMap.load" );
        if ( bLoad )
        {
            // 적어 넣은 경로에 파일이 없으면 지금 맵을 그대로 둔다(새 문서로 표시하면 편집 중인 맵이 저장 안 한 채 깨끗해진다).
            // 읽기(`EditorToolAssetCommands::loadTileMap`)와 같은 해석으로 묻는다 — 입력란은 리소스 경로와 디스크 경로를 모두 받는다.
            if ( editor::getSubsystem<AssetPaths>().findLooseFile( path.c_str() ).has_value() )
                reloadDocument();
            else
                SW_LOG_WARNING( "No tile map at '%#'", path.c_str() );
        }
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileMap", "LoadTip" ) );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Save" ) );
        const bool bSave = ImGui::Button( "Save" );
        EditorSelfTestMarks::note( "tileMap.save" );
        if ( bSave )
        {
            if ( saveXML() )
                _document.setStatus( SW_LOCKEYFORMAT( "EditorTileMap", "SavedStatus", TextArgumentList().addText( "path", path.c_str() ) ) );
            else
                _document.setStatus( SW_LOCKEY( "EditorTileMap", "SaveFailedStatus" ) );
        }
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileMap", "SaveTip" ) );
    }

    void TileMapPanel::drawLayerControls()
    {
        vector<const utf8*> listLayerName;
        _document.collectPaintLayerNames( listLayerName );
        int32 layerIndex = _document.getPaintLayerIndex();
        if ( ImGui::Combo( "Layer", &layerIndex, listLayerName.data(), static_cast<int32>( listLayerName.size() ) ) )
            _document.selectPaintLayer( layerIndex );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileMap", "LayerTip" ) );

        const PaintLayer layer = _document.getPaintLayer();
        if ( layer == PaintLayer::Visual )
        {
            ImGui::InputInt( "Height", &_document.editPaintHeight() );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileMap", "PaintHeightTip" ) );
            ImGui::InputInt( "Atlas ID", &_document.editAtlasID() );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileMap", "AtlasIDTip" ) );
            ImGui::ColorEdit3( "Tint", _document.editTint() );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileMap", "TintTip" ) );
        }
        else if ( layer == PaintLayer::Warp )
        {
            fixed_string<constant::kMaxBuffer128>& warpTarget = _document.editWarpTarget();
            ImGui::InputText( "Warp Target", warpTarget.data(), warpTarget.capacity() );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileMap", "WarpTargetTip" ) );
            ImGui::InputInt( "Target TX", &_document.editWarpTileX() );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileMap", "WarpTargetXTip" ) );
            ImGui::InputInt( "Target TY", &_document.editWarpTileY() );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileMap", "WarpTargetYTip" ) );
        }
        else if ( layer == PaintLayer::Tile )
        {
            fixed_string<constant::kMaxBuffer256>& tileSetInput = _document.editTileSetInput();
            ImGui::InputText( "Tile Set", tileSetInput.data(), tileSetInput.capacity() );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileMap", "TileSetTip" ) );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Use" ) );
            if ( ImGui::Button( "Use" ) && _document.useTileSetInput() )
                notifyDocumentEdited( "Set Tile Set", "tilemap-tileset" );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileMap", "UseTileSetTip" ) );
            drawTilePalette();
        }
        drawDataLayerControls();

        ImGui::Separator();
        ImGui::TextUnformatted( "Edge Warp Presets" );
        const utf8* edges[] = { "N", "E", "S", "W" };
        for ( int32 edgeIndex = 0; edgeIndex < TileMapDocument::kEdgeCount; ++edgeIndex )
        {
            TileMapDocument::EdgeWarpPreset& preset = _document.editEdgePreset( edgeIndex );
            ImGui::PushID( edgeIndex );
            EditorChrome::setNextItemWidthColumns( 24.0f );
            ImGui::InputText( edges[edgeIndex], preset._target.data(), preset._target.capacity() );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileMap", "EdgeTargetTip" ) );
            ImGui::SameLine();
            EditorChrome::setNextItemWidthColumns( 7.0f );
            ImGui::InputInt( "##tx", &preset._tileX );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileMap", "EdgeTargetXTip" ) );
            ImGui::SameLine();
            EditorChrome::setNextItemWidthColumns( 7.0f );
            ImGui::InputInt( "##ty", &preset._tileY );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileMap", "EdgeTargetYTip" ) );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Apply" ) );
            if ( ImGui::Button( "Apply" ) && _document.paintEdgeWarp( edgeIndex ) )
                notifyDocumentEdited( "Paint Tile Map Edge", "tilemap-edge" );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileMap", "ApplyEdgeTip" ) );
            ImGui::PopID();
        }

        ImGui::Separator();
    }

    bool TileMapPanel::saveXML()
    {
        if ( _document.saveXML() == false )
            return false;
        clearDocumentDirty();
        syncDocumentUndoBaseline();
        return true;
    }

    bool TileMapPanel::saveDocument()
    {
        return saveXML();
    }

    void TileMapPanel::drawTilePalette()
    {
        const TileSetAsset&      tileSet   = _document.getTileSet();
        const vector<TileBrush>& listBrush = tileSet.getBrushes();
        if ( listBrush.empty() )
        {
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EditorTileMap", "NoTileSet" ) );
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
                const float4 uvRect = tileSet.computeCellUvRect( brush._defaultVisual._listFrameCell.front() );
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
        if ( pVisual == nullptr )
            return;
        if ( pAtlasTexture != nullptr )
        {
            // 규칙이 고른 셀의 이미지 — 이웃을 칠하면 이미지가 따라 바뀌는 것이 규칙 타일이 일하는 모습이다.
            const float4 uvRect = tileSet.computeCellUvRect( pVisual->computeCellAt( 0.0f ) );
            pDrawList->AddImage( reinterpret_cast<ImTextureID>( pAtlasTexture ), cellMin, cellMax, ImVec2( uvRect._x, uvRect._y ),
                                 ImVec2( uvRect._x + uvRect._z, uvRect._y + uvRect._w ) );
            return;
        }
        // 브러시마다 다른 색(이름 해시) + 규칙이 고른 아틀라스 셀 번호 — 이웃을 칠하면 번호가 바뀌는 것이 규칙 타일이 일하는 모습이다.
        const uint16 brushValue = listBrushIndex[_document.indexOf( x, y )];
        const uint32 hash       = static_cast<uint32>( tileSet.getBrushes()[brushValue - 1u]._name.getHash() );
        const ImU32  color      = EditorColorUtil::toImU32( Color::fromBytes( static_cast<uint8>( 80 + ( hash & 0x7Fu ) ), static_cast<uint8>( 80 + ( ( hash >> 8 ) & 0x7Fu ) ), static_cast<uint8>( 80 + ( ( hash >> 16 ) & 0x7Fu ) ) ) );
        pDrawList->AddRectFilled( cellMin, cellMax, color );
        const string number = to_string( pVisual->computeCellAt( 0.0f ) );
        pDrawList->AddText( ImVec2( cellMin.x + 2.0f, cellMin.y + 1.0f ), EditorColorUtil::toImU32( TileMapPanelInternal::kCellNumberText ), number.c_str() );
    }
} // namespace sw::editor
