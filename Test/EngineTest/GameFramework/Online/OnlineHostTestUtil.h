/**
 * @file OnlineHostTestUtil.h
 * @brief 온라인 키트 끝단 시험 하니스 — 루프백 망 위의 서버(호스트 · 메모리 저장소 · 메모리 캐시 · 로컬 버스 · 가짜 접속 상태)와 끝점 하나에 연결 여럿인 클라이언트 묶음입니다.
 * @details - 로그인은 시험 서비스(영역 `OnlineMethodRange::kGame`)가 몸의 계정 id 로 `host.bindAccount` 한다 — 계정 키트 없이 "로그인한 연결" 을 만든다.
 *          - I/O 스레드 없이 `tickAll` 로 돈다(결정적). 서버 여럿은 같은 데이터(`MemoryServiceDatabase` · `MemoryEphemeralDatabase` · `LocalServerBusHub`)를 나눠 쓴다.
 *          - 클라이언트는 공유 끝점 모드(`OnlineServiceClient::initializeOnSharedEndpoint`) — 연결이 열리면 붙는다. `connect` 뒤 `tickAll` 한 번이면 Hello 까지 끝난다.
 *          - 접속 상태는 `FakeAccountPresence`(서버마다 하나 — 시험이 `setOnline` 으로 "다른 서버에 붙은 계정" 을 만든다). 결과는 서버 `tick` 에 한 번 알린다.
 *          - 내리는 순서는 실제 서버와 같다: 키트(로직 · 바인딩) → 호스트 → 저장소 · 캐시. 키트 묶음은 `IOnlineTestKit` 을 구현해 `server.addKit( this )` 로 올리면
 *            서버 소멸자가 맨 먼저 `stop` 한다 — 선언 순서에 기대지 않는다.
 *          스위트 파일이 아니다 — 키트 끝단 시험(`<Kit>StreamTest`)이 include 한다. 쓰는 법은 `ServerDirectoryStreamTest` 를 본다.
 */
#pragma once
#include "Core/Container/StringUtil.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/Network/BitStream.h"
#include "Core/Network/Message/NetRequest.h"
#include "Core/Network/Message/StreamMessageEndpoint.h"
#include "Core/Network/Transport/LoopbackStreamTransport.h"

#include "GameFramework/Base/Online/Bus/LocalServerBus.h"
#include "GameFramework/Base/Online/Cache/MemoryEphemeralStore.h"
#include "GameFramework/Base/Online/Identity/AccountPresence.h"
#include "GameFramework/Base/Online/Service/OnlineProtocol.h"
#include "GameFramework/Base/Online/Service/OnlineServiceClient.h"
#include "GameFramework/Base/Online/Service/OnlineServiceHost.h"
#include "GameFramework/Base/Online/Store/MemoryServiceStore.h"

#include "TestFramework/TestFramework.h"

#include <initializer_list>

namespace test
{
    /** @brief 시험 로그인 — 몸 `varuint 계정 id` 로 이 연결을 그 계정에 묶습니다. 이미 다른 연결에 붙은 계정이면 kConflict. */
    class TestLoginService final : public sw::IOnlineService
    {
    public:
        static constexpr uint16 kLoginMethod = sw::OnlineMethodRange::kGame + 0x01;

        uint16 getMethodRange() const override { return sw::OnlineMethodRange::kGame; }
        uint32 getProtocolVersion() const override { return 1; }
        bool   isAnonymousMethod( uint16 method ) const override { return method == kLoginMethod; }

        void onServiceRequest( sw::OnlineServiceHost& host, const sw::OnlineCallContext& context, sw::BitReader& body ) override
        {
            const sw::AccountID accountID = body.readVarUint();
            if ( body.hasOverflowed() || accountID == sw::kInvalidAccountID )
            {
                (void)host.respondError( context._token, sw::OnlineError::kInvalidRequest );
                return;
            }
            if ( host.bindAccount( context._connection, accountID ) == false )
            {
                (void)host.respondError( context._token, sw::OnlineError::kConflict );
                return;
            }
            (void)host.respondOk( context._token, sw::BitWriter{} );
        }
    };
} // namespace test

namespace test
{
    /** @brief 클라이언트 쪽 시험 로그인(영역만 맞추면 되는 빈 서비스 — Hello 판 목록에 들어간다). */
    class TestLoginClientService final : public sw::IOnlineClientService
    {
    public:
        uint16 getMethodRange() const override { return sw::OnlineMethodRange::kGame; }
        uint32 getProtocolVersion() const override { return 1; }
        void   onServicePush( uint16 kind, sw::BitReader& body ) override
        {
            (void)kind;
            (void)body;
        }
    };
} // namespace test

