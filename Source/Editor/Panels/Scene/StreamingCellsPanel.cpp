#include "pch.h"

#include "Editor/Panels/Scene/StreamingCellsPanel.h"

#include "Core/String/fixed_string.h"

#include "Editor/Common/Config/EditorIconGlyphTable.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"

#include <imgui.h>

namespace sw::editor
{
    SW_EDITOR_PANEL( StreamingCellsPanel, StreamingCellsPanel::kPanelID, EditorPanelCategory::Tool, 2042 );

    StreamingCellsPanel::StreamingCellsPanel()
        : IEditorPanel( false ) // 필요할 때 여는 도구라 닫힌 채 시작한다
        , _viewModel{}
        , _viewState{}
    {
    }

    void StreamingCellsPanel::drawContent()
    {
        _viewModel.gather( _viewState );
        switch ( _viewState._content )
        {
            case StreamingCellsContent::NoActiveScene:
                EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorStreamingCells", "NoActiveScene" ) );
                return;
            case StreamingCellsContent::NotStreaming:
                EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorStreamingCells", "NotStreaming" ) );
                return;
            case StreamingCellsContent::CookedScene:
                ImGui::TextUnformatted( SW_LOCKEYFORMAT( "EdFormat", "StreamingOfCellsLoaded", TextArgumentList().addInteger( "loadedCellCount", static_cast<int64>( _viewState._loadedCellCount ) ).addInteger( "cellCount", static_cast<int64>( _viewState._cellCount ) ) ).c_str() );
                EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorStreamingCells", "CookedScene" ) );
                return;
            case StreamingCellsContent::NoPreview:
            case StreamingCellsContent::Violations:
            case StreamingCellsContent::Cells:
            case StreamingCellsContent::GridInvalid:
                break;
        }

        if ( ImGui::Button( EditorThemeUtil::makeIconLabel( SW_EDITOR_ICON( "Refresh" ), SW_LOCKEY( "EdButton", "PreviewSplit" ), "Preview Split" ) ) )
            (void)_viewModel.refreshPreview(); // 나누지 못한 이유는 쿠커가 로그에 남긴다 — 결과는 다음 프레임 gather 가 모은다
        EditorSelfTestMarks::record( "streamingCells.preview" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorStreamingCells", "PreviewTip" ) );
        ImGui::SameLine();
        ImGui::TextDisabled( "%s", SW_LOCKEYFORMAT( "EdFormat", "CellMRadiusM", TextArgumentList().addDecimal( "cellSize", static_cast<float64>( _viewState._cellSize ), 1 ).addDecimal( "loadingRadius", static_cast<float64>( _viewState._loadingRadius ), 1 ).addDecimal( "unloadMargin", static_cast<float64>( _viewState._unloadMargin ), 1 ).addInteger( "maxCellLoadsPerFrame", static_cast<int64>( _viewState._maxCellLoadsPerFrame ) ) ).c_str() );

        if ( _viewState._bLastSplitFailed != SW_FALSE )
            ImGui::TextColored( EditorColorUtil::toImVec4( SW_THEME_COLOR( "Signal", "Error" ) ), "%s", SW_LOCKEY( "EditorStreamingCells", "SplitFailed" ) );
        if ( _viewState._content == StreamingCellsContent::NoPreview )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorStreamingCells", "NoPreview" ) );
            return;
        }
        if ( _viewState._content == StreamingCellsContent::Violations )
        {
            drawViolations();
            return;
        }
        if ( _viewState._content == StreamingCellsContent::Cells )
            drawCells();
    }

