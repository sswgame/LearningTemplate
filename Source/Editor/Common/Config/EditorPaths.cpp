#include "pch.h"

#include "Editor/Common/Config/EditorPaths.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Engine/Resource/ResourcePaths.h"

#include "sw/config/ConfigConstants.h"

namespace sw::editor
{
    SW_TEST_GLOBAL_VARIABLE( sw::string, gv_editorStateDir, "",
                             "에디터 상태 폴더(Saved/Editor 대신, 프로젝트 루트 기준 상대 또는 절대 경로) — 자동화가 빈 폴더로 시작할 때 쓴다 (비우면 Saved/Editor)" );
} // namespace sw::editor

namespace sw::editor
{
    SW_LOG_CALLER( "EditorPaths" );

    string EditorPaths::getProjectRootPath()
    {
        const string& projectRoot = ResourcePaths::getProjectFolderPath();
        if ( projectRoot.empty() == false )
            return projectRoot;

        const string& resourceRoot = ResourcePaths::getRootFolderPath();
        if ( resourceRoot.empty() )
            return {};

        return FileUtil::getDirectoryPart( FileUtil::trimTrailingSlashes( resourceRoot ) );
    }

    string EditorPaths::getEditorStateDirectory()
    {
        // 자동화 실행(AppScenarioTest)은 빈 임시 폴더를 준다 — 사용자의 레이아웃 · 테마 · 최근 씬과 무관하게 시작한다.
        string stateDir = gv_editorStateDir.empty() ? string{} : resolveProjectRelativePath( gv_editorStateDir );
        if ( stateDir.empty() )
        {
            const string projectRoot = getProjectRootPath();
            if ( projectRoot.empty() )
                return stateDir;
            stateDir = FileUtil::joinPath( projectRoot, config::kDirSavedEditor );
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
        {
            const string projectRoot = getProjectRootPath();
            if ( projectRoot.empty() == false )
                path = FileUtil::joinPath( projectRoot, path );
        }
        return path;
    }
} // namespace sw::editor
