#include "pch.h"

#include "Editor/Panels/BugIt/BugItPanel.h"

#include "Core/Common/Defines.h"
#include "Core/File/FileUtil.h"

#include "Editor/Common/Commands/Asset/EditorAssetFileCommands.h"
#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/EditorIconGlyphs.h"
#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/GUI/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorNotificationManager.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Localization/LocText.h"
#include "Engine/Renderer/Capture/BugItReport.h"
#include "Engine/Scene/SceneManager.h"

#include <algorithm>
#include <imgui.h>

namespace sw::editor
{
    SW_LOG_CALLER( "BugItPanel" );

    namespace
    {
        struct BugItPanelInternal
        {
            /** @brief BugIt 폴더들의 루트입니다(`BugItReport::makeInputs` 가 쓰는 경로와 같다). */
            static string makeRoot()
            {
                return FileUtil::joinPath( path::kSavedFolder, BugItReport::kFolderName );
            }

            /** @brief @p directory 의 절대 경로입니다. 바꾸지 못하면 그대로 돌려줍니다. */
            static string makeAbsolute( const string& directory )
            {
                string absolutePath;
                return FileUtil::makeAbsolutePath( directory, absolutePath ) ? absolutePath : directory;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_EDITOR_PANEL( BugItPanel, BugItPanel::kPanelID, EditorPanelCategory::Tool, 2045 );

    BugItPanel::BugItPanel()
        : IEditorPanel( false ) // 필요할 때 여는 도구라 닫힌 채 시작한다
        , _listRow{}
        , _note{}
        , _bRowsDirty{ true }
    {
    }

    bool BugItPanel::captureNow()
    {
        string     directory;
        const bool bCaptured         = BugItReport::capture( BugItReport::makeInputs( _note ), directory );
        _bRowsDirty                  = true;
        EditorContext& editorContext = editor::getSubsystem<EditorContext>();
        if ( bCaptured )
            editorContext.getSubsystem<EditorNotificationManager>().push( SW_LOCKEY( "EdCommon", "NotifyBugReportSaved" ), directory, NotificationType::Success );
        else
            editorContext.getSubsystem<EditorNotificationManager>().push( SW_LOCKEY( "EdCommon", "NotifyBugReportTitle" ), SW_LOCKEY( "EdCommon", "NotifyBugReportFailed" ), NotificationType::Error );
        return bCaptured;
    }

    void BugItPanel::refreshRows()
    {
        _bRowsDirty = false;
        _listRow.clear();
        vector<string> listFolder;
        if ( FileUtil::collectFolders( BugItPanelInternal::makeRoot(), listFolder, false ) == false )
            return; // 아직 한 번도 모으지 않았으면 루트가 없다
        // 이름이 yyyyMMdd-HHmmss(-n) 이라 이름 역순이 최근 순이다(BugItReport::findLatestDirectory 와 같은 규칙).
        std::sort( listFolder.begin(), listFolder.end(), []( const string& lhs, const string& rhs )
        { return rhs < lhs; } );
        _listRow.reserve( listFolder.size() );
        for ( const string& folder : listFolder )
        {
            BugItPanelRow row{};
            row._directory = folder;
            row._name      = FileUtil::getFileNamePart( folder );
            BugItReport report;
            if ( BugItReport::readInfo( folder, report ) )
                row._note = report._note;
            _listRow.push_back( std::move( row ) );
        }
    }

    void BugItPanel::drawRowActions( const BugItPanelRow& row ) const
    {
        if ( ImGui::SmallButton( editoricon::kFolderOpen ) )
            (void)EditorAssetFileCommands::showInFileExplorer( BugItPanelInternal::makeAbsolute( row._directory ) );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorBugIt", "ShowInExplorer" ) );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( editoricon::kCopy ) );
        if ( ImGui::SmallButton( editoricon::kCopy ) )
            ImGui::SetClipboardText( BugItPanelInternal::makeAbsolute( row._directory ).c_str() );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorBugIt", "CopyPath" ) );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( editoricon::kCamera ) );
        if ( ImGui::SmallButton( editoricon::kCamera ) )
        {
            SceneManager* pSceneManager = editor::findSubsystem<SceneManager>();
            string        reply;
            if ( pSceneManager == nullptr || BugItReport::goTo( *pSceneManager, row._directory, reply ) == false )
                SW_LOG_WARNING( "BugIt go to failed: %#", reply.c_str() );
            else
                SW_LOG_INFO( "%#", reply.c_str() );
        }
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorBugIt", "GoTo" ) );
    }

    void BugItPanel::drawContent()
    {
        if ( _bRowsDirty )
            refreshRows();

        (void)EditorWidgets::drawTextFieldMultiline( "##bugit_note", _note, 3 );
        EditorSelfTestMarks::note( "bugIt.note" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorBugIt", "NoteHint" ) );

        if ( ImGui::Button( EditorThemeUtil::makeIconLabel( editoricon::kBug, "Collect Now" ) ) && captureNow() )
            _note.clear(); // 모은 메모는 info.txt 에 남았다
        EditorSelfTestMarks::note( "bugIt.capture" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorBugIt", "Capture" ) );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( EditorThemeUtil::makeIconLabel( editoricon::kRefresh, "Refresh" ) ) );
        if ( ImGui::Button( EditorThemeUtil::makeIconLabel( editoricon::kRefresh, "Refresh" ) ) )
            _bRowsDirty = true;

        ImGui::Separator();
        if ( _listRow.empty() )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorBugIt", "NoReports" ) );
            return;
        }

        constexpr ImGuiTableFlags kTableFlags =
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
        if ( ImGui::BeginTable( "##bugit_rows", 3, kTableFlags, ImGui::GetContentRegionAvail() ) == false )
            return;
        const float32 dpiScale = EditorThemeUtil::getDpiScale();
        ImGui::TableSetupColumn( "Folder", ImGuiTableColumnFlags_WidthStretch, 1.0f );
        ImGui::TableSetupColumn( "Note", ImGuiTableColumnFlags_WidthStretch, 2.0f );
        ImGui::TableSetupColumn( "", ImGuiTableColumnFlags_WidthFixed, 96.0f * dpiScale );
        ImGui::TableHeadersRow();
        for ( uint32 rowIndex = 0; rowIndex < static_cast<uint32>( _listRow.size() ); ++rowIndex )
        {
            const BugItPanelRow& row = _listRow[rowIndex];
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
