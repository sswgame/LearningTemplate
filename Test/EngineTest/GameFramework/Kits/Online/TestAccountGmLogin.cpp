// GM 로그인 도우미 — 계정 이름 · 비밀번호 검사, 등록, 연결이 Ready 가 되기 전의 상태.
// 서버와 연결을 요구하는 흐름(로그인 응답으로 LoggedIn)은 서버 호스트 위의 `AccountStreamTest` 가 쓰는 계정 클라이언트가 이미 덮는다.
#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Account/Shared/API/AccountGmLogin.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

/**
 * @brief [AccountGmLoginTest] 빈 이름 · 빈 비밀번호는 거절하고, 등록은 한 번만 하며, 계정을 정하기 전이나 연결 전에는 로그인을 보내지 않는다
 */
SW_TEST_CASE( AccountGmLoginTest, RefusesEmptyCredentialsAndWaitsForTheConnection )
{
    OnlineServiceClient client;
    AccountGmLogin      login;
    SW_EXPECT_TRUE( login.getState() == AccountGmLoginState::Idle );

    SW_EXPECT_FALSE( login.setCredentials( "", "password1" ) );
    SW_EXPECT_FALSE( login.setCredentials( "gm_alpha", "" ) );

    AccountClientInfo info;
    info._build    = "1.0.0";
    info._platform = "windows";
    SW_ASSERT_TRUE( login.attach( client, info ) );
    SW_EXPECT_TRUE( login.attach( client, info ) ); // 두 번째 attach 는 같은 등록을 그대로 둔다

    // 계정을 정하지 않았으면 Ready 가 돼도 로그인하지 않는다.
    login.onReady( client );
    SW_EXPECT_TRUE( login.getState() == AccountGmLoginState::Idle );
    login.update();
    SW_EXPECT_TRUE( login.getState() == AccountGmLoginState::Idle );
    SW_EXPECT_TRUE( login.getFailureText().empty() );

    SW_ASSERT_TRUE( login.setCredentials( "gm_alpha", "password1" ) );
    SW_EXPECT_TRUE( login.getState() == AccountGmLoginState::Idle );
}
