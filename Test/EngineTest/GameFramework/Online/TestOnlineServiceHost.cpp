#include "pch.h"

#include "Core/Log/LogContext.h"
#include "Core/Network/Message/NetRequest.h"
#include "Core/Network/Message/StreamMessageEndpoint.h"
#include "Core/Network/Transport/LoopbackStreamTransport.h"

#include "GameFramework/Base/Online/Bus/LocalServerBus.h"
#include "GameFramework/Base/Online/Cache/MemoryEphemeralStore.h"
#include "GameFramework/Base/Online/Guard/RequestLimits.h"
#include "GameFramework/Base/Online/Service/OnlineProtocol.h"
#include "GameFramework/Base/Online/Service/OnlineServiceClient.h"
#include "GameFramework/Base/Online/Service/OnlineServiceHost.h"

#include "TestFramework/TestFramework.h"

// 서비스 틀 — 루프백 스트림 위 호스트 + 클라이언트(한 스레드, 결정적). Hello 전 요청 거절 · 판 불일치 · 로그인 확인(익명 메서드는 통과) · 몸 상한 · 주소 도배 제한 ·
// 늦은 답(토큰) · 닫힌 뒤 답은 버림 · 계정 떼기 → 모든 서비스에 떠남 · 알림은 그 계정에만 · 모두에게 알림 · 멱등 재시도 · 캐시 답과 버스 메시지는 맡긴 · 구독한 서비스로만 ·
// 공유 끝점 클라이언트 둘 · 요청 추적 id 가 클라이언트 문맥에서 서비스의 로그 문맥까지.

using namespace sw;

namespace
{
    struct OnlineServiceHostTestInternal
    {
        static constexpr uint16 kRangeA = OnlineMethodRange::kGame;
        static constexpr uint16 kRangeB = OnlineMethodRange::kGame + OnlineMethodRange::kSize;

        // 메서드(영역 + n)
        static constexpr uint16 kEcho   = 1; ///< 익명 — 몸을 그대로 돌려준다
        static constexpr uint16 kLogin  = 2; ///< 익명 — varuint 계정 id 를 이 연결에 붙인다
        static constexpr uint16 kSecure = 3; ///< 로그인 필요
        static constexpr uint16 kLate   = 4; ///< 로그인 필요 — 다음 서비스 틱에 답한다
        static constexpr uint16 kCount  = 5; ///< 로그인 필요 — 처리기를 센다(멱등)
    };

    /** @brief 가짜 서비스 — 받은 것을 모두 적는다. */
    class FakeOnlineService final : public IOnlineService
    {
    public:
        explicit FakeOnlineService( uint16 range )
            : _listLateToken{}
            , _listLateAnswer{}
            , _listLeftAccount{}
            , _listBusMessage{}
            , _listCacheReply{}
            , _cacheKeyToRead{}
            , _lastLogContext{}
            , _lastTraceID{}
            , _range{ range }
            , _handledCount{ 0 }
            , _hostShutdownCount{ 0 }
            , _cacheReplyCountAtHostShutdown{ -1 }
            , _bReadCacheOnTick{ false }
            , _bHoldLateAnswers{ false }
        {
        }

        uint16 getMethodRange() const override { return _range; }
        uint32 getProtocolVersion() const override { return 1; }
        bool   isAnonymousMethod( uint16 method ) const override
        {
            const uint16 offset = static_cast<uint16>( method - _range );
            return offset == OnlineServiceHostTestInternal::kEcho || offset == OnlineServiceHostTestInternal::kLogin;
        }

        void onServiceRequest( OnlineServiceHost& host, const OnlineCallContext& context, BitReader& body ) override
        {
            ++_handledCount;
            _lastTraceID    = context._traceID;
            _lastLogContext = LogContext::getCurrent();
            BitWriter answer;
            switch ( static_cast<uint16>( context._method - _range ) )
            {
                case OnlineServiceHostTestInternal::kEcho:
                {
                    const uint64 value = body.readVarUint();
                    answer.writeVarUint( value );
                    (void)host.respondOk( context._token, answer );
                    break;
                }
                case OnlineServiceHostTestInternal::kLogin:
                {
                    const AccountID accountID = body.readVarUint();
                    if ( host.bindAccount( context._connection, accountID ) )
                        (void)host.respondOk( context._token, answer );
                    else
                        (void)host.respondError( context._token, OnlineError::kConflict );
                    break;
                }
                case OnlineServiceHostTestInternal::kLate:
                {
                    _listLateToken.push_back( context._token );
                    break;
                }
                default:
                {
                    answer.writeVarUint( context._accountID );
                    (void)host.respondOk( context._token, answer );
                    break;
                }
            }
        }

        void onServiceTick( OnlineServiceHost& host, int64 nowMs ) override
        {
            (void)nowMs;
            if ( _bHoldLateAnswers )
                return;
            for ( const NetRequestToken& token : _listLateToken )
            {
                _listLateAnswer.push_back( host.respondOk( token, BitWriter{} ) );
            }
            _listLateToken.clear();
            if ( _bReadCacheOnTick && host.getEphemeralRouter() != nullptr )
            {
                _bReadCacheOnTick = false;
                (void)host.getEphemeralRouter()->submit( EphemeralRequest::makeGet( _cacheKeyToRead ),
                                                         EphemeralStoreRouter::ReplyDelegate::create<&FakeOnlineService::onCacheReply>( this ) );
            }
        }

