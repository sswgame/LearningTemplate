#include "pch.h"

#include "GameFramework/Base/Online/Service/OnlineServiceHost.h"

#include "Core/Container/LittleEndianUtil.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Log/LogContext.h"
#include "Core/Network/Transport/IStreamTransport.h"

#include "GameFramework/Base/Online/Config/RemoteConfig.h"
#include "GameFramework/Base/Online/Guard/RequestLimits.h"
#include "GameFramework/Base/Online/Service/OnlineProtocol.h"
#include "GameFramework/Base/Online/Store/ServiceStore.h"

namespace sw
{
    namespace
    {
        struct OnlineServiceHostInternal
        {
            static constexpr size_t kMaxTrackedBucketCount = 100000;
            static constexpr int64  kHelloSweepIntervalMs  = 250; ///< Hello 기한을 넘긴 연결을 찾는 간격 — 연결 표를 틱마다 돌지 않는다

            /** @brief 주소마다의 도배 제한 단위 — 포트는 빼고 IP 만(포트를 바꿔 새 연결로 우회하지 못하게). */
            static uint64 makeRemoteKey( const NetAddress& remote ) { return static_cast<uint64>( remote._ipv4 ) + 1; }

            /** @brief 노출이 @p exposure 인 서비스가 이 연결의 요청을 받는지입니다. GM 서비스는 GM 리슨의 연결만 받는다(GM 리슨이 없으면 기동이 멈춘다). */
            static bool isServedOn( OnlineServiceExposure exposure, bool bAdminConnection )
            {
                switch ( exposure )
                {
                    case OnlineServiceExposure::Player:
                        return bAdminConnection == false;
                    case OnlineServiceExposure::Admin:
                        return bAdminConnection;
                    case OnlineServiceExposure::PlayerAndAdmin:
                        return true;
                }
                return false;
            }

