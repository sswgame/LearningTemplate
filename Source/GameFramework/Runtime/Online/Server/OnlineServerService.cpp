#include "pch.h"

#include "Core/Delegate/Subscription.h"
#include "Core/Subsystem/Subsystem.h"
#include "Core/Time/Clock.h"
#include "Core/Time/WallClock.h"

#include "Engine/Config/ConfigManager.h"
#include "Engine/Config/Server/ServerSecret.h"
#include "Engine/Config/ServerConfig.h"
#include "Engine/Foundation/EngineSubsystemHost.h"
#include "Engine/Module/ModuleSubsystemRegistrar.h"
#include "Engine/Network/GameNetSession.h"
#include "Engine/Reflection/TypeInfo.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Server/ServerService.h"

#include "GameFramework/Runtime/Foundation/Subsystem/GameSubsystems.h"
#include "GameFramework/Runtime/Online/Server/OnlineServerConfig.h"
#include "GameFramework/Runtime/Online/Server/OnlineServerKit.h"
#include "GameFramework/Runtime/Online/Server/OnlineServerRuntime.h"

// 전용 서버의 서버 서비스로 온라인 서비스 조립(`OnlineServerRuntime`)을 띄운다. 서버 설정 옆 `<이름>.online.json` 이 있을 때만 띄운다.
// GameFramework 의 모듈 서브시스템(호스트 `kServer`)이 서비스를 들고 `IServerService` 로 공개한다. 클라이언트 호스트는 서브시스템을 초기화하지 않고, 찾는 쪽도 없다.
// 같은 런타임이 이미 모든 구성에서 지어진다. 클라이언트 배포본에서 빼려면 서버 전용 폴더 · 모듈(`GF_Server_<X>`)로 옮긴다.

namespace sw
{
    namespace
    {
        /**
         * @class OnlineServerService
         * @brief 온라인 서비스 조립을 서버 서비스로 감쌉니다. 서버 설정과 온라인 설정을 런타임 설정으로 옮기고, 비밀은 환경 변수에서 읽어 넘긴 뒤 비웁니다.
         * @details 키트는 다른 모듈(`GF_Server_<X>` · 게임)에 있고 GameFramework 는 리로드하지 않는다. 그래서 어느 모듈이든 내려가기 직전에(`TypeRegistry::subscribeModuleUnloading`,
         *          그 모듈의 코드가 아직 있을 때) 런타임을 내리고, 리로드가 끝난 다음 `update` 에서 같은 문맥으로 다시 세운다. 다시 세우다 지면 다음 리로드 뒤에 또 해 본다.
         *          게임 모듈 리로드에서는 게임이 배치 앞에서 먼저 내려간다(`GameModuleManager::onBeforeCommitBatch`). 게임의 마지막 저장은 아직 선 저장소에 닿고, 런타임 종료가 남은 일을 끝낸다.
         */
        class OnlineServerService final : public IServerService
        {
        public:
            OnlineServerService();

            /** @brief 설정을 읽어 런타임을 세웁니다. 서버 설정이 없거나 온라인 설정 파일이 없으면 할 일 없이 성공입니다. */
            [[nodiscard]] expected<void> startService( const ServerServiceContext& context ) override;
            void                         stopService() override;
            bool                         isServiceRunning() const override { return _bRunning == SW_TRUE; }
            void                         update( UnixMilliseconds nowMs ) override;
            int32                        getConnectionCount() const override { return _runtime.getConnectionCount(); }
            int32                        getOpenJobCount() const override { return _runtime.getOpenJobCount(); }
            void                         beginShutdown( const ServerShutdownNotice& notification ) override { _runtime.beginShutdown( notification ); }

        private:
            /** @brief 든 문맥으로 런타임을 세웁니다. 지면 세운 것까지 내린다. */
            [[nodiscard]] expected<void> launchRuntime();
            [[nodiscard]] expected<void> startRuntime( const ServerServiceContext& context );
            /** @brief 키트를 게임 모듈의 창구에서 거두고 런타임을 내립니다. 문맥과 구독은 남긴다(리로드 뒤 다시 세운다). */
            void stopRuntime();
            /** @brief 모듈이 내려가기 직전입니다. 런타임을 내리고 다음 `update` 에서 다시 세우게 표시합니다. */
            void onModuleUnloading( string_view moduleName );

        private:
            SystemMonotonicClock   _clock; ///< 런타임이 빌린다(메모리 캐시 만료 · 외부 HTTP 시한). 런타임보다 먼저 선언한다
            OnlineServerRuntime    _runtime;
            ServerServiceContext   _context;                 ///< 서버가 시작할 때 넘긴 문맥(리로드 뒤 다시 세울 때 쓴다)
            Subscription           _moduleUnloading;         ///< 서버가 시작한 뒤부터 멈출 때까지 건다
            ServerKitDirectory*    _pKitDirectory;           ///< 공개한 호스트 창구(호스트 소유). 공개하지 않았으면 nullptr
            uint8                  _bStarted            : 1; ///< 서버가 시작했고 아직 멈추지 않았다(`startService` ~ `stopService`)
            uint8                  _bRunning            : 1; ///< 런타임이 서 있다(설정상 할 일이 없어도 선 것으로 친다)
            uint8                  _bRestartAfterReload : 1; ///< 모듈 리로드로 내렸다. 다음 `update` 에서 다시 세운다
            [[maybe_unused]] uint8 _reserved            : 5;
        };