        void onAccountLeft( OnlineServiceHost& host, AccountID accountID ) override
        {
            (void)host;
            _listLeftAccount.push_back( accountID );
        }

        void onServerBusMessage( OnlineServiceHost& host, const ServerBusMessage& message ) override
        {
            (void)host;
            _listBusMessage.push_back( message );
        }

        void onHostShutdown( OnlineServiceHost& host ) override
        {
            (void)host;
            ++_hostShutdownCount;
            _cacheReplyCountAtHostShutdown = static_cast<int32>( _listCacheReply.size() );
        }

        void onCacheReply( const EphemeralReply& reply ) { _listCacheReply.push_back( reply ); }

        vector<NetRequestToken>  _listLateToken;
        vector<bool>             _listLateAnswer;
        vector<AccountID>        _listLeftAccount;
        vector<ServerBusMessage> _listBusMessage;
        vector<EphemeralReply>   _listCacheReply;
        string                   _cacheKeyToRead;
        LogContext               _lastLogContext; ///< 마지막 요청을 처리할 때의 로그 문맥
        LogTraceID               _lastTraceID;    ///< 마지막 요청의 추적 id
        uint16                   _range;
        int32                    _handledCount;
        int32                    _hostShutdownCount;
        int32                    _cacheReplyCountAtHostShutdown; ///< 호스트가 내려간다고 알릴 때 이미 받은 캐시 답 수(-1 = 아직 안 알림)
        bool                     _bReadCacheOnTick;
        bool                     _bHoldLateAnswers; ///< 켜면 늦은 요청에 답하지 않고 토큰만 든다
    };

    /** @brief 클라이언트 쪽 가짜 키트 — 알림을 적는다. */
    class FakeClientService final : public IOnlineClientService
    {
    public:
        FakeClientService( uint16 range, uint32 version )
            : _listPushKind{}
            , _range{ range }
            , _version{ version }
            , _readyCount{ 0 }
        {
        }

        uint16 getMethodRange() const override { return _range; }
        uint32 getProtocolVersion() const override { return _version; }
        void   onServicePush( uint16 kind, BitReader& body ) override
        {
            (void)body;
            _listPushKind.push_back( kind );
        }
        void onClientReady( OnlineServiceClient& client ) override
        {
            (void)client;
            ++_readyCount;
        }

        vector<uint16> _listPushKind;
        uint16         _range;
        uint32         _version;
        int32          _readyCount;
    };

    /** @brief 응답 하나를 적는 곳입니다. */
    struct ResponseRecord
    {
        vector<uint8>    _bodyBytes{};
        uint16           _errorCode{ 0 };
        NetRequestStatus _status{ NetRequestStatus::Ok };
        bool             _bAnswered{ false };

        void onResponse( const OnlineResponse& response )
        {
            _bodyBytes.assign( response._pBody, response._pBody + response._bodySize );
            _errorCode = response._errorCode;
            _status    = response._status;
            _bAnswered = true;
        }
    };

    /** @brief 클라이언트 하나 — 자기 끝점 모드. */
    struct ClientSide
    {
        unique_ptr<IStreamTransport> _transport;
        FakeClientService            _serviceA;
        FakeClientService            _serviceB;
        OnlineServiceClient          _client;

        ClientSide( LoopbackStreamNetwork& network, uint16 port, uint32 versionA = 1 )
            : _transport{ network.createTransport() }
            , _serviceA{ OnlineServiceHostTestInternal::kRangeA, versionA }
            , _serviceB{ OnlineServiceHostTestInternal::kRangeB, 1 }
            , _client{}
        {
            SW_EXPECT_TRUE( _client.registerClientService( &_serviceA ) );
            SW_EXPECT_TRUE( _client.registerClientService( &_serviceB ) );
            OnlineServiceClientSettings settings;
            settings._transportSettings._ioThreadCount = 0;
            settings._serverAddress                    = NetAddress::makeLoopback( port );
            settings._gameBuild                        = "test-1";
            string error;
            SW_EXPECT_TRUE_MSG( _client.initialize( _transport.get(), settings, error ), error.c_str() );
        }
    };

    /** @brief 호스트 하나 + 가짜 서비스 둘. */
    struct HostRig
    {
        LoopbackStreamNetwork        _network;
        unique_ptr<IStreamTransport> _hostTransport;
        FakeOnlineService            _serviceA;
        FakeOnlineService            _serviceB;
        OnlineServiceHost            _host;
        vector<ClientSide*>          _listClient;
        int64                        _nowMs;