            /** @brief GM 리슨 없이 GM 서비스(`Admin`)를 올리려 하면 오류 문장을 채우고 false 입니다. 플레이어 포트로 받는 숨은 폴백은 두지 않는다. */
            [[nodiscard]] static bool checkExposure( const IOnlineHostService& service, bool bHasAdminListener, string& outError )
            {
                if ( bHasAdminListener || service.getExposure() != OnlineServiceExposure::Admin )
                    return true;
                outError = "online service range " + to_string( static_cast<uint32>( service.getMethodRange() ) ) +
                           " is admin-only but the host has no admin listener (set the server config _adminServicePort)";
                return false;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    /**
     * @class OnlineServiceHost::ListenerTransport
     * @brief 리슨마다 빌린 전송 하나(플레이어 · GM)를 묶어 전송 하나로 보입니다.
     * @details - 전송 계약(`IStreamTransport`)은 리슨이 하나라서 IOCP · epoll · 루프백 구현을 고치지 않고 전송 여럿을 묶는다.
     *          - 바깥 핸들의 슬롯 번호는 `안쪽 슬롯 × 리슨 수 + 리슨` 이다. 두 전송의 핸들이 겹치지 않고, 연결이 들어온 리슨을 핸들만으로 안다(`isAdminConnection`).
     *            세대는 그대로 두므로 무효 핸들(세대 0)은 무효로 남는다.
     *          - 콜백은 각 전송의 I/O 스레드에서 온다. 리슨마다 둔 중계(`ChildHandler`)는 바뀌지 않는 값만 읽으므로 겹쳐 불려도 된다.
     */
    class OnlineServiceHost::ListenerTransport final : public IStreamTransport
    {
    public:
        static constexpr uint32 kPlayerListener = 0;
        static constexpr uint32 kAdminListener  = 1;
        static constexpr uint32 kMaxListener    = 2;

        ListenerTransport( IStreamTransport* pPlayerTransport, IStreamTransport* pAdminTransport )
            : _arrHandler{}
            , _arrTransport{ pPlayerTransport, pAdminTransport }
            , _pHandler{ nullptr }
            , _listenerCount{ pAdminTransport != nullptr ? kMaxListener : 1 }
        {
            SW_ASSERT( pPlayerTransport != nullptr );
            for ( uint32 listener = 0; listener < kMaxListener; ++listener )
            {
                _arrHandler[listener]._pOwner   = this;
                _arrHandler[listener]._listener = listener;
            }
        }

        bool hasAdminListener() const { return _listenerCount == kMaxListener; }
        bool isAdminConnection( StreamConnectionHandle handle ) const { return handle._index % _listenerCount == kAdminListener; }

        /** @brief GM 리슨에서 받기 시작합니다. GM 전송이 없으면 false 입니다. */
        [[nodiscard]] bool listenAdmin( const NetAddress& bindAddress ) { return hasAdminListener() && _arrTransport[kAdminListener]->listen( bindAddress ); }
        uint16             getAdminListenPort() const { return hasAdminListener() ? _arrTransport[kAdminListener]->getListenPort() : 0; }

        bool initialize( IStreamHandler* pHandler, const StreamTransportConfig& settings ) override
        {
            _pHandler = pHandler;
            for ( uint32 listener = 0; listener < _listenerCount; ++listener )
            {
                if ( _arrTransport[listener]->initialize( &_arrHandler[listener], settings ) == false )
                    return false; // 이미 띄운 전송은 호스트의 정리(`shutdown`)가 내린다
            }
            return true;
        }

        void shutdown() override
        {
            for ( uint32 listener = _listenerCount; listener > 0; --listener )
            {
                _arrTransport[listener - 1]->shutdown();
            }
        }

        /** @brief 플레이어 리슨에서 받기 시작합니다. GM 리슨은 `listenAdmin` 입니다. */
        bool   listen( const NetAddress& bindAddress ) override { return _arrTransport[kPlayerListener]->listen( bindAddress ); }
        uint16 getListenPort() const override { return _arrTransport[kPlayerListener]->getListenPort(); }

        StreamConnectionHandle connect( const NetAddress& remote ) override { return makeOuterHandle( kPlayerListener, _arrTransport[kPlayerListener]->connect( remote ) ); }

        StreamSendResult send( StreamConnectionHandle handle, const uint8* pData, int32 size ) override
        {
            return getTransport( handle ).send( makeInnerHandle( handle ), pData, size );
        }

        void close( StreamConnectionHandle handle, StreamCloseMode mode ) override { getTransport( handle ).close( makeInnerHandle( handle ), mode ); }

        void setReceivePaused( StreamConnectionHandle handle, bool bPaused ) override { getTransport( handle ).setReceivePaused( makeInnerHandle( handle ), bPaused ); }

        int32 pollIO( int32 timeoutMilli ) override
        {
            int32 completionCount = 0;
            for ( uint32 listener = 0; listener < _listenerCount; ++listener )
            {
                completionCount += _arrTransport[listener]->pollIO( timeoutMilli );
            }
            return completionCount;
        }

        NetAddress getRemoteAddress( StreamConnectionHandle handle ) const override
        {
            return getTransport( handle ).getRemoteAddress( makeInnerHandle( handle ) );
        }

        StreamTransportStats getStats() const override
        {
            StreamTransportStats total;
            for ( uint32 listener = 0; listener < _listenerCount; ++listener )
            {
                const StreamTransportStats stats = _arrTransport[listener]->getStats();
                total._acceptedCount += stats._acceptedCount;
                total._connectedCount += stats._connectedCount;
                total._closedCount += stats._closedCount;
                total._receivedBytes += stats._receivedBytes;
                total._sentBytes += stats._sentBytes;
                total._receiveCallCount += stats._receiveCallCount;
                total._sendCallCount += stats._sendCallCount;
                total._openCount += stats._openCount;
            }
            return total;
        }

    private:
        /** @brief 전송 하나의 콜백을 바깥 핸들로 바꿔 넘기는 중계입니다. */
        struct ChildHandler final : public IStreamHandler
        {
            ListenerTransport* _pOwner{ nullptr };
            uint32             _listener{ 0 };

            void onStreamOpened( StreamConnectionHandle handle, const NetAddress& remote, bool bAccepted ) override
            {
                _pOwner->_pHandler->onStreamOpened( _pOwner->makeOuterHandle( _listener, handle ), remote, bAccepted );
            }
            void onStreamReceived( StreamConnectionHandle handle, const uint8* pData, int32 size ) override
            {
                _pOwner->_pHandler->onStreamReceived( _pOwner->makeOuterHandle( _listener, handle ), pData, size );
            }
            void onStreamWritable( StreamConnectionHandle handle ) override { _pOwner->_pHandler->onStreamWritable( _pOwner->makeOuterHandle( _listener, handle ) ); }
            void onStreamClosed( StreamConnectionHandle handle, StreamCloseReason reason ) override
            {
                _pOwner->_pHandler->onStreamClosed( _pOwner->makeOuterHandle( _listener, handle ), reason );
            }
        };

        StreamConnectionHandle makeOuterHandle( uint32 listener, StreamConnectionHandle inner ) const
        {
            SW_ASSERT( inner._index <= ( invalid_index::kUint32 - listener ) / _listenerCount );
            return StreamConnectionHandle::make( inner._index * _listenerCount + listener, inner._generation );
        }

        StreamConnectionHandle makeInnerHandle( StreamConnectionHandle outer ) const { return StreamConnectionHandle::make( outer._index / _listenerCount, outer._generation ); }

        IStreamTransport& getTransport( StreamConnectionHandle outer ) const { return *_arrTransport[outer._index % _listenerCount]; }

        ChildHandler      _arrHandler[kMaxListener];
        IStreamTransport* _arrTransport[kMaxListener];
        IStreamHandler*   _pHandler;
        uint32            _listenerCount;
    };
} // namespace sw

namespace sw
{
    OnlineServiceHost::OnlineServiceHost()
        : _settings{}
        , _endpoint{}
        , _requestServer{}
        , _ephemeralRouter{}
        , _accountBucket{}
        , _remoteBucket{}
        , _listService{}
        , _listBusSubscription{}
        , _listBusScratch{}
        , _responseBytes{}
        , _mapConnection{}
        , _mapAccountToConnection{}
        , _mapRemoteConnectionCount{}
        , _listHandleScratch{}
        , _pServerBus{}
        , _listenerTransport{}
        , _nowMs{ 0 }
        , _nextHelloSweepMs{ 0 }
        , _schemaHash{ 0 }
        , _bInitialized{ SW_FALSE }
    {
    }

