#include "pch.h"

#include "GameFramework/Base/Online/Service/OnlineServiceClient.h"

#include "Core/Network/Transport/IStreamTransport.h"

#include "GameFramework/Base/Online/Service/OnlineProtocol.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct OnlineServiceClientInternal
        {
            static constexpr int64 kFirstBackoffMs = 250;

            static uint16 readUint16( const uint8* pData ) { return static_cast<uint16>( pData[0] | ( pData[1] << 8 ) ); }
        };
    } // namespace
} // namespace sw

namespace sw
{
    OnlineServiceClient::OnlineServiceClient()
        : _settings{}
        , _listService{}
        , _listQueuedCall{}
        , _mapPendingCall{}
        , _ownedEndpoint{}
        , _ownedRequestClient{}
        , _pEndpoint{ nullptr }
        , _pRequestClient{ nullptr }
        , _pTransport{ nullptr }
        , _pSendingCall{ nullptr }
        , _connection{}
        , _nowMs{ 0 }
        , _nextConnectMs{ 0 }
        , _backoffMs{ 0 }
        , _serverTimeMs{ 0 }
        , _remoteConfigHash{ 0 }
        , _nextRequestId{ 1 }
        , _state{ OnlineClientState::Disconnected }
        , _bSharedEndpoint{ SW_FALSE }
        , _bInitialized{ SW_FALSE }
    {
    }

    OnlineServiceClient::~OnlineServiceClient() { shutdown(); }

    bool OnlineServiceClient::registerClientService( IOnlineClientService* pService )
    {
        SW_ASSERT( pService != nullptr && _bInitialized == SW_FALSE );
        for ( const IOnlineClientService* pExisting : _listService )
        {
            if ( pExisting->getMethodRange() == pService->getMethodRange() )
                return false;
        }
        _listService.push_back( pService );
        return true;
    }

    bool OnlineServiceClient::initialize( IStreamTransport* pTransport, const OnlineServiceClientSettings& settings, string& outError )
    {
        SW_ASSERT( _bInitialized == SW_FALSE );
        _settings           = settings;
        _pTransport         = pTransport;
        _ownedEndpoint      = sw::make_unique<StreamMessageEndpoint>();
        _ownedRequestClient = sw::make_unique<NetRequestClient>();
        if ( pTransport == nullptr || _ownedEndpoint->initialize( pTransport, settings._endpointSettings ) == false ||
             pTransport->initialize( _ownedEndpoint.get(), settings._transportSettings ) == false )
        {
            outError = "online service client could not start its stream transport";
            return false;
        }
        _ownedRequestClient->initialize( _ownedEndpoint.get() );
        _pEndpoint      = _ownedEndpoint.get();
        _pRequestClient = _ownedRequestClient.get();
        _bInitialized   = SW_TRUE;
        beginConnect();
        return true;
    }

    bool OnlineServiceClient::initializeOnSharedEndpoint( StreamMessageEndpoint* pEndpoint, NetRequestClient* pRequestClient, StreamConnectionHandle handle,
                                                          const OnlineServiceClientSettings& settings )
    {
        SW_ASSERT( _bInitialized == SW_FALSE );
        if ( pEndpoint == nullptr || pRequestClient == nullptr || handle.isValid() == false )
            return false;
        _settings        = settings;
        _pEndpoint       = pEndpoint;
        _pRequestClient  = pRequestClient;
        _connection      = handle;
        _bSharedEndpoint = SW_TRUE;
        _bInitialized    = SW_TRUE;
        _state           = OnlineClientState::Negotiating;
        sendHello();
        return true;
    }

    void OnlineServiceClient::shutdown()
    {
        if ( _bInitialized == SW_FALSE )
            return;
        _bInitialized = SW_FALSE;
        failQueuedCalls( NetRequestStatus::Cancelled, OnlineError::kUnavailable );
        if ( _bSharedEndpoint == SW_FALSE )
        {
            _ownedRequestClient->shutdown(); // 날아가던 요청은 Cancelled
            _pTransport->shutdown();
            _ownedEndpoint->shutdown();
        }
        _mapPendingCall.clear();
        _state = OnlineClientState::Disconnected;
    }