        explicit HostRig( OnlineServiceHostSettings settings = makeSettings() )
            : _network{ 13u }
            , _hostTransport{ _network.createTransport() }
            , _serviceA{ OnlineServiceHostTestInternal::kRangeA }
            , _serviceB{ OnlineServiceHostTestInternal::kRangeB }
            , _host{}
            , _listClient{}
            , _nowMs{ 1000 }
        {
            SW_EXPECT_TRUE( _host.registerService( &_serviceA ) );
            SW_EXPECT_TRUE( _host.registerService( &_serviceB ) );
            string error;
            SW_EXPECT_TRUE_MSG( _host.initialize( _hostTransport.get(), settings, error ), error.c_str() );
        }

        static OnlineServiceHostSettings makeSettings()
        {
            OnlineServiceHostSettings settings;
            settings._transportSettings._ioThreadCount = 0;
            settings._listenAddress                    = NetAddress::makeLoopback( 0 );
            settings._requestBurstPerRemote            = 1000; // 시험 클라이언트는 모두 127.0.0.1 — 도배 제한 시험만 낮춘다
            settings._requestBurstPerAccount           = 1000;
            return settings;
        }

        uint16 getPort() const { return _host.getListenPort(); }

        void step( int32 count = 6 )
        {
            for ( int32 index = 0; index < count; ++index )
            {
                _nowMs += 10;
                for ( ClientSide* pClient : _listClient )
                {
                    pClient->_client.tick( _nowMs );
                }
                _host.tick( _nowMs );
            }
        }

        /** @brief 요청 하나를 보내고 답이 올 때까지 돕니다. */
        ResponseRecord call( ClientSide& client, uint16 method, uint64 value, const NetRequestOptions& options = NetRequestOptions{} )
        {
            ResponseRecord record;
            BitWriter      body;
            body.writeVarUint( value );
            (void)client._client.sendRequest( method, body, options, OnlineResponseDelegate::create<&ResponseRecord::onResponse>( &record ) );
            for ( int32 attempt = 0; attempt < 50 && record._bAnswered == false; ++attempt )
            {
                step( 1 );
            }
            SW_EXPECT_TRUE( record._bAnswered );
            return record;
        }
    };

    /** @brief 서비스 틀을 모르는 맨 요청 클라이언트(Hello 전 요청 · 손으로 지은 Hello). */
    struct RawClientSide final : public IStreamEndpointListener
    {
        unique_ptr<IStreamTransport> _transport;
        StreamMessageEndpoint        _endpoint;
        NetRequestClient             _requestClient;
        StreamConnectionHandle       _handle;
        NetResponse                  _lastResponse;
        vector<uint8>                _lastBodyBytes;
        bool                         _bAnswered;

        RawClientSide( LoopbackStreamNetwork& network, uint16 port )
            : _transport{ network.createTransport() }
            , _endpoint{}
            , _requestClient{}
            , _handle{}
            , _lastResponse{}
            , _lastBodyBytes{}
            , _bAnswered{ false }
        {
            StreamTransportSettings transportSettings;
            transportSettings._ioThreadCount = 0;
            SW_EXPECT_TRUE( _endpoint.initialize( _transport.get(), StreamEndpointSettings{} ) );
            SW_EXPECT_TRUE( _transport->initialize( &_endpoint, transportSettings ) );
            _requestClient.initialize( &_endpoint );
            _handle = _endpoint.connect( NetAddress::makeLoopback( port ) );
        }

        ~RawClientSide() override
        {
            _transport->shutdown();
            _endpoint.shutdown();
        }

        void pump()
        {
            (void)_transport->pollIo( 0 );
            (void)_endpoint.pump( *this );
        }

        void send( uint16 method, const BitWriter& body )
        {
            _bAnswered = false;
            (void)_requestClient.sendRequest( _handle, method, body.getBytes().data(), body.getByteCount(), NetRequestOptions{},
                                              Delegate<void( const NetResponse& )>::create<&RawClientSide::onResponse>( this ) );
        }

        uint16 getErrorCode() const { return _lastBodyBytes.size() >= 2 ? static_cast<uint16>( _lastBodyBytes[0] | ( _lastBodyBytes[1] << 8 ) ) : 0; }

        void onResponse( const NetResponse& response )
        {
            _lastResponse = response;
            _lastBodyBytes.assign( response._pBody, response._pBody + response._bodySize );
            _bAnswered = true;
        }

        void onEndpointOpened( StreamConnectionHandle handle, const NetAddress& remote, bool bAccepted ) override
        {
            (void)handle;
            (void)remote;
            (void)bAccepted;
        }
        void onEndpointFrame( StreamConnectionHandle handle, StreamFrameKind kind, const uint8* pBody, int32 bodySize ) override
        {
            (void)_requestClient.handleFrame( handle, kind, pBody, bodySize );
        }
        void onEndpointClosed( StreamConnectionHandle handle, StreamCloseReason reason ) override
        {
            (void)reason;
            _requestClient.onConnectionClosed( handle );
        }
    };
    /** @brief 공유 끝점 — 끝점 · 요청 클라이언트 하나에 연결 둘(부하 시험 봇 모양). 리스너가 응답은 요청 클라이언트로, 나머지는 그 연결의 클라이언트로. */
    struct SharedEndpointBots final : public IStreamEndpointListener
    {
        unique_ptr<IStreamTransport> _transport;
        StreamMessageEndpoint        _endpoint;
        NetRequestClient             _requestClient;
        FakeClientService            _arrService[2]{
            {OnlineServiceHostTestInternal::kRangeA, 1},
            {OnlineServiceHostTestInternal::kRangeA, 1}
        };
        OnlineServiceClient    _arrClient[2]{};
        StreamConnectionHandle _arrHandle[2]{};

