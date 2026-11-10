#include "pch.h"

#include "Editor/Panels/TileSet/TileSetPanel.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/fixed_string.h"
#include "Core/String/formatString.h"

#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Input/RawInputEvent.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct TileSetPanelInternal
        {
            static constexpr float32 kListLines       = 10.0f; ///< 브러시 · 규칙 목록이 보이는 줄 수
            static constexpr float32 kPreviewCellEm   = 3.0f;  ///< 규칙 결과 · 프레임 그림 한 칸의 크기(글자 높이 배수)
            static constexpr float32 kPatternButtonEm = 2.0f;  ///< 이웃 패턴 단추 한 칸의 크기(글자 높이 배수)

            /** @brief 문서를 읽은 열린 타일셋 창입니다. 없으면 nullptr 입니다. */
            [[nodiscard]] static const TileSetPanel* findLoadedPanel()
            {
                const EditorPanelManager* pPanels = EditorPanelManager::findInEditorContext();
                if ( pPanels == nullptr )
                    return nullptr;
                const TileSetPanel* pPanel = pPanels->findPanel<TileSetPanel>();
                return pPanel != nullptr && pPanel->isOpen() && pPanel->isTileSetDocumentLoaded() ? pPanel : nullptr;
            }

            [[nodiscard]] static bool readBrushCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const TileSetPanel* pPanel = findLoadedPanel();
                if ( pPanel == nullptr )
                    return false;
                outValue = static_cast<float64>( pPanel->getModel().getContent()._listBrush.size() );
                return true;
            }

            [[nodiscard]] static bool readRuleCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const TileSetPanel* pPanel = findLoadedPanel();
                const TileBrush*    pBrush = pPanel != nullptr ? pPanel->getModel().findSelectedBrush() : nullptr;
                if ( pBrush == nullptr )
                    return false;
                outValue = static_cast<float64>( pBrush->_listRule.size() );
                return true;
            }

            [[nodiscard]] static bool readFrameCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const TileSetPanel* pPanel  = findLoadedPanel();
                const TileVisual*   pVisual = pPanel != nullptr ? pPanel->getModel().findSelectedVisual() : nullptr;
                if ( pVisual == nullptr )
                    return false;
                outValue = static_cast<float64>( pVisual->_listFrameCell.size() );
                return true;
            }

            [[nodiscard]] static bool readFirstCell( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const TileSetPanel* pPanel  = findLoadedPanel();
                const TileVisual*   pVisual = pPanel != nullptr ? pPanel->getModel().findSelectedVisual() : nullptr;
                if ( pVisual == nullptr || pVisual->_listFrameCell.empty() )
                    return false;
                outValue = static_cast<float64>( pVisual->_listFrameCell.front() );
                return true;
            }

            [[nodiscard]] static bool readIssueCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const TileSetPanel* pPanel = findLoadedPanel();
                if ( pPanel == nullptr )
                    return false;
                outValue = static_cast<float64>( pPanel->getIssueCount() );
                return true;
            }

            /** @brief 이웃 조건의 단추 글자입니다 — 파일의 패턴 글자 그대로(`. o x`)라 파일과 화면이 같은 말이다. */
            static const utf8* getNeighborLabel( TileNeighborRule rule )
            {
                switch ( rule )
                {
                    case TileNeighborRule::Any:
                        return ".";
                    case TileNeighborRule::Same:
                        return "o";
                    case TileNeighborRule::Other:
                        return "x";
                }
                return ".";
            }

            static void recordIndexedMark( const utf8* pPrefix, int32 index )
            {
                if ( EditorSelfTestMarks::isEnabled() == false )
                    return;
                fixed_string<constant::buffer::kMax64> key;
                formatstring( key.data(), key.capacity(), "%#.%#", pPrefix, index );
                EditorSelfTestMarks::record( key.c_str() );
            }
        };
    } // namespace

    SW_EDITOR_PANEL( TileSetPanel, TileSetPanel::kPanelID, EditorPanelCategory::Tool, 1510 );
    SW_AUTOMATION_WATCH( editorTileSetBrushCount, "Editor.TileSetBrushCount", "Brushes of the tile set the Tile Set Editor holds", &TileSetPanelInternal::readBrushCount );
    SW_AUTOMATION_WATCH( editorTileSetRuleCount, "Editor.TileSetRuleCount", "Rules of the brush selected in the Tile Set Editor", &TileSetPanelInternal::readRuleCount );
    SW_AUTOMATION_WATCH( editorTileSetFrameCount, "Editor.TileSetFrameCount", "Frames of the visual the Tile Set Editor edits (the selected rule, else the brush default)",
                         &TileSetPanelInternal::readFrameCount );
    SW_AUTOMATION_WATCH( editorTileSetFirstCell, "Editor.TileSetFirstCell", "First atlas cell of the visual the Tile Set Editor edits", &TileSetPanelInternal::readFirstCell );
    SW_AUTOMATION_WATCH( editorTileSetIssueCount, "Editor.TileSetIssueCount", "Rule violations in the tile set the Tile Set Editor holds (saving is refused while above 0)",
                         &TileSetPanelInternal::readIssueCount );

    TileSetPanel::TileSetPanel()
        : EditorDocumentPanel{ EditorAssetType{ TileSetAsset::kAssetType }, false }
        , _model{}
        , _atlasThumbnail{}
        , _listIssue{}
        , _nameBuffer{}
        , _nameBrush{ -1 }
    {
    }

    ToolAssetLoadResult TileSetPanel::loadDocument()
    {
        _nameBrush = -1;
        return _model.loadDocument( getLoadedAssetPath() );
    }

    bool TileSetPanel::saveDocument()
    {
        if ( _model.saveDocument( getLoadedAssetPath() ) == false )
            return false; // 까닭은 상태 줄에 있다
        syncDocumentUndoBaseline();
        return true;
    }

    void TileSetPanel::shutdown()
    {
        _atlasThumbnail.clear();
    }

    void TileSetPanel::drawDocumentContent()
    {
        updateFocusedDocument();
        ensureDocumentLoaded();
        drawDocumentOpenBar( "tileSet" );
        _atlasThumbnail.update();
        _model.collectIssues( _listIssue );

        drawAtlasRows();
        constexpr ImGuiTableFlags kSplitFlags = ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV;
        if ( ImGui::BeginTable( "##TileSetSplit", 2, kSplitFlags, ImVec2{ 0.0f, ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() } ) )
        {
            ImGui::TableSetupColumn( "##Brushes", ImGuiTableColumnFlags_WidthStretch, 0.45f );
            ImGui::TableSetupColumn( "##Atlas", ImGuiTableColumnFlags_WidthStretch, 0.55f );
            ImGui::TableNextColumn();
            drawBrushList();
            const TileBrush* pBrush = _model.findSelectedBrush();
            if ( pBrush != nullptr )
            {
                const TileBrush brush = *pBrush; // 편집이 목록을 바꾸므로 사본으로 그린다
                drawBrushDetail( brush );
                drawRuleSection( brush );
            }
            ImGui::TableNextColumn();
            drawAtlasGrid();
            ImGui::EndTable();
        }
        EditorWidgets::drawPanelStatus( _model.getStatus().c_str() );
    }

    void TileSetPanel::drawAtlasRows()
    {
        if ( ImGui::Button( SW_LOCKEY( "EdVerb", "Save" ) ) )
            (void)saveDocumentAndClearDirty(); // 위반이 있으면 쓰지 않고 dirty 가 남는다 — 까닭은 상태 줄
        EditorSelfTestMarks::record( "tileSet.save" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileSet", "SaveTip" ) );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdVerb", "Reload" ) ) );
        if ( ImGui::Button( SW_LOCKEY( "EdVerb", "Reload" ) ) )
            reloadDocument();
        EditorSelfTestMarks::record( "tileSet.reload" );

        const TileSetContent&                   content = _model.getContent();
        fixed_string<constant::buffer::kMax256> atlas;
        atlas = string_view( content._atlasPath );
        EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdField", "Atlas" ) );
        if ( EditorWidgets::drawAssetPathField( "##TileSetAtlas", atlas, "atlas", nullptr, "tileSet.atlasPath" )._bDeactivatedAfterEdit && _model.setAtlasPath( atlas.c_str() ) )
            notifyDocumentEdited( "Edit Tile Set Atlas" );
        int32 grid[2] = { content._columnCount, content._rowCount };
        EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EditorTileSet", "Grid" ), SW_LOCKEY( "EditorTileSet", "GridTip" ) );
        ImGui::InputInt2( "##TileSetGrid", grid );
        if ( ImGui::IsItemDeactivatedAfterEdit() && _model.setAtlasGrid( grid[0], grid[1] ) )
            notifyDocumentEdited( "Edit Tile Set Grid" );
        float32 tileSize = content._tileSize;
        EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EditorTileSet", "TileSize" ), SW_LOCKEY( "EditorTileSet", "TileSizeTip" ) );
        ImGui::InputFloat( "##TileSetTileSize", &tileSize, 0.0f, 0.0f, "%.3f" );
        if ( ImGui::IsItemDeactivatedAfterEdit() && _model.setTileSize( tileSize ) )
            notifyDocumentEdited( "Edit Tile Set Tile Size" );
        drawIssuesFor( nullopt, nullopt ); // 타일셋 전체의 문제
    }

    void TileSetPanel::drawBrushList()
    {
        using Internal = TileSetPanelInternal;
        ImGui::SeparatorText( SW_LOCKEY( "EditorTileSet", "Brushes" ) );
        if ( ImGui::Button( SW_LOCKEY( "EditorTileSet", "AddTile" ) ) )
        {
            _model.addBrush( false );
            notifyDocumentEdited( "Add Tile Brush" );
        }
        EditorSelfTestMarks::record( "tileSet.addTile" );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EditorTileSet", "AddRuleTile" ) ) );
        if ( ImGui::Button( SW_LOCKEY( "EditorTileSet", "AddRuleTile" ) ) )
        {
            _model.addBrush( true );
            notifyDocumentEdited( "Add Rule Tile Brush" );
        }
        EditorSelfTestMarks::record( "tileSet.addRuleTile" );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdVerb", "Remove" ) ) );
        ImGui::BeginDisabled( _model.findSelectedBrush() == nullptr );
        if ( ImGui::Button( EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EdVerb", "Remove" ), "Remove##brush" ) ) && _model.removeSelectedBrush() )
            notifyDocumentEdited( "Remove Tile Brush" );
        ImGui::EndDisabled();
        EditorSelfTestMarks::record( "tileSet.removeBrush" );

        const vector<TileBrush>& listBrush = _model.getContent()._listBrush;
        if ( listBrush.empty() )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorTileSet", "NoBrushesHint" ) );
            return;
        }
        if ( ImGui::BeginChild( "##TileSetBrushes", ImVec2{ 0.0f, ImGui::GetTextLineHeightWithSpacing() * Internal::kListLines }, ImGuiChildFlags_Borders ) )
        {
            for ( int32 brushIndex = 0; brushIndex < static_cast<int32>( listBrush.size() ); ++brushIndex )
            {
                const TileBrush& brush  = listBrush[static_cast<size_t>( brushIndex )];
                bool             bIssue = false;
                for ( const TileSetIssue& issue : _listIssue )
                {
                    bIssue = bIssue || issue._brushIndex == brushIndex;
                }
                fixed_string<constant::buffer::kMax128> label;
                formatstring( label.data(), label.capacity(), "%#  (%#)", brush._name.c_str(),
                              brush.isRuleTile() ? SW_LOCKEY( "EditorTileSet", "KindRule" ) : SW_LOCKEY( "EditorTileSet", "KindTile" ) );
                ImGui::PushID( brushIndex );
                // 위반이 있는 브러시는 빨간 표시 하나다(색만이 아니라 아래 문장이 까닭을 말한다).
                if ( bIssue )
                    ImGui::PushStyleColor( ImGuiCol_Text, EditorColorUtil::toImVec4( SW_THEME_COLOR( "Signal", "Error" ) ) );
                if ( ImGui::Selectable( label.c_str(), _model.getSelectedBrush() == brushIndex ) )
                    _model.selectBrush( brushIndex );
                if ( bIssue )
                    ImGui::PopStyleColor();
                Internal::recordIndexedMark( "tileSet.brush", brushIndex );
                ImGui::PopID();
            }
        }
        ImGui::EndChild();
    }

    void TileSetPanel::drawBrushDetail( const TileBrush& brush )
    {
        if ( _nameBrush != _model.getSelectedBrush() )
        {
            _nameBuffer = brush._name.c_str();
            _nameBrush  = _model.getSelectedBrush();
        }
        EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdWidget", "Name" ) );
        EditorWidgets::drawTextField( "##TileSetBrushName", _nameBuffer, -1.0f );
        if ( ImGui::IsItemDeactivatedAfterEdit() )
        {
            if ( _model.renameSelectedBrush( _nameBuffer ) )
                notifyDocumentEdited( "Rename Tile Brush" );
            else
                _nameBuffer = brush._name.c_str(); // 거절한 이름은 칸에서도 되돌린다(까닭은 상태 줄)
        }
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileSet", "BrushNameTip" ) );
        bool bSolid = brush._bSolid == SW_TRUE;
        EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EditorTileSet", "Solid" ), SW_LOCKEY( "EditorTileSet", "SolidTip" ) );
        if ( ImGui::Checkbox( "##TileSetSolid", &bSolid ) && _model.setSelectedSolid( bSolid ) )
            notifyDocumentEdited( "Edit Tile Brush" );
        EditorSelfTestMarks::record( "tileSet.solid" );
        int32 navCost = static_cast<int32>( brush._navCost );
        EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EditorTileSet", "NavCost" ), SW_LOCKEY( "EditorTileSet", "NavCostTip" ) );
        ImGui::InputInt( "##TileSetNavCost", &navCost );
        if ( ImGui::IsItemDeactivatedAfterEdit() && _model.setSelectedNavCost( navCost ) )
            notifyDocumentEdited( "Edit Tile Brush" );
        if ( brush.isRuleTile() )
        {
            bool bOutsideSame = brush._bOutsideIsSame == SW_TRUE;
            EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EditorTileSet", "OutsideSame" ), SW_LOCKEY( "EditorTileSet", "OutsideSameTip" ) );
            if ( ImGui::Checkbox( "##TileSetOutside", &bOutsideSame ) && _model.setSelectedOutsideSame( bOutsideSame ) )
                notifyDocumentEdited( "Edit Tile Brush" );
        }
    }

    void TileSetPanel::drawRuleSection( const TileBrush& brush )
    {
        using Internal = TileSetPanelInternal;
        ImGui::SeparatorText( SW_LOCKEY( "EditorTileSet", "Look" ) );
        // 기본 모습은 그냥 타일의 모습이고, 규칙 타일에서는 어느 규칙도 맞지 않을 때의 모습이다.
        if ( ImGui::Selectable( SW_LOCKEY( "EditorTileSet", "DefaultLook" ), _model.getSelectedRule() == TileSetEditModel::kDefaultVisual ) )
            _model.selectRule( TileSetEditModel::kDefaultVisual );
        EditorSelfTestMarks::record( "tileSet.defaultLook" );
        drawIssuesFor( _model.getSelectedBrush(), nullopt ); // 기본 모습
        if ( brush.isRuleTile() )
        {
            for ( int32 ruleIndex = 0; ruleIndex < static_cast<int32>( brush._listRule.size() ); ++ruleIndex )
            {
                const TileRule& rule = brush._listRule[static_cast<size_t>( ruleIndex )];
                ImGui::PushID( 1000 + ruleIndex );
                fixed_string<constant::buffer::kMax64> label;
                formatstring( label.data(), label.capacity(), "%#  %#", SW_LOCKEYFORMAT( "EditorTileSet", "RuleLabel", TextArgumentList().addInteger( "index", static_cast<int64>( ruleIndex + 1 ) ) ).c_str(),
                              rule._visual._listFrameCell.empty() ? -1 : rule._visual._listFrameCell.front() );
                if ( ImGui::Selectable( label.c_str(), _model.getSelectedRule() == ruleIndex ) )
                    _model.selectRule( ruleIndex );
                Internal::recordIndexedMark( "tileSet.rule", ruleIndex );
                ImGui::PopID();
                drawIssuesFor( _model.getSelectedBrush(), ruleIndex );
            }
            if ( ImGui::Button( SW_LOCKEY( "EditorTileSet", "AddRule" ) ) && _model.addRule() )
                notifyDocumentEdited( "Add Tile Rule" );
            EditorSelfTestMarks::record( "tileSet.addRule" );
            const bool bRuleSelected = _model.getSelectedRule() >= 0;
            ImGui::BeginDisabled( bRuleSelected == false );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdVerb", "Remove" ) ) );
            if ( ImGui::Button( EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EdVerb", "Remove" ), "Remove##rule" ) ) && _model.removeSelectedRule() )
                notifyDocumentEdited( "Remove Tile Rule" );
            EditorSelfTestMarks::record( "tileSet.removeRule" );
            EditorChrome::sameLineIfFits( EditorChrome::measureArrowButton() );
            if ( ImGui::ArrowButton( "##ruleUp", ImGuiDir_Up ) && _model.moveSelectedRule( -1 ) )
                notifyDocumentEdited( "Reorder Tile Rules" );
            EditorChrome::sameLineIfFits( EditorChrome::measureArrowButton() );
            if ( ImGui::ArrowButton( "##ruleDown", ImGuiDir_Down ) && _model.moveSelectedRule( 1 ) )
                notifyDocumentEdited( "Reorder Tile Rules" );
            ImGui::EndDisabled();
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileSet", "RuleOrderTip" ) );
        }

        const TileVisual* pVisual = _model.findSelectedVisual();
        if ( pVisual == nullptr )
            return;
        const TileVisual visual = *pVisual;
        drawVisualRow( visual );
        const int32 ruleIndex = _model.getSelectedRule();
        if ( ruleIndex < 0 || ruleIndex >= static_cast<int32>( brush._listRule.size() ) )
            return;
        // 이웃 패턴 3 × 3 — 가운데는 이 규칙이 고르는 셀이다. 단추를 누르면 상관없음(.) → 같음(o) → 다름(x) 으로 돈다(파일의 패턴 글자 그대로).
        ImGui::TextUnformatted( SW_LOCKEY( "EditorTileSet", "Pattern" ) );
        EditorWidgets::drawHelpMarker( SW_LOCKEY( "EditorTileSet", "PatternTip" ) );
        const TileRule& rule       = brush._listRule[static_cast<size_t>( ruleIndex )];
        const float32   buttonSide = ImGui::GetFontSize() * Internal::kPatternButtonEm;
        // 이웃 번호(`kNeighborOffset`)를 3 × 3 칸에 놓는다: 0 1 2 / 3 · 4 / 5 6 7.
        constexpr int32 kArrNeighborOfCell[9] = { 0, 1, 2, 3, -1, 4, 5, 6, 7 };
        for ( int32 cell = 0; cell < 9; ++cell )
        {
            if ( cell % 3 != 0 )
                ImGui::SameLine();
            const int32 neighbor = kArrNeighborOfCell[cell];
            if ( neighbor < 0 )
            {
                drawCellImage( visual._listFrameCell.empty() ? -1 : visual._listFrameCell.front(), buttonSide );
                continue;
            }
            ImGui::PushID( 2000 + neighbor );
            if ( ImGui::Button( Internal::getNeighborLabel( rule._arrNeighbor[neighbor] ), ImVec2{ buttonSide, buttonSide } ) && _model.cycleNeighbor( neighbor ) )
                notifyDocumentEdited( "Edit Tile Rule Pattern" );
            Internal::recordIndexedMark( "tileSet.neighbor", neighbor );
            ImGui::PopID();
        }
    }

    void TileSetPanel::drawVisualRow( const TileVisual& visual )
    {
        using Internal = TileSetPanelInternal;
        ImGui::TextUnformatted( SW_LOCKEY( "EditorTileSet", "Frames" ) );
        EditorWidgets::drawHelpMarker( SW_LOCKEY( "EditorTileSet", "FramesTip" ) );
        const float32 side = ImGui::GetFontSize() * Internal::kPreviewCellEm;
        for ( size_t frameIndex = 0; frameIndex < visual._listFrameCell.size(); ++frameIndex )
        {
            if ( frameIndex > 0 )
                EditorChrome::sameLineIfFits( side );
            drawCellImage( visual._listFrameCell[frameIndex], side );
        }
        ImGui::BeginDisabled( visual._listFrameCell.size() <= 1 );
        if ( ImGui::Button( SW_LOCKEY( "EditorTileSet", "RemoveLastFrame" ) ) && _model.removeLastFrame() )
            notifyDocumentEdited( "Remove Tile Frame" );
        ImGui::EndDisabled();
        EditorSelfTestMarks::record( "tileSet.removeLastFrame" );
        if ( visual._listFrameCell.size() > 1 )
        {
            float32 fps = visual._framesPerSecond;
            EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EditorTileSet", "Fps" ) );
            ImGui::InputFloat( "##TileSetFps", &fps, 0.0f, 0.0f, "%.2f" );
            if ( ImGui::IsItemDeactivatedAfterEdit() && _model.setFramesPerSecond( fps ) )
                notifyDocumentEdited( "Edit Tile Frames" );
        }
    }

    void TileSetPanel::drawCellImage( int32 cell, float32 side )
    {
        const TileSetContent& content  = _model.getContent();
        void*                 pTexture = content._atlasPath.empty() ? nullptr : _atlasThumbnail.findTexture( content._atlasPath.c_str() );
        const bool            bInside  = 0 <= cell && cell < content.getCellCount();
        if ( pTexture == nullptr || bInside == false )
        {
            // 그림이 없으면 셀 번호를 같은 크기 칸에 적는다 — 빈 칸만 남기지 않는다.
            fixed_string<constant::buffer::kMax32> label;
            formatstring( label.data(), label.capacity(), "%#", cell );
            ImGui::Button( label.c_str(), ImVec2{ side, side } );
            return;
        }
        // 런타임과 같은 셀 해석(`TileSetAsset::computeCellUVRect`)으로 자른다.
        const float4 uv = TileSetAsset::computeCellUVRect( content, cell );
        ImGui::Image( reinterpret_cast<ImTextureID>( pTexture ), ImVec2{ side, side }, ImVec2{ uv._x, uv._y }, ImVec2{ uv._x + uv._z, uv._y + uv._w } );
    }

    void TileSetPanel::drawAtlasGrid()
    {
        const TileSetContent& content = _model.getContent();
        ImGui::SeparatorText( SW_LOCKEY( "EditorTileSet", "AtlasCells" ) );
        void* pTexture = content._atlasPath.empty() ? nullptr : _atlasThumbnail.findTexture( content._atlasPath.c_str() );
        if ( pTexture == nullptr )
        {
            EditorWidgets::drawEmptyHint( content._atlasPath.empty() ? SW_LOCKEY( "EditorTileSet", "NoAtlasHint" ) : SW_LOCKEY( "EditorTileSet", "LoadingAtlasHint" ) );
            return;
        }
        if ( ImGui::BeginChild( "##TileSetAtlas", ImVec2{ 0.0f, 0.0f }, ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar ) )
        {
            // 셀은 정사각형으로 본다(타일 아틀라스) — 폭에 맞추고 높이는 행 수만큼이다. 넘치면 창이 스크롤한다.
            const float32 width    = ImGui::GetContentRegionAvail().x;
            const float32 cellSide = width / static_cast<float32>( MathUtil::max( content._columnCount, 1 ) );
            const float32 height   = cellSide * static_cast<float32>( MathUtil::max( content._rowCount, 1 ) );
            const ImVec2  imageMin = ImGui::GetCursorScreenPos();
            ImGui::Image( reinterpret_cast<ImTextureID>( pTexture ), ImVec2{ width, height } );
            const bool bHovered = ImGui::IsItemHovered();
            EditorSelfTestMarks::recordRect( "tileSet.atlas", float2{ imageMin.x, imageMin.y }, float2{ imageMin.x + width, imageMin.y + height } );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTileSet", "AtlasCellTip" ) );

            // 고른 모습의 셀은 강조색 틀이다(선택). 첫 프레임은 굵게.
            ImDrawList*       pDrawList = ImGui::GetWindowDrawList();
            const ImU32       accent    = EditorColorUtil::toImU32( SW_THEME_COLOR( "Base", "Accent" ) );
            const TileVisual* pVisual   = _model.findSelectedVisual();
            if ( pVisual != nullptr )
            {
                for ( size_t frameIndex = 0; frameIndex < pVisual->_listFrameCell.size(); ++frameIndex )
                {
                    const int32 cell = pVisual->_listFrameCell[frameIndex];
                    if ( cell < 0 || cell >= content.getCellCount() )
                        continue;
                    const float32 x = imageMin.x + cellSide * static_cast<float32>( cell % content._columnCount );
                    const float32 y = imageMin.y + cellSide * static_cast<float32>( cell / content._columnCount );
                    pDrawList->AddRect( ImVec2{ x, y }, ImVec2{ x + cellSide, y + cellSide }, accent, 0.0f, 0, frameIndex == 0 ? 2.0f : 1.0f );
                }
            }
            if ( bHovered && EditorInputGUI::readPointer().wasPressed( EditorMouseButton::Left ) )
            {
                const ImVec2 mouse   = ImGui::GetMousePos();
                const int32  column  = static_cast<int32>( ( mouse.x - imageMin.x ) / cellSide );
                const int32  row     = static_cast<int32>( ( mouse.y - imageMin.y ) / cellSide );
                const bool   bInGrid = 0 <= column && column < content._columnCount && 0 <= row && row < content._rowCount;
                const bool   bAppend = EditorInputGUI::readPointer().hasModifier( ModifierKey::Shift );
                if ( bInGrid && _model.pickCell( row * content._columnCount + column, bAppend ) )
                    notifyDocumentEdited( bAppend ? "Add Tile Frame" : "Pick Tile Cell" );
            }
        }
        ImGui::EndChild();
    }

    void TileSetPanel::drawIssuesFor( optional<int32> brushIndex, optional<int32> ruleIndex )
    {
        for ( const TileSetIssue& issue : _listIssue )
        {
            if ( issue._brushIndex != brushIndex || issue._ruleIndex != ruleIndex )
                continue;
            ImGui::PushStyleColor( ImGuiCol_Text, EditorColorUtil::toImVec4( SW_THEME_COLOR( "Signal", "Error" ) ) );
            ImGui::TextWrapped( "%s", issue._message.c_str() );
            ImGui::PopStyleColor();
        }
    }
} // namespace sw::editor
