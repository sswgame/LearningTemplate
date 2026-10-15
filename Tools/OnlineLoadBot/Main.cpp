/**
 * @file Main.cpp
 * @brief 온라인 부하 테스트 봇 — `OnlineLoadBot --scenario=<json> (--server=<ip:port> [--tls[=<신뢰 PEM>]] | --local-server[=<port>]) [--bots=N] [--ramp-up-seconds=S] [--duration-seconds=S] [--report=<json>]
 *        [--max-error-percent=N] [--light-hash]`.
 * @details - `--local-server` 는 같은 프로세스에 서버 조립(`LoadBotLocalServer` — 전용 서버와 같은 `OnlineServerRuntime`)을 띄우고 루프백 TCP(플랫폼 전송, 평문)로 붙는다.
 *            서버 시각은 UTC 벽시계, 봇 지연은 `MonotonicClock`.
 *          - `--server` 는 전용 서버(`Server` 의 온라인 서비스)에 붙는다. 그 서비스 포트는 TLS 1.3 이므로 `--tls` 를 함께 준다(신뢰 PEM 을 비우면 Dev 개발용 인증서).
 *          - 끝에 동작별 지연 백분위 표를 로그로, `--report` 면 JSON 을 파일로 쓴다. 오류율이 상한(기본 1 %)을 넘으면 종료 코드 1, 인자 · 시나리오 오류는 2.
 */
#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Network/NetTypes.h"
#include "Core/Network/Security/INetSecurityProvider.h"
#include "Core/Network/Transport/IStreamTransport.h"
#include "Core/String/hashed_string.h"
#include "Core/Subsystem/SubsystemCollection.h"
#include "Core/Time/MonotonicClock.h"
#include "Core/Time/WallClock.h"

#include "Engine/Bootstrap/CrashHandlerSubsystem.h"
#include "Engine/Bootstrap/DiagnosticsSubsystem.h"
#include "Engine/Bootstrap/LoggerSubsystem.h"
#include "Engine/Bootstrap/NamePoolSubsystem.h"

#include "GameFramework/Base/Online/Security/NetSecurity.h"

#include "OnlineLoadBot/LoadBotLocalServer.h"
#include "OnlineLoadBot/LoadBotRunner.h"
#include "OnlineLoadBot/LoadBotScenario.h"

SW_LOG_CALLER( "OnlineLoadBot" );
namespace sw
{
    namespace
    {
        /**
         * @brief 프로세스 바닥 — 엔진 호스트와 같은 부트스트랩 서브시스템(진단 도구 · 이름 풀 · 로거 · 크래시 핸들러)을 세우고 역순으로 내립니다.
         * @details 엔진 서비스(리소스 · 태스크 · 씬)와 명령줄 · 리소스 루트는 세우지 않는다 — 봇과 서버 조립은 네트워크 · 키트만 쓰고 인자는 스스로 읽는다.
         *          main 의 맨 처음에 두어 hashed_string 을 든 객체가 모두 사라진 뒤에 이름 풀이 내려가게 한다(로거는 비동기라 내리지 않으면 마지막 줄을 잃는다).
         */
        class ToolProcessScope
        {
        public:
            ToolProcessScope()
                : _bootstrap{}
            {
                _bootstrap.add( make_unique<DiagnosticsSubsystem>( false ) );
                _bootstrap.add( make_unique<NamePoolSubsystem>() );
                _bootstrap.add( make_unique<LoggerSubsystem>() );
                _bootstrap.add( make_unique<CrashHandlerSubsystem>( nullptr ) );
                // 정렬 오류(선언 오타 · 순환)는 로거가 서기 전이다 — 표준 오류로 낸다.
                if ( _bootstrap.initializeAll( SubsystemHostMask::All ) == false )
                    std::fprintf( stderr, "OnlineLoadBot: bootstrap failed: %s\n", _bootstrap.getError().c_str() );
            }

            ~ToolProcessScope() { _bootstrap.destroyAll(); }

            ToolProcessScope( const ToolProcessScope& )            = delete;
            ToolProcessScope& operator=( const ToolProcessScope& ) = delete;

        private:
            SubsystemCollection _bootstrap;
        };

