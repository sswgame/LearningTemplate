#include "pch.h"

#include "GameFramework/Runtime/Online/Server/OnlineServerRuntime.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Foundation/Constants.h"
#include "Core/Network/Security/INetSecurityProvider.h"
#include "Core/Network/Transport/IStreamTransport.h"
#include "Core/Network/Transport/WebSocketAcceptTransport.h"
#include "Core/String/StringUtil.h"
#include "Core/Time/Clock.h"

#include "Engine/Config/Server/ServerSecret.h"
#include "Engine/Config/ServerConfig.h"
#include "Engine/Server/ServiceHealthRegistry.h"

#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Online/Audit/Server/ServiceAuditLog.h"
#include "GameFramework/Runtime/Online/Bus/Server/EphemeralServerBus.h"
#include "GameFramework/Runtime/Online/Cache/Server/MemoryEphemeralStore.h"
#include "GameFramework/Runtime/Online/Config/Server/RemoteConfig.h"
#include "GameFramework/Runtime/Online/Directory/Server/ServerRegistration.h"
#include "GameFramework/Runtime/Online/Ledger/Server/Ledger.h"
#include "GameFramework/Runtime/Online/Report/Server/ServiceReportStore.h"
#include "GameFramework/Runtime/Online/Schedule/Server/ServiceRetentionJob.h"
#include "GameFramework/Runtime/Online/Schedule/Server/ServiceScheduler.h"
#include "GameFramework/Runtime/Online/Security/NetSecurity.h"
#include "GameFramework/Runtime/Online/Server/IOnlineStoreDriver.h"
#include "GameFramework/Runtime/Online/Server/OnlineServerKit.h"
#include "GameFramework/Runtime/Online/Service/Server/OnlineServiceHost.h"
#include "GameFramework/Runtime/Online/Store/Server/MemoryServiceStore.h"

namespace sw
{
    namespace
    {
        struct OnlineServerRuntimeInternal
        {
            /** @brief 원격 설정을 저장소에서 다시 읽는 간격입니다. 바꾼 서버는 버스로 바로 알리므로, 이것은 버스를 놓친 서버의 늦은 따라잡기다. */
            static constexpr int64 kRemoteConfigReloadMs = 60000;

            static bool isMemoryDriver( string_view driver ) { return driver == kMemoryStoreDriverName; }

            /** @brief 서버 설정의 항목 목록에서 @p name 을 찾습니다. 비었거나 없으면 오류입니다(첫 항목이나 메모리로 대신하지 않는다). */
            template <typename TEntry>
            [[nodiscard]] static expected<const TEntry*> findEntry( const vector<TEntry>& listEntry, const string& name, const utf8* pNameField, const utf8* pListField )
            {
                if ( name.empty() )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "the online service config must name its entry (use a 'memory' driver entry for an in-process one)", pNameField } );
                const TEntry* const pEntry = VectorUtil::findBy<&TEntry::_name>( listEntry, name );
                if ( pEntry == nullptr )
                {
                    Error error{ ErrorKind::NotFound, "the named entry is not in the server config list", name };
                    error.appendDetail( " (" ).appendDetail( pNameField ).appendDetail( " in " ).appendDetail( pListField ).appendDetail( ")" );
                    return unexpected( error );
                }
                return pEntry;
            }

            /** @brief 키트 @p kitName 의 @p pPhase 단계 오류에 키트 이름을 붙입니다(종류 · 문장은 키트가 준 그대로, 대상 앞에 "kit '<이름>' <단계>: "). */
            static Error makeKitError( const Error& inner, const string& kitName, const utf8* pPhase )
            {
                Error error{ inner.kind(), inner.message().data(), "kit '" };
                error.appendDetail( kitName ).appendDetail( "' " ).appendDetail( pPhase ).appendDetail( ": " ).appendDetail( inner.detail() );
                return error;
            }

            static constexpr int64 kDayMs = 24 * constant::time::kSecondsPerHour * constant::time::kMillisecondsPerSecond;
            /** @brief 분개를 내역보다 하루 더 남긴다 — 내역이 가리키는 분개가 먼저 사라지지 않게 한다. */
            static constexpr int64 kJournalExtraDays = 1;

            static ServiceRetentionRule makeRetentionRule( const hashed_string& table, int64 days, ServiceRetentionTimeFn pfnReadTime )
            {
                ServiceRetentionRule rule;
                rule._table       = table;
                rule._retentionMs = days * kDayMs;
                rule._pfnReadTime = pfnReadTime;
                return rule;
            }

