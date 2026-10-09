#include "pch.h"

#include "GameFramework/Base/Online/Service/OnlineServiceHost.h"

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

            /** @brief 주소마다의 도배 제한 단위 — 포트는 빼고 IP 만(포트를 바꿔 새 연결로 우회하지 못하게). */
            static uint64 makeRemoteKey( const NetAddress& remote ) { return static_cast<uint64>( remote._ipv4 ) + 1; }

            static void appendUint16( vector<uint8>& outBytes, uint16 errorCode )
            {
                outBytes.push_back( static_cast<uint8>( errorCode & 0xFFu ) );
                outBytes.push_back( static_cast<uint8>( errorCode >> 8 ) );
            }
        };
    } // namespace
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
        , _pTransport{ nullptr }
        , _nowMs{ 0 }
        , _bInitialized{ SW_FALSE }
    {
    }

    OnlineServiceHost::~OnlineServiceHost() { shutdown(); }

    bool OnlineServiceHost::initialize( IStreamTransport* pTransport, const OnlineServiceHostSettings& settings, string& outError )
    {
        SW_ASSERT( _bInitialized == SW_FALSE );
        if ( pTransport == nullptr )
        {
            outError = "online service host needs a stream transport";
            return false;
        }
        _settings   = settings;
        _pTransport = pTransport;
        _accountBucket.initialize( settings._requestBurstPerAccount, settings._requestRefillMsPerAccount, OnlineServiceHostInternal::kMaxTrackedBucketCount );
        _remoteBucket.initialize( settings._requestBurstPerRemote, settings._requestRefillMsPerRemote, OnlineServiceHostInternal::kMaxTrackedBucketCount );
        if ( _endpoint.initialize( pTransport, settings._endpointSettings ) == false )
        {
            outError = "online service host could not set up its stream endpoint";
            return false;
        }
        NetRequestServerSettings requestSettings;
        _requestServer.initialize( &_endpoint, requestSettings );
        if ( _requestServer.registerMethod( OnlineHostMethod::kHello, this ) == false || _requestServer.registerMethod( OnlineHostMethod::kPing, this ) == false )
        {
            outError = "online service host methods are already registered";
            return false;
        }
        for ( IOnlineService* pService : _listService )
        {
            for ( uint16 offset = 0; offset < OnlineMethodRange::kMethodCount; ++offset )
            {
                (void)_requestServer.registerMethod( static_cast<uint16>( pService->getMethodRange() + offset ), this ); // registerService 가 겹침을 이미 봤다
            }
        }
        if ( pTransport->initialize( &_endpoint, settings._transportSettings ) == false || pTransport->listen( settings._listenAddress ) == false )
        {
            outError = "online service host could not listen on " + settings._listenAddress.toString();
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
        // 서비스가 든 호스트 포인터 · 구독을 여기서 떼게 한다 — 서비스 객체는 호스트보다 늦게 내려가도 된다(사라진 호스트를 부르지 않는다).
        for ( size_t index = _listService.size(); index > 0; --index )
        {
            _listService[index - 1]->onHostShutdown( *this );
        }
        for ( const BusSubscription& subscription : _listBusSubscription )
        {
            _settings._pServerBus->unsubscribe( subscription._topic );
        }
        _listBusSubscription.clear();
        _pTransport->shutdown();
        _requestServer.shutdown();
        _endpoint.shutdown();
        _mapConnection.clear();
        _mapAccountToConnection.clear();
        _pTransport = nullptr;
    }

    bool OnlineServiceHost::registerService( IOnlineService* pService )
    {
        SW_ASSERT( pService != nullptr );
        const uint16 range = pService->getMethodRange();
        if ( OnlineMethodRange::getRangeBase( range ) != range || range == OnlineMethodRange::kHost )
        {
            SW_LOG_ERROR( "Online service range %# is not a range base (a multiple of 0x100 above the host range)", static_cast<uint32>( range ) );
            return false;
        }
        for ( const IOnlineService* pExisting : _listService )
        {
            if ( pExisting->getMethodRange() == range )
            {
                SW_LOG_ERROR( "Online service range %# is already taken - two kits claim one method range", static_cast<uint32>( range ) );
                return false;
            }
        }
        if ( _bInitialized == SW_TRUE )
        {
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

    void OnlineServiceHost::tick( int64 nowMs )
    {
        if ( _bInitialized == SW_FALSE )
            return;
        _nowMs = nowMs;
        if ( _settings._transportSettings._ioThreadCount == 0 )
            (void)_pTransport->pollIo( 0 );
        (void)_endpoint.pump( *this );
        _requestServer.update();
        (void)_ephemeralRouter.pump();
        if ( _settings._pServerBus != nullptr )
        {
            _listBusScratch.clear();
            (void)_settings._pServerBus->pollMessages( _listBusScratch );
            for ( const ServerBusMessage& message : _listBusScratch )
            {
                dispatchServerBusMessage( message );
            }
        }
        if ( _settings._pServiceStore != nullptr )
            (void)_settings._pServiceStore->pollCompletions(); // 끝난 저장 일의 complete 가 맡긴 서비스를 부른다 — 서비스 틱보다 먼저
        for ( IOnlineService* pService : _listService )
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
        OnlineServiceHostInternal::appendUint16( _responseBytes, errorCode );
        if ( pDetail != nullptr )
            _responseBytes.insert( _responseBytes.end(), pDetail->getBytes().begin(), pDetail->getBytes().begin() + pDetail->getByteCount() );
        return _requestServer.respond( token, NetRequestStatus::ApplicationError, _responseBytes.data(), static_cast<int32>( _responseBytes.size() ) );
    }

    bool OnlineServiceHost::bindAccount( StreamConnectionHandle connection, AccountId accountId )
    {
        const auto connectionIt = _mapConnection.find( connection.packed() );
        if ( connectionIt == _mapConnection.end() || accountId == kInvalidAccountId )
            return false;
        const auto boundIt = _mapAccountToConnection.find( accountId );
        if ( boundIt != _mapAccountToConnection.end() && boundIt->second != connection )
            return false;
        if ( connectionIt->second._accountId != kInvalidAccountId && connectionIt->second._accountId != accountId )
            return false; // 한 연결에 계정 하나
        connectionIt->second._accountId    = accountId;
        _mapAccountToConnection[accountId] = connection;
        _requestServer.setPrincipal( connection, accountId ); // 멱등 범위 = 계정(재접속한 재시도도 잡힌다)
        return true;
    }

    void OnlineServiceHost::unbindAccount( AccountId accountId )
    {
        const auto boundIt = _mapAccountToConnection.find( accountId );
        if ( boundIt == _mapAccountToConnection.end() )
            return;
        const StreamConnectionHandle connection = boundIt->second;
        _mapAccountToConnection.erase( boundIt );
        const auto connectionIt = _mapConnection.find( connection.packed() );
        if ( connectionIt != _mapConnection.end() )
            connectionIt->second._accountId = kInvalidAccountId;
        _endpoint.close( connection, StreamCloseMode::Graceful );
        notifyAccountLeft( accountId );
    }

    bool OnlineServiceHost::findConnection( AccountId accountId, StreamConnectionHandle& outConnection ) const
    {
        const auto boundIt = _mapAccountToConnection.find( accountId );
        if ( boundIt == _mapAccountToConnection.end() )
            return false;
        outConnection = boundIt->second;
        return true;
    }

    bool OnlineServiceHost::sendPush( AccountId accountId, uint16 kind, const BitWriter& body )
    {
        StreamConnectionHandle connection;
        if ( findConnection( accountId, connection ) == false )
            return false;
        return sendPushToConnection( connection, kind, body );
    }

    int32 OnlineServiceHost::sendPushToAll( uint16 kind, const BitWriter& body )
    {
        int32 sentCount = 0;
        for ( const auto& [accountId, connection] : _mapAccountToConnection )
        {
            (void)accountId;
            sentCount += sendPushToConnection( connection, kind, body ) ? 1 : 0;
        }
        return sentCount;
    }

    EphemeralStoreRouter* OnlineServiceHost::getEphemeralRouter() { return _settings._pEphemeralStore != nullptr ? &_ephemeralRouter : nullptr; }

    void OnlineServiceHost::subscribeServerBus( string_view topic, IOnlineService* pService )
    {
        if ( _settings._pServerBus == nullptr )
            return;
        bool bTopicOpen = false;
        for ( const BusSubscription& subscription : _listBusSubscription )
        {
            if ( subscription._topic == topic && subscription._pService == pService )
                return;
            bTopicOpen = bTopicOpen || subscription._topic == topic;
        }
        if ( bTopicOpen == false )
            _settings._pServerBus->subscribe( topic );
        _listBusSubscription.push_back( BusSubscription{ string( topic ), pService } );
    }

    void OnlineServiceHost::unsubscribeServerBus( string_view topic, IOnlineService* pService )
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
        if ( bRemoved && bTopicStillOpen == false && _settings._pServerBus != nullptr )
            _settings._pServerBus->unsubscribe( topic );
    }

    uint16 OnlineServiceHost::getListenPort() const { return _pTransport != nullptr ? _pTransport->getListenPort() : 0; }

    void OnlineServiceHost::onEndpointOpened( StreamConnectionHandle handle, const NetAddress& remote, bool bAccepted )
    {
        (void)bAccepted;
        ConnectionState& connection = _mapConnection[handle.packed()];
        connection                  = ConnectionState{};
        connection._remoteKey       = OnlineServiceHostInternal::makeRemoteKey( remote );
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
        const AccountId accountId = connectionIt->second._accountId;
        _mapConnection.erase( connectionIt );
        if ( accountId == kInvalidAccountId )
            return;
        const auto boundIt = _mapAccountToConnection.find( accountId );
        if ( boundIt != _mapAccountToConnection.end() && boundIt->second == handle )
        {
            _mapAccountToConnection.erase( boundIt );
            notifyAccountLeft( accountId );
        }
    }

    void OnlineServiceHost::onNetRequest( NetRequestServer& server, const NetRequestContext& context )
    {
        (void)server;
        const auto connectionIt = _mapConnection.find( context._token._handle.packed() );
        if ( connectionIt == _mapConnection.end() )
            return; // 닫히는 연결
        ConnectionState& connection = connectionIt->second;
        const uint16     method     = context._token._method;
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
        const bool bAllowed     = connection._accountId != kInvalidAccountId ? _accountBucket.tryConsume( connection._accountId, _nowMs, retryAfterMs )
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
        IOnlineService* pService = findService( method );
        if ( pService == nullptr )
        {
            (void)respondError( context._token, OnlineError::kInvalidRequest );
            return;
        }
        if ( connection._accountId == kInvalidAccountId && pService->isAnonymousMethod( method ) == false )
        {
            (void)respondError( context._token, OnlineError::kUnauthenticated );
            return;
        }
        OnlineCallContext callContext;
        callContext._token          = context._token;
        callContext._idempotencyKey = context._idempotencyKey;
        callContext._connection     = context._token._handle;
        callContext._accountId      = connection._accountId;
        callContext._remoteKey      = connection._remoteKey;
        callContext._traceId        = context._traceId;
        callContext._nowMs          = _nowMs;
        callContext._method         = method;
        BitReader        body( context._pBody, context._bodySize );
        ScopedLogContext scope( LogContext{ callContext._traceId, callContext._accountId } ); // 서비스가 맡긴 저장소 일까지 같은 꼬리표
        pService->onServiceRequest( *this, callContext, body );
    }

    void OnlineServiceHost::handleHello( const NetRequestContext& context, ConnectionState& connection )
    {
        // 몸: [기반 판 varuint][키트 수 varuint][(영역 u16, 판 varuint) …][게임 빌드 blob][플랫폼 u8]
        BitReader    reader( context._pBody, context._bodySize );
        const uint64 baseVersion = reader.readVarUint();
        const uint64 kitCount    = reader.readVarUint();
        bool         bMatches    = baseVersion == OnlineProtocolConstant::kBaseVersion && kitCount <= static_cast<uint64>( OnlineProtocolConstant::kMaxKitCount );
        for ( uint64 index = 0; bMatches && index < kitCount; ++index )
        {
            const uint16          range    = static_cast<uint16>( reader.readBits( 16 ) );
            const uint64          version  = reader.readVarUint();
            const IOnlineService* pService = findService( range );
            bMatches                       = pService != nullptr && pService->getMethodRange() == range && pService->getProtocolVersion() == version;
        }
        vector<uint8> buildBytes;
        bMatches = bMatches && reader.readBlob( buildBytes, OnlineProtocolConstant::kMaxBuildTextSize );
        (void)reader.readBits( 8 ); // 플랫폼 — 최소 빌드 판 확인은 계정 키트 몫
        BitWriter answer;
        writeServerVersions( answer );
        if ( bMatches == false || reader.hasOverflowed() )
        {
            (void)respondError( context._token, OnlineError::kVersionMismatch, &answer );
            return;
        }
        connection._bHelloDone = SW_TRUE;
        answer.writeVarUint( _settings._pRemoteConfig != nullptr ? _settings._pRemoteConfig->getSnapshotHash() : 0 );
        answer.writeVarInt( _nowMs );
        (void)respondOk( context._token, answer );
    }

    void OnlineServiceHost::writeServerVersions( BitWriter& outWriter ) const
    {
        outWriter.writeVarUint( OnlineProtocolConstant::kBaseVersion );
        outWriter.writeVarUint( _listService.size() );
        for ( const IOnlineService* pService : _listService )
        {
            outWriter.writeBits( pService->getMethodRange(), 16 );
            outWriter.writeVarUint( pService->getProtocolVersion() );
        }
    }

    IOnlineService* OnlineServiceHost::findService( uint16 method ) const
    {
        const uint16 range = OnlineMethodRange::getRangeBase( method );
        for ( IOnlineService* pService : _listService )
        {
            if ( pService->getMethodRange() == range )
                return pService;
        }
        return nullptr;
    }

    bool OnlineServiceHost::sendPushToConnection( StreamConnectionHandle connection, uint16 kind, const BitWriter& body )
    {
        _responseBytes.clear();
        OnlineServiceHostInternal::appendUint16( _responseBytes, kind ); // 종류 u16 리틀 엔디언(오류 코드와 같은 모양)
        _responseBytes.insert( _responseBytes.end(), body.getBytes().begin(), body.getBytes().begin() + body.getByteCount() );
        const StreamSendResult result = _endpoint.sendMessage( connection, _responseBytes.data(), static_cast<int32>( _responseBytes.size() ) );
        return result == StreamSendResult::Queued || result == StreamSendResult::QueuedAboveHighWatermark;
    }

    void OnlineServiceHost::dispatchServerBusMessage( const ServerBusMessage& message )
    {
        // 서비스가 메시지를 받는 동안 구독을 바꿀 수 있다 — 대상 목록을 먼저 뽑는다.
        vector<IOnlineService*> listTarget;
        for ( const BusSubscription& subscription : _listBusSubscription )
        {
            if ( subscription._topic == message._topic )
                listTarget.push_back( subscription._pService );
        }
        for ( IOnlineService* pService : listTarget )
        {
            pService->onServerBusMessage( *this, message );
        }
    }

    void OnlineServiceHost::notifyAccountLeft( AccountId accountId )
    {
        for ( IOnlineService* pService : _listService )
        {
            pService->onAccountLeft( *this, accountId );
        }
    }
} // namespace sw
