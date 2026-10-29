#include "pch.h"

#include "Editor/Panels/Scene/StreamingCellsPanel.h"

#include "Core/String/fixed_string.h"

#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorIconGlyphs.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Localization/LocText.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Scene/SceneStreamingSubsystem.h"
#include "Engine/Scene/StreamingCellGrid.h"

#include <imgui.h>

namespace sw::editor
{
    SW_EDITOR_PANEL( StreamingCellsPanel, StreamingCellsPanel::kPanelID, EditorPanelCategory::Tool, 2042 );

    StreamingCellsPanel::StreamingCellsPanel()
        : IEditorPanel( false ) // 필요할 때 여는 도구라 닫힌 채 시작한다
        , _model{}
    {
    }

    void StreamingCellsPanel::drawContent()
    {
        Scene*        pScene        = editor::getActiveScene();
        SceneManager* pSceneManager = editor::findSubsystem<SceneManager>();
        const bool    bNoScene      = pScene == nullptr || pSceneManager == nullptr || pScene->getObjectManager() == nullptr;
        if ( bNoScene )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorStreamingCells", "NoActiveScene" ) );
            return;
        }
        const SceneSettings& settings = pScene->getSceneSettings();
        if ( settings._bStreaming == false )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorStreamingCells", "NotStreaming" ) );
            return;
        }
        // 쿠킹본으로 지은 씬은 이미 셀로 나뉘어 있고 문서로 쓰지 않는다 — 나눠 보지 않고 올라 있는 셀 수만 보인다.
        const SceneStreamingSubsystem& streaming = pScene->getObjectManager()->getSubsystem<SceneStreamingSubsystem>();
        if ( streaming.isStreaming() )
        {
            ImGui::Text( "streaming: %u of %u cells loaded", streaming.getLoadedCellCount(), streaming.getCellCount() );
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorStreamingCells", "CookedScene" ) );
            return;
        }
        // 다른 씬의 결과는 그 씬의 오브젝트 id 라 버린다.
        _model.discardIfSceneChanged( pSceneManager->getSceneGeneration() );

        if ( ImGui::Button( EditorThemeUtil::makeIconLabel( editoricon::kRefresh, "Preview Split" ) ) )
            (void)_model.refreshPreview(); // 나누지 못한 이유는 쿠커가 로그에 남긴다
        EditorSelfTestMarks::note( "streamingCells.preview" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorStreamingCells", "PreviewTip" ) );
        ImGui::SameLine();
        ImGui::TextDisabled( "cell %.1f m, radius %.1f m, margin %.1f m, %u per frame", static_cast<float64>( settings._cellGrid._cellSize ), static_cast<float64>( settings._cellGrid._loadingRadius ), static_cast<float64>( settings._unloadMargin ),
                             settings._maxCellLoadsPerFrame );

        if ( _model.didLastSplitFail() )
            ImGui::TextColored( EditorColorUtil::toImVec4( EditorThemeUtil::getErrorColor() ), "%s", SW_LOCKEY( "EditorStreamingCells", "SplitFailed" ) );
        if ( _model.hasPreview() == false )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorStreamingCells", "NoPreview" ) );
            return;
        }
        if ( _model.getPreview()._listViolation.empty() == false )
        {
            drawViolations();
            return;
        }
        drawCells( *pScene );
    }

    void StreamingCellsPanel::drawViolations()
    {
        const StreamingCellPreview& preview = _model.getPreview();
        ImGui::TextColored( EditorColorUtil::toImVec4( EditorThemeUtil::getErrorColor() ), "%s %u", SW_LOCKEY( "EditorStreamingCells", "ViolationHeader" ),
                            static_cast<uint32>( preview._listViolation.size() ) );
        constexpr ImGuiTableFlags kTableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY;
        if ( ImGui::BeginTable( "##world_cell_violations", 2, kTableFlags, ImGui::GetContentRegionAvail() ) == false )
            return;
        ImGui::TableSetupColumn( "Object", ImGuiTableColumnFlags_WidthStretch, 1.0f );
        ImGui::TableSetupColumn( "Refers to (another cell)", ImGuiTableColumnFlags_WidthStretch, 1.0f );
        ImGui::TableHeadersRow();
        for ( uint32 rowIndex = 0; rowIndex < static_cast<uint32>( preview._listViolation.size() ); ++rowIndex )
        {
            const StreamingCellReference& violation = preview._listViolation[rowIndex];
            ImGui::PushID( static_cast<int32>( rowIndex ) );
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex( 0 );
            if ( ImGui::Selectable( StreamingCellsModel::findObjectName( violation._fromObjectID ).c_str(), false, ImGuiSelectableFlags_SpanAllColumns ) )
                StreamingCellsModel::selectObjects( vector<uint64>{ violation._fromObjectID, violation._toObjectID } );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorStreamingCells", "ViolationTip" ) );
            ImGui::TableSetColumnIndex( 1 );
            ImGui::TextUnformatted( StreamingCellsModel::findObjectName( violation._toObjectID ).c_str() );
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    void StreamingCellsPanel::drawCells( Scene& scene )
    {
        const StreamingCellPreview& preview  = _model.getPreview();
        const SceneSettings&        settings = scene.getSceneSettings();
        StreamingCellGrid           grid;
        if ( StreamingCellGrid::tryCreate( settings._cellGrid, grid ) == false )
            return; // 미리보기가 섰다면 격자는 쓸 수 있다 — 그 사이 설정을 고쳤다(오류는 로그에)
        const CameraComponent* pCamera = scene.getActiveGameCamera();

        if ( ImGui::SmallButton( "Always loaded" ) )
            StreamingCellsModel::selectObjects( preview._listAlwaysLoadedID );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorStreamingCells", "AlwaysLoadedTip" ) );
        ImGui::SameLine();
        ImGui::Text( "%u entities, %u cells", static_cast<uint32>( preview._listAlwaysLoadedID.size() ), static_cast<uint32>( preview._listCell.size() ) );
        if ( pCamera == nullptr )
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorStreamingCells", "NoGameCamera" ) );

        constexpr ImGuiTableFlags kTableFlags =
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
        if ( ImGui::BeginTable( "##world_cells", 3, kTableFlags, ImGui::GetContentRegionAvail() ) == false )
            return;
        ImGui::TableSetupColumn( "Cell (x, z)", ImGuiTableColumnFlags_WidthStretch, 1.0f );
        ImGui::TableSetupColumn( "Entities", ImGuiTableColumnFlags_WidthStretch, 0.6f );
        ImGui::TableSetupColumn( "From game camera", ImGuiTableColumnFlags_WidthStretch, 0.8f );
        ImGui::TableHeadersRow();
        for ( uint32 rowIndex = 0; rowIndex < static_cast<uint32>( preview._listCell.size() ); ++rowIndex )
        {
            const StreamingCellPreviewCell& cell  = preview._listCell[rowIndex];
            const StreamingCellPreviewState state = StreamingCellsModel::computeState( grid, settings, cell._cell, pCamera );
            ImGui::PushID( static_cast<int32>( rowIndex ) );
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex( 0 );
            fixed_string<constant::kMaxBuffer64> label;
            formatstring( label.data(), label.capacity(), "%#, %#", cell._cell._x, cell._cell._z );
            if ( ImGui::Selectable( label.c_str(), false, ImGuiSelectableFlags_SpanAllColumns ) )
                StreamingCellsModel::selectObjects( cell._listObjectID );
            if ( EditorSelfTestMarks::isEnabled() )
            {
                fixed_string<constant::kMaxBuffer64> mark;
                formatstring( mark.data(), mark.capacity(), "streamingCells.cell.%#", rowIndex );
                EditorSelfTestMarks::note( mark.c_str() );
            }
            ImGui::TableSetColumnIndex( 1 );
            ImGui::Text( "%u", static_cast<uint32>( cell._listObjectID.size() ) );
            ImGui::TableSetColumnIndex( 2 );
            const Color stateColor = state == StreamingCellPreviewState::Load ? EditorThemeUtil::getSuccessColor()
                                   : state == StreamingCellPreviewState::Keep ? EditorThemeUtil::getWarningColor()
                                                                              : EditorThemeUtil::getTextMutedColor();
            ImGui::TextColored( EditorColorUtil::toImVec4( stateColor ), "%s", StreamingCellsModel::getStateLabel( state ) );
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
} // namespace sw::editor
