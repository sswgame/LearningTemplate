#include "pch.h"

#include "Editor/Common/Workspace/ConfigHotReload.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

#include "Editor/Common/Config/EditorToolDefaults.h"
#include "Editor/Common/EditorUtil.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Config/ConfigManager.h"
#include "Engine/Resource/ResourceUtil.h"

#include "sw/config/ConfigConstants.h"

namespace sw::editor
{
    SW_LOG_CALLER( "ConfigHotReload" );

    ConfigHotReload::ConfigHotReload()
        : _pFileWatchDispatcher{ nullptr }
        , _configWatchHandle{}
    {
    }

    ConfigHotReload::~ConfigHotReload()
    {
        shutdown();
    }

    bool ConfigHotReload::initialize()
    {
        shutdown();

        const string configFolder = FileUtil::joinPath( ResourceUtil::getProjectFolderPath(), getEditorToolDefaults()._configFolder );
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
        _configWatchHandle = _pFileWatchDispatcher->registerWatch( configFolder, { ".json" }, onChanged );
        return _configWatchHandle.isValid();
    }

    void ConfigHotReload::shutdown()
    {
        if ( _pFileWatchDispatcher == nullptr )
            return;
        if ( _configWatchHandle.isValid() )
            _pFileWatchDispatcher->unregisterWatch( _configWatchHandle );
        _configWatchHandle = {};
        _pFileWatchDispatcher->shutdown();
        _pFileWatchDispatcher.reset();
    }

    void ConfigHotReload::update()
    {
        if ( _pFileWatchDispatcher != nullptr )
            _pFileWatchDispatcher->update();
    }

    bool ConfigHotReload::reloadChangedFile( ConfigManager* pConfigManager, string_view fullPath )
    {
        if ( pConfigManager != nullptr && pConfigManager->reloadConfigFile( fullPath ) )
            return true;

        // 에디터 도구 시드는 에디터가 들고 있다. 깨진 파일이면 이전 값을 그대로 둔다(읽기 실패는 loadFromHostPath 가 알린다).
        const string toolDefaultsPath = EditorUtil::resolveProjectRelativePath( config::kFileRuntimeEditorToolDefaults );
        if ( FileUtil::pathsEqualNormalized( toolDefaultsPath, fullPath ) == false )
            return false;
        EditorToolDefaults reloaded{};
        if ( reloaded.loadFromHostPath() == false )
            return false;
        getEditorToolDefaults() = std::move( reloaded );
        SW_LOG_INFO( "Editor tool defaults reloaded from %#", toolDefaultsPath.c_str() );
        return true;
    }

    void ConfigHotReload::onConfigFileChanged( const FileChangeEvent& changeEvent )
    {
        if ( changeEvent._action == FileWatcherAction::Removed )
            return;
        const string fullPath = FileUtil::joinPath( changeEvent._directory, changeEvent._filename );
        (void)reloadChangedFile( ConfigManager::findPrimary(), fullPath ); // 모르는 파일(EditorConfig · 임포트 설정)은 그냥 넘긴다
    }
} // namespace sw::editor
