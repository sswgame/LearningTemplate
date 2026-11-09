/**
 * @file OnlineHostTestUtil.h
 * @brief 온라인 서비스 틀 테스트 하니스 — 루프백 망 위의 서버(호스트 · 메모리 저장소 · 메모리 캐시 · 로컬 버스)와 엔드포인트 하나에 연결 여럿인 클라이언트 묶음입니다.
 * @details - 로그인은 테스트 서비스(영역 `OnlineMethodRange::kGame`)가 몸의 계정 id 로 `host.bindAccount` 한다 — 계정 키트 없이 "로그인한 연결" 을 만든다.
 *          - I/O 스레드 없이 `updateAll` 로 돈다(결정적). 서버 여럿은 같은 데이터(`MemoryServiceDatabase` · `MemoryEphemeralDatabase` · `LocalServerBusHub`)를 나눠 쓴다.
 *          - 클라이언트는 공유 엔드포인트 모드(`OnlineServiceClient::initializeOnSharedEndpoint`) — 연결이 열리면 붙는다. `connect` 뒤 `updateAll` 한 번이면 Hello 까지 끝난다.
 *          - 내리는 순서는 실제 서버와 같다: 저장소 → 호스트 → 남은 저장소 완료 → 캐시.
 *          스위트 파일이 아니다 — 서비스 틀 테스트(`ServiceClientCallTableTest`)와 키트 끝단 테스트(`ServerListModelTest` — 키트 DLL)가 include 한다.
 *          키트 서비스의 서버 쪽은 테스트가 가짜(`IOnlineServerService`)로 올려도 된다 — 키트의 프로토콜 항목만 같으면 클라이언트 창구를 그대로 지난다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/Network/Message/NetRequest.h"
#include "Core/Network/Message/StreamMessageEndpoint.h"
#include "Core/Network/Transport/LoopbackStreamTransport.h"
#include "Core/Serialization/BitStream.h"
#include "Core/Testing/TestFramework.h"
#include "Core/Time/Clock.h"
#include "Core/Time/TimePoint.h"

#include "GameFramework/Runtime/Online/Bus/Server/LocalServerBus.h"
#include "GameFramework/Runtime/Online/Cache/Server/MemoryEphemeralStore.h"
#include "GameFramework/Runtime/Online/Identity/AccountDirectory.h"
#include "GameFramework/Runtime/Online/Service/OnlineProtocol.h"
#include "GameFramework/Runtime/Online/Service/OnlineProtocolRegistry.h"
#include "GameFramework/Runtime/Online/Service/OnlineServiceClient.h"
#include "GameFramework/Runtime/Online/Service/Server/OnlineServiceHost.h"
#include "GameFramework/Runtime/Online/Store/Server/MemoryServiceStore.h"

#include <initializer_list>

namespace sw::test
{
    /** @brief 메서드가 없는 테스트 키트의 메서드 이름입니다. 열거자가 없으므로 등록 항목이 리플렉션에서 이름을 찾지 않습니다(`OnlineProtocolEntry::computeMethodHash`). */
    enum class TestNoMethodName : uint8
    {
        Count,
    };

    /** @brief @p RangeBase 영역의 빈 메서드 표입니다. 영역만 다른 항목 여럿을 만드는 호스트 · 레지스트리 테스트가 씁니다. */
    template <uint16 RangeBase>
    struct TestEmptyMethodTable : OnlineMethodTable<TestNoMethodName, RangeBase, OnlineNoPushName>
    {
    };

    /**
     * @brief 테스트 로그인 서비스의 프로토콜 항목입니다(서버와 클라이언트가 같은 해시를 얻는다).
     * @details 항목은 GameFramework.dll 한 곳(`OnlineHostTestUtil.cpp`)에 있다 — 헤더의 inline 변수면 이 헤더를 include 하는 DLL(키트 끝단 테스트)마다 한 벌씩 생겨
     *          같은 이름이 레지스트리에 두 번 올라 멈춘다(`OnlineProtocolRegistry::registerEntry`).
     */
    SW_GF_API const OnlineProtocolEntry& getTestLoginProtocol();

    /** @brief 테스트 로그인 — 몸 `varuint 계정 id` 로 이 연결을 그 계정에 묶습니다. 계정 0 이면 kInvalidRequest, 이미 다른 연결에 붙은 계정이면 kConflict. */
    class TestLoginService final : public IOnlineServerService
    {
    public:
        static constexpr uint16 kLoginMethod = OnlineMethodRange::kGame + 0x01;

        const OnlineProtocolEntry& getProtocol() const override { return getTestLoginProtocol(); }
        bool                       isAnonymousMethod( uint16 method ) const override { return method == kLoginMethod; }

        void onServiceRequest( OnlineServiceHost& host, const OnlineCallContext& context, BitReader& body ) override
        {
            const AccountID accountID = body.readVarUint();
            if ( body.hasOverflowed() || accountID == kInvalidAccountID )
            {
                (void)host.respondError( context._token, OnlineError::kInvalidRequest );
                return;
            }
            if ( host.bindAccount( context._connection, accountID ) == false )
            {
                (void)host.respondError( context._token, OnlineError::kConflict );
                return;
            }
            (void)host.respondOk( context._token, BitWriter{}, NetResponseRetention::Keep );
        }
    };

    /** @brief 클라이언트 쪽 테스트 로그인(영역만 맞추면 되는 빈 서비스). */
    class TestLoginClientService final : public IOnlineClientService
    {
    public:
        const OnlineProtocolEntry& getProtocol() const override { return getTestLoginProtocol(); }
        void                       onServicePush( uint16 kind, BitReader& body ) override
        {
            (void)kind;
            (void)body;
        }
    };
} // namespace sw::test

