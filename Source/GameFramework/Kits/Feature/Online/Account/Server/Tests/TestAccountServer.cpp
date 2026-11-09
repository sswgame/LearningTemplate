// AccountServerTest — 계정 바인딩(패킷 → 로그인 서비스 → 응답 · 연결 묶기): 깨진 몸 · 모르는 메서드 · 로그인 안 한 요청, 한 연결에 계정 하나(겹쳐 보낸 로그인 포함), 중복 로그인으로 밀어내기,
// 끊김 뒤 재접속 · 위조 토큰, 운영 끊기와 사유 코드, 다른 서버의 끊기 알림, 공통 오류의 상세 몸(시도 제한 · 빌드). 진짜 `LoginService`(메모리 저장소, 가짜 암호)와 진짜 `AccountClient` 를
// 루프백으로 잇는다 — 저장 규칙은 LoginStoreLogicTest, 서비스 규칙은 LoginServiceTest 가 본다. 온라인 키트라 `UnitTests.OnlineKits` 가 돌린다(nogpu).
#include "pch.h"

#include "Core/Hash/HashUtil.h"
#include "Core/Testing/TestFramework.h"

#include "GameFramework/Kits/Feature/Online/Account/Runtime/API/AccountClient.h"
#include "GameFramework/Kits/Feature/Online/Account/Runtime/Protocol/AccountProtocol.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Service/AccountServer.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Service/LoginService.h"
#include "GameFramework/Runtime/Online/Audit/ServiceAuditLog.h"
#include "GameFramework/Runtime/Online/Config/Server/RemoteConfig.h"
#include "GameFramework/Runtime/Online/Service/OnlineServiceClient.h"
#include "GameFramework/Runtime/Online/Service/Server/OnlineServiceHost.h"
#include "GameFramework/Runtime/Online/Service/Server/Tests/OnlineHostTestUtil.h"

using namespace sw;

namespace
{
    constexpr int64       kNowMs    = 1700000000000ll;
    constexpr const utf8* kPassword = "password123";

    /** @brief 결정적인 가짜 암호입니다. 바인딩을 보려는 테스트라 비용 없는 해시면 된다. */
    class FakeBindingCrypto final : public ILoginCrypto
    {
    public:
        bool fillRandom( uint8* pOut, int32 size ) override
        {
            for ( int32 index = 0; index < size; ++index )
            {
                pOut[index] = static_cast<uint8>( ++_counter );
            }
            return true;
        }
        bool computePasswordHash( string_view password, const uint8* pSalt, int32 saltSize, const NetPasswordHashParams& /*params*/, uint8* pOut, int32 outSize ) override
        {
            return fill( reinterpret_cast<const uint8*>( password.data() ), static_cast<int32>( password.size() ), pSalt, saltSize, pOut, outSize );
        }
        bool computeKeyedHash( const uint8* pKey, int32 keySize, const uint8* pInfo, int32 infoSize, uint8* pOut, int32 outSize ) override
        {
            return fill( pKey, keySize, pInfo, infoSize, pOut, outSize );
        }

    private:
        static bool fill( const uint8* pA, int32 aSize, const uint8* pB, int32 bSize, uint8* pOut, int32 outSize )
        {
            uint64 hash = HashUtil::kFNVOffset64;
            for ( int32 index = 0; index < outSize; ++index )
            {
                for ( int32 a = 0; a < aSize; ++a )
                {
                    hash = HashUtil::fnvStep64( hash, pA[a] );
                }
                for ( int32 b = 0; b < bSize; ++b )
                {
                    hash = HashUtil::fnvStep64( hash, pB[b] );
                }
                hash        = HashUtil::fnvStep64( hash, static_cast<uint8>( index ) );
                pOut[index] = static_cast<uint8>( hash >> 24 );
            }
            return true;
        }

        uint32 _counter{ 0 };
    };

    /** @brief 맨 요청(`sendRequest`)의 답을 받습니다. 깨진 몸을 그대로 보내는 테스트가 씁니다. */
    struct RawResponseSink
    {
        int32  _count{ 0 };
        uint16 _errorCode{ OnlineError::kOk };

        void onResponse( const OnlineResponse& response )
        {
            ++_count;
            _errorCode = response._errorCode;
        }
    };

    /**
     * @brief 로그인 서비스 + 바인딩 + 루프백 서버 + 클라이언트들입니다.
     * @details 서버를 먼저 내린다(저장소 → 호스트 → 남은 완료) — 완료와 `onAccountLeft` 가 서비스를 부르므로 서비스의 `shutdown` 은 그 뒤다(`LoginService.h` 의 종료 순서).
     */
    struct AccountServerFixture
    {
        FakeBindingCrypto                   _crypto{};
        LoginService                        _loginService{};
        AccountServer                       _accountServer{};
        vector<unique_ptr<AccountClient>>   _listAccount{};
        unique_ptr<test::OnlineTestFixture> _pRig{};

