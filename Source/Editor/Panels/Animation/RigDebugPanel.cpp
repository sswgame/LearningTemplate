/**
 * @file RigDebugPanel.cpp
 * @brief 리그 디버그 창 — 고른 오브젝트의 `PoseModifierComponent` 노드 목록(종류 · 대상 본 · 가중치 · 켜짐)을 보이고 가중치와 켜짐을 고칩니다.
 * @details 언리얼 Control Rig 의 디버그 보기와 Animation Blueprint 디버그, 유니티 Animation Rigging 의 Rig 가중치 · 제약 켜짐에 해당합니다.
 *          고친 값은 실행 중인 인스턴스의 노드 복제본에 들어가고, "Save to rig" 를 누르면 리그 에셋(`*.rig.xml`)의 노드 `weight` · `enabled` 에
 *          되써 넣는다(`RigSaveParams::writeToRigFile` — 다른 키는 그대로, 결과를 다시 읽어 규칙을 본다). 되돌리기는 문서 명령(`EditorTransaction::push`)으로 기록한다.
 *          고른 노드가 쓰는 본은 디버그 드로우 큐에 구와 이름으로 표시한다.
 *          대상 표에서 자기 유닛 대상(본 · 소켓)의 "Gizmo" 를 켜면 뷰포트 시각화 "Rig Target"(`RigTargetGizmoVisualizer`)이 그 대상에 기즈모를 놓고, 끌기를 놓으면 대상의
 *          `translation` · `rotation`(부모 본 · 소켓 공간)을 같은 되쓰기로 저장한다(파일 글 전후가 되돌리기 하나). 바깥 대상(다른 유닛 · 오브젝트)은 옮기지 않는다.
 *          읽기와 명령은 `RigDebugViewModel` 이 하고 이 창은 `RigDebugViewState` 만 그린다(MVVM 계약).
 */
#include "pch.h"

#include "Core/Foundation/Types.h"
#include "Core/String/fixed_string.h"
#include "Core/String/formatString.h"

#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanel.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Panels/Animation/Model/RigDebugViewModel.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"

#include <imgui.h>

namespace sw::editor
{
    /** @brief 리그 디버그 도구 창입니다. */
    class RigDebugPanel : public EditorPanel
    {
    public:
        /** @brief 패널 id 입니다. 등록(`SW_EDITOR_PANEL`)과 조회(`EditorPanelManager::findPanel<…>` · `setPanelOpen<…>`)가 모두 이 상수에서 온다. */
        static constexpr const utf8* kPanelID = "rig_debug";

        RigDebugPanel();
        virtual ~RigDebugPanel() override = default;

        const utf8* getPanelTitle() const override { return "Rig Debug"; }
        void        drawContent() override;

    private:
        /** @brief 노드 표를 그립니다(이름 · 종류 · 본 · 가중치 · 이번 프레임 값 · 켜짐). @p tableFlags 는 ImGuiTableFlags 입니다. */
        void drawNodeTable( int32 tableFlags );
        /** @brief 대상 표를 그립니다(이름 · 오프셋 · 기즈모). 자기 유닛 대상의 "Gizmo" 를 켜면 뷰포트 시각화가 그 대상에 기즈모를 놓습니다. */
        void drawTargetTable( int32 tableFlags );

    private:
        RigDebugViewModel      _viewModel;
        RigDebugViewState      _viewState;        ///< 프레임마다 `gather` 가 채운다
        float32                _weightBeforeEdit; ///< 가중치를 끌기 시작할 때의 값이다(되돌리기 기록용)
        uint8                  _bDrawMarkers : 1;
        [[maybe_unused]] uint8 _reserved     : 7;
    };

    SW_EDITOR_PANEL( RigDebugPanel, RigDebugPanel::kPanelID, EditorPanelCategory::Tool, 1660 );

    namespace
    {
        struct RigDebugPanelInternal
        {
            /** @brief @p nodeIndex 번 노드 줄의 칸 @p pCell 에 표식을 남깁니다(시나리오가 켜짐 · 가중치를 고친다). */
            static void recordNodeMark( uint32 nodeIndex, const utf8* pCell )
            {
                EditorSelfTestMarks::recordFormat( "rigdebug.node.%#.%#", nodeIndex, pCell );
            }
        };
    } // namespace

    RigDebugPanel::RigDebugPanel()
        : EditorPanel( false ) // 필요할 때 여는 도구라 닫힌 채 시작한다
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
            if ( EditorWidgets::sliderFloat( SW_WIDGET_ID( "##weight" ), &weight, 0.0f, 1.0f, "%.2f" ) )
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
        gatherViewState( _viewModel, _viewState );
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
