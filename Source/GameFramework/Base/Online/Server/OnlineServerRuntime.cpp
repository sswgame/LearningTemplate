#include "pch.h"

#include "GameFramework/Base/Online/Server/OnlineServerRuntime.h"

#include "Core/Network/Security/INetSecurityProvider.h"
#include "Core/Network/Transport/IStreamTransport.h"

#include "Engine/Observability/ServiceHealthRegistry.h"

#include "GameFramework/Base/Online/Bus/EphemeralServerBus.h"
#include "GameFramework/Base/Online/Cache/MemoryEphemeralStore.h"
#include "GameFramework/Base/Online/Config/RemoteConfig.h"
#include "GameFramework/Base/Online/Directory/ServerRegistration.h"
#include "GameFramework/Base/Online/Schedule/ServiceScheduler.h"
#include "GameFramework/Base/Online/Security/NetSecurity.h"
#include "GameFramework/Base/Online/Server/OnlineServerKit.h"
#include "GameFramework/Base/Online/Server/OnlineStoreDriverTable.h"
#include "GameFramework/Base/Online/Service/OnlineServiceHost.h"
#include "GameFramework/Base/Online/Store/MemoryServiceStore.h"

namespace sw
{
    SW_LOG_CALLER( "OnlineServerRuntime" );

    namespace
    {
        struct OnlineServerRuntimeInternal
        {
            /** @brief 원격 설정을 저장소에서 다시 읽는 간격입니다. 바꾼 서버는 버스로 바로 알리므로, 이것은 버스를 놓친 서버의 늦은 따라잡기다. */
            static constexpr int64 kRemoteConfigReloadMs = 60000;

            static bool isMemoryDriver( string_view driver ) { return driver.empty() || driver == OnlineStoreDriverTable::kMemoryDriverName; }

