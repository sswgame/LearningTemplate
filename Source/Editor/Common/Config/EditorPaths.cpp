#include "pch.h"

#include "Editor/Common/Config/EditorPaths.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Automation/Scenario/AutomationScenario.h"
#include "Engine/Automation/Scenario/AutomationScratch.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/FileSystem/AssetPaths.h"

#include "sw/config/ConfigConstants.h"

namespace sw::editor
{
    SW_TEST_GLOBAL_VARIABLE( sw::GlobalDiskPath, gv_editorStateDir, "",
                             "에디터 상태 폴더(Saved/Editor 대신, 프로젝트 루트 기준 상대 또는 절대 경로) — 자동화가 빈 폴더로 시작할 때 쓴다 (비우면 Saved/Editor)" );
} // namespace sw::editor

namespace sw::editor
{
    namespace
    {
        struct EditorPathsInternal
        {
            /**
             * @brief 시나리오 실행(`-scenario`)의 상태 폴더입니다. 시나리오 실행이 아니거나 `-gv_editorStateDir` 를 주었으면 빈 문자열입니다.
             * @details 시나리오가 닫은 패널 · 바꾼 설정이 사용자의 Saved/Editor 에 남아 다음 실행(사람 · 다른 시나리오)이 그 상태로 시작했다.
             *          이름에 프로세스 꼬리를 붙여 동시에 도는 App 끼리 겹치지 않는다.
             */
            static string getAutomationStateDirectory()
            {
                if ( gv_editorStateDir.empty() == false || editor::getSubsystem<CommandLineManager>().isArgumentProvided( CommandLineArgument::SCENARIO ) == false )
                    return {};
                return FileUtil::joinPath( FileUtil::joinPath( makeAutomationOutputFolderPath(), kAutomationEditorStateFolder ), AutomationScratch::makeInstanceID( "editor" ) );
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_LOG_CALLER( "EditorPaths" );

    string EditorPaths::findContentFolder( string_view folderID )
    {
        const std::optional<AbsolutePath> folder = editor::getSubsystem<AssetPaths>().toDiskPath( folderID );
        return folder.has_value() && FileUtil::isDirectory( folder->str() ) ? folder->str() : string{};
    }

    string EditorPaths::getGamePacksFolderPath()
    {
        return FileUtil::joinPath( ProcessRoots::get()._content.str(), path::kGamePack );
    }

    string EditorPaths::getSceneDialogFolderPath()
    {
        return findContentFolder( FileUtil::joinPath( editor::getSubsystem<const GameConfig>()._packRoot.get(), path::kMapsFolder ) );
    }

    void EditorPaths::prepareAutomationStateDirectory()
    {
        const string stateDir = EditorPathsInternal::getAutomationStateDirectory();
        if ( stateDir.empty() )
            return;
        // 같은 프로세스 번호를 썼던 지난 실행이 남긴 상태를 읽지 않게 비우고 시작하며, 끝난 실행의 상태 폴더는 거둔다.
        AutomationScratch::resetInstanceFolder( stateDir );
        SW_LOG_INFO( "Scenario run: editor state goes to %# (Saved/Editor is neither read nor written)", stateDir.c_str() );
    }

    string EditorPaths::getEditorStateDirectory()
    {
        // 자동화 실행은 빈 폴더를 쓴다 — 사용자의 레이아웃 · 테마 · 최근 씬과 무관하게 시작하고, 사용자 상태에 쓰지 않는다.
        string stateDir = gv_editorStateDir.empty() ? EditorPathsInternal::getAutomationStateDirectory() : resolveProjectRelativePath( gv_editorStateDir );
        if ( stateDir.empty() )
        {
            stateDir = FileUtil::joinPath( ProcessRoots::get()._project.str(), config::kDirSavedEditor );
        }
        // 경로를 묻는 함수라 폴더를 못 만들어도 경로는 돌려준다 — 이유는 ensure 가 남기고, 이 폴더에 쓰는 쪽이 쓰기 실패로 다시 알린다.
        (void)FileUtil::ensureDirectoryExists( stateDir );
        return stateDir;
    }

    string EditorPaths::resolveEditorStateFile( const utf8* pFileName )
    {
        if ( StringUtil::isNullOrEmpty( pFileName ) )
            return {};

        const string stateDir = getEditorStateDirectory();
        if ( stateDir.empty() )
            return {};

        return FileUtil::joinPath( stateDir, pFileName );
    }

    string EditorPaths::resolveProjectRelativePath( string_view hostRelativePath )
    {
        // 반환 지점을 하나로 둔다. 어떤 경로는 지역 변수를, 어떤 경로는 다른 식을 반환하면 NRVO 가 적용되지 않는다.
        string path = FileUtil::normalizeSeparators( hostRelativePath );
        if ( FileUtil::isAbsolutePath( path ) == false )
            path = FileUtil::joinPath( ProcessRoots::get()._project.str(), path );
        return path;
    }
} // namespace sw::editor
