#include "pch.h"

#include "GameFramework/Base/Online/Service/OnlineServiceClient.h"

#include "Core/Container/LittleEndianUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Log/LogContext.h"
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
        };
    } // namespace
} // namespace sw

namespace sw
{
    OnlineServiceClient::OnlineServiceClient()
        : _settings{}
        , _listService{}
        , _listQueuedCall{}
        , _listDrainingCall{}
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
        , _schemaHash{ 0 }
        , _requestIDAllocator{}
        , _gateOwnerRange{ 0 }
        , _state{ OnlineClientState::Disconnected }
        , _bGateClosed{ SW_FALSE }
        , _bSharedEndpoint{ SW_FALSE }
        , _bInitialized{ SW_FALSE }
    {
    }

    IOnlineClientService::~IOnlineClientService()
    {
        while ( _listRegisteredClient.empty() == false )
        {
            _listRegisteredClient.back()->unregisterClientService( this ); // 목록에서 자기를 지운다
        }
    }

    OnlineServiceClient::~OnlineServiceClient()
    {
        shutdown();
        for ( IOnlineClientService* pService : _listService ) // 키트가 더 오래 살면 그 소멸자가 파괴된 이 객체를 부르지 않게 끊는다
        {
            VectorUtil::removeIf( pService->_listRegisteredClient, [this]( const OnlineServiceClient* pClient )
            { return pClient == this; } );
        }
    }

    bool OnlineServiceClient::registerClientService( IOnlineClientService* pService )
    {
        SW_ASSERT( pService != nullptr && _bInitialized == SW_FALSE );
        for ( const IOnlineClientService* pExisting : _listService )
        {
            if ( pExisting->getMethodRange() == pService->getMethodRange() )
                return false;
        }
        _listService.push_back( pService );
        pService->_listRegisteredClient.push_back( this );
        pService->_registeredRange = pService->getMethodRange();
        return true;
    }

    void OnlineServiceClient::unregisterClientService( IOnlineClientService* pService )
    {
        VectorUtil::removeIf( pService->_listRegisteredClient, [this]( const OnlineServiceClient* pClient )
        { return pClient == this; } );
        if ( VectorUtil::removeFirst( _listService, pService ) == 0 )
            return;
        cancelRequestsOf( pService->_registeredRange ); // 기반 소멸자에서 불리므로 getMethodRange(가상)는 부르지 않는다
    }

    void OnlineServiceClient::cancelRequestsOf( uint16 methodRange )
    {
        VectorUtil::removeIf( _listQueuedCall, [methodRange]( const QueuedCall& call )
        { return OnlineMethodRange::getRangeBase( call._method ) == methodRange; } );
        for ( vector<QueuedCall>* pDraining : _listDrainingCall ) // 꺼내 도는 목록은 지우지 않고 표시만 — 도는 쪽의 인덱스를 지킨다
        {
            for ( QueuedCall& call : *pDraining )
            {
                if ( OnlineMethodRange::getRangeBase( call._method ) == methodRange )
                    call._bCancelled = SW_TRUE;
            }
        }
        for ( auto pendingIt = _mapPendingCall.begin(); pendingIt != _mapPendingCall.end(); )
        {
            if ( OnlineMethodRange::getRangeBase( pendingIt->second._method ) == methodRange )
                pendingIt = _mapPendingCall.erase( pendingIt ); // 요청 클라이언트의 항목은 남는다 — 응답이 오면 onNetResponse 가 찾지 못해 버린다
            else
                ++pendingIt;
        }
    }

