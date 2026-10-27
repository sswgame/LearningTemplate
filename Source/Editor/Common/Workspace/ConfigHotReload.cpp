#include "pch.h"

#include "Editor/Common/Workspace/ConfigHotReload.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

#include "Editor/Common/Config/EditorPaths.h"
#include "Editor/Common/Config/EditorToolDefaults.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Config/ConfigManager.h"
#include "Engine/Resource/ResourcePaths.h"

#include "sw/config/ConfigConstants.h"

namespace sw::editor
{
    SW_LOG_CALLER( "ConfigHotReload" );

    ConfigHotReload::ConfigHotReload()
        : _pFileWatchDispatcher{ nullptr }
        , _configWatch{}
    {
    }

    ConfigHotReload::~ConfigHotReload()
    {
        shutdown();
    }

    bool ConfigHotReload::initialize()
    {
        shutdown();

        const string configFolder = FileUtil::joinPath( ResourcePaths::getProjectFolderPath(), config::kDirConfig );
        if ( FileUtil::isDirectory( configFolder ) == false )
        {
            SW_LOG_INFO( "Config hot reload is off - no config folder at %#", configFolder.c_str() );
            return false;
        }

        _pFileWatchDispatcher = make_unique<FileWatchDispatcher>();
        if ( _pFileWatchDispatcher->initialize( configFolder ) == false )
        {
            _pFileWatchDispatcher.reset();
            return false;
        }
        const FileWatchMatchDelegate onChanged{ SW_DELEGATE_METHOD( FileWatchMatchDelegate, &ConfigHotReload::onConfigFileChanged, this ) };
        _configWatch = _pFileWatchDispatcher->registerWatch( configFolder, { ".json" }, onChanged );
        return _configWatch.isActive();
    }

    void ConfigHotReload::shutdown()
    {
        if ( _pFileWatchDispatcher == nullptr )
            return;
        _configWatch.reset();
        _pFileWatchDispatcher->shutdown();
        _pFileWatchDispatcher.reset();
    }

    void ConfigHotReload::update()
    {
        if ( _pFileWatchDispatcher != nullptr )
            _pFileWatchDispatcher->update();
    }

    ReloadOutcome ConfigHotReload::reloadChangedFile( ConfigManager* pConfigManager, string_view fullPath )
    {
        if ( pConfigManager != nullptr )
        {
            const ReloadOutcome hostOutcome = pConfigManager->reloadConfigFile( fullPath );
            if ( hostOutcome != ReloadOutcome::NotTracked )
                return hostOutcome;
        }

        // 에디터 도구 시드는 에디터가 보관하고 있다. 깨진 파일이면 이전 값을 그대로 둔다(읽기 실패는 loadFromHostPath 가 키 이름과 오류로 알린다).
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

    void ConfigHotReload::onConfigFileChanged( const FileChangeEvent& changeEvent )
    {
        if ( changeEvent._action == FileWatcherAction::Removed )
            return;
        const string fullPath = FileUtil::joinPath( changeEvent._directory, changeEvent._filename );
        // 모르는 파일(EditorConfig · 임포트 설정)은 NotTracked 로 지나간다. 깨진 파일은 읽는 쪽이 키 이름과 오류를 남겼다 — 이전 값으로 돈다는 것만 덧붙인다.
        if ( reloadChangedFile( ConfigManager::findPrimary(), fullPath ) == ReloadOutcome::Failed )
            SW_LOG_WARNING( "Config hot reload kept the previous values - fix and save %# again", fullPath.c_str() );
    }
} // namespace sw::editor
