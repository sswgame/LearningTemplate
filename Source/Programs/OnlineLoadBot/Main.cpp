/**
 * @file Main.cpp
 * @brief 온라인 부하 테스트 봇 — `OnlineLoadBot --scenario=<json> --load=<.scenario.xml> (--server=<ip:port> [--tls=<신뢰 PEM>] | --local-server=<port> --online-config=<json>)
 *        [--game=<ip:port>] [--report=<json>] [--max-error-percent=N] [--light-hash]`.
 * @details - 인자는 `CommandLineManager` 가 읽는다(경로는 기동 폴더 기준으로 한 번 절대화, 틀린 인자는 모두 모아 종료 코드 2).
 *          - 부하 값(봇 수 · 늘리는 시간 · 길이)은 `--load` 의 Automation `<Load>` 한 곳에서 온다 — 시나리오 JSON 은 단계만 든다.
 *          - `--local-server` 는 같은 프로세스에 서버 조립(`LoadBotLocalServer` — 전용 서버와 같은 `OnlineServerRuntime`)을 띄우고 루프백 TCP(플랫폼 전송, 평문)로 붙는다.
 *            키트 목록 · 옵션은 `--online-config`(`LoadBot.online.json`)를 전용 서버와 같은 로더로 읽는다. 포트 0 이면 OS 가 고른다. 서버 시각은 UTC 벽시계, 봇 지연은 `MonotonicClock`.
 *          - `--server` 는 전용 서버(`Server` 의 온라인 서비스)에 붙는다. 그 서비스 포트는 TLS 1.3 이므로 `--tls=<신뢰 PEM>` 을 함께 준다(값이 없으면 오류 - 개발용 인증서로 대신하지 않는다).
 *          - 끝에 동작별 지연 백분위 표를 로그로, `--report` 면 JSON 을 파일로 쓴다. 오류율이 상한(기본 1 %)을 넘으면 종료 코드 1, 인자 · 시나리오 오류는 2, 엔진 기동 실패는 3,
 *            보고서를 쓰지 못하면 4.
 *          - 프로세스 바닥은 전용 서버(`ServerApp`)와 같은 모양이다 — 호스트 범위(모듈 라이브러리 창구) 위에 `Engine`(`DedicatedServer`)을 세운다. 서버 키트가 쓰는 엔진 서비스와
 *            타입 레지스트리(설정의 범위 검사 · 리플렉션)가 이 엔진에 있다. 없이 돌리면 키트 `initialize` 가 "could not initialize" 로 멈춘다.
 */
#include "pch.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Module/ModuleLibraryUtil.h"
#include "Core/Network/NetTypes.h"
#include "Core/Network/Security/INetSecurityProvider.h"
#include "Core/Network/Transport/IStreamTransport.h"
#include "Core/Path/AbsolutePath.h"
#include "Core/String/hashed_string.h"
#include "Core/Subsystem/SubsystemScope.h"
#include "Core/Time/MonotonicClock.h"
#include "Core/Time/WallClock.h"

#include "Engine/Automation/Scenario/AutomationScenario.h"
#include "Engine/Config/ConfigManager.h"
#include "Engine/Engine.h"
#include "Engine/Module/ModuleTypeRegistry.h"

#include "GameFramework/Base/Foundation/Framework/GameService.h"
#include "GameFramework/Base/Online/Security/NetSecurity.h"

#include "ModuleManager/ModuleLoaderSubsystem.h"

#include "Programs/OnlineLoadBot/LoadBotLocalServer.h"
#include "Programs/OnlineLoadBot/LoadBotRunner.h"
#include "Programs/OnlineLoadBot/LoadBotScenario.h"

