#include "pch.h"

#include "Editor/Panels/Animation/RigDebugPanel.h"

#include "Core/String/fixed_string.h"
#include "Core/String/formatString.h"

#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"

#include <imgui.h>

namespace sw::editor
{
    SW_EDITOR_PANEL( RigDebugPanel, RigDebugPanel::kPanelID, EditorPanelCategory::Tool, 1660 );

    namespace
    {
        struct RigDebugPanelInternal
        {
            /** @brief @p nodeIndex 번 노드 줄의 칸 @p pCell 에 표식을 남깁니다(시나리오가 켜짐 · 가중치를 고친다). */
            static void recordNodeMark( uint32 nodeIndex, const utf8* pCell )
            {
                fixed_string<constant::buffer::kMax64> mark;
                formatstring( mark.data(), mark.capacity(), "rigdebug.node.%#.%#", nodeIndex, pCell );
                EditorSelfTestMarks::record( mark.c_str() );
            }
        };
    } // namespace

    RigDebugPanel::RigDebugPanel()
        : IEditorPanel( false ) // 필요할 때 여는 도구라 닫힌 채 시작한다
        , _viewModel{}
        , _viewState{}
        , _weightBeforeEdit{ 0.0f }
        , _bDrawMarkers{ SW_TRUE }
        , _reserved{ 0 }
    {
    }

    void RigDebugPanel::drawNodeTable( int32 tableFlags )
    {
        if ( ImGui::BeginTable( "##rig_nodes", 6, tableFlags ) == false )
            return;
        ImGui::TableSetupColumn( "Node" );
        ImGui::TableSetupColumn( "Type" );
        ImGui::TableSetupColumn( "Bones" );
        ImGui::TableSetupColumn( "Weight" );
        ImGui::TableSetupColumn( "Effective" );
        ImGui::TableSetupColumn( "On" );
        ImGui::TableHeadersRow();
        for ( uint32 nodeIndex = 0; nodeIndex < static_cast<uint32>( _viewState._listNode.size() ); ++nodeIndex )
        {
            const RigDebugNodeRow& node = _viewState._listNode[nodeIndex];
            ImGui::PushID( static_cast<int32>( nodeIndex ) );
            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            const bool bSelected = _viewState._selectedNode == nodeIndex;
            if ( ImGui::Selectable( node._name.c_str(), bSelected ) )
                _viewModel.toggleNodeSelection( nodeIndex );

            ImGui::TableNextColumn();
            ImGui::TextUnformatted( node._typeName.c_str() );

            ImGui::TableNextColumn();
            ImGui::TextUnformatted( node._bones.c_str() );

            ImGui::TableNextColumn();
            float32 weight = node._weight;
            ImGui::SetNextItemWidth( -1.0f );
            if ( ImGui::SliderFloat( "##weight", &weight, 0.0f, 1.0f, "%.2f" ) )
                _viewModel.setNodeWeight( nodeIndex, weight );
            if ( ImGui::IsItemActivated() )
                _weightBeforeEdit = node._weight;
            if ( ImGui::IsItemDeactivatedAfterEdit() )
                _viewModel.commitNodeWeight( nodeIndex, _weightBeforeEdit );
            RigDebugPanelInternal::recordNodeMark( nodeIndex, "weight" );

            ImGui::TableNextColumn();
            ImGui::Text( "%.2f", static_cast<float64>( node._effective ) );
            if ( node._weightSlot.empty() == false )
            {
                ImGui::SameLine();
                ImGui::TextDisabled( "[%s]", node._weightSlot.c_str() );
            }

            ImGui::TableNextColumn();
            bool bEnabled = node._bEnabled != SW_FALSE;
            if ( ImGui::Checkbox( "##on", &bEnabled ) )
                _viewModel.setNodeEnabled( nodeIndex, bEnabled );
            RigDebugPanelInternal::recordNodeMark( nodeIndex, "on" );
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    void RigDebugPanel::drawTargetTable( int32 tableFlags )
    {
        // 대상 — 자기 유닛 대상(본 · 소켓)을 고르면 뷰포트에 기즈모가 뜬다(시각화 "Rig Target"). 끌기를 놓으면 그 대상의 오프셋을 리그 파일에 되써 넣는다.
        const uint32 targetCount = static_cast<uint32>( _viewState._listTarget.size() );
        if ( targetCount > 0 && ImGui::BeginTable( "##rig_targets", 3, tableFlags ) )
        {
            ImGui::TableSetupColumn( "Target" );
            ImGui::TableSetupColumn( "Translation" );
            ImGui::TableSetupColumn( "Gizmo" );
            ImGui::TableHeadersRow();
            for ( uint32 targetIndex = 0; targetIndex < targetCount; ++targetIndex )
            {
                const RigDebugTargetRow& target = _viewState._listTarget[targetIndex];
                ImGui::PushID( static_cast<int32>( targetIndex ) );
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted( target._name.c_str() );
                ImGui::TableNextColumn();
                ImGui::Text( "%.3f, %.3f, %.3f", static_cast<float64>( target._translation._x ), static_cast<float64>( target._translation._y ),
                             static_cast<float64>( target._translation._z ) );
                ImGui::TableNextColumn();
                ImGui::BeginDisabled( target._bOwnUnit == SW_FALSE || _viewState._bCanSave == SW_FALSE );
                bool bEdit = target._bGizmoOn != SW_FALSE;
                if ( ImGui::Checkbox( "##gizmo", &bEdit ) )
                    _viewModel.setTargetGizmo( targetIndex, bEdit );
                ImGui::EndDisabled();
                if ( target._bOwnUnit == SW_FALSE )
                    EditorWidgets::drawTooltip( SW_LOCKEY( "EdTip", "TargetsOnOtherUnitsOrObjectsAre" ) );
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        EditorSelfTestMarks::record( "rigdebug.targets" );
        if ( _viewState._targetStatus.empty() == false )
            ImGui::TextDisabled( "%s", SW_LOCKEYFORMAT( "EdFormat", "Target", TextArgumentList().addText( "target", _viewState._targetStatus ) ).c_str() );
    }

    void RigDebugPanel::drawContent()
    {
        _viewModel.gather( _viewState );
        if ( _viewState._content == RigDebugContent::NoRig )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorRigDebug", "SelectRig" ) );
            return;
        }
        if ( _viewState._content == RigDebugContent::RigNotReady )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorRigDebug", "RigNotReady" ) );
            return;
        }

        bool bDrawMarkers = _bDrawMarkers == SW_TRUE;
        if ( ImGui::Checkbox( SW_LOCKEY( "EdLabel", "BoneMarkers" ), &bDrawMarkers ) )
            _bDrawMarkers = bDrawMarkers ? SW_TRUE : SW_FALSE;
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorRigDebug", "BoneMarkersTip" ) );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "SaveToRig" ) ) );
        ImGui::BeginDisabled( _viewState._bCanSave == SW_FALSE );
        if ( ImGui::Button( SW_LOCKEY( "EdText", "SaveToRig" ) ) )
            _viewModel.saveToRig();
        ImGui::EndDisabled();
        EditorSelfTestMarks::record( "rigdebug.save" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EdTip", "WriteTheWeightsAndOnOffOf" ) );

        const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;
        drawNodeTable( flags );
        drawTargetTable( flags );

        EditorWidgets::drawPanelStatus( _viewState._status.c_str() );
        if ( _bDrawMarkers == SW_TRUE )
            _viewModel.submitBoneMarkers( SW_THEME_COLOR( "Viewport", "Marker" ).toFloat4() );
    }
} // namespace sw::editor
