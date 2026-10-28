#include "pch.h"

#include "Core/Container/VectorUtil.h"
#include "Core/Time/Clock.h"
#include "Core/Time/WallClock.h"

#include "Engine/Config/ConfigManager.h"
#include "Engine/Config/Server/ServerConfig.h"
#include "Engine/Config/Server/ServerSecret.h"
#include "Engine/DedicatedServer/ServerServiceRegistry.h"

#include "GameFramework/Base/Online/Server/OnlineServerConfig.h"
#include "GameFramework/Base/Online/Server/OnlineServerRuntime.h"

// 전용 서버의 서버 서비스로 온라인 서비스 조립(`OnlineServerRuntime`)을 띄운다. 서버 설정 옆 `<이름>.online.json` 이 있을 때만 띄운다.
// 서버 코드가 없는 빌드(클라이언트 배포본)에는 이 파일이 비어 있다.

#if defined( SW_WITH_SERVER_CODE )
namespace sw
{
    SW_LOG_CALLER( "OnlineServerService" );

    namespace
    {
        struct OnlineServerServiceInternal
        {
            /** @brief 서버 설정의 항목 목록에서 @p name 을 찾습니다. 비우면 첫 항목입니다. 항목이 없으면 nullptr 이고 그때는 메모리 드라이버를 씁니다. */
            template <typename TEntry>
            static const TEntry* findEntry( const vector<TEntry>& listEntry, const string& name )
            {
                if ( name.empty() )
                    return listEntry.empty() ? nullptr : &listEntry.front();
                return VectorUtil::findBy<&TEntry::_name>( listEntry, name );
            }
        };

        /**
         * @class OnlineServerService
         * @brief 온라인 서비스 조립을 서버 서비스로 감쌉니다. 서버 설정과 온라인 설정을 런타임 설정으로 옮기고, 비밀은 환경 변수에서 읽어 넘긴 뒤 비웁니다.
         */
        class OnlineServerService final : public IServerService
        {
        public:
            [[nodiscard]] bool initialize( const ServerServiceContext& context, string& outError ) override;
            void               shutdown() override { _runtime.shutdown(); }
            void               update( UnixMilliseconds nowMs ) override { _runtime.update( nowMs ); }
            int32              getConnectionCount() const override { return _runtime.getConnectionCount(); }
            int32              getOpenWorkCount() const override { return _runtime.getOpenWorkCount(); }
            void               beginShutdown( const ServerShutdownNotice& notice ) override { _runtime.beginShutdown( notice ); }

        private:
            [[nodiscard]] static bool fillStoreSettings( const ServerConfig& serverConfig, const OnlineServerConfig& onlineConfig, OnlineServerRuntimeConfig& outSettings,
                                                         string& outError );

