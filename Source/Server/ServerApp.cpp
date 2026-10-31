#include "pch.h"

#include "Server/ServerApp.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Console/DevCommandRegistry.h"
#include "Core/Diagnostics/CrashContext.h"
#include "Core/Diagnostics/CrashHandler.h"
#include "Core/File/FileUtil.h"
#include "Core/Foundation/BuildInfo.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"
#include "Core/Log/Sink/ConsoleLogOutput.h"
#include "Core/Memory/Memory.h"
#include "Core/Network/NetTypes.h"
#include "Core/Process/ShutdownSignal.h"
#include "Core/String/fixed_string.h"
#include "Core/Time/MonotonicClock.h"
#include "Core/Time/WallClock.h"

#include "Engine/Config/ConfigManager.h"
#include "Engine/Config/GameSelection.h"
#include "Engine/Config/Server/ServerConfig.h"
#include "Engine/DedicatedServer/ServerServiceManager.h"
#include "Engine/DedicatedServer/ServiceHealthRegistry.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Module/HostServices.h"
#include "Engine/Net/GameConnectSecurity.h"
#include "Engine/Net/GameNetSession.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Observability/MetricRegistry.h"
#include "Engine/Observability/ProcessMetrics.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Profiling/ProfilerTraceExport.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Scene/SceneStateHash.h"
#include "Engine/Telemetry/CrashReportService.h"

#include "ModuleManager/GameModuleManager.h"
#include "ModuleManager/ModuleLoaderSubsystem.h"

#include "sw/config/ConfigConstants.h"

namespace sw
{
    /** @brief `-gv_serverExitAfterTicks=N`: N 틱을 돈 뒤 스스로 정상 종료합니다(기동과 종료 테스트용). Shipping 에도 등록하며, 배포 서버 산출물을 같은 테스트가 실행합니다. */
    SW_TEST_GLOBAL_VARIABLE_SHIPPED( int32, gv_serverExitAfterTicks, 0, "이 틱 수를 돈 뒤 정상 종료 (0=끄기)" );
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
            /** @brief 이 종료 까닭은 접속이 비기를 기다린다. 오케스트레이터의 종료 신호만 — 사람이 내린 것(콘솔 · Ctrl+C) · 테스트 틱 상한 · OS 가 시한을 주는 창 닫기 · 시스템 종료는 바로 내린다. */
            static bool shouldDrainFor( ShutdownCause cause ) { return cause == ShutdownCause::Terminate || cause == ShutdownCause::ServiceStop; }

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
        , _pModuleImageHost{ nullptr }
        , _devTools{}
        , _engine{}
        , _gameModuleManager{ nullptr }
        , _moduleReload{}
        , _debugControl{ SW_DELEGATE_LAMBDA( ServerTickCountDelegate, [this]()
    { return _tickCount; } ) }
        , _moduleCatalog{}
        , _moduleResolution{}
        , _console{}
        , _observability{}
        , _serverServices{ nullptr }
        , _reloadStopsServices{}
        , _serverConfigPath{}
        , _serverServiceContext{}
        , _pServerConfig{ nullptr }
        , _drainDeadlineMs{ 0 }
        , _tickCount{ 0 }
        , _startNanoseconds{ 0 }
        , _tickNanoseconds{ 0 }
        , _statusTickSumNanoseconds{ 0 }
        , _statusTickMaxNanoseconds{ 0 }
        , _statusTickSampleCount{ 0 }
        , _droppedTickCount{ 0 }
        , _hostServices{}
        , _bReady{ SW_FALSE }
        , _bRestartServerServices{ SW_FALSE }
        , _bDraining{ SW_FALSE }
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
        // 개발 도구 — 엔진 기동 확장으로 걸어, 엔진이 기동 단계 범위를 채울 때 자기 서브시스템을 더한다(배포는 빈 구현).
        _devTools.addToHostScope( _host, _engine );
        _pModuleImageHost = ModuleLoaderSubsystem::initializeHostScope( _host, SW_DELEGATE_METHOD( ModuleTypeLoaderDelegate, &ServerApp::loadModuleLibrarys, this ), "ServerApp" );
        if ( _pModuleImageHost == nullptr )
            return false;
        if ( _engine.initialize( argc, pArgv, EngineHostRole::DedicatedServer, &_host ) == false )
        {
            SW_LOG_ERROR( "Engine initialization failed (dedicated server)" );
            return false;
        }
        // 엔진이 섰다 — 개발 도구의 프레임 훅을 붙인다(씬 틱 뒤 HUD · 콘솔, 프레임 끝에 셰이더 재컴파일 · 단축키 · 설정 리로드).
        _devTools.attachFrameHooks( _engine );
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