    OnlineServiceHost::~OnlineServiceHost()
    {
        shutdown();
    }

    bool OnlineServiceHost::initialize( IStreamTransport* pTransport, const OnlineServiceHostConfig& settings, string& outError )
    {
        SW_ASSERT( _bInitialized == SW_FALSE );
        if ( pTransport == nullptr )
        {
            outError = "online service host needs a stream transport";
            return false;
        }
        _settings          = settings;
        _listenerTransport = make_unique<ListenerTransport>( pTransport, settings._pAdminTransport );
        _pServerBus        = settings._pServerBusTransport != nullptr ? make_unique<ServerBus>( *settings._pServerBusTransport ) : nullptr;
        _schemaHash        = OnlineProtocolRegistry::computeSchemaHash(); // 서비스를 올린 뒤 기동 때 한 번. 같은 빌드의 클라이언트와 같은 값이다
        _accountBucket.initialize( settings._requestBurstPerAccount, settings._requestRefillMsPerAccount, OnlineServiceHostInternal::kMaxTrackedBucketCount );
        _remoteBucket.initialize( settings._requestBurstPerRemote, settings._requestRefillMsPerRemote, OnlineServiceHostInternal::kMaxTrackedBucketCount );
        if ( _endpoint.initialize( _listenerTransport.get(), settings._endpointSettings ) == false )
        {
            outError = "online service host could not set up its stream endpoint";
            releaseTransportSide();
            return false;
        }
        NetRequestServerConfig requestSettings;
        _requestServer.initialize( &_endpoint, requestSettings );
        if ( _requestServer.registerMethod( OnlineHostMethod::kHello, this ) == false || _requestServer.registerMethod( OnlineHostMethod::kPing, this ) == false )
        {
            outError = "online service host methods are already registered";
            releaseTransportSide();
            return false;
        }
        for ( IOnlineHostService* pService : _listService )
        {
            if ( OnlineServiceHostInternal::checkExposure( *pService, _listenerTransport->hasAdminListener(), outError ) == false )
            {
                releaseTransportSide();
                return false;
            }
            for ( uint16 offset = 0; offset < OnlineMethodRange::kMethodCount; ++offset )
            {
                (void)_requestServer.registerMethod( static_cast<uint16>( pService->getMethodRange() + offset ), this ); // registerHostService 가 겹침을 이미 봤다
            }
        }
        if ( _listenerTransport->initialize( &_endpoint, settings._transportSettings ) == false || _listenerTransport->listen( settings._listenAddress ) == false )
        {
            outError = "online service host could not listen on " + settings._listenAddress.toString();
            releaseTransportSide(); // 이미 선 엔드포인트 · 요청 서버 · 전송을 해제한다 — `_bInitialized` 가 거짓이라 `shutdown` 은 아무것도 하지 않는다
            return false;
        }
        const bool bAdminListenFailed = _listenerTransport->hasAdminListener() && _listenerTransport->listenAdmin( settings._adminListenAddress ) == false;
        if ( bAdminListenFailed )
        {
            outError = "online service host could not listen for the admin port on " + settings._adminListenAddress.toString();
            releaseTransportSide();
            return false;
        }
        if ( settings._pEphemeralStore != nullptr )
            _ephemeralRouter.initialize( settings._pEphemeralStore );
        _bInitialized = SW_TRUE;
        return true;
    }

