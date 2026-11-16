/**
 * @file LoadBotMain.cpp
 * @brief 온라인 부하 테스트 봇의 진입점(모듈 `OnlineLoadBotCore` 가 내보낸다) — `OnlineLoadBot --scenario=<json> --load=<.scenario.xml> (--server=<ip:port> [--tls=<신뢰 PEM>] | --local-server=<port> --online-config=<json>)
 *        [--game=<ip:port>] [--report=<json>] [--max-error-percent=N] [--light-hash]`.
 * @details - 실행 파일(`Main.cpp`)이 엔진을 세우고 이 모듈을 올린 뒤 부른다(`LoadBotModuleEntry.h`). 로그는 엔진 로거(콘솔 · 파일)로 간다.
 *          - 인자는 `CommandLineManager` 가 읽는다(경로는 기동 폴더 기준으로 한 번 절대화, 틀린 인자는 모두 모아 종료 코드 2).
 *          - 부하 값(봇 수 · 늘리는 시간 · 길이)은 `--load` 의 Automation `<Load>` 한 곳에서 온다 — 시나리오 JSON 은 단계만 든다.
 *          - `--local-server` 는 같은 프로세스에 서버 조립(`LoadBotLocalServer` — 전용 서버와 같은 `OnlineServerRuntime`)을 띄우고 루프백 TCP(플랫폼 전송, 평문)로 붙는다.
 *            키트 목록 · 옵션은 `--online-config`(`LoadBot.online.json`)를 전용 서버와 같은 로더로 읽는다. 포트 0 이면 OS 가 고른다. 서버 시각은 UTC 벽시계, 봇 지연은 `MonotonicClock`.
 *          - `--server` 는 전용 서버(`Server` 의 온라인 서비스)에 붙는다. 그 서비스 포트는 TLS 1.3 이므로 `--tls=<신뢰 PEM>` 을 함께 준다(값이 없으면 오류 - 개발용 인증서로 대신하지 않는다).
 *          - 끝에 동작별 지연 백분위 표를 로그로, `--report` 면 JSON 을 파일로 쓴다. 오류율이 상한(기본 1 %)을 넘으면 종료 코드 1, 인자 · 시나리오 오류는 2,
 *            보고서를 쓰지 못하면 4(`LoadBotModuleEntry`).
 */
#include "pch.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Network/NetTypes.h"
#include "Core/Network/Security/INetSecurityProvider.h"
#include "Core/Network/Transport/IStreamTransport.h"
#include "Core/Path/AbsolutePath.h"
#include "Core/Time/MonotonicClock.h"
#include "Core/Time/WallClock.h"

#include "Engine/Automation/Scenario/AutomationScenario.h"
#include "Engine/Config/ConfigManager.h"

#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Online/Security/NetSecurity.h"
#include "GameFramework/Runtime/Online/Service/OnlineProtocol.h"

#include "Programs/OnlineLoadBot/LoadBotLocalServer.h"
#include "Programs/OnlineLoadBot/LoadBotModuleEntry.h"
#include "Programs/OnlineLoadBot/LoadBotRunner.h"
#include "Programs/OnlineLoadBot/LoadBotScenario.h"

namespace sw
{
    namespace
    {
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
            static constexpr uint16 kDefaultServerPort    = OnlineProtocolConstant::kDefaultPort;
            static constexpr int64  kIdleSleepNs          = 200000; ///< 틱 사이 — 전송을 I/O 스레드 없이 돌리므로 짧게
            static constexpr int32  kPercentScale         = 100;
            static constexpr int32  kSpareConnectionCount = 16;

            /**
             * @brief 봇 인자를 `CommandLineManager` 로 읽습니다(엔진 표준 인자는 등록하지 않는다 — 봇 인자만 받는다).
             * @details 경로 인자는 `CommandLineDiskPath` 라 받을 때 기동 폴더 기준으로 한 번 절대화되고 비면 거절된다. 값 없는 `--tls` 도 그래서 거절된다
             *          (예전에는 값 없는 `--tls` 가 개발용 인증서를 신뢰했다). 틀린 인자는 모두 모아 여러 줄 글(쓰는 법 포함)로 돌려준다 — 길이가 정해지지 않아 오류 형식이 글이다.
             */
            [[nodiscard]] static expected<LoadBotOptions, string> readOptions( int32 argc, utf8* argv[] )
            {
                const optional<AbsolutePath> baseDirectory = AbsolutePath::tryMake( FileUtil::getCurrentPath() );
                if ( baseDirectory.has_value() == false )
                    return unexpected( string( "cannot read the startup directory - path arguments cannot be resolved" ) );
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
                    string usage;
                    for ( const string& error : commandLine.getParseErrors() )
                    {
                        usage += error + "\n";
                    }
                    usage += "usage:\n" + commandLine.makeUsageText();
                    return unexpected( std::move( usage ) );
                }

