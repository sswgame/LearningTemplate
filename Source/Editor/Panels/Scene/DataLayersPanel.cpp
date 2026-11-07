#include "pch.h"

#include "Editor/Panels/Scene/DataLayersPanel.h"

#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Localization/LocText.h"

#include <imgui.h>

namespace sw::editor
{
    SW_EDITOR_PANEL( DataLayersPanel, DataLayersPanel::kPanelID, EditorPanelCategory::Tool, 2043 );

    DataLayersPanel::DataLayersPanel()
        : IEditorPanel( false ) // 필요할 때 여는 도구라 닫힌 채 시작한다
        , _viewModel{}
        , _viewState{}
    {
    }

    void DataLayersPanel::drawContent()
    {
        _viewModel.gather( _viewState );
        if ( _viewState._content == DataLayersContent::NoActiveScene )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorDataLayers", "NoActiveScene" ) );
            return;
        }
        if ( _viewState._content == DataLayersContent::NoLayers )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorDataLayers", "NoLayers" ) );
            return;
        }

        constexpr ImGuiTableFlags kTableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
        if ( ImGui::BeginTable( "##data_layers", 4, kTableFlags, ImGui::GetContentRegionAvail() ) == false )
            return;
        ImGui::TableSetupColumn( "Layer", ImGuiTableColumnFlags_WidthStretch, 1.0f );
        ImGui::TableSetupColumn( "Shown", ImGuiTableColumnFlags_WidthStretch, 0.4f );
        ImGui::TableSetupColumn( "On at load", ImGuiTableColumnFlags_WidthStretch, 0.4f );
        ImGui::TableSetupColumn( "Objects", ImGuiTableColumnFlags_WidthStretch, 0.4f );
        ImGui::TableHeadersRow();
        for ( const DataLayerRow& row : _viewState._listRow )
        {
            ImGui::PushID( row._name.c_str() );
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex( 0 );
            ImGui::TextUnformatted( row._name.c_str() );
            ImGui::TableSetColumnIndex( 1 );
            bool bShown = row._bShown != SW_FALSE;
            if ( ImGui::Checkbox( "##shown", &bShown ) )
                _viewModel.setLayerShown( row._name, bShown );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorDataLayers", "ShownTip" ) );
            if ( EditorSelfTestMarks::isEnabled() )
                EditorSelfTestMarks::record( ( string( "dataLayers.shown." ) + row._name ).c_str() );
            ImGui::TableSetColumnIndex( 2 );
            bool bInitiallyActive = row._bInitiallyActive != SW_FALSE;
            if ( ImGui::Checkbox( "##initial", &bInitiallyActive ) )
                _viewModel.setLayerInitiallyActive( row._name, bInitiallyActive );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorDataLayers", "InitialTip" ) );
            ImGui::TableSetColumnIndex( 3 );
            ImGui::Text( "%u", row._objectCount );
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
} // namespace sw::editor