SW_LOG_CALLER( "OnlineLoadBot" );
namespace sw
{
    namespace
    {
        /**
         * @brief 프로세스 바닥 — 전용 서버(`ServerApp`)와 같은 모양의 호스트 범위와 `Engine`(`DedicatedServer`)을 세우고 역순으로 내립니다.
         * @details 호스트 범위(모듈 라이브러리 창구)가 엔진 범위의 부모이고, 엔진이 부트스트랩(이름 풀 · 로거 · 크래시 핸들러) · 서비스 · 기동 단계를 세운다. 서버 키트는 엔진 서비스와
         *          타입 레지스트리를 쓰므로 키트 `initialize` 전에 엔진이 서 있어야 한다. 엔진 기동 단계 `ModuleTypes` 가 창구의 로더를 불러 서버 키트 모듈을 올리고 그 타입을 등록한다.
         *          main 의 맨 처음에 두어 hashed_string 을 든 객체가 모두 사라진 뒤에 이름 풀이 내려가게 한다(로거는 비동기라 내리지 않으면 마지막 줄을 잃는다).
         *          게임 모듈 · 서버 설정 · 서버 서비스는 세우지 않는다 — 봇은 서버 조립(`LoadBotLocalServer`)을 스스로 연다.
         */
        class ToolHostScope
        {
        public:
            ToolHostScope()
                : _host{}
                , _engine{}
                , _bEngineStarted{ SW_FALSE }
                , _bLoadServerKit{ SW_FALSE }
            {
            }

            ~ToolHostScope()
            {
                // `Engine::shutdown` 은 일부만 서도 된다. 엔진 범위가 사라진 뒤 부트스트랩(종료 누수 보고)보다 먼저 호스트 범위를 내린다.
                if ( _bEngineStarted == SW_TRUE )
                    _engine.shutdown( _host );
                else
                    _host.destroyAll();
            }

            ToolHostScope( const ToolHostScope& )            = delete;
            ToolHostScope& operator=( const ToolHostScope& ) = delete;

            /**
             * @brief 호스트 범위와 엔진을 세웁니다. @p bLoadServerKit 이면 엔진의 `ModuleTypes` 단계에서 서버 키트 모듈을 올립니다(`--local-server`).
             * @details 엔진에는 프로그램 이름만 넘긴다 — 봇 인자(`--scenario=` …)는 봇이 읽고, 엔진의 표준 인자(`-scenario` 자동화 시나리오 …)로 풀리지 않게 한다.
             *          로거가 서기 전의 실패는 표준 오류로 알린다.
             * @return 서지 못하면 false 입니다(폴백 없음 — 호출자가 멈춘다).
             */
            [[nodiscard]] bool initialize( utf8* pProgramName, bool bLoadServerKit )
            {
                _bLoadServerKit = bLoadServerKit ? SW_TRUE : SW_FALSE;
                if ( ModuleLoaderSubsystem::initializeHostScope( _host, SW_DELEGATE_METHOD( ModuleTypeLoaderDelegate, &ToolHostScope::loadServerKitModules, this ), "OnlineLoadBot" ) == nullptr )
                    return false;
                utf8* arrProgramArgument[] = { pProgramName, nullptr };
                _bEngineStarted            = SW_TRUE;
                if ( _engine.initialize( 1, arrProgramArgument, EngineHostRole::DedicatedServer, &_host ) == false )
                {
                    std::fprintf( stderr, "OnlineLoadBot: the engine did not start (dedicated server role)\n" );
                    return false;
                }
                return true;
            }

            /** @brief 엔진의 설정 표입니다(`initialize` 뒤에만). 로컬 서버의 온라인 설정을 전용 서버와 같은 로더로 읽는다. */
            ConfigManager* getConfigManager() const { return _engine.getConfigManager(); }

        private:
            /** @brief 서버 조립(`--local-server`)이 쓰는 서버 키트 모듈 — 실행 파일이 그 심볼을 쓰지 않아 링커가 import 를 버리므로 이름으로 올린다. */
            static constexpr const utf8* kArrServerKitModule[] = { "GF_Server_Account", "GF_Server_Chat", "GF_Server_Leaderboard", "GF_Server_LiveOps",
                                                                   "GF_Server_Matchmaking", "GF_Server_ServerDirectory", "GF_Server_Social" };