                LoadBotOptions options;
                (void)commandLine.getArgument( "scenario", options._scenarioPath );
                (void)commandLine.getArgument( "load", options._loadPath );
                (void)commandLine.getArgument( "online-config", options._onlineConfigPath );
                (void)commandLine.getArgument( "report", options._reportPath );
                (void)commandLine.getArgument( "tls", options._tlsTrustFile );
                (void)commandLine.getArgument( "server", options._serverAddress );
                (void)commandLine.getArgument( "game", options._gameAddress );
                (void)commandLine.getArgument( "max-error-percent", options._maxErrorPercent );
                bool bLightHash = false;
                (void)commandLine.getArgument( "light-hash", bLightHash );
                options._bLightHash = bLightHash ? SW_TRUE : SW_FALSE;
                if ( commandLine.isArgumentProvided( "local-server" ) )
                    (void)commandLine.getArgument( "local-server", options._localServerPort );

                const bool bLocalServer          = options._localServerPort >= 0;
                const bool bNoTarget             = options._serverAddress.empty() && bLocalServer == false;
                const bool bTwoTarget            = options._serverAddress.empty() == false && bLocalServer;
                const bool bInvalidTargetOptions = options._scenarioPath.empty() || options._loadPath.empty() || bNoTarget || bTwoTarget;
                if ( bInvalidTargetOptions )
                    return unexpected( string( "need --scenario=<json>, --load=<.scenario.xml> and exactly one of --server=<ip:port> or --local-server=<port>" ) );
                if ( options._localServerPort > constant::network::kMaxPort || options._maxErrorPercent < 0 )
                    return unexpected( string( "--local-server port must be 0..65535 (0 = any free port) and --max-error-percent not negative" ) );
                if ( bLocalServer && options._onlineConfigPath.empty() )
                    return unexpected( string( "--local-server needs --online-config=<LoadBot.online.json> (the kits and options the local server runs)" ) );
                if ( bLocalServer && options._tlsTrustFile.empty() == false )
                    return unexpected( string( "--tls goes with --server (the local server listens in plaintext)" ) );
                return options;
            }

