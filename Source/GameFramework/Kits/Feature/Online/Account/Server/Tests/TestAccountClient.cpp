// AccountClientTest — 계정 클라이언트(서버에서 온 바이트 — 신뢰 경계): 응답의 잘림 · 다른 타입 · 깨진 몸, 공통 오류의 대응과 상세 몸(시도 제한 · 상점 주소), 토큰 · 신원의 바뀜,
// 재접속 동안 모은 요청과 그 결과, 끊김, 밀려남 알림, 보내기 전에 거절하는 범위 밖 값. 서버 쪽은 같은 프로토콜 항목의 가짜(`OnlineHostTestUtil` 루프백)라 어떤 몸이든 답할 수 있다.
// 진짜 서버와 잇는 길은 AccountServerTest 가 본다. 온라인 키트라 `UnitTests.OnlineKits` 가 돌린다(nogpu).
#include "pch.h"

#include "Core/Container/unordered_map.h"
#include "Core/Network/Connection/NetEndpointSecurity.h"
#include "Core/Testing/TestFramework.h"

#include "GameFramework/Kits/Feature/Online/Account/Runtime/API/AccountClient.h"
#include "GameFramework/Kits/Feature/Online/Account/Runtime/Protocol/AccountProtocol.h"
#include "GameFramework/Runtime/Online/Service/OnlineServiceClient.h"
#include "GameFramework/Runtime/Online/Service/Server/OnlineServiceHost.h"
#include "GameFramework/Runtime/Online/Service/Server/Tests/OnlineHostTestUtil.h"

using namespace sw;

namespace
{
    constexpr int64     kNowMs     = 1700000000000ll;
    constexpr AccountID kAccountID = 42;
    constexpr uint64    kSessionID = 7;

    /** @brief 몸을 만듭니다. */
    struct AccountClientTestUtil
    {
        static vector<uint8> toBytes( const BitWriter& writer ) { return vector<uint8>( writer.getBytes().begin(), writer.getBytes().begin() + writer.getByteCount() ); }

        static LoginSessionToken makeToken( uint8 secretByte = 5 )
        {
            LoginSessionToken token;
            token._sessionID = kSessionID;
            for ( uint8& byte : token._arrSecret )
            {
                byte = secretByte;
            }
            return token;
        }

        static vector<uint8> makeGrantBody( LoginResult result, uint8 secretByte = 5, const utf8* pDisplayName = "alice" )
        {
            AccountGrantReply reply;
            reply._grant._identity._accountID   = kAccountID;
            reply._grant._identity._displayName = pDisplayName;
            reply._grant._token                 = makeToken( secretByte );
            reply._result                       = result;
            BitWriter writer;
            SW_EXPECT_TRUE( AccountProtocol::MessageTypes::write( writer, reply ) );
            return toBytes( writer );
        }

        static vector<uint8> makeLinkBody()
        {
            AccountLinkReply reply;
            reply._summary._bHasCredential = SW_TRUE;
            BitWriter writer;
            SW_EXPECT_TRUE( AccountProtocol::MessageTypes::write( writer, reply ) );
            return toBytes( writer );
        }

        static vector<uint8> makeTicketBody()
        {
            AccountTicketReply reply;
            reply._ticket._expiresAtMs = kNowMs + 1000;
            BitWriter writer;
            SW_EXPECT_TRUE( AccountProtocol::MessageTypes::write( writer, reply ) );
            return toBytes( writer );
        }

        static vector<uint8> makeVarUintBody( uint64 value )
        {
            BitWriter writer;
            writer.writeVarUint( value );
            return toBytes( writer );
        }

        static vector<uint8> makeStoreURLBody( const utf8* pStoreURL )
        {
            AccountUpdateDetail detail;
            detail._storeURL = pStoreURL;
            BitWriter writer;
            SW_EXPECT_TRUE( AccountProtocol::MessageTypes::write( writer, detail ) );
            return toBytes( writer );
        }

        static BitWriter toWriter( const vector<uint8>& listByte )
        {
            BitWriter writer;
            if ( listByte.empty() == false )
                writer.writeBytes( listByte.data(), static_cast<int32>( listByte.size() ) );
            return writer;
        }
    };

    /** @brief 서버 쪽 가짜 — 메서드마다 정해 둔 몸이나 오류로 답합니다. `_bHold` 를 켜면 토큰만 들고 있다가 `answerHeld` 로 답합니다. */
    class FakeAccountService final : public IOnlineServerService
    {
    public:
        vector<uint16> _listMethod{};
        vector<uint8>  _detailBytes{};
        uint16         _errorCode{ OnlineError::kOk };
        uint8          _bHold : 1;

