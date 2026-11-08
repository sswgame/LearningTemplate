#include "pch.h"

#include "Editor/Panels/BugReport/Model/BugReportViewModel.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Foundation/Constants.h"
#include "Core/Log/Logger.h"

#include "Editor/Common/Commands/Asset/EditorAssetFileCommands.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorNotificationManager.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Localization/LocText.h"
#include "Engine/Renderer/Capture/BugReport.h"
#include "Engine/Scene/SceneManager.h"

#include <algorithm>

namespace sw::editor
{
    namespace
    {
        struct BugReportViewModelInternal
        {
            /** @brief BugReport 폴더들의 루트입니다(`BugReport::makeInputs` 가 쓰는 경로와 같다). */
            static string makeRoot()
            {
                return FileUtil::joinPath( ProcessRoots::get()._saved.str(), BugReport::kFolderName );
            }
        };
    } // namespace

    BugReportViewModel::BugReportViewModel()
        : _listRow{}
        , _bRowsDirty{ SW_TRUE }
    {
    }

    void BugReportViewModel::gather( BugReportViewState& outState )
    {
        if ( _bRowsDirty )
        {
            _bRowsDirty = SW_FALSE;
            readRows();
        }
        outState._listRow = _listRow;
    }

    bool BugReportViewModel::captureNow( const string& note )
    {
        string     directory;
        const bool bCaptured         = BugReport::capture( BugReport::makeInputs( note ), directory );
        _bRowsDirty                  = SW_TRUE;
        EditorContext& editorContext = editor::getSubsystem<EditorContext>();
        if ( bCaptured )
            editorContext.getSubsystem<EditorNotificationManager>().push( SW_LOCKEY( "EdCommon", "NotifyBugReportSaved" ), directory, NotificationKind::Success );
        else
            editorContext.getSubsystem<EditorNotificationManager>().push( SW_LOCKEY( "EdCommon", "NotifyBugReportTitle" ), SW_LOCKEY( "EdCommon", "NotifyBugReportFailed" ), NotificationKind::Error );
        return bCaptured;
    }

    void BugReportViewModel::readRows()
    {
        _listRow.clear();
        vector<string> listFolder;
        if ( FileUtil::collectFolders( BugReportViewModelInternal::makeRoot(), listFolder, false ) == false )
            return; // 아직 한 번도 모으지 않았으면 루트가 없다
        // 이름이 yyyyMMdd-HHmmss(-n) 이라 이름 역순이 최근 순이다(BugReport::findLatestDirectory 와 같은 규칙).
        VectorUtil::sort( listFolder, []( const string& lhs, const string& rhs )
        { return rhs < lhs; } );
        _listRow.reserve( listFolder.size() );
        for ( const string& folder : listFolder )
        {
            BugReportRow row{};
            row._directory = folder;
            row._name      = FileUtil::getFileNamePart( folder );
            BugReport report;
            if ( BugReport::readInfo( folder, report ) )
                row._note = report._note;
            _listRow.push_back( std::move( row ) );
        }
    }

    void BugReportViewModel::showInFileExplorer( const string& directory )
    {
        (void)EditorAssetFileCommands::showInFileExplorer( directory );
    }

    void BugReportViewModel::goTo( const string& directory )
    {
        SceneManager* pSceneManager = editor::findSubsystem<SceneManager>();
        string        reply;
        if ( pSceneManager == nullptr || BugReport::goTo( *pSceneManager, directory, reply ) == false )
            SW_LOG_WARNING_ID( "BugReportPanel.BugReportGoFailed", "BugReport go to failed: %#", reply.c_str() );
        else
            SW_LOG_INFO_ID( "BugReportPanel.Reply", "%#", reply.c_str() );
    }
} // namespace sw::editor
