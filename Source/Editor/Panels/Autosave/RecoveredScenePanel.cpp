#include "pch.h"

#include "Editor/Panels/Autosave/RecoveredScenePanel.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/fixed_string.h"

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
    SW_EDITOR_PANEL( RecoveredScenePanel, RecoveredScenePanel::kPanelID, EditorPanelCategory::Tool, 2046 );

    RecoveredScenePanel::RecoveredScenePanel()
        : IEditorPanel( false ) // 후보가 있을 때 자동 저장이 연다
        , _viewState{}
    {
    }

    void RecoveredScenePanel::drawContent()
    {
        RecoveredSceneViewModel::gather( _viewState );
        const vector<RecoveredSceneRow>& listRow = _viewState._listRow;
        if ( listRow.empty() )
        {
            // 빈 칸은 틀을 두고 왜 비었는지 한 문장으로 말한다
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorAutosave", "NothingToRecover" ) );
            return;
        }
        ImGui::TextWrapped( "%s", SW_LOCKEY( "EditorAutosave", "RecoveryIntro" ) );

        // 줄은 선과 정렬로만 나눈다(얼룩 줄 없음). 수는 오른쪽 정렬
        constexpr ImGuiTableFlags kTableFlags = ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
        if ( ImGui::BeginTable( "##recovered_rows", 5, kTableFlags, ImGui::GetContentRegionAvail() ) == false )
            return;
        const float32 dpiScale = EditorThemeUtil::getDPIScale();
        ImGui::TableSetupColumn( "Scene", ImGuiTableColumnFlags_WidthStretch, 2.0f );
        ImGui::TableSetupColumn( "Saved", ImGuiTableColumnFlags_WidthFixed, 90.0f * dpiScale );
        ImGui::TableSetupColumn( "Copies", ImGuiTableColumnFlags_WidthFixed, 56.0f * dpiScale );
        ImGui::TableSetupColumn( "Status", ImGuiTableColumnFlags_WidthStretch, 1.6f );
        ImGui::TableSetupColumn( "", ImGuiTableColumnFlags_WidthFixed, 150.0f * dpiScale );
        ImGui::TableHeadersRow();

        uint32 restoreIndex = MathUtil::kMaxUInt32; // 그린 뒤에 부른다(명령이 목록을 바꾼다)
        uint32 discardIndex = MathUtil::kMaxUInt32;
        for ( uint32 rowIndex = 0; rowIndex < static_cast<uint32>( listRow.size() ); ++rowIndex )
        {
            const RecoveredSceneRow& row = listRow[rowIndex];
            ImGui::PushID( static_cast<int32>( rowIndex ) );
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex( 0 );
            ImGui::TextUnformatted( row._sceneID.c_str() );
            ImGui::TableSetColumnIndex( 1 );
            ImGui::SetCursorPosX( ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize( row._savedAge.c_str() ).x );
            ImGui::TextUnformatted( row._savedAge.c_str() );
            ImGui::TableSetColumnIndex( 2 );
            fixed_string<constant::buffer::kMax32> copies;
            formatstring( copies.data(), copies.capacity(), "%#", row._copyCount );
            ImGui::SetCursorPosX( ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize( copies.c_str() ).x );
            ImGui::TextUnformatted( copies.c_str() );
            ImGui::TableSetColumnIndex( 3 );
            if ( row._refusal.empty() )
                ImGui::TextDisabled( "%s", SW_LOCKEY( "EditorAutosave", "CanRestore" ) );
            else
                ImGui::TextColored( EditorColorUtil::toImVec4( SW_THEME_COLOR( "Signal", "Warning" ) ), "%s", row._refusal.c_str() ); // 색과 함께 이유를 글로 적는다
            ImGui::TableSetColumnIndex( 4 );
            ImGui::BeginDisabled( row._refusal.empty() == false );
            if ( ImGui::SmallButton( SW_LOCKEY( "EdButton", "Restore" ) ) )
                restoreIndex = rowIndex;
            ImGui::EndDisabled();
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorAutosave", "RestoreTooltip" ) );
            if ( EditorSelfTestMarks::isEnabled() )
            {
                fixed_string<constant::buffer::kMax64> text2;
                formatstring( text2.data(), text2.capacity(), "recoveredWork.restore.%#", rowIndex );
                EditorSelfTestMarks::record( text2.c_str() );
            }
            ImGui::SameLine();
            if ( ImGui::SmallButton( SW_LOCKEY( "EdButton", "Discard" ) ) )
                discardIndex = rowIndex;
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorAutosave", "DiscardTooltip" ) );
            if ( EditorSelfTestMarks::isEnabled() )
            {
                fixed_string<constant::buffer::kMax64> text;
                formatstring( text.data(), text.capacity(), "recoveredWork.discard.%#", rowIndex );
                EditorSelfTestMarks::record( text.c_str() );
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
        if ( restoreIndex != MathUtil::kMaxUInt32 )
            RecoveredSceneViewModel::restore( restoreIndex );
        else if ( discardIndex != MathUtil::kMaxUInt32 )
            RecoveredSceneViewModel::discard( discardIndex );
    }
} // namespace sw::editor
