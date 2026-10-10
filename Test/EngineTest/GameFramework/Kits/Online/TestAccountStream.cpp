// 계정 스트림 바인딩 — 루프백 스트림(평문 · TLS 1.3) 위 AccountServer + AccountClient + 메모리 저장소, 실제 암호(OpenSSL — 가벼운 Argon2id).
// 가입 → 로그인 → 접속 표 → UDP NetHost(Encrypted + 인증기) 접속(주체 = 계정), 두 번째 로그인이 첫 연결을 밀어냄(알림 + 닫힘), 스트림이 끊기면 다시 붙어
// 재접속을 먼저 보내고 그동안 낸 표 요청은 그 뒤에, 틀린 토큰 · 변조 표 · 다른 서버 표 거절, 로그인 전 표 요청 kUnauthenticated, 낡은 빌드 kUpdateRequired,
// 서버 재시작 뒤 재접속, 운영 끊기(사유 코드), 게스트 장치 비밀(로컬 저장 봉인)로 같은 계정.
#include "pch.h"

#include "Core/Memory/Memory.h"
#include "Core/Network/Connection/NetHost.h"
#include "Core/Network/Security/INetSecurityProvider.h"
#include "Core/Network/Transport/LoopbackStreamTransport.h"
#include "Core/Network/Transport/NetTransport.h"

