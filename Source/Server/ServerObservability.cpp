#include "pch.h"

#include "Server/ServerObservability.h"

#include "Core/Diagnostics/CrashContext.h"
#include "Core/Diagnostics/CrashHandler.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Network/NetTypes.h"
#include "Core/Network/Transport/IStreamTransport.h"
#include "Core/Time/MonotonicClock.h"
#include "Core/Time/WallClock.h"

#include "Engine/Config/Server/ServerConfig.h"
#include "Engine/DedicatedServer/OpsHTTPEndpoint.h"
#include "Engine/DedicatedServer/ServerFrameHistory.h"
#include "Engine/DedicatedServer/ServiceHealthRegistry.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Net/GameNetSession.h"
#include "Engine/Observability/MetricRegistry.h"
#include "Engine/Observability/ProcessMetrics.h"

namespace sw
{
    SW_LOG_CALLER( "Server" );

    namespace
    {
        struct ServerObservabilityInternal
        {
            /** @brief 운영 HTTP 에는 스크레이퍼와 감시자 몇 개만 붙으므로 연결 상한과 유휴 시한을 작게 잡습니다. */
            static constexpr int32   kOpsMaxConnections     = 64;
            static constexpr float64 kOpsIdleTimeoutSeconds = 10.0;
            /** @brief 프로세스 지표를 다시 읽는 간격입니다. 운영체제에 묻는 비용이 있고 스크레이퍼도 보통 15 초마다 읽으므로 초에 한 번이면 된다. */
            static constexpr int64 kProcessSampleIntervalMs = 1000;
            /** @brief 프레임 이력 덤프(`dumpFrameHistory`)가 놓이는 폴더 이름입니다. 로그 폴더(`Saved/Logs`) 옆에 둔다(`CrashReports` 묶음 폴더와 같은 자리). */
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
        };
    } // namespace

    ServerObservability::ServerObservability()
        : _pServerConfig{ nullptr }
        , _metricRegistry{ nullptr }
        , _healthRegistry{ nullptr }
        , _opsTransport{ nullptr }
        , _opsEndpoint{ nullptr }
        , _processMetrics{ nullptr }
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
        , _nextProcessSampleMs{ 0 }
    {
    }

    ServerObservability::~ServerObservability() = default;

    bool ServerObservability::initialize( const ServerConfig& config, int64 processStartNanoseconds )
    {
        _pServerConfig       = &config;
        _metricRegistry      = make_unique<MetricRegistry>();
        _healthRegistry      = make_unique<ServiceHealthRegistry>();
        _pTickHistogram      = _metricRegistry->registerHistogram( "server_tick_seconds", "Dedicated server tick body duration", MetricRegistry::makeLatencyBounds() );
        _pDroppedTickCounter = _metricRegistry->registerCounter( "server_dropped_ticks_total", "Ticks dropped because the server fell too far behind" );
        _pServiceConnectionGauge =
            _metricRegistry->registerGauge( "server_service_connections", "Connections held by the server services (online service host and others)" );
        _pTickUtilizationGauge = _metricRegistry->registerGauge( "server_tick_utilization_ratio", "Tick body time divided by wall time over the last second (1.0 = no idle time left)" );
        _frameHistory          = make_unique<ServerFrameHistory>();
        _frameHistory->initialize( static_cast<uint32>( config._frameHistoryTicks ) );
        // 크래시 때 이력을 새 파일로 쓰지 않고, 크래시 핸들러가 문맥 파일을 쓰며 이 링 버퍼를 읽어 끝에 싣는다(풀기는 `shutdown`).
        if ( _frameHistory->getCapacity() > 0 )
            _crashExtraTextSubscription = bindCrashExtraTextWriter( &ServerObservabilityInternal::writeFrameCrashTail, _frameHistory.get() );
        _processMetrics = make_unique<ProcessMetrics>();
        _processMetrics->initialize( _metricRegistry.get(), WallClock::nowUnixMilliseconds().count() - ( MonotonicClock::nowNanoseconds() - processStartNanoseconds ) / constant::kNanosecondsPerMillisecond );
        if ( config._opsPort == 0 )
            return true;
        OpsHTTPEndpointConfig opsSettings;
        if ( NetAddress::parse( config._opsListenAddress, static_cast<uint16>( config._opsPort ), opsSettings._bindAddress ) == false )
        {
            SW_LOG_ERROR( "Server config _opsListenAddress '%#' is not an IPv4 address", config._opsListenAddress.c_str() );
            return false;
        }
        opsSettings._bindAddress._port = static_cast<uint16>( config._opsPort ); // 포트는 `_opsPort` 가 기준이다(주소 필드에 포트를 적어도 무시한다).
        _opsTransport                  = StreamTransportFactory::createPlatformTransport();
        if ( _opsTransport == nullptr )
        {
            SW_LOG_ERROR( "Ops HTTP endpoint needs a stream transport and this platform has none" );
            return false;
        }
        StreamTransportConfig transportSettings;
        transportSettings._ioThreadCount      = 1;
        transportSettings._maxConnections     = ServerObservabilityInternal::kOpsMaxConnections;
        transportSettings._idleTimeoutSeconds = ServerObservabilityInternal::kOpsIdleTimeoutSeconds;
        _opsEndpoint                          = make_unique<OpsHTTPEndpoint>();
        if ( _opsEndpoint->initialize( _opsTransport.get(), transportSettings, opsSettings, _metricRegistry.get(), _healthRegistry.get() ) == false )
        {
            _opsEndpoint.reset();
            _opsTransport.reset();
            return false; // 운영자가 켠 엔드포인트를 열지 못하면 서버를 시작하지 않는다. 감시자가 볼 수 없는 서버이기 때문이다.
        }
        return true;
    }

