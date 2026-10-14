#include "pch.h"

#include "Core/Time/WallClock.h"

#include "Engine/Config/ConfigManager.h"
#include "Engine/Config/Server/ServerConfig.h"
#include "Engine/Config/Server/ServerSecret.h"
#include "Engine/Observability/ServerServiceRegistry.h"

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
                for ( const TEntry& entry : listEntry )
                {
                    if ( entry._name == name )
                        return &entry;
                }
                return nullptr;
            }
        };

        /**
         * @class OnlineServerService
         * @brief 온라인 서비스 조립을 서버 서비스로 감쌉니다. 서버 설정과 온라인 설정을 런타임 설정으로 옮기고, 비밀은 환경 변수에서 읽어 넘긴 뒤 비웁니다.
         */
        class OnlineServerService final : public IServerService
        {
        public:
            bool  initialize( const ServerServiceContext& context, string& outError ) override;
            void  shutdown() override { _runtime.shutdown(); }
            void  tick( int64 nowMs ) override { _runtime.tick( nowMs ); }
            int32 getConnectionCount() const override { return _runtime.getConnectionCount(); }

        private:
            [[nodiscard]] static bool fillStoreSettings( const ServerConfig& serverConfig, const OnlineServerConfig& onlineConfig, OnlineServerRuntimeSettings& outSettings,
                                                         string& outError );

            OnlineServerRuntime _runtime;
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
        const string           path   = OnlineServerConfig::makeConfigPath( context._serverConfigPath );
        const ConfigReadResult result = context._pConfigManager->loadConfigFile<OnlineServerConfig>( path );
        if ( result == ConfigReadResult::Missing )
        {
            SW_LOG_INFO( "Online services are off - '%#' does not exist", path.c_str() );
            return true;
        }
        const OnlineServerConfig* pOnlineConfig = result == ConfigReadResult::Loaded ? context._pConfigManager->getConfig<OnlineServerConfig>() : nullptr;
        if ( pOnlineConfig == nullptr )
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

        OnlineServerRuntimeSettings settings;
        settings._config          = *pOnlineConfig;
        settings._pMetricRegistry = context._pMetricRegistry;
        settings._pHealthRegistry = context._pHealthRegistry;
        settings._gamePort        = static_cast<uint16>( serverConfig._gamePort );
        if ( NetAddress::parse( serverConfig._listenAddress, static_cast<uint16>( serverConfig._servicePort ), settings._listenAddress ) == false )
        {
            outError = "server config _listenAddress '" + serverConfig._listenAddress + "' is not an IPv4 address";
            return false;
        }
        settings._listenAddress._port = static_cast<uint16>( serverConfig._servicePort ); // 포트는 `_servicePort` 가 기준이다
        if ( fillStoreSettings( serverConfig, *pOnlineConfig, settings, outError ) == false )
            return false;
        // 서비스 스트림은 TLS 1.3 이다. 인증서와 키 경로는 서버 설정에 있고, 키 암호는 환경 변수에서 읽는다(결정 기록 5-9).
        settings._bTLS               = SW_TRUE;
        settings._tlsCertificateFile = serverConfig._tlsCertificateFile;
        settings._tlsPrivateKeyFile  = serverConfig._tlsPrivateKeyFile;
        if ( ServerSecret::read( serverConfig._tlsPrivateKeySecretEnvironment, settings._tlsPrivateKeyPassphrase ) == false )
        {
            outError = "the TLS private key passphrase environment variable is not set (name in _tlsPrivateKeySecretEnvironment)";
            return false;
        }
        return _runtime.initialize( std::move( settings ), WallClock::nowUnixMilliseconds(), outError );
    }

    bool OnlineServerService::fillStoreSettings( const ServerConfig& serverConfig, const OnlineServerConfig& onlineConfig, OnlineServerRuntimeSettings& outSettings,
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