#include "GameFramework/Base/Online/Audit/ServiceAuditLog.h"
#include "GameFramework/Base/Online/Config/RemoteConfig.h"
#include "GameFramework/Base/Online/Local/MemoryLocalStore.h"
#include "GameFramework/Base/Online/Security/NetSecurity.h"
#include "GameFramework/Base/Online/Service/OnlineServiceClient.h"
#include "GameFramework/Base/Online/Service/OnlineServiceHost.h"
#include "GameFramework/Base/Online/Store/MemoryServiceStore.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/NetSecurityLoginCrypto.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Service/AccountConnectAuthenticator.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Service/AccountServer.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Service/LoginService.h"
#include "GameFramework/Kits/Feature/Online/Account/Shared/Api/AccountClient.h"
#include "GameFramework/Kits/Feature/Online/Account/Shared/Api/AccountDeviceSecret.h"
#include "GameFramework/Kits/Feature/Online/Account/Shared/Protocol/AccountProtocol.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    constexpr uint16 kServerPort = 7311;

    /** @brief 시험용 고정 봉인 키입니다. */
    class FixedSealKey final : public ILocalStoreKeyProvider
    {
    public:
        bool getSealKey( uint8 ( &outKey )[kKeySize] ) override
        {
            Memory::set( outKey, 0x3C, kKeySize );
            return true;
        }
    };

    struct TLSPair
    {
        unique_ptr<ITLSContext> _serverContext;
        unique_ptr<ITLSContext> _clientContext;

        TLSPair()
            : _serverContext{}
            , _clientContext{}
        {
            INetSecurityProvider& provider = NetSecurity::getProvider();
            string                certificatePem;
            string                privateKeyPem;
            (void)provider.createSelfSignedCertificate( "localhost", 1, certificatePem, privateKeyPem ); // 실패면 PEM 이 비어 아래 TLS 준비가 실패로 드러난다
            TLSContextSettings serverTLS;
            serverTLS._role           = TLSRole::Server;
            serverTLS._certificatePem = certificatePem;
            serverTLS._privateKeyPem  = privateKeyPem;
            TLSContextSettings clientTLS;
            clientTLS._role       = TLSRole::Client;
            clientTLS._trustPem   = certificatePem;
            clientTLS._serverName = "localhost";
            string error;
            _serverContext = provider.createTLSContext( serverTLS, error );
            _clientContext = provider.createTLSContext( clientTLS, error );
        }
    };

    /** @brief 서버 프로세스 하나 — 호스트 + 로그인 서비스 + 바인딩. 저장소 데이터 · 설정은 밖에서 빌린다. */
    struct ServerSide
    {
        MemoryServiceStore           _store;
        NetSecurityLoginCrypto       _crypto;
        LoginService                 _loginService;
        AccountServer                _accountServer;
        unique_ptr<IStreamTransport> _transport;
        OnlineServiceHost            _host;

        ServerSide( LoopbackStreamNetwork& network, MemoryServiceDatabase& database, ITLSContext* pTLSContext, const RemoteConfig* pRemoteConfig )
            : _store{ &database }
            , _crypto{ &NetSecurity::getProvider() }
            , _loginService{}
            , _accountServer{}
            , _transport{ network.createTransport() }
            , _host{}
        {
            LoginSettings settings;
            settings._passwordHashParams._memoryKiB                        = 256; // 시험 — 가벼운 Argon2id
            settings._passwordHashParams._iterationCount                   = 1;
            const uint8 arrMasterKey[LoginTicketAuthority::kMasterKeySize] = { 9, 8, 7, 6, 5, 4, 3, 2, 1 };
            _loginService.initialize( &_store, &_crypto, settings, arrMasterKey );
            _loginService.setRemoteConfig( pRemoteConfig );
            _accountServer.initialize( &_loginService, AccountServerSettings{} );
            SW_EXPECT_TRUE( _host.registerService( &_accountServer ) );
            OnlineServiceHostSettings hostSettings;
            hostSettings._transportSettings._ioThreadCount        = 0;
            hostSettings._endpointSettings._security._pTLSContext = pTLSContext;
            hostSettings._listenAddress                           = NetAddress::makeLoopback( kServerPort );
            hostSettings._pServiceStore                           = &_store;
            hostSettings._requestBurstPerRemote                   = 1000;
            hostSettings._requestBurstPerAccount                  = 1000;
            string error;
            SW_EXPECT_TRUE_MSG( _host.initialize( _transport.get(), hostSettings, error ), error.c_str() );
        }

        ~ServerSide()
        {
            _host.shutdown();
            _store.shutdown();
            (void)_store.pollCompletions();
            _accountServer.shutdown();
            _loginService.shutdown();
        }
    };

    /** @brief 클라이언트 하나 — 서비스 클라이언트(자기 끝점) + 계정 클라이언트. */
    struct ClientSide
    {
        unique_ptr<IStreamTransport> _transport;
        AccountClient                _account;
        OnlineServiceClient          _client;

        ClientSide( LoopbackStreamNetwork& network, ITLSContext* pTLSContext, const AccountClientInfo& clientInfo = AccountClientInfo{} )
            : _transport{ network.createTransport() }
            , _account{}
            , _client{}
        {
            _account.initialize( &_client, clientInfo );
            SW_EXPECT_TRUE( _client.registerClientService( &_account ) );
            OnlineServiceClientSettings settings;
            settings._transportSettings._ioThreadCount        = 0;
            settings._endpointSettings._security._pTLSContext = pTLSContext;
            settings._serverAddress                           = NetAddress::makeLoopback( kServerPort );
            settings._gameBuild                               = "test";
            settings._maxBackoffMs                            = 200;
            string error;
            SW_EXPECT_TRUE_MSG( _client.initialize( _transport.get(), settings, error ), error.c_str() );
        }

        ~ClientSide() { _client.shutdown(); }
    };

    struct AccountRig
    {
        LoopbackStreamNetwork  _network;
        TLSPair                _tls;
        MemoryServiceDatabase  _database;
        RemoteConfig           _remoteConfig;
        unique_ptr<ServerSide> _server;
        vector<ClientSide*>    _listClient;
        int64                  _nowMs;
        bool                   _bSecure;

        explicit AccountRig( bool bSecure )
            : _network{ 21u }
            , _tls{}
            , _database{}
            , _remoteConfig{}
            , _server{}
            , _listClient{}
            , _nowMs{ 100000 }
            , _bSecure{ bSecure }
        {
            restartServer();
        }

        ITLSContext* getServerContext() const { return _bSecure ? _tls._serverContext.get() : nullptr; }
        ITLSContext* getClientContext() const { return _bSecure ? _tls._clientContext.get() : nullptr; }

        void restartServer()
        {
            _server.reset();
            _server = make_unique<ServerSide>( _network, _database, getServerContext(), &_remoteConfig );
        }

        void step( int32 count = 1 )
        {
            for ( int32 index = 0; index < count; ++index )
            {
                _nowMs += 20;
                for ( ClientSide* pClient : _listClient )
                {
                    pClient->_client.tick( _nowMs );
                }
                if ( _server != nullptr )
                    _server->_host.tick( _nowMs );
            }
        }

        bool waitReady( ClientSide& client )
        {
            for ( int32 attempt = 0; attempt < 400 && client._client.isReady() == false; ++attempt )
            {
                step();
            }
            return client._client.isReady();
        }

        /** @brief 요청 id 의 응답이 올 때까지 돌립니다. */
        AccountClientReply waitReply( ClientSide& client, uint64 requestID )
        {
            for ( int32 attempt = 0; attempt < 400; ++attempt )
            {
                vector<AccountClientReply> listReply;
                (void)client._account.pollReplies( listReply );
                for ( AccountClientReply& reply : listReply )
                {
                    if ( reply._requestID == requestID )
                        return std::move( reply );
                }
                step();
            }
            AccountClientReply lost;
            lost._result    = LoginResult::StoreUnavailable;
            lost._errorCode = OnlineError::kInternal;
            return lost;
        }
    };

    /** @brief UDP 게임 서버 + 클라이언트 한 쌍(루프백 데이터그램). */
    struct UDPPair
    {
        LoopbackNetwork             _network{};
        AccountConnectAuthenticator _authenticator;
        NetHost                     _server{};
        NetHost                     _client{};
        float64                     _time{ 0.0 };

        UDPPair( const LoginTicketAuthority& authority, const utf8* pServerID, int64 nowMs )
            : _authenticator{ &authority, hashed_string( pServerID ) }
        {
            _authenticator.setNowMs( nowMs );
            NetHostSettings serverSettings;
            serverSettings._saltSeed                 = 31u;
            serverSettings._security._mode           = NetSecurityMode::Encrypted;
            serverSettings._security._pProvider      = &NetSecurity::getProvider();
            serverSettings._security._pAuthenticator = &_authenticator;
            NetHostSettings clientSettings;
            clientSettings._saltSeed            = 32u;
            clientSettings._security._mode      = NetSecurityMode::Encrypted;
            clientSettings._security._pProvider = &NetSecurity::getProvider();
            _server.initialize( _network.createEndpoint( 4100 ), serverSettings );
            _client.initialize( _network.createEndpoint( 5100 ), clientSettings );
            (void)_server.listen();
        }

        void run( float64 seconds )
        {
            for ( float64 elapsed = 0.0; elapsed < seconds; elapsed += 1.0 / 60.0 )
            {
                _time += 1.0 / 60.0;
                _server.update( _time );
                _client.update( _time );
            }
        }
    };
} // namespace