            /**
             * @brief 서버 키트 모듈을 올려 정적 등록자가 서버 조립 레지스트리에 오르게 하고, 그 모듈의 리플렉션 타입을 타입 레지스트리에 올립니다.
             * @details 하나라도 못 올리면 false 입니다(기동 멈춤). 프로세스가 끝날 때까지 내리지 않는다. 모듈을 올릴 때마다 바로 등록한다 — 전역 등록자 머리가 그 모듈 것만 남게 한다.
             */
            [[nodiscard]] bool loadServerKitModules()
            {
                if ( _bLoadServerKit == SW_FALSE )
                    return true;
                for ( const utf8* pModule : kArrServerKitModule )
                {
                    const std::optional<AbsolutePath> libraryPath = AbsolutePath::tryMake( ModuleLibraryUtil::findModuleLibraryPath( pModule ) );
                    if ( libraryPath.has_value() == false || ModuleLibraryUtil::loadLibraryAt( *libraryPath ) == nullptr )
                    {
                        SW_LOG_ERROR( "OnlineLoadBot: cannot load the server kit module %#", pModule );
                        return false;
                    }
                    engine::registerModuleTypes( pModule );
                }
                return true;
            }

            SubsystemScope _host;
            Engine         _engine;
            uint8          _bEngineStarted;
            uint8          _bLoadServerKit;
        };

        /** @brief 명령줄 인자입니다(`LoadBotMainInternal::readOptions` 가 `CommandLineManager` 로 읽는다). 경로는 모두 기동 때 절대화된 것이다. */
        struct LoadBotOptions
        {
            string _scenarioPath{};     ///< `--scenario=<json>` — 단계 나무(필수)
            string _loadPath{};         ///< `--load=<.scenario.xml>` — 봇 수 · 늘리는 시간 · 길이의 정본 `<Load>`(필수)
            string _onlineConfigPath{}; ///< `--online-config=<LoadBot.online.json>` — 로컬 서버의 키트 목록 · 옵션(`--local-server` 와 필수)
            string _reportPath{};
            string _serverAddress{};
            string _gameAddress{};  ///< `--game=<ip:port>` — 전용 서버의 게임 UDP 포트. 시나리오의 `game_connect` 가 붙는다(서버 디버깅 S5)
            string _tlsTrustFile{}; ///< `--tls=<PEM>` — 신뢰할 인증서(필수 값 - 비우면 개발용 인증서로 대신하지 않는다)
            int32  _maxErrorPercent{ 1 };
            int32  _localServerPort{ -1 }; ///< `--local-server=<port>` — 0 이면 OS 가 고른다. 음수면 로컬 서버를 띄우지 않는다
            uint8  _bLightHash{ SW_FALSE };
        };

        struct LoadBotMainInternal
        {
            static constexpr int32   kExitOk                = 0;
            static constexpr int32   kExitErrorRate         = 1;
            static constexpr int32   kExitUsage             = 2;
            static constexpr int32   kExitStartup           = 3;
            static constexpr int32   kExitReport            = 4; ///< 보고서(`--report`)를 쓰지 못했다 — 결과를 잃은 실행은 성공이 아니다
            static constexpr uint16  kDefaultServerPort     = OnlineServiceHostConfig::kDefaultPort;
            static constexpr int32   kMaxPort               = 65535;
            static constexpr int64   kIdleSleepNs           = 200000; ///< 틱 사이 — 전송을 I/O 스레드 없이 돌리므로 짧게
            static constexpr int32   kPercentScale          = 100;
            static constexpr int32   kSpareConnectionCount  = 16;
            static constexpr float32 kMillisecondsPerSecond = 1000.0f;

