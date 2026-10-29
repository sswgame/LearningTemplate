#include "pch.h"

#include "Server/ServerApp.h"

#include "ModuleManager/GameModuleManager.h"
#include "ModuleManager/ModuleCatalogLoader.h"
#include "ModuleManager/ModuleLoaderSubsystem.h"

#if !defined( SW_SHIPPING )
    #include "DevTools/AssetReload/ContentFolderWatchService.h"
    #include "DevTools/DevToolsHost.h"
    #include "ModuleManager/LiveReload/LiveReloadManager.h"
#endif

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Foundation/BuildInfo.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Sink/ConsoleLogOutput.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"
#include "Core/Network/NetTypes.h"
#include "Core/Network/Transport/IStreamTransport.h"
#include "Core/Process/ProcessEnvironment.h"
#include "Core/Process/ShutdownSignal.h"
#include "Core/Container/StringUtil.h"
#include "Core/Diagnostics/CrashContext.h"
#include "Core/Diagnostics/CrashHandler.h"
#include "Core/File/FileUtil.h"
#include "Core/String/fixed_string.h"
#include "Core/Time/MonotonicClock.h"
#include "Core/Time/WallClock.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Config/ConfigManager.h"
#include "Engine/Config/GameSelection.h"
#include "Engine/Config/Server/ServerConfig.h"
#include "Engine/Module/ModuleLoader.h"
#include "Engine/Net/GameConnectSecurity.h"
#include "Engine/Net/GameNetSession.h"
#include "Engine/Observability/MetricRegistry.h"
#include "Engine/Observability/DebugChannel.h"
#include "Engine/Scene/SceneStateHash.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/DedicatedServer/OpsHTTPEndpoint.h"
#include "Engine/Observability/ProcessMetrics.h"
#include "Engine/DedicatedServer/ServerFrameHistory.h"
#include "Engine/DedicatedServer/ServerServiceManager.h"
#include "Engine/DedicatedServer/ServiceHealthRegistry.h"
#include "Core/Console/DevCommandRegistry.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Profiling/ProfilerTraceExport.h"
#include "Engine/Telemetry/CrashReportService.h"
#if SW_DEV_COMMANDS_ENABLED
    #include "DevTools/Console/DevConsole.h"
#endif

#include "sw/config/ConfigConstants.h"

namespace sw
{
    /** @brief `-gv_serverExitAfterTicks=N`: N 틱을 돈 뒤 스스로 정상 종료합니다(기동과 종료 테스트용). Shipping 에도 등록하며, 배포 서버 산출물을 같은 테스트가 실행합니다. */
    SW_TEST_GLOBAL_VARIABLE_SHIPPED( int32, gv_serverExitAfterTicks, 0, "이 틱 수를 돈 뒤 정상 종료 (0=끄기)" );
    /** @brief `-gv_debugChannelPort=N`: 디버그 채널(개발 명령 원격 실행, 루프백 TCP)을 이 포트에 엽니다(0=끄기). 토큰(`gv_debugChannelToken`)이 있어야 열립니다. Shipping 에는 없습니다. */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_debugChannelPort, 0, "디버그 채널 포트 (0=끄기, 루프백 전용, 토큰 필요)" );
    SW_TEST_GLOBAL_VARIABLE( string, gv_debugChannelToken, "", "디버그 채널 접속 토큰" );
    /**
     * @brief `-gv_serverStartPaused=1`: 첫 틱을 돌기 전에 멈춘 채로 기동합니다(서버 디버깅 S7 되감기).
     * @details 명령(`intent.play` · `step N`)은 기동 뒤 표준 입력이나 디버그 채널로 오므로 그 사이 틱이 돌면 목표 틱에서 정확히 멈출 수 없다. 멈춘 채 뜨면 틱 0 에서
     *          재생을 걸고 `step` 으로 한 틱씩 진행할 수 있다(언리얼 `-pause` · 유니티 Play Mode 의 Pause On Start 와 같은 쓰임). Shipping 에는 없다.
     */
    SW_TEST_GLOBAL_VARIABLE( bool, gv_serverStartPaused, false, "첫 틱 전에 멈춘 채 기동 (재생 서버를 틱 0 에서 잡는다)" );
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
            /** @brief 관리 채널 연결이 이만큼(초) 말이 없으면 닫는다 - 열어 두고 잊은 연결이 쌓이지 않게. */
            static constexpr float64 kAdminIdleTimeoutSeconds = 300.0;
            /** @brief 프레임 이력 덤프(`ServerApp::dumpFrameHistory`)가 놓이는 폴더 이름입니다. 로그 폴더(`Saved/Logs`) 옆에 둔다(`CrashReports` 묶음 폴더와 같은 자리). */
            static constexpr const utf8* kServerReportsFolderName = "ServerReports";
            /** @brief 느린 틱 덤프의 최소 간격입니다. 과부하 서버가 덤프 때문에 더 느려지지 않게 한다. */
            static constexpr int64 kMinFrameDumpIntervalMs = 10000;
            /** @brief 틱 사용률 경고의 최소 간격입니다(1 분에 한 번). */
            static constexpr int64 kLoadWarningIntervalMs = 60000;

            /** @brief 크래시 문맥 파일 끝에 싣는 최근 틱 수입니다(문맥 파일 버퍼 8KB 안에서 — 한 줄 70 바이트쯤). */
            static constexpr uint32 kCrashTailTicks = 40;

            /** @brief 크래시 핸들러가 부르는 글쓰기(`bindCrashExtraTextWriter`)입니다. 이력 링 버퍼를 직접 읽는다 — 할당 · 잠금 없음. */
            static uint32 writeFrameCrashTail( void* pUser, utf8* pOutText, uint32 capacity )
            {
                const ServerFrameHistory* pHistory = static_cast<const ServerFrameHistory*>( pUser );
                return pHistory != nullptr ? pHistory->writeCrashTail( pOutText, capacity, kCrashTailTicks ) : 0u;
            }

            /** @brief 이 종료 까닭은 접속이 비기를 기다린다. 오케스트레이터의 종료 신호만 — 사람이 내린 것(콘솔 · Ctrl+C) · 테스트 틱 상한 · OS 가 시한을 주는 창 닫기 · 시스템 종료는 바로 내린다. */
            static bool shouldDrainFor( ShutdownCause cause ) { return cause == ShutdownCause::Terminate || cause == ShutdownCause::ServiceStop; }