        FakeAccountService()
            : _bHold{ SW_FALSE }
        {
        }

        const OnlineProtocolEntry& getProtocol() const override { return AccountProtocol::kOnlineEntry; }
        bool                       isAnonymousMethod( uint16 method ) const override
        {
            (void)method;
            return true;
        }

        void setBody( uint16 method, vector<uint8> bytes ) { _mapMethodToBody[method] = std::move( bytes ); }

        void onServiceRequest( OnlineServiceHost& host, const OnlineCallContext& context, BitReader& body ) override
        {
            (void)body;
            _listMethod.push_back( context._method );
            _listHeld.push_back( HeldCall{ context._token, context._method } );
            if ( _bHold == SW_FALSE )
                answerHeld( host );
        }

        void answerHeld( OnlineServiceHost& host )
        {
            for ( const HeldCall& held : _listHeld )
            {
                if ( _errorCode != OnlineError::kOk )
                {
                    const BitWriter detail = AccountClientTestUtil::toWriter( _detailBytes );
                    (void)host.respondError( held._token, _errorCode, _detailBytes.empty() ? nullptr : &detail );
                    continue;
                }
                const auto bodyIt = _mapMethodToBody.find( held._method );
                (void)host.respondOk( held._token, AccountClientTestUtil::toWriter( bodyIt != _mapMethodToBody.end() ? bodyIt->second : vector<uint8>{} ), NetResponseRetention::Keep );
            }
            _listHeld.clear();
        }

    private:
        struct HeldCall
        {
            NetRequestToken _token;
            uint16          _method;
        };

        unordered_map<uint16, vector<uint8>> _mapMethodToBody{};
        vector<HeldCall>                     _listHeld{};
    };

    /** @brief 가짜 서버 + 연결 하나의 계정 클라이언트입니다. 선언 순서가 수명이다. @p pToken 이 있으면 Hello 뒤의 자동 재접속을 들고 멈춘 채로 선다. */
    struct AccountClientFixture
    {
        FakeAccountService      _service{};
        AccountClient           _account{};
        test::OnlineTestFixture _rig{ false };
        int32                   _clientIndex{ -1 };

        explicit AccountClientFixture( const LoginSessionToken* pToken = nullptr )
        {
            SW_EXPECT_TRUE( _rig._server._host.registerServerService( &_service ) );
            _rig._server.start();
            _clientIndex = _rig._clients.connect( _rig._server._port, { &_account } );
            _account.initialize( &getClient(), AccountClientInfo{} );
            if ( pToken != nullptr )
            {
                _account.setToken( *pToken );
                _service._bHold = SW_TRUE;
            }
            pump();
            SW_EXPECT_TRUE( getClient().isReady() );
        }

        OnlineServiceClient& getClient() { return _rig._clients.getClient( _clientIndex ); }

        void pump() { test::updateAll( { &_rig._server }, _rig._clients, kNowMs ); }

        vector<AccountClientReply> takeAll()
        {
            pump();
            vector<AccountClientReply> listReply;
            (void)_account.pollReplies( listReply );
            return listReply;
        }

        AccountClientReply take()
        {
            vector<AccountClientReply> listReply = takeAll();
            SW_EXPECT_EQUAL( size_t( 1 ), listReply.size() );
            return listReply.empty() ? AccountClientReply{} : listReply.front();
        }

        /** @brief 서버가 Ok 와 토큰으로 답하는 로그인을 마칩니다. */
        void logIn()
        {
            _service.setBody( AccountMethod::Login::kID, AccountClientTestUtil::makeGrantBody( LoginResult::Ok ) );
            (void)_account.login( "alice", "password123" );
            SW_EXPECT_TRUE( take()._result == LoginResult::Ok );
            SW_EXPECT_TRUE( _account.isLoggedIn() );
        }
    };
} // namespace

