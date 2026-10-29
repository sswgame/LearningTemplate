#include "pch.h"

#include "Engine/AssetReload/ConfigReloadSubsystem.h"

#include "Core/Container/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

#include "Engine/AssetReload/FileWatchDispatcher.h"
#include "Engine/Config/ConfigManager.h"
#include "Engine/FileSystem/AssetPaths.h"

#include "sw/config/ConfigConstants.h"

SW_LOG_CALLER( "ConfigReload" );

namespace sw
{
    ConfigReloadSubsystem::ConfigReloadSubsystem( ConfigManager& configManager )
        : _configManager{ configManager }
        , _pFileWatchDispatcher{ nullptr }
        , _configWatch{}
        , _listHandler{}
    {
    }

    ConfigReloadSubsystem::~ConfigReloadSubsystem()
    {
        shutdownSubsystem();
    }

    SubsystemInitResult ConfigReloadSubsystem::initializeSubsystem()
    {
        const string configFolder = FileUtil::joinPath( AssetPaths::getProjectFolderPath(), config::kDirConfig );
        if ( FileUtil::isDirectory( configFolder ) == false )
        {
            SW_LOG_INFO( "Config reload is off - no config folder at %#", configFolder.c_str() );
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
        const uint64 id = _nextHandlerID++;
        _listHandler.push_back( HandlerEntry{ id, handler } );
        return _subscriptions.issue( id );
    }

    void ConfigReloadSubsystem::releaseSubscription( uint64 id )
    {
        (void)VectorUtil::removeIf( _listHandler, [id]( const HandlerEntry& entry )
        { return entry._id == id; } );
    }

    ReloadOutcome ConfigReloadSubsystem::reloadChangedFile( string_view fullPath )
    {
        const ReloadOutcome hostOutcome = _configManager.reloadConfigFile( fullPath );
        if ( hostOutcome != ReloadOutcome::NotTracked )
            return hostOutcome;
        // 처리기가 목록을 바꿔도(구독 해제) 견디게 사본을 돈다.
        const vector<HandlerEntry> listHandler = _listHandler;
        for ( const HandlerEntry& entry : listHandler )
        {
            const ReloadOutcome outcome = entry._handler( fullPath );
            if ( outcome != ReloadOutcome::NotTracked )
                return outcome;
        }
        return ReloadOutcome::NotTracked;
    }

    void ConfigReloadSubsystem::onConfigFileChanged( const FileChangeEvent& changeEvent )
    {
        if ( changeEvent._action == FileWatcherAction::Removed )
            return;
        const string fullPath = FileUtil::joinPath( changeEvent._directory, changeEvent._filename );
        // 모르는 파일은 NotTracked 로 지나간다. 깨진 파일은 읽는 쪽이 키 이름과 오류를 남겼다 — 이전 값으로 돈다는 것만 덧붙인다.
        if ( reloadChangedFile( fullPath ) == ReloadOutcome::Failed )
            SW_LOG_WARNING( "Config reload kept the previous values - fix and save %# again", fullPath.c_str() );
    }
} // namespace sw