namespace sw::test
{
    /** @brief 서버 프로세스 하나입니다. 키트 서비스는 테스트가 `_host.registerServerService` 로 올린 뒤 `start` 합니다. */
    class OnlineTestServer final
    {
    public:
        static constexpr uint16 kFirstPort = 7400;

        MemoryServiceStore           _store;
        MemoryEphemeralStore         _cache;
        LocalServerBus               _bus;
        TestLoginService             _loginService;
        unique_ptr<IStreamTransport> _transport;
        OnlineServiceHost            _host;
        uint16                       _port;

        OnlineTestServer( LoopbackStreamNetwork& network, MemoryServiceDatabase* pDatabase, MemoryEphemeralDatabase* pCacheDatabase, LocalServerBusHub* pBusHub,
                          uint64 serverID )
            : _store{ pDatabase }
            , _cache{ pCacheDatabase }
            , _bus{ pBusHub, serverID }
            , _loginService{}
            , _transport{ network.createTransport() }
            , _host{}
            , _port{ static_cast<uint16>( kFirstPort + serverID ) }
        {
            SW_EXPECT_TRUE( _host.registerServerService( &_loginService ) );
        }

        ~OnlineTestServer()
        {
            _store.shutdown();
            _host.shutdown(); // 테스트가 직접 올린 서비스의 캐시 요청은 여기서 Unavailable 로 끝난다
            (void)_store.pollCompletions();
            _cache.shutdown();
        }

        OnlineTestServer( const OnlineTestServer& )            = delete;
        OnlineTestServer& operator=( const OnlineTestServer& ) = delete;

        /** @brief 받기 시작합니다(서비스를 모두 올린 뒤). */
        void start()
        {
            OnlineServiceHostConfig settings;
            settings._transportSettings._ioThreadCount = 0;
            settings._listenAddress                    = NetAddress::makeLoopback( _port );
            settings._pServiceStore                    = &_store;
            settings._pEphemeralStore                  = &_cache;
            settings._pServerBusTransport              = &_bus;
            settings._requestBurstPerRemote            = 1000; // 테스트 클라이언트는 모두 127.0.0.1 이다
            settings._requestBurstPerAccount           = 1000;
            {
                const auto result = _host.initialize( _transport.get(), settings );
                SW_EXPECT_TRUE_MSG( result.has_value(), result.has_value() ? std::string_view{} : result.error().message() );
            }
        }

        /** @brief 호스트 틱(전송 · 저장소 완료 · 캐시 라우터 · 버스 · 서비스)입니다. 테스트 클라이언트와 같은 틱 시각을 서버 시각으로 씁니다. */
        void update( int64 nowMs ) { _host.update( UnixMilliseconds{ nowMs } ); }
    };
} // namespace sw::test