    void ServerObservability::shutdown()
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
        _pServerConfig = nullptr;
    }

    void ServerObservability::beginLoadWindow( int64 monotonicMs )
    {
        _loadWindowStartMs         = monotonicMs;
        _loadWindowBusyNanoseconds = 0;
    }

    void ServerObservability::recordTick( int64 tickNanoseconds, uint64 tickCount, int32 connectionCount, uint32 droppedTickTotal, int64 monotonicMs )
    {
        _pTickHistogram->observe( static_cast<float64>( tickNanoseconds ) * 1.0e-9 );
        _healthRegistry->markTick( monotonicMs );
        _loadWindowBusyNanoseconds += tickNanoseconds;
        if ( _frameHistory == nullptr )
            return;
        ServerFrameSample sample;
        sample._tick             = tickCount;
        sample._bodyMicroseconds = tickNanoseconds / constant::kNanosecondsPerMicrosecond;
        sample._connectionCount  = static_cast<uint32>( connectionCount );
        sample._droppedTickTotal = droppedTickTotal;
        _frameHistory->record( sample );
        const int64 slowNanoseconds = static_cast<int64>( _pServerConfig->_slowTickDumpMs ) * constant::kNanosecondsPerMillisecond;
        const bool  bSlowTick       = slowNanoseconds > 0 && tickNanoseconds >= slowNanoseconds;
        const bool  bDumpDue        = monotonicMs - _lastFrameDumpMs >= ServerObservabilityInternal::kMinFrameDumpIntervalMs;
        if ( bSlowTick && bDumpDue )
        {
            _lastFrameDumpMs = monotonicMs;
            dumpFrameHistory( "slow_tick", tickCount );
        }
    }

    void ServerObservability::addDroppedTicks( uint32 dropped )
    {
        _pDroppedTickCounter->add( dropped );
    }

    void ServerObservability::markAlive( int64 monotonicMs )
    {
        _healthRegistry->markTick( monotonicMs );
    }

    void ServerObservability::dumpFrameHistory( const utf8* pReason, uint64 tickCount )
    {
        if ( _frameHistory == nullptr || _frameHistory->getCount() == 0 )
            return;
        const string crashFolder = getCrashReportFolder();
        if ( crashFolder.empty() )
            return; // 로그 폴더가 서지 않았다 — 덤프를 놓을 곳이 없다
        const string reportsFolder = FileUtil::joinPath( FileUtil::getDirectoryPart( FileUtil::trimTrailingSlashes( crashFolder ) ), ServerObservabilityInternal::kServerReportsFolderName );
        if ( FileUtil::ensureDirectoryExists( reportsFolder ) == false )
            return; // 실패는 ensureDirectoryExists 가 남겼다
        string          netStatsLine;
        GameNetSession* pNetSession = engine::areEngineServicesBound() ? engine::findSubsystem<GameNetSession>() : nullptr;
        if ( pNetSession != nullptr )
            netStatsLine = pNetSession->makeStatsReport().makeLine(); // 메시지 종류별 누적 셈 — 덤프 순간 한 번만 읽는다(틱마다 읽으면 Profiler 탭의 "지난 보고" 상태를 건드린다)
        string text;
        _frameHistory->writeReport( text, pReason, CrashHandler::getSessionID(), netStatsLine );
        string fileName{ "frames_" };
        fileName.append( CrashHandler::getSessionID() ).append( "_tick" ).append( to_string( tickCount ) ).append( "_" ).append( pReason ).append( ".txt" );
        const string path = FileUtil::joinPath( reportsFolder, fileName );
        if ( FileUtil::writeTextFile( path, text ) == false )
        {
            SW_LOG_WARNING( "Frame history could not be written to '%#'", path.c_str() );
            return;
        }
        SW_LOG_INFO( "Frame history (%#) written to '%#' (%# ticks)", pReason, path.c_str(), _frameHistory->getCount() );
    }

    void ServerObservability::updateLoad( int64 monotonicMs )
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
        const bool    bWarnDue    = monotonicMs - _lastLoadWarningMs >= ServerObservabilityInternal::kLoadWarningIntervalMs;
        if ( bOverloaded && bWarnDue )
        {
            _lastLoadWarningMs = monotonicMs;
            SW_LOG_WARNING( "Server tick load is %# percent of the tick budget (warn above %# percent) - the server is close to falling behind", static_cast<int32>( utilization * 100.0 ),
                            _pServerConfig->_tickLoadWarnPercent );
        }
    }

    void ServerObservability::updateEverySecond( int64 monotonicMs, int32 connectionCount )
    {
        if ( monotonicMs < _nextProcessSampleMs )
            return;
        _nextProcessSampleMs = monotonicMs + ServerObservabilityInternal::kProcessSampleIntervalMs;
        updateLoad( monotonicMs );
        if ( _processMetrics != nullptr )
            _processMetrics->update();
        if ( _pServiceConnectionGauge != nullptr )
            _pServiceConnectionGauge->set( static_cast<float64>( connectionCount ) );
    }
} // namespace sw
