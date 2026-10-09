#include "pch.h"

#include "Server/ServerApp.h"

#include "ModuleHost/ModuleCatalogLoader.h"
#include "ModuleHost/ModuleHost.h"

#if !defined( SW_SHIPPING )
    #include "ModuleHost/LiveReloadManager.h"
#endif

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Common/BuildInfo.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/LogSink/ConsoleLogOutput.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"
#include "Core/Network/NetTypes.h"
#include "Core/Network/Transport/IStreamTransport.h"
#include "Core/Process/ShutdownSignal.h"
#include "Core/Container/StringUtil.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Config/ConfigManager.h"
#include "Engine/Config/Server/ServerConfig.h"
#include "Engine/Observability/MetricRegistry.h"
#include "Engine/Observability/OpsHttpEndpoint.h"
#include "Engine/Observability/ServiceHealthRegistry.h"
#include "Engine/Utility/Console/DevCommandRegistry.h"
#include "Engine/Utility/Profiling/FrameProfiler.h"
#if SW_DEV_COMMANDS_ENABLED
    #include "Engine/Utility/Console/DevConsole.h"
#endif

#include "sw/config/ConfigConstants.h"

namespace sw
{
    /** @brief `-gv_serverExitAfterTicks=N`: N 틱을 돈 뒤 스스로 정상 종료합니다(기동 · 종료 시험). Shipping 에도 등록합니다 — 배포 서버 산출물을 같은 시험이 띄운다. */
    SW_TEST_GLOBAL_VARIABLE_SHIPPED( int32, gv_serverExitAfterTicks, 0, "이 틱 수를 돈 뒤 정상 종료 (0=끄기)" );
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "Server" );

    namespace
    {
        struct ServerAppInternal
        {
            /** @brief 밀림 경고를 몇 번에 한 번만 남기나 — 과부하 서버가 로그 때문에 더 느려지지 않게. */
            static constexpr uint32 kDroppedTickWarningInterval = 100;
            /** @brief 클라이언트 산출물에 없어야 할 표식(`BuildTargetImageTest`) — 서버 실행 파일 자신도 서버 전용이다. */
            static constexpr utf8 kArrServerImageMarker[] = "sw-server-only-module:Server";
            /** @brief 운영 HTTP 는 스크레이퍼 · 감시자 몇이 붙는다 — 연결 상한과 유휴 시한을 작게. */
            static constexpr int32   kOpsMaxConnections     = 64;
            static constexpr float64 kOpsIdleTimeoutSeconds = 10.0;

            static int64 nowMonotonicMs() { return MonotonicClock::nowNanoseconds() / 1000000; }
        };

        /** @brief 표식 글을 링커가 버리지 못하게 정적 초기화에서 한 번 읽는다(서버 전용 모듈의 생성 표식과 같은 방식). */
        struct ServerImageMarkerInternal
        {
            ServerImageMarkerInternal()
            {
                const volatile utf8* pMarker = ServerAppInternal::kArrServerImageMarker;
                (void)pMarker[0];
            }
        };
        const ServerImageMarkerInternal s_serverImageMarker{};
    } // namespace

    ServerApp::ServerApp()
        : _engineLoop{}
        , _moduleHost{ nullptr }
#if !defined( SW_SHIPPING )
        , _liveReloadManager{ nullptr }
        , _devConsole{ nullptr }
