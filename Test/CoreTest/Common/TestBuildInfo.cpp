#include "pch.h"

#include "Core/Common/BuildInfo.h"

#include "TestFramework/TestFramework.h"

#include <cstring>

/**
 * @brief [BuildInfoTest] CMake 가 정한 구성 · 플랫폼 이름이 같은 빌드의 구성 · 플랫폼 매크로와 맞는다
 * @details 이름(`SW_BUILD_CONFIG_NAME` · `SW_PLATFORM_NAME`)과 분기용 매크로(`SW_SHIPPING` · `SW_DEBUG` · `SW_PLATFORM_*`)는 CMake 의 다른 줄에서
 *          정의되므로 한쪽만 바뀌면 크래시 리포트가 엉뚱한 구성을 적는다.
 */
SW_TEST_CASE( BuildInfoTest, NamesAgreeWithTheBuildMacros )
{
#if defined( SW_SHIPPING )
    SW_EXPECT_STREQ( "Shipping", sw::build::kConfigName );
#elif defined( SW_DEBUG )
    SW_EXPECT_STREQ( "Debug", sw::build::kConfigName );
#else
    SW_EXPECT_TRUE( std::strcmp( sw::build::kConfigName, "Debug" ) != 0 && std::strcmp( sw::build::kConfigName, "Shipping" ) != 0 );
    SW_EXPECT_TRUE( sw::build::kConfigName[0] != '\0' );
#endif

#if defined( SW_PLATFORM_WINDOWS )
    SW_EXPECT_STREQ( "Windows", sw::build::kPlatformName );
#elif defined( SW_PLATFORM_LINUX )
    SW_EXPECT_STREQ( "Linux", sw::build::kPlatformName );
#endif
}

/**
 * @brief [BuildInfoTest] 빌드 타깃 이름과 코드 매크로가 맞물린다 — Game 은 둘 다, Client 는 클라이언트만, Server 는 서버만
 */
SW_TEST_CASE( BuildInfoTest, TargetNameMatchesTheCodeMacros )
{
    // 분기 없이 견준다 — 이름이 컴파일 시간 상수라 if 사슬은 쓰지 않는 갈래가 "닿지 않는 코드" 경고가 된다.
    const sw::string_view targetName{ sw::build::kTargetName };
    SW_EXPECT_TRUE_MSG( targetName == "Game" || targetName == "Client" || targetName == "Server", sw::build::kTargetName );
    SW_EXPECT_EQUAL( targetName != "Server", sw::build::kWithClientCode );
    SW_EXPECT_EQUAL( targetName != "Client", sw::build::kWithServerCode );
}