    bool OnlineServiceClient::initialize( IStreamTransport* pTransport, const OnlineServiceClientConfig& settings, string& outError )
    {
        SW_ASSERT( _bInitialized == SW_FALSE );
        _settings                        = settings;
        _pTransport                      = pTransport;
        _schemaHash                      = OnlineProtocolRegistry::computeSchemaHash();
        _ownedEndpoint                   = sw::make_unique<StreamMessageEndpoint>();
        _ownedRequestClient              = sw::make_unique<NetRequestClient>();
        const bool bTransportStartFailed = pTransport == nullptr || _ownedEndpoint->initialize( pTransport, settings._endpointSettings ) == false ||
                                           pTransport->initialize( _ownedEndpoint.get(), settings._transportSettings ) == false;
        if ( bTransportStartFailed )
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
                                                          const OnlineServiceClientConfig& settings )
    {
        SW_ASSERT( _bInitialized == SW_FALSE );
        const bool bInvalidClientArguments = pEndpoint == nullptr || pRequestClient == nullptr || handle.isValid() == false;
        if ( bInvalidClientArguments )
            return false;
        _settings        = settings;
        _schemaHash      = OnlineProtocolRegistry::computeSchemaHash();
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

    void OnlineServiceClient::update( MonotonicMilliseconds nowMs )
    {
        if ( _bInitialized == SW_FALSE )
            return;
        _nowMs = nowMs;
        if ( _bSharedEndpoint == SW_FALSE )
        {
            if ( _settings._transportSettings._ioThreadCount == 0 )
                (void)_pTransport->pollIO( 0 );
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
            deliver( call._onResponse, call._requestID, response );
        }
    }

    uint64 OnlineServiceClient::sendRequest( uint16 method, const BitWriter& body, const NetRequestOptions& options, OnlineResponseDelegate onResponse )
    {
        QueuedCall call;
        call._bodyBytes.assign( body.getBytes().begin(), body.getBytes().begin() + body.getByteCount() );
        call._options = options;
        if ( call._options._traceID.isValid() == false )
            call._options._traceID = LogContext::getCurrent()._traceID; // Hello 전에 모았다 보내도 부른 쪽의 추적 id 로
        call._onResponse       = onResponse;
        call._requestID        = _requestIDAllocator.allocate();
        call._deadlineMs       = _nowMs + static_cast<int64>( options._timeoutSeconds * constant::kMillisecondsPerSecond );
        call._method           = method;
        const uint64 requestID = call._requestID;
        if ( _bInitialized == SW_FALSE || isStopped() )
        {
            const bool     bStopped = isStopped();
            const uint16   stopCode = _state == OnlineClientState::VersionMismatch ? OnlineError::kVersionMismatch : OnlineError::kInvalidRequest;
            OnlineResponse response;
            response._requestID = requestID;
            response._status    = bStopped ? NetRequestStatus::ApplicationError : NetRequestStatus::ConnectionLost;
            response._errorCode = bStopped ? stopCode : OnlineError::kUnavailable;
            if ( onResponse.isBound() )
                onResponse( response );
            return requestID;
        }
        if ( _state != OnlineClientState::Ready || isGated( method ) )
        {
            _listQueuedCall.push_back( std::move( call ) );
            return requestID;
        }
        sendQueuedCall( call );
        return requestID;
    }

    uint64 OnlineServiceClient::rejectRequest( uint16 errorCode, OnlineResponseDelegate onResponse )
    {
        OnlineResponse response;
        response._requestID = _requestIDAllocator.allocate();
        response._status    = NetRequestStatus::ApplicationError;
        response._errorCode = errorCode;
        if ( onResponse.isBound() )
            onResponse( response );
        return response._requestID;
    }

    void OnlineServiceClient::setRequestGate( uint16 ownerRange, bool bClosed )
    {
        _gateOwnerRange = ownerRange;
        _bGateClosed    = bClosed ? SW_TRUE : SW_FALSE;
        if ( bClosed == false && _state == OnlineClientState::Ready )
            flushQueuedCalls();
    }

    bool OnlineServiceClient::isGated( uint16 method ) const
    {
        return _bGateClosed == SW_TRUE && OnlineMethodRange::getRangeBase( method ) != _gateOwnerRange;
    }

    bool OnlineServiceClient::isStopped() const
    {
        return _state == OnlineClientState::VersionMismatch || _state == OnlineClientState::HandshakeFailed;
    }

    void OnlineServiceClient::stopAfterHello( OnlineClientState state, uint16 errorCode )
    {
        _state = state;
        failQueuedCalls( NetRequestStatus::ApplicationError, errorCode );
        if ( _connection.isValid() )
            _pEndpoint->close( _connection, StreamCloseMode::Graceful ); // 서버도 닫지만 이쪽에서도 연결을 남기지 않는다. 닫힘 알림은 멈춘 상태라 다시 연결하지 않는다
    }

    void OnlineServiceClient::handleFrame( StreamFrameKind kind, const uint8* pBody, int32 bodySize )
    {
        if ( kind == StreamFrameKind::Message )
            dispatchPush( pBody, bodySize );
    }

    void OnlineServiceClient::handleClosed( StreamCloseReason reason )
    {
        (void)reason;
        if ( isStopped() == false )
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
        if ( isStopped() )
            return; // 다시 연결하지 않는다
        notifyServices( &IOnlineClientService::onClientDisconnected );
        _state         = OnlineClientState::Disconnected;
        _nextConnectMs = _nowMs + _backoffMs;
        _backoffMs     = _backoffMs <= 0 ? OnlineServiceClientInternal::kFirstBackoffMs : std::min( _backoffMs * 2, _settings._maxBackoffMs );
    }

    void OnlineServiceClient::sendHello()
    {
        // 몸: [스키마 해시 u64 리틀 엔디언][게임 빌드 blob][플랫폼 u8]. 해시는 B 가 아닌 고정 형식이라 스키마가 바뀌어도 서버가 늘 읽는다.
        BitWriter    rest;
        const size_t buildSize = std::min( _settings._gameBuild.size(), static_cast<size_t>( OnlineProtocolConstant::kMaxBuildTextSize ) );
        rest.writeBlob( reinterpret_cast<const uint8*>( _settings._gameBuild.data() ), static_cast<int32>( buildSize ) );
        rest.writeBits( _settings._platform, 8 );
        vector<uint8> helloBytes;
        LittleEndianUtil::appendUint64( helloBytes, _schemaHash );
        helloBytes.insert( helloBytes.end(), rest.getBytes().begin(), rest.getBytes().begin() + rest.getByteCount() );
        NetRequestOptions options;
        options._timeoutSeconds = 10.0;
        (void)_pRequestClient->sendRequest( _connection, OnlineHostMethod::kHello, helloBytes.data(), static_cast<int32>( helloBytes.size() ), options,
                                            Delegate<void( const NetResponse& )>::create<&OnlineServiceClient::onHelloResponse>( this ) );
    }

    void OnlineServiceClient::sendQueuedCall( QueuedCall& call )
    {
        const int64       remainingMs = std::max<int64>( 1, call._deadlineMs - _nowMs );
        NetRequestOptions options     = call._options;
        options._timeoutSeconds       = static_cast<float64>( remainingMs ) / constant::kMillisecondsPerSecond;
        _pSendingCall                 = &call;
        const uint64 netRequestID     = _pRequestClient->sendRequest( _connection, call._method, call._bodyBytes.data(), static_cast<int32>( call._bodyBytes.size() ),
                                                                      options, Delegate<void( const NetResponse& )>::create<&OnlineServiceClient::onNetResponse>( this ) );
        _pSendingCall                 = nullptr;
        if ( netRequestID != 0 )
            _mapPendingCall[netRequestID] = PendingCall{ call._onResponse, call._requestID, call._method };
    }

    void OnlineServiceClient::flushQueuedCalls()
    {
        vector<QueuedCall> listCall = std::move( _listQueuedCall );
        _listQueuedCall.clear();
        _listDrainingCall.push_back( &listCall ); // 콜백이 키트를 파괴하면 cancelRequestsOf 가 남은 항목을 표시한다
        for ( size_t index = 0; index < listCall.size(); ++index )
        {
            QueuedCall& call = listCall[index];
            if ( call._bCancelled == SW_TRUE )
                continue;
            if ( _state == OnlineClientState::Ready && isGated( call._method ) == false )
                sendQueuedCall( call );
            else
                _listQueuedCall.push_back( std::move( call ) ); // 보내는 도중 끊겼다 · 문이 닫혔다 — 다음 Hello · 문 열림을 기다린다
        }
        _listDrainingCall.pop_back();
    }

    void OnlineServiceClient::failQueuedCalls( NetRequestStatus status, uint16 errorCode )
    {
        vector<QueuedCall> listCall = std::move( _listQueuedCall );
        _listQueuedCall.clear();
        _listDrainingCall.push_back( &listCall ); // 콜백이 키트를 파괴하면 cancelRequestsOf 가 남은 항목을 표시한다
        for ( size_t index = 0; index < listCall.size(); ++index )
        {
            const QueuedCall& call = listCall[index];
            if ( call._bCancelled == SW_TRUE )
                continue;
            OnlineResponse response;
            response._requestID = call._requestID;
            response._status    = status;
            response._errorCode = errorCode;
            if ( call._onResponse.isBound() )
                call._onResponse( response );
        }
        _listDrainingCall.pop_back();
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
            BitReader reader( response._pBody, response._bodySize );
            _remoteConfigHash = reader.readVarUint();
            _serverTimeMs     = reader.readVarInt();
            if ( reader.hasOverflowed() )
            {
                SW_LOG_ERROR( "Online service Hello answer is malformed - the handshake failed" );
                stopAfterHello( OnlineClientState::HandshakeFailed, OnlineError::kInvalidRequest );
                return;
            }
            _state     = OnlineClientState::Ready;
            _backoffMs = 0;
            notifyServices( &IOnlineClientService::onClientReady ); // 재접속 요청이 모은 요청보다 먼저 나간다
            flushQueuedCalls();
            return;
        }
        // 연결이 닫혔거나 이쪽이 취소했으면 닫힘 처리가 다시 연결을 맡는다.
        const bool bClosing = response._status == NetRequestStatus::ConnectionLost || response._status == NetRequestStatus::Cancelled;
        if ( bClosing )
            return;
        // 시한 초과 · 과부하는 지나가는 일이다. 연결을 닫아 물러남 뒤 다시 연결하게 한다.
        const bool bTransient = response._status == NetRequestStatus::DeadlineExceeded || response._status == NetRequestStatus::Overloaded;
        if ( bTransient )
        {
            SW_LOG_WARNING( "Online service Hello got no answer (%#) - reconnecting after a backoff", toString( response._status ) );
            if ( _connection.isValid() )
                _pEndpoint->close( _connection, StreamCloseMode::Graceful );
            return;
        }
        const bool   bApplicationError = response._status == NetRequestStatus::ApplicationError && response._bodySize >= 2;
        const uint16 errorCode         = bApplicationError ? LittleEndianUtil::readUint16( response._pBody ) : OnlineError::kOk;
        const bool   bMismatch         = errorCode == OnlineError::kVersionMismatch && response._bodySize == 2 + OnlineProtocolConstant::kSchemaHashSize;
        if ( bMismatch )
        {
            const uint64 serverHash = LittleEndianUtil::readUint64( response._pBody + 2 );
            SW_LOG_WARNING( "Online schema hash %# differs from the server's %# - an update is required", _schemaHash, serverHash );
            stopAfterHello( OnlineClientState::VersionMismatch, OnlineError::kVersionMismatch );
            return;
        }
        // 그 밖의 답(다른 오류 코드, 서버 해시가 없는 거절, 모르는 메서드)은 같은 빌드에서 나오지 않는다. 기다리지 않고 멈춘다.
        SW_LOG_ERROR( "Online service Hello was refused (%#, error %#, %# bytes) - the handshake failed", toString( response._status ), static_cast<uint32>( errorCode ),
                      response._bodySize );
        stopAfterHello( OnlineClientState::HandshakeFailed, OnlineError::kInvalidRequest );
    }