        explicit AccountServerFixture( const LoginServiceConfig& settings = LoginServiceConfig{} )
        {
            _pRig = make_unique<test::OnlineTestFixture>( false );
            const uint8 arrMasterKey[LoginTicketAuthority::kMasterKeySize]{ 1, 2, 3 };
            _loginService.initialize( &_pRig->_server._store, &_crypto, settings, arrMasterKey );
            _accountServer.initialize( &_loginService, AccountServerConfig{} );
            SW_EXPECT_TRUE( _pRig->_server._host.registerServerService( &_accountServer ) );
            _pRig->_server.start();
        }

        ~AccountServerFixture()
        {
            _pRig.reset();
            _accountServer.shutdown();
            _loginService.shutdown();
        }

        /** @brief 새 연결과 그 계정 클라이언트를 만듭니다. @p pToken 이 있으면 Hello 뒤에 자동 재접속을 보낸다. */
        int32 addClient( const LoginSessionToken* pToken = nullptr, const AccountClientInfo& clientInfo = AccountClientInfo{} )
        {
            unique_ptr<AccountClient>& account = _listAccount.emplace_back( make_unique<AccountClient>() );
            const int32                index   = _pRig->_clients.connect( _pRig->_server._port, { account.get() } );
            account->initialize( &_pRig->_clients.getClient( index ), clientInfo );
            if ( pToken != nullptr )
                account->setToken( *pToken );
            pump();
            return index;
        }

        AccountClient&       getAccount( int32 index ) { return *_listAccount[static_cast<size_t>( index )]; }
        OnlineServiceClient& getClient( int32 index ) { return _pRig->_clients.getClient( index ); }
        OnlineServiceHost&   getHost() { return _pRig->_server._host; }

        void pump() { test::updateAll( { &_pRig->_server }, _pRig->_clients, kNowMs, 12 ); }

        vector<AccountClientReply> takeAll( int32 index )
        {
            pump();
            vector<AccountClientReply> listReply;
            (void)getAccount( index ).pollReplies( listReply );
            return listReply;
        }

        AccountClientReply take( int32 index )
        {
            vector<AccountClientReply> listReply = takeAll( index );
            SW_EXPECT_EQUAL( size_t( 1 ), listReply.size() );
            return listReply.empty() ? AccountClientReply{} : listReply.front();
        }

        vector<AccountClientEvent> takeEvents( int32 index )
        {
            pump();
            vector<AccountClientEvent> listEvent;
            (void)getAccount( index ).pollEvents( listEvent );
            return listEvent;
        }

        /** @brief 가입합니다(계정 id). */
        AccountID registerAccount( int32 index, string_view name )
        {
            (void)getAccount( index ).registerAccount( name, kPassword );
            const AccountClientReply reply = take( index );
            SW_EXPECT_TRUE( reply._result == LoginResult::Ok );
            return reply._grant._identity._accountID;
        }

        /** @brief 가입하고 로그인합니다(계정 id). */
        AccountID signIn( int32 index, string_view name )
        {
            (void)registerAccount( index, name );
            (void)getAccount( index ).login( name, kPassword );
            const AccountClientReply reply = take( index );
            SW_EXPECT_TRUE( reply._result == LoginResult::Ok );
            SW_EXPECT_TRUE( getAccount( index ).isLoggedIn() );
            return reply._grant._identity._accountID;
        }

        /** @brief 맨 요청 하나를 보내고 답을 받습니다. 몸은 쓴 그대로 간다. */
        RawResponseSink sendRaw( int32 index, uint16 method, const BitWriter& body, const NetRequestOptions& options = NetRequestOptions{} )
        {
            RawResponseSink sink;
            (void)getClient( index ).sendRequest( method, body, options, OnlineResponseDelegate::create<&RawResponseSink::onResponse>( &sink ) );
            pump();
            SW_EXPECT_EQUAL( 1, sink._count );
            return sink;
        }
    };

    /** @brief 몸을 만듭니다. */
    struct AccountServerTestUtil
    {
        static BitWriter makeJunk( int32 byteCount )
        {
            const vector<uint8> listByte( static_cast<size_t>( byteCount ), uint8( 0xFF ) );
            BitWriter           writer;
            writer.writeBytes( listByte.data(), byteCount );
            return writer;
        }

        /** @brief 정상 로그인 몸입니다. */
        static BitWriter makeLogin( string_view name )
        {
            AccountLoginRequest request;
            request._loginName = string( name );
            request._password  = kPassword;
            BitWriter writer;
            SW_EXPECT_TRUE( AccountProtocol::MessageTypes::write( writer, request ) );
            return writer;
        }

