#include "pch.h"

#include "OnlineLoadBot/LoadBotRunner.h"

#include "Core/Network/Transport/IStreamTransport.h"
#include "Core/Time/MonotonicClock.h"

#include "OnlineLoadBot/LoadBot.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct LoadBotRunnerInternal
        {
            static constexpr int64 kMicrosecondsPerMs     = 1000;
            static constexpr int64 kMillisecondsPerSecond = 1000;
            static constexpr int32 kSpareConnectionCount  = 16; ///< 다시 연결하는 동안 닫히는 연결이 자리를 잡고 있다
        };
    } // namespace
} // namespace sw

namespace sw
{
    LoadBotRunner::LoadBotRunner()
        : _listBot{}
        , _mapConnectionToBot{}
        , _scenario{}
        , _settings{}
        , _metrics{}
        , _endpoint{}
        , _requestClient{}
        , _pTransport{ nullptr }
        , _startUs{ 0 }
        , _manualNowMs{ 0 }
        , _manualStartMs{ 0 }
        , _launchedCount{ 0 }
        , _bManualClock{ SW_FALSE }
        , _bInitialized{ SW_FALSE }
    {
    }

    LoadBotRunner::~LoadBotRunner() { shutdown(); }

    bool LoadBotRunner::initialize( IStreamTransport* pTransport, const LoadBotScenario& scenario, const LoadBotRunnerSettings& settings, string& outError )
    {
        SW_ASSERT( _bInitialized == SW_FALSE );
        _scenario = scenario;
        _settings = settings;
        if ( settings._botCountOverride > 0 )
            _scenario._botCount = settings._botCountOverride;
        StreamTransportSettings transportSettings;
        transportSettings._ioThreadCount  = 0;
        transportSettings._maxConnections = std::max( transportSettings._maxConnections, _scenario._botCount + LoadBotRunnerInternal::kSpareConnectionCount );
        if ( pTransport == nullptr || _endpoint.initialize( pTransport, StreamEndpointSettings{} ) == false ||
             pTransport->initialize( &_endpoint, transportSettings ) == false )
        {
            outError = "load bot runner could not start its stream transport";
            return false;
        }
        _pTransport = pTransport;
        _requestClient.initialize( &_endpoint );
        _listBot.reserve( static_cast<size_t>( _scenario._botCount ) );
        for ( int32 botIndex = 0; botIndex < _scenario._botCount; ++botIndex )
            _listBot.push_back( make_unique<LoadBot>( this, botIndex ) );
        _bInitialized = SW_TRUE;
        return true;
    }

    void LoadBotRunner::shutdown()
    {
        if ( _bInitialized == SW_FALSE )
            return;
        _bInitialized = SW_FALSE;
        for ( unique_ptr<LoadBot>& bot : _listBot )
            bot->shutdown();
        _requestClient.shutdown();
        _pTransport->shutdown();
        _endpoint.shutdown();
        _mapConnectionToBot.clear();
        _listBot.clear();
        _pTransport = nullptr;
    }

    void LoadBotRunner::setManualClock( int64 nowMs )
    {
        if ( _bManualClock == SW_FALSE )
            _manualStartMs = nowMs;
        _bManualClock = SW_TRUE;
        _manualNowMs  = nowMs;
    }

    int64 LoadBotRunner::getNowUs() const
    {
        if ( _bManualClock == SW_TRUE )
            return ( _manualNowMs - _manualStartMs ) * LoadBotRunnerInternal::kMicrosecondsPerMs;
        return _startUs > 0 ? MonotonicClock::nowMicroseconds() - _startUs : 0;
    }

    int64 LoadBotRunner::getNowMs() const { return getNowUs() / LoadBotRunnerInternal::kMicrosecondsPerMs; }

    int64 LoadBotRunner::computeLaunchMs( int32 botIndex ) const
    {
        const int64 rampUpMs = static_cast<int64>( _scenario._rampUpSeconds ) * LoadBotRunnerInternal::kMillisecondsPerSecond;
        return rampUpMs * botIndex / std::max( 1, _scenario._botCount );
    }

    bool LoadBotRunner::tick()
    {
        if ( _bInitialized == SW_FALSE )
            return false;
        if ( _bManualClock == SW_FALSE && _startUs == 0 )
            _startUs = MonotonicClock::nowMicroseconds();
        const int64 nowMs = getNowMs();
        (void)_pTransport->pollIo( 0 );
        (void)_endpoint.pump( *this );
        _requestClient.update();

        while ( _launchedCount < getBotCount() && computeLaunchMs( _launchedCount ) <= nowMs )
        {
            _listBot[static_cast<size_t>( _launchedCount )]->start();
            ++_launchedCount;
        }
        int32 runningCount = 0;
        for ( int32 botIndex = 0; botIndex < _launchedCount; ++botIndex )
            runningCount += _listBot[static_cast<size_t>( botIndex )]->tick( nowMs ) ? 1 : 0;

        const int64 endMs     = static_cast<int64>( _scenario._rampUpSeconds + _scenario._durationSeconds ) * LoadBotRunnerInternal::kMillisecondsPerSecond;
        const bool  bAllDone  = _launchedCount == getBotCount() && runningCount == 0;
        const bool  bTimedOut = nowMs >= endMs;
        return bAllDone == false && bTimedOut == false;
    }

    int32 LoadBotRunner::getRunningBotCount() const
    {
        int32 runningCount = 0;
        for ( int32 botIndex = 0; botIndex < _launchedCount; ++botIndex )
            runningCount += _listBot[static_cast<size_t>( botIndex )]->isFinished() ? 0 : 1;
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

    void LoadBotRunner::closeBot( StreamConnectionHandle handle ) { _endpoint.close( handle, StreamCloseMode::Graceful ); }

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