        /** @brief 명령줄 인자입니다. */
        struct LoadBotOptions
        {
            string _scenarioPath{};
            string _reportPath{};
            string _serverAddress{};
            string _tlsTrustFile{}; ///< `--tls=<PEM>` — 비면 Dev 개발용 인증서를 신뢰한다
            int32  _botCount{ 0 };
            int32  _rampUpSeconds{ -1 };   ///< `--ramp-up-seconds` — 0 이상이면 시나리오 값을 덮는다(부하 검증 `<Load rampUpSeconds>`)
            int32  _durationSeconds{ -1 }; ///< `--duration-seconds` — 0 이상이면 시나리오 값을 덮는다(`<Load durationSeconds>`)
            int32  _maxErrorPercent{ 1 };
            uint16 _localServerPort{ 0 };
            uint8  _bLocalServer{ SW_FALSE };
            uint8  _bLightHash{ SW_FALSE };
            uint8  _bTLS{ SW_FALSE }; ///< 전용 서버의 서비스 포트는 TLS 1.3 이다(`--server` 와 함께)
        };

        struct LoadBotMainInternal
        {
            static constexpr int32  kExitOk               = 0;
            static constexpr int32  kExitErrorRate        = 1;
            static constexpr int32  kExitUsage            = 2;
            static constexpr uint16 kDefaultServerPort    = 7100;
            static constexpr int32  kMaxPort              = 65535;
            static constexpr int64  kIdleSleepNs          = 200000; ///< 틱 사이 — 전송을 I/O 스레드 없이 돌리므로 짧게
            static constexpr int32  kPercentScale         = 100;
            static constexpr int32  kSpareConnectionCount = 16;

            [[nodiscard]] static bool readValue( string_view argument, string_view key, string& outValue )
            {
                if ( StringUtil::startsWith( argument, key ) == false )
                    return false;
                outValue = string( argument.substr( key.size() ) );
                return true;
            }