namespace sw::test
{
    /** @brief 클라이언트 묶음 — 엔드포인트 하나 · 요청 클라이언트 하나에 연결 여럿(공유 엔드포인트 모드 — 부하 테스트 봇과 같은 모양). */
    class OnlineTestClients final : public IStreamEndpointListener
    {
    public:
        explicit OnlineTestClients( LoopbackStreamNetwork& network )
            : _transport{ network.createTransport() }
            , _endpoint{}
            , _requestClient{}
            , _listClient{}
            , _listHandle{}
            , _loginClientService{}
        {
            StreamTransportConfig transportSettings;
            transportSettings._ioThreadCount = 0;
            SW_EXPECT_TRUE( _endpoint.initialize( _transport.get(), StreamEndpointConfig{} ) );
            SW_EXPECT_TRUE( _transport->initialize( &_endpoint, transportSettings ) );
            _requestClient.initialize( &_endpoint );
        }

        ~OnlineTestClients() override
        {
            for ( unique_ptr<OnlineServiceClient>& client : _listClient )
            {
                client->shutdown();
            }
            _requestClient.shutdown();
            _transport->shutdown();
            _endpoint.shutdown();
        }

        OnlineTestClients( const OnlineTestClients& )            = delete;
        OnlineTestClients& operator=( const OnlineTestClients& ) = delete;

        /** @brief 연결을 열고 @p listService 를 등록합니다(테스트 로그인 서비스는 언제나 함께). 연결 번호입니다 — 열리면 Hello 를 한다. */
        int32 connect( uint16 port, std::initializer_list<IOnlineClientService*> listService )
        {
            unique_ptr<OnlineServiceClient>& client = _listClient.emplace_back( make_unique<OnlineServiceClient>() );
            SW_EXPECT_TRUE( client->registerClientService( &_loginClientService ) );
            for ( IOnlineClientService* pService : listService )
            {
                SW_EXPECT_TRUE( client->registerClientService( pService ) );
            }
            _listHandle.push_back( _endpoint.connect( NetAddress::makeLoopback( port ) ) );
            return static_cast<int32>( _listClient.size() ) - 1;
        }

        /** @brief 테스트 로그인을 보냅니다(응답은 기다리지 않는다 — 다음 `updateAll` 뒤면 붙어 있다). */
        void login( int32 clientIndex, AccountID accountID )
        {
            BitWriter body;
            body.writeVarUint( accountID );
            (void)getClient( clientIndex ).sendRequest( TestLoginService::kLoginMethod, body, NetRequestOptions{}, OnlineResponseDelegate{} );
        }

        /** @brief 연결을 끊습니다(서버는 계정이 떠났다고 본다). */
        void drop( int32 clientIndex ) { _endpoint.close( _listHandle[static_cast<size_t>( clientIndex )], StreamCloseMode::Abort ); }

        OnlineServiceClient& getClient( int32 clientIndex ) { return *_listClient[static_cast<size_t>( clientIndex )]; }

        void update( int64 nowMs )
        {
            (void)_transport->pollIO( 0 );
            (void)_endpoint.pump( *this );
            _requestClient.update();
            for ( unique_ptr<OnlineServiceClient>& client : _listClient )
            {
                client->update( MonotonicMilliseconds{ nowMs } ); // 클라이언트는 테스트의 같은 틱 카운터를 단조 시각으로 쓴다
            }
        }

        // IStreamEndpointListener — 연결마다 그 클라이언트로
        void onEndpointOpened( StreamConnectionHandle handle, const NetAddress& remote, bool bAccepted ) override
        {
            (void)remote;
            (void)bAccepted;
            const int32 clientIndex = findClient( handle );
            if ( clientIndex >= 0 )
                SW_EXPECT_TRUE( getClient( clientIndex ).initializeOnSharedEndpoint( &_endpoint, &_requestClient, handle, OnlineServiceClientConfig{ OnlineClientSecurity::sharedEndpoint() } ) );
        }

        void onEndpointFrame( StreamConnectionHandle handle, StreamFrameKind kind, const uint8* pBody, int32 bodySize ) override
        {
            if ( _requestClient.handleFrame( handle, kind, pBody, bodySize ) )
                return;
            const int32 clientIndex = findClient( handle );
            if ( clientIndex >= 0 )
                getClient( clientIndex ).handleFrame( kind, pBody, bodySize );
        }