/** @brief [AccountClientTest] 로그인 성공은 토큰 · 신원을 보관하고 로그인 상태가 된다. 요청 id 가 답과 같다 */
SW_TEST_CASE( AccountClientTest, LoginStoresTokenAndIdentity )
{
    AccountClientFixture rig;
    SW_EXPECT_FALSE( rig._account.isLoggedIn() );
    rig._service.setBody( AccountMethod::Login::kID, AccountClientTestUtil::makeGrantBody( LoginResult::Ok ) );
    const uint64             requestID = rig._account.login( "alice", "password123" );
    const AccountClientReply reply     = rig.take();
    SW_EXPECT_EQUAL( requestID, reply._requestID );
    SW_EXPECT_TRUE( reply._operation == AccountClientOperation::Login );
    SW_EXPECT_TRUE( reply._result == LoginResult::Ok );
    SW_EXPECT_EQUAL( OnlineError::kOk, reply._errorCode );
    SW_EXPECT_TRUE( rig._account.isLoggedIn() );
    SW_EXPECT_EQUAL( kSessionID, rig._account.getToken()._sessionID );
    SW_EXPECT_EQUAL( kAccountID, rig._account.getIdentity()._accountID );
}

/** @brief [AccountClientTest] 업무 결과가 실패면 토큰 · 로그인 상태를 바꾸지 않는다 */
SW_TEST_CASE( AccountClientTest, FailedLoginChangesNothing )
{
    AccountClientFixture rig;
    rig._service.setBody( AccountMethod::Login::kID, AccountClientTestUtil::makeGrantBody( LoginResult::WrongCredentials ) );
    (void)rig._account.login( "alice", "wrong" );
    SW_EXPECT_TRUE( rig.take()._result == LoginResult::WrongCredentials );
    SW_EXPECT_FALSE( rig._account.isLoggedIn() );
    SW_EXPECT_TRUE( rig._account.getToken().isEmpty() );
}

/** @brief [AccountClientTest] 성공 응답의 몸이 잘렸거나 깨졌으면 InvalidRequest 이고, 몸에 든 토큰을 믿지 않는다 */
SW_TEST_CASE( AccountClientTest, TruncatedOrJunkOkBodyIsRefused )
{
    AccountClientFixture rig;
    const vector<uint8>  whole = AccountClientTestUtil::makeGrantBody( LoginResult::Ok );
    SW_REQUIRE_TRUE( whole.size() > 8 );
    vector<vector<uint8>> listBody;
    listBody.emplace_back();
    listBody.push_back( vector<uint8>( whole.begin(), whole.begin() + 1 ) );
    listBody.push_back( vector<uint8>( whole.begin(), whole.begin() + static_cast<ptrdiff_t>( whole.size() / 2 ) ) );
    listBody.push_back( vector<uint8>( whole.begin(), whole.end() - 1 ) );
    listBody.push_back( vector<uint8>( 32, uint8( 0xFF ) ) );
    for ( const vector<uint8>& body : listBody )
    {
        rig._service.setBody( AccountMethod::Login::kID, body );
        (void)rig._account.login( "alice", "password123" );
        const AccountClientReply reply = rig.take();
        SW_EXPECT_TRUE( reply._result.hasError( OnlineError::kUnavailable ) ); // 같은 스키마 해시인데 못 읽은 몸은 기반이 일시 장애로 알린다(`OnlineProtocolClient::onResponse`)
        SW_EXPECT_EQUAL( OnlineError::kUnavailable, reply._errorCode );
        SW_EXPECT_FALSE( rig._account.isLoggedIn() );
        SW_EXPECT_TRUE( rig._account.getToken().isEmpty() );
    }
}

/** @brief [AccountClientTest] 그 요청의 타입이 아닌 몸(메서드와 타입의 짝이 어긋남)은 스키마 해시 머리로 걸러 공통 범주 kUnavailable 이다. 티켓 · 연동 목록도 같다 */
SW_TEST_CASE( AccountClientTest, BodyOfAnotherTypeIsRefused )
{
    AccountClientFixture rig;
    rig._service.setBody( AccountMethod::Login::kID, AccountClientTestUtil::makeTicketBody() );
    (void)rig._account.login( "alice", "password123" );
    SW_EXPECT_TRUE( rig.take()._result.hasError( OnlineError::kUnavailable ) );
    SW_EXPECT_FALSE( rig._account.isLoggedIn() );

    rig._service.setBody( AccountMethod::IssueGameTicket::kID, AccountClientTestUtil::makeGrantBody( LoginResult::Ok ) );
    (void)rig._account.issueGameTicket( "game" );
    const AccountClientReply ticket = rig.take();
    SW_EXPECT_TRUE( ticket._result.hasError( OnlineError::kUnavailable ) );
    SW_EXPECT_EQUAL( int64( 0 ), ticket._ticket._expiresAtMs );

    rig._service.setBody( AccountMethod::ListLinks::kID, AccountClientTestUtil::makeGrantBody( LoginResult::Ok ) );
    (void)rig._account.listLinks();
    SW_EXPECT_TRUE( rig.take()._result.hasError( OnlineError::kUnavailable ) );
}