    void StreamingCellsPanel::drawViolations()
    {
        ImGui::TextColored( EditorColorUtil::toImVec4( SW_THEME_COLOR( "Signal", "Error" ) ), "%s %u", SW_LOCKEY( "EditorStreamingCells", "ViolationHeader" ),
                            static_cast<uint32>( _viewState._listViolation.size() ) );
        constexpr ImGuiTableFlags kTableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY;
        if ( ImGui::BeginTable( "##world_cell_violations", 2, kTableFlags, ImGui::GetContentRegionAvail() ) == false )
            return;
        ImGui::TableSetupColumn( "Object", ImGuiTableColumnFlags_WidthStretch, 1.0f );
        ImGui::TableSetupColumn( "Refers to (another cell)", ImGuiTableColumnFlags_WidthStretch, 1.0f );
        ImGui::TableHeadersRow();
        for ( uint32 rowIndex = 0; rowIndex < static_cast<uint32>( _viewState._listViolation.size() ); ++rowIndex )
        {
            const StreamingCellViolationRow& violation = _viewState._listViolation[rowIndex];
            ImGui::PushID( static_cast<int32>( rowIndex ) );
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex( 0 );
            if ( ImGui::Selectable( violation._fromName.c_str(), false, ImGuiSelectableFlags_SpanAllColumns ) )
                _viewModel.selectViolationObjects( rowIndex );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorStreamingCells", "ViolationTip" ) );
            ImGui::TableSetColumnIndex( 1 );
            ImGui::TextUnformatted( violation._toName.c_str() );
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    void StreamingCellsPanel::drawCells()
    {
        if ( ImGui::SmallButton( SW_LOCKEY( "EdText", "AlwaysLoaded" ) ) )
            _viewModel.selectAlwaysLoadedObjects();
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorStreamingCells", "AlwaysLoadedTip" ) );
        ImGui::SameLine();
        ImGui::TextUnformatted( SW_LOCKEYFORMAT( "EdFormat", "EntitiesCells", TextArgumentList().addInteger( "alwaysLoadedIDCount", static_cast<int64>( _viewState._alwaysLoadedCount ) ).addInteger( "cellCount", static_cast<int64>( _viewState._listCell.size() ) ) ).c_str() );
        if ( _viewState._bHasGameCamera == SW_FALSE )
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorStreamingCells", "NoGameCamera" ) );

        constexpr ImGuiTableFlags kTableFlags =
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
        if ( ImGui::BeginTable( "##world_cells", 3, kTableFlags, ImGui::GetContentRegionAvail() ) == false )
            return;
        ImGui::TableSetupColumn( "Cell (x, z)", ImGuiTableColumnFlags_WidthStretch, 1.0f );
        ImGui::TableSetupColumn( "Entities", ImGuiTableColumnFlags_WidthStretch, 0.6f );
        ImGui::TableSetupColumn( "From game camera", ImGuiTableColumnFlags_WidthStretch, 0.8f );
        ImGui::TableHeadersRow();
        for ( uint32 rowIndex = 0; rowIndex < static_cast<uint32>( _viewState._listCell.size() ); ++rowIndex )
        {
            const StreamingCellRow& cell = _viewState._listCell[rowIndex];
            ImGui::PushID( static_cast<int32>( rowIndex ) );
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex( 0 );
            fixed_string<constant::buffer::kMax64> label;
            formatstring( label.data(), label.capacity(), "%#, %#", cell._cellX, cell._cellZ );
            if ( ImGui::Selectable( label.c_str(), false, ImGuiSelectableFlags_SpanAllColumns ) )
                _viewModel.selectCellObjects( rowIndex );
            if ( EditorSelfTestMarks::isEnabled() )
            {
                fixed_string<constant::buffer::kMax64> mark;
                formatstring( mark.data(), mark.capacity(), "streamingCells.cell.%#", rowIndex );
                EditorSelfTestMarks::record( mark.c_str() );
            }
            ImGui::TableSetColumnIndex( 1 );
            ImGui::Text( "%u", cell._objectCount );
            ImGui::TableSetColumnIndex( 2 );
            const Color stateColor = cell._state == StreamingCellPreviewState::Load ? SW_THEME_COLOR( "Signal", "Success" )
                                   : cell._state == StreamingCellPreviewState::Keep ? SW_THEME_COLOR( "Signal", "Warning" )
                                                                                    : SW_THEME_COLOR( "Base", "TextMuted" );
            ImGui::TextColored( EditorColorUtil::toImVec4( stateColor ), "%s", StreamingCellsViewModel::getStateLabel( cell._state ) );
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
} // namespace sw::editor