        SharedEndpointBots( LoopbackStreamNetwork& network, uint16 port )
            : _transport{ network.createTransport() }
            , _endpoint{}
            , _requestClient{}
        {
            StreamTransportSettings transportSettings;
            transportSettings._ioThreadCount = 0;
            SW_EXPECT_TRUE( _endpoint.initialize( _transport.get(), StreamEndpointSettings{} ) );
            SW_EXPECT_TRUE( _transport->initialize( &_endpoint, transportSettings ) );
            _requestClient.initialize( &_endpoint );
            for ( int32 index = 0; index < 2; ++index )
            {
                SW_EXPECT_TRUE( _arrClient[index].registerClientService( &_arrService[index] ) );
                _arrHandle[index] = _endpoint.connect( NetAddress::makeLoopback( port ) );
            }
        }

        ~SharedEndpointBots() override
        {
            for ( OnlineServiceClient& client : _arrClient )
            {
                client.shutdown();
            }
            _requestClient.shutdown();
            _transport->shutdown();
            _endpoint.shutdown();
        }

        void pump()
        {
            (void)_transport->pollIo( 0 );
            (void)_endpoint.pump( *this );
            _requestClient.update();
        }

        void onEndpointOpened( StreamConnectionHandle handle, const NetAddress& remote, bool bAccepted ) override
        {
            (void)remote;
            (void)bAccepted;
            for ( int32 index = 0; index < 2; ++index )
            {
                if ( _arrHandle[index] == handle )
                    SW_EXPECT_TRUE( _arrClient[index].initializeOnSharedEndpoint( &_endpoint, &_requestClient, handle, OnlineServiceClientSettings{} ) );
            }
        }
        void onEndpointFrame( StreamConnectionHandle handle, StreamFrameKind kind, const uint8* pBody, int32 bodySize ) override
        {
            if ( _requestClient.handleFrame( handle, kind, pBody, bodySize ) )
                return;
            for ( int32 index = 0; index < 2; ++index )
            {
                if ( _arrHandle[index] == handle )
                    _arrClient[index].handleFrame( kind, pBody, bodySize );
            }
        }
        void onEndpointClosed( StreamConnectionHandle handle, StreamCloseReason reason ) override
        {
            _requestClient.onConnectionClosed( handle );
            for ( int32 index = 0; index < 2; ++index )
            {
                if ( _arrHandle[index] == handle )
                    _arrClient[index].handleClosed( reason );
            }
        }
    };
} // namespace

SW_TEST_CASE( OnlineServiceHostTest, RequestsBeforeHelloAreRefusedAndVersionsAreNegotiated )
{
    HostRig       rig;
    RawClientSide raw( rig._network, rig.getPort() );
    for ( int32 index = 0; index < 4; ++index )
    {
        raw.pump();
        rig.step( 1 );
    }
    BitWriter echo;
    echo.writeVarUint( 5 );
    raw.send( OnlineServiceHostTestInternal::kRangeA + OnlineServiceHostTestInternal::kEcho, echo );
    for ( int32 index = 0; index < 6 && raw._bAnswered == false; ++index )
    {
        raw.pump();
        rig.step( 1 );
        raw.pump();
    }
    SW_ASSERT_TRUE( raw._bAnswered );
    SW_EXPECT_TRUE( raw._lastResponse._status == NetRequestStatus::ApplicationError );
    SW_EXPECT_EQUAL( OnlineError::kInvalidRequest, raw.getErrorCode() );
    SW_EXPECT_EQUAL( 0, rig._serviceA._handledCount );

    // 서버에 없는 영역 0x9000 을 낸 Hello — 판 불일치와 서버의 (영역, 판) 목록
    BitWriter hello;
    hello.writeVarUint( OnlineProtocolConstant::kBaseVersion );
    hello.writeVarUint( 1 );
    hello.writeBits( 0x9000, 16 );
    hello.writeVarUint( 1 );
    hello.writeBlob( nullptr, 0 );
    hello.writeBits( 0, 8 );
    raw.send( OnlineHostMethod::kHello, hello );
    for ( int32 index = 0; index < 6 && raw._bAnswered == false; ++index )
    {
        raw.pump();
        rig.step( 1 );
        raw.pump();
    }
    SW_ASSERT_TRUE( raw._bAnswered );
    SW_EXPECT_EQUAL( OnlineError::kVersionMismatch, raw.getErrorCode() );
    BitReader serverList( raw._lastBodyBytes.data() + 2, static_cast<int32>( raw._lastBodyBytes.size() ) - 2 );
    SW_EXPECT_EQUAL( uint64( OnlineProtocolConstant::kBaseVersion ), serverList.readVarUint() );
    SW_EXPECT_EQUAL( uint64( 2 ), serverList.readVarUint() );
    SW_EXPECT_EQUAL( uint32( OnlineServiceHostTestInternal::kRangeA ), serverList.readBits( 16 ) );
}

