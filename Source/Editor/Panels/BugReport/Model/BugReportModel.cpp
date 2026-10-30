#include "pch.h"

#include "Editor/Panels/BugReport/Model/BugReportModel.h"

#include "Core/Container/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Foundation/Defines.h"
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
    SW_LOG_CALLER( "BugReportPanel" );

    namespace
    {
        struct BugReportModelInternal
        {
            /** @brief BugReport 폴더들의 루트입니다(`BugReport::makeInputs` 가 쓰는 경로와 같다). */
            static string makeRoot()
            {
                return FileUtil::joinPath( FileUtil::getSavedFolderPath(), BugReport::kFolderName );
            }
        };
    } // namespace

    BugReportModel::BugReportModel()
        : _listRow{}
        , _note{}
        , _bRowsDirty{ true }
    {
    }

    bool BugReportModel::captureNow()
    {
        string     directory;
        const bool bCaptured         = BugReport::capture( BugReport::makeInputs( _note ), directory );
        _bRowsDirty                  = true;
        EditorContext& editorContext = editor::getSubsystem<EditorContext>();
        if ( bCaptured )
            editorContext.getSubsystem<EditorNotificationManager>().push( SW_LOCKEY( "EdCommon", "NotifyBugReportSaved" ), directory, NotificationType::Success );
        else
            editorContext.getSubsystem<EditorNotificationManager>().push( SW_LOCKEY( "EdCommon", "NotifyBugReportTitle" ), SW_LOCKEY( "EdCommon", "NotifyBugReportFailed" ), NotificationType::Error );
        if ( bCaptured )
            _note.clear(); // 모은 메모는 info.txt 에 남았다
        return bCaptured;
    }

    void BugReportModel::refreshIfDirty()
    {
        if ( _bRowsDirty == false )
            return;
        _bRowsDirty = false;
        _listRow.clear();
        vector<string> listFolder;
        if ( FileUtil::collectFolders( BugReportModelInternal::makeRoot(), listFolder, false ) == false )
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

    string BugReportModel::makeAbsolutePath( const BugReportRow& row )
    {
        string absolutePath;
        return FileUtil::makeAbsolutePath( row._directory, absolutePath ) ? absolutePath : row._directory;
    }

    void BugReportModel::showInFileExplorer( const BugReportRow& row )
    {
        (void)EditorAssetFileCommands::showInFileExplorer( makeAbsolutePath( row ) );
    }

    void BugReportModel::goTo( const BugReportRow& row )
    {
        SceneManager* pSceneManager = editor::findSubsystem<SceneManager>();
        string        reply;
        if ( pSceneManager == nullptr || BugReport::goTo( *pSceneManager, row._directory, reply ) == false )
            SW_LOG_WARNING( "BugReport go to failed: %#", reply.c_str() );
        else
            SW_LOG_INFO( "%#", reply.c_str() );
    }
} // namespace sw::editor