            /** @brief 설정의 보존 일수(0 이면 지우지 않음)로 쓸기 규칙을 만듭니다. */
            static vector<ServiceRetentionRule> makeRetentionRules( const OnlineServerConfig& config )
            {
                vector<ServiceRetentionRule> listRule;
                if ( config._retentionAuditDays > 0 )
                    listRule.push_back( makeRetentionRule( ServiceAuditLog::getTable(), config._retentionAuditDays, &ServiceAuditLog::readTimeMs ) );
                if ( config._retentionLedgerDays > 0 )
                {
                    listRule.push_back( makeRetentionRule( Ledger::getHistoryTable(), config._retentionLedgerDays, &Ledger::readHistoryTimeMs ) );
                    listRule.push_back( makeRetentionRule( Ledger::getJournalTable(), config._retentionLedgerDays + kJournalExtraDays, &Ledger::readJournalTimeMs ) );
                }
                if ( config._retentionReportDays > 0 )
                {
                    ServiceRetentionRule rule = makeRetentionRule( ServiceReportStore::getTable(), config._retentionReportDays, &ServiceReportStore::readClosedTimeMs );
                    rule._keyPrefix           = "closed/";
                    listRule.push_back( std::move( rule ) );
                }
                return listRule;
            }

            static ServiceRetentionJobConfig makeRetentionSettings( const OnlineServerConfig& config )
            {
                ServiceRetentionJobConfig settings;
                settings._minuteOfDay = config._retentionMinuteOfDay;
                return settings;
            }

            /** @brief 설정의 키트 목록에 @p name 이 있는지입니다. */
            static bool isKitListed( const OnlineServerConfig& config, string_view name )
            {
                for ( const OnlineServerKitEntry& entry : config._listKit )
                {
                    if ( entry._name == name )
                        return true;
                }
                return false;
            }
        };

        /**
         * @class OnlineServerKitNode
         * @brief 키트 하나를 지역 컬렉션의 노드로 둡니다. 의존은 공통 기반 + 필수 키트 + 설정에 있는 선택 키트입니다.
         */
        class OnlineServerKitNode final : public ISubsystem
        {
        public:
            OnlineServerKitNode( unique_ptr<IOnlineServerKit> kit, const utf8* pName, const OnlineServerKitEntry& entry, vector<const utf8*> listDependency,
                                 OnlineServerKitContext& context, optional<Error>& startupError )
                : _kit{ std::move( kit ) }
                , _entry{ entry }
                , _listDependency{ std::move( listDependency ) }
                , _context{ context }
                , _startupError{ startupError }
                , _pName{ pName }
            {
            }

            SubsystemInitResult initializeSubsystem() override
            {
                const expected<void> result = _kit->initialize( _context, _entry );
                if ( result.has_value() )
                    return SubsystemInitResult::Succeeded;
                _startupError = OnlineServerRuntimeInternal::makeKitError( result.error(), _entry._name, "initialize" );
                return SubsystemInitResult::Failed;
            }
            void stopSubsystem() override { _kit->stop(); }
            void shutdownSubsystem() override { _kit->shutdown(); }

            const utf8*             getSubsystemName() const override { return _pName; }
            span<const utf8* const> getSubsystemDependencies() const override { return { _listDependency.data(), _listDependency.size() }; }

            [[nodiscard]] expected<void> start()
            {
                const expected<void> result = _kit->startKit( _context );
                if ( result.has_value() )
                    return {};
                return unexpected( OnlineServerRuntimeInternal::makeKitError( result.error(), _entry._name, "start" ) );
            }
            void          update( UnixMilliseconds nowMs ) { _kit->update( nowMs ); }
            int32         getOpenWorkCount() const { return _kit->getOpenWorkCount(); }
            void          beginShutdown( const ServerShutdownNotice& notice ) { _kit->beginShutdown( notice ); }
            const string& getKitName() const { return _entry._name; }

            /**
             * @brief 의존이 없는 키트끼리의 순서를 이름 역순으로 못 박습니다(`OnlineServerRuntimeConfig::_bReverseIndependentKitOrder`).
             * @details 컬렉션은 준비된 노드가 여럿이면 이름 순으로 세운다. 이름 역순으로 고른 위상 순서대로 앞 노드를 의존에 더해, 이름 순에 기댄 조립(적지 않은 의존)이
             *          테스트에서 드러나게 한다. 순환이 있으면 거기서 멈춘다 — 그 오류는 컬렉션 정렬이 알린다.
             */
            static void chainReverseNameOrder( vector<unique_ptr<OnlineServerKitNode>>& inoutListNode )
            {
                const size_t  nodeCount = inoutListNode.size();
                vector<uint8> listPlaced( nodeCount, SW_FALSE );
                const utf8*   pPrevious = nullptr;
                for ( size_t placedCount = 0; placedCount < nodeCount; ++placedCount )
                {
                    size_t pick = nodeCount;
                    for ( size_t index = 0; index < nodeCount; ++index )
                    {
                        const bool bCandidate = listPlaced[index] == SW_FALSE && isReady( inoutListNode, listPlaced, index );
                        const bool bLaterName = pick == nodeCount || string_view( inoutListNode[index]->_pName ) > string_view( inoutListNode[pick]->_pName );
                        if ( bCandidate && bLaterName )
                            pick = index;
                    }
                    if ( pick == nodeCount )
                        return;
                    if ( pPrevious != nullptr )
                        inoutListNode[pick]->_listDependency.push_back( pPrevious );
                    listPlaced[pick] = SW_TRUE;
                    pPrevious        = inoutListNode[pick]->_pName;
                }
            }

        private:
            /** @brief @p index 노드의 의존(기반 노드 말고)이 모두 놓였는지입니다. */
            static bool isReady( const vector<unique_ptr<OnlineServerKitNode>>& listNode, const vector<uint8>& listPlaced, size_t index )
            {
                for ( const utf8* pDependency : listNode[index]->_listDependency )
                {
                    if ( string_view( pDependency ) == OnlineServerKitName::kBase )
                        continue;
                    bool bPlaced = false;
                    for ( size_t other = 0; other < listNode.size(); ++other )
                    {
                        bPlaced = bPlaced || ( listPlaced[other] == SW_TRUE && string_view( listNode[other]->_pName ) == pDependency );
                    }
                    if ( bPlaced == false )
                        return false;
                }
                return true;
            }

            unique_ptr<IOnlineServerKit> _kit;
            OnlineServerKitEntry         _entry;
            vector<const utf8*>          _listDependency; ///< 이 노드가 살아 있는 동안 컬렉션이 읽는다
            OnlineServerKitContext&      _context;
            optional<Error>&             _startupError; ///< 런타임의 기동 오류 칸(컬렉션 오류는 어느 노드인지만 적는다)
            const utf8*                  _pName;        ///< 팩토리의 이름(키트 모듈 라이브러리의 상수, 노드보다 오래 산다)
        };
    } // namespace
} // namespace sw

