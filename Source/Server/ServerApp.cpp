#include "pch.h"

#include "Server/ServerApp.h"

#include "ModuleHost/ModuleCatalogLoader.h"
#include "ModuleHost/ModuleHost.h"
#include "ModuleHost/ModuleImageHostSubsystem.h"

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
#include "Core/Diagnostics/CrashContext.h"
#include "Core/Diagnostics/CrashHandler.h"
#include "Core/File/FileUtil.h"
#include "Core/Time/MonotonicClock.h"
#include "Core/Time/WallClock.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Config/ConfigManager.h"
#include "Engine/Config/GameSelection.h"
#include "Engine/Config/Server/ServerConfig.h"
#include "Engine/Module/ModuleImageHost.h"
#include "Engine/Observability/MetricRegistry.h"
#include "Engine/Observability/DebugChannel.h"
#include "Engine/Scene/SceneStateHash.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Observability/OpsHTTPEndpoint.h"
#include "Engine/Observability/ProcessMetrics.h"
#include "Engine/Observability/ServerServiceManager.h"
#include "Engine/Observability/ServiceHealthRegistry.h"
#include "Core/Console/DevCommandRegistry.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Profiling/ProfilerTraceExport.h"
#include "Engine/Telemetry/CrashReportService.h"
#if SW_DEV_COMMANDS_ENABLED
    #include "Engine/Console/DevConsole.h"
#endif

#include "sw/config/ConfigConstants.h"