/** @brief [AccountClientTest] 티켓 · 연동 목록 응답은 자기 칸에 담는다 */
SW_TEST_CASE( AccountClientTest, TicketAndLinkRepliesFillTheirProperties )
{
    AccountClientFixture rig;
    rig._service.setBody( AccountMethod::IssueGameTicket::kID, AccountClientTestUtil::makeTicketBody() );
    (void)rig._account.issueGameTicket( "game" );
    SW_EXPECT_EQUAL( kNowMs + 1000, rig.take()._ticket._expiresAtMs );

    rig._service.setBody( AccountMethod::ListLinks::kID, AccountClientTestUtil::makeLinkBody() );
    (void)rig._account.listLinks();
    SW_EXPECT_TRUE( rig.take()._linkSummary._bHasCredential == SW_TRUE );
}

/** @brief [AccountClientTest] 공통 오류는 결과에도 같은 범주로 남는다(업무 결과로 바꾸지 않는다). `_errorCode` 는 받은 그대로다 */
SW_TEST_CASE( AccountClientTest, ErrorCodesStayCommonCategories )
{
    AccountClientFixture rig;
    const uint16         arrErrorCode[] = { OnlineError::kRateLimited, OnlineError::kUpdateRequired, OnlineError::kVersionMismatch, OnlineError::kUnauthenticated, OnlineError::kInvalidRequest,
                                            OnlineError::kUnavailable, OnlineError::kForbidden, OnlineError::kNotFound, OnlineError::kInternal, uint16( 999 ) };
    for ( const uint16 errorCode : arrErrorCode )
    {
        rig._service._errorCode = errorCode;
        (void)rig._account.login( "alice", "password123" );
        const AccountClientReply reply = rig.take();
        SW_EXPECT_TRUE( reply._result.hasError( errorCode ) );
        SW_EXPECT_FALSE( reply._result == LoginResult::Ok );
        SW_EXPECT_EQUAL( errorCode, reply._errorCode );
        SW_EXPECT_FALSE( rig._account.isLoggedIn() );
    }
}

/** @brief [AccountClientTest] 시도 제한의 상세 몸: 기다릴 시간을 읽는다. 없거나 잘렸거나 int64 를 넘는 값이면 0 이다(음수로 감기지 않는다) */
SW_TEST_CASE( AccountClientTest, RateLimitDetailIsBounded )
{
    AccountClientFixture rig;
    rig._service._errorCode = OnlineError::kRateLimited;

    rig._service._detailBytes = AccountClientTestUtil::makeVarUintBody( 1500 );
    (void)rig._account.login( "alice", "password123" );
    SW_EXPECT_EQUAL( int64( 1500 ), rig.take()._grant._retryAfterMs );

    rig._service._detailBytes.clear();
    (void)rig._account.login( "alice", "password123" );
    SW_EXPECT_EQUAL( int64( 0 ), rig.take()._grant._retryAfterMs );

    rig._service._detailBytes = AccountClientTestUtil::makeVarUintBody( 0xFFFFFFFFFFFFFFFFull );
    (void)rig._account.login( "alice", "password123" );
    const AccountClientReply overflowed = rig.take();
    SW_EXPECT_TRUE( overflowed._result.hasError( OnlineError::kRateLimited ) );
    SW_EXPECT_EQUAL( int64( 0 ), overflowed._grant._retryAfterMs );

    rig._service._detailBytes = { 0xFF };
    (void)rig._account.login( "alice", "password123" );
    SW_EXPECT_EQUAL( int64( 0 ), rig.take()._grant._retryAfterMs );
}

/** @brief [AccountClientTest] 업데이트 필요의 상세 몸: 상점 주소를 읽고, 없거나 깨졌으면 결과는 그대로 주소만 빈다 */
SW_TEST_CASE( AccountClientTest, UpdateDetailIsOptional )
{
    AccountClientFixture rig;
    rig._service._errorCode = OnlineError::kUpdateRequired;

    rig._service._detailBytes = AccountClientTestUtil::makeStoreURLBody( "https://store.example/x" );
    (void)rig._account.login( "alice", "password123" );
    SW_EXPECT_EQUAL( string( "https://store.example/x" ), rig.take()._grant._storeURL );

    for ( const vector<uint8>& detail : { vector<uint8>{}, vector<uint8>( 16, uint8( 0xFF ) ) } )
    {
        rig._service._detailBytes = detail;
        (void)rig._account.login( "alice", "password123" );
        const AccountClientReply reply = rig.take();
        SW_EXPECT_TRUE( reply._result.hasError( OnlineError::kUpdateRequired ) );
        SW_EXPECT_TRUE( reply._grant._storeURL.empty() );
    }
}

