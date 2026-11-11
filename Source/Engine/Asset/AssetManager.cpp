#include "pch.h"

#include "Engine/Asset/AssetManager.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Compression/CompressionCodecRegistry.h"
#include "Core/File/AsyncFileIO.h"
#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Log/Logger.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Asset/AssetStreamingQueue.h"
#include "Engine/Asset/Cache/AssetCacheBase.h"
#include "Engine/Asset/Cache/BuiltInAssetCache.h"
#include "Engine/Config/ConfigManager.h"
#include "Engine/Config/EngineConfig.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Content/Pack/PackManager.h"
#include "Engine/Content/Pack/PackPatchTrustSource.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Foundation/EngineSubsystemRegistration.h"
#include "Engine/Serialization/Inherit/ResourceComposition.h"

namespace sw
{
    namespace
    {
        /** @brief 내장 캐시 정렬 — 종류 이름 순(비우기 순서를 링크 순서와 떼어 고정한다). */
        bool isKindNameLess( const AssetCacheBase* pLeft, const AssetCacheBase* pRight )
        {
            return string_view{ pLeft->getAssetKindName() } < string_view{ pRight->getAssetKindName() };
        }

        /** @brief 캐시를 올릴 때 복사할 종류 이름입니다(`RegistrationList` 가 지금 복사한다 — 모듈이 내리지 않고 사라지면 나중에는 물어볼 수 없다). nullptr 이면 빈 글입니다. */
        string_view findKindName( const AssetCacheBase* pCache )
        {
            const utf8* const pKindName = pCache != nullptr ? pCache->getAssetKindName() : nullptr;
            return pKindName != nullptr ? string_view{ pKindName } : string_view{};
        }
    } // namespace
} // namespace sw

namespace sw
{
    void AssetManager::addLoadTask( const TaskHandle& task )
    {
        ScopedLock<mutex> lock{ _loadStageMutex };
        engine::getSubsystem<TaskManager>().addToStage( _loadStage, task, "AssetLoad" );
    }

    void AssetManager::launchLoadJob( string_view name, const TaskDelegate& job )
    {
        TaskHandle handle = engine::getSubsystem<TaskManager>().emplaceTask( name, job );
        handle.setPriority( TaskPriority::Low ); // 백그라운드 IO 큐에서 돌아 프레임 작업을 막지 않는다(AssetStreamingQueue 와 같다)
        addLoadTask( handle );                   // 제출 전에 — 결정적 실행의 프레임 경계 대기가 놓치지 않는다
        handle.submit();
    }

    void AssetManager::waitLoadTasks( const utf8* pOwner )
    {
        TaskStageHandle loadStage;
        {
            ScopedLock<mutex> lock{ _loadStageMutex };
            loadStage = _loadStage;
        }
        if ( loadStage.isValid() )
            engine::getSubsystem<TaskManager>().waitStage( loadStage, pOwner );
    }

    void AssetManager::finishPendingAssetJobs()
    {
        constexpr string_view kOwner      = "AssetManager::finishPendingAssetJobs";
        TaskManager&          taskManager = engine::getSubsystem<TaskManager>();
        AssetStreamingQueue&  streaming   = engine::getSubsystem<AssetStreamingQueue>();
        AsyncFileIO&          asyncIO     = engine::getSubsystem<AsyncFileIO>();
        for ( ;; )
        {
            TaskStageHandle loadStage;
            {
                ScopedLock<mutex> lock{ _loadStageMutex };
                loadStage = _loadStage;
            }
            taskManager.waitStage( loadStage, kOwner );
            streaming.waitForOwnTasks( kOwner );
            asyncIO.waitIdle();
            if ( taskManager.getStagePendingCount( loadStage ) == 0 && streaming.getOwnTaskPendingCount() == 0 && asyncIO.getOutstandingCount() == 0 )
                return;
        }
    }