namespace sw
{
    /** @brief `-gv_serverExitAfterTicks=N`: N 틱을 돈 뒤 스스로 정상 종료합니다(기동과 종료 테스트용). Shipping 에도 등록하며, 배포 서버 산출물을 같은 테스트가 실행합니다. */
    SW_TEST_GLOBAL_VARIABLE_SHIPPED( int32, gv_serverExitAfterTicks, 0, "이 틱 수를 돈 뒤 정상 종료 (0=끄기)" );
    /** @brief `-gv_debugChannelPort=N`: 디버그 채널(개발 명령 원격 실행, 루프백 TCP)을 이 포트에 엽니다(0=끄기). 토큰(`gv_debugChannelToken`)이 있어야 열립니다. Shipping 에는 없습니다. */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_debugChannelPort, 0, "디버그 채널 포트 (0=끄기, 루프백 전용, 토큰 필요)" );
    SW_TEST_GLOBAL_VARIABLE( string, gv_debugChannelToken, "", "디버그 채널 접속 토큰" );
    /** @brief `-gv_stateHashHistory=N`: 서버가 틱마다 씬 상태 해시를 남기고 최근 N 틱을 보관합니다(0=끄기). 개발 명령 `state.hashes` 로 읽습니다. */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_stateHashHistory, 0, "틱별 씬 상태 해시 이력 길이 (0=끄기)" );
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "Server" );

    namespace
    {
        struct ServerAppInternal
        {
            /** @brief 밀림 경고를 몇 번에 한 번만 남길지 정합니다. 과부하 서버가 로그 때문에 더 느려지지 않게 하기 위해서입니다. */
            static constexpr uint32 kDroppedTickWarningInterval = 100;
            /** @brief 클라이언트 산출물에 없어야 할 표식(`BuildTargetImageTest`)입니다. 서버 실행 파일 자신도 서버 전용입니다. */
            static constexpr utf8 kArrServerImageMarker[] = "sw-server-only-module:Server";
            /** @brief 운영 HTTP 에는 스크레이퍼와 감시자 몇 개만 붙으므로 연결 상한과 유휴 시한을 작게 잡습니다. */
            static constexpr int32   kOpsMaxConnections     = 64;
            static constexpr float64 kOpsIdleTimeoutSeconds = 10.0;
            /** @brief 프로세스 지표를 다시 읽는 간격입니다. 운영체제에 묻는 비용이 있고 스크레이퍼도 보통 15 초마다 읽으므로 초에 한 번이면 된다. */
            static constexpr int64 kProcessSampleIntervalMs = 1000;
            /** @brief Dev 서버 루프의 명령(`executePauseCommand`)입니다. 표준 입력 `help` 와 디버그 채널 `help` 가 같은 줄을 보입니다. */
            static constexpr const utf8* kLoopCommandHelp = "pause | resume | step [N]";

            static int64 nowMonotonicMs() { return MonotonicClock::nowNanoseconds() / constant::kNanosecondsPerMillisecond; }
        };

        /** @brief 링커가 표식 문자열을 버리지 못하게 정적 초기화에서 한 번 읽습니다(서버 전용 모듈의 생성 표식과 같은 방식입니다). */
        struct ServerImageMarker
        {
            ServerImageMarker()
            {
                const volatile utf8* pMarker = ServerAppInternal::kArrServerImageMarker;
                (void)pMarker[0];
            }
        };
        const ServerImageMarker s_serverImageMarker{};
    } // namespace

    ServerApp::ServerApp()
        : _host{}
        , _pModuleImageHost{ nullptr }
        , _engine{}
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
        , _processMetrics{ nullptr }
        , _serverServices{ nullptr }
        , _serverConfigPath{}
        , _serverServiceContext{}
        , _listPendingCommand{}
        , _pServerConfig{ nullptr }
        , _pTickHistogram{ nullptr }
        , _pDroppedTickCounter{ nullptr }
        , _pServiceConnectionGauge{ nullptr }
        , _tickCount{ 0 }
        , _startNanoseconds{ 0 }
        , _tickNanoseconds{ 0 }
        , _statusTickSumNanoseconds{ 0 }
        , _statusTickMaxNanoseconds{ 0 }
        , _statusTickSampleCount{ 0 }
        , _droppedTickCount{ 0 }
        , _pendingStepCount{ 0 }
        , _nextProcessSampleMs{ 0 }
        , _bReady{ SW_FALSE }
        , _bSignalHandlerInstalled{ SW_FALSE }
        , _bPaused{ SW_FALSE }
        , _bRestartServerServices{ SW_FALSE }
        , _bCrashReportsStarted{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    ServerApp::~ServerApp() = default;

    int32 ServerApp::runMain( int32 argc, utf8* pArgv[] )
    {
        // 줄마다 내보낸다. 테스트, journald, docker logs 가 "ready" 줄을 바로 볼 수 있다. 엔진(로거)을 시작하기 전에 켠다.
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
        // 호스트 범위를 먼저 세운다 — 엔진 범위의 부모다. 엔진의 `ModuleTypes` 단계가 여기서 모듈 이미지 창구를 찾아 서버 대상 모듈을 올린다(`loadModuleImages`).
        {
            SW_MEMORY_SCOPE( EngineMisc );
            _pModuleImageHost   = &_host.addSubsystem( make_unique<ModuleImageHostSubsystem>( SW_DELEGATE_METHOD( ModuleTypeLoaderDelegate, &ServerApp::loadModuleImages, this ) ) );
            const bool bExposed = _host.expose<IModuleImageHost>( *_pModuleImageHost );
            SW_ASSERT( bExposed );
            (void)bExposed;
            if ( _host.getCollection().initializeAll( SubsystemHostMask::All ) == false ) // 로거 앞이라 로그를 남기지 않는다
            {
                std::fprintf( stderr, "[ServerApp] host scope failed: %s\n", _host.getCollection().getError().c_str() );
                return false;
            }
        }
        if ( _engine.initialize( argc, pArgv, EngineHostRole::DedicatedServer, &_host ) == false )
        {
            SW_LOG_ERROR( "Engine initialization failed (dedicated server)" );
            return false;
        }
        // 헤드리스 작업(서버 팩의 씬 쿠킹)은 엔진 기동 안에서 끝났다. 실패는 종료 코드로 알린다. CookAssets.py 가 이 코드를 본다.
        if ( _engine.isHeadless() )
            return _engine.didHeadlessTaskFail() == false;

        if ( loadServerConfig() == false )
            return false;
        _tickNanoseconds = constant::kNanosecondsPerSecond / static_cast<int64>( _pServerConfig->_tickRateHz );
        if ( initializeObservability() == false )
            return false;
        collectPreviousCrashes();
        // 게임 인스턴스를 만들기 전에 묶는다 — 게임과 키트가 기동하며 서버 지표 · 상태 레지스트리에 자기 것을 올릴 수 있게 한다.
        _serverServiceContext._serverConfigPath = _serverConfigPath;
        _serverServiceContext._pServerConfig    = _pServerConfig;
        _serverServiceContext._pConfigManager   = _engine.getConfigManager();
        _serverServiceContext._pMetricRegistry  = _metricRegistry.get();
        _serverServiceContext._pHealthRegistry  = _healthRegistry.get();
        ServerServiceRegistry::bindHostContext( &_serverServiceContext );

        if ( _moduleHost == nullptr )
            _moduleHost = make_unique<ModuleHost>();
        if ( _moduleHost->initializeDedicatedServer( getLiveReloadManager() ) == false )
        {
            SW_LOG_ERROR( "The game module could not be started on the dedicated server" );
            return false;
        }
        // 게임 인스턴스 뒤에 시작한다. 서비스가 게임이 정한 상태(게임 서비스 등)를 볼 수 있게 하고, 종료는 그 반대 순서다.
        if ( startServerServices() == false )
            return false;

        _bSignalHandlerInstalled = ShutdownSignal::install() ? SW_TRUE : SW_FALSE;
        if ( _bSignalHandlerInstalled == SW_FALSE )
            SW_LOG_WARNING( "Shutdown signal handlers are not installed - stop the server with the 'quit' console command" );
        if ( _pServerConfig->_bConsoleInput )
            _console.start();
#if SW_DEV_COMMANDS_ENABLED
        _devConsole = make_unique<DevConsole>();
#endif

#if !defined( SW_SHIPPING )
        if ( startDebugTools() == false )
            return false;
#endif
        _bReady = SW_TRUE;
        // 여기부터 모든 줄에 `[tick=N]`이 붙는다. N 은 끝낸 틱 수이며, 지금 도는 틱의 0 기준 번호다. 멀티 프로세스 재생의 Output Log 가 서버 줄을 틱 기준으로 맞춰 본다.
        Logger::setClockNumber( LogClock::Tick, _tickCount );
        SW_LOG_INFO( "Dedicated server ready - target %#, %# Hz, game port %#, service port %#, startup %# ms", build::kTargetName, _pServerConfig->_tickRateHz,
                     _pServerConfig->_gamePort, _pServerConfig->_servicePort, ( MonotonicClock::nowNanoseconds() - _startNanoseconds ) / constant::kNanosecondsPerMillisecond );
        return true;
    }

    bool ServerApp::loadModuleImages()
    {
        SW_MEMORY_SCOPE( EngineMisc );
#if !defined( SW_SHIPPING )
        // 전용 서버는 Server 대상 모듈만 로드한다. Game 빌드에서도 에디터와 RHI 모듈을 로드하지 않는다.
        if ( ModuleCatalogLoader::loadAndResolve( static_cast<uint8>( ModuleTarget::Server ), _moduleCatalog, _moduleResolution ) == false )
            return false;
        _liveReloadManager = make_unique<LiveReloadManager>();
        // 모듈 DLL 을 언로드할 수 있는 곳은 "씬은 사라졌고 서비스는 아직 있는" 구간뿐이다(App 과 같은 훅).
        _pModuleImageHost->setScenesReleasedHook( SW_DELEGATE_LAMBDA( Delegate<void()>, [this]()
        {
            if ( _liveReloadManager != nullptr )
                _liveReloadManager->shutdown();
        } ) );
#endif
        _moduleHost = make_unique<ModuleHost>();
        if ( _moduleHost->loadModuleImages( getLiveReloadManager(), _moduleCatalog, _moduleResolution ) == false )
            return false;
#if !defined( SW_SHIPPING )
        // 모듈 호스트가 건 리로드 직전 콜백을 감싼다. 서버 서비스 코드가 리로드될 모듈 안에 있으므로 서비스를 먼저 멈춘다.
        if ( _liveReloadManager != nullptr )
        {
            _liveReloadManager->setOnBeforeCommitBatch(
                SW_DELEGATE_METHOD( LiveReloadManager::OnBeforeCommitBatchDelegate, &ServerApp::onBeforeModuleReload, this ) );
        }
#endif
        return true;
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
        ConfigManager*            pConfigManager = _engine.getConfigManager();
        const CommandLineManager* pCommandLine   = _engine.getCommandLineManager();
        string                    path           = GameSelection::getServerConfigPath();
        if ( pCommandLine != nullptr )
        {
            string overridePath;
            if ( pCommandLine->getArgument( CommandLineArgument::SERVER_CONFIG, overridePath ) && overridePath.empty() == false )
                path = overridePath;
        }
        if ( pConfigManager == nullptr )
            return false;
        // Shipping 도 디스크에서 읽는다. 운영자가 고치는 파일이므로 베이크하지 않는다. 파일이 없으면 Shipping 은 서버를 시작하지 않는다.
        // 기본 포트로 조용히 뜬 서버는 틀린 DB 와 캐시에 붙는 서버이기 때문이다.
        _serverConfigPath = path;
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
        _pServiceConnectionGauge =
            _metricRegistry->registerGauge( "server_service_connections", "Connections held by the server services (online service host and others)" );
        _processMetrics = make_unique<ProcessMetrics>();
        _processMetrics->initialize( _metricRegistry.get(), WallClock::nowUnixMilliseconds() - ( MonotonicClock::nowNanoseconds() - _startNanoseconds ) / constant::kNanosecondsPerMillisecond );
        if ( _pServerConfig->_opsPort == 0 )
            return true;
        OpsHTTPEndpointConfig opsSettings;
        if ( NetAddress::parse( _pServerConfig->_opsListenAddress, static_cast<uint16>( _pServerConfig->_opsPort ), opsSettings._bindAddress ) == false )
        {
            SW_LOG_ERROR( "Server config _opsListenAddress '%#' is not an IPv4 address", _pServerConfig->_opsListenAddress.c_str() );
            return false;
        }
        opsSettings._bindAddress._port = static_cast<uint16>( _pServerConfig->_opsPort ); // 포트는 `_opsPort` 가 기준이다(주소 필드에 포트를 적어도 무시한다).
        _opsTransport                  = StreamTransportFactory::createPlatformTransport();
        if ( _opsTransport == nullptr )
        {
            SW_LOG_ERROR( "Ops HTTP endpoint needs a stream transport and this platform has none" );
            return false;
        }
        StreamTransportConfig transportSettings;
        transportSettings._ioThreadCount      = 1;
        transportSettings._maxConnections     = ServerAppInternal::kOpsMaxConnections;
        transportSettings._idleTimeoutSeconds = ServerAppInternal::kOpsIdleTimeoutSeconds;
        _opsEndpoint                          = make_unique<OpsHTTPEndpoint>();
        if ( _opsEndpoint->initialize( _opsTransport.get(), transportSettings, opsSettings, _metricRegistry.get(), _healthRegistry.get() ) == false )
        {
            _opsEndpoint.reset();
            _opsTransport.reset();
            return false; // 운영자가 켠 엔드포인트를 열지 못하면 서버를 시작하지 않는다. 감시자가 볼 수 없는 서버이기 때문이다.
        }
        return true;
    }

    void ServerApp::shutdownObservability()
    {
        if ( _opsEndpoint != nullptr )
            _opsEndpoint->shutdown();
        _opsEndpoint.reset();
        _opsTransport.reset();
        _processMetrics.reset();
        _pTickHistogram          = nullptr;
        _pDroppedTickCounter     = nullptr;
        _pServiceConnectionGauge = nullptr;
        _healthRegistry.reset();
        _metricRegistry.reset();
    }

    bool ServerApp::startServerServices()
    {
        if ( _serverServices == nullptr )
            _serverServices = make_unique<ServerServiceManager>();
        return _serverServices->startAll( _serverServiceContext );
    }

    void ServerApp::collectPreviousCrashes()
    {
        const string crashFolder = getCrashReportFolder();
        if ( crashFolder.empty() || engine::areEngineServicesBound() == false )
            return;
        CrashReportService& crashReports  = engine::getSubsystem<CrashReportService>();
        const string        reportsFolder = FileUtil::joinPath( FileUtil::getDirectoryPart( FileUtil::trimTrailingSlashes( crashFolder ) ), CrashReportService::kReportsFolderName );
        crashReports.initialize( crashFolder, reportsFolder, CrashHandler::getSessionID() );
        _bCrashReportsStarted = SW_TRUE;
        // 서버의 보내기 동의는 플레이어 설정이 아니라 운영 설정의 몫이다. 그 설정이 생기기 전에는 기계 안에만 둔다(클라이언트 기본값과 같다).
        crashReports.setConsent( CrashReportConsent::Local );
        const uint32               newCount = crashReports.collectNewCrashes();
        vector<CrashReportSummary> listReport;
        crashReports.collectReports( listReport );
        if ( newCount > 0 )
            SW_LOG_WARNING( "Bundled %# crash report(s) from previous runs into '%#' (%# kept)", newCount, reportsFolder.c_str(), static_cast<uint32>( listReport.size() ) );
        MetricGauge* pCrashGauge = _metricRegistry->registerGauge( "server_crash_reports", "Crash report bundles kept on this machine (previous runs that crashed)" );
        if ( pCrashGauge != nullptr )
            pCrashGauge->set( static_cast<float64>( listReport.size() ) );
    }

    void ServerApp::updateProcessMetrics( int64 monotonicMs )
    {
        if ( monotonicMs < _nextProcessSampleMs )
            return;
        _nextProcessSampleMs = monotonicMs + ServerAppInternal::kProcessSampleIntervalMs;
        if ( _processMetrics != nullptr )
            _processMetrics->update();
        if ( _pServiceConnectionGauge != nullptr && _serverServices != nullptr )
            _pServiceConnectionGauge->set( static_cast<float64>( _serverServices->getConnectionCount() ) );
    }

#if !defined( SW_SHIPPING )
    bool ServerApp::onBeforeModuleReload( const vector<string>& listModuleName )
    {
        if ( _serverServices != nullptr && _serverServices->isRunning() )
        {
            SW_LOG_INFO( "Stopping %# server services for a module reload", _serverServices->getRunningCount() );
            _serverServices->stopAll();
            _bRestartServerServices = SW_TRUE;
        }
        return _moduleHost->onBeforeCommitBatch( listModuleName );
    }
#endif

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
#if !defined( SW_SHIPPING )
            if ( _debugChannel != nullptr )
                (void)_debugChannel->pump(); // 일시정지 중에도 돈다 - 멈춘 상태의 값을 읽는 것이 쓰임이다
#endif

            const ShutdownCause cause = ShutdownSignal::getRequestedCause();
            if ( cause != ShutdownCause::None || _engine.isQuitRequested() )
            {
                SW_LOG_INFO( "Dedicated server shutdown requested (%#) after %# ticks", ShutdownSignal::getCauseName( cause ), _tickCount );
                _healthRegistry->setDraining( true ); // /readyz 는 503 을 반환한다. 부하 분산기가 새 접속을 끊는다(종료하는 동안에도 /healthz 는 살아 있다).
                break;
            }

            // 서버 서비스는 게임 틱과 따로 돈다. 일시정지(Dev `pause`) 중에도 로그인이나 채팅 요청은 처리한다.
            if ( _serverServices != nullptr )
                _serverServices->update( WallClock::nowUnixMilliseconds() );
            updateProcessMetrics( ServerAppInternal::nowMonotonicMs() );

            if ( _bPaused == SW_TRUE && _pendingStepCount == 0 )
            {
                // 일시정지에서는 게임 틱만 멈춘다. 서버는 살아 있으므로 상태 확인 박동은 계속 보내(감시자가 재시작하지 않게 한다). 재개할 때 밀린 틱을 몰아서 돌지 않게 마감을 지금으로 바꾼다.
                _healthRegistry->markTick( ServerAppInternal::nowMonotonicMs() );
                MonotonicClock::sleepUntilNanoseconds( MonotonicClock::nowNanoseconds() + _tickNanoseconds );
                deadline = MonotonicClock::nowNanoseconds();
                continue;
            }

            const int64 tickStart = MonotonicClock::nowNanoseconds();
            updateOnce( deltaSeconds );
            const int64 tickTime = MonotonicClock::nowNanoseconds() - tickStart;
            _pTickHistogram->observe( static_cast<float64>( tickTime ) * 1.0e-9 );
#if SW_PROFILER_TRACE_ENABLED
            // 느린 틱 자동 캡처(`-gv_traceSlowTickMs`)는 꺼져 있으면 정수 비교 하나다. Shipping 서버에는 없다(지표 `server_tick_seconds` 만 있다).
            ProfilerTraceExport::updateSlowFrameCapture( engine::getSubsystem<FrameProfiler>(), static_cast<uint64>( tickStart ),
                                                         static_cast<uint64>( tickStart + tickTime ), "Server" );
#endif
            _healthRegistry->markTick( ServerAppInternal::nowMonotonicMs() );
            _statusTickSumNanoseconds += tickTime;
            _statusTickMaxNanoseconds = tickTime > _statusTickMaxNanoseconds ? tickTime : _statusTickMaxNanoseconds;
            ++_statusTickSampleCount;
            ++_tickCount;
            Logger::setClockNumber( LogClock::Tick, _tickCount );
#if !defined( SW_SHIPPING )
            if ( StateHashHistory::get().isRecording() && engine::areEngineServicesBound() )
            {
                Scene* pScene = engine::getSubsystem<SceneManager>().getActiveScene();
                if ( pScene != nullptr && pScene->getObjectManager() != nullptr )
                    StateHashHistory::get().record( _tickCount, SceneStateHash::compute( *pScene->getObjectManager() ) );
            }
#endif
            const bool bPausedStepCompleted = _bPaused == SW_TRUE && _pendingStepCount > 0 && --_pendingStepCount == 0;
            if ( bPausedStepCompleted )
                SW_LOG_INFO( "Server stepped to tick %# (paused)", _tickCount );
            if ( gv_serverExitAfterTicks > 0 && _tickCount >= static_cast<uint64>( gv_serverExitAfterTicks ) )
                ShutdownSignal::request( ShutdownCause::TickLimit );

            deadline += _tickNanoseconds;
            const int64 now = MonotonicClock::nowNanoseconds();
            if ( now - deadline > maxBehind )
            {
                // 따라잡을 수 없을 만큼 밀렸다. 밀린 시간을 버리고 지금부터 다시 센다(몰아서 돌다가 다음 틱을 또 밀지 않게 한다).
                const uint32 dropped = static_cast<uint32>( ( now - deadline ) / _tickNanoseconds );
                if ( _droppedTickCount == 0 ||
                     _droppedTickCount / ServerAppInternal::kDroppedTickWarningInterval != ( _droppedTickCount + dropped ) / ServerAppInternal::kDroppedTickWarningInterval )
                    SW_LOG_WARNING( "Server is behind: dropped %# ticks (total %#), last tick %# us", dropped, _droppedTickCount + dropped, tickTime / constant::kNanosecondsPerMicrosecond );
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

    void ServerApp::updateOnce( float32 deltaSeconds )
    {
        // App::run 의 한 프레임에서 창, 에디터, 사용자 설정, 백엔드 교체를 뺀 것이다. 한 틱은 고정 스텝 하나다.
        _engine.beginFrame( deltaSeconds, deltaSeconds ); // 서버에는 시간 배율이 없다 — 틱 하나가 실제 경과다.
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
        if ( _bRestartServerServices == SW_TRUE )
        {
            // 리로드가 끝났다(취소됐어도 서비스는 멈춰 있다). 새 모듈의 팩토리로 다시 시작한다.
            _bRestartServerServices = SW_FALSE;
            if ( startServerServices() == false )
                SW_LOG_ERROR( "Server services could not be restarted after the module reload - fix the error and reload again" );
        }
#endif
        const ModuleFrameState& frameState = _moduleHost->getFrameState();
        _engine.update( deltaSeconds, HostViewTargets{}, ViewCameraProviderDelegate{}, frameState._bTickScene == SW_TRUE );
        _engine.endFrame();
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
#if SW_DEV_COMMANDS_ENABLED
            SW_LOG_INFO( "Server commands: quit | stop | status | help (Dev: %#, dev commands and gv_* variables too)", ServerAppInternal::kLoopCommandHelp );
#else
            SW_LOG_INFO( "Server commands: quit | stop | status | help" );
#endif
            return;
        }
        if ( executePauseCommand( command ) )
            return;
#if SW_DEV_COMMANDS_ENABLED
        // Dev 는 개발 콘솔과 같은 명령(개발 명령과 gv_* 전역 변수)을 받는다. 응답과 실패는 DevConsole 이 로그에 남긴다. Shipping 서버는 위의 네 가지만 받는다.
        if ( _devConsole != nullptr )
        {
            (void)_devConsole->submit( command );
            return;
        }
#endif
        SW_LOG_WARNING( "Unknown server command '%#' - type help", string( command ).c_str() );
    }

    bool ServerApp::executePauseCommand( string_view command )
    {
#if SW_DEV_COMMANDS_ENABLED
        if ( command == "pause" )
        {
            _bPaused          = SW_TRUE;
            _pendingStepCount = 0;
            SW_LOG_INFO( "Server paused at tick %#", _tickCount );
            return true;
        }
        if ( command == "resume" )
        {
            _bPaused          = SW_FALSE;
            _pendingStepCount = 0;
            SW_LOG_INFO( "Server resumed at tick %#", _tickCount );
            return true;
        }
        const string_view kStep = "step";
        const bool        bStep = command.substr( 0, kStep.size() ) == kStep && ( command.size() == kStep.size() || command[kStep.size()] == ' ' );
        if ( bStep == false )
            return false;
        int32             stepCount = 1;
        const string_view argument  = StringUtil::trim( command.substr( kStep.size() ) );
        if ( argument.empty() == false && ( StringUtil::parseInt( argument, stepCount ) == false || stepCount <= 0 ) )
        {
            SW_LOG_WARNING( "step takes a positive tick count - got '%#'", string( argument ).c_str() );
            return true;
        }
        if ( _bPaused == SW_FALSE )
            SW_LOG_INFO( "Server paused at tick %#", _tickCount );
        _bPaused          = SW_TRUE; // 도는 중에 step 이면 멈추고 그만큼만 돈다.
        _pendingStepCount = static_cast<uint32>( stepCount );
        return true;
#else
        (void)command;
        return false;
#endif
    }

    void ServerApp::logStatus() const
    {
        const int64 uptimeSeconds   = ( MonotonicClock::nowNanoseconds() - _startNanoseconds ) / constant::kNanosecondsPerSecond;
        const int64 averageMicro    = _statusTickSampleCount > 0 ? _statusTickSumNanoseconds / _statusTickSampleCount / constant::kNanosecondsPerMicrosecond : 0;
        const int32 serviceCount    = _serverServices != nullptr ? _serverServices->getRunningCount() : 0;
        const int32 connectionCount = _serverServices != nullptr ? _serverServices->getConnectionCount() : 0;
        uint64      residentBytes   = 0;
        uint64      peakBytes       = 0;
        (void)ProcessMetrics::tryGetResidentBytes( residentBytes, peakBytes ); // 답하지 못하면 0 을 적는다
        SW_LOG_INFO( "Dedicated server status - uptime %# s, tick %#%#, tick time avg %# us / max %# us (budget %# us), dropped ticks %#, services %# (connections %#), "
                     "resident %# MiB",
                     uptimeSeconds, _tickCount, _bPaused == SW_TRUE ? " (paused)" : "", averageMicro, _statusTickMaxNanoseconds / constant::kNanosecondsPerMicrosecond, _tickNanoseconds / constant::kNanosecondsPerMicrosecond,
                     _droppedTickCount, serviceCount, connectionCount, residentBytes / static_cast<uint64>( constant::kBytesPerMegabyte ) );
    }

#if !defined( SW_SHIPPING )
    bool ServerApp::startDebugTools()
    {
        if ( gv_stateHashHistory > 0 )
            StateHashHistory::get().setCapacity( static_cast<uint32>( gv_stateHashHistory ) );
        if ( gv_debugChannelPort <= 0 )
            return true;
        const uint16 port = static_cast<uint16>( gv_debugChannelPort );
        _debugTransport   = StreamTransportFactory::createPlatformTransport();
        _debugChannel     = make_unique<DebugChannel>();
        DebugChannelConfig settings;
        settings._bindAddress = NetAddress::makeLoopback( port );
        settings._token       = gv_debugChannelToken;
        StreamTransportConfig transportSettings;
        transportSettings._idleTimeoutSeconds = 0.0; // 에디터는 서버가 멈춰 있는 동안에도 붙어 있다
        const bool bStarted                   = _devConsole != nullptr &&
                              _debugChannel->initialize( _debugTransport.get(), transportSettings, settings,
                                                         SW_DELEGATE_LAMBDA( DebugChannelExecuteDelegate, [this]( string_view line, string& outReply )
        {
            // 일시정지 · 한 틱 진행은 개발 명령이 아니라 서버 루프의 명령이다 - 같은 길로 받는다. 답은 서버가 지금 어디인지다.
            if ( executePauseCommand( line ) )
            {
                outReply = ( _bPaused == SW_TRUE ? "paused tick " : "running tick " ) + to_string( _tickCount );
                return true;
            }
            const bool bOk = _devConsole->execute( line, outReply ) == DevConsoleResult::Ok;
            // 채널 `help` 는 채널이 받는 줄 전부를 알린다 — 개발 콘솔의 목록(변수 · 개발 명령, 에디터 자동완성의 원천) 앞에 서버 루프 명령 한 줄.
            const string_view trimmed = StringUtil::trim( line );
            if ( bOk && StringUtil::equals( trimmed.substr( 0, trimmed.find( ' ' ) ), "help", true ) )
            {
                string reply{ "server loop: " };
                reply.append( ServerAppInternal::kLoopCommandHelp ).append( "\n" ).append( outReply );
                outReply = std::move( reply );
            }
            return bOk;
        } ) );
        if ( bStarted == false )
        {
            SW_LOG_ERROR( "The debug channel was requested (-gv_debugChannelPort=%#) but could not start", gv_debugChannelPort );
            _debugChannel.reset();
            _debugTransport.reset();
            return false;
        }
        return true;
    }
#endif

    void ServerApp::shutdown()
    {
        _console.stop();
#if !defined( SW_SHIPPING )
        if ( _debugChannel != nullptr )
            _debugChannel->shutdown();
        _debugChannel.reset();
        _debugTransport.reset();
        StateHashHistory::get().setCapacity( 0 );
#endif
        vector<string>{}.swap( _listPendingCommand ); // 엔진 종료의 누수 보고 전에 해제한다(기동 뒤에 잡은 용량).
#if SW_DEV_COMMANDS_ENABLED
        _devConsole.reset();
#endif
        // 서버 서비스는 모듈 코드다. 모듈을 언로드하기 전에, 시작의 반대 순서로 게임보다 먼저 멈춘다.
        if ( _serverServices != nullptr )
        {
            _serverServices->stopAll();
            _serverServices.reset();
        }
        // 게임과 모듈 인스턴스를 엔진보다 먼저 종료한다(App::shutdown 과 같은 순서).
        if ( _moduleHost != nullptr )
        {
            _moduleHost->shutdown();
            _moduleHost.reset();
        }
        ServerServiceRegistry::bindHostContext( nullptr ); // 게임과 모듈이 내려간 뒤 — 레지스트리를 지우기 전
        if ( _bCrashReportsStarted == SW_TRUE && engine::areEngineServicesBound() )
            engine::getSubsystem<CrashReportService>().shutdown();
        _bCrashReportsStarted = SW_FALSE;
        // 운영 엔드포인트는 게임을 종료하는 동안에도 비우는 중(/readyz 503)임을 알리다가 마지막에 닫는다. 로그를 남기므로 엔진(로거)보다 먼저 닫는다.
        shutdownObservability();
        // 로거는 엔진 종료와 함께 종료된다. 마지막 줄은 그 앞에 남긴다(테스트와 운영 도구가 이 줄로 정상 종료를 확인한다).
        SW_LOG_INFO( "Dedicated server shutdown complete" );
        _engine.shutdown();
        // 엔진 범위가 사라졌다 — 이제 호스트 범위를 내린다(모듈 이미지 창구 소멸).
        _host.destroyAll();
        _pModuleImageHost = nullptr;
        Logger::clearClockNumber();
        if ( _bSignalHandlerInstalled == SW_TRUE )
            ShutdownSignal::uninstall();
        // Windows 창 닫기와 시스템 종료 처리기가 이것을 기다린다.
        ShutdownSignal::notifyShutdownComplete();
    }
} // namespace sw