            /**
             * @brief 부하 선언(Automation `<Load clients rampUpSeconds durationSeconds>`)을 읽어 시나리오에 넣습니다 — 부하 값의 정본은 이것 하나다.
             * @return 파일을 못 읽거나 `<Load>` 가 없거나 봇 수가 0 이면 그 이유입니다(대상은 경로 · 시나리오 읽기 글).
             */
            [[nodiscard]] static expected<void> readLoad( const string& loadPath, LoadBotScenario& inoutScenario )
            {
                string text;
                if ( FileUtil::readTextFile( loadPath, text ) == false )
                    return unexpected( Error{ ErrorKind::NotFound, "cannot read the load declaration", loadPath } );
                AutomationScenario declaration;
                if ( const expected<void> parsed = declaration.parse( text, loadPath ); parsed.has_value() == false )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "the load declaration does not parse", parsed.error().message() }.appendDetail( parsed.error().detail() ) );
                const AutomationLoad& load = declaration.getTestOptions()._load;
                if ( load._bDeclared == false || load._clientCount == 0 )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "needs <Load clients=\"N\" rampUpSeconds=\"S\" durationSeconds=\"S\"/> with clients > 0", loadPath } );
                inoutScenario._botCount   = static_cast<int32>( load._clientCount );
                inoutScenario._rampUpMs   = static_cast<int64>( load._rampUpSeconds * constant::time::kMillisecondsPerSecond );
                inoutScenario._durationMs = static_cast<int64>( load._durationSeconds * constant::time::kMillisecondsPerSecond );
                return {};
            }

            static void logLines( LogLevel level, const string& text )
            {
                size_t lineStart = 0;
                while ( lineStart < text.size() )
                {
                    size_t lineEnd = text.find( '\n', lineStart );
                    if ( lineEnd == string::npos )
                        lineEnd = text.size();
                    if ( level == LogLevel::Error )
                        SW_LOG_ERROR_ID( "OnlineLoadBot.OnlineLoadBot", "OnlineLoadBot: %#", text.substr( lineStart, lineEnd - lineStart ) );
                    else
                        SW_LOG_INFO_ID( "OnlineLoadBot.Output", "%#", text.substr( lineStart, lineEnd - lineStart ) );
                    lineStart = lineEnd + 1;
                }
            }

            /** @brief 인자를 읽고 시나리오 · 서버 · 봇을 세워 끝까지 돌립니다. 종료 코드는 `LoadBotModuleEntry` 의 것입니다. */
            [[nodiscard]] static int32 run( int32 argc, utf8* argv[] )
            {
                using Exit                                               = LoadBotModuleEntry;
                const expected<LoadBotOptions, string> readOptionsResult = readOptions( argc, argv );
                if ( readOptionsResult.has_value() == false )
                {
                    logLines( LogLevel::Error, readOptionsResult.error() );
                    return Exit::kExitUsage;
                }
                const LoadBotOptions& options      = *readOptionsResult;
                const bool            bLocalServer = options._localServerPort >= 0;

                expected<LoadBotScenario> loadedScenario = LoadBotScenario::loadFromFile( options._scenarioPath );
                const expected<void>      loaded         = loadedScenario.has_value() ? readLoad( options._loadPath, *loadedScenario ) : expected<void>( unexpected( loadedScenario.error() ) );
                if ( loaded.has_value() == false )
                {
                    SW_LOG_ERROR_ID( "OnlineLoadBot.OnlineLoadBot", "OnlineLoadBot: %#: %#", loaded.error().message(), loaded.error().detail() );
                    return Exit::kExitUsage;
                }
                const LoadBotScenario& scenario = *loadedScenario;

                LoadBotRunnerSettings        runnerSettings;
                unique_ptr<IStreamTransport> serverTransport;
                SystemMonotonicClock         serverClock; // 로컬 서버가 빌린다(메모리 캐시 만료). 서버보다 먼저 선언한다
                LoadBotLocalServer           localServer;
                if ( bLocalServer )
                {
                    LoadBotLocalServerSettings serverSettings;
                    // 전용 서버의 `<이름>.online.json` 과 같은 로더로 읽는다(모르는 키 · 범위 밖 값은 오류).
                    const ConfigReadResult configRead = ConfigManager::readConfigFileInto( options._onlineConfigPath, serverSettings._config );
                    if ( configRead != ConfigReadResult::Loaded )
                    {
                        SW_LOG_ERROR_ID( "OnlineLoadBot.OnlineLoadBotOnlineConfig", "OnlineLoadBot: online config '%#' is %#", options._onlineConfigPath, configRead == ConfigReadResult::Missing ? "missing" : "invalid" );
                        return Exit::kExitUsage;
                    }
                    serverTransport                    = StreamTransportFactory::createPlatformTransport();
                    serverSettings._port               = static_cast<uint16>( options._localServerPort );
                    serverSettings._region             = scenario._region;
                    serverSettings._maxConnections     = scenario._botCount + kSpareConnectionCount;
                    serverSettings._bLightPasswordHash = options._bLightHash;
                    serverSettings._pClock             = &serverClock;
                    const expected<void> started       = serverTransport != nullptr ? localServer.initialize( serverTransport.get(), serverSettings )
                                                                                    : expected<void>( unexpected( Error{ ErrorKind::ExternalFailure, "no platform stream transport" } ) );
                    if ( started.has_value() == false )
                    {
                        SW_LOG_ERROR_ID( "OnlineLoadBot.OnlineLoadBotLocalServerDid", "OnlineLoadBot: local server did not start: %#: %#", started.error().message(), started.error().detail() );
                        return Exit::kExitUsage;
                    }
                    // 포트 0 이면 OS 가 고른 포트다 — 받은 포트로 붙는다.
                    runnerSettings._serverAddress = NetAddress::makeLoopback( localServer.getListenPort() );
                }
                else if ( NetAddress::parse( options._serverAddress, kDefaultServerPort, runnerSettings._serverAddress ) == false )
                {
                    SW_LOG_ERROR_ID( "OnlineLoadBot.OnlineLoadBotCannotReadServer", "OnlineLoadBot: cannot read server address '%#'", options._serverAddress );
                    return Exit::kExitUsage;
                }

                if ( options._gameAddress.empty() == false && NetAddress::parse( options._gameAddress, 0, runnerSettings._gameAddress ) == false )
                {
                    SW_LOG_ERROR_ID( "OnlineLoadBot.OnlineLoadBotCannotReadGame", "OnlineLoadBot: cannot read game address '%#' (--game=<ip:port>)", options._gameAddress );
                    return Exit::kExitUsage;
                }

                unique_ptr<ITLSContext> tlsContext;
                if ( options._tlsTrustFile.empty() == false )
                {
                    expected<unique_ptr<ITLSContext>> created = NetSecurity::createClientTLSContext( game::getSubsystem<INetSecurityProvider>(), options._tlsTrustFile, "" );
                    if ( created.has_value() == false )
                    {
                        SW_LOG_ERROR_ID( "OnlineLoadBot.OnlineLoadBotTLSClientContext", "OnlineLoadBot: TLS client context failed: %# (%#)", created.error().message(), created.error().detail() );
                        return Exit::kExitUsage;
                    }
                    tlsContext                  = std::move( *created );
                    runnerSettings._pTLSContext = tlsContext.get();
                }

                unique_ptr<IStreamTransport> botTransport = StreamTransportFactory::createPlatformTransport();
                LoadBotRunner                runner;
                const expected<void>         runnerStarted = botTransport != nullptr ? runner.initialize( botTransport.get(), scenario, runnerSettings )
                                                                                     : expected<void>( unexpected( Error{ ErrorKind::ExternalFailure, "no platform stream transport" } ) );
                if ( runnerStarted.has_value() == false )
                {
                    SW_LOG_ERROR_ID( "OnlineLoadBot.OnlineLoadBot", "OnlineLoadBot: %#: %#", runnerStarted.error().message(), runnerStarted.error().detail() );
                    return Exit::kExitUsage;
                }
                SW_LOG_INFO_ID( "OnlineLoadBot.OnlineLoadBotScenarioBotsAgainst", "OnlineLoadBot: scenario '%#' with %# bots against %#", scenario._name, scenario._botCount, runnerSettings._serverAddress.toString() );

                bool bRunning = true;
                while ( bRunning )
                {
                    localServer.update( WallClock::nowUnixMilliseconds() );
                    bRunning = runner.update();
                    MonotonicClock::sleepUntilNanoseconds( MonotonicClock::nowNanoseconds() + kIdleSleepNs );
                }

                const LoadBotMetrics& metrics   = runner.getMetrics();
                const int64           elapsedMs = runner.getElapsedMs();
                logLines( LogLevel::Info, metrics.formatTable( elapsedMs ) );
                bool bReportFailed = false;
                if ( options._reportPath.empty() == false )
                {
                    const expected<string> report = metrics.formatJSON( runner.getScenario(), elapsedMs );
                    if ( report.has_value() == false )
                        SW_LOG_ERROR_ID( "OnlineLoadBot.ReportNotWritable", "OnlineLoadBot: report could not be written as JSON: %# (%#)", report.error().message(), report.error().detail() );
                    bReportFailed = report.has_value() == false || FileUtil::writeTextFile( options._reportPath, *report ) == false;
                }
                if ( bReportFailed )
                    SW_LOG_ERROR_ID( "OnlineLoadBot.OnlineLoadBotCannotWriteReport", "OnlineLoadBot: cannot write report '%#'", options._reportPath );
                runner.shutdown();
                localServer.shutdown();

                if ( bReportFailed )
                    return Exit::kExitReport;
                const int64 completedCount = metrics.getTotalCompletedCount();
                const int64 errorCount     = metrics.getTotalErrorCount();
                const bool  bTooManyErrors = completedCount == 0 || errorCount * kPercentScale > completedCount * options._maxErrorPercent;
                if ( bTooManyErrors )
                    SW_LOG_ERROR_ID( "OnlineLoadBot.OnlineLoadBotErrorsCompletedActions", "OnlineLoadBot: %# errors in %# completed actions - over the limit of %# percent", errorCount, completedCount, options._maxErrorPercent );
                return bTooManyErrors ? Exit::kExitErrorRate : Exit::kExitOk;
            }
        };
    } // namespace
} // namespace sw

extern "C" SW_MODULE_API int32 swRunOnlineLoadBot( int32 argc, utf8** argv )
{
    static_assert( std::is_same_v<decltype( &swRunOnlineLoadBot ), sw::PFN_RunOnlineLoadBot> );
    return sw::LoadBotMainInternal::run( argc, argv );
}