    AssetManager::AssetManager()
        : _listOwnedAssetCache{}
        , _pPackManager{ make_unique<PackManager>() }
        , _listBuiltInAssetCache{}
        , _registeredAssetCache{ "AssetManager.AssetCache", RegistrationList<AssetCacheBase>::ReleaseFunction::create<&AssetManager::releaseAssetCache>( this ) }
        , _listReloadEntry{}
    {
        // 내장 캐시는 기능 폴더가 등록표에 올린 것이다 — Resource 는 그 타입을 모른다. 모두 여기서 만들어 소유한다(정적 창구는 `getAssetCache<T>()` 로 찾는다).
        // 등록 순서는 링크 순서라 종류 이름 순으로 늘어놓아 비우기 순서를 고정한다. 등록 · "내장인가" 판정이 모두 이 목록을 본다.
        for ( const BuiltInAssetCacheRegistration* pRegistration : BuiltInAssetCacheRegistry::getAll() )
        {
            _listOwnedAssetCache.push_back( pRegistration->_pCreate() );
            _listBuiltInAssetCache.push_back( _listOwnedAssetCache.back().get() );
            if ( pRegistration->_pReload != nullptr )
                _listReloadEntry.push_back( ReloadEntry{ _listBuiltInAssetCache.back(), pRegistration->_pReload } );
        }
        VectorUtil::sort( _listBuiltInAssetCache, isKindNameLess );
        for ( AssetCacheBase* pCache : _listBuiltInAssetCache )
        {
            (void)addAssetCache( pCache ); // 거절은 목록이 로그로 남겼다
        }
    }

    AssetManager::~AssetManager() = default;

    bool AssetManager::initialize()
    {
        // 루트는 경로 상태의 주인(코어 범위의 `ContentRootSubsystem`)이 이미 찾았다 — 여기서 다시 찾지 않고 찾았는지만 본다.
        if ( engine::getSubsystem<AssetPaths>().isInitialized() == false )
        {
            SW_LOG_ERROR_ID( "AssetManager.FailedInitializeAssetManagerContent", "Failed to initialize AssetManager - the content root was not found" );
            return false;
        }

        return true;
    }

    bool AssetManager::mountContent( const vector<string>& listSearchPriority, ContentSource source )
    {
        // 우선순위 적용과 마운트를 **여기서 붙여 둔다.** 둘을 호출자에게 맡기면 순서를 뒤집거나
        // 사이에 다른 것을 끼워 넣을 수 있고, 실제로 그래서 팩이 게임 도메인 없이 실린 적이 있다.
        if ( listSearchPriority.empty() == false )
            engine::getSubsystem<AssetPaths>().setSearchPriority( listSearchPriority );

        // 소스를 읽는 실행(개발 · 쿠킹)은 팩을 올리지 않는다 — 팩은 쿠킹의 산출물이라 소스와 섞어 읽으면 어긋난 쿠킹본을 모르고 읽는다.
        _pPackManager->setContentSource( source );
        const bool bMounted = source == ContentSource::Cooked && mountStartupPacks();
        return bMounted;
    }

    bool AssetManager::mountStartupPacks()
    {
        if ( _pPackManager == nullptr )
            return false;

        // 패치 목록 서명 — 엔진 설정에 공개 키가 있으면 마운트 전에 신뢰 설정을 건다(키가 있는데 제공자 칸이 비었거나 키가 틀리면 기동을 멈춘다. 서명 없이 패치를 올리지 않는다).
        // 설정이 없는 채널(스팀)은 서명을 보지 않는다.
        if ( const EngineConfig* pEngineConfig = engine::findSubsystem<EngineConfig>() )
        {
            PackPatchTrust trust;
            const auto     trustResult = PackPatchTrustSource::resolve( *pEngineConfig, trust );
            if ( trustResult == PackPatchTrustResult::Invalid )
                return false;
            if ( trustResult == PackPatchTrustResult::Ready && _pPackManager->setPatchTrust( trust ) == false )
                return false;
        }

        // 팩 폴더는 한 곳이다(`ProcessRoots::_packs` - 실행 파일 기준). 예전에는 실행 파일 · 작업 폴더 · 프로젝트 · "Bin/Packs" 를 차례로 찾았다(띄운 자리에 따라 다른 팩).
        return _pPackManager->scanAndMountPacks( ProcessRoots::get()._packs.str() );
    }

    void AssetManager::shutdown()
    {
        if ( _pPackManager != nullptr )
            _pPackManager->unmountAll();

        clearAssetCaches();
        // 비운 **뒤에** 말한다. 여기까지 왔다는 것은 죽은 포인터를 아직 밟지 않았다는 뜻이고,
        // 다음 실행에서 같은 일이 반복되지 않게 이름을 남겨야 한다.
        warnAboutRemainingModuleCaches();
    }

    void AssetManager::garbageCollectUnusedAssets()
    {
        engine::getSubsystem<AssetStreamingQueue>().clearCompletionRecord();
        // 캐시 자체는 참조가 0 이 되는 자리에서 스스로 지운다(`MaterialCache::release`).
        // 여기서는 아직 할 일이 없다. 있게 되면 레지스트리를 훑는다.
    }