        /** @brief 정상 로그인 몸의 앞 @p byteCount 바이트만 씁니다. */
        static BitWriter makeTruncatedLogin( string_view name, int32 byteCount )
        {
            AccountLoginRequest request;
            request._loginName = string( name );
            request._password  = kPassword;
            BitWriter whole;
            SW_EXPECT_TRUE( AccountProtocol::MessageTypes::write( whole, request ) );
            BitWriter writer;
            writer.writeBytes( whole.getBytes().data(), byteCount );
            return writer;
        }
    };
} // namespace

/** @brief [AccountServerTest] 가입 → 로그인 → 로그아웃: 로그인만 연결을 묶고, 로그아웃은 세션을 지우고 묶음을 푼다 */
SW_TEST_CASE( AccountServerTest, RegisterLoginLogout )
{
    AccountServerFixture rig;
    const int32          client    = rig.addClient();
    const AccountID      accountID = rig.registerAccount( client, "alice_01" );
    SW_EXPECT_NOT_EQUAL( kInvalidAccountID, accountID );
    SW_EXPECT_FALSE( rig.getAccount( client ).isLoggedIn() ); // 가입은 연결을 묶지 않는다
    StreamConnectionHandle connection;
    SW_EXPECT_FALSE( rig.getHost().findConnection( accountID, connection ) );

    (void)rig.getAccount( client ).login( "alice_01", kPassword );
    const AccountClientReply loggedIn = rig.take( client );
    SW_REQUIRE_TRUE( loggedIn._result == LoginResult::Ok );
    SW_EXPECT_EQUAL( accountID, loggedIn._grant._identity._accountID );
    SW_EXPECT_EQUAL( loggedIn._grant._token._sessionID, rig._accountServer.findSessionID( accountID ) );
    SW_EXPECT_TRUE( rig.getHost().findConnection( accountID, connection ) );
    SW_EXPECT_TRUE( rig._loginService.isAccountOnline( accountID ) );

    (void)rig.getAccount( client ).logout();
    SW_EXPECT_TRUE( rig.take( client )._result == LoginResult::Ok );
    SW_EXPECT_EQUAL( uint64( 0 ), rig._accountServer.findSessionID( accountID ) );
    SW_EXPECT_FALSE( rig.getHost().findConnection( accountID, connection ) );
    SW_EXPECT_TRUE( rig.getAccount( client ).getToken().isEmpty() );
}

/** @brief [AccountServerTest] 업무 실패(틀린 비밀번호 · 없는 계정)는 연결을 묶지 않고, 같은 연결이 다시 로그인할 수 있다 */
SW_TEST_CASE( AccountServerTest, FailedLoginKeepsTheConnectionFree )
{
    AccountServerFixture rig;
    const int32          client    = rig.addClient();
    const AccountID      accountID = rig.registerAccount( client, "alice_01" );

    (void)rig.getAccount( client ).login( "alice_01", "wrong-password" );
    SW_EXPECT_TRUE( rig.take( client )._result == LoginResult::WrongCredentials );
    (void)rig.getAccount( client ).login( "nobody_01", kPassword );
    SW_EXPECT_TRUE( rig.take( client )._result == LoginResult::WrongCredentials );
    SW_EXPECT_EQUAL( uint64( 0 ), rig._accountServer.findSessionID( accountID ) );

    (void)rig.getAccount( client ).login( "alice_01", kPassword );
    SW_EXPECT_TRUE( rig.take( client )._result == LoginResult::Ok );
}

/** @brief [AccountServerTest] 패킷: 깨진 · 잘린 · 빈 몸은 kInvalidRequest 이고 서비스에 닿지 않는다. 그 뒤 같은 연결의 정상 로그인이 막히지 않는다 */
SW_TEST_CASE( AccountServerTest, MalformedBodiesAreRefused )
{
    AccountServerFixture rig;
    const int32          client = rig.addClient();
    (void)rig.registerAccount( client, "alice_01" );
    const int32 pendingBefore = rig._loginService.getPendingCount();

    for ( const uint16 method : { AccountMethod::Register::kID, AccountMethod::Login::kID, AccountMethod::GuestLogin::kID, AccountMethod::PlatformLogin::kID, AccountMethod::Resume::kID } )
    {
        SW_EXPECT_EQUAL( OnlineError::kInvalidRequest, rig.sendRaw( client, method, BitWriter{} )._errorCode );
        SW_EXPECT_EQUAL( OnlineError::kInvalidRequest, rig.sendRaw( client, method, AccountServerTestUtil::makeJunk( 32 ) )._errorCode );
    }
    SW_EXPECT_EQUAL( OnlineError::kInvalidRequest, rig.sendRaw( client, AccountMethod::Login::kID, AccountServerTestUtil::makeTruncatedLogin( "alice_01", 12 ) )._errorCode );
    SW_EXPECT_EQUAL( pendingBefore, rig._loginService.getPendingCount() );

    (void)rig.getAccount( client ).login( "alice_01", kPassword );
    SW_EXPECT_TRUE( rig.take( client )._result == LoginResult::Ok );
}

