// AccountGMLoginTest — 운영 도구 로그인 도우미: Ready 에서 이름 · 비밀번호 로그인을 한 번 보내고 응답으로 상태를 바꾸며, 토큰이 있으면 다시 보내지 않고, 거절이면 이유를 적고
// 계정을 다시 정할 때까지 멈춘다. 빈 이름 · 빈 비밀번호는 받지 않는다. 서버 쪽은 같은 프로토콜 항목의 가짜(`OnlineHostTestUtil` 루프백)다. 온라인 키트라 `UnitTests.OnlineKits` 가 돌린다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Kits/Feature/Online/Account/Runtime/API/AccountGMLogin.h"
#include "GameFramework/Kits/Feature/Online/Account/Runtime/Protocol/AccountProtocol.h"
#include "GameFramework/Runtime/Online/Service/OnlineServiceClient.h"
#include "GameFramework/Runtime/Online/Service/Server/OnlineServiceHost.h"
#include "GameFramework/Runtime/Online/Service/Server/Tests/OnlineHostTestUtil.h"

using namespace sw;

namespace
{
    constexpr int64     kNowMs     = 1700000000000ll;
    constexpr AccountID kAccountID = 9;

    /** @brief 로그인에만 답하는 가짜 계정 서버입니다. `_result` 로 답한다. */
    class FakeGMAccountService final : public IOnlineServerService
    {
    public:
        vector<uint16>     _listMethod{};
        LoginServiceResult _result{ LoginResult::Ok };

        const OnlineProtocolEntry& getProtocol() const override { return AccountProtocol::kOnlineEntry; }
        bool                       isAnonymousMethod( uint16 method ) const override
        {
            (void)method;
            return true;
        }

        void onServiceRequest( OnlineServiceHost& host, const OnlineCallContext& context, BitReader& body ) override
        {
            (void)body;
            _listMethod.push_back( context._method );
            if ( _result.isFailed() )
            {
                (void)host.respondError( context._token, _result._errorCode ); // 공통 범주는 오류 코드로만 간다
                return;
            }
            AccountGrantReply reply;
            reply._result = _result._result;
            if ( _result == LoginResult::Ok )
            {
                reply._grant._identity._accountID   = kAccountID;
                reply._grant._identity._displayName = "gm_kim";
                reply._grant._token._sessionID      = 3;
            }
            (void)host.respondMessage<AccountProtocol::MessageTypes>( context._token, reply );
        }

        int32 countLogins() const
        {
            int32 count = 0;
            for ( const uint16 method : _listMethod )
            {
                count += method == AccountMethod::Login::kID ? 1 : 0;
            }
            return count;
        }
    };

    /** @brief 가짜 서버 + 로그인 도우미를 붙인 연결 하나입니다. 선언 순서가 수명이다. */
    struct GMLoginFixture
    {
        FakeGMAccountService    _service{};
        AccountGMLogin          _login{};
        test::OnlineTestFixture _rig{ false };
        int32                   _clientIndex{ -1 };

        GMLoginFixture()
        {
            SW_EXPECT_TRUE( _rig._server._host.registerServerService( &_service ) );
            _rig._server.start();
            _clientIndex = _rig._clients.connect( _rig._server._port, {} );
            SW_EXPECT_TRUE( _login.attach( getClient(), AccountClientInfo{} ) ); // 연결이 열리기 전에 붙인다
            pump();
            SW_EXPECT_TRUE( getClient().isReady() );
        }

        OnlineServiceClient& getClient() { return _rig._clients.getClient( _clientIndex ); }

        void pump() { test::updateAll( { &_rig._server }, _rig._clients, kNowMs ); }

        /** @brief Ready 알림을 흉내 내고 왕복을 돌린 뒤 응답을 꺼냅니다. */
        void readyAndSettle()
        {
            _login.onReady( getClient() );
            pump();
            _login.update();
        }
    };
} // namespace

