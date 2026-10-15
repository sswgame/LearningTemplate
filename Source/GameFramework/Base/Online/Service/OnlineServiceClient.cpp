#include "pch.h"

#include "GameFramework/Base/Online/Service/OnlineServiceClient.h"

#include "Core/Container/LittleEndianUtil.h"
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
        , _nextRequestID{ 1 }
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
            vector<OnlineServiceClient*>& listClient = pService->_listRegisteredClient;
            listClient.erase( std::remove( listClient.begin(), listClient.end(), this ), listClient.end() );
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
        vector<OnlineServiceClient*>& listClient = pService->_listRegisteredClient;
        listClient.erase( std::remove( listClient.begin(), listClient.end(), this ), listClient.end() );
        const auto serviceIt = std::find( _listService.begin(), _listService.end(), pService );
        if ( serviceIt == _listService.end() )
            return;
        _listService.erase( serviceIt );
        cancelRequestsOf( pService->_registeredRange ); // 기반 소멸자에서 불리므로 getMethodRange(가상)는 부르지 않는다
    }

    void OnlineServiceClient::cancelRequestsOf( uint16 methodRange )
    {
        const auto queuedEnd = std::remove_if( _listQueuedCall.begin(), _listQueuedCall.end(),
                                               [methodRange]( const QueuedCall& call )
        { return OnlineMethodRange::getRangeBase( call._method ) == methodRange; } );
        _listQueuedCall.erase( queuedEnd, _listQueuedCall.end() );
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

    bool OnlineServiceClient::initialize( IStreamTransport* pTransport, const OnlineServiceClientSettings& settings, string& outError )
    {
        SW_ASSERT( _bInitialized == SW_FALSE );
        _settings                        = settings;
        _pTransport                      = pTransport;
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
                                                          const OnlineServiceClientSettings& settings )
    {
        SW_ASSERT( _bInitialized == SW_FALSE );
        const bool bInvalidClientArguments = pEndpoint == nullptr || pRequestClient == nullptr || handle.isValid() == false;
        if ( bInvalidClientArguments )
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

    void OnlineServiceClient::update( int64 nowMs )
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
        call._requestID        = _nextRequestID++;
        call._deadlineMs       = _nowMs + static_cast<int64>( options._timeoutSeconds * constant::kMillisecondsPerSecond );
        call._method           = method;
        const uint64 requestID = call._requestID;
        if ( _bInitialized == SW_FALSE || _state == OnlineClientState::VersionMismatch )
        {
            const bool     bMismatch = _state == OnlineClientState::VersionMismatch;
            OnlineResponse response;
            response._requestID = requestID;
            response._status    = bMismatch ? NetRequestStatus::ApplicationError : NetRequestStatus::ConnectionLost;
            response._errorCode = bMismatch ? OnlineError::kVersionMismatch : OnlineError::kUnavailable;
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
        notifyServices( &IOnlineClientService::onClientDisconnected );
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
            BitReader    reader( response._pBody, response._bodySize );
            const uint64 baseVersion = reader.readVarUint();
            const uint64 kitCount    = reader.readVarUint();
            for ( uint64 index = 0; index < kitCount && reader.hasOverflowed() == false; ++index )
            {
                (void)reader.readBits( 16 ); // 키트 항목은 건너뛴다 — 넘침은 아래 hasOverflowed 가 본다
                (void)reader.readVarUint();  // 키트 항목은 건너뛴다 — 넘침은 아래 hasOverflowed 가 본다
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
            notifyServices( &IOnlineClientService::onClientReady ); // 재접속 요청이 모은 요청보다 먼저 나간다
            flushQueuedCalls();
            return;
        }
        const bool bMismatch = response._status == NetRequestStatus::ApplicationError && response._bodySize >= 2 &&
                               LittleEndianUtil::readUint16( response._pBody ) == OnlineError::kVersionMismatch;
        if ( bMismatch )
        {
            SW_LOG_WARNING( "Online service protocol versions differ from the server - an update is required" );
            _state = OnlineClientState::VersionMismatch;
            failQueuedCalls( NetRequestStatus::ApplicationError, OnlineError::kVersionMismatch );
        }
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
            if ( std::find( _listService.begin(), _listService.end(), pService ) == _listService.end() )
                continue;
            ( pService->*pNotify )( *this );
        }
    }
} // namespace sw