#endif
        , _moduleCatalog{}
        , _moduleResolution{}
        , _console{}
        , _metricRegistry{ nullptr }
        , _healthRegistry{ nullptr }
        , _opsTransport{ nullptr }
        , _opsEndpoint{ nullptr }
        , _listPendingCommand{}
        , _pServerConfig{ nullptr }
        , _pTickHistogram{ nullptr }
        , _pDroppedTickCounter{ nullptr }
        , _tickCount{ 0 }
        , _startNanoseconds{ 0 }
        , _tickNanoseconds{ 0 }
        , _statusTickSumNanoseconds{ 0 }
        , _statusTickMaxNanoseconds{ 0 }
        , _statusTickSampleCount{ 0 }
        , _droppedTickCount{ 0 }
        , _bReady{ SW_FALSE }
        , _bSignalHandlerInstalled{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    ServerApp::~ServerApp() = default;

    int32 ServerApp::runMain( int32 argc, utf8* pArgv[] )
    {
        // 줄마다 내보낸다 — 시험 · journald · docker logs 가 "ready" 줄을 바로 본다. 엔진(로거)을 세우기 전에 켠다.
        ConsoleLogOutput::setFlushEveryLine( true );
        ServerApp  server{};
        const bool bStarted = server.initialize( argc, pArgv );
        if ( bStarted )
            server.run();
        server.shutdown();
        return bStarted ? 0 : 1;
    }

    bool ServerApp::initialize( int32 argc, utf8* pArgv[] )
    {
        _startNanoseconds = MonotonicClock::nowNanoseconds();
        _engineLoop.setModuleTypeLoader( SW_DELEGATE_METHOD( ModuleTypeLoaderDelegate, &ServerApp::loadModuleImages, this ) );
        if ( _engineLoop.initialize( argc, pArgv, EngineHostRole::DedicatedServer ) == false )
        {
            SW_LOG_ERROR( "EngineLoop initialization failed (dedicated server)" );
            return false;
        }
        // 헤드리스 작업(서버 팩의 씬 쿠킹)은 엔진 기동 안에서 끝났다. 실패는 종료 코드로 — CookAssets.py 가 본다.
        if ( _engineLoop.isHeadless() )
            return _engineLoop.didHeadlessTaskFail() == false;

        if ( loadServerConfig() == false )
            return false;
        _tickNanoseconds = constant::kNanosecondsPerSecond / static_cast<int64>( _pServerConfig->_tickRateHz );
        if ( initializeObservability() == false )
            return false;

        if ( _moduleHost == nullptr )
            _moduleHost = make_unique<ModuleHost>();
        if ( _moduleHost->initializeDedicatedServer( getLiveReloadManager() ) == false )
        {
            SW_LOG_ERROR( "The game module could not be started on the dedicated server" );
            return false;
        }

        _bSignalHandlerInstalled = ShutdownSignal::install() ? SW_TRUE : SW_FALSE;
        if ( _bSignalHandlerInstalled == SW_FALSE )
            SW_LOG_WARNING( "Shutdown signal handlers are not installed - stop the server with the 'quit' console command" );
        if ( _pServerConfig->_bConsoleInput )
            _console.start();
#if SW_DEV_COMMANDS_ENABLED
        _devConsole = make_unique<DevConsole>();
#endif

        _bReady = SW_TRUE;
        SW_LOG_INFO( "Dedicated server ready - target %#, %# Hz, game port %#, service port %#, startup %# ms", build::kTargetName, _pServerConfig->_tickRateHz,
                     _pServerConfig->_gamePort, _pServerConfig->_servicePort, ( MonotonicClock::nowNanoseconds() - _startNanoseconds ) / 1000000 );
        return true;
    }

    bool ServerApp::loadModuleImages()
    {
        SW_MEMORY_SCOPE( EngineMisc );
#if !defined( SW_SHIPPING )
        // 전용 서버는 Server 대상 모듈만 올린다 — Game 빌드에서도 에디터 · RHI 모듈을 올리지 않는다.
        if ( ModuleCatalogLoader::loadAndResolve( static_cast<uint8>( ModuleTarget::Server ), _moduleCatalog, _moduleResolution ) == false )
            return false;
        _liveReloadManager = make_unique<LiveReloadManager>();
        // 모듈 DLL 을 내릴 수 있는 곳은 "씬은 사라졌고 서비스는 아직 있는" 구간뿐이다(App 과 같은 훅).
        _engineLoop.setOnScenesReleased( SW_DELEGATE_LAMBDA( Delegate<void()>, [this]()
        {
            if ( _liveReloadManager != nullptr )
                _liveReloadManager->shutdown();
        } ) );
#endif
        _moduleHost = make_unique<ModuleHost>();
        return _moduleHost->loadModuleImages( getLiveReloadManager(), _moduleCatalog, _moduleResolution );
    }

    LiveReloadManager* ServerApp::getLiveReloadManager() const
    {
#if defined( SW_SHIPPING )
        return nullptr;
#else
        return _liveReloadManager.get();
#endif
    }

    bool ServerApp::loadServerConfig()
    {
        ConfigManager*            pConfigManager = _engineLoop.getConfigManager();
        const CommandLineManager* pCommandLine   = _engineLoop.getCommandLineManager();
        string                    path           = config::kFileRuntimeServerConfig;
        if ( pCommandLine != nullptr )
        {
            string overridePath;
            if ( pCommandLine->getArgument( CommandLineArgument::SERVER_CONFIG, overridePath ) && overridePath.empty() == false )
                path = overridePath;
        }
        if ( pConfigManager == nullptr )
            return false;
        // Shipping 도 디스크에서 읽는다 — 운영자가 고치는 파일이다(굽지 않는다). 없으면 Shipping 은 서지 않는다: 기본 포트로 조용히 뜬 서버는
        // 틀린 DB · 캐시에 붙는 서버다.
        if ( pConfigManager->loadConfig<ServerConfig>( path ) )
        {
            _pServerConfig = pConfigManager->getConfig<ServerConfig>();
            SW_LOG_INFO( "Server config: %#", path.c_str() );
            return _pServerConfig != nullptr;
        }
#if defined( SW_SHIPPING )
        SW_LOG_ERROR( "Server config '%#' could not be read - pass -server-config=<path>", path.c_str() );
        return false;
#else
        SW_LOG_WARNING( "Server config '%#' could not be read - using built-in defaults (Dev only)", path.c_str() );
        _pServerConfig = pConfigManager->ensureConfig<ServerConfig>( path );
        return _pServerConfig != nullptr;
#endif
    }

    bool ServerApp::initializeObservability()
    {
        _metricRegistry      = make_unique<MetricRegistry>();
        _healthRegistry      = make_unique<ServiceHealthRegistry>();
        _pTickHistogram      = _metricRegistry->registerHistogram( "server_tick_seconds", "Dedicated server tick body duration", MetricRegistry::makeLatencyBounds() );
        _pDroppedTickCounter = _metricRegistry->registerCounter( "server_dropped_ticks_total", "Ticks dropped because the server fell too far behind" );
        if ( _pServerConfig->_opsPort == 0 )
            return true;
        OpsHttpEndpointSettings opsSettings;
        if ( NetAddress::parse( _pServerConfig->_opsListenAddress, static_cast<uint16>( _pServerConfig->_opsPort ), opsSettings._bindAddress ) == false )
        {
            SW_LOG_ERROR( "Server config _opsListenAddress '%#' is not an IPv4 address", _pServerConfig->_opsListenAddress.c_str() );
            return false;
        }
        opsSettings._bindAddress._port = static_cast<uint16>( _pServerConfig->_opsPort ); // 포트는 _opsPort 가 정본(주소 칸에 포트를 적어도 무시)
        _opsTransport                  = StreamTransportFactory::createPlatformTransport();
        if ( _opsTransport == nullptr )
        {
            SW_LOG_ERROR( "Ops HTTP endpoint needs a stream transport and this platform has none" );
            return false;
        }
        StreamTransportSettings transportSettings;
        transportSettings._ioThreadCount      = 1;
        transportSettings._maxConnections     = ServerAppInternal::kOpsMaxConnections;
        transportSettings._idleTimeoutSeconds = ServerAppInternal::kOpsIdleTimeoutSeconds;
        _opsEndpoint                          = make_unique<OpsHttpEndpoint>();
        if ( _opsEndpoint->initialize( _opsTransport.get(), transportSettings, opsSettings, _metricRegistry.get(), _healthRegistry.get() ) == false )
        {
            _opsEndpoint.reset();
            _opsTransport.reset();
            return false; // 운영자가 켠 끝점을 못 열면 서지 않는다 — 감시자가 볼 수 없는 서버다
        }
        return true;
    }

    void ServerApp::shutdownObservability()
    {
        if ( _opsEndpoint != nullptr )
            _opsEndpoint->shutdown();
        _opsEndpoint.reset();
        _opsTransport.reset();
        _pTickHistogram      = nullptr;
        _pDroppedTickCounter = nullptr;
        _healthRegistry.reset();
        _metricRegistry.reset();
    }

    void ServerApp::run()
    {
        if ( _bReady == SW_FALSE )
            return;
        const float32 deltaSeconds = 1.0f / static_cast<float32>( _pServerConfig->_tickRateHz );
        const int64   maxBehind    = _tickNanoseconds * static_cast<int64>( _pServerConfig->_maxCatchUpTicks );
        int64         deadline     = MonotonicClock::nowNanoseconds();
        while ( true )
        {
            _console.drainLines( _listPendingCommand );
            for ( const string& line : _listPendingCommand )
            {
                executeCommand( line );
            }
            _listPendingCommand.clear();

            const ShutdownCause cause = ShutdownSignal::getRequestedCause();
            if ( cause != ShutdownCause::None || _engineLoop.isQuitRequested() )
            {
                SW_LOG_INFO( "Dedicated server shutdown requested (%#) after %# ticks", ShutdownSignal::getCauseName( cause ), _tickCount );
                _healthRegistry->setDraining( true ); // /readyz 503 — 부하 분산기가 새 접속을 끊는다(내리는 동안 /healthz 는 살아 있음)
                break;
            }

            const int64 tickStart = MonotonicClock::nowNanoseconds();
            tickOnce( deltaSeconds );
            const int64 tickTime = MonotonicClock::nowNanoseconds() - tickStart;
            _pTickHistogram->observe( static_cast<float64>( tickTime ) * 1.0e-9 );
            _healthRegistry->markTick( ServerAppInternal::nowMonotonicMs() );
            _statusTickSumNanoseconds += tickTime;
            _statusTickMaxNanoseconds = tickTime > _statusTickMaxNanoseconds ? tickTime : _statusTickMaxNanoseconds;
            ++_statusTickSampleCount;
            ++_tickCount;
            if ( gv_serverExitAfterTicks > 0 && _tickCount >= static_cast<uint64>( gv_serverExitAfterTicks ) )
                ShutdownSignal::request( ShutdownCause::TickLimit );

            deadline += _tickNanoseconds;
            const int64 now = MonotonicClock::nowNanoseconds();
            if ( now - deadline > maxBehind )
            {
                // 따라잡을 수 없을 만큼 밀렸다 — 밀린 시간을 버리고 지금부터 다시 센다(몰아 돌기가 다음 틱을 또 밀지 않게).
                const uint32 dropped = static_cast<uint32>( ( now - deadline ) / _tickNanoseconds );
                if ( _droppedTickCount == 0 ||
                     _droppedTickCount / ServerAppInternal::kDroppedTickWarningInterval != ( _droppedTickCount + dropped ) / ServerAppInternal::kDroppedTickWarningInterval )
                    SW_LOG_WARNING( "Server is behind: dropped %# ticks (total %#), last tick %# us", dropped, _droppedTickCount + dropped, tickTime / 1000 );
                _droppedTickCount += dropped;
                _pDroppedTickCounter->add( dropped );
                deadline = now;
            }
            else if ( deadline > now )
            {
                MonotonicClock::sleepUntilNanoseconds( deadline );
            }
        }
    }

    void ServerApp::tickOnce( float32 deltaSeconds )
    {
        // App::run 의 한 프레임에서 창 · 에디터 · 사용자 설정 · 백엔드 교체를 뺀 것이다. 한 틱 = 고정 스텝 하나.
        _engineLoop.beginFrame( deltaSeconds );
        _moduleHost->beginFrame();
        {
            SW_PROFILE_SCOPE( "GT.Game.fixedUpdate" );
            _moduleHost->fixedUpdateGame( deltaSeconds );
        }
        {
            SW_PROFILE_SCOPE( "GT.Game.update" );
            _moduleHost->updateGame( deltaSeconds );
        }
#if !defined( SW_SHIPPING )
        if ( _liveReloadManager != nullptr )
            _liveReloadManager->update();
#endif
        const ModuleFrameState& frameState = _moduleHost->getFrameState();
        _engineLoop.tick( deltaSeconds, HostViewTargets{}, ViewCameraProviderDelegate{}, frameState._bTickScene == SW_TRUE );
        _engineLoop.endFrame();
    }

    void ServerApp::executeCommand( string_view line )
    {
        const string_view command = StringUtil::trim( line );
        if ( command.empty() )
            return;
        if ( command == "quit" || command == "stop" )
        {
            ShutdownSignal::request( ShutdownCause::ConsoleCommand );
            return;
        }
        if ( command == "status" )
        {
            logStatus();
            return;
        }
        if ( command == "help" )
        {
            SW_LOG_INFO( "Server commands: quit | stop | status | help%#", SW_DEV_COMMANDS_ENABLED ? " (Dev: dev commands and gv_* variables too)" : "" );
            return;
        }
#if SW_DEV_COMMANDS_ENABLED
        // Dev 는 개발 콘솔과 같은 명령(개발 명령 · gv_* 전역 변수)을 받는다 — 답 · 실패는 DevConsole 이 로그에 남긴다. Shipping 서버는 위 넷뿐이다.
        if ( _devConsole != nullptr )
        {
            (void)_devConsole->submit( command );
            return;
        }
#endif
        SW_LOG_WARNING( "Unknown server command '%#' - type help", string( command ).c_str() );
    }

    void ServerApp::logStatus() const
    {
        const int64 uptimeSeconds = ( MonotonicClock::nowNanoseconds() - _startNanoseconds ) / constant::kNanosecondsPerSecond;
        const int64 averageMicro  = _statusTickSampleCount > 0 ? _statusTickSumNanoseconds / _statusTickSampleCount / 1000 : 0;
        SW_LOG_INFO( "Dedicated server status - uptime %# s, tick %#, tick time avg %# us / max %# us (budget %# us), dropped ticks %#", uptimeSeconds, _tickCount,
                     averageMicro, _statusTickMaxNanoseconds / 1000, _tickNanoseconds / 1000, _droppedTickCount );
    }

    void ServerApp::shutdown()
    {
        _console.stop();
#if SW_DEV_COMMANDS_ENABLED
        _devConsole.reset();
#endif
        // 게임 · 모듈 인스턴스를 엔진보다 먼저 내린다(App::shutdown 과 같은 순서).
        if ( _moduleHost != nullptr )
        {
            _moduleHost->shutdown();
            _moduleHost.reset();
        }
        // 운영 끝점은 게임을 내리는 동안에도 비우는 중(/readyz 503)을 알리다 마지막에 닫는다 — 로그를 남기므로 엔진(로거)보다 먼저.
        shutdownObservability();
        // 로거는 엔진 종료와 함께 내려간다 — 마지막 줄은 그 앞에 남긴다(시험 · 운영 도구가 이 줄로 정상 종료를 본다).
        SW_LOG_INFO( "Dedicated server shutdown complete" );
        _engineLoop.shutdown();
        if ( _bSignalHandlerInstalled == SW_TRUE )
            ShutdownSignal::uninstall();
        // Windows 창 닫기 · 시스템 종료 처리기가 이것을 기다린다.
        ShutdownSignal::notifyShutdownComplete();
    }
} // namespace sw