    void OnlineServiceClient::onNetResponse( const NetResponse& response )
    {
        const auto pendingIt = _mapPendingCall.find( response._requestID );
        if ( pendingIt != _mapPendingCall.end() )
        {
            const PendingCall pending = pendingIt->second;
            _mapPendingCall.erase( pendingIt );
            deliver( pending._onResponse, pending._requestID, response );
            return;
        }
        if ( _pSendingCall != nullptr ) // 보내지도 못했다(끊김 · 과부하) — sendRequest 안에서 불렸다
            deliver( _pSendingCall->_onResponse, _pSendingCall->_requestID, response );
    }

    void OnlineServiceClient::deliver( const OnlineResponseDelegate& onResponse, uint64 requestID, const NetResponse& response )
    {
        if ( onResponse.isBound() == false )
            return;
        OnlineResponse answer;
        answer._requestID = requestID;
        answer._status    = response._status;
        answer._pBody     = response._pBody;
        answer._bodySize  = response._bodySize;
        if ( response._status == NetRequestStatus::ApplicationError && response._bodySize >= 2 )
        {
            answer._errorCode = LittleEndianUtil::readUint16( response._pBody );
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
        const uint16 kind  = LittleEndianUtil::readUint16( pBody );
        const uint16 range = OnlineMethodRange::getRangeBase( kind );
        for ( IOnlineClientService* pService : _listService )
        {
            if ( pService->getMethodRange() != range )
                continue;
            BitReader reader( pBody + 2, bodySize - 2 );
            pService->onServicePush( kind, reader ); // 하나만 부르고 곧 나간다 — 콜백이 목록을 바꿔도 순회를 잇지 않는다
            return;
        }
    }

    void OnlineServiceClient::notifyServices( void ( IOnlineClientService::*pNotify )( OnlineServiceClient& ) )
    {
        // 콜백이 키트를 파괴하면 소멸자가 _listService 에서 지우므로 사본을 돈다(키트 수는 몇 개뿐).
        // 부르기 직전에 아직 등록돼 있는지 본다. 순회 중 새로 등록되는 키트는 없다 — registerClientService 는 초기화 전에만 된다.
        const vector<IOnlineClientService*> listService = _listService;
        for ( IOnlineClientService* pService : listService )
        {
            if ( VectorUtil::contains( _listService, pService ) == false )
                continue;
            ( pService->*pNotify )( *this );
        }
    }
} // namespace sw