        void onEndpointClosed( StreamConnectionHandle handle, StreamCloseReason reason ) override
        {
            _requestClient.onConnectionClosed( handle );
            const int32 clientIndex = findClient( handle );
            if ( clientIndex >= 0 )
                getClient( clientIndex ).handleClosed( reason );
        }

    private:
        int32 findClient( StreamConnectionHandle handle ) const
        {
            for ( size_t index = 0; index < _listHandle.size(); ++index )
            {
                if ( _listHandle[index] == handle )
                    return static_cast<int32>( index );
            }
            return -1;
        }

    private:
        unique_ptr<IStreamTransport>            _transport;
        StreamMessageEndpoint                   _endpoint;
        NetRequestClient                        _requestClient;
        vector<unique_ptr<OnlineServiceClient>> _listClient;
        vector<StreamConnectionHandle>          _listHandle;
        TestLoginClientService                  _loginClientService;
    };

    /**
     * @brief 서버 하나 + 클라이언트 묶음(같은 망 · 메모리 저장소 · 캐시 · 버스)입니다. 키트 서비스를 올리려면 `_server._host.registerServerService` 뒤 `_server.start()` 를 부른다.
     * @details @p bStart 가 false 면 받기를 시작하지 않는다 — 키트 서비스를 먼저 올리는 테스트가 쓴다(호스트는 `initialize` 전에만 서비스를 받는다).
     */
    struct OnlineTestFixture
    {
        LoopbackStreamNetwork   _network{ 5u };
        MemoryServiceDatabase   _database{};
        SystemMonotonicClock    _cacheClock{};
        MemoryEphemeralDatabase _cacheDatabase{ _cacheClock };
        LocalServerBusHub       _hub{};
        OnlineTestServer        _server{ _network, &_database, &_cacheDatabase, &_hub, 1 };
        OnlineTestClients       _clients{ _network };

        explicit OnlineTestFixture( bool bStart = true )
        {
            if ( bStart )
                _server.start();
        }
    };

    /** @brief 클라이언트 묶음과 서버들을 번갈아 @p roundCount 번 돌립니다(요청 → 응답 · 알림 한 왕복에 넉넉한 수). */
    inline void updateAll( std::initializer_list<OnlineTestServer*> listServer, OnlineTestClients& clients, int64 nowMs, int32 roundCount = 6 )
    {
        for ( int32 round = 0; round < roundCount; ++round )
        {
            clients.update( nowMs );
            for ( OnlineTestServer* pServer : listServer )
            {
                pServer->update( nowMs );
            }
        }
    }
} // namespace sw::test

namespace sw::test
{
    /** @brief 키트 @p TProtocol 의 가짜 서버 서비스입니다. 몸을 읽지 않고 `_reply` 로만 답하며(진짜 키트 서버와 같은 `respondReply`) 불린 수를 셉니다. */
    template <typename TProtocol, typename TReply>
    class TestReplyServerService final : public IOnlineServerService
    {
    public:
        TReply _reply{};
        int32  _callCount{ 0 };
        uint16 _errorCode{ OnlineError::kOk }; ///< `kOk` 가 아니면 `_reply` 대신 이 공통 범주로 답한다

        const OnlineProtocolEntry& getProtocol() const override { return TProtocol::kOnlineEntry; }
        bool                       isAnonymousMethod( uint16 method ) const override
        {
            (void)method;
            return true;
        }

        void onServiceRequest( OnlineServiceHost& host, const OnlineCallContext& context, BitReader& body ) override
        {
            (void)body;
            ++_callCount;
            if ( _errorCode != OnlineError::kOk )
                (void)host.respondError( context._token, _errorCode );
            else
                (void)host.respondReply<typename TProtocol::MessageTypes>( context._token, _reply );
        }
    };

    /** @brief 키트 @p TProtocol 의 빈 클라이언트 서비스입니다(Hello 의 스키마 해시를 서버와 맞춘다). */
    template <typename TProtocol>
    class TestProtocolClientService final : public IOnlineClientService
    {
    public:
        const OnlineProtocolEntry& getProtocol() const override { return TProtocol::kOnlineEntry; }
        void                       onServicePush( uint16 kind, BitReader& body ) override
        {
            (void)kind;
            (void)body;
        }
    };