SW_TEST_CASE( AccountStreamTest, LoginIssuesATicketThatOpensAnEncryptedUDPConnection )
{
    const bool arrSecure[] = { false, true };
    for ( const bool bSecure : arrSecure )
    {
        AccountRig rig{ bSecure };
        ClientSide client{ rig._network, rig.getClientContext() };
        rig._listClient.push_back( &client );
        SW_ASSERT_TRUE( rig.waitReady( client ) );

        const AccountClientReply registered = rig.waitReply( client, client._account.registerAccount( "Udp_Hero", "password123" ) );
        SW_ASSERT_TRUE( registered._result == LoginResult::Ok );
        const AccountClientReply loggedIn = rig.waitReply( client, client._account.login( "udp_hero", "password123" ) );
        SW_ASSERT_TRUE( loggedIn._result == LoginResult::Ok );
        SW_EXPECT_TRUE( client._account.isLoggedIn() );
        SW_EXPECT_EQUAL( registered._grant._identity._accountID, loggedIn._grant._identity._accountID );
        SW_EXPECT_EQUAL( loggedIn._grant._token._sessionID, rig._server->_accountServer.findSessionID( loggedIn._grant._identity._accountID ) );

        const AccountClientReply ticket = rig.waitReply( client, client._account.issueGameTicket( "zone-1" ) );
        SW_ASSERT_TRUE( ticket._result == LoginResult::Ok );

        UDPPair               udp{ rig._server->_loginService.getTicketAuthority(), "zone-1", rig._nowMs };
        NetConnectCredentials credentials;
        AccountClient::makeConnectCredentials( ticket._ticket, credentials );
        SW_ASSERT_TRUE( udp._client.connect( NetAddress::makeLoopback( 4100 ), credentials ) );
        udp.run( 0.5 );
        SW_ASSERT_EQUAL( 1, udp._server.getConnectedCount() );
        vector<NetHostEvent> listEvent;
        udp._server.drainEvents( listEvent );
        int32 connectionID = -1;
        for ( const NetHostEvent& event : listEvent )
        {
            if ( event._kind == NetHostEvent::Kind::Connected )
                connectionID = event._connectionID;
        }
        SW_ASSERT_TRUE( connectionID >= 0 );
        SW_EXPECT_EQUAL( loggedIn._grant._identity._accountID, udp._server.getConnectionPrincipal( connectionID ) );
        rig._listClient.clear();
    }
}