namespace test
{
    /**
     * @brief 가짜 접속 상태 — 시험이 계정 → 서버를 적어 두고, 찾기는 다음 `tick` 에 맡긴 델리게이트로 알린다(실제 `OnlinePresence` 와 같은 "한 번 · 나중에").
     *        원격 알림은 받는 계정 · 종류만 적는다(`_listRemotePush`).
     */
    class FakeAccountPresence final : public sw::IAccountPresence
    {
    public:
        struct RemotePush
        {
            sw::AccountID _accountID{ sw::kInvalidAccountID };
            uint16        _kind{ 0 };
        };

        sw::vector<RemotePush> _listRemotePush;

        FakeAccountPresence()
            : _listRemotePush{}
            , _mapAccountToEntry{}
            , _listPending{}
            , _nextRequestID{ 1 }
        {
        }

        void setOnline( const sw::AccountIdentity& identity, uint64 serverID ) { _mapAccountToEntry[identity._accountID] = Entry{ identity, serverID }; }
        void setOffline( sw::AccountID accountID ) { _mapAccountToEntry.erase( accountID ); }

        /** @brief 쌓인 찾기 결과를 알립니다(서버 `tick` 이 부른다). */
        void tick()
        {
            sw::vector<Pending> listPending = std::move( _listPending );
            _listPending.clear();
            for ( const Pending& pending : listPending )
            {
                if ( pending._onFound.isBound() )
                    pending._onFound( pending._result );
            }
        }

        uint64 submitFindByDisplayName( sw::string_view displayName, const sw::AccountPresenceDelegate& onFound ) override
        {
            sw::AccountPresenceResult result;
            result._requestID = _nextRequestID++;
            for ( const auto& [accountID, entry] : _mapAccountToEntry )
            {
                if ( sw::StringUtil::toLower( entry._identity._displayName.c_str() ) == sw::StringUtil::toLower( sw::string( displayName ).c_str() ) )
                {
                    result._identity = entry._identity;
                    result._serverID = entry._serverID;
                }
            }
            _listPending.push_back( Pending{ result, onFound } );
            return result._requestID;
        }

        uint64 submitFindByAccount( sw::AccountID accountID, const sw::AccountPresenceDelegate& onFound ) override
        {
            sw::AccountPresenceResult result;
            result._requestID  = _nextRequestID++;
            const auto entryIt = _mapAccountToEntry.find( accountID );
            if ( entryIt != _mapAccountToEntry.end() )
            {
                result._identity._accountID = accountID;
                result._serverID            = entryIt->second._serverID;
            }
            _listPending.push_back( Pending{ result, onFound } );
            return result._requestID;
        }

        bool sendRemotePush( sw::AccountID accountID, uint16 kind, const sw::BitWriter& body ) override
        {
            (void)body;
            _listRemotePush.push_back( RemotePush{ accountID, kind } );
            return true;
        }

        void cancel( uint64 requestID ) override
        {
            for ( Pending& pending : _listPending )
            {
                if ( pending._result._requestID == requestID )
                    pending._onFound = sw::AccountPresenceDelegate{};
            }
        }

        /** @brief 아직 알릴(취소하지 않은) 찾기 수입니다. */
        int32 getLivePendingCount() const
        {
            int32 count = 0;
            for ( const Pending& pending : _listPending )
            {
                count += pending._onFound.isBound() ? 1 : 0;
            }
            return count;
        }

    private:
        struct Entry
        {
            sw::AccountIdentity _identity{};
            uint64              _serverID{ 0 };
        };

        struct Pending
        {
            sw::AccountPresenceResult   _result{};
            sw::AccountPresenceDelegate _onFound{};
        };

        sw::unordered_map<sw::AccountID, Entry> _mapAccountToEntry;
        sw::vector<Pending>                     _listPending;
        uint64                                  _nextRequestID;
    };
} // namespace test

namespace test
{
    /** @brief 서버에 올린 키트 묶음(로직 + 바인딩)입니다 — 서버가 내려가기 전에 `stop` 이 불린다(그 뒤로 키트는 호스트 · 저장소를 만지지 않는다). */
    class IOnlineTestKit
    {
    public:
        virtual ~IOnlineTestKit() = default;
        virtual void stop()       = 0;
    };
} // namespace test