    void OnlineServiceHost::shutdown()
    {
        if ( _bInitialized == SW_FALSE )
            return;
        _bInitialized = SW_FALSE;
        _ephemeralRouter.shutdown(); // 기다리던 캐시 요청은 Unavailable 로 한 번씩 — 서비스를 내리기 전
        // 서비스가 가진 호스트 포인터 · 구독을 여기서 분리하게 한다 — 서비스 객체는 호스트보다 늦게 종료돼도 된다(사라진 호스트를 부르지 않는다).
        for ( size_t index = _listService.size(); index > 0; --index )
        {
            _listService[index - 1]->onHostShutdown( *this );
        }
        for ( const BusSubscription& subscription : _listBusSubscription )
        {
            _settings._pServerBusTransport->unsubscribe( subscription._topic );
        }
        _listBusSubscription.clear();
        releaseTransportSide();
    }

    void OnlineServiceHost::releaseTransportSide()
    {
        if ( _listenerTransport != nullptr )
            _listenerTransport->shutdown();
        _requestServer.shutdown();
        _endpoint.shutdown();
        _mapConnection.clear();
        _mapRemoteConnectionCount.clear();
        _mapAccountToConnection.clear();
        _listenerTransport.reset(); // 엔드포인트가 내려간 뒤 — 엔드포인트는 이 묶음을 전송으로 들고 있었다
    }

    bool OnlineServiceHost::registerHostService( IOnlineHostService* pService )
    {
        SW_ASSERT( pService != nullptr );
        const uint16 range = pService->getMethodRange();
        if ( OnlineMethodRange::getRangeBase( range ) != range || range == OnlineMethodRange::kHost )
        {
            SW_LOG_ERROR( "Online service range %# is not a range base (a multiple of 0x100 above the host range)", static_cast<uint32>( range ) );
            return false;
        }
        for ( const IOnlineHostService* pExisting : _listService )
        {
            if ( pExisting->getMethodRange() == range )
            {
                SW_LOG_ERROR( "Online service range %# is already taken - two kits claim one method range", static_cast<uint32>( range ) );
                return false;
            }
        }
        if ( _bInitialized == SW_TRUE )
        {
            string exposureError;
            if ( OnlineServiceHostInternal::checkExposure( *pService, _listenerTransport->hasAdminListener(), exposureError ) == false )
            {
                SW_LOG_ERROR( "%#", exposureError );
                return false;
            }
            for ( uint16 offset = 0; offset < OnlineMethodRange::kMethodCount; ++offset )
            {
                if ( _requestServer.registerMethod( static_cast<uint16>( range + offset ), this ) )
                    continue;
                for ( uint16 undo = 0; undo < offset; ++undo )
                {
                    _requestServer.unregisterMethod( static_cast<uint16>( range + undo ) );
                }
                return false;
            }
        }
        _listService.push_back( pService );
        return true;
    }

