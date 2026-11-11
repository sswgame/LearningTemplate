#include "pch.h"

#include "DevTools/AssetReload/ConfigReloadSubsystem.h"

#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Log/Logger.h"

#include "DevTools/AssetReload/FileWatchDispatcher.h"
#include "DevTools/AssetReload/ReloadTracker.h"

#include "Engine/Config/ConfigManager.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Foundation/EngineServices.h"

#include "sw/config/ConfigConstants.h"

namespace sw
{
    ConfigReloadSubsystem::ConfigReloadSubsystem( ConfigManager& configManager )
        : _configManager{ configManager }
        , _pFileWatchDispatcher{ nullptr }
        , _configWatch{}
        , _fileHandlers{}
    {
    }

    ConfigReloadSubsystem::~ConfigReloadSubsystem()
    {
        shutdownSubsystem();
    }

    SubsystemInitResult ConfigReloadSubsystem::initializeSubsystem()
    {
        const string configFolder = FileUtil::joinPath( ProcessRoots::get()._project.str(), config::kDirConfig );
        if ( FileUtil::isDirectory( configFolder ) == false )
        {
            SW_LOG_INFO_ID( "ConfigReload.ConfigReloadOffNo", "Config reload is off - no config folder at %#", configFolder.c_str() );
            return SubsystemInitResult::Succeeded;
        }
        _pFileWatchDispatcher = make_unique<FileWatchDispatcher>();
        if ( _pFileWatchDispatcher->initialize( configFolder ) == false )
        {
            _pFileWatchDispatcher.reset(); // 개발 도구 하나 때문에 기동을 멈추지 않는다 — 감시기가 이유를 남겼다
            return SubsystemInitResult::Succeeded;
        }
        const FileWatchMatchDelegate onChanged{ SW_DELEGATE_METHOD( FileWatchMatchDelegate, &ConfigReloadSubsystem::onConfigFileChanged, this ) };
        _configWatch = _pFileWatchDispatcher->registerWatch( configFolder, { ".json" }, onChanged );
        return SubsystemInitResult::Succeeded;
    }

    void ConfigReloadSubsystem::shutdownSubsystem()
    {
        if ( _pFileWatchDispatcher == nullptr )
            return;
        _configWatch.reset();
        _pFileWatchDispatcher->shutdown();
        _pFileWatchDispatcher.reset();
    }

    span<const utf8* const> ConfigReloadSubsystem::getSubsystemDependencies() const
    {
        static constexpr const utf8* kArrDependency[] = { ConfigManager::kSubsystemName };
        return kArrDependency;
    }

    void ConfigReloadSubsystem::update()
    {
        if ( _pFileWatchDispatcher != nullptr )
            _pFileWatchDispatcher->update();
    }

    Subscription ConfigReloadSubsystem::registerFileHandler( const ConfigFileReloadDelegate& handler )
    {
        return _fileHandlers.add( handler );
    }

    ReloadResult ConfigReloadSubsystem::reloadChangedFile( string_view fullPath )
    {
        const ReloadResult hostResult = _configManager.reloadConfigFile( fullPath );
        if ( hostResult != ReloadResult::NotTracked )
            return hostResult;
        ReloadResult result = ReloadResult::NotTracked;
        (void)_fileHandlers.broadcastUntil( [&result]( ReloadResult handlerResult )
        {
            result = handlerResult;
            return handlerResult != ReloadResult::NotTracked; // 처음 맡은 처리기에서 멈춘다
        }, fullPath );
        return result;
    }

    void ConfigReloadSubsystem::onConfigFileChanged( const FileChangeEvent& changeEvent )
    {
        if ( changeEvent._action == FileWatcherAction::Removed )
            return;
        const string fullPath = FileUtil::joinPath( changeEvent._directory, changeEvent._filename );
        // 모르는 파일은 NotTracked 로 지나간다. 깨진 파일은 읽는 쪽이 키 이름과 오류를 남겼다 — 이전 값으로 돈다는 것만 덧붙인다.
        const ReloadResult result = reloadChangedFile( fullPath );
        if ( ReloadTracker* const pTracker = engine::findSubsystem<ReloadTracker>() )
            pTracker->record( ReloadSource::Config, fullPath, result, "the file did not parse - see the log for the key" );
        if ( result == ReloadResult::Failed )
            SW_LOG_WARNING_ID( "ConfigReload.ConfigReloadKeptPrevious", "Config reload kept the previous values - fix and save %# again", fullPath.c_str() );
    }
} // namespace sw