SW_TEST_CASE( OnlineServiceHostTest, VersionMismatchStopsTheClient )
{
    HostRig    rig;
    ClientSide client( rig._network, rig.getPort(), 2 ); // 서버는 판 1
    rig._listClient.push_back( &client );
    {
        SW_TEST_DEFENSIVE_SCOPE( "the client warns that an update is required" );
        rig.step( 10 );
    }
    SW_EXPECT_TRUE( client._client.getState() == OnlineClientState::VersionMismatch );
    const ResponseRecord record = rig.call( client, OnlineServiceHostTestInternal::kRangeA + OnlineServiceHostTestInternal::kEcho, 1 );
    SW_EXPECT_EQUAL( OnlineError::kVersionMismatch, record._errorCode );
    SW_EXPECT_EQUAL( 0, client._serviceA._readyCount );
}

SW_TEST_CASE( OnlineServiceHostTest, AnonymousMethodsPassAndOthersNeedALogin )
{
    HostRig    rig;
    ClientSide client( rig._network, rig.getPort() );
    rig._listClient.push_back( &client );
    rig.step( 10 );
    SW_ASSERT_TRUE( client._client.isReady() );
    SW_EXPECT_EQUAL( 1, client._serviceA._readyCount );

    const ResponseRecord echo = rig.call( client, OnlineServiceHostTestInternal::kRangeA + OnlineServiceHostTestInternal::kEcho, 42 );
    SW_ASSERT_TRUE( echo._status == NetRequestStatus::Ok );
    BitReader echoBody( echo._bodyBytes.data(), static_cast<int32>( echo._bodyBytes.size() ) );
    SW_EXPECT_EQUAL( uint64( 42 ), echoBody.readVarUint() );

    SW_EXPECT_EQUAL( OnlineError::kUnauthenticated, rig.call( client, OnlineServiceHostTestInternal::kRangeB + OnlineServiceHostTestInternal::kSecure, 0 )._errorCode );
    SW_EXPECT_TRUE( rig.call( client, OnlineServiceHostTestInternal::kRangeA + OnlineServiceHostTestInternal::kLogin, 77 )._status == NetRequestStatus::Ok );
    const ResponseRecord secure = rig.call( client, OnlineServiceHostTestInternal::kRangeB + OnlineServiceHostTestInternal::kSecure, 0 ); // 다른 영역 — 라우팅
    SW_ASSERT_TRUE( secure._status == NetRequestStatus::Ok );
    BitReader secureBody( secure._bodyBytes.data(), static_cast<int32>( secure._bodyBytes.size() ) );
    SW_EXPECT_EQUAL( uint64( 77 ), secureBody.readVarUint() ); // 호출자 문맥의 계정
    SW_EXPECT_EQUAL( 1, rig._serviceB._handledCount );
}

SW_TEST_CASE( OnlineServiceHostTest, OversizedBodiesAndFloodsAreRefused )
{
    OnlineServiceHostSettings settings = HostRig::makeSettings();
    settings._requestBurstPerRemote    = 3;
    settings._requestRefillMsPerRemote = 10000;
    HostRig    rig( settings );
    ClientSide client( rig._network, rig.getPort() );
    rig._listClient.push_back( &client );
    rig.step( 10 );
    SW_ASSERT_TRUE( client._client.isReady() );

    ResponseRecord oversized;
    BitWriter      bigBody;
    vector<uint8>  bytes( static_cast<size_t>( RequestLimits::kMaxRequestBodySize ) + 1, 7 );
    bigBody.writeBytes( bytes.data(), static_cast<int32>( bytes.size() ) );
    (void)client._client.sendRequest( OnlineServiceHostTestInternal::kRangeA + OnlineServiceHostTestInternal::kEcho, bigBody, NetRequestOptions{},
                                      OnlineResponseDelegate::create<&ResponseRecord::onResponse>( &oversized ) );
    rig.step( 10 );
    SW_EXPECT_EQUAL( OnlineError::kInvalidRequest, oversized._errorCode );

    for ( int32 index = 0; index < 3; ++index )
    {
        SW_EXPECT_TRUE( rig.call( client, OnlineServiceHostTestInternal::kRangeA + OnlineServiceHostTestInternal::kEcho, 1 )._status == NetRequestStatus::Ok );
    }
    const ResponseRecord limited = rig.call( client, OnlineServiceHostTestInternal::kRangeA + OnlineServiceHostTestInternal::kEcho, 1 );
    SW_ASSERT_EQUAL( OnlineError::kRateLimited, limited._errorCode );
    BitReader retry( limited._bodyBytes.data(), static_cast<int32>( limited._bodyBytes.size() ) );
    SW_EXPECT_TRUE( retry.readVarUint() > 0 ); // 기다릴 ms
}