#if SW_DEV_COMMANDS_ENABLED || !defined( SW_SHIPPING )
            /** @brief Dev 서버 루프의 명령(`executePauseCommand`)입니다. 표준 입력 `help` 와 디버그 채널 `help` 가 같은 줄을 보입니다(쓰는 두 곳과 같은 조건). */
            static constexpr const utf8* kLoopCommandHelp = "pause | resume | step [N]";
#endif

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

        /**
         * @brief 호스트 범위의 종료 신호 처리기(POSIX SIGTERM · SIGINT, Windows 콘솔 제어)입니다. 엔진보다 먼저 걸고 엔진 종료가 끝난 뒤 뗀다.
         * @details 걸지 못해도 서버는 선다(표준 입력 `quit` 로 끝낸다) — 로거가 서기 전이라 로그 아래 티어(`SW_LOW_LEVEL_WARNING`)로 알린다. 떼기는 건 것이 없으면 아무것도 하지 않는다.
         */
        class ShutdownSignalSubsystem final : public ISubsystem
        {
        public:
            SubsystemInitResult initializeSubsystem() override
            {
                if ( ShutdownSignal::install() == false )
                    SW_LOW_LEVEL_WARNING( "Shutdown signal handlers are not installed - stop the server with the 'quit' console command" );
                return SubsystemInitResult::Succeeded;
            }
            void        shutdownSubsystem() override { ShutdownSignal::uninstall(); }
            const utf8* getSubsystemName() const override { return "ShutdownSignal"; }
        };
    } // namespace

    ServerApp::ServerApp()
        : _host{}
        , _pDevTools{ nullptr }
        , _devToolsFrameSubscription{}
        , _pModuleImageHost{ nullptr }
        , _engine{}
        , _gameModuleManager{ nullptr }
        , _moduleReload{}
#if !defined( SW_SHIPPING )
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
        , _reloadStopsServices{}
        , _serverConfigPath{}
        , _serverServiceContext{}
        , _pServerConfig{ nullptr }
        , _pTickHistogram{ nullptr }
        , _pDroppedTickCounter{ nullptr }
        , _pServiceConnectionGauge{ nullptr }
        , _pTickUtilizationGauge{ nullptr }
        , _frameHistory{ nullptr }
        , _crashExtraTextSubscription{}
        , _lastFrameDumpMs{ 0 }
        , _loadWindowStartMs{ 0 }
        , _loadWindowBusyNanoseconds{ 0 }
        , _lastLoadWarningMs{ 0 }
        , _drainDeadlineMs{ 0 }
        , _tickCount{ 0 }
        , _startNanoseconds{ 0 }
        , _tickNanoseconds{ 0 }
        , _statusTickSumNanoseconds{ 0 }
        , _statusTickMaxNanoseconds{ 0 }
        , _statusTickSampleCount{ 0 }
        , _droppedTickCount{ 0 }
        , _pendingStepCount{ 0 }
        , _nextProcessSampleMs{ 0 }
        , _hostServices{}
        , _bReady{ SW_FALSE }
        , _bPaused{ SW_FALSE }
        , _bRestartServerServices{ SW_FALSE }
        , _bDraining{ SW_FALSE }
        , _bMaintenance{ SW_FALSE }
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
        // 호스트 범위를 먼저 세운다 — 엔진 범위의 부모다. 엔진의 `ModuleLoadStage` 단계가 여기서 모듈 라이브러리 창구를 찾아 서버 대상 모듈을 올린다(`loadModuleLibrarys`).
        // 종료 신호 처리기도 이 범위에 둔다 — 엔진 종료가 끝날 때까지 걸려 있어야 하고, 기동 중에 온 신호는 깃발로 남아 루프 첫 바퀴가 정상 종료한다.
        {
            SW_MEMORY_SCOPE( EngineMisc );
            _host.addSubsystem( make_unique<ShutdownSignalSubsystem>() );
        }
#if !defined( SW_SHIPPING )
        // 개발 도구 — 엔진 기동 확장(델리게이트)으로 걸어, 엔진이 기동 단계 범위를 채울 때 자기 서브시스템을 더한다(엔진은 무엇인지 모른다).
        {
            SW_MEMORY_SCOPE( EngineMisc );
            _pDevTools = &_host.addSubsystem( make_unique<DevToolsHost>() );
        }
        _engine.setStartupExtension( _pDevTools->makeStartupExtension() );
        // 콘텐츠 폴더 감시 — 기능 · 게임의 데이터 다시 읽기가 범위에서 찾는다(배포에는 없다).
        const bool bFolderWatchBound = _host.bind<IContentFolderWatch>( &_pDevTools->getContentFolderWatch() );
        SW_ASSERT( bFolderWatchBound );
        (void)bFolderWatchBound;
#endif
        _pModuleImageHost = ModuleLoaderSubsystem::initializeHostScope( _host, SW_DELEGATE_METHOD( ModuleTypeLoaderDelegate, &ServerApp::loadModuleLibrarys, this ), "ServerApp" );
        if ( _pModuleImageHost == nullptr )
            return false;
        if ( _engine.initialize( argc, pArgv, EngineHostRole::DedicatedServer, &_host ) == false )
        {
            SW_LOG_ERROR( "Engine initialization failed (dedicated server)" );
            return false;
        }