/** @brief [AccountClientTest] 토큰 없는 재접속은 보내지 않고 바로 InvalidToken 이다 */
SW_TEST_CASE( AccountClientTest, ResumeWithoutTokenIsNotSent )
{
    AccountClientFixture rig;
    const uint64         requestID = rig._account.resume();
    SW_EXPECT_NOT_EQUAL( uint64( 0 ), requestID );
    const AccountClientReply reply = rig.take();
    SW_EXPECT_EQUAL( requestID, reply._requestID );
    SW_EXPECT_TRUE( reply._result == LoginResult::InvalidToken );
    SW_EXPECT_TRUE( rig._service._listMethod.empty() );
}

/** @brief [AccountClientTest] 재접속 결과: 토큰이 죽었다는 답(InvalidToken · Expired · Revoked)만 토큰을 버린다. 저장소 오류 · 깨진 몸은 토큰을 지켜 다시 시도하게 한다 */
SW_TEST_CASE( AccountClientTest, ResumeDropsTokenOnlyWhenDead )
{
    AccountClientFixture rig;
    rig.logIn();
    rig._service._errorCode = OnlineError::kUnavailable; // 저장소 장애는 공통 범주로 온다
    (void)rig._account.resume();
    SW_EXPECT_TRUE( rig.take()._result.hasError( OnlineError::kUnavailable ) );
    SW_EXPECT_FALSE( rig._account.getToken().isEmpty() );
    rig._service._errorCode = OnlineError::kOk;

    rig._service.setBody( AccountMethod::Resume::kID, vector<uint8>{ 1, 2, 3 } );
    (void)rig._account.resume();
    SW_EXPECT_TRUE( rig.take()._result.hasError( OnlineError::kUnavailable ) ); // 못 읽은 몸
    SW_EXPECT_FALSE( rig._account.getToken().isEmpty() );

    for ( LoginResult dead : { LoginResult::InvalidToken, LoginResult::Expired, LoginResult::Revoked } )
    {
        rig._account.setToken( AccountClientTestUtil::makeToken() );
        rig._service.setBody( AccountMethod::Resume::kID, AccountClientTestUtil::makeGrantBody( dead ) );
        (void)rig._account.resume();
        SW_EXPECT_TRUE( rig.take()._result == dead );
        SW_EXPECT_TRUE( rig._account.getToken().isEmpty() );
    }
}

/** @brief [AccountClientTest] 재접속 성공은 서버가 준 새 비밀로 토큰을 갈아 끼운다 */
SW_TEST_CASE( AccountClientTest, ResumeReplacesTheSecret )
{
    AccountClientFixture rig;
    rig.logIn();
    rig._service.setBody( AccountMethod::Resume::kID, AccountClientTestUtil::makeGrantBody( LoginResult::Ok, 9 ) );
    (void)rig._account.resume();
    SW_EXPECT_TRUE( rig.take()._result == LoginResult::Ok );
    SW_EXPECT_EQUAL( uint8( 9 ), rig._account.getToken()._arrSecret[0] );
}