/** @brief [AccountServerTest] 세션이 있는 연결의 깨진 몸도 kInvalidRequest 이고 세션은 그대로다. 빈 서버 id 는 발급할 곳이 없어 같다. 모르는 메서드도 같다 */
SW_TEST_CASE( AccountServerTest, SessionMethodsCheckTheirBodies )
{
    AccountServerFixture rig;
    const int32          client    = rig.addClient();
    const AccountID      accountID = rig.signIn( client, "alice_01" );

    for ( const uint16 method : { AccountMethod::LinkCredential::kID, AccountMethod::LinkPlatform::kID, AccountMethod::UnlinkPlatform::kID, AccountMethod::IssueGameTicket::kID } )
    {
        SW_EXPECT_EQUAL( OnlineError::kInvalidRequest, rig.sendRaw( client, method, AccountServerTestUtil::makeJunk( 16 ) )._errorCode );
    }
    (void)rig.getAccount( client ).issueGameTicket( "" );
    SW_EXPECT_TRUE( rig.take( client )._result.hasError( OnlineError::kInvalidRequest ) );
    SW_EXPECT_EQUAL( OnlineError::kInvalidRequest, rig.sendRaw( client, static_cast<uint16>( OnlineMethodRange::kAccount + 0x70 ), BitWriter{} )._errorCode );
    SW_EXPECT_NOT_EQUAL( uint64( 0 ), rig._accountServer.findSessionID( accountID ) );
}

/** @brief [AccountServerTest] 로그인 전: 세션 메서드는 호스트가 kUnauthenticated 로 막는다 — 클라이언트 결과는 공통 범주 실패다(업무 결과 InvalidToken 이 아니다 — OnlineOutcome 계약) */
SW_TEST_CASE( AccountServerTest, SessionMethodsNeedALogin )
{
    AccountServerFixture rig;
    const int32          client = rig.addClient();
    (void)rig.getAccount( client ).logout();
    const AccountClientReply reply = rig.take( client );
    SW_EXPECT_EQUAL( OnlineError::kUnauthenticated, reply._errorCode );
    SW_EXPECT_TRUE( reply._result.isFailed() && reply._result._errorCode == OnlineError::kUnauthenticated );
    SW_EXPECT_EQUAL( OnlineError::kUnauthenticated, rig.sendRaw( client, static_cast<uint16>( OnlineMethodRange::kAccount + 0x70 ), BitWriter{} )._errorCode );
}

/** @brief [AccountServerTest] 호스트에 묶였어도 이 바인딩의 세션이 없는 연결(테스트 로그인)은 세션 메서드가 kUnauthenticated, 로그인 메서드가 kInvalidRequest 다 */
SW_TEST_CASE( AccountServerTest, BoundConnectionWithoutSessionIsRefused )
{
    AccountServerFixture rig;
    const int32          client = rig.addClient();
    rig._pRig->_clients.login( client, 77 );
    rig.pump();

    (void)rig.getAccount( client ).listLinks();
    SW_EXPECT_EQUAL( OnlineError::kUnauthenticated, rig.take( client )._errorCode );
    (void)rig.getAccount( client ).login( "alice_01", kPassword );
    SW_EXPECT_EQUAL( OnlineError::kInvalidRequest, rig.take( client )._errorCode );
}

/** @brief [AccountServerTest] 로그인한 연결이 다시 로그인하면 kInvalidRequest 이고 주체는 그대로다(한 연결에 계정 하나) */
SW_TEST_CASE( AccountServerTest, LoggedInConnectionCannotLogInAgain )
{
    AccountServerFixture rig;
    const int32          first     = rig.addClient();
    const AccountID      firstID   = rig.signIn( first, "alice_01" );
    const int32          second    = rig.addClient();
    const AccountID      secondID  = rig.registerAccount( second, "bob_0001" );
    const uint64         sessionID = rig._accountServer.findSessionID( firstID );

    (void)rig.getAccount( first ).login( "bob_0001", kPassword );
    const AccountClientReply reply = rig.take( first );
    SW_EXPECT_EQUAL( OnlineError::kInvalidRequest, reply._errorCode );
    SW_EXPECT_TRUE( reply._result.hasError( OnlineError::kInvalidRequest ) );
    SW_EXPECT_EQUAL( sessionID, rig._accountServer.findSessionID( firstID ) );
    SW_EXPECT_EQUAL( uint64( 0 ), rig._accountServer.findSessionID( secondID ) );
}