            SystemMonotonicClock _clock; ///< 런타임이 빌린다(메모리 캐시 만료 · 외부 HTTP 시한). 런타임보다 먼저 선언한다
            OnlineServerRuntime  _runtime;
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool OnlineServerService::initialize( const ServerServiceContext& context, string& outError )
    {
        const bool bNoServerConfig = context._pServerConfig == nullptr || context._pConfigManager == nullptr || context._serverConfigPath.empty();
        if ( bNoServerConfig )
            return true; // 서버 설정이 없는 호스트(테스트)에서는 할 일이 없다
        const string path = OnlineServerConfig::makeConfigPath( context._serverConfigPath );
        // 설정 타입이 이 모듈에 있다 — 엔진의 ConfigManager 는 모듈보다 오래 살므로 테이블에 등록하지 않고 이 서비스가 직접 소유한다.
        OnlineServerConfig     onlineConfig;
        const ConfigReadResult result = context._pConfigManager->readConfigFileInto( path, onlineConfig );
        if ( result == ConfigReadResult::Missing )
        {
            SW_LOG_INFO( "Online services are off - '%#' does not exist", path.c_str() );
            return true;
        }
        if ( result != ConfigReadResult::Loaded )
        {
            outError = "online service config '" + path + "' is invalid";
            return false;
        }
        const ServerConfig& serverConfig = *context._pServerConfig;
        if ( serverConfig._servicePort <= 0 )
        {
            outError = "online service config '" + path + "' exists but the server config's _servicePort is 0";
            return false;
        }

        OnlineServerRuntimeConfig settings;
        settings._config          = std::move( onlineConfig );
        settings._pMetricRegistry = context._pMetricRegistry;
        settings._pHealthRegistry = context._pHealthRegistry;
        settings._pClock          = &_clock;
        settings._gamePort        = static_cast<uint16>( serverConfig._gamePort );
        // 연이은 포트 샤드면 배정이 계정마다 한 포트를 고른다. 한 포트를 나누면(`_bGamePortReuse`) 커널이 나누므로 알릴 포트는 하나다.
        settings._gamePortCount = static_cast<uint8>( serverConfig._bGamePortReuse ? 1 : serverConfig._gamePortShardCount );
        if ( NetAddress::parse( serverConfig._listenAddress, static_cast<uint16>( serverConfig._servicePort ), settings._listenAddress ) == false )
        {
            outError = "server config _listenAddress '" + serverConfig._listenAddress + "' is not an IPv4 address";
            return false;
        }
        settings._listenAddress._port = static_cast<uint16>( serverConfig._servicePort ); // 포트는 `_servicePort` 가 기준이다
        if ( serverConfig._adminServicePort > 0 )
        {
            const uint16 adminPort = static_cast<uint16>( serverConfig._adminServicePort );
            if ( NetAddress::parse( serverConfig._adminListenAddress, adminPort, settings._adminListenAddress ) == false )
            {
                outError = "server config _adminListenAddress '" + serverConfig._adminListenAddress + "' is not an IPv4 address";
                return false;
            }
            settings._adminListenAddress._port = adminPort;
            settings._bAdminListener           = SW_TRUE;
        }
        if ( fillStoreSettings( serverConfig, settings._config, settings, outError ) == false )
            return false;
        // 서비스 스트림은 TLS 1.3 이다. 인증서와 키 경로는 서버 설정에 있고, 키 암호는 환경 변수에서 읽는다.
        settings._bTLS               = SW_TRUE;
        settings._tlsCertificateFile = serverConfig._tlsCertificateFile;
        settings._tlsPrivateKeyFile  = serverConfig._tlsPrivateKeyFile;
        if ( ServerSecret::read( serverConfig._tlsPrivateKeySecretEnvironment, settings._tlsPrivateKeyPassphrase ) == false )
        {
            outError = "the TLS private key passphrase environment variable is not set (name in _tlsPrivateKeySecretEnvironment)";
            return false;
        }
        return _runtime.initialize( std::move( settings ), WallClock::nowUnixMilliseconds(), outError ); // 예외 — 호스트 최상위(서버 서비스의 뿌리)가 시각을 정한다
    }

    bool OnlineServerService::fillStoreSettings( const ServerConfig& serverConfig, const OnlineServerConfig& onlineConfig, OnlineServerRuntimeConfig& outSettings,
                                                 string& outError )
    {
        const ServerStoreEntry* pStore = OnlineServerServiceInternal::findEntry( serverConfig._listStore, onlineConfig._storeName );
        if ( pStore == nullptr && onlineConfig._storeName.empty() == false )
        {
            outError = "store '" + onlineConfig._storeName + "' is not in the server config _listStore";
            return false;
        }
        if ( pStore != nullptr )
        {
            outSettings._store._driver     = pStore->_driver;
            outSettings._store._connection = pStore->_connection;
            if ( ServerSecret::read( pStore->_secretEnvironment, outSettings._store._secret ) == false )
            {
                outError = "the secret environment variable of store '" + pStore->_name + "' is not set";
                return false;
            }
        }
        const ServerCacheEntry* pCache = OnlineServerServiceInternal::findEntry( serverConfig._listCache, onlineConfig._cacheName );
        if ( pCache == nullptr && onlineConfig._cacheName.empty() == false )
        {
            outError = "cache '" + onlineConfig._cacheName + "' is not in the server config _listCache";
            return false;
        }
        if ( pCache != nullptr )
        {
            outSettings._cache._driver     = pCache->_driver;
            outSettings._cache._connection = pCache->_endpoint;
            if ( ServerSecret::read( pCache->_secretEnvironment, outSettings._cache._secret ) == false )
            {
                outError = "the secret environment variable of cache '" + pCache->_name + "' is not set";
                return false;
            }
        }
        return true;
    }

    /** @brief 서버 서비스 중 앞쪽에 둔다. 게임과 키트가 올린 서버 서비스가 온라인 서비스를 볼 수 있게 한다. */
    SW_REGISTER_SERVER_SERVICE( OnlineServerService, "online_services", 100 );
} // namespace sw
#endif
