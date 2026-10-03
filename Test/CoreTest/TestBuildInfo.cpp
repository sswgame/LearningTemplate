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