#if !defined( SW_SHIPPING )
        // 엔진이 섰다 — 개발 도구의 프레임 훅을 붙인다(씬 틱 뒤 HUD · 콘솔, 프레임 끝에 셰이더 재컴파일 · 단축키 · 설정 리로드).
        _pDevTools->setEngine( &_engine );
        _devToolsFrameSubscription = _engine.registerFrameHooks( _pDevTools->makeFrameHooks() );
#endif
        // 헤드리스 작업(서버 팩의 씬 쿠킹)은 엔진 기동 안에서 끝났다. 실패는 종료 코드로 알린다. 빌드(CookAssets 타깃) · `Pack.py` 가 이 코드를 본다.
        if ( _engine.isHeadless() )
            return _engine.didHeadlessTaskFail() == false;

        if ( loadServerConfig() == false )
            return false;
        _tickNanoseconds = constant::kNanosecondsPerSecond / static_cast<int64>( _pServerConfig->_tickRateHz );
        // 호스트 서비스(관측 · 크래시 묶음 · 서버 문맥 · 게임 · 개발 콘솔 · 디버그 채널 · 표준 입력)을 의존 순서로 세운다. App 과 같은 표(`HostService`)이고,
        // 내리는 것은 `shutdown` 의 역순 한 줄이다.
        if ( startHostServices( _hostServices, kArrServerHostService, SW_DELEGATE_METHOD( HostServiceStartDelegate, &ServerApp::startHostService, this ),
                                SW_DELEGATE_METHOD( HostServiceStopDelegate, &ServerApp::stopHostService, this ) ) == false )
        {
            SW_LOG_ERROR( "Host services failed to start: %#", _hostServices.getError().c_str() );
            return false;
        }
        SW_LOG_INFO( "Host services up: %#", describeHostServiceOrder( _hostServices ).c_str() );
        // 서버 서비스는 호스트 서비스가 아니다 — 모듈 리로드마다 멈췄다 다시 서는 수명이다. 게임 뒤에 시작해 서비스가 게임이 정한 상태를 보게 하고,
        // `shutdown` 이 호스트 서비스보다 먼저 멈춘다.
        if ( startServerServices() == false )
            return false;
        // 게임 포트가 접속 티켓을 요구하는데 확인할 조립이 서지 않았으면 모든 접속을 거절하는 서버가 된다 — 그런 서버로 뜨지 않는다.
        if ( _pServerConfig->_bGameConnectTicket && engine::getSubsystem<GameNetSession>().getConnectSecurity().isAuthenticatorBound() == false )
        {
            SW_LOG_ERROR( "Server config _bGameConnectTicket is on but no online kit bound a game connect authenticator (add the account kit 'gameConnect')" );
            return false;
        }

#if SW_DEV_COMMANDS_ENABLED
        if ( gv_serverStartPaused )
        {
            _bPaused = SW_TRUE;
            SW_LOG_INFO( "Server starts paused at tick %# (-gv_serverStartPaused)", _tickCount );
        }