SW_TEST_CASE( AccountStreamTest, ForgedOrForeignTicketsAndBadTokensAreRefused )
{
    AccountRig rig{ false };
    ClientSide client{ rig._network, nullptr };
    rig._listClient.push_back( &client );
    SW_ASSERT_TRUE( rig.waitReady( client ) );
    SW_ASSERT_TRUE( rig.waitReply( client, client._account.registerAccount( "ticket_fan", "password123" ) )._result == LoginResult::Ok );

    const AccountClientReply early = rig.waitReply( client, client._account.issueGameTicket( "zone-1" ) ); // 로그인 전
    SW_EXPECT_EQUAL( OnlineError::kUnauthenticated, early._errorCode );

    LoginSessionToken bogus;
    bogus._sessionID = 12345;
    client._account.setToken( bogus );
    SW_EXPECT_TRUE( rig.waitReply( client, client._account.resume() )._result == LoginResult::InvalidToken );

    SW_ASSERT_TRUE( rig.waitReply( client, client._account.login( "ticket_fan", "password123" ) )._result == LoginResult::Ok );
    const AccountClientReply ticket = rig.waitReply( client, client._account.issueGameTicket( "zone-1" ) );
    SW_ASSERT_TRUE( ticket._result == LoginResult::Ok );

    NetGameTicket forged = ticket._ticket;
    forged._arrToken[3] ^= 0x01;
    UDPPair               forgedPair{ rig._server->_loginService.getTicketAuthority(), "zone-1", rig._nowMs };
    NetConnectCredentials forgedCredentials;
    AccountClient::makeConnectCredentials( forged, forgedCredentials );
    SW_ASSERT_TRUE( forgedPair._client.connect( NetAddress::makeLoopback( 4100 ), forgedCredentials ) );
    forgedPair.run( 0.5 );
    SW_EXPECT_EQUAL( 0, forgedPair._server.getConnectedCount() );

    UDPPair               otherZone{ rig._server->_loginService.getTicketAuthority(), "zone-2", rig._nowMs };
    NetConnectCredentials credentials;
    AccountClient::makeConnectCredentials( ticket._ticket, credentials );
    SW_ASSERT_TRUE( otherZone._client.connect( NetAddress::makeLoopback( 4100 ), credentials ) );
    otherZone.run( 0.5 );
    SW_EXPECT_EQUAL( 0, otherZone._server.getConnectedCount() );

    UDPPair expired{ rig._server->_loginService.getTicketAuthority(), "zone-1", ticket._ticket._expiresAtMs };
    SW_ASSERT_TRUE( expired._client.connect( NetAddress::makeLoopback( 4100 ), credentials ) );
    expired.run( 0.5 );
    SW_EXPECT_EQUAL( 0, expired._server.getConnectedCount() );
}

SW_TEST_CASE( AccountStreamTest, SecondLoginKicksTheFirstConnection )
{
    AccountRig rig{ true };
    ClientSide first{ rig._network, rig.getClientContext() };
    ClientSide second{ rig._network, rig.getClientContext() };
    rig._listClient.push_back( &first );
    rig._listClient.push_back( &second );
    SW_ASSERT_TRUE( rig.waitReady( first ) );
    SW_ASSERT_TRUE( rig.waitReady( second ) );
    SW_ASSERT_TRUE( rig.waitReply( first, first._account.registerAccount( "twice", "password123" ) )._result == LoginResult::Ok );
    SW_ASSERT_TRUE( rig.waitReply( first, first._account.login( "twice", "password123" ) )._result == LoginResult::Ok );
    const AccountClientReply secondLogin = rig.waitReply( second, second._account.login( "twice", "password123" ) );
    SW_ASSERT_TRUE( secondLogin._result == LoginResult::Ok );
    rig.step( 10 );
    vector<AccountClientEvent> listEvent;
    (void)first._account.pollEvents( listEvent );
    SW_ASSERT_EQUAL( size_t( 1 ), listEvent.size() );
    SW_EXPECT_TRUE( listEvent[0]._reason == LoginRevokeReason::DuplicateLogin );
    SW_EXPECT_FALSE( first._account.isLoggedIn() );
    SW_EXPECT_TRUE( first._account.getToken().isEmpty() ); // 밀려난 토큰으로 돌아오지 않는다
    SW_EXPECT_TRUE( second._account.isLoggedIn() );
    SW_EXPECT_EQUAL( secondLogin._grant._token._sessionID, rig._server->_accountServer.findSessionID( secondLogin._grant._identity._accountID ) );
}