SW_TEST_CASE( OnlineServiceHostTest, LateAnswersUseTheTokenAndAnswersAfterCloseAreDropped )
{
    HostRig    rig;
    ClientSide client( rig._network, rig.getPort() );
    rig._listClient.push_back( &client );
    rig.step( 10 );
    (void)rig.call( client, OnlineServiceHostTestInternal::kRangeA + OnlineServiceHostTestInternal::kLogin, 5 );
    const ResponseRecord late = rig.call( client, OnlineServiceHostTestInternal::kRangeA + OnlineServiceHostTestInternal::kLate, 0 );
    SW_EXPECT_TRUE( late._status == NetRequestStatus::Ok );
    SW_ASSERT_EQUAL( size_t( 1 ), rig._serviceA._listLateAnswer.size() );
    SW_EXPECT_TRUE( rig._serviceA._listLateAnswer[0] );

    // 답하기 전에 연결이 닫히면 그 답은 버려진다.
    rig._serviceA._bHoldLateAnswers = true;
    ResponseRecord orphan;
    (void)client._client.sendRequest( OnlineServiceHostTestInternal::kRangeA + OnlineServiceHostTestInternal::kLate, BitWriter{}, NetRequestOptions{},
                                      OnlineResponseDelegate::create<&ResponseRecord::onResponse>( &orphan ) );
    rig.step( 6 );
    SW_ASSERT_EQUAL( size_t( 1 ), rig._serviceA._listLateToken.size() );
    const NetRequestToken held = rig._serviceA._listLateToken[0];
    rig._listClient.clear();
    client._client.shutdown();
    rig.step( 4 );
    SW_EXPECT_EQUAL( 0, rig._host.getConnectionCount() );
    SW_EXPECT_TRUE( rig._host.respondOk( held, BitWriter{} ) );                           // 처리는 끝났다(멱등 기억은 남는다) — 닫힌 연결이라 끝점이 보내지 않는다
    SW_EXPECT_FALSE( rig._host.respondOk( held, BitWriter{} ) );                          // 한 번만
    SW_EXPECT_TRUE( orphan._bAnswered && orphan._status == NetRequestStatus::Cancelled ); // 클라이언트는 내릴 때 끝냈다
}

SW_TEST_CASE( OnlineServiceHostTest, UnbindAccountClosesTheConnectionAndTellsEveryService )
{
    HostRig    rig;
    ClientSide client( rig._network, rig.getPort() );
    rig._listClient.push_back( &client );
    rig.step( 10 );
    (void)rig.call( client, OnlineServiceHostTestInternal::kRangeA + OnlineServiceHostTestInternal::kLogin, 9 );
    StreamConnectionHandle connection;
    SW_ASSERT_TRUE( rig._host.findConnection( 9, connection ) );
    rig._host.unbindAccount( 9 );
    SW_EXPECT_FALSE( rig._host.findConnection( 9, connection ) );
    SW_ASSERT_EQUAL( size_t( 1 ), rig._serviceA._listLeftAccount.size() );
    SW_ASSERT_EQUAL( size_t( 1 ), rig._serviceB._listLeftAccount.size() );
    SW_EXPECT_EQUAL( AccountID( 9 ), rig._serviceB._listLeftAccount[0] );
    rig.step( 6 );
    SW_EXPECT_TRUE( client._client.isReady() == false || client._client.getConnection() != connection ); // 닫혔다(다시 걸 수는 있다)
    SW_EXPECT_EQUAL( size_t( 1 ), rig._serviceA._listLeftAccount.size() );                               // 닫힘이 또 알리지 않는다
}

SW_TEST_CASE( OnlineServiceHostTest, PushReachesOnlyTheBoundAccountAndPushToAllEveryBoundAccount )
{
    HostRig    rig;
    ClientSide first( rig._network, rig.getPort() );
    ClientSide second( rig._network, rig.getPort() );
    ClientSide anonymous( rig._network, rig.getPort() );
    rig._listClient = { &first, &second, &anonymous };
    rig.step( 10 );
    (void)rig.call( first, OnlineServiceHostTestInternal::kRangeA + OnlineServiceHostTestInternal::kLogin, 1 );
    (void)rig.call( second, OnlineServiceHostTestInternal::kRangeA + OnlineServiceHostTestInternal::kLogin, 2 );
    SW_EXPECT_FALSE( rig.call( anonymous, OnlineServiceHostTestInternal::kRangeA + OnlineServiceHostTestInternal::kLogin, 1 )._status == NetRequestStatus::Ok ); // 붙은 계정

    BitWriter body;
    body.writeVarUint( 3 );
    SW_EXPECT_TRUE( rig._host.sendPush( 1, OnlineServiceHostTestInternal::kRangeA + 0x80, body ) );
    SW_EXPECT_FALSE( rig._host.sendPush( 3, OnlineServiceHostTestInternal::kRangeA + 0x80, body ) );
    rig.step( 6 );
    SW_ASSERT_EQUAL( size_t( 1 ), first._serviceA._listPushKind.size() );
    SW_EXPECT_EQUAL( uint16( OnlineServiceHostTestInternal::kRangeA + 0x80 ), first._serviceA._listPushKind[0] );
    SW_EXPECT_TRUE( second._serviceA._listPushKind.empty() );

    SW_EXPECT_EQUAL( 2, rig._host.sendPushToAll( OnlineServiceHostTestInternal::kRangeB + 0x81, body ) );
    rig.step( 6 );
    SW_EXPECT_EQUAL( size_t( 1 ), first._serviceB._listPushKind.size() );
    SW_EXPECT_EQUAL( size_t( 1 ), second._serviceB._listPushKind.size() );
    SW_EXPECT_TRUE( anonymous._serviceB._listPushKind.empty() );
}