namespace sw
{
    /** @brief 조립의 부품입니다. 키트 범위가 키트와 기반 노드를 소유하고, `shutdown` 이 범위를 비운 뒤에야 나머지 부품이 사라집니다. */
    struct OnlineServerRuntime::Parts
    {
        MemoryServiceDatabase _memoryDatabase{};

        const IClock&                       _clock;                 ///< 설정이 준 단조 시계(메모리 캐시 · 키트의 HTTP 시한)
        unique_ptr<MemoryEphemeralDatabase> _memoryCacheDatabase{}; ///< 캐시 드라이버가 메모리일 때만 있다
        OnlineServerConfig                  _config{};
        SubsystemScope                      _kitScope{}; ///< 기반 노드와 키트 노드를 소유하고, 키트가 내놓은 인터페이스를 타입으로 찾게 한다
        OnlineServerKitContext              _context{ _kitScope, _clock };
        unique_ptr<IServiceStore>           _store{};
        unique_ptr<IEphemeralStore>         _cache{};
        unique_ptr<IStreamTransport>        _ownedTransport{};
        unique_ptr<IStreamTransport>        _ownedAdminTransport{};
        unique_ptr<ITLSContext>             _tlsContext{};
        ServiceScheduler                    _scheduler{};
        ServiceRetentionJob                 _retention{};
        RemoteConfig                        _remoteConfig{};
        OnlineServiceHost                   _host{};
        unique_ptr<EphemeralServerBus>      _bus{};
        unique_ptr<ServerBus>               _busFront{}; ///< 키트 문맥에 넘기는 타입 있는 앞이다. 전송(`_bus`)은 호스트만 받는다
        ServerRegistration                  _registration{};
        vector<OnlineServerKitNode*>        _listKit{};      ///< 초기화한 키트(초기화 순서). 소유는 `_kitScope`
        optional<Error>                     _startupError{}; ///< 노드가 남긴 기동 오류(어느 키트 · 무엇)
        ServiceHealthRegistry*              _pHealthRegistry{ nullptr };
        UnixMilliseconds                    _nextRemoteConfigReloadMs{ 0 };
        optional<int32>                     _healthCheckIndex; ///< 헬스 표에 올린 검사(표가 없으면 비어 있다)
        uint8                               _bRegistered{ SW_FALSE };
        explicit Parts( const IClock& clock )
            : _clock{ clock }
        {
        }
    };

    /**
     * @class OnlineServerRuntime::BaseNode
     * @brief 공통 기반(저장소 · 캐시 · 버스 · 예약 작업 · 호스트)의 노드입니다. 모든 키트가 이것에 의존하므로 처음 서고, 키트가 모두 멈춘 뒤 멈추며, 키트가 모두 종료한 뒤 종료합니다.
     * @details 호스트는 이 노드의 `initialize` 가 아니라 키트 `initialize` 뒤에 런타임이 띄운다(키트가 바인딩을 먼저 등록한다). 내리는 것은 이 노드의 `stop` 이다.
     */
    class OnlineServerRuntime::BaseNode final : public ISubsystem
    {
    public:
        /** @p settings 는 `initialize` 의 인자이고 그 함수가 도는 동안(컬렉션 초기화)만 읽는다. */
        BaseNode( OnlineServerRuntime& runtime, OnlineServerRuntimeConfig& settings, UnixMilliseconds nowMs )
            : _runtime{ runtime }
            , _settings{ settings }
            , _nowMs{ nowMs }
        {
        }