    void OnlineServiceClient::tick( int64 nowMs )
    {
        if ( _bInitialized == SW_FALSE )
            return;
        _nowMs = nowMs;
        if ( _bSharedEndpoint == SW_FALSE )
        {
            if ( _settings._transportSettings._ioThreadCount == 0 )
                (void)_pTransport->pollIo( 0 );
            (void)_pEndpoint->pump( *this );
            _pRequestClient->update();
            if ( _state == OnlineClientState::Disconnected && nowMs >= _nextConnectMs )
                beginConnect();
        }
        // 모은 요청의 시한 — 맡긴 때부터 센다(연결을 기다리다 시한을 넘기면 DeadlineExceeded).
        for ( size_t index = 0; index < _listQueuedCall.size(); )
        {
            if ( _listQueuedCall[index]._deadlineMs > nowMs )
            {
                ++index;
                continue;
            }
            const QueuedCall call = std::move( _listQueuedCall[index] );
            _listQueuedCall.erase( _listQueuedCall.begin() + static_cast<ptrdiff_t>( index ) );
            NetResponse response;
            response._method = call._method;
            response._status = NetRequestStatus::DeadlineExceeded;
            deliver( call._onResponse, call._requestId, response );
        }
    }

    uint64 OnlineServiceClient::sendRequest( uint16 method, const BitWriter& body, const NetRequestOptions& options, OnlineResponseDelegate onResponse )
    {
        QueuedCall call;
        call._bodyBytes.assign( body.getBytes().begin(), body.getBytes().begin() + body.getByteCount() );
        call._options          = options;
        call._onResponse       = onResponse;
        call._requestId        = _nextRequestId++;
        call._deadlineMs       = _nowMs + static_cast<int64>( options._timeoutSeconds * 1000.0 );
        call._method           = method;
        const uint64 requestId = call._requestId;
        if ( _bInitialized == SW_FALSE || _state == OnlineClientState::VersionMismatch )
        {
            const bool     bMismatch = _state == OnlineClientState::VersionMismatch;
            OnlineResponse response;
            response._requestId = requestId;
            response._status    = bMismatch ? NetRequestStatus::ApplicationError : NetRequestStatus::ConnectionLost;
            response._errorCode = bMismatch ? OnlineError::kVersionMismatch : OnlineError::kUnavailable;
            if ( onResponse.isBound() )
                onResponse( response );
            return requestId;
        }
        if ( _state != OnlineClientState::Ready )
        {
            _listQueuedCall.push_back( std::move( call ) );
            return requestId;
        }
        sendQueuedCall( call );
        return requestId;
    }

    void OnlineServiceClient::handleFrame( StreamFrameKind kind, const uint8* pBody, int32 bodySize )
    {
        if ( kind == StreamFrameKind::Message )
            dispatchPush( pBody, bodySize );
    }

    void OnlineServiceClient::handleClosed( StreamCloseReason reason )
    {
        (void)reason;
        if ( _state != OnlineClientState::VersionMismatch )
            _state = OnlineClientState::Disconnected;
        failQueuedCalls( NetRequestStatus::ConnectionLost, OnlineError::kUnavailable ); // 공유 모드는 다시 연결하지 않는다
    }

    void OnlineServiceClient::onEndpointOpened( StreamConnectionHandle handle, const NetAddress& remote, bool bAccepted )
    {
        (void)remote;
        (void)bAccepted;
        if ( handle != _connection )
            return;
        _state = OnlineClientState::Negotiating;
        sendHello();
    }

    void OnlineServiceClient::onEndpointFrame( StreamConnectionHandle handle, StreamFrameKind kind, const uint8* pBody, int32 bodySize )
    {
        if ( _pRequestClient->handleFrame( handle, kind, pBody, bodySize ) )
            return;
        if ( handle == _connection )
            handleFrame( kind, pBody, bodySize );
    }

