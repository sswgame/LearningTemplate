#include "pch.h"

#include "Editor/Panels/Autosave/RecoveredWorkPanel.h"

#include "Core/File/FileUtil.h"
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
    namespace
    {
        struct RecoveredWorkPanelInternal
        {
            /** @brief 사본을 쓴 지 얼마나 됐는지를 사람이 읽는 글로 냅니다("4 min ago"). 시각을 모르면 회색 대시입니다. */
            static string formatAge( int64 savedTicks )
            {
                if ( savedTicks <= 0 )
                    return string( "-" );
                const int64 seconds = ( FileUtil::getCurrentFileWriteTime() - savedTicks ) / FileUtil::kFileTimeTicksPerSecond;
                if ( seconds < 60 )
                    return formatToString( "%# s ago", seconds < 0 ? 0 : seconds );
                if ( seconds < 3600 )
                    return formatToString( "%# min ago", seconds / 60 );
                return formatToString( "%# h ago", seconds / 3600 );
            }
        };
    } // namespace

    SW_EDITOR_PANEL( RecoveredWorkPanel, RecoveredWorkPanel::kPanelID, EditorPanelCategory::Tool, 2046 );

    RecoveredWorkPanel::RecoveredWorkPanel()
        : IEditorPanel( false ) // 후보가 있을 때 자동 저장이 연다
    {
    }

    void RecoveredWorkPanel::drawContent()
    {
        const vector<EditorAutosaveEntry>& listEntry = EditorAutosaveCommands::getRecoverable();
        if ( listEntry.empty() )
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

        uint32 restoreIndex = MathUtil::kMaxUInt32; // 그린 뒤에 적용한다(적용이 목록을 바꾼다)
        uint32 discardIndex = MathUtil::kMaxUInt32;
        for ( uint32 rowIndex = 0; rowIndex < static_cast<uint32>( listEntry.size() ); ++rowIndex )
        {
            const EditorAutosaveEntry& entry = listEntry[rowIndex];
            ImGui::PushID( static_cast<int32>( rowIndex ) );
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex( 0 );
            ImGui::TextUnformatted( entry._sourceID.c_str() );
            ImGui::TableSetColumnIndex( 1 );
            const string age = RecoveredWorkPanelInternal::formatAge( entry._savedTicks );
            ImGui::SetCursorPosX( ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize( age.c_str() ).x );
            ImGui::TextUnformatted( age.c_str() );
            ImGui::TableSetColumnIndex( 2 );
            fixed_string<constant::buffer::kMax32> copies;
            formatstring( copies.data(), copies.capacity(), "%#", entry._generationCount );
            ImGui::SetCursorPosX( ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize( copies.c_str() ).x );
            ImGui::TextUnformatted( copies.c_str() );
            ImGui::TableSetColumnIndex( 3 );
            if ( entry._refusal.empty() )
                ImGui::TextDisabled( "%s", SW_LOCKEY( "EditorAutosave", "CanRestore" ) );
            else
                ImGui::TextColored( EditorColorUtil::toImVec4( SW_THEME_COLOR( "Signal", "Warning" ) ), "%s", entry._refusal.c_str() ); // 색과 함께 이유를 글로 적는다
            ImGui::TableSetColumnIndex( 4 );
            ImGui::BeginDisabled( entry._refusal.empty() == false );
            if ( ImGui::SmallButton( SW_LOCKEY( "EdButton", "Restore" ) ) )
                restoreIndex = rowIndex;
            ImGui::EndDisabled();
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorAutosave", "RestoreTooltip" ) );
            if ( EditorSelfTestMarks::isEnabled() )
                EditorSelfTestMarks::record( formatToString( "recoveredWork.restore.%#", rowIndex ).c_str() );
            ImGui::SameLine();
            if ( ImGui::SmallButton( SW_LOCKEY( "EdButton", "Discard" ) ) )
                discardIndex = rowIndex;
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorAutosave", "DiscardTooltip" ) );
            if ( EditorSelfTestMarks::isEnabled() )
                EditorSelfTestMarks::record( formatToString( "recoveredWork.discard.%#", rowIndex ).c_str() );
            ImGui::PopID();
        }
        ImGui::EndTable();
        if ( restoreIndex != MathUtil::kMaxUInt32 )
            (void)EditorAutosaveCommands::restore( restoreIndex ); // 거절 이유는 restore 가 알린다
        else if ( discardIndex != MathUtil::kMaxUInt32 )
            EditorAutosaveCommands::discard( discardIndex );
    }
} // namespace sw::editor