        SubsystemInitResult initializeSubsystem() override
        {
            expected<void> result = _runtime.createStores( _settings, _nowMs );
            if ( result.has_value() )
                return SubsystemInitResult::Succeeded;
            _runtime._parts->_startupError = std::move( result ).error();
            // 컬렉션은 초기화에 실패한 노드를 종료하지 않는다 — 반쯤 만든 저장소 · 캐시를 여기서 내린다.
            stopSubsystem();
            shutdownSubsystem();
            return SubsystemInitResult::Failed;
        }

        void stopSubsystem() override
        {
            // 키트가 모두 멈춘 뒤다. 시작하지 않았어도 종료해도 된다 — 호스트는 자기 초기화 비트를 보고, 스케줄러는 목록과 저장소 포인터를 비울 뿐이다.
            Parts& parts = *_runtime._parts;
            parts._host.shutdown();
            parts._retention.shutdown(); // 저장소가 살아 있을 때 맡긴 쓸기를 회수한다
            parts._scheduler.shutdown();
            if ( parts._store != nullptr )
            {
                parts._store->shutdown();
                (void)parts._store->pollCompletions(); // 남은 완료가 로그인 서비스 같은 로직을 부른다 — 키트 shutdown 전에
            }
        }

        void shutdownSubsystem() override
        {
            Parts& parts = *_runtime._parts;
            if ( parts._cache != nullptr )
                parts._cache->shutdown();
        }

        const utf8* getSubsystemName() const override { return OnlineServerKitName::kBase; }

    private:
        OnlineServerRuntime&       _runtime;
        OnlineServerRuntimeConfig& _settings;
        UnixMilliseconds           _nowMs;
    };
} // namespace sw

namespace sw
{
    OnlineServerRuntime::OnlineServerRuntime()
        : _parts{}
        , _bInitialized{ SW_FALSE }
    {
    }

    OnlineServerRuntime::~OnlineServerRuntime()
    {
        shutdown();
    }

    expected<void> OnlineServerRuntime::resolveStores( const ServerConfig& serverConfig, const OnlineServerConfig& onlineConfig, OnlineServerRuntimeConfig& outSettings )
    {
        using Internal                                 = OnlineServerRuntimeInternal;
        const expected<const ServerStoreEntry*> pStore = Internal::findEntry( serverConfig._listStore, onlineConfig._storeName, "_storeName", "_listStore" );
        if ( pStore.has_value() == false )
            return unexpected( pStore.error() );
        outSettings._store._driver     = ( *pStore )->_driver;
        outSettings._store._connection = ( *pStore )->_connection;
        if ( ServerSecret::read( ( *pStore )->_secretEnvironment, outSettings._store._secret ) == false )
            return unexpected( Error{ ErrorKind::NotFound, "the secret environment variable of the store is not set", ( *pStore )->_name } );
        const expected<const ServerCacheEntry*> pCache = Internal::findEntry( serverConfig._listCache, onlineConfig._cacheName, "_cacheName", "_listCache" );
        if ( pCache.has_value() == false )
            return unexpected( pCache.error() );
        outSettings._cache._driver     = ( *pCache )->_driver;
        outSettings._cache._connection = ( *pCache )->_endpoint;
        if ( ServerSecret::read( ( *pCache )->_secretEnvironment, outSettings._cache._secret ) == false )
            return unexpected( Error{ ErrorKind::NotFound, "the secret environment variable of the cache is not set", ( *pCache )->_name } );
        return {};
    }