#endif
        _bReady            = SW_TRUE;
        _loadWindowStartMs = ServerAppInternal::nowMonotonicMs();
        // 여기부터 모든 줄에 `[tick=N]`이 붙는다. N 은 끝낸 틱 수이며, 지금 도는 틱의 0 기준 번호다. 멀티 프로세스 재생의 Output Log 가 서버 줄을 틱 기준으로 맞춰 본다.
        Logger::setClockNumber( LogClock::Tick, _tickCount );
        SW_LOG_INFO( "Dedicated server ready - target %#, %# Hz, game port %#, service port %#, startup %# ms", build::kTargetName, _pServerConfig->_tickRateHz,
                     _pServerConfig->_gamePort, _pServerConfig->_servicePort, ( MonotonicClock::nowNanoseconds() - _startNanoseconds ) / constant::kNanosecondsPerMillisecond );
        return true;
    }

    bool ServerApp::loadModuleLibrarys()
    {
        SW_MEMORY_SCOPE( EngineMisc );
#if !defined( SW_SHIPPING )
        // 전용 서버는 Server 대상 모듈만 로드한다. Game 빌드에서도 에디터와 RHI 모듈을 로드하지 않는다.
        if ( ModuleCatalogLoader::loadAndResolve( static_cast<uint8>( ModuleTarget::Server ), _moduleCatalog, _moduleResolution ) == false )
            return false;
        // 리로드 구동기(App 과 같은 것)가 매니저를 만들고 자신을 호스트 범위에 걸며, 매니저 종료를 "씬은 사라졌고 서비스는 아직 있는" 구간의 훅에 건다.
        _moduleReload.initialize( _host, *_pModuleImageHost );
#endif
        _gameModuleManager = make_unique<GameModuleManager>();
        if ( _gameModuleManager->loadModuleLibrarys( _moduleReload.getLiveReloadManager(), _moduleCatalog, _moduleResolution ) == false )
            return false;
#if !defined( SW_SHIPPING )
        // 서버 서비스 코드가 리로드될 모듈 안에 있으므로 서비스를 먼저 멈춘다. 모듈 호스트보다 나중에 더하므로 먼저 불린다(배치 콜백은 나중 것이 먼저).
        LiveReloadManager* pLiveReloadManager = _moduleReload.getLiveReloadManager();
        if ( pLiveReloadManager != nullptr )
            _reloadStopsServices = pLiveReloadManager->addOnBeforeCommitBatch(
                SW_DELEGATE_METHOD( LiveReloadManager::OnBeforeCommitBatchDelegate, &ServerApp::onBeforeModuleReload, this ) );
#endif
        return true;
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
        // 모든 구성이 디스크에서 읽는다. 운영자가 고치는 파일이므로 베이크하지 않는다. 파일이 없으면 서버를 시작하지 않는다.
        // 기본 포트로 조용히 뜬 서버는 틀린 DB 와 캐시에 붙는 서버이기 때문이다(Dev 도 같다).
        _serverConfigPath = path;
        _pServerConfig    = pConfigManager->loadHostConfig<ServerConfig>( ConfigSource::disk( path ) );
        if ( _pServerConfig == nullptr )
        {
            SW_LOG_ERROR( "Server config '%#' could not be read - pass -server-config=<path>", path.c_str() );
            return false;
        }
        if ( _pServerConfig->_bJSONLogs )
        {
            // 로거는 엔진 기동에서 이미 섰다. 설정을 읽은 지금부터 줄이 JSON 한 줄로 나간다(콘솔 장치는 꺼진다). 켰는데 못 켜면 수집기가 못 읽는 줄을 내는 서버가 되므로 기동을 멈춘다.
            ILogSink* pLogSink = Logger::getGlobalSink();
            if ( pLogSink == nullptr || pLogSink->enableJSONLinesOutput() == false )
            {
                SW_LOG_ERROR( "Server config _bJSONLogs is on but the log sink could not add the JSON lines output" );
                return false;
            }
        }
        SW_LOG_INFO( "Server config: %#", path.c_str() );
        return true;
    }

    bool ServerApp::startHostService( HostServiceID id )
    {
        switch ( id )
        {
            case HostServiceID::Observability:
            {
                return initializeObservability();
            }
            case HostServiceID::CrashReports:
            {
                return collectPreviousCrashes();
            }
            case HostServiceID::ServiceContext:
            {
                // 게임 인스턴스를 만들기 전에 건다(의존 선언) — 게임과 키트가 기동하며 서버 지표 · 상태 레지스트리에 자기 것을 올리고 게임 포트를 연다.
                _serverServiceContext._serverConfigPath = _serverConfigPath;
                _serverServiceContext._pServerConfig    = _pServerConfig;
                _serverServiceContext._pConfigManager   = _engine.getConfigManager();
                _serverServiceContext._pMetricRegistry  = _metricRegistry.get();
                _serverServiceContext._pHealthRegistry  = _healthRegistry.get();
                return _host.bind( &_serverServiceContext ); // 게임 · 키트(네트워크 세션)가 `engine::findSubsystem<ServerServiceContext>()` 로 찾는다
            }
            case HostServiceID::Game:
            {
                if ( _gameModuleManager == nullptr )
                    _gameModuleManager = make_unique<GameModuleManager>();
                const bool bStarted = _gameModuleManager->initializeDedicatedServer( _moduleReload.getLiveReloadManager() );
                if ( bStarted == false )
                    SW_LOG_ERROR( "The game module could not be started on the dedicated server" );
                return bStarted;
            }
            case HostServiceID::StandardInput:
            {
                if ( _pServerConfig->_bConsoleInput )
                    _console.start();
                return true;
            }
            case HostServiceID::DevConsole:
#if SW_DEV_COMMANDS_ENABLED
                _devConsole = make_unique<DevConsole>();
#endif
                return true;
            case HostServiceID::DebugChannel:
            {
#if !defined( SW_SHIPPING )
                if ( startDebugTools() == false )
                    return false;
#endif
                return startAdminChannel(); // 관리 채널은 Shipping 도 여는 길이다(기본 꺼짐, 켜면 토큰 필수)
            }
            case HostServiceID::BackendSwitcher:
            case HostServiceID::UserSettings:
            case HostServiceID::RuntimeUIHooks:
            {
                break;
            }
        }
        SW_LOG_ERROR( "Host service %# is not a dedicated server service", HostService::getName( id ) );
        return false;
    }

    void ServerApp::stopHostService( HostServiceID id )
    {
        switch ( id )
        {
            case HostServiceID::Observability:
            {
                // 운영 엔드포인트는 게임을 내리는 동안에도 비우는 중(/readyz 503)을 알리다가 마지막에 닫는다. 로그를 남기므로 엔진(로거)보다 먼저 닫는다.
                shutdownObservability();
                return;
            }
            case HostServiceID::CrashReports:
            {
                engine::getSubsystem<CrashReportService>().shutdown(); // 묶을 폴더가 없어 초기화하지 않았어도 된다
                return;
            }
            case HostServiceID::ServiceContext:
            {
                _host.unbind<ServerServiceContext>(); // 게임과 모듈이 내려간 뒤 — 레지스트리를 지우기 전
                return;
            }
            case HostServiceID::Game:
            {
                _gameModuleManager->shutdown();
                _gameModuleManager.reset();
                return;
            }
            case HostServiceID::StandardInput:
            {
                _console.stop(); // 읽기 스레드를 멈추고, 줄 버퍼도 엔진 종료의 누수 보고 전에 해제한다
                return;
            }
            case HostServiceID::DevConsole:
#if SW_DEV_COMMANDS_ENABLED
                _devConsole.reset();
#endif
                return;
            case HostServiceID::DebugChannel:
#if !defined( SW_SHIPPING )
                if ( _debugChannel != nullptr )
                    _debugChannel->shutdown();
                _debugChannel.reset();
                _debugTransport.reset();
#endif
                if ( _adminChannel != nullptr )
                    _adminChannel->shutdown();
                _adminChannel.reset();
                _adminTransport.reset();
                return;
            case HostServiceID::BackendSwitcher:
            case HostServiceID::UserSettings:
            case HostServiceID::RuntimeUIHooks:
            {
                return;
            }
        }
    }

    bool ServerApp::initializeObservability()
    {
        _metricRegistry      = make_unique<MetricRegistry>();
        _healthRegistry      = make_unique<ServiceHealthRegistry>();
        _pTickHistogram      = _metricRegistry->registerHistogram( "server_tick_seconds", "Dedicated server tick body duration", MetricRegistry::makeLatencyBounds() );
        _pDroppedTickCounter = _metricRegistry->registerCounter( "server_dropped_ticks_total", "Ticks dropped because the server fell too far behind" );
        _pServiceConnectionGauge =
            _metricRegistry->registerGauge( "server_service_connections", "Connections held by the server services (online service host and others)" );
        _pTickUtilizationGauge = _metricRegistry->registerGauge( "server_tick_utilization_ratio", "Tick body time divided by wall time over the last second (1.0 = no idle time left)" );
        _frameHistory          = make_unique<ServerFrameHistory>();
        _frameHistory->initialize( static_cast<uint32>( _pServerConfig->_frameHistoryTicks ) );
        // 크래시 때 이력을 새 파일로 쓰지 않고, 크래시 핸들러가 문맥 파일을 쓰며 이 링 버퍼를 읽어 끝에 싣는다(풀기는 `shutdownObservability`).
        if ( _frameHistory->getCapacity() > 0 )
            _crashExtraTextSubscription = bindCrashExtraTextWriter( &ServerAppInternal::writeFrameCrashTail, _frameHistory.get() );
        _processMetrics = make_unique<ProcessMetrics>();
        _processMetrics->initialize( _metricRegistry.get(), WallClock::nowUnixMilliseconds().count() - ( MonotonicClock::nowNanoseconds() - _startNanoseconds ) / constant::kNanosecondsPerMillisecond );
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
        _pTickUtilizationGauge   = nullptr;
        _crashExtraTextSubscription.reset(); // 이력을 지우기 전에 푼다 — 크래시 경로가 지운 버퍼를 읽지 않게
        _frameHistory.reset();
        _healthRegistry.reset();
        _metricRegistry.reset();
    }

    bool ServerApp::startServerServices()
    {
        if ( _serverServices == nullptr )
            _serverServices = make_unique<ServerServiceManager>();
        return _serverServices->startAll( _serverServiceContext );
    }

    bool ServerApp::collectPreviousCrashes()
    {
        const string crashFolder = getCrashReportFolder();
        if ( crashFolder.empty() || engine::areEngineServicesBound() == false )
            return true;
        CrashReportService& crashReports  = engine::getSubsystem<CrashReportService>();
        const string        reportsFolder = FileUtil::joinPath( FileUtil::getDirectoryPart( FileUtil::trimTrailingSlashes( crashFolder ) ), CrashReportService::kReportsFolderName );
        crashReports.initialize( crashFolder, reportsFolder, CrashHandler::getSessionID() );
        // 서버의 보내기 동의는 플레이어 설정이 아니라 운영 설정(`ServerConfig::_crashReportConsent`)의 몫이다 — 기본은 기계 안에만(`local`).
        // 서버에는 "보낼까요?" 를 물을 사람이 없어 `ask` 는 받지 않고, 모르는 값은 오류로 기동을 멈춘다(보내지 않는 쪽으로 조용히 바꾸지 않는다).
        CrashReportConsent consent{ CrashReportConsent::Local };
        if ( parseCrashReportConsent( _pServerConfig->_crashReportConsent, consent ) == false || consent == CrashReportConsent::Ask )
        {
            SW_LOG_ERROR( "Server config _crashReportConsent '%#' must be 'local' or 'send'", _pServerConfig->_crashReportConsent.c_str() );
            return false;
        }
        crashReports.setConsent( consent );
        const uint32               newCount = crashReports.collectNewCrashes();
        vector<CrashReportSummary> listReport;
        crashReports.collectReports( listReport );
        // 지난 실행의 크래시를 묶는 것은 이번 실행의 이상이 아니라 정리다 — 클라이언트(TelemetryService)와 같이 정보로 남긴다. 크래시 자체는 그 실행이 이미
        // 오류로 남겼다. 경고로 두면 같은 바이너리 폴더(Bin/Saved/Logs)를 쓰는 앞선 실행의 잔재가 이번 실행의 판정(시나리오의 처리 안 된 경고)을 바꾼다.
        if ( newCount > 0 )
            SW_LOG_INFO( "Bundled %# crash report(s) from previous runs into '%#' (%# kept)", newCount, reportsFolder.c_str(), static_cast<uint32>( listReport.size() ) );
        MetricGauge* pCrashGauge = _metricRegistry->registerGauge( "server_crash_reports", "Crash report bundles kept on this machine (previous runs that crashed)" );
        if ( pCrashGauge != nullptr )
            pCrashGauge->set( static_cast<float64>( listReport.size() ) );
        return true;
    }

    void ServerApp::onTickFinished( int64 tickNanoseconds, int64 monotonicMs )
    {
        _loadWindowBusyNanoseconds += tickNanoseconds;
        if ( _frameHistory == nullptr )
            return;
        ServerFrameSample sample;
        sample._tick             = _tickCount;
        sample._bodyMicroseconds = tickNanoseconds / constant::kNanosecondsPerMicrosecond;
        sample._connectionCount  = _serverServices != nullptr ? static_cast<uint32>( _serverServices->getConnectionCount() ) : 0u;
        sample._droppedTickTotal = _droppedTickCount;
        _frameHistory->record( sample );
        const int64 slowNanoseconds = static_cast<int64>( _pServerConfig->_slowTickDumpMs ) * constant::kNanosecondsPerMillisecond;
        const bool  bSlowTick       = slowNanoseconds > 0 && tickNanoseconds >= slowNanoseconds;
        const bool  bDumpDue        = monotonicMs - _lastFrameDumpMs >= ServerAppInternal::kMinFrameDumpIntervalMs;
        if ( bSlowTick && bDumpDue )
        {
            _lastFrameDumpMs = monotonicMs;
            dumpFrameHistory( "slow_tick" );
        }
    }

    void ServerApp::dumpFrameHistory( const utf8* pReason )
    {
        if ( _frameHistory == nullptr || _frameHistory->getCount() == 0 )
            return;
        const string crashFolder = getCrashReportFolder();
        if ( crashFolder.empty() )
            return; // 로그 폴더가 서지 않았다 — 덤프를 놓을 곳이 없다
        const string reportsFolder = FileUtil::joinPath( FileUtil::getDirectoryPart( FileUtil::trimTrailingSlashes( crashFolder ) ), ServerAppInternal::kServerReportsFolderName );
        if ( FileUtil::ensureDirectoryExists( reportsFolder ) == false )
            return; // 실패는 ensureDirectoryExists 가 남겼다
        string          netStatsLine;
        GameNetSession* pNetSession = engine::areEngineServicesBound() ? engine::findSubsystem<GameNetSession>() : nullptr;
        if ( pNetSession != nullptr )
            netStatsLine = pNetSession->makeStatsReport().makeLine(); // 메시지 종류별 누적 셈 — 덤프 순간 한 번만 읽는다(틱마다 읽으면 Profiler 탭의 "지난 보고" 상태를 건드린다)
        string text;
        _frameHistory->writeReport( text, pReason, CrashHandler::getSessionID(), netStatsLine );
        string fileName{ "frames_" };
        fileName.append( CrashHandler::getSessionID() ).append( "_tick" ).append( to_string( _tickCount ) ).append( "_" ).append( pReason ).append( ".txt" );
        const string path = FileUtil::joinPath( reportsFolder, fileName );
        if ( FileUtil::writeTextFile( path, text ) == false )
        {
            SW_LOG_WARNING( "Frame history could not be written to '%#'", path.c_str() );
            return;
        }
        SW_LOG_INFO( "Frame history (%#) written to '%#' (%# ticks)", pReason, path.c_str(), _frameHistory->getCount() );
    }

    void ServerApp::updateLoad( int64 monotonicMs )
    {
        const int64 windowMs = monotonicMs - _loadWindowStartMs;
        if ( windowMs <= 0 )
            return;
        const float64 utilization  = static_cast<float64>( _loadWindowBusyNanoseconds ) / ( static_cast<float64>( windowMs ) * static_cast<float64>( constant::kNanosecondsPerMillisecond ) );
        _loadWindowStartMs         = monotonicMs;
        _loadWindowBusyNanoseconds = 0;
        if ( _pTickUtilizationGauge != nullptr )
            _pTickUtilizationGauge->set( utilization );
        const float64 warnRatio   = static_cast<float64>( _pServerConfig->_tickLoadWarnPercent ) * 0.01;
        const bool    bOverloaded = warnRatio > 0.0 && utilization > warnRatio;
        const bool    bWarnDue    = monotonicMs - _lastLoadWarningMs >= ServerAppInternal::kLoadWarningIntervalMs;
        if ( bOverloaded && bWarnDue )
        {
            _lastLoadWarningMs = monotonicMs;
            SW_LOG_WARNING( "Server tick load is %# percent of the tick budget (warn above %# percent) - the server is close to falling behind", static_cast<int32>( utilization * 100.0 ),
                            _pServerConfig->_tickLoadWarnPercent );
        }
    }

    int32 ServerApp::countDrainBlockers() const
    {
        // 서비스 연결(로그인 · 채팅 …) + 게임 UDP 접속(복제 키트의 플레이어) + 키트가 끝내야 하는 진행 중인 일(매치 · 거래). 셋이 모두 0 이어야 조용히 내려갈 수 있다.
        const int32           serviceConnections = _serverServices != nullptr ? _serverServices->getConnectionCount() : 0;
        const int32           openWork           = _serverServices != nullptr ? _serverServices->getOpenWorkCount() : 0;
        const GameNetSession* pNetSession        = engine::areEngineServicesBound() ? engine::findSubsystem<GameNetSession>() : nullptr;
        const int32           gameConnections    = pNetSession != nullptr && pNetSession->getRole() == GameNetRole::Server ? pNetSession->getConnectedCount() : 0;
        return serviceConnections + gameConnections + openWork;
    }

    void ServerApp::beginDrain( ShutdownCause cause, int64 monotonicMs )
    {
        _bDraining = SW_TRUE;
        _healthRegistry->setDraining( true ); // /readyz 는 503 을 반환한다. 부하 분산기가 새 접속을 끊는다(종료하는 동안에도 /healthz 는 살아 있다).
        const bool  bWait  = ServerAppInternal::shouldDrainFor( cause );
        const int64 waitMs = bWait ? static_cast<int64>( _pServerConfig->_drainWaitSeconds ) * constant::kMillisecondsPerSecond : 0;
        _drainDeadlineMs   = monotonicMs + waitMs;
        // 키트에 종료를 알리고(진행 중인 일 마무리) 게임 접속자에게 끊김 사유를 보낸다. 기다리지 않는 종료(콘솔 · Ctrl+C)는 사유 패킷이 나가기 전에 내려갈 수 있어 최선만 다한다.
        ServerShutdownNotice notice;
        notice._cause        = cause;
        notice._graceSeconds = static_cast<uint32>( waitMs / constant::kMillisecondsPerSecond );
        if ( _serverServices != nullptr )
            _serverServices->beginShutdown( notice );
        GameNetSession* pNetSession = engine::areEngineServicesBound() ? engine::findSubsystem<GameNetSession>() : nullptr;
        if ( pNetSession != nullptr && pNetSession->getRole() == GameNetRole::Server )
            pNetSession->sendShutdownNotice( GameShutdownNotice{ bWait ? GameShutdownReason::ServerRestart : GameShutdownReason::ServerStopped, notice._graceSeconds } );
        if ( waitMs > 0 )
            SW_LOG_INFO( "Draining: waiting up to %# s for %# connections / open jobs to finish (%#)", _pServerConfig->_drainWaitSeconds, countDrainBlockers(),
                         ShutdownSignal::getCauseName( cause ) );
    }

    bool ServerApp::isDrainFinished( int64 monotonicMs ) const
    {
        if ( monotonicMs >= _drainDeadlineMs )
            return true;
        return countDrainBlockers() == 0;
    }

    void ServerApp::updateProcessMetrics( int64 monotonicMs )
    {
        if ( monotonicMs < _nextProcessSampleMs )
            return;
        _nextProcessSampleMs = monotonicMs + ServerAppInternal::kProcessSampleIntervalMs;
        updateLoad( monotonicMs );
        if ( _processMetrics != nullptr )
            _processMetrics->update();
        if ( _pServiceConnectionGauge != nullptr && _serverServices != nullptr )
            _pServiceConnectionGauge->set( static_cast<float64>( _serverServices->getConnectionCount() ) );
    }