    void OnlineServiceHost::update( int64 nowMs )
    {
        if ( _bInitialized == SW_FALSE )
            return;
        _nowMs = nowMs;
        if ( _settings._transportSettings._ioThreadCount == 0 )
            (void)_listenerTransport->pollIO( 0 );
        (void)_endpoint.pump( *this );
        closeExpiredHelloConnections();
        _requestServer.update();
        (void)_ephemeralRouter.pump();
        if ( _settings._pServerBusTransport != nullptr )
        {
            _listBusScratch.clear();
            (void)_settings._pServerBusTransport->pollMessages( _listBusScratch );
            for ( const ServerBusMessage& message : _listBusScratch )
            {
                dispatchServerBusMessage( message );
            }
        }
        if ( _settings._pServiceStore != nullptr )
            (void)_settings._pServiceStore->pollCompletions(); // 끝난 저장 일의 complete 가 맡긴 서비스를 부른다 — 서비스 틱보다 먼저
        for ( IOnlineHostService* pService : _listService )
        {
            pService->onServiceTick( *this, nowMs );
        }
    }

    bool OnlineServiceHost::respondOk( const NetRequestToken& token, const BitWriter& body )
    {
        const vector<uint8>& bodyBytes = body.getBytes();
        return _requestServer.respond( token, NetRequestStatus::Ok, bodyBytes.data(), body.getByteCount() );
    }

    bool OnlineServiceHost::respondError( const NetRequestToken& token, uint16 errorCode, const BitWriter* pDetail )
    {
        _responseBytes.clear();
        LittleEndianUtil::appendUint16( _responseBytes, errorCode );
        if ( pDetail != nullptr )
            _responseBytes.insert( _responseBytes.end(), pDetail->getBytes().begin(), pDetail->getBytes().begin() + pDetail->getByteCount() );
        return _requestServer.respond( token, NetRequestStatus::ApplicationError, _responseBytes.data(), static_cast<int32>( _responseBytes.size() ) );
    }

    bool OnlineServiceHost::bindAccount( StreamConnectionHandle connection, AccountID accountID )
    {
        const auto connectionIt = _mapConnection.find( connection.packed() );
        if ( connectionIt == _mapConnection.end() || accountID == kInvalidAccountID )
            return false;
        const auto boundIt = _mapAccountToConnection.find( accountID );
        if ( boundIt != _mapAccountToConnection.end() && boundIt->second != connection )
            return false;
        if ( connectionIt->second._accountID != kInvalidAccountID && connectionIt->second._accountID != accountID )
            return false; // 한 연결에 계정 하나
        const bool bNewlyBound             = boundIt == _mapAccountToConnection.end();
        connectionIt->second._accountID    = accountID;
        _mapAccountToConnection[accountID] = connection;
        _requestServer.setPrincipal( connection, accountID ); // 멱등 범위 = 계정(재접속한 재시도도 잡힌다)
        if ( bNewlyBound )
        {
            for ( IOnlineHostService* pService : _listService )
            {
                pService->onAccountJoined( *this, accountID );
            }
        }
        return true;
    }

    void OnlineServiceHost::unbindAccount( AccountID accountID )
    {
        const auto boundIt = _mapAccountToConnection.find( accountID );
        if ( boundIt == _mapAccountToConnection.end() )
            return;
        const StreamConnectionHandle connection = boundIt->second;
        _mapAccountToConnection.erase( boundIt );
        const auto connectionIt = _mapConnection.find( connection.packed() );
        if ( connectionIt != _mapConnection.end() )
            connectionIt->second._accountID = kInvalidAccountID;
        _endpoint.close( connection, StreamCloseMode::Graceful );
        notifyAccountLeft( accountID );
    }

    bool OnlineServiceHost::findConnection( AccountID accountID, StreamConnectionHandle& outConnection ) const
    {
        const auto boundIt = _mapAccountToConnection.find( accountID );
        if ( boundIt == _mapAccountToConnection.end() )
            return false;
        outConnection = boundIt->second;
        return true;
    }

    bool OnlineServiceHost::sendPush( AccountID accountID, uint16 kind, const BitWriter& body )
    {
        StreamConnectionHandle connection;
        if ( findConnection( accountID, connection ) == false )
            return false;
        return sendPushToConnection( connection, kind, body );
    }

    int32 OnlineServiceHost::sendPushToAll( uint16 kind, const BitWriter& body )
    {
        int32 sentCount = 0;
        for ( const auto& [accountID, connection] : _mapAccountToConnection )
        {
            (void)accountID;
            sentCount += sendPushToConnection( connection, kind, body ) ? 1 : 0;
        }
        return sentCount;
    }