            /**
             * @brief 봇 인자를 `CommandLineManager` 로 읽습니다(엔진 표준 인자는 등록하지 않는다 — 봇 인자만 받는다).
             * @details 경로 인자는 `CommandLineDiskPath` 라 받을 때 기동 폴더 기준으로 한 번 절대화되고 비면 거절된다. 값 없는 `--tls` 도 그래서 거절된다
             *          (예전에는 값 없는 `--tls` 가 개발용 인증서를 신뢰했다). 틀린 인자는 모두 모아 @p outError 로 돌려준다.
             */
            [[nodiscard]] static bool readOptions( int32 argc, utf8* argv[], LoadBotOptions& outOptions, string& outError )
            {
                const std::optional<AbsolutePath> baseDirectory = AbsolutePath::tryMake( FileUtil::getCurrentPath() );
                if ( baseDirectory.has_value() == false )
                {
                    outError = "cannot read the startup directory - path arguments cannot be resolved";
                    return false;
                }
                CommandLineManager commandLine;
                commandLine.addArgument( { "scenario" }, CommandLineDiskPath{}, false );
                commandLine.addArgument( { "load" }, CommandLineDiskPath{}, false );
                commandLine.addArgument( { "online-config" }, CommandLineDiskPath{}, false );
                commandLine.addArgument( { "report" }, CommandLineDiskPath{}, false );
                commandLine.addArgument( { "tls" }, CommandLineDiskPath{}, false );
                commandLine.addArgument( { "server" }, string{}, false );
                commandLine.addArgument( { "game" }, string{}, false );
                commandLine.addArgument( { "local-server" }, int32{ 0 }, false );
                commandLine.addArgument( { "max-error-percent" }, int32{ 1 }, true );
                commandLine.addArgument( { "light-hash" }, false, true );
                commandLine.parse( argc, argv, *baseDirectory );
                if ( commandLine.getParseErrors().empty() == false )
                {
                    for ( const string& error : commandLine.getParseErrors() )
                    {
                        outError += error + "\n";
                    }
                    outError += "usage:\n" + commandLine.makeUsageText();
                    return false;
                }

                (void)commandLine.getArgument( "scenario", outOptions._scenarioPath );
                (void)commandLine.getArgument( "load", outOptions._loadPath );
                (void)commandLine.getArgument( "online-config", outOptions._onlineConfigPath );
                (void)commandLine.getArgument( "report", outOptions._reportPath );
                (void)commandLine.getArgument( "tls", outOptions._tlsTrustFile );
                (void)commandLine.getArgument( "server", outOptions._serverAddress );
                (void)commandLine.getArgument( "game", outOptions._gameAddress );
                (void)commandLine.getArgument( "max-error-percent", outOptions._maxErrorPercent );
                bool bLightHash = false;
                (void)commandLine.getArgument( "light-hash", bLightHash );
                outOptions._bLightHash = bLightHash ? SW_TRUE : SW_FALSE;
                if ( commandLine.isArgumentProvided( "local-server" ) )
                    (void)commandLine.getArgument( "local-server", outOptions._localServerPort );

                const bool bLocalServer          = outOptions._localServerPort >= 0;
                const bool bNoTarget             = outOptions._serverAddress.empty() && bLocalServer == false;
                const bool bTwoTarget            = outOptions._serverAddress.empty() == false && bLocalServer;
                const bool bInvalidTargetOptions = outOptions._scenarioPath.empty() || outOptions._loadPath.empty() || bNoTarget || bTwoTarget;
                if ( bInvalidTargetOptions )
                {
                    outError = "need --scenario=<json>, --load=<.scenario.xml> and exactly one of --server=<ip:port> or --local-server=<port>";
                    return false;
                }
                if ( outOptions._localServerPort > kMaxPort || outOptions._maxErrorPercent < 0 )
                {
                    outError = "--local-server port must be 0..65535 (0 = any free port) and --max-error-percent not negative";
                    return false;
                }
                if ( bLocalServer && outOptions._onlineConfigPath.empty() )
                {
                    outError = "--local-server needs --online-config=<LoadBot.online.json> (the kits and options the local server runs)";
                    return false;
                }
                if ( bLocalServer && outOptions._tlsTrustFile.empty() == false )
                {
                    outError = "--tls goes with --server (the local server listens in plaintext)";
                    return false;
                }
                return true;
            }

