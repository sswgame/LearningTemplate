#include "pch.h"

#include "Core/Time/Clock.h"
#include "Core/Time/WallClock.h"

#include "Engine/Config/ConfigManager.h"
#include "Engine/Config/Server/ServerConfig.h"
#include "Engine/Config/Server/ServerSecret.h"
#include "Engine/DedicatedServer/ServerServiceRegistry.h"

#include "GameFramework/Runtime/Online/Server/OnlineServerConfig.h"
#include "GameFramework/Runtime/Online/Server/OnlineServerRuntime.h"

// 전용 서버의 서버 서비스로 온라인 서비스 조립(`OnlineServerRuntime`)을 띄운다. 서버 설정 옆 `<이름>.online.json` 이 있을 때만 띄운다.
// 등록은 모든 구성에 있지만 서버 서비스는 전용 서버 호스트(`ServerServiceManager`)만 띄운다 — 구성 매크로 갈래는 없다(`Core/Foundation/BuildInfo.h`).
// 같은 런타임(`OnlineServerRuntime`)이 이미 모든 구성에서 지어진다. 클라이언트 배포본에서 빼려면 서버 전용 폴더 · 모듈(`GF_Server_<X>`)로 옮긴다.

namespace sw
{
    namespace
    {
        /**
         * @class OnlineServerService
         * @brief 온라인 서비스 조립을 서버 서비스로 감쌉니다. 서버 설정과 온라인 설정을 런타임 설정으로 옮기고, 비밀은 환경 변수에서 읽어 넘긴 뒤 비웁니다.
         */
        class OnlineServerService final : public IServerService
        {
        public:
            /** @brief 설정을 읽어 런타임을 세웁니다. 서버 설정이 없거나 온라인 설정 파일이 없으면 할 일 없이 성공입니다. */
            [[nodiscard]] expected<void> initialize( const ServerServiceContext& context ) override;
            void                         shutdown() override { _runtime.shutdown(); }
            void                         update( UnixMilliseconds nowMs ) override { _runtime.update( nowMs ); }
            int32                        getConnectionCount() const override { return _runtime.getConnectionCount(); }
            int32                        getOpenWorkCount() const override { return _runtime.getOpenWorkCount(); }
            void                         beginShutdown( const ServerShutdownNotice& notice ) override { _runtime.beginShutdown( notice ); }

        private:
            SystemMonotonicClock _clock; ///< 런타임이 빌린다(메모리 캐시 만료 · 외부 HTTP 시한). 런타임보다 먼저 선언한다
            OnlineServerRuntime  _runtime;
        };
    } // namespace
} // namespace sw

namespace sw
{
    expected<void> OnlineServerService::initialize( const ServerServiceContext& context )
    {
        const bool bNoServerConfig = context._pServerConfig == nullptr || context._serverConfigPath.empty();
        if ( bNoServerConfig )
            return {}; // 서버 설정이 없는 호스트(테스트)에서는 할 일이 없다
        const string path = OnlineServerConfig::makeConfigPath( context._serverConfigPath );
        // 설정 타입이 이 모듈에 있다 — 엔진의 ConfigManager 는 모듈보다 오래 살므로 테이블에 등록하지 않고 이 서비스가 직접 소유한다.
        OnlineServerConfig     onlineConfig;
        const ConfigReadResult result = ConfigManager::readConfigFileInto( path, onlineConfig );
        if ( result == ConfigReadResult::Missing )
        {
            SW_LOG_INFO_ID( "OnlineServerService.OnlineServicesOffDoes", "Online services are off - '%#' does not exist", path.c_str() );
            return {};
        }
        if ( result != ConfigReadResult::Loaded )
            return unexpected( Error{ ErrorKind::InvalidArgument, "the online service config is invalid", path } );
        const ServerConfig& serverConfig = *context._pServerConfig;
        if ( serverConfig._servicePort <= 0 )
            return unexpected( Error{ ErrorKind::InvalidArgument, "the online service config exists but the server config's _servicePort is 0", path } );

        OnlineServerRuntimeConfig settings;
        settings._config          = std::move( onlineConfig );
        settings._pMetricRegistry = context._pMetricRegistry;
        settings._pHealthRegistry = context._pHealthRegistry;
        settings._pClock          = &_clock;
        settings._gamePort        = static_cast<uint16>( serverConfig._gamePort );
        // 연이은 포트 샤드면 배정이 계정마다 한 포트를 고른다. 한 포트를 나누면(`_bGamePortReuse`) 커널이 나누므로 알릴 포트는 하나다.
        settings._gamePortCount = static_cast<uint8>( serverConfig._bGamePortReuse ? 1 : serverConfig._gamePortShardCount );
        if ( NetAddress::parse( serverConfig._listenAddress, static_cast<uint16>( serverConfig._servicePort ), settings._listenAddress ) == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "server config _listenAddress is not an IPv4 address", serverConfig._listenAddress } );
        settings._listenAddress._port = static_cast<uint16>( serverConfig._servicePort ); // 포트는 `_servicePort` 가 기준이다
        if ( serverConfig._adminServicePort > 0 )
        {
            const uint16 adminPort = static_cast<uint16>( serverConfig._adminServicePort );
            if ( NetAddress::parse( serverConfig._adminListenAddress, adminPort, settings._adminListenAddress ) == false )
                return unexpected( Error{ ErrorKind::InvalidArgument, "server config _adminListenAddress is not an IPv4 address", serverConfig._adminListenAddress } );
            settings._adminListenAddress._port = adminPort;
            settings._bAdminListener           = SW_TRUE;
        }
        if ( expected<void> stores = OnlineServerRuntime::resolveStores( serverConfig, settings._config, settings ); stores.has_value() == false )
            return stores;
        // 서비스 스트림은 TLS 1.3 이다. 인증서와 키 경로는 서버 설정에 있고, 키 암호는 환경 변수에서 읽는다.
        settings._bTLS               = SW_TRUE;
        settings._tlsCertificateFile = serverConfig._tlsCertificateFile;
        settings._tlsPrivateKeyFile  = serverConfig._tlsPrivateKeyFile;
        if ( ServerSecret::read( serverConfig._tlsPrivateKeySecretEnvironment, settings._tlsPrivateKeyPassphrase ) == false )
            return unexpected( Error{ ErrorKind::NotFound, "the TLS private key passphrase environment variable is not set", serverConfig._tlsPrivateKeySecretEnvironment } );
        return _runtime.initialize( std::move( settings ), WallClock::nowUnixMilliseconds() ); // 예외 — 호스트 최상위(서버 서비스의 뿌리)가 시각을 정한다
    }

    /** @brief 서버 서비스 중 앞쪽에 둔다. 게임과 키트가 올린 서버 서비스가 온라인 서비스를 볼 수 있게 한다. */
    SW_REGISTER_SERVER_SERVICE( OnlineServerService, "online_services", 100 );
} // namespace sw