    uint32 AssetManager::reloadComposedDependents()
    {
        vector<string> listAssetID;
        engine::getSubsystem<ResourceComposition>().takePendingReload( listAssetID );
        if ( listAssetID.empty() )
            return 0;
        const vector<AssetCacheBase*> listCache     = getAllAssetCache();
        uint32                        reloadedCount = 0;
        for ( const string& assetID : listAssetID )
        {
            for ( AssetCacheBase* pCache : listCache )
            {
                if ( pCache->reloadComposed( assetID ) )
                    ++reloadedCount;
            }
        }
        if ( reloadedCount > 0 )
            SW_LOG_INFO_ID( "Asset.ReloadedComposed", "Reloaded %# held asset(s) whose resource tags or layers changed", reloadedCount );
        return reloadedCount;
    }

    uint32 AssetManager::reloadChangedPaths( const vector<string>& listAssetID, vector<string>& outListStale )
    {
        const vector<AssetCacheBase*> listCache     = getAllAssetCache();
        uint32                        reloadedCount = 0;
        for ( const string& assetID : listAssetID )
        {
            for ( AssetCacheBase* pCache : listCache )
            {
                if ( pCache->isCached( assetID ) == false )
                    continue;
                if ( pCache->reloadComposed( assetID ) )
                    ++reloadedCount;
                else
                    outListStale.push_back( assetID );
            }
        }
        return reloadedCount;
    }

    Subscription AssetManager::registerAssetCache( AssetCacheBase* pCache, AssetCacheReloadFunction pReload )
    {
        // nullptr · 같은 종류 이름은 목록이 거절하고 로그로 남긴다(빈 구독).
        Subscription subscription = _registeredAssetCache.addSubscribed( pCache, "Asset cache kind", findKindName( pCache ) );
        if ( subscription.isActive() && pReload != nullptr )
            _listReloadEntry.push_back( ReloadEntry{ pCache, pReload } );
        return subscription;
    }

    AssetCacheReloadFunction AssetManager::findReloadFunction( const AssetCacheBase* pCache ) const
    {
        for ( const ReloadEntry& entry : _listReloadEntry )
        {
            if ( entry._pCache == pCache )
                return entry._pReload;
        }
        return nullptr;
    }

    bool AssetManager::addAssetCache( AssetCacheBase* pCache )
    {
        if ( pCache == nullptr )
            return false;
        return _registeredAssetCache.addLogged( pCache, "Asset cache kind", findKindName( pCache ) ) == RegistrationResult::Added;
    }

    bool AssetManager::isBuiltInAssetCache( const AssetCacheBase* pCache ) const
    {
        for ( const AssetCacheBase* pBuiltIn : _listBuiltInAssetCache )
        {
            if ( pBuiltIn == pCache )
                return pCache != nullptr;
        }
        return false;
    }

    void AssetManager::releaseAssetCache( AssetCacheBase* pCache )
    {
        (void)_registeredAssetCache.remove( pCache ); // 구독 하나가 등록 하나라 늘 있다(주소로만 찾는다 — 역참조하지 않는다)
        _listReloadEntry.erase( std::remove_if( _listReloadEntry.begin(), _listReloadEntry.end(), [pCache]( const ReloadEntry& entry )
        { return entry._pCache == pCache; } ),
                                _listReloadEntry.end() );
    }

    vector<AssetCacheBase*> AssetManager::getAllAssetCache() const
    {
        return _registeredAssetCache.getItems();
    }

    AssetCacheBase* AssetManager::findAssetCache( string_view assetKindName ) const
    {
        // 이름은 등록 시점 사본으로 맞춘다. 죽은 모듈의 가상 함수를 부르지 않는다.
        return _registeredAssetCache.findByName( assetKindName );
    }

    shared_ptr<const void> AssetManager::pinAsset( string_view relativePath )
    {
        // 종류는 접미사로 갈리므로 한 캐시만 답한다 — 첫 답을 받는다.
        for ( AssetCacheBase* pCache : _registeredAssetCache.getItems() )
        {
            shared_ptr<const void> pin = pCache->pinAsset( relativePath );
            if ( pin != nullptr )
                return pin;
        }
        return {};
    }

    void AssetManager::clearAssetCaches()
    {
        for ( AssetCacheBase* pCache : _registeredAssetCache.getItems() )
        {
            pCache->clear();
        }
    }