#if !defined( SW_SHIPPING )
    bool ServerApp::onBeforeModuleReload( [[maybe_unused]] const vector<string>& listModuleName )
    {
        if ( _serverServices != nullptr && _serverServices->isRunning() )
        {
            SW_LOG_INFO( "Stopping %# server services for a module reload", _serverServices->getRunningCount() );
            _serverServices->stopAll();
            _bRestartServerServices = SW_TRUE;
        }
        return true; // 게임 쪽 처리는 모듈 호스트가 건 배치 콜백이 이어서 한다
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
            if ( _console.poll( SW_DELEGATE_METHOD( StandardInputCommandDelegate, &ServerApp::executeCommand, this ) ) )
                ShutdownSignal::request( ShutdownCause::ConsoleCommand );
#if !defined( SW_SHIPPING )
            if ( _debugChannel != nullptr )
                (void)_debugChannel->pump(); // 일시정지 중에도 돈다 - 멈춘 상태의 값을 읽는 것이 쓰임이다
#endif
            if ( _adminChannel != nullptr )
                (void)_adminChannel->pump();

            const ShutdownCause cause = ShutdownSignal::getRequestedCause();
            if ( cause != ShutdownCause::None || _engine.isQuitRequested() )
            {
                const int64 nowMs = ServerAppInternal::nowMonotonicMs();
                if ( _bDraining == SW_FALSE )
                    beginDrain( cause, nowMs ); // 처음 본 순간 /readyz 를 503 으로 바꾸고 기한을 정한다
                if ( isDrainFinished( nowMs ) )
                {
                    SW_LOG_INFO( "Dedicated server shutdown requested (%#) after %# ticks", ShutdownSignal::getCauseName( cause ), _tickCount );
                    break;
                }
                // 접속이 남았고 기한 전이다 — 틱을 계속 돌려 진행 중인 매치 · 거래가 끝나게 한다(아래의 일반 틱 경로).
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
            onTickFinished( tickTime, ServerAppInternal::nowMonotonicMs() );
#if !defined( SW_SHIPPING )
            StateHashHistory* pStateHashHistory = engine::areEngineServicesBound() ? engine::findSubsystem<StateHashHistory>() : nullptr;
            if ( pStateHashHistory != nullptr && pStateHashHistory->isRecording() )
            {
                pStateHashHistory->recordActiveScene( _tickCount );
                // 클라이언트가 같은 틱 번호로 자기 해시를 남긴다(S7) — 이력을 남기는 서버만 보낸다.
                GameNetSession* pNetSession = engine::findSubsystem<GameNetSession>();
                if ( pNetSession != nullptr )
                    pNetSession->sendServerTick( _tickCount );
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
        _gameModuleManager->beginFrame();
        {
            SW_PROFILE_SCOPE( "GT.Game.fixedUpdate" );
            _gameModuleManager->fixedUpdateGame( deltaSeconds );
        }
        {
            SW_PROFILE_SCOPE( "GT.Game.update" );
            _gameModuleManager->updateGame( deltaSeconds );
        }
        _moduleReload.update();
#if !defined( SW_SHIPPING )
        if ( _bRestartServerServices == SW_TRUE )
        {
            // 리로드가 끝났다(취소됐어도 서비스는 멈춰 있다). 새 모듈의 팩토리로 다시 시작한다.
            _bRestartServerServices = SW_FALSE;
            if ( startServerServices() == false )
                SW_LOG_ERROR( "Server services could not be restarted after the module reload - fix the error and reload again" );
        }
#endif
        const ModuleFrameState& frameState = _gameModuleManager->getFrameState();
        _engine.update( deltaSeconds, HostViewTargets{}, ViewCameraProviderDelegate{}, frameState._bTickScene == SW_TRUE );
        _engine.endFrame();
    }

    void ServerApp::executeCommand( string_view command )
    {
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
            (void)_devConsole->submit( command ); // 응답과 실패는 DevConsole 이 로그에 남긴다(위 설명)
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

    bool ServerApp::startAdminChannel()
    {
        if ( _pServerConfig->_adminChannelPort <= 0 )
            return true;
        // 토큰은 환경에서만 읽는다(설정 파일 · 명령줄에 두지 않는다). 없거나 비면 채널을 토큰 없이 여는 대신 기동을 멈춘다.
        string token;
        if ( ProcessEnvironment::tryReadNonEmpty( _pServerConfig->_adminChannelTokenEnvironment, token ) == false )
        {
            SW_LOG_ERROR( "The admin channel is enabled (_adminChannelPort=%#) but the token environment variable '%#' is not set or empty - the server does not start without a token",
                          _pServerConfig->_adminChannelPort, _pServerConfig->_adminChannelTokenEnvironment.c_str() );
            return false;
        }
        _adminTransport = StreamTransportFactory::createPlatformTransport();
        _adminChannel   = make_unique<DebugChannel>();
        DebugChannelConfig settings;
        settings._bindAddress = NetAddress::makeLoopback( static_cast<uint16>( _pServerConfig->_adminChannelPort ) );
        settings._token       = std::move( token );
        StreamTransportConfig transportSettings;
        transportSettings._idleTimeoutSeconds = ServerAppInternal::kAdminIdleTimeoutSeconds; // 열어 두고 잊은 연결을 닫는다
        const bool bStarted                   = _adminChannel->initialize( _adminTransport.get(), transportSettings, settings,
                                                                           SW_DELEGATE_LAMBDA( DebugChannelExecuteDelegate, [this]( string_view line, string& outReply )
                          { return executeAdminCommand( line, outReply ); } ) );
        if ( bStarted == false )
        {
            SW_LOG_ERROR( "The admin channel (_adminChannelPort=%#) could not start", _pServerConfig->_adminChannelPort );
            _adminChannel.reset();
            _adminTransport.reset();
            return false;
        }
        SW_LOG_INFO( "Admin channel listening on loopback port %# (token from environment variable %#)", _adminChannel->getListenPort(),
                     _pServerConfig->_adminChannelTokenEnvironment.c_str() );
        return true;
    }

    bool ServerApp::executeAdminCommand( string_view line, string& outReply )
    {
        const string_view command  = StringUtil::trim( line );
        const size_t      space    = command.find( ' ' );
        const string_view name     = space == string_view::npos ? command : command.substr( 0, space );
        const string_view argument = space == string_view::npos ? string_view{} : StringUtil::trim( command.substr( space + 1 ) );
        if ( name == "help" )
        {
            outReply = "admin commands: status | maintenance on|off|state | shutdown";
            return true;
        }
        if ( name == "status" )
        {
            const int32 connectionCount = _serverServices != nullptr ? _serverServices->getConnectionCount() : 0;
            outReply                    = "tick " + to_string( _tickCount ) + " connections " + to_string( connectionCount ) + " maintenance " + ( _bMaintenance == SW_TRUE ? "on" : "off" ) + " draining " +
                       ( _bDraining == SW_TRUE ? "yes" : "no" );
            return true;
        }
        if ( name == "maintenance" )
        {
            if ( argument == "state" )
            {
                outReply = _bMaintenance == SW_TRUE ? "maintenance on" : "maintenance off";
                return true;
            }
            if ( argument != "on" && argument != "off" )
            {
                outReply = "usage: maintenance on|off|state";
                return false;
            }
            if ( _bDraining == SW_TRUE )
            {
                outReply = "the server is draining for shutdown - maintenance mode cannot change";
                return false;
            }
            _bMaintenance = argument == "on" ? SW_TRUE : SW_FALSE;
            _healthRegistry->setDraining( _bMaintenance == SW_TRUE ); // /readyz 503 이면 부하 분산기가 새 접속을 보내지 않는다. 이미 접속한 사람은 그대로다.
            SW_LOG_INFO( "Admin channel: maintenance mode %#", _bMaintenance == SW_TRUE ? "on" : "off" );
            outReply = _bMaintenance == SW_TRUE ? "maintenance on" : "maintenance off";
            return true;
        }
        if ( name == "shutdown" )
        {
            SW_LOG_INFO( "Admin channel: graceful shutdown requested" );
            ShutdownSignal::request( ShutdownCause::Terminate ); // 종료 신호(SIGTERM)와 같은 길 - 드레인 뒤 정상 종료
            outReply = "shutdown requested";
            return true;
        }
        outReply = "unknown admin command (help lists them)";
        return false;
    }

    void ServerApp::shutdown()
    {
        // 서버 서비스는 모듈 코드이고 호스트 서비스가 아니다 — 호스트 서비스(게임 · 모듈)이 내려가기 전에 멈춘다.
        if ( _serverServices != nullptr )
        {
            _serverServices->stopAll();
            _serverServices.reset();
        }
        _reloadStopsServices.reset();
        // 호스트 서비스를 선 순서의 역순으로 내린다 — 표준 입력 · 디버그 채널 · 개발 콘솔, 게임(엔진보다 먼저), 서버 문맥, 크래시 묶음, 마지막에 관측.
        // 순서는 `HostService` 의 의존 선언이 정한다(App 과 같은 표). 헤드리스 부팅 · 기동 실패면 선 것만 내린다.
        _hostServices.destroyAll();
        // 헤드리스 부팅은 이미지만 올리고 호스트 서비스(게임)을 세우지 않는다 — 이미지를 쥔 모듈 호스트는 엔진(모듈 라이브러리 종료 단계)보다 먼저 놓는다.
        _gameModuleManager.reset();
        // 로거는 엔진 종료와 함께 종료된다. 마지막 줄은 그 앞에 남긴다(테스트와 운영 도구가 이 줄로 정상 종료를 확인한다).
        SW_LOG_INFO( "Dedicated server shutdown complete" );
        // 엔진이 엔진 범위를 내린 뒤 기동(종료 누수 보고)보다 먼저 호스트 범위를 비운다(모듈 라이브러리 창구 소멸, 종료 신호 처리기 떼기 — `Engine::shutdown`).
#if !defined( SW_SHIPPING )
        _devToolsFrameSubscription.reset(); // 개발 도구는 엔진 서비스를 빌려 쓴다 — 엔진보다 먼저 뗀다
        if ( _pDevTools != nullptr )
            _pDevTools->setEngine( nullptr );
#endif
        _engine.shutdown( _host );
        _pModuleImageHost = nullptr;
        _pDevTools        = nullptr;
        Logger::clearClockNumber();
        // Windows 창 닫기와 시스템 종료 처리기가 이것을 기다린다.
        ShutdownSignal::notifyShutdownComplete();
    }
} // namespace sw