/** @brief [AccountClientTest] Hello 뒤 자동 재접속: 세션 요청을 보류해 모았다가, 성공하면 순서대로 보내고 보류를 푼다. 다른 키트의 요청도 그때 나간다 */
SW_TEST_CASE( AccountClientTest, AutoResumeHoldsSessionRequestsUntilItSucceeds )
{
    const LoginSessionToken token = AccountClientTestUtil::makeToken();
    AccountClientFixture    rig( &token );
    SW_REQUIRE_EQUAL( size_t( 1 ), rig._service._listMethod.size() );
    SW_EXPECT_EQUAL( AccountMethod::Resume::kID, rig._service._listMethod[0] );
    SW_EXPECT_TRUE( rig.getClient().isHoldingSessionRequests() );

    const uint64 listRequestID = rig._account.listLinks();
    rig._rig._clients.login( rig._clientIndex, kAccountID ); // 다른 영역(테스트 로그인) — 문 앞에서 기다린다
    rig.pump();
    SW_EXPECT_EQUAL( size_t( 1 ), rig._service._listMethod.size() );

    rig._service.setBody( AccountMethod::Resume::kID, AccountClientTestUtil::makeGrantBody( LoginResult::Ok, 9 ) );
    rig._service.setBody( AccountMethod::ListLinks::kID, AccountClientTestUtil::makeLinkBody() );
    rig._service._bHold = SW_FALSE;
    rig._service.answerHeld( rig._rig._server._host );

    const vector<AccountClientReply> listReply = rig.takeAll();
    SW_REQUIRE_EQUAL( size_t( 2 ), listReply.size() );
    SW_EXPECT_TRUE( listReply[0]._operation == AccountClientOperation::Resume );
    SW_EXPECT_TRUE( listReply[0]._bAutomatic == SW_TRUE );
    SW_EXPECT_EQUAL( listRequestID, listReply[1]._requestID );
    SW_EXPECT_TRUE( listReply[1]._result == LoginResult::Ok );
    SW_EXPECT_TRUE( rig._account.isLoggedIn() );
    SW_EXPECT_FALSE( rig.getClient().isHoldingSessionRequests() );
    StreamConnectionHandle connection;
    SW_EXPECT_TRUE( rig._rig._server._host.findConnection( kAccountID, connection ) ); // 문 뒤의 테스트 로그인이 닿았다
}

/** @brief [AccountClientTest] 자동 재접속이 저장소 오류로 지면 모은 요청도 같은 범주(kUnavailable)로 끝난다 — 산 세션을 "로그인 안 됨" 으로 알리지 않는다. 보류는 풀린다 */
SW_TEST_CASE( AccountClientTest, DeferredRequestsEndWithTheResumeResult )
{
    const LoginSessionToken token = AccountClientTestUtil::makeToken();
    AccountClientFixture    rig( &token );
    const uint64            listRequestID = rig._account.listLinks();
    const uint64            ticketID      = rig._account.issueGameTicket( "game" );

    rig._service._errorCode = OnlineError::kUnavailable;
    rig._service._bHold     = SW_FALSE;
    rig._service.answerHeld( rig._rig._server._host );

    const vector<AccountClientReply> listReply = rig.takeAll();
    SW_REQUIRE_EQUAL( size_t( 3 ), listReply.size() );
    SW_EXPECT_TRUE( listReply[0]._result.hasError( OnlineError::kUnavailable ) );
    SW_EXPECT_EQUAL( listRequestID, listReply[1]._requestID );
    SW_EXPECT_TRUE( listReply[1]._result.hasError( OnlineError::kUnavailable ) );
    SW_EXPECT_EQUAL( ticketID, listReply[2]._requestID );
    SW_EXPECT_TRUE( listReply[2]._result.hasError( OnlineError::kUnavailable ) );
    SW_EXPECT_FALSE( rig._account.getToken().isEmpty() );
    SW_EXPECT_FALSE( rig.getClient().isHoldingSessionRequests() );
    SW_EXPECT_EQUAL( size_t( 1 ), rig._service._listMethod.size() ); // 모은 요청은 나가지 않았다
}

/** @brief [AccountClientTest] 자동 재접속이 죽은 토큰으로 지면 토큰을 버리고 모은 요청은 "로그인 안 됨"(kUnauthenticated)으로 끝난다 */
SW_TEST_CASE( AccountClientTest, DeadTokenEndsDeferredRequests )
{
    const LoginSessionToken token = AccountClientTestUtil::makeToken();
    AccountClientFixture    rig( &token );
    (void)rig._account.logout();

    rig._service.setBody( AccountMethod::Resume::kID, AccountClientTestUtil::makeGrantBody( LoginResult::Expired ) );
    rig._service._bHold = SW_FALSE;
    rig._service.answerHeld( rig._rig._server._host );

    const vector<AccountClientReply> listReply = rig.takeAll();
    SW_REQUIRE_EQUAL( size_t( 2 ), listReply.size() );
    SW_EXPECT_TRUE( listReply[0]._result == LoginResult::Expired );
    SW_EXPECT_TRUE( listReply[1]._operation == AccountClientOperation::Logout );
    SW_EXPECT_TRUE( listReply[1]._result.hasError( OnlineError::kUnauthenticated ) );
    SW_EXPECT_TRUE( rig._account.getToken().isEmpty() );
    SW_EXPECT_FALSE( rig._account.isLoggedIn() );
}