    expected<void> OnlineServerRuntime::initialize( OnlineServerRuntimeConfig settings, UnixMilliseconds nowMs )
    {
        SW_ASSERT( _bInitialized == SW_FALSE );
        if ( settings._pClock == nullptr )
            return unexpected( Error{ ErrorKind::InvalidArgument, "the online runtime needs a monotonic clock (_pClock)" } );
        _parts        = make_unique<Parts>( *settings._pClock );
        _bInitialized = SW_TRUE; // 실패해도 `shutdown` 이 만든 것까지 정리한다
        Parts& parts  = *_parts;

        parts._config          = settings._config;
        parts._pHealthRegistry = settings._pHealthRegistry;
        if ( parts._pHealthRegistry != nullptr )
            parts._healthCheckIndex = parts._pHealthRegistry->registerCheck( kHealthCheckName, true );

        SubsystemCollection& collection = parts._kitScope.getCollection();
        const ISubsystem&    baseNode   = collection.add( make_unique<BaseNode>( *this, settings, nowMs ) );
        if ( expected<void> kits = addKits( settings._bReverseIndependentKitOrder == SW_TRUE ); kits.has_value() == false )
            return kits;
        if ( collection.initializeAll( SubsystemHostMask::All ) == false )
        {
            if ( parts._startupError.has_value() )
                return unexpected( *parts._startupError );
            return unexpected( Error{ ErrorKind::Cycle, "the online kits could not be ordered", collection.getError() } ); // 정렬 오류(순환)는 컬렉션 글이다
        }
        for ( const uint32 index : collection.getInitializedIndices() )
        {
            ISubsystem& node = collection.getSubsystem( index );
            if ( &node != &baseNode )
                parts._listKit.push_back( static_cast<OnlineServerKitNode*>( &node ) );
        }
        if ( expected<void> host = startHost( settings ); host.has_value() == false )
            return host;
        for ( OnlineServerKitNode* pKit : parts._listKit )
        {
            if ( expected<void> started = pKit->start(); started.has_value() == false )
                return started;
        }
        if ( expected<void> registered = registerServer( nowMs ); registered.has_value() == false )
            return registered;

        if ( parts._pHealthRegistry != nullptr && parts._healthCheckIndex.has_value() )
            parts._pHealthRegistry->setCheck( *parts._healthCheckIndex, HealthState::Ok, "" );
        vector<string> listKitName;
        collectKitNames( listKitName );
        string kitText;
        for ( const string& name : listKitName )
        {
            if ( kitText.empty() == false )
                kitText += ", ";
            kitText += name;
        }
        SW_LOG_INFO_ID( "OnlineServerRuntime.OnlineServicesListeningPort", "Online services listening on port %# (admin port %#, server %#, kits: %#)", parts._host.getListenPort(), parts._host.getAdminListenPort(),
                        parts._context._serverID, kitText.empty() ? "none" : kitText.c_str() );
        return {};
    }

    expected<void> OnlineServerRuntime::createStores( OnlineServerRuntimeConfig& settings, UnixMilliseconds nowMs )
    {
        using Internal = OnlineServerRuntimeInternal;
        Parts& parts   = *_parts;
        // 드라이버는 필수다 - 항목을 빠뜨린 설정이 재시작에 데이터를 잃는 저장소로 정상 기동하지 않게 한다.
        if ( settings._store._driver.empty() || settings._cache._driver.empty() )
        {
            StringUtil::wipeSecret( settings._store._secret );
            StringUtil::wipeSecret( settings._cache._secret );
            return unexpected( Error{ ErrorKind::InvalidArgument, "the online runtime needs a store and a cache driver (use 'memory' for an in-process one)" } );
        }
        if ( Internal::isMemoryDriver( settings._store._driver ) )
        {
            parts._store = make_unique<MemoryServiceStore>( &parts._memoryDatabase );
        }
        else
        {
            IServiceStoreDriver* const pDriver = game::findSubsystem<IServiceStoreDriver>();
            if ( pDriver == nullptr )
            {
                StringUtil::wipeSecret( settings._store._secret );
                return unexpected( Error{ ErrorKind::Unsupported, "the store driver needs a storage kit (GF_Server_SQLStore) that is not loaded", settings._store._driver } );
            }
            expected<unique_ptr<IServiceStore>> store =
                pDriver->createServiceStore( settings._store._driver, settings._store._connection, settings._store._secret, parts._config._storeWorkerCount, nowMs );
            StringUtil::wipeSecret( settings._store._secret );
            if ( store.has_value() == false )
                return unexpected( store.error() );
            parts._store = std::move( *store );
        }
        StringUtil::wipeSecret( settings._store._secret );

        if ( Internal::isMemoryDriver( settings._cache._driver ) )
        {
            parts._memoryCacheDatabase = make_unique<MemoryEphemeralDatabase>( parts._clock );
            parts._cache               = make_unique<MemoryEphemeralStore>( parts._memoryCacheDatabase.get() );
        }
        else
        {
            IEphemeralStoreDriver* const pDriver = game::findSubsystem<IEphemeralStoreDriver>();
            if ( pDriver == nullptr )
            {
                StringUtil::wipeSecret( settings._cache._secret );
                return unexpected( Error{ ErrorKind::Unsupported, "the cache driver needs a storage kit (GF_Server_CacheStore) that is not loaded", settings._cache._driver } );
            }
            expected<unique_ptr<IEphemeralStore>> cache = pDriver->createEphemeralStore( settings._cache._driver, settings._cache._connection, settings._cache._secret );
            StringUtil::wipeSecret( settings._cache._secret );
            if ( cache.has_value() == false )
                return unexpected( cache.error() );
            parts._cache = std::move( *cache );
        }
        StringUtil::wipeSecret( settings._cache._secret );

        const uint64 serverID = static_cast<uint64>( parts._config._serverID );
        parts._bus            = make_unique<EphemeralServerBus>( nullptr, serverID ); // 라우터는 호스트가 연 뒤에 묶는다
        parts._busFront       = make_unique<ServerBus>( *parts._bus );
        parts._scheduler.initialize( parts._store.get(), serverID );
        if ( parts._retention.initialize( parts._store.get(), &parts._scheduler, Internal::makeRetentionRules( parts._config ), Internal::makeRetentionSettings( parts._config ) ) == false )
            return unexpected( Error{ ErrorKind::ExternalFailure, "the retention sweep job could not be scheduled" } );
        parts._remoteConfig.requestReload( *parts._store );
        parts._nextRemoteConfigReloadMs = nowMs + Internal::kRemoteConfigReloadMs;

        OnlineServerKitContext& context = parts._context;
        context._pHost                  = &parts._host;
        context._pStore                 = parts._store.get();
        context._pCache                 = parts._cache.get();
        context._pBus                   = parts._busFront.get();
        context._pScheduler             = &parts._scheduler;
        context._pRemoteConfig          = &parts._remoteConfig;
        context._pMetricRegistry        = settings._pMetricRegistry;
        context._pHealthRegistry        = settings._pHealthRegistry;
        context._pConfig                = &parts._config;
        context._serverID               = serverID;
        context._nowMs                  = nowMs;
        ServerDescriptor& descriptor    = context._serverDescriptor;
        descriptor._serverID            = serverID;
        descriptor._kind                = parts._config._serverKind;
        descriptor._region              = parts._config._region;
        descriptor._address             = parts._config._publicAddress;
        descriptor._port                = settings._gamePort;
        descriptor._portCount           = settings._gamePortCount;
        descriptor._capacity            = parts._config._capacity;
        return {};
    }