SW_TEST_CASE( OnlineServiceHostTest, RetryWithTheSameIdempotencyKeyRunsTheHandlerOnce )
{
    HostRig    rig;
    ClientSide client( rig._network, rig.getPort() );
    rig._listClient.push_back( &client );
    rig.step( 10 );
    (void)rig.call( client, OnlineServiceHostTestInternal::kRangeA + OnlineServiceHostTestInternal::kLogin, 11 );
    NetRequestOptions options;
    options._idempotencyKey = NetIdempotencyKey::makeRandom();
    const int32 before      = rig._serviceB._handledCount;
    SW_EXPECT_TRUE( rig.call( client, OnlineServiceHostTestInternal::kRangeB + OnlineServiceHostTestInternal::kCount, 0, options )._status == NetRequestStatus::Ok );
    SW_EXPECT_TRUE( rig.call( client, OnlineServiceHostTestInternal::kRangeB + OnlineServiceHostTestInternal::kCount, 0, options )._status == NetRequestStatus::Ok );
    SW_EXPECT_EQUAL( before + 1, rig._serviceB._handledCount );
}

SW_TEST_CASE( OnlineServiceHostTest, EphemeralRepliesReachOnlyTheSubmittingService )
{
    MemoryEphemeralDatabase   database;
    MemoryEphemeralStore      writer( &database );
    MemoryEphemeralStore      hostCache( &database );
    OnlineServiceHostSettings settings = HostRig::makeSettings();
    settings._pEphemeralStore          = &hostCache;
    HostRig rig( settings );
    (void)writer.submit( EphemeralRequest::makeSet( "a", vector<uint8>{ 1 }, 0 ) );
    (void)writer.submit( EphemeralRequest::makeSet( "b", vector<uint8>{ 2, 2 }, 0 ) );
    rig._serviceA._cacheKeyToRead   = "a";
    rig._serviceB._cacheKeyToRead   = "b";
    rig._serviceA._bReadCacheOnTick = true;
    rig._serviceB._bReadCacheOnTick = true;
    rig.step( 2 );
    SW_ASSERT_EQUAL( size_t( 1 ), rig._serviceA._listCacheReply.size() );
    SW_ASSERT_EQUAL( size_t( 1 ), rig._serviceB._listCacheReply.size() );
    SW_EXPECT_TRUE( rig._serviceA._listCacheReply[0]._value == vector<uint8>{ 1 } );
    SW_EXPECT_TRUE( ( rig._serviceB._listCacheReply[0]._value == vector<uint8>{ 2, 2 } ) );
    SW_EXPECT_EQUAL( 0, rig._host.getEphemeralRouter()->getPendingCount() );
}

SW_TEST_CASE( OnlineServiceHostTest, ServerBusMessagesFanOutToSubscribedServices )
{
    LocalServerBusHub         hub;
    LocalServerBus            busOfHost( &hub, 1 );
    LocalServerBus            busOfOther( &hub, 2 );
    OnlineServiceHostSettings settings = HostRig::makeSettings();
    settings._pServerBus               = &busOfHost;
    HostRig rig( settings );
    rig._host.subscribeServerBus( "sd.changed", &rig._serviceA );
    rig._host.subscribeServerBus( "sd.changed", &rig._serviceB );
    rig._host.subscribeServerBus( "sd.changed", &rig._serviceB ); // 두 번 — 한 번만 받는다
    const uint8 arrPayload[1] = { 4 };
    busOfOther.publish( "sd.changed", arrPayload, 1 );
    rig.step( 1 );
    SW_ASSERT_EQUAL( size_t( 1 ), rig._serviceA._listBusMessage.size() );
    SW_ASSERT_EQUAL( size_t( 1 ), rig._serviceB._listBusMessage.size() );
    SW_EXPECT_EQUAL( uint64( 2 ), rig._serviceA._listBusMessage[0]._originServerID );

    rig._host.unsubscribeServerBus( "sd.changed", &rig._serviceA );
    busOfOther.publish( "sd.changed", arrPayload, 1 );
    rig.step( 1 );
    SW_EXPECT_EQUAL( size_t( 1 ), rig._serviceA._listBusMessage.size() );
    SW_EXPECT_EQUAL( size_t( 2 ), rig._serviceB._listBusMessage.size() ); // 주제는 아직 열려 있다
}