            /** @brief 비밀을 0 으로 덮고 비웁니다. 드라이버에 넘긴 뒤 부릅니다. */
            static void wipeSecret( string& inoutSecret )
            {
                for ( utf8& character : inoutSecret )
                {
                    character = 0;
                }
                inoutSecret.clear();
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    /** @brief 조립의 부품입니다. 선언 순서가 사라지는 순서의 반대입니다 — 버스와 키트는 호스트(라우터)보다 먼저, 저장소는 호스트보다 나중에 사라집니다. */
    struct OnlineServerRuntime::Parts
    {
        /** @brief 붙은 키트 하나입니다. */
        struct KitInstance
        {
            unique_ptr<IOnlineServerKit> _kit{};
            OnlineServerKitEntry         _entry{};
        };

        MemoryServiceDatabase          _memoryDatabase{};
        MemoryEphemeralDatabase        _memoryCacheDatabase{};
        OnlineServerConfig             _config{};
        OnlineServerKitContext         _context{};
        unique_ptr<IServiceStore>      _store{};
        unique_ptr<IEphemeralStore>    _cache{};
        unique_ptr<IStreamTransport>   _ownedTransport{};
        unique_ptr<ITLSContext>        _tlsContext{};
        ServiceScheduler               _scheduler{};
        RemoteConfig                   _remoteConfig{};
        OnlineServiceHost              _host{};
        unique_ptr<EphemeralServerBus> _bus{};
        unique_ptr<ServerBus>          _busFront{}; ///< 키트 문맥에 넘기는 타입 있는 앞이다. 전송(`_bus`)은 호스트만 받는다
        ServerRegistration             _registration{};
        vector<KitInstance>            _listKit{};
        ServiceHealthRegistry*         _pHealthRegistry{ nullptr };
        int64                          _nextRemoteConfigReloadMs{ 0 };
        int32                          _healthCheckIndex{ -1 };
        uint8                          _bMemoryCache{ SW_FALSE };
        uint8                          _bManualCacheTime{ SW_FALSE };
        uint8                          _bRegistered{ SW_FALSE };
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

    bool OnlineServerRuntime::initialize( OnlineServerRuntimeConfig settings, int64 nowMs, string& outError )
    {
        SW_ASSERT( _bInitialized == SW_FALSE );
        _parts        = make_unique<Parts>();
        _bInitialized = SW_TRUE; // 실패해도 `shutdown` 이 만든 것까지 내린다
        Parts& parts  = *_parts;

        parts._config           = settings._config;
        parts._bManualCacheTime = settings._bManualCacheTime;
        parts._pHealthRegistry  = settings._pHealthRegistry;
        if ( parts._pHealthRegistry != nullptr )
            parts._healthCheckIndex = parts._pHealthRegistry->registerCheck( kHealthCheckName, true );

        const bool bAssembled = createStores( settings, nowMs, outError ) && createKits( settings, outError ) && startHost( settings, outError );
        if ( bAssembled == false )
            return false;
        for ( Parts::KitInstance& instance : parts._listKit )
        {
            if ( instance._kit->start( parts._context, outError ) == false )
            {
                outError = "online kit '" + instance._entry._name + "' could not start: " + outError;
                return false;
            }
        }
        if ( registerServer( settings, outError ) == false )
            return false;

        if ( parts._pHealthRegistry != nullptr )
            parts._pHealthRegistry->setCheck( parts._healthCheckIndex, HealthState::Ok, "" );
        vector<string> listKitName;
        collectKitNames( listKitName );
        string kitText;
        for ( const string& name : listKitName )
        {
            if ( kitText.empty() == false )
                kitText += ", ";
            kitText += name;
        }
        SW_LOG_INFO( "Online services listening on port %# (server %#, kits: %#)", parts._host.getListenPort(), parts._context._serverID,
                     kitText.empty() ? "none" : kitText.c_str() );
        return true;
    }

    bool OnlineServerRuntime::createStores( OnlineServerRuntimeConfig& settings, int64 nowMs, string& outError )
    {
        using Internal = OnlineServerRuntimeInternal;
        Parts& parts   = *_parts;
        if ( Internal::isMemoryDriver( settings._store._driver ) )
        {
            parts._store = make_unique<MemoryServiceStore>( &parts._memoryDatabase );
        }
        else
        {
            const OnlineStoreDriverTable::CreateServiceStoreFunction pCreate = OnlineStoreDriverTable::findServiceStoreFactory();
            if ( pCreate == nullptr )
            {
                outError = "store driver '" + settings._store._driver + "' needs a storage kit (GF_Server_SQLStore) that is not loaded";
                Internal::wipeSecret( settings._store._secret );
                return false;
            }
            parts._store = pCreate( settings._store._driver, settings._store._connection, settings._store._secret, parts._config._storeWorkerCount, nowMs, outError );
        }
        Internal::wipeSecret( settings._store._secret );
        if ( parts._store == nullptr )
            return false;

        if ( Internal::isMemoryDriver( settings._cache._driver ) )
        {
            parts._bMemoryCache = SW_TRUE;
            if ( parts._bManualCacheTime == SW_TRUE )
                parts._memoryCacheDatabase.setManualTimeMs( nowMs );
            parts._cache = make_unique<MemoryEphemeralStore>( &parts._memoryCacheDatabase );
        }
        else
        {
            const OnlineStoreDriverTable::CreateEphemeralStoreFunction pCreate = OnlineStoreDriverTable::findEphemeralStoreFactory();
            if ( pCreate == nullptr )
            {
                outError = "cache driver '" + settings._cache._driver + "' needs a storage kit (GF_Server_CacheStore) that is not loaded";
                Internal::wipeSecret( settings._cache._secret );
                return false;
            }
            parts._cache = pCreate( settings._cache._driver, settings._cache._connection, settings._cache._secret, outError );
        }
        Internal::wipeSecret( settings._cache._secret );
        if ( parts._cache == nullptr )
            return false;

        const uint64 serverID = static_cast<uint64>( parts._config._serverID );
        parts._bus            = make_unique<EphemeralServerBus>( nullptr, serverID ); // 라우터는 호스트가 연 뒤에 묶는다
        parts._busFront       = make_unique<ServerBus>( *parts._bus );
        parts._scheduler.initialize( parts._store.get(), serverID );
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
        return true;
    }

    bool OnlineServerRuntime::createKits( const OnlineServerRuntimeConfig& settings, string& outError )
    {
        (void)settings;
        Parts&                          parts = *_parts;
        vector<OnlineServerKitFactory*> listFactory;
        OnlineServerKitRegistry::collectFactories( listFactory ); // 시작 순서대로
        vector<const OnlineServerKitEntry*> listEntryByFactory( listFactory.size(), nullptr );
        if ( parts._config._listKit.empty() )
        {
            // 띄울 키트는 운영자가 고른다. 빌드에 든 키트를 모두 띄우면 데이터가 필요한 키트(경제의 카탈로그)가 기동을 막고, 띄울 생각이 없던 포트가 열린다.
            outError = "the online service config lists no kits (_listKit)";
            return false;
        }
        for ( const OnlineServerKitEntry& entry : parts._config._listKit )
        {
            bool bFound = false;
            for ( size_t factoryIndex = 0; factoryIndex < listFactory.size(); ++factoryIndex )
            {
                if ( string_view( listFactory[factoryIndex]->_pName ) != entry._name )
                    continue;
                if ( listEntryByFactory[factoryIndex] != nullptr )
                {
                    outError = "online kit '" + entry._name + "' is listed twice";
                    return false;
                }
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
                outError = "online kit '" + entry._name + "' is not in this build (registered: " + ( known.empty() ? string( "none" ) : known ) + ")";
                return false;
            }
        }
        // 설정에 적힌 순서와 상관없이 시작 순서(인터페이스를 내놓는 키트가 먼저)로 붙인다.
        for ( size_t factoryIndex = 0; factoryIndex < listFactory.size(); ++factoryIndex )
        {
            if ( listEntryByFactory[factoryIndex] == nullptr )
                continue;
            Parts::KitInstance& instance = parts._listKit.emplace_back();
            instance._entry              = *listEntryByFactory[factoryIndex];
            instance._kit                = listFactory[factoryIndex]->_pCreate();
            if ( instance._kit == nullptr )
            {
                outError = "online kit '" + instance._entry._name + "' could not be created";
                return false;
            }
            if ( instance._kit->initialize( parts._context, instance._entry, outError ) == false )
            {
                outError = "online kit '" + instance._entry._name + "' could not initialize: " + outError;
                return false;
            }
        }
        return true;
    }

    bool OnlineServerRuntime::startHost( OnlineServerRuntimeConfig& settings, string& outError )
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
        hostSettings._requestBurstPerAccount            = parts._config._requestBurstPerAccount;
        if ( settings._bTLS == SW_TRUE )
        {
            // 서비스 스트림은 TLS 1.3 이다(결정 기록 5-9). 경로가 둘 다 비면 Dev 만 개발용 인증서를 쓰고 Shipping 은 여기서 멈춘다.
            parts._tlsContext = NetSecurity::createServerTLSContext( settings._tlsCertificateFile, settings._tlsPrivateKeyFile, settings._tlsPrivateKeyPassphrase, outError );
            OnlineServerRuntimeInternal::wipeSecret( settings._tlsPrivateKeyPassphrase );
            if ( parts._tlsContext == nullptr )
            {
                outError = "online service TLS context could not be created: " + outError;
                return false;
            }
            hostSettings._endpointSettings._security._pTLSContext = parts._tlsContext.get();
        }
        IStreamTransport* pTransport = settings._pTransport;
        if ( pTransport == nullptr )
        {
            parts._ownedTransport = StreamTransportFactory::createPlatformTransport();
            pTransport            = parts._ownedTransport.get();
        }
        if ( pTransport == nullptr )
        {
            outError = "online services need a stream transport and this platform has none";
            return false;
        }
        if ( parts._host.initialize( pTransport, hostSettings, outError ) == false )
            return false;
        parts._context._pRouter                  = parts._host.getEphemeralRouter();
        [[maybe_unused]] const bool bRouterBound = parts._bus->bindRouter( parts._context._pRouter );
        SW_ASSERT( bRouterBound ); // 호스트를 띄우기 전에는 아무도 버스를 구독하지 않았다
        return true;
    }

    bool OnlineServerRuntime::registerServer( const OnlineServerRuntimeConfig& settings, string& outError )
    {
        Parts& parts = *_parts;
        if ( parts._config._publicAddress.empty() )
            return true; // 서비스 전용 서버 — 게임 서버로 고를 대상이 아니다
        ServerDescriptor descriptor;
        descriptor._serverID = parts._context._serverID;
        descriptor._kind     = parts._config._serverKind;
        descriptor._region   = parts._config._region;
        descriptor._address  = parts._config._publicAddress;
        descriptor._port     = settings._gamePort;
        descriptor._capacity = parts._config._capacity;
        if ( parts._registration.initialize( parts._context._pRouter, descriptor ) == false )
        {
            outError = "this server could not be registered in the server directory (kind '" + descriptor._kind + "', region '" + descriptor._region + "')";
            return false;
        }
        parts._registration.setState( ServerState::Open );
        parts._bRegistered = SW_TRUE;
        return true;
    }

    void OnlineServerRuntime::shutdown()
    {
        if ( _bInitialized == SW_FALSE )
            return;
        _bInitialized = SW_FALSE;
        Parts& parts  = *_parts;
        if ( parts._bRegistered == SW_TRUE )
            parts._registration.shutdown();
        // 키트 — 캐시 요청을 취소하고 구독을 풀고 저장 일을 회수한다(호스트와 저장소가 살아 있을 때). 반대 순서다.
        for ( size_t index = parts._listKit.size(); index > 0; --index )
        {
            Parts::KitInstance& instance = parts._listKit[index - 1];
            if ( instance._kit != nullptr )
                instance._kit->stop();
        }
        // 둘 다 시작하지 않았어도 내려도 된다 — 호스트는 자기 초기화 비트를 보고, 스케줄러는 목록과 저장소 포인터를 비울 뿐이다.
        parts._host.shutdown();
        parts._scheduler.shutdown();
        if ( parts._store != nullptr )
        {
            parts._store->shutdown();
            (void)parts._store->pollCompletions(); // 남은 완료가 로그인 서비스 같은 로직을 부른다 — 키트 shutdown 전에
        }
        for ( size_t index = parts._listKit.size(); index > 0; --index )
        {
            Parts::KitInstance& instance = parts._listKit[index - 1];
            if ( instance._kit != nullptr )
                instance._kit->shutdown();
        }
        parts._listKit.clear();
        if ( parts._cache != nullptr )
            parts._cache->shutdown();
        if ( parts._pHealthRegistry != nullptr && parts._healthCheckIndex >= 0 )
            parts._pHealthRegistry->setCheck( parts._healthCheckIndex, HealthState::Failing, "stopped" );
        _parts.reset();
    }

    void OnlineServerRuntime::update( int64 nowMs )
    {
        if ( _bInitialized == SW_FALSE )
            return;
        Parts& parts = *_parts;
        // 메모리 캐시의 만료(TTL)를 서비스와 같은 시계로 잰다 — 캐시가 제 단조 시계를 쓰면 가짜 시계에서 서버 등록 · 매칭 표가 서비스 시각보다 먼저 만료된다.
        if ( parts._bMemoryCache == SW_TRUE && parts._bManualCacheTime == SW_TRUE )
            parts._memoryCacheDatabase.setManualTimeMs( nowMs );
        parts._host.update( nowMs );
        parts._scheduler.update( nowMs );
        for ( Parts::KitInstance& instance : parts._listKit )
        {
            instance._kit->update( nowMs );
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
        SW_ASSERT( _parts != nullptr );
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

    void OnlineServerRuntime::collectKitNames( vector<string>& outListName ) const
    {
        outListName.clear();
        if ( _parts == nullptr )
            return;
        for ( const Parts::KitInstance& instance : _parts->_listKit )
        {
            outListName.push_back( instance._entry._name );
        }
    }

    const OnlineServerKitContext& OnlineServerRuntime::getContext() const
    {
        SW_ASSERT( _parts != nullptr );
        return _parts->_context;
    }

    MemoryEphemeralDatabase* OnlineServerRuntime::findMemoryCacheDatabase()
    {
        return _parts != nullptr && _parts->_bMemoryCache == SW_TRUE ? &_parts->_memoryCacheDatabase : nullptr;
    }
} // namespace sw
