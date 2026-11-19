#include "pch.h"

#include "Programs/OnlineLoadBot/LoadBotRunner.h"

#include "Core/Foundation/Constants.h"
#include "Core/Math/MathUtil.h"
#include "Core/Network/Transport/IStreamTransport.h"
#include "Core/Time/MonotonicClock.h"
#include "Core/Time/WallClock.h"

#include "Programs/OnlineLoadBot/LoadBot.h"

#include <algorithm>

namespace sw
{
    LoadBotRunner::LoadBotRunner()
        : _listBot{}
        , _mapConnectionToBot{}
        , _scenario{}
        , _settings{}
        , _metrics{}
        , _arrivalSchedule{ 0, 0 }
        , _endpoint{}
        , _requestClient{}
        , _pTransport{ nullptr }
        , _startUs{ 0 }
        , _runID{ 0 }
        , _manualNowMs{ 0 }
        , _manualStartMs{ 0 }
        , _arrivedCount{ 0 }
        , _launchedCount{ 0 }
        , _bManualClock{ SW_FALSE }
        , _bInitialized{ SW_FALSE }
    {
    }

    LoadBotRunner::~LoadBotRunner()
    {
        shutdown();
    }

    expected<void> LoadBotRunner::initialize( IStreamTransport* pTransport, const LoadBotScenario& scenario, const LoadBotRunnerSettings& settings )
    {
        SW_ASSERT( _bInitialized == SW_FALSE );
        _scenario = scenario;
        _settings = settings;
        _runID    = WallClock::nowUnixMilliseconds().count();
        if ( _scenario._botCount <= 0 || _scenario._durationMs <= 0 || _scenario._rampUpMs < 0 )
            return unexpected( Error{ ErrorKind::InvalidArgument, "the load (bots, ramp-up, duration) is not set - it comes from the <Load> declaration (--load)" } );
        if ( _scenario._executor == LoadBotExecutor::ConstantArrivalRate )
        {
            // 도착률은 길이 동안 고른 속도다 — 늘리는 시간을 받으면 그 값이 무엇을 뜻하는지 숨는다(조용히 버리지 않는다)
            if ( _scenario._rampUpMs != 0 || _scenario._arrivalRatePerSecond <= 0 )
                return unexpected( Error{ ErrorKind::InvalidArgument, "constant_arrival_rate needs rampUpSeconds=\"0\" in <Load> and _arrivalRatePerSecond > 0" } );
            _arrivalSchedule = LoadBotArrivalSchedule{ _scenario._arrivalRatePerSecond, _scenario._durationMs };
        }
        StreamTransportConfig transportSettings;
        transportSettings._ioThreadCount  = 0;
        transportSettings._maxConnections = MathUtil::max( transportSettings._maxConnections, _scenario._botCount + kSpareConnectionCount );
        StreamEndpointConfig endpointSettings;
        endpointSettings._security._pTLSContext = settings._pTLSContext;
        const bool bTransportSetupFailed        = pTransport == nullptr || _endpoint.initialize( pTransport, endpointSettings ) == false || pTransport->initialize( &_endpoint, transportSettings ) == false;
        if ( bTransportSetupFailed )
            return unexpected( Error{ ErrorKind::ExternalFailure, "load bot runner could not start its stream transport" } );
        _pTransport = pTransport;
        _requestClient.initialize( &_endpoint );
        _listBot.reserve( static_cast<size_t>( _scenario._botCount ) );
        for ( int32 botIndex = 0; botIndex < _scenario._botCount; ++botIndex )
        {
            _listBot.push_back( make_unique<LoadBot>( this, botIndex ) );
        }
        _bInitialized = SW_TRUE;
        return {};
    }

    void LoadBotRunner::shutdown()
    {
        if ( _bInitialized == SW_FALSE )
            return;
        _bInitialized = SW_FALSE;
        for ( unique_ptr<LoadBot>& bot : _listBot )
        {
            bot->shutdown();
        }
        _requestClient.shutdown();
        _pTransport->shutdown();
        _endpoint.shutdown();
        _mapConnectionToBot.clear();
        _listBot.clear();
        _pTransport = nullptr;
    }

    void LoadBotRunner::setManualClock( MonotonicMilliseconds nowMs )
    {
        if ( _bManualClock == SW_FALSE )
            _manualStartMs = nowMs;
        _bManualClock = SW_TRUE;
        _manualNowMs  = nowMs;
    }

    int64 LoadBotRunner::getNowUs() const
    {
        if ( _bManualClock == SW_TRUE )
            return ( _manualNowMs - _manualStartMs ) * constant::time::kMicrosecondsPerMillisecond;
        return _startUs > 0 ? MonotonicClock::nowMicroseconds() - _startUs : 0;
    }

    MonotonicMilliseconds LoadBotRunner::getNowMs() const
    {
        return kRunStart + getNowUs() / constant::time::kMicrosecondsPerMillisecond;
    }

    int64 LoadBotRunner::computeLaunchMs( int32 botIndex ) const
    {
        return _scenario._rampUpMs * botIndex / MathUtil::max( 1, _scenario._botCount );
    }

