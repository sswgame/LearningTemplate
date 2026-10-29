#include "pch.h"

#include "Editor/Panels/BugReport/BugReportPanel.h"

#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorIconGlyphs.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Localization/LocText.h"

#include <imgui.h>

namespace sw::editor
{
    SW_EDITOR_PANEL( BugReportPanel, BugReportPanel::kPanelID, EditorPanelCategory::Tool, 2045 );

    BugReportPanel::BugReportPanel()
        : IEditorPanel( false ) // 필요할 때 여는 도구라 닫힌 채 시작한다
        , _model{}
    {
    }

    void BugReportPanel::drawRowActions( const BugReportRow& row )
    {
        if ( ImGui::SmallButton( editoricon::kFolderOpen ) )
            BugReportModel::showInFileExplorer( row );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorBugReport", "ShowInExplorer" ) );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( editoricon::kCopy ) );
        if ( ImGui::SmallButton( editoricon::kCopy ) )
            ImGui::SetClipboardText( BugReportModel::makeAbsolutePath( row ).c_str() );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorBugReport", "CopyPath" ) );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( editoricon::kCamera ) );
        if ( ImGui::SmallButton( editoricon::kCamera ) )
            BugReportModel::goTo( row );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorBugReport", "GoTo" ) );
    }

    void BugReportPanel::drawContent()
    {
        _model.refreshIfDirty();

        (void)EditorWidgets::drawTextFieldMultiline( "##bugreport_note", _model.editNote(), 3 );
        EditorSelfTestMarks::note( "bugReport.note" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorBugReport", "NoteHint" ) );

        if ( ImGui::Button( EditorThemeUtil::makeIconLabel( editoricon::kBug, "Collect Now" ) ) )
            (void)_model.captureNow(); // 결과는 알림으로 알린다
        EditorSelfTestMarks::note( "bugReport.capture" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorBugReport", "Capture" ) );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( EditorThemeUtil::makeIconLabel( editoricon::kRefresh, "Refresh" ) ) );
        if ( ImGui::Button( EditorThemeUtil::makeIconLabel( editoricon::kRefresh, "Refresh" ) ) )
            _model.markDirty();

        ImGui::Separator();
        const vector<BugReportRow>& listRow = _model.getRows();
        if ( listRow.empty() )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorBugReport", "NoReports" ) );
            return;
        }

        constexpr ImGuiTableFlags kTableFlags =
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
        if ( ImGui::BeginTable( "##bugreport_rows", 3, kTableFlags, ImGui::GetContentRegionAvail() ) == false )
            return;
        const float32 dpiScale = EditorThemeUtil::getDPIScale();
        ImGui::TableSetupColumn( "Folder", ImGuiTableColumnFlags_WidthStretch, 1.0f );
        ImGui::TableSetupColumn( "Note", ImGuiTableColumnFlags_WidthStretch, 2.0f );
        ImGui::TableSetupColumn( "", ImGuiTableColumnFlags_WidthFixed, 96.0f * dpiScale );
        ImGui::TableHeadersRow();
        for ( uint32 rowIndex = 0; rowIndex < static_cast<uint32>( listRow.size() ); ++rowIndex )
        {
            const BugReportRow& row = listRow[rowIndex];
            ImGui::PushID( static_cast<int32>( rowIndex ) );
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex( 0 );
            ImGui::TextUnformatted( row._name.c_str() );
            ImGui::TableSetColumnIndex( 1 );
            ImGui::TextUnformatted( row._note.c_str() );
            ImGui::TableSetColumnIndex( 2 );
            drawRowActions( row );
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
} // namespace sw::editor