/** @brief [AccountServerTest] 겹쳐 보낸 로그인: 앞 로그인이 끝나기 전의 둘째는 거절한다 — 받아 주면 둘째가 묶이지 못한 채 Ok 를 받아 응답의 세션과 연결의 주체가 갈린다 */
SW_TEST_CASE( AccountServerTest, PipelinedLoginsOnOneConnection )
{
    AccountServerFixture rig;
    const int32          helper   = rig.addClient();
    const AccountID      secondID = rig.registerAccount( helper, "bob_0001" );
    const int32          client   = rig.addClient();
    const AccountID      firstID  = rig.registerAccount( client, "alice_01" );

    const uint64                     firstRequest  = rig.getAccount( client ).login( "alice_01", kPassword );
    const uint64                     secondRequest = rig.getAccount( client ).login( "bob_0001", kPassword );
    const vector<AccountClientReply> listReply     = rig.takeAll( client );
    SW_REQUIRE_EQUAL( size_t( 2 ), listReply.size() );
    for ( const AccountClientReply& reply : listReply )
    {
        if ( reply._requestID == firstRequest )
            SW_EXPECT_TRUE( reply._result == LoginResult::Ok );
        if ( reply._requestID == secondRequest )
            SW_EXPECT_EQUAL( OnlineError::kInvalidRequest, reply._errorCode );
    }
    SW_EXPECT_NOT_EQUAL( uint64( 0 ), rig._accountServer.findSessionID( firstID ) );
    SW_EXPECT_EQUAL( uint64( 0 ), rig._accountServer.findSessionID( secondID ) );
    SW_EXPECT_FALSE( rig._loginService.isAccountOnline( secondID ) );
    SW_EXPECT_EQUAL( firstID, rig.getAccount( client ).getIdentity()._accountID );
}

/** @brief [AccountServerTest] 중복 로그인: 새 연결이 이기고, 앞 연결은 DuplicateLogin 알림을 한 번 받은 뒤 끊기며 토큰을 잃는다 */
SW_TEST_CASE( AccountServerTest, DuplicateLoginKicksTheOldConnection )
{
    AccountServerFixture rig;
    const int32          first      = rig.addClient();
    const AccountID      accountID  = rig.signIn( first, "alice_01" );
    const uint64         oldSession = rig._accountServer.findSessionID( accountID );

    const int32 second = rig.addClient();
    (void)rig.getAccount( second ).login( "alice_01", kPassword );
    const AccountClientReply reply = rig.take( second );
    SW_REQUIRE_TRUE( reply._result == LoginResult::Ok );
    SW_EXPECT_EQUAL( reply._grant._token._sessionID, rig._accountServer.findSessionID( accountID ) );
    SW_EXPECT_NOT_EQUAL( oldSession, reply._grant._token._sessionID );

    const vector<AccountClientEvent> listEvent = rig.takeEvents( first );
    SW_REQUIRE_EQUAL( size_t( 1 ), listEvent.size() );
    SW_EXPECT_TRUE( listEvent[0]._reason == LoginRevokeReason::DuplicateLogin );
    SW_EXPECT_TRUE( rig.getAccount( first ).getToken().isEmpty() );
    SW_EXPECT_FALSE( rig.getAccount( first ).isLoggedIn() );
    SW_EXPECT_TRUE( rig.getAccount( second ).isLoggedIn() );
    StreamConnectionHandle connection;
    SW_EXPECT_TRUE( rig.getHost().findConnection( accountID, connection ) );
}

/** @brief [AccountServerTest] 끊김: 묶음과 세션 표에서 빠지지만 저장소의 세션은 유예로 남아, 새 연결이 토큰으로 같은 세션에 돌아오고 비밀은 새로 받는다 */
SW_TEST_CASE( AccountServerTest, DisconnectThenResumeOnANewConnection )
{
    AccountServerFixture    rig;
    const int32             first     = rig.addClient();
    const AccountID         accountID = rig.signIn( first, "alice_01" );
    const LoginSessionToken token     = rig.getAccount( first ).getToken();

    rig._pRig->_clients.drop( first );
    rig.pump();
    SW_EXPECT_EQUAL( uint64( 0 ), rig._accountServer.findSessionID( accountID ) );
    SW_EXPECT_FALSE( rig._loginService.isAccountOnline( accountID ) );
    StreamConnectionHandle connection;
    SW_EXPECT_FALSE( rig.getHost().findConnection( accountID, connection ) );

    const int32              second = rig.addClient( &token );
    const AccountClientReply reply  = rig.take( second );
    SW_REQUIRE_TRUE( reply._result == LoginResult::Ok );
    SW_EXPECT_TRUE( reply._operation == AccountClientOperation::Resume );
    SW_EXPECT_TRUE( reply._bAutomatic == SW_TRUE );
    SW_EXPECT_EQUAL( token._sessionID, rig.getAccount( second ).getToken()._sessionID );
    SW_EXPECT_NOT_EQUAL( 0, Memory::compare( token._arrSecret, rig.getAccount( second ).getToken()._arrSecret, sizeof( token._arrSecret ) ) );
    SW_EXPECT_EQUAL( token._sessionID, rig._accountServer.findSessionID( accountID ) );
    SW_EXPECT_TRUE( rig.getHost().findConnection( accountID, connection ) );
}