    bool LoadBotRunner::update()
    {
        if ( _bInitialized == SW_FALSE )
            return false;
        if ( _bManualClock == SW_FALSE && _startUs == 0 )
            _startUs = MonotonicClock::nowMicroseconds();
        const MonotonicMilliseconds nowMs = getNowMs();
        (void)_pTransport->pollIO( 0 );
        (void)_endpoint.poll( *this );
        _requestClient.update();

        const bool bArrivalsDone = launchDueBots( nowMs );
        int32      runningCount  = 0;
        for ( int32 botIndex = 0; botIndex < _launchedCount; ++botIndex )
        {
            runningCount += _listBot[static_cast<size_t>( botIndex )]->update( nowMs ) ? 1 : 0;
        }

        const bool  bArrival  = _scenario._executor == LoadBotExecutor::ConstantArrivalRate;
        const int64 endMs     = bArrival ? _scenario._durationMs + _scenario._gracefulStopMs : _scenario._rampUpMs + _scenario._durationMs;
        const bool  bAllDone  = bArrivalsDone && runningCount == 0;
        const bool  bTimedOut = nowMs >= kRunStart + endMs;
        return bAllDone == false && bTimedOut == false;
    }

    bool LoadBotRunner::launchDueBots( MonotonicMilliseconds nowMs )
    {
        if ( _scenario._executor == LoadBotExecutor::PerUserIterations )
        {
            while ( _launchedCount < getBotCount() && kRunStart + computeLaunchMs( _launchedCount ) <= nowMs )
            {
                _listBot[static_cast<size_t>( _launchedCount )]->start();
                ++_launchedCount;
            }
            return _launchedCount == getBotCount();
        }
        const int64 dueCount = _arrivalSchedule.computeDueCount( getNowUs() );
        while ( _arrivedCount < dueCount )
        {
            ++_arrivedCount;
            if ( _launchedCount < getBotCount() )
            {
                _listBot[static_cast<size_t>( _launchedCount )]->start();
                ++_launchedCount;
            }
            else
            {
                _metrics.recordDroppedArrival(); // 풀이 바닥났다 — 도착을 미루지 않는다(미루면 닫힌 모델이 된다)
            }
        }
        return _arrivedCount == _arrivalSchedule.getTotalCount();
    }

    int32 LoadBotRunner::getRunningBotCount() const
    {
        int32 runningCount = 0;
        for ( int32 botIndex = 0; botIndex < _launchedCount; ++botIndex )
        {
            runningCount += _listBot[static_cast<size_t>( botIndex )]->isFinished() ? 0 : 1;
        }
        return runningCount;
    }

    LoadBot* LoadBotRunner::findBot( int32 botIndex ) const
    {
        if ( botIndex < 0 || botIndex >= getBotCount() )
            return nullptr;
        return _listBot[static_cast<size_t>( botIndex )].get();
    }

    StreamConnectionHandle LoadBotRunner::connectBot( int32 botIndex )
    {
        const StreamConnectionHandle handle = _endpoint.connect( _settings._serverAddress );
        if ( handle.isValid() )
            _mapConnectionToBot[handle.packed()] = botIndex;
        else
            _metrics.recordConnection( false );
        return handle;
    }

    void LoadBotRunner::closeBot( StreamConnectionHandle handle )
    {
        _endpoint.close( handle, StreamCloseMode::Graceful );
    }

    LoadBot* LoadBotRunner::findBotByConnection( StreamConnectionHandle handle ) const
    {
        const auto botIt = _mapConnectionToBot.find( handle.packed() );
        return botIt != _mapConnectionToBot.end() ? findBot( botIt->second ) : nullptr;
    }

    void LoadBotRunner::onEndpointOpened( StreamConnectionHandle handle, const NetAddress& remote, bool bAccepted )
    {
        (void)remote;
        (void)bAccepted;
        LoadBot* pBot = findBotByConnection( handle );
        if ( pBot == nullptr )
            return;
        _metrics.recordConnection( true );
        pBot->handleOpened( &_endpoint, &_requestClient, handle );
    }

    void LoadBotRunner::onEndpointFrame( StreamConnectionHandle handle, StreamFrameKind kind, const uint8* pBody, int32 bodySize )
    {
        if ( _requestClient.handleFrame( handle, kind, pBody, bodySize ) )
            return;
        if ( kind == StreamFrameKind::Message && bodySize >= 2 )
            _metrics.recordPush( static_cast<uint16>( pBody[0] | ( pBody[1] << 8 ) ) ); // 알림 머리 — 종류 u16(리틀 엔디언)
        LoadBot* pBot = findBotByConnection( handle );
        if ( pBot != nullptr )
            pBot->handleFrame( kind, pBody, bodySize );
    }

    void LoadBotRunner::onEndpointClosed( StreamConnectionHandle handle, StreamCloseReason reason )
    {
        _requestClient.onConnectionClosed( handle ); // 날아가던 요청은 ConnectionLost
        LoadBot* pBot = findBotByConnection( handle );
        _mapConnectionToBot.erase( handle.packed() );
        if ( pBot == nullptr )
            return;
        if ( pBot->getConnection() == handle && pBot->isConnectionOpen() == false )
            _metrics.recordConnection( false ); // 열리지도 못했다
        else
            _metrics.recordDisconnect();
        if ( pBot->getConnection() == handle )
            pBot->handleClosed( reason );
    }
} // namespace sw