    expected<void> OnlineServerRuntime::addKits( bool bReverseIndependentOrder )
    {
        Parts&                          parts = *_parts;
        vector<OnlineServerKitFactory*> listFactory;
        OnlineServerKitRegistry::collectFactories( listFactory );
        vector<const OnlineServerKitEntry*> listEntryByFactory( listFactory.size(), nullptr );
        if ( parts._config._listKit.empty() )
        {
            // 띄울 키트는 운영자가 고른다. 빌드에 포함된 키트를 모두 띄우면 데이터가 필요한 키트(경제의 카탈로그)가 기동을 막고, 띄울 생각이 없던 포트가 열린다.
            return unexpected( Error{ ErrorKind::InvalidArgument, "the online service config lists no kits (_listKit)" } );
        }
        for ( const OnlineServerKitEntry& entry : parts._config._listKit )
        {
            bool bFound = false;
            for ( size_t factoryIndex = 0; factoryIndex < listFactory.size(); ++factoryIndex )
            {
                if ( string_view( listFactory[factoryIndex]->_pName ) != entry._name )
                    continue;
                if ( listEntryByFactory[factoryIndex] != nullptr )
                    return unexpected( Error{ ErrorKind::AlreadyExists, "the online kit is listed twice", entry._name } );
                listEntryByFactory[factoryIndex] = &entry;
                bFound                           = true;
                break;
            }
            if ( bFound == false )
            {
                string known;
                for ( const OnlineServerKitFactory* pFactory : listFactory )
                {
                    known += known.empty() ? "" : ", ";
                    known += pFactory->_pName;
                }
                Error error{ ErrorKind::NotFound, "the online kit is not in this build", entry._name };
                error.appendDetail( " (registered: " ).appendDetail( known.empty() ? string_view( "none" ) : string_view( known ) ).appendDetail( ")" );
                return unexpected( error );
            }
        }
        // 순서는 설정에 적힌 순서가 아니라 노드의 이름 의존이 정한다(컬렉션의 위상 정렬).
        vector<unique_ptr<OnlineServerKitNode>> listNode;
        for ( size_t factoryIndex = 0; factoryIndex < listFactory.size(); ++factoryIndex )
        {
            const OnlineServerKitEntry* pEntry = listEntryByFactory[factoryIndex];
            if ( pEntry == nullptr )
                continue;
            unique_ptr<IOnlineServerKit> kit = listFactory[factoryIndex]->_pCreate();
            if ( kit == nullptr )
                return unexpected( Error{ ErrorKind::ExternalFailure, "the online kit could not be created", pEntry->_name } );
            vector<const utf8*> listDependency{ OnlineServerKitName::kBase };
            for ( const utf8* pRequired : kit->getRequiredKits() )
            {
                if ( OnlineServerRuntimeInternal::isKitListed( parts._config, pRequired ) == false )
                {
                    Error error{ ErrorKind::NotFound, "the online kit needs a kit the online service config does not list (_listKit)", pEntry->_name };
                    error.appendDetail( " needs " ).appendDetail( pRequired );
                    return unexpected( error );
                }
                listDependency.push_back( pRequired );
            }
            for ( const utf8* pOptional : kit->getOptionalKits() )
            {
                if ( OnlineServerRuntimeInternal::isKitListed( parts._config, pOptional ) )
                    listDependency.push_back( pOptional );
            }
            listNode.push_back(
                make_unique<OnlineServerKitNode>( std::move( kit ), listFactory[factoryIndex]->_pName, *pEntry, std::move( listDependency ), parts._context, parts._startupError ) );
        }
        if ( bReverseIndependentOrder )
            OnlineServerKitNode::chainReverseNameOrder( listNode );
        for ( unique_ptr<OnlineServerKitNode>& node : listNode )
        {
            (void)parts._kitScope.getCollection().add( std::move( node ) );
        }
        return {};
    }

