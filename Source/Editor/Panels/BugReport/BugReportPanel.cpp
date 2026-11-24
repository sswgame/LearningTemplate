/**
 * @file BugReportPanel.cpp
 * @brief 버그 리포트 창입니다. 메모를 붙여 지금 상태를 `Saved/BugReports/<시각>/` 에 모으고, 지난 폴더를 열거나 경로를 복사하거나 그 위치로 돌아갑니다(언리얼 BugReport).
 * @details 모으기는 커맨드 `help.reportBug` 와 같은 `BugReport::makeInputs` → `BugReport::capture` 이고, 이 창은 메모 칸과 지난 폴더 목록만 더합니다.
 *          줄은 `BugReportViewModel::gather` 가 모으고 명령도 그쪽이 받는다. 이 패널은 메모 편집 버퍼 · 그리기 · 경로 복사만 한다(MVVM 계약).
 */
#include "pch.h"

#include "Core/Foundation/Types.h"

#include "Editor/Common/Config/EditorIconGlyphTable.h"
#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanel.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Panels/BugReport/Model/BugReportViewModel.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Localization/LocText.h"

#include <imgui.h>

namespace sw::editor
{
    /** @brief 메모 입력 · 지금 상태 모으기 · 지난 BugReport 폴더 목록을 보이는 도구 창입니다. */
    class BugReportPanel : public EditorPanel
    {
    public:
        /** @brief 패널 id 입니다(`SW_EDITOR_PANEL`, `-gv_editorOpenPanel`). */
        static constexpr const utf8* kPanelID = "bug_it";

        /** @brief 버그 리포트 창을 만듭니다(닫힌 채 시작한다). */
        BugReportPanel();

        /** @brief 창 제목입니다. */
        const utf8* getPanelTitle() const override { return "Bug Report"; }
        /** @brief 메모 필드 · 모으기 단추 · 폴더 목록을 그립니다. */
        void drawContent() override;

    private:
        /** @brief 폴더 줄 @p row 의 단추(탐색기 · 경로 복사 · 그 위치로)를 그립니다. */
        static void drawRowActions( const BugReportRow& row );

    private:
        BugReportViewModel _viewModel;
        BugReportViewState _viewState; ///< 프레임마다 다시 채운다
        string             _note;      ///< 메모 칸의 편집 버퍼(모으면 비운다)
    };

    SW_EDITOR_PANEL( BugReportPanel, BugReportPanel::kPanelID, EditorPanelCategory::Tool, 2045 );

    BugReportPanel::BugReportPanel()
        : EditorPanel( false ) // 필요할 때 여는 도구라 닫힌 채 시작한다
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
        gatherViewState( _viewModel, _viewState );

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