    /**
     * @brief 키트 응답의 보존 규칙(`OnlineServiceHost::respondReply` 가 `TReply::isRetryable` 로 정한다)을 재는 하니스입니다.
     * @details 같은 멱등 키로 보낸 요청이 서비스에 닿았는지를 `send` 가 돌려준다 — 다시 시도할 결과 뒤면 닿고(1), 끝난 결과 뒤면 서버가 기억한 답을 준다(0).
     *          선언 순서가 수명이다(가짜 서비스가 망보다 오래 산다).
     */
    template <typename TProtocol, typename TReply>
    struct ReplyRetentionFixture
    {
        static constexpr int64 kNowMs = 1700000000000ll;

        TestReplyServerService<TProtocol, TReply> _service{};
        TestProtocolClientService<TProtocol>      _clientService{};
        OnlineTestFixture                         _rig{ false };
        int32                                     _clientIndex{ -1 };

        ReplyRetentionFixture()
        {
            SW_EXPECT_TRUE( _rig._server._host.registerServerService( &_service ) );
            _rig._server.start();
            _clientIndex = _rig._clients.connect( _rig._server._port, { &_clientService } );
            updateAll( { &_rig._server }, _rig._clients, kNowMs );
            SW_EXPECT_TRUE( _rig._clients.getClient( _clientIndex ).isReady() );
        }

        /** @brief 서비스가 @p reply 로 답하게 두고 @p method 를 @p key 로 보내 응답까지 돌립니다. 그사이 서비스가 불린 수입니다(0 이면 서버가 기억한 답을 줬다). */
        int32 send( uint16 method, const NetIdempotencyKey& key, const TReply& reply, uint16 errorCode = OnlineError::kOk )
        {
            _service._reply               = reply;
            _service._errorCode           = errorCode;
            const int32       callsBefore = _service._callCount;
            NetRequestOptions options;
            options._idempotencyKey = key;
            (void)_rig._clients.getClient( _clientIndex ).sendRequest( method, BitWriter{}, options, OnlineResponseDelegate{} );
            updateAll( { &_rig._server }, _rig._clients, kNowMs );
            return _service._callCount - callsBefore;
        }

        /**
         * @brief @p retryable(다시 시도할 결과) 뒤의 같은 키 재시도는 서비스에 다시 닿고, @p finished(끝난 결과) 뒤의 재시도는 닿지 않는지 봅니다.
         * @details 결과가 둘 다 같은 키 하나로 이어진다 — 결제 · 수령의 재시도가 지나는 실제 순서이다.
         */
        void expectRetryReachesService( uint16 method, const TReply& retryable, const TReply& finished )
        {
            SW_EXPECT_TRUE( retryable.isRetryable() );
            SW_EXPECT_FALSE( finished.isRetryable() );
            const NetIdempotencyKey key = NetIdempotencyKey::makeRandom();
            SW_EXPECT_EQUAL( 1, send( method, key, retryable ) );
            SW_EXPECT_EQUAL( 1, send( method, key, finished ) ); // 기억하지 않았으니 서비스가 다시 처리한다
            SW_EXPECT_EQUAL( 0, send( method, key, finished ) ); // 끝난 결과는 기억한 답이다
        }

        /**
         * @brief 다시 시도할 공통 범주 @p errorCode(일시 장애 · 충돌) 뒤의 같은 키 재시도는 서비스에 다시 닿고, @p finished(키트 업무 결과) 뒤의 재시도는 닿지 않는지 봅니다.
         * @details 키트 결과 열거형에는 공통 범주가 없다. 재시도할 실패는 `fail` 길로만 나가므로 보존이 형식에서 나온다.
         */
        void expectFailureReachesService( uint16 method, uint16 errorCode, const TReply& finished )
        {
            SW_EXPECT_TRUE( OnlineError::isRetryable( errorCode ) );
            const NetIdempotencyKey key = NetIdempotencyKey::makeRandom();
            SW_EXPECT_EQUAL( 1, send( method, key, finished, errorCode ) );
            SW_EXPECT_EQUAL( 1, send( method, key, finished ) ); // 기억하지 않았으니 서비스가 다시 처리한다
            SW_EXPECT_EQUAL( 0, send( method, key, finished ) ); // 끝난 결과는 기억한 답이다
        }
    };
} // namespace sw::test