    expected<void> OnlineServerRuntime::startHost( OnlineServerRuntimeConfig& settings )
    {
        Parts&                  parts = *_parts;
        OnlineServiceHostConfig hostSettings;
        hostSettings._transportSettings._ioThreadCount  = parts._config._ioThreadCount;
        hostSettings._transportSettings._maxConnections = parts._config._maxConnections;
        hostSettings._listenAddress                     = settings._listenAddress;
        hostSettings._pServiceStore                     = parts._store.get();
        hostSettings._pEphemeralStore                   = parts._cache.get();
        hostSettings._pServerBusTransport               = parts._bus.get();
        hostSettings._pRemoteConfig                     = &parts._remoteConfig;
        hostSettings._requestBurstPerRemote             = parts._config._requestBurstPerRemote;
        hostSettings._maxConnectionsPerRemote           = parts._config._maxConnectionsPerRemote;
        hostSettings._requestBurstPerAccount            = parts._config._requestBurstPerAccount;
        if ( settings._bTLS == SW_TRUE )
        {
            // 서비스 스트림은 TLS 1.3 이다. 경로가 둘 다 비면 Dev 만 개발용 인증서를 쓰고 Shipping 은 여기서 멈춘다.
            expected<unique_ptr<ITLSContext>> tlsContext =
                NetSecurity::createServerTLSContext( game::getSubsystem<INetSecurityProvider>(), settings._tlsCertificateFile, settings._tlsPrivateKeyFile, settings._tlsPrivateKeyPassphrase );
            StringUtil::wipeSecret( settings._tlsPrivateKeyPassphrase );
            if ( tlsContext.has_value() == false )
                return unexpected( tlsContext.error() );
            parts._tlsContext                                     = std::move( *tlsContext );
            hostSettings._endpointSettings._security._pTLSContext = parts._tlsContext.get();
        }
        IStreamTransport* pTransport = settings._pTransport;
        if ( pTransport == nullptr )
        {
            // 플레이어 서비스 리슨은 브라우저 클라이언트(WebSocket)도 받는다 — 첫 바이트로 가르는 장식자라 원시 TCP 클라이언트는 그대로 지난다.
            unique_ptr<IStreamTransport> platformTransport = StreamTransportFactory::createPlatformTransport();
            if ( platformTransport != nullptr )
                parts._ownedTransport = make_unique<WebSocketAcceptTransport>( std::move( platformTransport ) );
            pTransport = parts._ownedTransport.get();
        }
        if ( settings._bAdminListener == SW_TRUE )
        {
            // GM 리슨 — 서비스의 노출(`OnlineServiceExposure`)대로 GM 서비스는 이 리슨만, 플레이어 서비스는 서비스 리슨만 받는다(OnlineKits I6).
            hostSettings._adminListenAddress = settings._adminListenAddress;
            hostSettings._pAdminTransport    = settings._pAdminTransport;
            if ( hostSettings._pAdminTransport == nullptr )
            {
                parts._ownedAdminTransport    = StreamTransportFactory::createPlatformTransport();
                hostSettings._pAdminTransport = parts._ownedAdminTransport.get();
            }
        }
        const bool bAdminTransportMissing = settings._bAdminListener == SW_TRUE && hostSettings._pAdminTransport == nullptr;
        if ( pTransport == nullptr || bAdminTransportMissing )
            return unexpected( Error{ ErrorKind::Unsupported, "online services need a stream transport and this platform has none" } );
        if ( expected<void> host = parts._host.initialize( pTransport, hostSettings ); host.has_value() == false )
            return host;
        parts._context._pRouter                  = parts._host.getEphemeralRouter();
        [[maybe_unused]] const bool bRouterBound = parts._bus->bindRouter( parts._context._pRouter );
        SW_ASSERT( bRouterBound ); // 호스트를 띄우기 전에는 아무도 버스를 구독하지 않았다
        return {};
    }