    void AssetManager::warnAboutRemainingModuleCaches() const
    {
        for ( uint32 index = 0; index < _registeredAssetCache.getCount(); ++index )
        {
            if ( isBuiltInAssetCache( _registeredAssetCache.getAt( index ) ) )
                continue;

            // 이름은 사본이라 안전하다. 포인터는 이미 죽었을 수도 있어 **역참조하지 않는다.**
            SW_LOG_WARNING_ID( "AssetManager.AssetCacheStillRegistered", "Asset cache '%#' is still registered - the registering side (module) must call "
                                                                         "release its registerAssetCache subscription before it goes down. Otherwise the next clear jumps into dead code.",
                               _registeredAssetCache.getNameAt( index ).c_str() );
        }
    }

    AssetCacheBase& AssetManager::getBuiltInAssetCache( string_view assetKindName ) const
    {
        AssetCacheBase* pCache = _registeredAssetCache.findByName( assetKindName );
        if ( pCache == nullptr || isBuiltInAssetCache( pCache ) == false )
            SW_LOG_ERROR_ID( "AssetManager.BuiltAssetCacheNot", "Built-in asset cache '%#' is not registered - its .cpp needs SW_BUILT_IN_ASSET_CACHE", assetKindName );
        if ( pCache == nullptr )
            ::sw::internal::reportMissingRequiredService( SW_FUNCTION_SIGNATURE ); // 위 오류가 어느 종류인지 남겼다 — Release 에서도 멈춘다
        return *pCache;
    }

    PackManager& AssetManager::getPackManager()
    {
        return *_pPackManager;
    }

    const PackManager& AssetManager::getPackManager() const
    {
        return *_pPackManager;
    }

} // namespace sw

namespace sw
{
    SubsystemInitResult AssetManager::initializeSubsystem()
    {
        if ( initialize() == false )
            return SubsystemInitResult::Failed;
        // 쿠킹(씬 · 팩)은 배포 구성에서도 소스를 읽는다 — 팩은 쿠킹의 산출물이라 자기 팩을 읽으면 순환이다.
        // 개발 구성도 `-cooked-content` 면 배포와 같은 길(쿠킹한 팩만)을 읽는다 — 배포 읽기 경로를 배포 빌드 없이 돌려 본다.
        ContentSource source = kBuildContentSource;
        if ( engine::areEngineServicesBound() )
        {
            const CommandLineManager& commandLine = engine::getSubsystem<CommandLineManager>();
            const auto                isGiven     = [&commandLine]( CommandLineArgument argument )
            {
                bool bGiven = false;
                return commandLine.getArgument( argument, bGiven ) && bGiven;
            };
            if ( isGiven( CommandLineArgument::COOK_SCENES ) || isGiven( CommandLineArgument::COOK_PACKS ) || isGiven( CommandLineArgument::COOK_ALL ) )
                source = ContentSource::Source;
            else if ( isGiven( CommandLineArgument::COOKED_CONTENT ) )
                source = ContentSource::Cooked;
        }
        (void)mountContent( engine::getSubsystem<EngineConfig>()._listResourcePriority, source ); // 느슨한 트리만 있어도 기동한다
        return SubsystemInitResult::Succeeded;
    }

    span<const utf8* const> AssetManager::getSubsystemDependencies() const
    {
        static constexpr const utf8* kArrDependency[] = { CompressionCodecRegistry::kSubsystemName, ConfigManager::kSubsystemName };
        return kArrDependency;
    }
} // namespace sw

namespace sw
{
    SW_ENGINE_SERVICE_SUBSYSTEM( AssetManager, GameVisible, Startup, (), []( engine::EngineSubsystemFactoryScope& scope, engine::EngineSubsystemBuild& build )
    {
        (void)build;
        AssetManager& assetManager = scope.emplaceSubsystem<AssetManager>();
        (void)scope.bind( &assetManager.getPackManager() ); // 새 범위 — 겹치지 않는다. 리소스 입구(`AssetIO`)가 서비스 칸으로 찾는다
    } );

    // 위 공장이 건 팩 매니저의 서비스 칸(아래 층 · 아무 스레드의 리소스 입구가 읽는다). 비어 있을 수 있다 — 엔진 없이 도는 도구 · 쿠커 · 테스트는 팩이 없고
    // 빌드 기본 정책을 읽는다(언리얼 `FPakPlatformFile` 이 플랫폼 파일 계층에 걸리는 자리).
    SW_ENGINE_SERVICE( PackManager, HostOnly );
} // namespace sw
