/**
 * @file StreamingCellsPanel.cpp
 * @brief 월드 셀 창 — 스트리밍하는 씬을 쿠커처럼 셀로 나눠 셀마다 실릴 엔티티와 게임 카메라 자리에서의 올림 · 내림을 미리 보고, 쿠킹이 실패할 셀을 넘는 참조를 짚습니다.
 * @details 언리얼 World Partition 창의 셀 보기 · 맵 검사 자리입니다(D1-7). 나누기는 `StreamingCellPreview::build` 한 길이라
 *          쿠커와 같은 결과다. 씬을 한 번 더 짓는 무거운 일이라 단추로만 다시 계산하고, 활성 씬이 바뀌면 결과를 버린다.
 *          셀의 상태(올림 · 유지 · 내림)는 게임 카메라에서 셀까지의 거리를 격자의 로딩 반경과 반경 + 여유(`SceneSettings::_unloadMargin`)로 비교한 것으로,
 *          `SceneStreamingSubsystem` 의 갱신 규칙과 같다. 쿠킹본으로 지은 씬(스트리밍 중)은 문서로 쓰지 않으므로 나눠 보지 않고 올라 있는 셀 수만 보인다.
 *          격자 선은 디버그 HUD 범주 `worldcells` 가 씬 뷰에 그린다. 상태와 명령은 `StreamingCellsViewModel`(Model/, ImGui 없음)이 들고 값은 `StreamingCellsViewState` 로 받아, 이 패널은 그리기만 한다(MVVM 계약).
 */
#include "pch.h"

#include "Core/Foundation/Types.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Config/EditorIconGlyphTable.h"
#include "Editor/Common/GUI/Panel/EditorPanel.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Panels/Scene/Model/StreamingCellsViewModel.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"

#include <imgui.h>

namespace sw::editor
{
    /** @brief 월드 셀 미리보기 도구 창입니다. */
    class StreamingCellsPanel : public EditorPanel
    {
    public:
        /** @brief 패널 id 입니다(`SW_EDITOR_PANEL`, `-gv_editorOpenPanel`). */
        static constexpr const utf8* kPanelID = "world_cells";

        /** @brief 월드 셀 창을 만듭니다(닫힌 채 시작한다). */
        StreamingCellsPanel();

        const utf8* getPanelTitle() const override { return "Streaming Cells"; }
        void        drawContent() override;

    private:
        /** @brief 셀을 넘는 참조 표 — 줄을 누르면 가리키는 쪽 오브젝트를 고른다. */
        void drawViolations();
        /** @brief 셀 표 — 좌표 · 엔티티 수 · 상태. 줄을 누르면 그 셀의 엔티티를 고른다. */
        void drawCells();

    private:
        StreamingCellsViewModel _viewModel;
        StreamingCellsViewState _viewState; ///< 프레임마다 `gather` 가 채운다
    };

    SW_EDITOR_PANEL( StreamingCellsPanel, StreamingCellsPanel::kPanelID, EditorPanelCategory::Tool, 2042 );

    StreamingCellsPanel::StreamingCellsPanel()
        : EditorPanel( false ) // 필요할 때 여는 도구라 닫힌 채 시작한다
        , _viewModel{}
        , _viewState{}
    {
    }

    void StreamingCellsPanel::drawContent()
    {
        gatherViewState( _viewModel, _viewState );
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
        constexpr ImGuiTableFlags kTableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_NoBordersInBody | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY; // 줄 전체가 고르기다 — 몸의 칸 경계가 누름을 먹지 않게 칸 폭은 머리에서만 끈다
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
            ImGuiTableFlags_Borders | ImGuiTableFlags_NoBordersInBody | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp; // 줄 전체가 고르기다 — 몸의 칸 경계가 누름을 먹지 않게 칸 폭은 머리에서만 끈다
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
            EditorSelfTestMarks::recordFormat( "streamingCells.cell.%#", rowIndex );
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
