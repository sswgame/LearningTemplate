#include "pch.h"

#include "Editor/Common/Config/EditorPaths.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Workspace/EditorSubsystems.h"

#include "Engine/Config/GameConfig.h"
#include "Engine/Content/AssetPaths.h"

#include "sw/config/ConfigConstants.h"

namespace sw::editor
{
    SW_TEST_GLOBAL_VARIABLE( sw::GlobalDiskPath, gv_editorStateDir, "",
                             "에디터 상태 폴더(Saved/Editor 대신, 절대 경로 또는 기동한 셸의 작업 폴더 기준 상대 경로) — 자동화가 빈 폴더로 시작할 때 쓴다 (비우면 Saved/Editor)" );
} // namespace sw::editor

namespace sw::editor
{
    namespace
    {
        struct EditorPathsInternal
        {
            /** @brief 실행 뿌리 아래 에디터 상태 폴더 이름입니다(`<실행 뿌리>/editor`). */
            static constexpr string_view kRunStateFolder = "editor";

            /**
             * @brief 상태 폴더 @p folder 를 만들고 돌려줍니다. 경로를 묻는 함수라 폴더를 못 만들어도 경로는 돌려준다 — 이유는 ensure 가 남기고, 이 폴더에 쓰는 쪽이 쓰기 실패로 다시 알린다.
             * @details 자동화 실행(`-unattended` · `-scenario` — `CommandLineManager::isAutomatedRun`)은 실행 뿌리 아래 `editor` 를 쓴다. 시나리오가 닫은 패널 · 바꾼 설정이
             *          사용자의 Saved/Editor 에 남아 다음 실행(사람 · 다른 시나리오)이 그 상태로 시작했다. 실행 뿌리(`ProcessRoots::_run`)가 실행마다 새 폴더라 비우지 않고,
             *          동시에 도는 App 끼리 겹치지 않는다. 리로드 · 백엔드 교체로 다시 선 인스턴스는 같은 폴더를 이어 쓴다(자동 저장 세션 폴더 · 레이아웃).
             */
            static AbsolutePath ensureStateDirectory( AbsolutePath folder )
            {
                (void)FileUtil::ensureDirectoryExists( folder );
                return folder;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    AbsolutePath EditorPaths::findContentFolder( string_view folderID )
    {
        const optional<AbsolutePath> folder = editor::getSubsystem<AssetPaths>().toDiskPath( folderID );
        return folder.has_value() && FileUtil::isDirectory( *folder ) ? *folder : AbsolutePath{};
    }

    string EditorPaths::getSceneDialogFolderPath()
    {
        return findContentFolder( FileUtil::joinPath( editor::getSubsystem<const GameConfig>()._packRoot.get(), constant::path::kMapsFolder ) ).str();
    }

    AbsolutePath EditorPaths::getEditorStateDirectory()
    {
        // 자동화 실행은 빈 폴더를 쓴다 — 사용자의 레이아웃 · 테마 · 최근 씬과 무관하게 시작하고, 사용자 상태에 쓰지 않는다.
        // `gv_editorStateDir` 는 디스크 경로 변수라 값을 받을 때 이미 절대로 풀었다(R5) — 여기서 다시 풀지 않는다.
        const AbsolutePath variableDir = gv_editorStateDir.getPath();
        if ( variableDir.empty() == false )
            return EditorPathsInternal::ensureStateDirectory( variableDir );
        const ProcessRoots& roots = ProcessRoots::get();
        return EditorPathsInternal::ensureStateDirectory( editor::getSubsystem<CommandLineManager>().isAutomatedRun() ? roots._run.join( EditorPathsInternal::kRunStateFolder )
                                                                                                                      : roots._project.join( config::kDirSavedEditor ) );
    }

    AbsolutePath EditorPaths::resolveEditorStateFile( const utf8* pFileName )
    {
        // 이름이 없으면 빈 경로다(그 경로의 I/O 는 없는 파일처럼 실패한다).
        if ( StringUtil::isNullOrEmpty( pFileName ) )
            return {};
        return getEditorStateDirectory().join( pFileName );
    }

    const AssetPaths& EditorPaths::getAssetPaths()
    {
        return editor::getSubsystem<AssetPaths>();
    }
} // namespace sw::editor