/** @brief [AccountServerTest] 위조 토큰: 비밀이 다르거나 없는 세션이면 InvalidToken 이고 연결을 묶지 않으며, 진짜 세션을 건드리지 않는다 */
SW_TEST_CASE( AccountServerTest, ForgedResumeTokenIsRefused )
{
    AccountServerFixture rig;
    const int32          owner     = rig.addClient();
    const AccountID      accountID = rig.signIn( owner, "alice_01" );
    const uint64         sessionID = rig._accountServer.findSessionID( accountID );

    LoginSessionToken wrongSecret   = rig.getAccount( owner ).getToken();
    wrongSecret._arrSecret[0]       = static_cast<uint8>( wrongSecret._arrSecret[0] + 1 );
    const int32              forger = rig.addClient( &wrongSecret );
    const AccountClientReply forged = rig.take( forger );
    SW_EXPECT_TRUE( forged._result == LoginResult::InvalidToken );
    SW_EXPECT_TRUE( rig.getAccount( forger ).getToken().isEmpty() );
    SW_EXPECT_FALSE( rig.getAccount( forger ).isLoggedIn() );
    SW_EXPECT_EQUAL( sessionID, rig._accountServer.findSessionID( accountID ) );
    SW_EXPECT_TRUE( rig.getAccount( owner ).isLoggedIn() );

    LoginSessionToken noSession;
    noSession._sessionID = 0x7777777777ull;
    const int32 stranger = rig.addClient( &noSession );
    SW_EXPECT_TRUE( rig.take( stranger )._result == LoginResult::InvalidToken );
    SW_EXPECT_TRUE( rig.getAccount( stranger ).getToken().isEmpty() );
}

/** @brief [AccountServerTest] 운영 끊기: 사유 코드가 알림에 실리고, 세션이 지워져 그 토큰으로 돌아올 수 없다 */
SW_TEST_CASE( AccountServerTest, AdministrativeRevokeCarriesTheReasonCode )
{
    AccountServerFixture    rig;
    const int32             client    = rig.addClient();
    const AccountID         accountID = rig.signIn( client, "alice_01" );
    const LoginSessionToken token     = rig.getAccount( client ).getToken();

    rig._accountServer.getSessionControl().revokeAccountSessions( accountID, "gm.kick", UnixMilliseconds{ kNowMs } );
    const vector<AccountClientEvent> listEvent = rig.takeEvents( client );
    SW_REQUIRE_EQUAL( size_t( 1 ), listEvent.size() );
    SW_EXPECT_EQUAL( string( "gm.kick" ), listEvent[0]._reasonCode );
    SW_EXPECT_TRUE( listEvent[0]._reason == LoginRevokeReason::Administrative );
    SW_EXPECT_EQUAL( uint64( 0 ), rig._accountServer.findSessionID( accountID ) );
    SW_EXPECT_TRUE( rig.getAccount( client ).getToken().isEmpty() );

    const int32 returning = rig.addClient( &token );
    SW_EXPECT_TRUE( rig.take( returning )._result != LoginResult::Ok );
}