/** @brief [AccountClientTest] 재접속 도중 끊기면 재접속과 모은 요청이 모두 kUnavailable 로 끝난다(멈춰 있지 않다). 토큰은 다음 연결을 위해 남는다 */
SW_TEST_CASE( AccountClientTest, DisconnectDuringResumeFinishesEverything )
{
    const LoginSessionToken token = AccountClientTestUtil::makeToken();
    AccountClientFixture    rig( &token );
    (void)rig._account.listLinks();
    rig._rig._clients.drop( rig._clientIndex );

    const vector<AccountClientReply> listReply = rig.takeAll();
    SW_REQUIRE_EQUAL( size_t( 2 ), listReply.size() );
    SW_EXPECT_TRUE( listReply[0]._operation == AccountClientOperation::Resume );
    SW_EXPECT_TRUE( listReply[0]._result.hasError( OnlineError::kUnavailable ) );
    SW_EXPECT_EQUAL( OnlineError::kUnavailable, listReply[0]._errorCode );
    SW_EXPECT_TRUE( listReply[1]._result.hasError( OnlineError::kUnavailable ) );
    SW_EXPECT_FALSE( rig._account.getToken().isEmpty() );
}

/** @brief [AccountClientTest] 로그아웃 · 탈퇴 성공만 토큰을 버린다. 실패한 로그아웃은 세션을 그대로 둔다 */
SW_TEST_CASE( AccountClientTest, LogoutAndDeletionDropTheToken )
{
    AccountClientFixture rig;
    rig.logIn();
    rig._service.setBody( AccountMethod::Logout::kID, AccountClientTestUtil::makeGrantBody( LoginResult::InvalidToken ) );
    (void)rig._account.logout();
    SW_EXPECT_TRUE( rig.take()._result == LoginResult::InvalidToken );
    SW_EXPECT_TRUE( rig._account.isLoggedIn() );
    SW_EXPECT_FALSE( rig._account.getToken().isEmpty() );

    rig._service.setBody( AccountMethod::Logout::kID, AccountClientTestUtil::makeGrantBody( LoginResult::Ok ) );
    (void)rig._account.logout();
    SW_EXPECT_TRUE( rig.take()._result == LoginResult::Ok );
    SW_EXPECT_FALSE( rig._account.isLoggedIn() );
    SW_EXPECT_TRUE( rig._account.getToken().isEmpty() );

    rig.logIn();
    rig._service.setBody( AccountMethod::RequestDeletion::kID, AccountClientTestUtil::makeGrantBody( LoginResult::Ok ) );
    (void)rig._account.requestDeletion();
    SW_EXPECT_TRUE( rig.take()._result == LoginResult::Ok );
    SW_EXPECT_FALSE( rig._account.isLoggedIn() );
    SW_EXPECT_TRUE( rig._account.getToken().isEmpty() );
}

/** @brief [AccountClientTest] 연동 성공은 신원만 갈아 끼우고 토큰은 그대로다(연동 응답은 토큰을 싣지 않는다) */
SW_TEST_CASE( AccountClientTest, LinkUpdatesIdentityOnly )
{
    AccountClientFixture rig;
    rig.logIn();
    rig._service.setBody( AccountMethod::LinkCredential::kID, AccountClientTestUtil::makeGrantBody( LoginResult::Ok, 9, "renamed" ) );
    (void)rig._account.linkCredential( "renamed", "password123" );
    SW_EXPECT_TRUE( rig.take()._result == LoginResult::Ok );
    SW_EXPECT_EQUAL( string( "renamed" ), rig._account.getIdentity()._displayName );
    SW_EXPECT_EQUAL( uint8( 5 ), rig._account.getToken()._arrSecret[0] );
}

