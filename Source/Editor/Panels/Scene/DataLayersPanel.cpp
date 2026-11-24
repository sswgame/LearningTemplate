/**
 * @file DataLayersPanel.cpp
 * @brief 데이터 레이어 창 — 활성 씬의 레이어를 보고 전환합니다(언리얼 Data Layers Outliner 자리). 값은 `DataLayersViewModel::gather` 가 모으고 명령도 그쪽이 받으며, 이 패널은 그리기만 합니다(MVVM 계약).
 */
#include "pch.h"

#include "Core/Foundation/Types.h"

#include "Editor/Common/GUI/Panel/EditorPanel.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Panels/Scene/Model/DataLayersViewModel.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Localization/LocText.h"

#include <imgui.h>

namespace sw::editor
{
    /** @brief 데이터 레이어 도구 창입니다. */
    class DataLayersPanel : public EditorPanel
    {
    public:
        /** @brief 패널 id 입니다(`SW_EDITOR_PANEL`, `-gv_editorOpenPanel`). */
        static constexpr const utf8* kPanelID = "data_layers";

        /** @brief 창을 만듭니다(닫힌 채 시작한다). */
        DataLayersPanel();

        const utf8* getPanelTitle() const override { return "Data Layers"; }
        void        drawContent() override;

    private:
        DataLayersViewModel _viewModel;
        DataLayersViewState _viewState; ///< 프레임마다 다시 채운다(목록 버퍼를 다시 쓴다)
    };

    SW_EDITOR_PANEL( DataLayersPanel, DataLayersPanel::kPanelID, EditorPanelCategory::Tool, 2043 );

    DataLayersPanel::DataLayersPanel()
        : EditorPanel( false ) // 필요할 때 여는 도구라 닫힌 채 시작한다
        , _viewModel{}
        , _viewState{}
    {
    }

    void DataLayersPanel::drawContent()
    {
        gatherViewState( _viewModel, _viewState );
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
            EditorSelfTestMarks::recordFormat( "dataLayers.shown.%#", row._name );
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