        _bReady = SW_TRUE;
        _observability.beginLoadWindow( ServerAppInternal::nowMonotonicMs() );
        // 여기부터 모든 줄에 `[tick=N]`이 붙는다. N 은 끝낸 틱 수이며, 지금 도는 틱의 0 기준 번호다. 멀티 프로세스 재생의 Output Log 가 서버 줄을 틱 기준으로 맞춰 본다.
        Logger::setClockNumber( LogClock::Tick, _tickCount );
        SW_LOG_INFO( "Dedicated server ready - target %#, %# Hz, game port %#, service port %#, startup %# ms", build::kTargetName, _pServerConfig->_tickRateHz,
                     _pServerConfig->_gamePort, _pServerConfig->_servicePort, ( MonotonicClock::nowNanoseconds() - _startNanoseconds ) / constant::kNanosecondsPerMillisecond );
        return true;
    }

    bool ServerApp::loadModuleLibrarys()
    {
        SW_MEMORY_SCOPE( EngineMisc );
        // 전용 서버는 Server 대상 모듈만 로드한다. Game 빌드에서도 에디터와 RHI 모듈을 로드하지 않는다. 리로드 구동기(App 과 같은 것)가 매니페스트를 해석하고
        // 매니저를 만들어 자신을 호스트 범위에 걸며, 매니저 종료를 "씬은 사라졌고 서비스는 아직 있는" 구간의 훅에 건다(배포는 정적 링크라 아무것도 하지 않는다).
        if ( _moduleReload.initialize( static_cast<uint8>( ModuleTarget::Server ), _host, *_pModuleImageHost, _moduleCatalog, _moduleResolution ) == false )
            return false;
        _gameModuleManager = make_unique<GameModuleManager>();
        if ( _gameModuleManager->loadModuleLibrarys( _moduleReload.getLiveReloadManager(), _moduleCatalog, _moduleResolution ) == false )
            return false;
        // 서버 서비스 코드가 리로드될 모듈 안에 있으므로 서비스를 먼저 멈춘다. 모듈 호스트보다 나중에 더하므로 먼저 불린다(배치 콜백은 나중 것이 먼저). 배포는 빈 구독이다.
        _reloadStopsServices = _moduleReload.addOnBeforeReloadBatch(
            SW_DELEGATE_METHOD( ModuleReloadDriver::BeforeReloadBatchDelegate, &ServerApp::onBeforeModuleReload, this ) );
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
                return _observability.initialize( *_pServerConfig, _startNanoseconds );
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
                _serverServiceContext._pMetricRegistry  = _observability.getMetricRegistry();
                _serverServiceContext._pHealthRegistry  = _observability.getHealthRegistry();
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
            {
                _debugControl.startConsole();
                return true;
            }
            case HostServiceID::DebugChannel:
            {
                if ( _debugControl.startChannel() == false )
                    return false;
                // 관리 채널은 Shipping 도 여는 길이다(기본 꺼짐, 켜면 토큰 필수). 상태는 서버 루프가 답한다.
                return _adminChannel.start( *_pServerConfig, *_observability.getHealthRegistry(), SW_DELEGATE_LAMBDA( ServerAdminStatusDelegate, [this]()
                { return ServerAdminStatus{ _tickCount, getServiceConnectionCount(), _bDraining == SW_TRUE }; } ) );
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
                _observability.shutdown();
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
            {
                _debugControl.stopConsole();
                return;
            }
            case HostServiceID::DebugChannel:
            {
                _debugControl.stopChannel();
                _adminChannel.stop();
                return;
            }
            case HostServiceID::BackendSwitcher:
            case HostServiceID::UserSettings:
            case HostServiceID::RuntimeUIHooks:
            {
                return;
            }
        }
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
        MetricGauge* pCrashGauge = _observability.getMetricRegistry()->registerGauge( "server_crash_reports", "Crash report bundles kept on this machine (previous runs that crashed)" );
        if ( pCrashGauge != nullptr )
            pCrashGauge->set( static_cast<float64>( listReport.size() ) );
        return true;
    }

    int32 ServerApp::getServiceConnectionCount() const
    {
        return _serverServices != nullptr ? _serverServices->getConnectionCount() : 0;
    }

    int32 ServerApp::countDrainBlockers() const
    {
        // 서비스 연결(로그인 · 채팅 …) + 게임 UDP 접속(복제 키트의 플레이어) + 키트가 끝내야 하는 진행 중인 일(매치 · 거래). 셋이 모두 0 이어야 조용히 내려갈 수 있다.
        const int32           serviceConnections = getServiceConnectionCount();
        const int32           openWork           = _serverServices != nullptr ? _serverServices->getOpenWorkCount() : 0;
        const GameNetSession* pNetSession        = engine::areEngineServicesBound() ? engine::findSubsystem<GameNetSession>() : nullptr;
        const int32           gameConnections    = pNetSession != nullptr && pNetSession->getRole() == GameNetRole::Server ? pNetSession->getConnectedCount() : 0;
        return serviceConnections + gameConnections + openWork;
    }

    void ServerApp::beginDrain( ShutdownCause cause, int64 monotonicMs )
    {
        _bDraining = SW_TRUE;
        _observability.getHealthRegistry()->setDraining( true ); // /readyz 는 503 을 반환한다. 부하 분산기가 새 접속을 끊는다(종료하는 동안에도 /healthz 는 살아 있다).
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
            _debugControl.pump(); // 일시정지 중에도 돈다 - 멈춘 상태의 값을 읽는 것이 쓰임이다
            _adminChannel.pump();

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
            _observability.updateEverySecond( ServerAppInternal::nowMonotonicMs(), getServiceConnectionCount() );

            if ( _debugControl.isHoldingTick() )
            {
                // 일시정지에서는 게임 틱만 멈춘다. 서버는 살아 있으므로 상태 확인 박동은 계속 보내(감시자가 재시작하지 않게 한다). 재개할 때 밀린 틱을 몰아서 돌지 않게 마감을 지금으로 바꾼다.
                _observability.markAlive( ServerAppInternal::nowMonotonicMs() );
                MonotonicClock::sleepUntilNanoseconds( MonotonicClock::nowNanoseconds() + _tickNanoseconds );
                deadline = MonotonicClock::nowNanoseconds();
                continue;
            }

            const int64 tickStart = MonotonicClock::nowNanoseconds();
            updateOnce( deltaSeconds );
            const int64 tickTime = MonotonicClock::nowNanoseconds() - tickStart;
#if SW_PROFILER_TRACE_ENABLED
            // 느린 틱 자동 캡처(`-gv_traceSlowTickMs`)는 꺼져 있으면 정수 비교 하나다. Shipping 서버에는 없다(지표 `server_tick_seconds` 만 있다).
            ProfilerTraceExport::updateSlowFrameCapture( engine::getSubsystem<FrameProfiler>(), static_cast<uint64>( tickStart ),
                                                         static_cast<uint64>( tickStart + tickTime ), "Server" );
#endif
            _statusTickSumNanoseconds += tickTime;
            _statusTickMaxNanoseconds = tickTime > _statusTickMaxNanoseconds ? tickTime : _statusTickMaxNanoseconds;
            ++_statusTickSampleCount;
            ++_tickCount;
            Logger::setClockNumber( LogClock::Tick, _tickCount );
            // 틱 지표 · 상태 박동 · 프레임 이력(O1) · 느린 틱 덤프 · 사용률 창(O5)은 관측이 든다.
            _observability.recordTick( tickTime, _tickCount, getServiceConnectionCount(), _droppedTickCount, ServerAppInternal::nowMonotonicMs() );
            // 이력은 `-gv_stateHashHistory=N` 이 켤 때만 남는다(배포에는 그 인자가 없어 늘 꺼져 있다).
            StateHashHistory* pStateHashHistory = engine::areEngineServicesBound() ? engine::findSubsystem<StateHashHistory>() : nullptr;
            if ( pStateHashHistory != nullptr && pStateHashHistory->isRecording() )
            {
                pStateHashHistory->recordActiveScene( _tickCount );
                // 클라이언트가 같은 틱 번호로 자기 해시를 남긴다(S7) — 이력을 남기는 서버만 보낸다.
                GameNetSession* pNetSession = engine::findSubsystem<GameNetSession>();
                if ( pNetSession != nullptr )
                    pNetSession->sendServerTick( _tickCount );
            }
            _debugControl.onTickFinished();
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
                _observability.addDroppedTicks( dropped );
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
        if ( _bRestartServerServices == SW_TRUE )
        {
            // 리로드가 끝났다(취소됐어도 서비스는 멈춰 있다). 새 모듈의 팩토리로 다시 시작한다.
            _bRestartServerServices = SW_FALSE;
            if ( startServerServices() == false )
                SW_LOG_ERROR( "Server services could not be restarted after the module reload - fix the error and reload again" );
        }
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
            SW_LOG_INFO( "Server commands: quit | stop | status | help%#", ServerDebugControl::getHelpSuffix() );
            return;
        }
        // Dev 는 일시정지 명령과 개발 콘솔의 명령(개발 명령과 gv_* 전역 변수)도 받는다. Shipping 서버는 위의 네 가지만 받는다.
        if ( _debugControl.executeCommand( command ) )
            return;
        SW_LOG_WARNING( "Unknown server command '%#' - type help", string( command ).c_str() );
    }

    void ServerApp::logStatus() const
    {
        const int64 uptimeSeconds   = ( MonotonicClock::nowNanoseconds() - _startNanoseconds ) / constant::kNanosecondsPerSecond;
        const int64 averageMicro    = _statusTickSampleCount > 0 ? _statusTickSumNanoseconds / _statusTickSampleCount / constant::kNanosecondsPerMicrosecond : 0;
        const int32 serviceCount    = _serverServices != nullptr ? _serverServices->getRunningCount() : 0;
        const int32 connectionCount = getServiceConnectionCount();
        uint64      residentBytes   = 0;
        uint64      peakBytes       = 0;
        (void)ProcessMetrics::tryGetResidentBytes( residentBytes, peakBytes ); // 답하지 못하면 0 을 적는다
        SW_LOG_INFO( "Dedicated server status - uptime %# s, tick %#%#, tick time avg %# us / max %# us (budget %# us), dropped ticks %#, services %# (connections %#), "
                     "resident %# MiB",
                     uptimeSeconds, _tickCount, _debugControl.isPaused() ? " (paused)" : "", averageMicro, _statusTickMaxNanoseconds / constant::kNanosecondsPerMicrosecond, _tickNanoseconds / constant::kNanosecondsPerMicrosecond,
                     _droppedTickCount, serviceCount, connectionCount, residentBytes / static_cast<uint64>( constant::kBytesPerMegabyte ) );
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
        _devTools.detach(); // 개발 도구는 엔진 서비스를 빌려 쓴다 — 엔진보다 먼저 뗀다
        _engine.shutdown( _host );
        _pModuleImageHost = nullptr;
        _devTools.release();
        Logger::clearClockNumber();
        // Windows 창 닫기와 시스템 종료 처리기가 이것을 기다린다.
        ShutdownSignal::notifyShutdownComplete();
    }
} // namespace sw