    expected<void> OnlineServerRuntime::registerServer( UnixMilliseconds nowMs )
    {
        Parts& parts = *_parts;
        if ( parts._config._publicAddress.empty() )
            return {}; // 서비스 전용 서버 — 게임 서버로 고를 대상이 아니다
        const ServerDescriptor& descriptor = parts._context._serverDescriptor;
        if ( parts._registration.initialize( parts._context._pRouter, descriptor ) == false )
        {
            Error error{ ErrorKind::ExternalFailure, "this server could not be registered in the server directory", "kind '" };
            error.appendDetail( descriptor._kind ).appendDetail( "', region '" ).appendDetail( descriptor._region ).appendDetail( "'" );
            return unexpected( error );
        }
        parts._registration.setState( ServerState::Open );
        parts._registration.update( nowMs ); // 기동 안에서 바로 쓴다 — 첫 틱에 서버 목록을 읽는 키트(매칭 · 디렉터리)가 이 서버를 본다. 첫 틱의 키트 뒤로 미루면 첫 읽기가 빈 목록을 받고 읽기 주기(벽시계)만큼 배정이 늦다
        parts._bRegistered = SW_TRUE;
        return {};
    }

    void OnlineServerRuntime::shutdown()
    {
        if ( _bInitialized == SW_FALSE )
            return;
        _bInitialized = SW_FALSE;
        Parts& parts  = *_parts;
        if ( parts._bRegistered == SW_TRUE )
            parts._registration.shutdown();
        // 키트 stop(역순) → 기반 stop(호스트 → 예약 작업 → 저장소와 남은 완료) → 키트 shutdown(역순) → 기반 shutdown(캐시) → 역순 소멸.
        // 순서는 모든 키트가 기반 노드에 의존하는 것 하나에서 나온다. 초기화에 성공한 노드만 종료를 받는다.
        parts._listKit.clear();
        parts._kitScope.destroyAll();
        if ( parts._pHealthRegistry != nullptr && parts._healthCheckIndex.has_value() )
            parts._pHealthRegistry->setCheck( *parts._healthCheckIndex, HealthState::Failing, "stopped" );
        _parts.reset();
    }

    void OnlineServerRuntime::update( UnixMilliseconds nowMs )
    {
        if ( _bInitialized == SW_FALSE )
            return;
        Parts& parts = *_parts;
        parts._host.update( nowMs );
        parts._scheduler.update( nowMs );
        for ( OnlineServerKitNode* pKit : parts._listKit )
        {
            pKit->update( nowMs );
        }
        if ( parts._bRegistered == SW_TRUE )
        {
            parts._registration.setLoad( parts._host.getConnectionCount() );
            parts._registration.update( nowMs );
        }
        if ( nowMs >= parts._nextRemoteConfigReloadMs )
        {
            parts._nextRemoteConfigReloadMs = nowMs + OnlineServerRuntimeInternal::kRemoteConfigReloadMs;
            parts._remoteConfig.requestReload( *parts._store );
        }
    }

    OnlineServiceHost& OnlineServerRuntime::getHost()
    {
        if ( _parts == nullptr )
            ::sw::internal::reportMissingRequiredService( SW_FUNCTION_SIGNATURE );
        return _parts->_host;
    }

    uint16 OnlineServerRuntime::getListenPort() const
    {
        return _bInitialized == SW_TRUE ? _parts->_host.getListenPort() : 0;
    }

    int32 OnlineServerRuntime::getConnectionCount() const
    {
        return _bInitialized == SW_TRUE ? _parts->_host.getConnectionCount() : 0;
    }

    int32 OnlineServerRuntime::getOpenWorkCount() const
    {
        if ( _bInitialized == SW_FALSE )
            return 0;
        int32 count = 0;
        for ( const OnlineServerKitNode* pKit : _parts->_listKit )
        {
            count += pKit->getOpenWorkCount();
        }
        return count;
    }

    void OnlineServerRuntime::beginShutdown( const ServerShutdownNotice& notice )
    {
        if ( _bInitialized == SW_FALSE )
            return;
        for ( OnlineServerKitNode* pKit : _parts->_listKit )
        {
            pKit->beginShutdown( notice );
        }
    }

    void OnlineServerRuntime::collectKitNames( vector<string>& outListName ) const
    {
        outListName.clear();
        if ( _parts == nullptr )
            return;
        for ( const OnlineServerKitNode* pKit : _parts->_listKit )
        {
            outListName.push_back( pKit->getKitName() );
        }
    }

    const OnlineServerKitContext& OnlineServerRuntime::getContext() const
    {
        if ( _parts == nullptr )
            ::sw::internal::reportMissingRequiredService( SW_FUNCTION_SIGNATURE );
        return _parts->_context;
    }
} // namespace sw