    EphemeralStoreRouter* OnlineServiceHost::getEphemeralRouter()
    {
        return _settings._pEphemeralStore != nullptr ? &_ephemeralRouter : nullptr;
    }

    void OnlineServiceHost::subscribeServerBus( string_view topic, IOnlineHostService* pService )
    {
        if ( _settings._pServerBusTransport == nullptr )
            return;
        bool bTopicOpen = false;
        for ( const BusSubscription& subscription : _listBusSubscription )
        {
            if ( subscription._topic == topic && subscription._pService == pService )
                return;
            bTopicOpen = bTopicOpen || subscription._topic == topic;
        }
        if ( bTopicOpen == false )
            _settings._pServerBusTransport->subscribe( topic );
        _listBusSubscription.push_back( BusSubscription{ string( topic ), pService } );
    }

    void OnlineServiceHost::unsubscribeServerBus( string_view topic, IOnlineHostService* pService )
    {
        bool bTopicStillOpen = false;
        bool bRemoved        = false;
        for ( size_t index = 0; index < _listBusSubscription.size(); )
        {
            if ( _listBusSubscription[index]._topic == topic && _listBusSubscription[index]._pService == pService )
            {
                _listBusSubscription.erase( _listBusSubscription.begin() + static_cast<ptrdiff_t>( index ) );
                bRemoved = true;
                continue;
            }
            bTopicStillOpen = bTopicStillOpen || _listBusSubscription[index]._topic == topic;
            ++index;
        }
        const bool bUnsubscribeTopic = bRemoved && bTopicStillOpen == false && _settings._pServerBusTransport != nullptr;
        if ( bUnsubscribeTopic )
            _settings._pServerBusTransport->unsubscribe( topic );
    }

    uint16 OnlineServiceHost::getListenPort() const
    {
        return _listenerTransport != nullptr ? _listenerTransport->getListenPort() : 0;
    }

    uint16 OnlineServiceHost::getAdminListenPort() const
    {
        return _listenerTransport != nullptr ? _listenerTransport->getAdminListenPort() : 0;
    }

    void OnlineServiceHost::onEndpointOpened( StreamConnectionHandle handle, const NetAddress& remote, bool bAccepted )
    {
        (void)bAccepted;
        ConnectionState& connection = _mapConnection[handle.packed()];
        connection                  = ConnectionState{};
        connection._remoteKey       = OnlineServiceHostInternal::makeRemoteKey( remote );
        connection._openedMs        = _nowMs;
        connection._bAdminListener  = _listenerTransport->isAdminConnection( handle ) ? SW_TRUE : SW_FALSE;
        // 주소마다 동시 연결 상한 — 전송의 `_maxConnections` 는 전체 상한이라 한 주소가 슬롯을 다 잡을 수 있다. 닫힘(`onEndpointClosed`)이 수를 되돌린다.
        const int32 remoteCount = ++_mapRemoteConnectionCount[connection._remoteKey];
        if ( remoteCount > _settings._maxConnectionsPerRemote )
        {
            connection._bClosing = SW_TRUE;
            _endpoint.close( handle, StreamCloseMode::Abort );
        }
    }

    void OnlineServiceHost::closeExpiredHelloConnections()
    {
        // Hello 를 끝내지 않은 연결은 짧은 기한 안에 닫는다 — 전송의 쉼 시한(60 초)까지 슬롯을 잡아 두는 열기만 하는 연결을 막는다.
        if ( _nowMs < _nextHelloSweepMs )
            return;
        _nextHelloSweepMs = _nowMs + OnlineServiceHostInternal::kHelloSweepIntervalMs;
        _listHandleScratch.clear();
        for ( auto& [packed, connection] : _mapConnection )
        {
            const bool bExpired = connection._bHelloDone == SW_FALSE && connection._bClosing == SW_FALSE && _nowMs - connection._openedMs > _settings._helloTimeoutMs;
            if ( bExpired == false )
                continue;
            connection._bClosing = SW_TRUE;
            _listHandleScratch.push_back( StreamConnectionHandle::fromPacked( packed ) );
        }
        for ( const StreamConnectionHandle handle : _listHandleScratch )
        {
            _endpoint.close( handle, StreamCloseMode::Abort ); // 닫힘 콜백이 표를 지우므로 표를 돌던 중에는 부르지 않는다
        }
    }