            /**
             * @brief 부하 선언(Automation `<Load clients rampUpSeconds durationSeconds>`)을 읽어 시나리오에 넣습니다 — 부하 값의 정본은 이것 하나다.
             * @return 파일을 못 읽거나 `<Load>` 가 없거나 봇 수가 0 이면 false 와 @p outError 입니다.
             */
            [[nodiscard]] static bool readLoad( const string& loadPath, LoadBotScenario& inoutScenario, string& outError )
            {
                string text;
                if ( FileUtil::readTextFile( loadPath, text ) == false )
                {
                    outError = "cannot read the load declaration '" + loadPath + "'";
                    return false;
                }
                AutomationScenario declaration;
                if ( declaration.parse( text, loadPath, outError ) == false )
                    return false;
                const AutomationLoad& load = declaration.getTestOptions()._load;
                if ( load._bDeclared == false || load._clientCount == 0 )
                {
                    outError = loadPath + ": needs <Load clients=\"N\" rampUpSeconds=\"S\" durationSeconds=\"S\"/> with clients > 0";
                    return false;
                }
                inoutScenario._botCount   = static_cast<int32>( load._clientCount );
                inoutScenario._rampUpMs   = static_cast<int64>( load._rampUpSeconds * kMillisecondsPerSecond );
                inoutScenario._durationMs = static_cast<int64>( load._durationSeconds * kMillisecondsPerSecond );
                return true;
            }

            static void logLines( const string& text )
            {
                size_t lineStart = 0;
                while ( lineStart < text.size() )
                {
                    size_t lineEnd = text.find( '\n', lineStart );
                    if ( lineEnd == string::npos )
                        lineEnd = text.size();
                    SW_LOG_INFO( "%#", text.substr( lineStart, lineEnd - lineStart ) );
                    lineStart = lineEnd + 1;
                }
            }
        };
    } // namespace
} // namespace sw