    void OnlineServiceClient::onEndpointClosed( StreamConnectionHandle handle, StreamCloseReason reason )
    {
        _pRequestClient->onConnectionClosed( handle ); // 날아가던 요청은 ConnectionLost
        if ( handle != _connection )
            return;
        (void)reason;
        _connection = StreamConnectionHandle{};
        if ( _state == OnlineClientState::VersionMismatch )
            return;
        _state         = OnlineClientState::Disconnected;
        _nextConnectMs = _nowMs + _backoffMs;
        _backoffMs     = _backoffMs <= 0 ? OnlineServiceClientInternal::kFirstBackoffMs : std::min( _backoffMs * 2, _settings._maxBackoffMs );
    }

    void OnlineServiceClient::sendHello()
    {
        // 몸: [기반 판 varuint][키트 수 varuint][(영역 u16, 판 varuint) …][게임 빌드 blob][플랫폼 u8]
        BitWriter hello;
        hello.writeVarUint( OnlineProtocolConstant::kBaseVersion );
        hello.writeVarUint( _listService.size() );
        for ( const IOnlineClientService* pService : _listService )
        {
            hello.writeBits( pService->getMethodRange(), 16 );
            hello.writeVarUint( pService->getProtocolVersion() );
        }
        const size_t buildSize = std::min( _settings._gameBuild.size(), static_cast<size_t>( OnlineProtocolConstant::kMaxBuildTextSize ) );
        hello.writeBlob( reinterpret_cast<const uint8*>( _settings._gameBuild.data() ), static_cast<int32>( buildSize ) );
        hello.writeBits( _settings._platform, 8 );
        NetRequestOptions options;
        options._timeoutSeconds = 10.0;
        (void)_pRequestClient->sendRequest( _connection, OnlineHostMethod::kHello, hello.getBytes().data(), hello.getByteCount(), options,
                                            Delegate<void( const NetResponse& )>::create<&OnlineServiceClient::onHelloResponse>( this ) );
    }

    void OnlineServiceClient::sendQueuedCall( QueuedCall& call )
    {
        const int64       remainingMs = std::max<int64>( 1, call._deadlineMs - _nowMs );
        NetRequestOptions options     = call._options;
        options._timeoutSeconds       = static_cast<float64>( remainingMs ) / 1000.0;
        _pSendingCall                 = &call;
        const uint64 netRequestId     = _pRequestClient->sendRequest( _connection, call._method, call._bodyBytes.data(), static_cast<int32>( call._bodyBytes.size() ),
                                                                      options, Delegate<void( const NetResponse& )>::create<&OnlineServiceClient::onNetResponse>( this ) );
        _pSendingCall                 = nullptr;
        if ( netRequestId != 0 )
            _mapPendingCall[netRequestId] = PendingCall{ call._onResponse, call._requestId };
    }

    void OnlineServiceClient::flushQueuedCalls()
    {
        vector<QueuedCall> listCall = std::move( _listQueuedCall );
        _listQueuedCall.clear();
        for ( QueuedCall& call : listCall )
        {
            if ( _state == OnlineClientState::Ready )
                sendQueuedCall( call );
            else
                _listQueuedCall.push_back( std::move( call ) ); // 보내는 도중 끊겼다 — 다음 Hello 를 기다린다
        }
    }

    void OnlineServiceClient::failQueuedCalls( NetRequestStatus status, uint16 errorCode )
    {
        vector<QueuedCall> listCall = std::move( _listQueuedCall );
        _listQueuedCall.clear();
        for ( const QueuedCall& call : listCall )
        {
            OnlineResponse response;
            response._requestId = call._requestId;
            response._status    = status;
            response._errorCode = errorCode;
            if ( call._onResponse.isBound() )
                call._onResponse( response );
        }
    }