    void OnlineServiceHost::onEndpointFrame( StreamConnectionHandle handle, StreamFrameKind kind, const uint8* pBody, int32 bodySize )
    {
        if ( _requestServer.handleFrame( handle, kind, pBody, bodySize ) )
            return;
        // 클라이언트 → 서버 Message 프레임은 이 틀에 없다(요청-응답만) — 버린다.
    }

    void OnlineServiceHost::onEndpointClosed( StreamConnectionHandle handle, StreamCloseReason reason )
    {
        (void)reason;
        _requestServer.onConnectionClosed( handle );
        const auto connectionIt = _mapConnection.find( handle.packed() );
        if ( connectionIt == _mapConnection.end() )
            return;
        const AccountID accountID = connectionIt->second._accountID;
        const auto      remoteIt  = _mapRemoteConnectionCount.find( connectionIt->second._remoteKey );
        if ( remoteIt != _mapRemoteConnectionCount.end() && --remoteIt->second <= 0 )
            _mapRemoteConnectionCount.erase( remoteIt );
        _mapConnection.erase( connectionIt );
        if ( accountID == kInvalidAccountID )
            return;
        const auto boundIt = _mapAccountToConnection.find( accountID );
        if ( boundIt != _mapAccountToConnection.end() && boundIt->second == handle )
        {
            _mapAccountToConnection.erase( boundIt );
            notifyAccountLeft( accountID );
        }
    }

    void OnlineServiceHost::onNetRequest( NetRequestServer& server, const NetRequestContext& context )
    {
        (void)server;
        const auto connectionIt = _mapConnection.find( context._token._handle.packed() );
        if ( connectionIt == _mapConnection.end() )
            return; // 닫히는 연결
        ConnectionState& connection = connectionIt->second;
        if ( connection._bClosing == SW_TRUE )
            return; // 상한 · Hello 기한으로 닫는 중 — 답하지 않는다
        const uint16 method = context._token._method;
        if ( method == OnlineHostMethod::kHello )
        {
            handleHello( context, connection );
            return;
        }
        if ( connection._bHelloDone == SW_FALSE || context._bodySize > RequestLimits::kMaxRequestBodySize )
        {
            (void)respondError( context._token, OnlineError::kInvalidRequest );
            return;
        }
        // 도배 제한 — 로그인했으면 계정마다, 아니면 주소마다.
        int64      retryAfterMs = 0;
        const bool bAllowed     = connection._accountID != kInvalidAccountID ? _accountBucket.tryConsume( connection._accountID, _nowMs, retryAfterMs )
                                                                             : _remoteBucket.tryConsume( connection._remoteKey, _nowMs, retryAfterMs );
        if ( bAllowed == false )
        {
            BitWriter detail;
            detail.writeVarUint( static_cast<uint64>( retryAfterMs ) );
            (void)respondError( context._token, OnlineError::kRateLimited, &detail );
            return;
        }
        if ( method == OnlineHostMethod::kPing )
        {
            BitWriter body;
            body.writeVarInt( _nowMs );
            (void)respondOk( context._token, body );
            return;
        }
        IOnlineHostService* pService = findService( method );
        const bool          bServed  = pService != nullptr && OnlineServiceHostInternal::isServedOn( pService->getExposure(), connection._bAdminListener == SW_TRUE );
        if ( bServed == false )
        {
            (void)respondError( context._token, OnlineError::kInvalidRequest );
            return;
        }
        if ( connection._accountID == kInvalidAccountID && pService->isAnonymousMethod( method ) == false )
        {
            (void)respondError( context._token, OnlineError::kUnauthenticated );
            return;
        }
        OnlineCallContext callContext;
        callContext._token          = context._token;
        callContext._idempotencyKey = context._idempotencyKey;
        callContext._connection     = context._token._handle;
        callContext._accountID      = connection._accountID;
        callContext._remoteKey      = connection._remoteKey;
        callContext._traceID        = context._traceID;
        callContext._nowMs          = _nowMs;
        callContext._method         = method;
        BitReader        body( context._pBody, context._bodySize );
        ScopedLogContext scope( LogContext{ callContext._traceID, callContext._accountID } ); // 서비스가 맡긴 저장소 일까지 같은 꼬리표
        pService->onServiceRequest( *this, callContext, body );
    }

