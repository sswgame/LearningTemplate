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
    namespace
    {
        /** @brief 두 표(씬 · 문서)가 함께 쓰는 열 크기입니다 — 같은 열은 같은 폭으로 줄이 맞는다. 고정 폭은 DPI 1 기준 픽셀이다. */
        struct RecoveredScenePanelInternal
        {
            static constexpr float32 kNameWeight   = 2.0f;   ///< 씬 · 문서 id 열의 늘어나는 몫
            static constexpr float32 kStatusWeight = 1.6f;   ///< 상태(거절 이유) 열의 늘어나는 몫
            static constexpr float32 kSavedWidth   = 90.0f;  ///< "4 min ago" 열
            static constexpr float32 kCopiesWidth  = 56.0f;  ///< 세대 수 열(씬만)
            static constexpr float32 kActionWidth  = 150.0f; ///< Restore · Discard 단추 열
        };
    } // namespace

    SW_EDITOR_PANEL( RecoveredScenePanel, RecoveredScenePanel::kPanelID, EditorPanelCategory::Tool, 2046 );

    RecoveredScenePanel::RecoveredScenePanel()
        : IEditorPanel( false ) // 후보가 있을 때 자동 저장이 연다
        , _viewState{}
    {
    }

    void RecoveredScenePanel::drawContent()
    {
        RecoveredSceneViewModel::gather( _viewState );
        if ( _viewState._listRow.empty() && _viewState._listDocumentRow.empty() )
        {
            // 빈 칸은 틀을 두고 왜 비었는지 한 문장으로 말한다
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorAutosave", "NothingToRecover" ) );
            return;
        }
        ImGui::TextWrapped( "%s", SW_LOCKEY( "EditorAutosave", "RecoveryIntro" ) );
        if ( _viewState._listRow.empty() == false )
            drawSceneRows();
        if ( _viewState._listDocumentRow.empty() == false )
            drawDocumentRows();
    }

    void RecoveredScenePanel::drawSceneRows()
    {
        const vector<RecoveredSceneRow>& listRow = _viewState._listRow;
        // 줄은 선과 정렬로만 나눈다(얼룩 줄 없음). 수는 오른쪽 정렬. 표는 내용 높이다 — 문서 표가 아래에 이어진다(창이 넘치면 창이 스크롤한다)
        constexpr ImGuiTableFlags kTableFlags = ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp;
        if ( ImGui::BeginTable( "##recovered_rows", 5, kTableFlags ) == false )
            return;
        const float32 dpiScale = EditorThemeUtil::getDPIScale();
        ImGui::TableSetupColumn( "Scene", ImGuiTableColumnFlags_WidthStretch, RecoveredScenePanelInternal::kNameWeight );
        ImGui::TableSetupColumn( "Saved", ImGuiTableColumnFlags_WidthFixed, RecoveredScenePanelInternal::kSavedWidth * dpiScale );
        ImGui::TableSetupColumn( "Copies", ImGuiTableColumnFlags_WidthFixed, RecoveredScenePanelInternal::kCopiesWidth * dpiScale );
        ImGui::TableSetupColumn( "Status", ImGuiTableColumnFlags_WidthStretch, RecoveredScenePanelInternal::kStatusWeight );
        ImGui::TableSetupColumn( "", ImGuiTableColumnFlags_WidthFixed, RecoveredScenePanelInternal::kActionWidth * dpiScale );
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

    void RecoveredScenePanel::drawDocumentRows()
    {
        const vector<RecoveredDocumentRow>& listRow = _viewState._listDocumentRow;
        ImGui::SeparatorText( SW_LOCKEY( "EditorAutosave", "DocumentsHeading" ) );
        // 씬 표와 같은 모양이다(선과 정렬로만 나눈다). 세대 열이 없다 — 문서는 가장 새 사본 하나만 둔다
        constexpr ImGuiTableFlags kTableFlags = ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp;
        if ( ImGui::BeginTable( "##recovered_documents", 4, kTableFlags ) == false )
            return;
        const float32 dpiScale = EditorThemeUtil::getDPIScale();
        ImGui::TableSetupColumn( "Document", ImGuiTableColumnFlags_WidthStretch, RecoveredScenePanelInternal::kNameWeight );
        ImGui::TableSetupColumn( "Saved", ImGuiTableColumnFlags_WidthFixed, RecoveredScenePanelInternal::kSavedWidth * dpiScale );
        ImGui::TableSetupColumn( "Status", ImGuiTableColumnFlags_WidthStretch, RecoveredScenePanelInternal::kStatusWeight );
        ImGui::TableSetupColumn( "", ImGuiTableColumnFlags_WidthFixed, RecoveredScenePanelInternal::kActionWidth * dpiScale );
        ImGui::TableHeadersRow();

        uint32 restoreIndex = MathUtil::kMaxUInt32; // 그린 뒤에 부른다(명령이 목록을 바꾼다)
        uint32 discardIndex = MathUtil::kMaxUInt32;
        for ( uint32 rowIndex = 0; rowIndex < static_cast<uint32>( listRow.size() ); ++rowIndex )
        {
            const RecoveredDocumentRow& row = listRow[rowIndex];
            ImGui::PushID( static_cast<int32>( rowIndex ) );
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex( 0 );
            ImGui::TextUnformatted( row._documentPath.c_str() );
            ImGui::TableSetColumnIndex( 1 );
            ImGui::SetCursorPosX( ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize( row._savedAge.c_str() ).x );
            ImGui::TextUnformatted( row._savedAge.c_str() );
            ImGui::TableSetColumnIndex( 2 );
            if ( row._refusal.empty() )
                ImGui::TextDisabled( "%s", SW_LOCKEY( "EditorAutosave", "CanRestore" ) );
            else
                ImGui::TextColored( EditorColorUtil::toImVec4( SW_THEME_COLOR( "Signal", "Warning" ) ), "%s", row._refusal.c_str() ); // 색과 함께 이유를 글로 적는다
            ImGui::TableSetColumnIndex( 3 );
            ImGui::BeginDisabled( row._refusal.empty() == false );
            if ( ImGui::SmallButton( SW_LOCKEY( "EdButton", "Restore" ) ) )
                restoreIndex = rowIndex;
            ImGui::EndDisabled();
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorAutosave", "RestoreDocumentTooltip" ) );
            if ( EditorSelfTestMarks::isEnabled() )
            {
                fixed_string<constant::buffer::kMax64> restoreMark;
                formatstring( restoreMark.data(), restoreMark.capacity(), "recoveredWork.restoreDocument.%#", rowIndex );
                EditorSelfTestMarks::record( restoreMark.c_str() );
            }
            ImGui::SameLine();
            if ( ImGui::SmallButton( SW_LOCKEY( "EdButton", "Discard" ) ) )
                discardIndex = rowIndex;
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorAutosave", "DiscardDocumentTooltip" ) );
            if ( EditorSelfTestMarks::isEnabled() )
            {
                fixed_string<constant::buffer::kMax64> discardMark;
                formatstring( discardMark.data(), discardMark.capacity(), "recoveredWork.discardDocument.%#", rowIndex );
                EditorSelfTestMarks::record( discardMark.c_str() );
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
        if ( restoreIndex != MathUtil::kMaxUInt32 )
            RecoveredSceneViewModel::restoreDocument( restoreIndex );
        else if ( discardIndex != MathUtil::kMaxUInt32 )
            RecoveredSceneViewModel::discardDocument( discardIndex );
    }
} // namespace sw::editor