/** @brief [AccountServerTest] 다른 서버의 끊기 알림(버스): 그 세션이 이 서버에 있을 때만 끊고 사유 코드를 싣는다. 옛 세션 알림 · 자기가 낸 알림은 무시한다 */
SW_TEST_CASE( AccountServerTest, RevokeNoticeFromAnotherServer )
{
    AccountServerFixture rig;
    const int32          client    = rig.addClient();
    const AccountID      accountID = rig.signIn( client, "alice_01" );
    const uint64         sessionID = rig._accountServer.findSessionID( accountID );

    test::OnlineTestFixture& world = *rig._pRig;
    test::OnlineTestServer   other( world._network, &world._database, &world._cacheDatabase, &world._hub, 2 );
    other.start();
    const auto pumpBoth = [&]()
    { test::updateAll( { &world._server, &other }, world._clients, kNowMs, 12 ); };
    pumpBoth();

    AccountRevoked notice;
    notice._reasonCode = "gm.kick";
    notice._accountID  = accountID;
    notice._reason     = LoginRevokeReason::Administrative;

    notice._sessionID = sessionID + 1; // 이미 바뀐 세션의 알림
    SW_REQUIRE_TRUE( AccountBus::kRevokeTopic.publish( *other._host.getServerBus(), notice ) );
    pumpBoth();
    SW_EXPECT_EQUAL( sessionID, rig._accountServer.findSessionID( accountID ) );

    notice._sessionID = sessionID; // 이 서버가 낸 알림은 이미 처리했다
    SW_REQUIRE_TRUE( AccountBus::kRevokeTopic.publish( *world._server._host.getServerBus(), notice ) );
    pumpBoth();
    SW_EXPECT_EQUAL( sessionID, rig._accountServer.findSessionID( accountID ) );

    SW_REQUIRE_TRUE( AccountBus::kRevokeTopic.publish( *other._host.getServerBus(), notice ) );
    pumpBoth();
    SW_EXPECT_EQUAL( uint64( 0 ), rig._accountServer.findSessionID( accountID ) );
    vector<AccountClientEvent> listEvent;
    SW_REQUIRE_EQUAL( 1, rig.getAccount( client ).pollEvents( listEvent ) );
    SW_EXPECT_EQUAL( string( "gm.kick" ), listEvent[0]._reasonCode );
    SW_EXPECT_TRUE( listEvent[0]._reason == LoginRevokeReason::Administrative );
}

/** @brief [AccountServerTest] 시도 제한은 공통 오류 kRateLimited 와 기다릴 시간으로 간다 */
SW_TEST_CASE( AccountServerTest, RateLimitReachesTheClient )
{
    LoginServiceConfig settings;
    settings._attemptBurst = 1;
    AccountServerFixture rig( settings );
    const int32          client = rig.addClient();
    (void)rig.registerAccount( client, "alice_01" );

    (void)rig.getAccount( client ).login( "alice_01", kPassword );
    const AccountClientReply reply = rig.take( client );
    SW_EXPECT_EQUAL( OnlineError::kRateLimited, reply._errorCode );
    SW_EXPECT_TRUE( reply._result.hasError( OnlineError::kRateLimited ) );
    SW_EXPECT_TRUE( reply._grant._retryAfterMs > 0 );
    SW_EXPECT_FALSE( rig.getAccount( client ).isLoggedIn() );
}

/** @brief [AccountServerTest] 저장소 오류는 kUnavailable 이고 연결을 묶지 않는다. 고쳐지면 같은 연결로 다시 로그인한다 */
SW_TEST_CASE( AccountServerTest, StoreFailureIsUnavailable )
{
    AccountServerFixture rig;
    const int32          client    = rig.addClient();
    const AccountID      accountID = rig.registerAccount( client, "alice_01" );

    rig._pRig->_database.armFault( ServiceStoreFault::RejectRead );
    (void)rig.getAccount( client ).login( "alice_01", kPassword );
    const AccountClientReply reply = rig.take( client );
    SW_EXPECT_EQUAL( OnlineError::kUnavailable, reply._errorCode );
    SW_EXPECT_TRUE( reply._result.hasError( OnlineError::kUnavailable ) );
    SW_EXPECT_EQUAL( uint64( 0 ), rig._accountServer.findSessionID( accountID ) );

    (void)rig.getAccount( client ).login( "alice_01", kPassword );
    SW_EXPECT_TRUE( rig.take( client )._result == LoginResult::Ok );
}

/** @brief [AccountServerTest] 빌드 버전: 최소 판보다 낮으면 kUpdateRequired 와 상점 주소가 상세 몸으로 간다 */
SW_TEST_CASE( AccountServerTest, UpdateRequiredCarriesTheStoreURL )
{
    AccountServerFixture rig;
    RemoteConfig         config;
    const auto           setText = [&]( string_view key, string_view text, int64 version )
    {
        RemoteConfigValue value;
        value._type = RemoteConfigValueType::Text;
        value._text = string( text );
        ServiceAuditEntry audit;
        audit._actor   = "gm.0000000000000001";
        audit._action  = "config.set";
        audit._subject = "config";
        audit._timeMs  = version;
        config.submitSet( rig._pRig->_server._store, key, value, audit );
        rig.pump();
    };
    setText( "account.minimum_build.windows", "2.0.0", 1 );
    setText( "account.store_url.windows", "https://store.example/x", 2 );
    rig._loginService.setRemoteConfig( &config );

    AccountClientInfo clientInfo;
    clientInfo._platform = "windows";
    clientInfo._build    = "1.0.0";
    const int32 client   = rig.addClient( nullptr, clientInfo );
    (void)rig.getAccount( client ).login( "alice_01", kPassword );
    const AccountClientReply reply = rig.take( client );
    SW_EXPECT_EQUAL( OnlineError::kUpdateRequired, reply._errorCode );
    SW_EXPECT_TRUE( reply._result.hasError( OnlineError::kUpdateRequired ) );
    SW_EXPECT_EQUAL( string( "https://store.example/x" ), reply._grant._storeURL );
    rig._loginService.setRemoteConfig( nullptr );
}