        /**
         * @class OnlineServerSubsystem
         * @brief 온라인 서버 서비스의 수명을 맡는 모듈 서브시스템입니다(전용 서버만 초기화한다). 서비스는 서버가 시작하고, 이 모듈이 내려가면 범위가 걷히며 멈춘다.
         */
        class OnlineServerSubsystem final : public Subsystem
        {
        public:
            static constexpr const utf8* kSubsystemName = "OnlineServer";

            SubsystemInitResult initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
            /** @brief 서버가 멈추기 전에 이 모듈이 내려가는 길에서도 빌린 문맥을 놓는다. 이미 멈췄으면 아무것도 하지 않는다. */
            void              stopSubsystem() override { _service.stopService(); }
            void              shutdownSubsystem() override {}
            const utf8*       getSubsystemName() const override { return kSubsystemName; }
            SubsystemHostMask getSubsystemHost() const override { return EngineSubsystemHost::kServer; }

            /** @brief 공개할 서비스입니다(`ModuleSubsystemRegistration::publishOne`). */
            OnlineServerService& getPublishedPart() { return _service; }

        private:
            OnlineServerService _service;
        };
    } // namespace
} // namespace sw

namespace sw
{
    OnlineServerService::OnlineServerService()
        : _clock{}
        , _runtime{}
        , _context{}
        , _moduleUnloading{}
        , _pKitDirectory{ nullptr }
        , _bStarted{ SW_FALSE }
        , _bRunning{ SW_FALSE }
        , _bRestartAfterReload{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    expected<void> OnlineServerService::startService( const ServerServiceContext& context )
    {
        if ( _bStarted == SW_TRUE )
            return {};
        _context = context;
        if ( expected<void> launched = launchRuntime(); launched.has_value() == false )
        {
            stopService();
            return launched;
        }
        _bStarted        = SW_TRUE;
        _moduleUnloading = game::getSubsystem<TypeRegistry>().subscribeModuleUnloading( SW_DELEGATE_METHOD( TypeRegistry::ModuleUnloadingDelegate, &OnlineServerService::onModuleUnloading, this ) );
        return {};
    }

    void OnlineServerService::stopService()
    {
        stopRuntime();
        _moduleUnloading     = Subscription{};
        _bStarted            = SW_FALSE;
        _bRestartAfterReload = SW_FALSE;
        _context             = ServerServiceContext{};
        _context._serverConfigPath.shrink_to_fit(); // 이동 대입은 libstdc++ 에서 버퍼를 남긴다(짧은 글). 엔진 종료의 누수 보고 전에 놓는다
    }

    expected<void> OnlineServerService::launchRuntime()
    {
        _bRunning              = SW_TRUE; // 지면 `stopRuntime` 이 세운 것까지 정리한다
        expected<void> started = startRuntime( _context );
        if ( started.has_value() == false )
            stopRuntime();
        return started;
    }

    void OnlineServerService::stopRuntime()
    {
        if ( _bRunning == SW_FALSE )
            return;
        _bRunning = SW_FALSE;
        if ( _pKitDirectory != nullptr )
            _pKitDirectory->withdraw( &_runtime.getKitScope() ); // 키트가 내려가기 전에 게임 모듈의 창구에서 거둔다
        _pKitDirectory = nullptr;
        _runtime.shutdown();
    }

    void OnlineServerService::onModuleUnloading( string_view moduleName )
    {
        if ( _bStarted == SW_FALSE )
            return;
        if ( _bRunning == SW_TRUE )
            SW_LOG_INFO_ID( "OnlineServerService.StoppingForModuleUnload", "Stopping online services before module '%#' unloads - they restart after the reload", moduleName );
        stopRuntime();
        _bRestartAfterReload = SW_TRUE;
    }

    void OnlineServerService::update( UnixMilliseconds nowMs )
    {
        if ( _bRestartAfterReload == SW_TRUE )
        {
            // 리로드가 끝났다(취소됐어도 런타임은 내려가 있다). 새 모듈의 키트 팩토리로 다시 세운다.
            _bRestartAfterReload = SW_FALSE;
            if ( const expected<void> launched = launchRuntime(); launched.has_value() == false )
                SW_LOG_ERROR_ID( "OnlineServerService.RestartFailed", "Online services could not be restarted after the module reload - fix the error and reload again: %# (%#)", launched.error().message(),
                                 launched.error().detail() );
        }
        if ( _bRunning == SW_FALSE )
            return;
        const GameNetSession* const pNetSession = game::findSubsystem<GameNetSession>();
        if ( pNetSession != nullptr && pNetSession->getRole() == GameNetRole::Server )
            _runtime.setGameConnectionCount( pNetSession->getConnectedCount() ); // 디렉터리의 부하는 게임 포트의 손님 수다
        _runtime.update( nowMs );
    }

    expected<void> OnlineServerService::startRuntime( const ServerServiceContext& context )
    {
        const bool bNoServerConfig = context._pServerConfig == nullptr || context._serverConfigPath.empty();
        if ( bNoServerConfig )
            return {}; // 서버 설정이 없는 호스트(테스트)에서는 할 일이 없다
        const string path = OnlineServerConfig::makeConfigPath( context._serverConfigPath );
        // 설정 타입이 이 모듈에 있다 — 엔진의 ConfigManager 는 모듈보다 오래 살므로 테이블에 등록하지 않고 이 서비스가 직접 소유한다.
        OnlineServerConfig     onlineConfig;
        const ConfigReadResult result       = ConfigManager::readConfigFileInto( path, onlineConfig );
        const ServerConfig&    serverConfig = *context._pServerConfig;
        // 켜고 끄는 것은 서버 설정의 칸이다. 파일은 그 칸과 맞아야 한다(맞지 않으면 어느 쪽이 맞는지 모른다).
        if ( serverConfig._bOnlineServices == false )
        {
            if ( result != ConfigReadResult::Missing )
                return unexpected( Error{ ErrorKind::InvalidArgument, "an online service config exists but the server config's _bOnlineServices is off", path } );
            SW_LOG_INFO_ID( "OnlineServerService.OnlineServicesOff", "Online services are off (server config _bOnlineServices)" );
            return {};
        }
        if ( result == ConfigReadResult::Missing )
            return unexpected( Error{ ErrorKind::NotFound, "the server config's _bOnlineServices is on but the online service config does not exist", path } );
        if ( result != ConfigReadResult::Loaded )
            return unexpected( Error{ ErrorKind::InvalidArgument, "the online service config is invalid", path } );
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
            return unexpected( Error{ ErrorKind::InvalidArgument, "server config _listenAddress is not an IP address", serverConfig._listenAddress } );
        settings._listenAddress._port = static_cast<uint16>( serverConfig._servicePort ); // 포트는 `_servicePort` 가 기준이다
        if ( serverConfig._adminServicePort > 0 )
        {
            const uint16 adminPort = static_cast<uint16>( serverConfig._adminServicePort );
            if ( NetAddress::parse( serverConfig._adminListenAddress, adminPort, settings._adminListenAddress ) == false )
                return unexpected( Error{ ErrorKind::InvalidArgument, "server config _adminListenAddress is not an IP address", serverConfig._adminListenAddress } );
            settings._adminListenAddress._port = adminPort;
            // GM 포트는 계정 로그인 뒤 운영 명령을 받는 자리다 — 공용 인터페이스에 열지 않는다(형식으로 막는다, 운영 HTTP 와 같은 규칙. 바깥 GM 은 VPN · 배스천으로 사설망에 들어온다).
            if ( settings._adminListenAddress.isLoopbackOrPrivate() == false )
                return unexpected( Error{ ErrorKind::InvalidArgument, "server config _adminListenAddress must be loopback or a private address (the GM port is never public)", serverConfig._adminListenAddress } );
            settings._bAdminListener = SW_TRUE;
        }
        if ( expected<void> stores = OnlineServerRuntime::resolveStores( serverConfig, settings._config, settings ); stores.has_value() == false )
            return stores;
        // 서비스 스트림은 TLS 1.3 이다. 인증서와 키 경로는 서버 설정에 있고, 키 암호는 환경 변수에서 읽는다.
        settings._bTLS               = SW_TRUE;
        settings._tlsCertificateFile = serverConfig._tlsCertificateFile;
        settings._tlsPrivateKeyFile  = serverConfig._tlsPrivateKeyFile;
        if ( ServerSecret::read( serverConfig._tlsPrivateKeySecretEnvironment, settings._tlsPrivateKeyPassphrase ) == false )
            return unexpected( Error{ ErrorKind::NotFound, "the TLS private key passphrase environment variable is not set", serverConfig._tlsPrivateKeySecretEnvironment } );
        if ( expected<void> started = _runtime.initialize( std::move( settings ), WallClock::nowUnixMilliseconds() ); started.has_value() == false ) // 예외 — 호스트 최상위(서버 서비스의 뿌리)가 시각을 정한다
            return started;
        _pKitDirectory = context._pKitDirectory;
        if ( _pKitDirectory != nullptr )
            _pKitDirectory->publish( &_runtime.getKitScope() ); // 게임 서버 코드가 키트 인터페이스(백필 …)를 찾는다
        return {};
    }

    /** @brief 전용 서버만 초기화하는 GameFramework 의 모듈 서브시스템입니다. 서버가 `IServerService` 로 찾아 시작 · 갱신 · 드레인 · 멈춤을 부른다. */
    SW_MODULE_SUBSYSTEM( Engine, Process, OnlineServerSubsystem, IServerService );
} // namespace sw