SW_TEST_CASE( AccountStreamTest, DroppedStreamReconnectsResumesFirstAndThenSendsQueuedRequests )
{
    AccountRig rig{ false };
    ClientSide client{ rig._network, nullptr };
    rig._listClient.push_back( &client );
    SW_ASSERT_TRUE( rig.waitReady( client ) );
    SW_ASSERT_TRUE( rig.waitReply( client, client._account.registerAccount( "roamer", "password123" ) )._result == LoginResult::Ok );
    const AccountClientReply login = rig.waitReply( client, client._account.login( "roamer", "password123" ) );
    SW_ASSERT_TRUE( login._result == LoginResult::Ok );

    client._transport->close( client._client.getConnection(), StreamCloseMode::Abort ); // 회선이 끊겼다
    rig.step( 2 );
    SW_ASSERT_TRUE( rig.waitReady( client ) );                                              // 물러남 뒤 다시 붙어 Hello
    const uint64               ticketRequest = client._account.issueGameTicket( "zone-1" ); // 재접속 응답 전 — 모았다가 그 뒤에
    vector<AccountClientReply> listReply;
    for ( int32 attempt = 0; attempt < 400 && listReply.size() < 2; ++attempt )
    {
        (void)client._account.pollReplies( listReply );
        rig.step();
    }
    SW_ASSERT_EQUAL( size_t( 2 ), listReply.size() );
    SW_EXPECT_TRUE( listReply[0]._operation == AccountClientOperation::Resume );
    SW_EXPECT_TRUE( listReply[0]._bAutomatic == SW_TRUE );
    SW_ASSERT_TRUE( listReply[0]._result == LoginResult::Ok );
    SW_EXPECT_EQUAL( login._grant._identity._accountID, listReply[0]._grant._identity._accountID );
    SW_EXPECT_EQUAL( login._grant._token._sessionID, listReply[0]._grant._token._sessionID );                                                          // 같은 세션
    SW_EXPECT_FALSE( Memory::compare( login._grant._token._arrSecret, listReply[0]._grant._token._arrSecret, LoginConstant::kTokenSecretSize ) == 0 ); // 새 비밀
    SW_EXPECT_EQUAL( ticketRequest, listReply[1]._requestID );
    SW_EXPECT_TRUE( listReply[1]._result == LoginResult::Ok );
}

SW_TEST_CASE( AccountStreamTest, ServerRestartKeepsSessionsAndOldBuildsAreToldToUpdate )
{
    AccountRig rig{ true };
    {
        ClientSide client{ rig._network, rig.getClientContext() };
        rig._listClient.push_back( &client );
        SW_ASSERT_TRUE( rig.waitReady( client ) );
        SW_ASSERT_TRUE( rig.waitReply( client, client._account.registerAccount( "survivor", "password123" ) )._result == LoginResult::Ok );
        const AccountClientReply login = rig.waitReply( client, client._account.login( "survivor", "password123" ) );
        SW_ASSERT_TRUE( login._result == LoginResult::Ok );

        rig.restartServer(); // 프로세스가 죽고 다시 떴다 — 저장소 데이터는 그대로
        SW_ASSERT_TRUE( rig.waitReady( client ) );
        vector<AccountClientReply> listReply;
        for ( int32 attempt = 0; attempt < 400 && listReply.empty(); ++attempt )
        {
            (void)client._account.pollReplies( listReply );
            rig.step();
        }
        SW_ASSERT_EQUAL( size_t( 1 ), listReply.size() );
        SW_EXPECT_TRUE( listReply[0]._result == LoginResult::Ok );
        SW_EXPECT_EQUAL( login._grant._identity._accountID, listReply[0]._grant._identity._accountID );
        rig._listClient.clear();
    }

    ServiceAuditEntry audit;
    audit._actor   = "gm.0000000000000001";
    audit._action  = "config.set";
    audit._subject = "config";
    audit._timeMs  = 1;
    RemoteConfigValue minimum;
    minimum._type = RemoteConfigValueType::Text;
    minimum._text = "2.0";
    RemoteConfigValue storeURL;
    storeURL._type = RemoteConfigValueType::Text;
    storeURL._text = "https://store.example/app";
    rig._remoteConfig.submitSet( rig._server->_store, nullptr, "account.minimum_build.windows", minimum, audit );
    rig._remoteConfig.submitSet( rig._server->_store, nullptr, "account.store_url.windows", storeURL, audit );
    (void)rig._server->_store.pollCompletions();
    AccountClientInfo oldBuild;
    oldBuild._build    = "1.9";
    oldBuild._platform = "windows";
    ClientSide stale{ rig._network, rig.getClientContext(), oldBuild };
    rig._listClient.push_back( &stale );
    SW_ASSERT_TRUE( rig.waitReady( stale ) );
    const AccountClientReply refused = rig.waitReply( stale, stale._account.login( "survivor", "password123" ) );
    SW_EXPECT_EQUAL( OnlineError::kUpdateRequired, refused._errorCode );
    SW_EXPECT_TRUE( refused._result == LoginResult::UpdateRequired );
    SW_EXPECT_EQUAL( string( "https://store.example/app" ), refused._grant._storeURL );
}