    void OnlineServiceClient::beginConnect()
    {
        _connection = _pEndpoint->connect( _settings._serverAddress );
        _state      = _connection.isValid() ? OnlineClientState::Connecting : OnlineClientState::Disconnected;
        if ( _connection.isValid() == false )
            _nextConnectMs = _nowMs + std::max( _backoffMs, OnlineServiceClientInternal::kFirstBackoffMs );
    }

    void OnlineServiceClient::onHelloResponse( const NetResponse& response )
    {
        if ( response._status == NetRequestStatus::Ok )
        {
            BitReader    reader( response._pBody, response._bodySize );
            const uint64 baseVersion = reader.readVarUint();
            const uint64 kitCount    = reader.readVarUint();
            for ( uint64 index = 0; index < kitCount && reader.hasOverflowed() == false; ++index )
            {
                (void)reader.readBits( 16 );
                (void)reader.readVarUint();
            }
            _remoteConfigHash = reader.readVarUint();
            _serverTimeMs     = reader.readVarInt();
            if ( reader.hasOverflowed() || baseVersion != OnlineProtocolConstant::kBaseVersion )
            {
                SW_LOG_WARNING( "Online service Hello answer is malformed" );
                return;
            }
            _state     = OnlineClientState::Ready;
            _backoffMs = 0;
            for ( IOnlineClientService* pService : _listService ) // 재접속 요청이 모은 요청보다 먼저 나간다
                pService->onClientReady( *this );
            flushQueuedCalls();
            return;
        }
        const bool bMismatch = response._status == NetRequestStatus::ApplicationError && response._bodySize >= 2 &&
                               OnlineServiceClientInternal::readUint16( response._pBody ) == OnlineError::kVersionMismatch;
        if ( bMismatch )
        {
            SW_LOG_WARNING( "Online service protocol versions differ from the server - an update is required" );
            _state = OnlineClientState::VersionMismatch;
            failQueuedCalls( NetRequestStatus::ApplicationError, OnlineError::kVersionMismatch );
        }
    }

    void OnlineServiceClient::onNetResponse( const NetResponse& response )
    {
        const auto pendingIt = _mapPendingCall.find( response._requestId );
        if ( pendingIt != _mapPendingCall.end() )
        {
            const PendingCall pending = pendingIt->second;
            _mapPendingCall.erase( pendingIt );
            deliver( pending._onResponse, pending._requestId, response );
            return;
        }
        if ( _pSendingCall != nullptr ) // 보내지도 못했다(끊김 · 과부하) — sendRequest 안에서 불렸다
            deliver( _pSendingCall->_onResponse, _pSendingCall->_requestId, response );
    }

    void OnlineServiceClient::deliver( const OnlineResponseDelegate& onResponse, uint64 requestId, const NetResponse& response )
    {
        if ( onResponse.isBound() == false )
            return;
        OnlineResponse answer;
        answer._requestId = requestId;
        answer._status    = response._status;
        answer._pBody     = response._pBody;
        answer._bodySize  = response._bodySize;
        if ( response._status == NetRequestStatus::ApplicationError && response._bodySize >= 2 )
        {
            answer._errorCode = OnlineServiceClientInternal::readUint16( response._pBody );
            answer._pBody     = response._pBody + 2;
            answer._bodySize  = response._bodySize - 2;
        }
        else if ( response._status != NetRequestStatus::Ok )
        {
            answer._errorCode = OnlineError::kUnavailable;
        }
        onResponse( answer );
    }

    void OnlineServiceClient::dispatchPush( const uint8* pBody, int32 bodySize )
    {
        if ( bodySize < 2 )
            return;
        const uint16 kind  = OnlineServiceClientInternal::readUint16( pBody );
        const uint16 range = OnlineMethodRange::getRangeBase( kind );
        for ( IOnlineClientService* pService : _listService )
        {
            if ( pService->getMethodRange() != range )
                continue;
            BitReader reader( pBody + 2, bodySize - 2 );
            pService->onServicePush( kind, reader );
            return;
        }
    }
} // namespace sw
