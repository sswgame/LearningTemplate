#include "pch.h"

#include "Editor/Common/Config/EditorToolDefaultsReload.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

#include "DevTools/AssetReload/ConfigReloadSubsystem.h"

#include "Editor/Common/Config/EditorPaths.h"
#include "Editor/Common/Config/EditorToolDefaults.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "sw/config/ConfigConstants.h"

namespace sw::editor
{
    SW_LOG_CALLER( "EditorToolDefaultsReload" );

    SubsystemInitResult EditorToolDefaultsReload::initializeSubsystem()
    {
        // 설정 리로드는 개발 빌드의 엔진 서브시스템이다 — 없으면(감시를 끈 호스트) 손 저장만 남는다.
        if ( ConfigReloadSubsystem* pConfigReload = editor::findSubsystem<ConfigReloadSubsystem>() )
            _handler = pConfigReload->registerFileHandler( ConfigFileReloadDelegate::create<&EditorToolDefaultsReload::reloadIfToolDefaults>() );
        return SubsystemInitResult::Succeeded;
    }

    ReloadOutcome EditorToolDefaultsReload::reloadIfToolDefaults( string_view fullPath )
    {
        const string toolDefaultsPath = EditorPaths::resolveProjectRelativePath( config::kFileRuntimeEditorToolDefaults );
        if ( FileUtil::pathsEqualNormalized( toolDefaultsPath, fullPath ) == false )
            return ReloadOutcome::NotTracked;
        // 편집기가 저장하며 잠시 지웠다 다시 쓰는 사건은 다시 읽을 것이 없다 — 이어지는 생성 사건이 읽는다. 정말 지웠으면 다음 기동이 오류로 멈춘다.
        if ( FileUtil::exists( toolDefaultsPath ) == false )
            return ReloadOutcome::NotTracked;
        EditorToolDefaults reloaded{};
        if ( reloaded.loadFromHostPath() == false )
            return ReloadOutcome::Failed;
        getEditorToolDefaults() = std::move( reloaded );
        SW_LOG_INFO( "Editor tool defaults reloaded from %#", toolDefaultsPath.c_str() );
        return ReloadOutcome::Reloaded;
    }
} // namespace sw::editor