            [[nodiscard]] static bool parseArguments( int32 argc, utf8* argv[], LoadBotOptions& outOptions, string& outError )
            {
                for ( int32 argumentIndex = 1; argumentIndex < argc; ++argumentIndex )
                {
                    const string_view argument = argv[argumentIndex];
                    string            value;
                    const bool        bPathOrAddressOption = readValue( argument, "--scenario=", outOptions._scenarioPath ) || readValue( argument, "--report=", outOptions._reportPath ) ||
                                                      readValue( argument, "--server=", outOptions._serverAddress );
                    if ( bPathOrAddressOption )
                        continue;
                    bool bNumberOk = true;
                    if ( readValue( argument, "--bots=", value ) )
                    {
                        bNumberOk = StringUtil::parseInt( value, outOptions._botCount ) && outOptions._botCount > 0;
                    }
                    else if ( readValue( argument, "--ramp-up-seconds=", value ) )
                    {
                        bNumberOk = StringUtil::parseInt( value, outOptions._rampUpSeconds ) && outOptions._rampUpSeconds >= 0;
                    }
                    else if ( readValue( argument, "--duration-seconds=", value ) )
                    {
                        bNumberOk = StringUtil::parseInt( value, outOptions._durationSeconds ) && outOptions._durationSeconds > 0;
                    }
                    else if ( readValue( argument, "--max-error-percent=", value ) )
                    {
                        bNumberOk = StringUtil::parseInt( value, outOptions._maxErrorPercent ) && outOptions._maxErrorPercent >= 0;
                    }
                    else if ( argument == "--local-server" || readValue( argument, "--local-server=", value ) )
                    {
                        int32 port                  = kDefaultServerPort;
                        bNumberOk                   = value.empty() || ( StringUtil::parseInt( value, port ) && 0 < port && port <= kMaxPort );
                        outOptions._bLocalServer    = SW_TRUE;
                        outOptions._localServerPort = static_cast<uint16>( port );
                    }
                    else if ( argument == "--light-hash" )
                    {
                        outOptions._bLightHash = SW_TRUE;
                    }
                    else if ( argument == "--tls" || readValue( argument, "--tls=", outOptions._tlsTrustFile ) )
                    {
                        outOptions._bTLS = SW_TRUE;
                    }
                    else
                    {
                        outError = "unknown argument '" + string( argument ) + "'";
                        return false;
                    }
                    if ( bNumberOk == false )
                    {
                        outError = "bad number in '" + string( argument ) + "'";
                        return false;
                    }
                }
                const bool bNoTarget             = outOptions._serverAddress.empty() && outOptions._bLocalServer == SW_FALSE;
                const bool bTwoTarget            = outOptions._serverAddress.empty() == false && outOptions._bLocalServer == SW_TRUE;
                const bool bInvalidTargetOptions = outOptions._scenarioPath.empty() || bNoTarget || bTwoTarget;
                if ( bInvalidTargetOptions )
                {
                    outError = "need --scenario=<json> and exactly one of --server=<ip:port> or --local-server[=<port>]";
                    return false;
                }
                if ( outOptions._bTLS == SW_TRUE && outOptions._bLocalServer == SW_TRUE )
                {
                    outError = "--tls goes with --server (the local server listens in plaintext)";
                    return false;
                }
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
    const ToolProcessScope processScope;

    LoadBotOptions options;
    string         error;
    if ( Internal::parseArguments( argc, argv, options, error ) == false )
    {
        SW_LOG_ERROR( "OnlineLoadBot: %#", error );
        return Internal::kExitUsage;
    }
    LoadBotScenario scenario;
    if ( LoadBotScenario::loadFile( options._scenarioPath, scenario, error ) == false )
    {
        SW_LOG_ERROR( "OnlineLoadBot: %#", error );
        return Internal::kExitUsage;
    }

    if ( options._rampUpSeconds >= 0 )
        scenario._rampUpSeconds = options._rampUpSeconds;
    if ( options._durationSeconds > 0 )
        scenario._durationSeconds = options._durationSeconds;

    LoadBotRunnerSettings runnerSettings;
    runnerSettings._botCountOverride = options._botCount;
    const int32 botCount             = options._botCount > 0 ? options._botCount : scenario._botCount;

    unique_ptr<IStreamTransport> serverTransport;
    LoadBotLocalServer           localServer;
    if ( options._bLocalServer == SW_TRUE )
    {
        serverTransport = StreamTransportFactory::createPlatformTransport();
        LoadBotLocalServerSettings serverSettings;
        serverSettings._port               = options._localServerPort;
        serverSettings._region             = scenario._region;
        serverSettings._maxConnections     = botCount + Internal::kSpareConnectionCount;
        serverSettings._bLightPasswordHash = options._bLightHash;
        if ( serverTransport == nullptr || localServer.initialize( serverTransport.get(), serverSettings, error ) == false )
        {
            SW_LOG_ERROR( "OnlineLoadBot: local server did not start: %#", error );
            return Internal::kExitUsage;
        }
        runnerSettings._serverAddress = NetAddress::makeLoopback( options._localServerPort );
    }
    else if ( NetAddress::parse( options._serverAddress, Internal::kDefaultServerPort, runnerSettings._serverAddress ) == false )
    {
        SW_LOG_ERROR( "OnlineLoadBot: cannot read server address '%#'", options._serverAddress );
        return Internal::kExitUsage;
    }

    unique_ptr<ITLSContext> tlsContext;
    if ( options._bTLS == SW_TRUE )
    {
        tlsContext = NetSecurity::createClientTLSContext( options._tlsTrustFile, "", error );
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
    SW_LOG_INFO( "OnlineLoadBot: scenario '%#' with %# bots against %#", scenario._name, botCount, runnerSettings._serverAddress.toString() );

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
    if ( options._reportPath.empty() == false && FileUtil::writeTextFile( options._reportPath, metrics.formatJSON( runner.getScenario(), elapsedMs ) ) == false )
        SW_LOG_ERROR( "OnlineLoadBot: cannot write report '%#'", options._reportPath );
    runner.shutdown();
    localServer.shutdown();

    const int64 completedCount = metrics.getTotalCompletedCount();
    const int64 errorCount     = metrics.getTotalErrorCount();
    const bool  bTooManyErrors = completedCount == 0 || errorCount * Internal::kPercentScale > completedCount * options._maxErrorPercent;
    return bTooManyErrors ? Internal::kExitErrorRate : Internal::kExitOk;
}