namespace test
{
    /** @brief 서버 프로세스 하나입니다. 키트 서비스는 시험이 `_host.registerService` 로 올린 뒤 `start` 하고, 키트 묶음은 `addKit` 으로 맡긴다. */
    class OnlineTestServer final
    {
    public:
        static constexpr uint16 kFirstPort = 7400;

        sw::vector<IOnlineTestKit*>          _listKit;
        sw::MemoryServiceStore               _store;
        sw::MemoryEphemeralStore             _cache;
        sw::LocalServerBus                   _bus;
        FakeAccountPresence                  _presence;
        TestLoginService                     _loginService;
        sw::unique_ptr<sw::IStreamTransport> _transport;
        sw::OnlineServiceHost                _host;
        uint16                               _port;

        OnlineTestServer( sw::LoopbackStreamNetwork& network, sw::MemoryServiceDatabase* pDatabase, sw::MemoryEphemeralDatabase* pCacheDatabase,
                          sw::LocalServerBusHub* pBusHub, uint64 serverID )
            : _listKit{}
            , _store{ pDatabase }
            , _cache{ pCacheDatabase }
            , _bus{ pBusHub, serverID }
            , _presence{}
            , _loginService{}
            , _transport{ network.createTransport() }
            , _host{}
            , _port{ static_cast<uint16>( kFirstPort + serverID ) }
        {
            SW_EXPECT_TRUE( _host.registerService( &_loginService ) );
        }

        ~OnlineTestServer()
        {
            for ( size_t index = _listKit.size(); index > 0; --index ) // 키트 먼저 — 저장 일을 거두고 캐시 · 접속 상태 요청을 취소한다
            {
                _listKit[index - 1]->stop();
            }
            _store.shutdown();
            _host.shutdown(); // 키트 밖(시험이 직접 올린 서비스)의 캐시 요청은 여기서 Unavailable 로 끝난다
            (void)_store.pollCompletions();
            _cache.shutdown();
        }

        /** @brief 키트 묶음을 맡깁니다 — 이 서버가 내려가기 전에(소멸자 맨 앞) 올린 반대 순서로 `stop` 한다. */
        void addKit( IOnlineTestKit* pKit ) { _listKit.push_back( pKit ); }

        /** @brief 받기 시작합니다(서비스를 모두 올린 뒤). */
        void start()
        {
            sw::OnlineServiceHostSettings settings;
            settings._transportSettings._ioThreadCount = 0;
            settings._listenAddress                    = sw::NetAddress::makeLoopback( _port );
            settings._pServiceStore                    = &_store;
            settings._pEphemeralStore                  = &_cache;
            settings._pServerBus                       = &_bus;
            settings._requestBurstPerRemote            = 1000;
            settings._requestBurstPerAccount           = 1000;
            sw::string error;
            SW_EXPECT_TRUE_MSG( _host.initialize( _transport.get(), settings, error ), error.c_str() );
        }

        uint64 getServerID() const { return _bus.getServerID(); }

        /** @brief 호스트 틱(전송 · 저장소 완료 · 캐시 라우터 · 버스 · 서비스) 뒤 가짜 접속 상태의 결과를 알립니다. */
        void tick( int64 nowMs )
        {
            _host.tick( nowMs );
            _presence.tick();
        }
    };
} // namespace test

namespace test
{
    /** @brief 클라이언트 묶음 — 끝점 하나 · 요청 클라이언트 하나에 연결 여럿(공유 끝점 모드 — 부하 시험 봇과 같은 모양). */
    class OnlineTestClients final : public sw::IStreamEndpointListener
    {
    public:
        explicit OnlineTestClients( sw::LoopbackStreamNetwork& network )
            : _transport{ network.createTransport() }
            , _endpoint{}
            , _requestClient{}
            , _listClient{}
            , _listHandle{}
            , _loginClientService{}
        {
            sw::StreamTransportSettings transportSettings;
            transportSettings._ioThreadCount = 0;
            SW_EXPECT_TRUE( _endpoint.initialize( _transport.get(), sw::StreamEndpointSettings{} ) );
            SW_EXPECT_TRUE( _transport->initialize( &_endpoint, transportSettings ) );
            _requestClient.initialize( &_endpoint );
        }

        ~OnlineTestClients() override
        {
            for ( sw::unique_ptr<sw::OnlineServiceClient>& client : _listClient )
            {
                client->shutdown();
            }
            _requestClient.shutdown();
            _transport->shutdown();
            _endpoint.shutdown();
        }