SW_TEST_CASE( OnlineServiceHostTest, ShutdownTellsEveryServiceOnceAfterPendingCacheReplies )
{
    // 서비스 객체는 호스트보다 늦게 내려가도 된다 — 호스트가 내려가며 서비스마다 한 번 알리고(그 뒤로 서비스는 호스트를 부르지 않는다),
    // 알리기 전에 기다리던 캐시 요청은 Unavailable 로 끝낸다.
    MemoryEphemeralDatabase   database;
    MemoryEphemeralStore      hostCache( &database );
    LocalServerBusHub         hub;
    LocalServerBus            busOfHost( &hub, 1 );
    OnlineServiceHostSettings settings = HostRig::makeSettings();
    settings._pEphemeralStore          = &hostCache;
    settings._pServerBus               = &busOfHost;
    HostRig rig( settings );
    rig._host.subscribeServerBus( "sd.changed", &rig._serviceA );
    (void)rig._host.getEphemeralRouter()->submit( EphemeralRequest::makeGet( "never.pumped" ),
                                                  EphemeralStoreRouter::ReplyDelegate::create<&FakeOnlineService::onCacheReply>( &rig._serviceA ) );
    rig._host.shutdown();
    SW_EXPECT_EQUAL( 1, rig._serviceA._hostShutdownCount );
    SW_EXPECT_EQUAL( 1, rig._serviceB._hostShutdownCount );
    SW_ASSERT_EQUAL( size_t( 1 ), rig._serviceA._listCacheReply.size() );
    SW_EXPECT_TRUE( rig._serviceA._listCacheReply[0]._result == EphemeralResult::Unavailable );
    SW_EXPECT_EQUAL( 1, rig._serviceA._cacheReplyCountAtHostShutdown ); // 답이 먼저
    rig._host.shutdown();                                               // 두 번째는 아무것도 하지 않는다
    SW_EXPECT_EQUAL( 1, rig._serviceA._hostShutdownCount );
}

SW_TEST_CASE( OnlineServiceHostTest, SharedEndpointClientsEachHelloAndGetTheirOwnPushes )
{
    HostRig            rig;
    SharedEndpointBots bots( rig._network, rig.getPort() );
    for ( int32 index = 0; index < 10; ++index )
    {
        bots.pump();
        rig.step( 1 );
    }
    SW_ASSERT_TRUE( bots._arrClient[0].isReady() && bots._arrClient[1].isReady() );
    ResponseRecord arrRecord[2];
    for ( int32 index = 0; index < 2; ++index )
    {
        BitWriter body;
        body.writeVarUint( static_cast<uint64>( 100 + index ) );
        (void)bots._arrClient[index].sendRequest( OnlineServiceHostTestInternal::kRangeA + OnlineServiceHostTestInternal::kLogin, body, NetRequestOptions{},
                                                  OnlineResponseDelegate::create<&ResponseRecord::onResponse>( &arrRecord[index] ) );
    }
    for ( int32 index = 0; index < 10; ++index )
    {
        bots.pump();
        rig.step( 1 );
    }
    SW_EXPECT_TRUE( arrRecord[0]._status == NetRequestStatus::Ok && arrRecord[1]._status == NetRequestStatus::Ok );
    SW_EXPECT_TRUE( rig._host.sendPush( 101, OnlineServiceHostTestInternal::kRangeA + 0x90, BitWriter{} ) );
    for ( int32 index = 0; index < 4; ++index )
    {
        rig.step( 1 );
        bots.pump();
    }
    SW_EXPECT_TRUE( bots._arrService[0]._listPushKind.empty() );
    SW_EXPECT_EQUAL( size_t( 1 ), bots._arrService[1]._listPushKind.size() );
}

SW_TEST_CASE( OnlineServiceHostTest, TraceIDReachesTheServiceLogContextWithTheAccount )
{
    HostRig    rig;
    ClientSide client( rig._network, rig.getPort() );
    rig._listClient.push_back( &client );
    rig.step( 10 );
    (void)rig.call( client, OnlineServiceHostTestInternal::kRangeA + OnlineServiceHostTestInternal::kLogin, 21 );
    LogContext caller;
    caller._traceID = LogTraceID{ 0x5, 0x6 };
    ResponseRecord record;
    {
        ScopedLogContext scope( caller ); // 클라이언트 쪽 문맥 — 요청 머리에 실린다
        BitWriter        body;
        body.writeVarUint( 0 );
        (void)client._client.sendRequest( OnlineServiceHostTestInternal::kRangeA + OnlineServiceHostTestInternal::kSecure, body, NetRequestOptions{},
                                          OnlineResponseDelegate::create<&ResponseRecord::onResponse>( &record ) );
    }
    for ( int32 attempt = 0; attempt < 50 && record._bAnswered == false; ++attempt )
    {
        rig.step( 1 ); // 문맥 밖 — 서비스의 문맥은 호스트가 건 것이다
    }
    SW_ASSERT_TRUE( record._bAnswered );
    SW_EXPECT_TRUE( rig._serviceA._lastTraceID == caller._traceID );
    SW_EXPECT_TRUE( rig._serviceA._lastLogContext._traceID == caller._traceID );
    SW_EXPECT_EQUAL( rig._serviceA._lastLogContext._principalID, uint64( 21 ) );
    SW_EXPECT_TRUE( LogContext::getCurrent().isEmpty() );
}