int32 main( int32 argc, utf8* argv[] )
{
    using namespace sw;
    using Internal = LoadBotMainInternal;
    ToolHostScope hostScope;

    LoadBotOptions options;
    string         error;
    if ( Internal::readOptions( argc, argv, options, error ) == false )
    {
        // 엔진(로거)을 세우기 전이다 — 표준 오류로 알린다. 엔진이 키트 모듈을 올릴지(`--local-server`)를 인자가 정하므로 인자를 먼저 읽는다.
        std::fprintf( stderr, "OnlineLoadBot: %s\n", error.c_str() );
        return Internal::kExitUsage;
    }
    const bool bLocalServer = options._localServerPort >= 0;
    if ( hostScope.initialize( argv[0], bLocalServer ) == false )
        return Internal::kExitStartup;

    LoadBotScenario scenario;
    if ( LoadBotScenario::loadFile( options._scenarioPath, scenario, error ) == false || Internal::readLoad( options._loadPath, scenario, error ) == false )
    {
        SW_LOG_ERROR( "OnlineLoadBot: %#", error );
        return Internal::kExitUsage;
    }

    LoadBotRunnerSettings        runnerSettings;
    unique_ptr<IStreamTransport> serverTransport;
    SystemMonotonicClock         serverClock; // 로컬 서버가 빌린다(메모리 캐시 만료). 서버보다 먼저 선언한다
    LoadBotLocalServer           localServer;
    if ( bLocalServer )
    {
        LoadBotLocalServerSettings serverSettings;
        // 전용 서버의 `<이름>.online.json` 과 같은 로더로 읽는다(모르는 키 · 범위 밖 값은 오류).
        const ConfigReadResult configRead = hostScope.getConfigManager()->readConfigFileInto( options._onlineConfigPath, serverSettings._config );
        if ( configRead != ConfigReadResult::Loaded )
        {
            SW_LOG_ERROR( "OnlineLoadBot: online config '%#' is %#", options._onlineConfigPath, configRead == ConfigReadResult::Missing ? "missing" : "invalid" );
            return Internal::kExitUsage;
        }
        serverTransport                    = StreamTransportFactory::createPlatformTransport();
        serverSettings._port               = static_cast<uint16>( options._localServerPort );
        serverSettings._region             = scenario._region;
        serverSettings._maxConnections     = scenario._botCount + Internal::kSpareConnectionCount;
        serverSettings._bLightPasswordHash = options._bLightHash;
        serverSettings._pClock             = &serverClock;
        if ( serverTransport == nullptr || localServer.initialize( serverTransport.get(), serverSettings, error ) == false )
        {
            SW_LOG_ERROR( "OnlineLoadBot: local server did not start: %#", error );
            return Internal::kExitUsage;
        }
        // 포트 0 이면 OS 가 고른 포트다 — 받은 포트로 붙는다.
        runnerSettings._serverAddress = NetAddress::makeLoopback( localServer.getListenPort() );
    }
    else if ( NetAddress::parse( options._serverAddress, Internal::kDefaultServerPort, runnerSettings._serverAddress ) == false )
    {
        SW_LOG_ERROR( "OnlineLoadBot: cannot read server address '%#'", options._serverAddress );
        return Internal::kExitUsage;
    }

    if ( options._gameAddress.empty() == false && NetAddress::parse( options._gameAddress, 0, runnerSettings._gameAddress ) == false )
    {
        SW_LOG_ERROR( "OnlineLoadBot: cannot read game address '%#' (--game=<ip:port>)", options._gameAddress );
        return Internal::kExitUsage;
    }

    unique_ptr<ITLSContext> tlsContext;
    if ( options._tlsTrustFile.empty() == false )
    {
        tlsContext = NetSecurity::createClientTLSContext( game::getSubsystem<INetSecurityProvider>(), options._tlsTrustFile, "", error );
        if ( tlsContext == nullptr )
        {
            SW_LOG_ERROR( "OnlineLoadBot: TLS client context failed: %#", error );
            return Internal::kExitUsage;
        }
        runnerSettings._pTLSContext = tlsContext.get();
    }

    unique_ptr<IStreamTransport> botTransport = StreamTransportFactory::createPlatformTransport();
    LoadBotRunner                runner;
    if ( botTransport == nullptr || runner.initialize( botTransport.get(), scenario, runnerSettings, error ) == false )
    {
        SW_LOG_ERROR( "OnlineLoadBot: %#", error );
        return Internal::kExitUsage;
    }
    SW_LOG_INFO( "OnlineLoadBot: scenario '%#' with %# bots against %#", scenario._name, scenario._botCount, runnerSettings._serverAddress.toString() );

    bool bRunning = true;
    while ( bRunning )
    {
        localServer.update( WallClock::nowUnixMilliseconds() );
        bRunning = runner.update();
        MonotonicClock::sleepUntilNanoseconds( MonotonicClock::nowNanoseconds() + Internal::kIdleSleepNs );
    }

    const LoadBotMetrics& metrics   = runner.getMetrics();
    const int64           elapsedMs = runner.getElapsedMs();
    Internal::logLines( metrics.formatTable( elapsedMs ) );
    const bool bReportFailed = options._reportPath.empty() == false && FileUtil::writeTextFile( options._reportPath, metrics.formatJSON( runner.getScenario(), elapsedMs ) ) == false;
    if ( bReportFailed )
        SW_LOG_ERROR( "OnlineLoadBot: cannot write report '%#'", options._reportPath );
    runner.shutdown();
    localServer.shutdown();

    if ( bReportFailed )
        return Internal::kExitReport;
    const int64 completedCount = metrics.getTotalCompletedCount();
    const int64 errorCount     = metrics.getTotalErrorCount();
    const bool  bTooManyErrors = completedCount == 0 || errorCount * Internal::kPercentScale > completedCount * options._maxErrorPercent;
    return bTooManyErrors ? Internal::kExitErrorRate : Internal::kExitOk;
}