/** @brief [AccountServerTest] 게스트 → 이름 연동: 응답의 신원이 클라이언트에 반영되고 토큰은 그대로이며 연동 목록에 나온다 */
SW_TEST_CASE( AccountServerTest, GuestLinksACredential )
{
    AccountServerFixture rig;
    const int32          client = rig.addClient();
    const uint8          arrDeviceSecret[LoginConstant::kDeviceSecretSize]{ 7, 7, 7 };
    (void)rig.getAccount( client ).guestLogin( arrDeviceSecret );
    const AccountClientReply guest = rig.take( client );
    SW_REQUIRE_TRUE( guest._result == LoginResult::Ok );
    SW_EXPECT_TRUE( guest._grant._identity._bGuest == SW_TRUE );
    const uint64 sessionID = rig.getAccount( client ).getToken()._sessionID;

    (void)rig.getAccount( client ).linkCredential( "alice_01", kPassword );
    SW_REQUIRE_TRUE( rig.take( client )._result == LoginResult::Ok );
    SW_EXPECT_TRUE( rig.getAccount( client ).getIdentity()._bGuest == SW_FALSE );
    SW_EXPECT_EQUAL( sessionID, rig.getAccount( client ).getToken()._sessionID );

    (void)rig.getAccount( client ).listLinks();
    SW_EXPECT_TRUE( rig.take( client )._linkSummary._bHasCredential == SW_TRUE );
}

/** @brief [AccountServerTest] 게임 접속 티켓은 세션이 있는 연결만 받고, 시한이 지금 뒤다 */
SW_TEST_CASE( AccountServerTest, GameTicketNeedsASession )
{
    AccountServerFixture rig;
    const int32          client = rig.addClient();
    (void)rig.getAccount( client ).issueGameTicket( "game-1" );
    SW_EXPECT_EQUAL( OnlineError::kUnauthenticated, rig.take( client )._errorCode );

    (void)rig.signIn( client, "alice_01" );
    (void)rig.getAccount( client ).issueGameTicket( "game-1" );
    const AccountClientReply reply = rig.take( client );
    SW_EXPECT_TRUE( reply._result == LoginResult::Ok );
    SW_EXPECT_TRUE( reply._ticket._expiresAtMs > kNowMs );
}

/** @brief [AccountServerTest] 다시 시도할 결과는 멱등 응답으로 기억하지 않는다 — 저장소 오류(공통 범주) 뒤 같은 키의 재시도가 서비스에 닿아 로그인된다. 업무 결과의 판정은 `AccountProtocol::isRetryable` 이다 */
SW_TEST_CASE( AccountServerTest, RetryableResultReachesTheServiceAgain )
{
    SW_EXPECT_TRUE( OnlineError::isRetryable( OnlineError::kUnavailable ) );
    SW_EXPECT_TRUE( AccountProtocol::isRetryable( LoginResult::ProviderUnavailable ) );
    SW_EXPECT_TRUE( OnlineError::isRetryable( OnlineError::kRateLimited ) );
    SW_EXPECT_TRUE( AccountProtocol::isRetryable( LoginResult::AccountLocked ) );
    SW_EXPECT_FALSE( AccountProtocol::isRetryable( LoginResult::Ok ) );
    SW_EXPECT_FALSE( AccountProtocol::isRetryable( LoginResult::WrongCredentials ) );
    SW_EXPECT_TRUE( ( AccountGrantReply{ LoginGrant{}, LoginResult::ProviderUnavailable } ).isRetryable() );

    AccountServerFixture rig;
    const int32          client    = rig.addClient();
    const AccountID      accountID = rig.registerAccount( client, "alice_01" );
    NetRequestOptions    options;
    options._idempotencyKey = NetIdempotencyKey::makeRandom();
    rig._pRig->_database.armFault( ServiceStoreFault::RejectRead );
    SW_EXPECT_EQUAL( OnlineError::kUnavailable, rig.sendRaw( client, AccountMethod::Login::kID, AccountServerTestUtil::makeLogin( "alice_01" ), options )._errorCode );
    SW_EXPECT_EQUAL( OnlineError::kOk, rig.sendRaw( client, AccountMethod::Login::kID, AccountServerTestUtil::makeLogin( "alice_01" ), options )._errorCode ); // 기억했다면 같은 오류다
    SW_EXPECT_NOT_EQUAL( uint64( 0 ), rig._accountServer.findSessionID( accountID ) );
}
