#include "pch.h"

#include "Editor/Panels/BugReport/BugReportPanel.h"

#include "Editor/Common/Config/EditorIconGlyphTable.h"
#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Localization/LocText.h"

#include <imgui.h>

namespace sw::editor
{
    SW_EDITOR_PANEL( BugReportPanel, BugReportPanel::kPanelID, EditorPanelCategory::Tool, 2045 );

    BugReportPanel::BugReportPanel()
        : IEditorPanel( false ) // 필요할 때 여는 도구라 닫힌 채 시작한다
        , _viewModel{}
        , _viewState{}
        , _note{}
    {
    }

    void BugReportPanel::drawRowActions( const BugReportRow& row )
    {
        if ( ImGui::SmallButton( SW_EDITOR_ICON( "FolderOpen" ) ) )
            BugReportViewModel::showInFileExplorer( row._directory );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorBugReport", "ShowInExplorer" ) );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_EDITOR_ICON( "Copy" ) ) );
        if ( ImGui::SmallButton( SW_EDITOR_ICON( "Copy" ) ) )
            ImGui::SetClipboardText( row._directory.c_str() );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorBugReport", "CopyPath" ) );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_EDITOR_ICON( "Camera" ) ) );
        if ( ImGui::SmallButton( SW_EDITOR_ICON( "Camera" ) ) )
            BugReportViewModel::goTo( row._directory );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorBugReport", "GoTo" ) );
    }

    void BugReportPanel::drawContent()
    {
        _viewModel.gather( _viewState );

        (void)EditorWidgets::drawTextFieldMultiline( "##bugreport_note", _note, 3, SW_LOCKEY( "EditorBugReport", "NoteHint" ) );
        EditorSelfTestMarks::record( "bugReport.note" );

        if ( ImGui::Button( EditorThemeUtil::makeIconLabel( SW_EDITOR_ICON( "Bug" ), SW_LOCKEY( "EdButton", "CollectNow" ), "Collect Now" ) ) )
        {
            if ( _viewModel.captureNow( _note ) ) // 결과는 알림으로 알린다
                _note.clear();                    // 모은 메모는 info.txt 에 남았다
        }
        EditorSelfTestMarks::record( "bugReport.capture" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorBugReport", "Capture" ) );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( EditorThemeUtil::makeIconLabel( SW_EDITOR_ICON( "Refresh" ), SW_LOCKEY( "EdVerb", "Refresh" ), "Refresh" ) ) );
        if ( ImGui::Button( EditorThemeUtil::makeIconLabel( SW_EDITOR_ICON( "Refresh" ), SW_LOCKEY( "EdVerb", "Refresh" ), "Refresh" ) ) )
            _viewModel.refresh();

        ImGui::Separator();
        const vector<BugReportRow>& listRow = _viewState._listRow;
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