SW_TEST_CASE( AccountStreamTest, AdministrativeRevokeCarriesTheReasonCodeAndGuestSecretComesFromLocalStore )
{
    AccountRig rig{ false };
    ClientSide client{ rig._network, nullptr };
    rig._listClient.push_back( &client );
    SW_ASSERT_TRUE( rig.waitReady( client ) );

    MemoryLocalDatabase localDatabase;
    FixedSealKey        sealKey;
    LocalSealContext    sealContext;
    sealContext._pSecurityProvider = &NetSecurity::getProvider();
    sealContext._pKeyProvider      = &sealKey;
    MemoryLocalStore    localStore{ &localDatabase, sealContext };
    AccountDeviceSecret firstSecret;
    firstSecret.begin( &localStore, &NetSecurity::getProvider() );
    for ( int32 attempt = 0; attempt < 4 && firstSecret.getState() != AccountDeviceSecretState::Ready; ++attempt )
    {
        vector<LocalStoreCompletion> listCompletion;
        (void)localStore.pollCompletions( listCompletion );
        for ( const LocalStoreCompletion& completion : listCompletion )
        {
            (void)firstSecret.handleCompletion( completion );
        }
    }
    SW_ASSERT_TRUE( firstSecret.getState() == AccountDeviceSecretState::Ready );
    AccountDeviceSecret again; // 다음 실행 — 같은 비밀을 읽는다
    again.begin( &localStore, &NetSecurity::getProvider() );
    vector<LocalStoreCompletion> listCompletion;
    (void)localStore.pollCompletions( listCompletion );
    for ( const LocalStoreCompletion& completion : listCompletion )
    {
        (void)again.handleCompletion( completion );
    }
    SW_ASSERT_TRUE( again.getState() == AccountDeviceSecretState::Ready );
    SW_EXPECT_EQUAL( 0, Memory::compare( firstSecret.getSecret(), again.getSecret(), LoginConstant::kDeviceSecretSize ) );

    const AccountClientReply guest = rig.waitReply( client, client._account.guestLogin( again.getSecret() ) );
    SW_ASSERT_TRUE( guest._result == LoginResult::Ok );
    SW_EXPECT_TRUE( guest._grant._bCreated == SW_TRUE );
    SW_EXPECT_TRUE( client._account.getIdentity()._bGuest == SW_TRUE );

    IAccountSessionControl& control = rig._server->_accountServer;
    control.revokeAccountSessions( guest._grant._identity._accountID, "sanction.cheating", rig._nowMs );
    rig.step( 10 );
    vector<AccountClientEvent> listEvent;
    (void)client._account.pollEvents( listEvent );
    SW_ASSERT_EQUAL( size_t( 1 ), listEvent.size() );
    SW_EXPECT_TRUE( listEvent[0]._reason == LoginRevokeReason::Administrative );
    SW_EXPECT_EQUAL( string( "sanction.cheating" ), listEvent[0]._reasonCode );
    SW_EXPECT_FALSE( client._account.isLoggedIn() );
}
