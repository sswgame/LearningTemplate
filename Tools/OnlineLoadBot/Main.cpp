/**
 * @file Main.cpp
 * @brief 온라인 부하 시험 봇 — `OnlineLoadBot --scenario=<json> (--server=<ip:port> | --local-server[=<port>]) [--bots=N] [--report=<json>]
 *        [--max-error-percent=N] [--light-hash]`.
 * @details - `--local-server` 는 같은 프로세스에 서버 조립(`LoadBotLocalServer`)을 띄우고 루프백 TCP(플랫폼 전송)로 붙는다 — 전용 서버가 온라인 서비스를
 *            조립하기 전까지 키트 · 와이어 · 전송을 함께 재는 길이다. 서버 시각은 UTC 벽시계, 봇 지연은 `MonotonicClock`.
 *          - 끝에 동작별 지연 백분위 표를 로그로, `--report` 면 JSON 을 파일로 쓴다. 오류율이 상한(기본 1 %)을 넘으면 종료 코드 1, 인자 · 시나리오 오류는 2.
 */
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Network/NetTypes.h"
#include "Core/Network/Transport/IStreamTransport.h"
#include "Core/Process/CrashHandler.h"
#include "Core/String/StringUtil.h"
#include "Core/String/hashed_string.h"
#include "Core/Time/MonotonicClock.h"
#include "Core/Time/WallClock.h"

#include "OnlineLoadBot/LoadBotLocalServer.h"
#include "OnlineLoadBot/LoadBotRunner.h"
#include "OnlineLoadBot/LoadBotScenario.h"

SW_LOG_CALLER( "OnlineLoadBot" );
namespace sw
{
    namespace
    {
        /**
         * @brief 프로세스 바닥 — 이름 풀(hashed_string) · 로거 · 크래시 핸들러를 `EngineBootstrap` 의 앞부분과 같은 순서로 세우고 거꾸로 내립니다.
         * @details 엔진 서비스(리소스 · 태스크 · 씬)는 세우지 않는다 — 봇과 서버 조립은 네트워크 · 키트만 쓴다. main 의 맨 처음에 두어 hashed_string 을
         *          든 객체가 모두 사라진 뒤에 이름 풀이 내려가게 한다. 로거는 비동기라 내리지 않으면 마지막 줄을 잃는다.
         */
        class ToolProcessScope
        {
        public:
            ToolProcessScope()
                : _logger{ make_unique<Logger>() }
            {
                HashedStringPool::initialize();
                _logger->initialize();
                CrashHandler::initialize();
            }

            ~ToolProcessScope()
            {
                _logger->shutdown();
                HashedStringPool::shutdown();
                CrashHandler::shutdown();
                _logger.reset();
            }

            ToolProcessScope( const ToolProcessScope& )            = delete;
            ToolProcessScope& operator=( const ToolProcessScope& ) = delete;

        private:
            unique_ptr<Logger> _logger;
        };

        /** @brief 명령줄 인자입니다. */
        struct LoadBotOptions
        {
            string _scenarioPath{};
            string _reportPath{};
            string _serverAddress{};
            int32  _botCount{ 0 };
            int32  _maxErrorPercent{ 1 };
            uint16 _localServerPort{ 0 };
            uint8  _bLocalServer{ SW_FALSE };
            uint8  _bLightHash{ SW_FALSE };
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
                    if ( readValue( argument, "--scenario=", outOptions._scenarioPath ) || readValue( argument, "--report=", outOptions._reportPath ) ||
                         readValue( argument, "--server=", outOptions._serverAddress ) )
                        continue;
                    bool bNumberOk = true;
                    if ( readValue( argument, "--bots=", value ) )
                    {
                        bNumberOk = StringUtil::parseInt( value, outOptions._botCount ) && outOptions._botCount > 0;
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
                const bool bNoTarget  = outOptions._serverAddress.empty() && outOptions._bLocalServer == SW_FALSE;
                const bool bTwoTarget = outOptions._serverAddress.empty() == false && outOptions._bLocalServer == SW_TRUE;
                if ( outOptions._scenarioPath.empty() || bNoTarget || bTwoTarget )
                {
                    outError = "need --scenario=<json> and exactly one of --server=<ip:port> or --local-server[=<port>]";
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
        localServer.tick( WallClock::nowUnixMilliseconds() );
        bRunning = runner.tick();
        MonotonicClock::sleepUntilNanoseconds( MonotonicClock::nowNanoseconds() + Internal::kIdleSleepNs );
    }

    const LoadBotMetrics& metrics   = runner.getMetrics();
    const int64           elapsedMs = runner.getElapsedMs();
    Internal::logLines( metrics.formatTable( elapsedMs ) );
    if ( options._reportPath.empty() == false && FileUtil::writeTextFile( options._reportPath, metrics.formatJson( runner.getScenario(), elapsedMs ) ) == false )
        SW_LOG_ERROR( "OnlineLoadBot: cannot write report '%#'", options._reportPath );
    runner.shutdown();
    localServer.shutdown();

    const int64 completedCount = metrics.getTotalCompletedCount();
    const int64 errorCount     = metrics.getTotalErrorCount();
    const bool  bTooManyErrors = completedCount == 0 || errorCount * Internal::kPercentScale > completedCount * options._maxErrorPercent;
    return bTooManyErrors ? Internal::kExitErrorRate : Internal::kExitOk;
}