/** @brief [AccountClientTest] 밀려남 알림: 토큰을 버리고 이유 · 사유 코드를 이벤트로 낸다. 깨진 몸 · 모르는 종류는 무시하고 세션을 지킨다 */
SW_TEST_CASE( AccountClientTest, RevokedPush )
{
    AccountClientFixture rig;
    rig.logIn();
    rig._rig._clients.login( rig._clientIndex, kAccountID );
    rig.pump();
    OnlineServiceHost& host = rig._rig._server._host;

    const BitWriter junk = AccountClientTestUtil::toWriter( vector<uint8>( 8, uint8( 0xFF ) ) );
    SW_EXPECT_TRUE( host.sendPush( kAccountID, AccountMethod::Revoked::kKind, junk ) );
    SW_EXPECT_TRUE( host.sendPush( kAccountID, AccountMethod::Revoked::kKind, BitWriter{} ) );
    AccountRevokedPush push;
    push._reasonCode = "ban.spam";
    push._reason     = LoginRevokeReason::Sanctioned;
    BitWriter body;
    SW_REQUIRE_TRUE( AccountProtocol::MessageTypes::write( body, push ) );
    SW_EXPECT_TRUE( host.sendPush( kAccountID, static_cast<uint16>( AccountMethod::Revoked::kKind + 1 ), body ) );
    rig.pump();
    vector<AccountClientEvent> listEvent;
    SW_EXPECT_EQUAL( 0, rig._account.pollEvents( listEvent ) );
    SW_EXPECT_TRUE( rig._account.isLoggedIn() );
    SW_EXPECT_FALSE( rig._account.getToken().isEmpty() );

    SW_EXPECT_TRUE( host.sendPush( kAccountID, AccountMethod::Revoked::kKind, body ) );
    rig.pump();
    SW_REQUIRE_EQUAL( 1, rig._account.pollEvents( listEvent ) );
    SW_EXPECT_EQUAL( string( "ban.spam" ), listEvent[0]._reasonCode );
    SW_EXPECT_TRUE( listEvent[0]._reason == LoginRevokeReason::Sanctioned );
    SW_EXPECT_FALSE( rig._account.isLoggedIn() );
    SW_EXPECT_TRUE( rig._account.getToken().isEmpty() );
    SW_EXPECT_EQUAL( 0, rig._account.pollEvents( listEvent ) ); // 꺼낸 이벤트는 다시 나오지 않는다
}

/** @brief [AccountClientTest] 범위 밖 값(상한을 넘는 이름 · 서버 id)은 보내지 않고 kInvalidRequest 로 끝난다. 재접속 중의 세션 요청도 몸을 먼저 쓰므로 같다 */
SW_TEST_CASE( AccountClientTest, OutOfRangeRequestIsNotSent )
{
    SW_EXPECT_LOG( "OnlineProtocol.MessageOutOfRange" );
    const string tooLong( static_cast<size_t>( LoginConstant::kMaxLoginNameWireSize ) + 1, 'a' );
    {
        AccountClientFixture     rig;
        const uint64             requestID = rig._account.login( tooLong, "password123" );
        const AccountClientReply reply     = rig.take();
        SW_EXPECT_EQUAL( requestID, reply._requestID );
        SW_EXPECT_TRUE( reply._result.hasError( OnlineError::kInvalidRequest ) );
        SW_EXPECT_EQUAL( OnlineError::kInvalidRequest, reply._errorCode );
        SW_EXPECT_TRUE( rig._service._listMethod.empty() );

        (void)rig._account.issueGameTicket( string( static_cast<size_t>( LoginConstant::kMaxServerIDSize ) + 1, 's' ) );
        SW_EXPECT_TRUE( rig.take()._result.hasError( OnlineError::kInvalidRequest ) );
        SW_EXPECT_TRUE( rig._service._listMethod.empty() );
    }
    const LoginSessionToken token = AccountClientTestUtil::makeToken();
    AccountClientFixture    resuming( &token );
    (void)resuming._account.issueGameTicket( string( static_cast<size_t>( LoginConstant::kMaxServerIDSize ) + 1, 's' ) );
    SW_EXPECT_TRUE( resuming.take()._result.hasError( OnlineError::kInvalidRequest ) ); // 재접속을 기다리지 않는다
}

/** @brief [AccountClientTest] 접속 자격: 티켓과 비밀을 그대로 옮기고 티켓 길이를 적는다 */
SW_TEST_CASE( AccountClientTest, ConnectCredentialsCopyTheTicket )
{
    NetGameTicket ticket;
    ticket._arrToken[0]                               = 11;
    ticket._arrToken[NetGameTicket::kTokenSize - 1]   = 12;
    ticket._arrSecret[0]                              = 21;
    ticket._arrSecret[NetGameTicket::kSecretSize - 1] = 22;
    NetConnectCredentials credentials;
    AccountClient::makeConnectCredentials( ticket, credentials );
    SW_EXPECT_EQUAL( NetGameTicket::kTokenSize, credentials._tokenSize );
    SW_EXPECT_EQUAL( uint8( 11 ), credentials._arrToken[0] );
    SW_EXPECT_EQUAL( uint8( 12 ), credentials._arrToken[NetGameTicket::kTokenSize - 1] );
    SW_EXPECT_EQUAL( uint8( 21 ), credentials._secret._arrByte[0] );
    SW_EXPECT_EQUAL( uint8( 22 ), credentials._secret._arrByte[NetGameTicket::kSecretSize - 1] );
}
