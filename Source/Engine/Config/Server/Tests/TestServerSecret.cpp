// ServerSecret — 비밀은 환경 변수에서만 온다(nogpu). 서버 코드라 클라이언트 구성은 이 폴더를 짓지 않는다.
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Config/Server/ServerSecret.h"

/**
 * @brief [ServerConfigTest] 비밀은 환경 변수에서만 — 이름이 비면 비밀 없음, 이름이 있는데 변수가 없으면 실패(로그에는 이름만)
 */
SW_TEST_CASE( ServerConfigTest, SecretComesFromTheEnvironmentOnly )
{
    sw::string secret;
    SW_EXPECT_TRUE( sw::ServerSecret::read( "", secret ) );
    SW_EXPECT_TRUE( secret.empty() );

    SW_EXPECT_LOG( "Server.SecretNotSet" );
    SW_EXPECT_FALSE( sw::ServerSecret::read( "SW_TEST_SECRET_THAT_IS_NOT_SET_7F3A", secret ) );
}