/** @brief [AccountGMLoginTest] Ready 에서 로그인을 한 번 보내 LoggedIn 이 되고, 토큰을 받은 뒤의 Ready(다시 연결)는 로그인을 다시 보내지 않는다 */
SW_TEST_CASE( AccountGMLoginTest, LogsInOnceOnReady )
{
    GMLoginFixture rig;
    SW_EXPECT_TRUE( rig._login.attach( rig.getClient(), AccountClientInfo{} ) ); // 두 번 붙여도 한 번이다
    SW_REQUIRE_TRUE( rig._login.setCredentials( "gm_kim", "secret-password" ) );
    SW_EXPECT_TRUE( rig._login.getState() == AccountGMLoginState::Idle );

    rig._login.onReady( rig.getClient() );
    SW_EXPECT_TRUE( rig._login.getState() == AccountGMLoginState::LoggingIn );
    rig.pump();
    rig._login.update();
    SW_EXPECT_TRUE( rig._login.getState() == AccountGMLoginState::LoggedIn );
    SW_EXPECT_TRUE( rig._login.getFailureText().empty() );
    SW_EXPECT_EQUAL( kAccountID, rig._login.getAccountClient().getIdentity()._accountID );
    SW_EXPECT_EQUAL( 1, rig._service.countLogins() );

    rig.readyAndSettle();
    SW_EXPECT_EQUAL( 1, rig._service.countLogins() );
    SW_EXPECT_TRUE( rig._login.getState() == AccountGMLoginState::LoggedIn );
}

/** @brief [AccountGMLoginTest] 거절되면 Failed 와 이유이고, 계정을 다시 정할 때까지 Ready 가 와도 보내지 않는다 */
SW_TEST_CASE( AccountGMLoginTest, RefusedLoginWaitsForNewCredentials )
{
    GMLoginFixture rig;
    rig._service._result = LoginResult::WrongCredentials;
    SW_REQUIRE_TRUE( rig._login.setCredentials( "gm_kim", "wrong-password" ) );
    rig.readyAndSettle();
    SW_EXPECT_TRUE( rig._login.getState() == AccountGMLoginState::Failed );
    SW_EXPECT_FALSE( rig._login.getFailureText().empty() );

    rig.readyAndSettle();
    SW_EXPECT_EQUAL( 1, rig._service.countLogins() );

    rig._service._result = LoginResult::Ok;
    SW_REQUIRE_TRUE( rig._login.setCredentials( "gm_kim", "secret-password" ) );
    SW_EXPECT_TRUE( rig._login.getFailureText().empty() );
    rig.readyAndSettle();
    SW_EXPECT_TRUE( rig._login.getState() == AccountGMLoginState::LoggedIn );
    SW_EXPECT_EQUAL( 2, rig._service.countLogins() );
}

/** @brief [AccountGMLoginTest] 빈 이름 · 빈 비밀번호는 받지 않고, 계정을 정하지 않았거나 붙이지 않았으면 Ready 가 와도 보내지 않는다 */
SW_TEST_CASE( AccountGMLoginTest, MissingCredentialsOrAttachSendNothing )
{
    GMLoginFixture rig;
    SW_EXPECT_LOG( "AccountGMLogin.GMLoginNeedsBoth" );
    SW_EXPECT_FALSE( rig._login.setCredentials( "", "secret-password" ) );
    SW_EXPECT_FALSE( rig._login.setCredentials( "gm_kim", "" ) );
    rig.readyAndSettle();
    SW_EXPECT_TRUE( rig._login.getState() == AccountGMLoginState::Idle );
    SW_EXPECT_EQUAL( 0, rig._service.countLogins() );

    AccountGMLogin detached;
    SW_REQUIRE_TRUE( detached.setCredentials( "gm_kim", "secret-password" ) );
    detached.onReady( rig.getClient() );
    detached.update();
    SW_EXPECT_TRUE( detached.getState() == AccountGMLoginState::Idle );
}