        /** @brief 연결을 열고 @p listService 를 등록합니다(시험 로그인 서비스는 언제나 함께). 연결 번호입니다 — 열리면 Hello 를 한다. */
        int32 connect( uint16 port, std::initializer_list<sw::IOnlineClientService*> listService )
        {
            sw::unique_ptr<sw::OnlineServiceClient>& client = _listClient.emplace_back( sw::make_unique<sw::OnlineServiceClient>() );
            SW_EXPECT_TRUE( client->registerClientService( &_loginClientService ) );
            for ( sw::IOnlineClientService* pService : listService )
            {
                SW_EXPECT_TRUE( client->registerClientService( pService ) );
            }
            _listHandle.push_back( _endpoint.connect( sw::NetAddress::makeLoopback( port ) ) );
            return static_cast<int32>( _listClient.size() ) - 1;
        }

        /** @brief 시험 로그인을 보냅니다(응답은 기다리지 않는다 — 다음 `tickAll` 뒤면 붙어 있다). */
        void login( int32 clientIndex, sw::AccountID accountID )
        {
            sw::BitWriter body;
            body.writeVarUint( accountID );
            (void)getClient( clientIndex ).sendRequest( TestLoginService::kLoginMethod, body, sw::NetRequestOptions{}, sw::OnlineResponseDelegate{} );
        }

        /** @brief 연결을 끊습니다(서버는 계정이 떠났다고 본다). */
        void drop( int32 clientIndex ) { _endpoint.close( _listHandle[static_cast<size_t>( clientIndex )], sw::StreamCloseMode::Abort ); }

        sw::OnlineServiceClient& getClient( int32 clientIndex ) { return *_listClient[static_cast<size_t>( clientIndex )]; }

        void tick( int64 nowMs )
        {
            (void)_transport->pollIO( 0 );
            (void)_endpoint.pump( *this );
            _requestClient.update();
            for ( sw::unique_ptr<sw::OnlineServiceClient>& client : _listClient )
            {
                client->tick( nowMs );
            }
        }

        // IStreamEndpointListener — 연결마다 그 클라이언트로
        void onEndpointOpened( sw::StreamConnectionHandle handle, const sw::NetAddress& remote, bool bAccepted ) override
        {
            (void)remote;
            (void)bAccepted;
            const int32 clientIndex = findClient( handle );
            if ( clientIndex >= 0 )
                SW_EXPECT_TRUE( getClient( clientIndex ).initializeOnSharedEndpoint( &_endpoint, &_requestClient, handle, sw::OnlineServiceClientSettings{} ) );
        }

        void onEndpointFrame( sw::StreamConnectionHandle handle, sw::StreamFrameKind kind, const uint8* pBody, int32 bodySize ) override
        {
            if ( _requestClient.handleFrame( handle, kind, pBody, bodySize ) )
                return;
            const int32 clientIndex = findClient( handle );
            if ( clientIndex >= 0 )
                getClient( clientIndex ).handleFrame( kind, pBody, bodySize );
        }

        void onEndpointClosed( sw::StreamConnectionHandle handle, sw::StreamCloseReason reason ) override
        {
            _requestClient.onConnectionClosed( handle );
            const int32 clientIndex = findClient( handle );
            if ( clientIndex >= 0 )
                getClient( clientIndex ).handleClosed( reason );
        }

    private:
        int32 findClient( sw::StreamConnectionHandle handle ) const
        {
            for ( size_t index = 0; index < _listHandle.size(); ++index )
            {
                if ( _listHandle[index] == handle )
                    return static_cast<int32>( index );
            }
            return -1;
        }

        sw::unique_ptr<sw::IStreamTransport>                _transport;
        sw::StreamMessageEndpoint                           _endpoint;
        sw::NetRequestClient                                _requestClient;
        sw::vector<sw::unique_ptr<sw::OnlineServiceClient>> _listClient;
        sw::vector<sw::StreamConnectionHandle>              _listHandle;
        TestLoginClientService                              _loginClientService;
    };

    /** @brief 클라이언트 묶음과 서버들을 번갈아 @p roundCount 번 돌립니다(요청 → 응답 · 알림 한 왕복에 넉넉한 수). */
    inline void tickAll( std::initializer_list<OnlineTestServer*> listServer, OnlineTestClients& clients, int64 nowMs, int32 roundCount = 6 )
    {
        for ( int32 round = 0; round < roundCount; ++round )
        {
            clients.tick( nowMs );
            for ( OnlineTestServer* pServer : listServer )
            {
                pServer->tick( nowMs );
            }
        }
    }
} // namespace test