    void OnlineServiceHost::handleHello( const NetRequestContext& context, ConnectionState& connection )
    {
        // 몸: [스키마 해시 u64 리틀 엔디언][게임 빌드 blob][플랫폼 u8]. 해시는 다른 어떤 바이트보다 먼저 비교한다.
        const int32 hashSize    = OnlineProtocolConstant::kSchemaHashSize;
        const bool  bHashEquals = context._bodySize >= hashSize && LittleEndianUtil::readUint64( context._pBody ) == _schemaHash;
        if ( bHashEquals == false )
        {
            respondVersionMismatch( context._token );
            _endpoint.close( context._token._handle, StreamCloseMode::Graceful ); // 답을 다 보낸 뒤 닫는다. 클라이언트는 다시 접속하지 않는다
            return;
        }
        BitReader     reader( context._pBody + hashSize, context._bodySize - hashSize );
        vector<uint8> buildBytes;
        const bool    bBuildRead = reader.readBlob( buildBytes, OnlineProtocolConstant::kMaxBuildTextSize );
        (void)reader.readBits( 8 ); // 플랫폼. 최소 빌드 버전 확인은 계정 키트 담당
        if ( bBuildRead == false || reader.hasOverflowed() )
        {
            (void)respondError( context._token, OnlineError::kInvalidRequest );
            _endpoint.close( context._token._handle, StreamCloseMode::Graceful ); // 해시는 같은데 몸이 깨졌다. 클라이언트도 실패로 끝내므로 연결을 남기지 않는다
            return;
        }
        connection._bHelloDone = SW_TRUE;
        BitWriter answer;
        answer.writeVarUint( _settings._pRemoteConfig != nullptr ? _settings._pRemoteConfig->getSnapshotHash() : 0 );
        answer.writeVarInt( _nowMs );
        (void)respondOk( context._token, answer );
    }

    void OnlineServiceHost::respondVersionMismatch( const NetRequestToken& token )
    {
        _responseBytes.clear();
        LittleEndianUtil::appendUint16( _responseBytes, OnlineError::kVersionMismatch );
        LittleEndianUtil::appendUint64( _responseBytes, _schemaHash );
        (void)_requestServer.respond( token, NetRequestStatus::ApplicationError, _responseBytes.data(), static_cast<int32>( _responseBytes.size() ) );
    }

    IOnlineHostService* OnlineServiceHost::findService( uint16 method ) const
    {
        const uint16 range = OnlineMethodRange::getRangeBase( method );
        return VectorUtil::findBy<&IOnlineHostService::getMethodRange>( _listService, range );
    }

    bool OnlineServiceHost::sendPushToConnection( StreamConnectionHandle connection, uint16 kind, const BitWriter& body )
    {
        _responseBytes.clear();
        LittleEndianUtil::appendUint16( _responseBytes, kind ); // 종류 u16 리틀 엔디언(오류 코드와 같은 모양)
        _responseBytes.insert( _responseBytes.end(), body.getBytes().begin(), body.getBytes().begin() + body.getByteCount() );
        const StreamSendResult result = _endpoint.sendMessage( connection, _responseBytes.data(), static_cast<int32>( _responseBytes.size() ) );
        return result == StreamSendResult::Queued || result == StreamSendResult::QueuedAboveHighWatermark;
    }

    void OnlineServiceHost::dispatchServerBusMessage( const ServerBusMessage& message )
    {
        // 서비스가 메시지를 받는 동안 구독을 바꿀 수 있다 — 대상 목록을 먼저 뽑는다.
        vector<IOnlineHostService*> listTarget;
        for ( const BusSubscription& subscription : _listBusSubscription )
        {
            if ( subscription._topic == message.getTopic() )
                listTarget.push_back( subscription._pService );
        }
        for ( IOnlineHostService* pService : listTarget )
        {
            pService->onServerBusMessage( *this, message );
        }
    }

    void OnlineServiceHost::notifyAccountLeft( AccountID accountID )
    {
        for ( IOnlineHostService* pService : _listService )
        {
            pService->onAccountLeft( *this, accountID );
        }
    }
} // namespace sw
