// BuildInfo — CMake 가 정한 구성 · 플랫폼 · 타깃 이름이 같은 빌드의 분기 매크로와 맞물리는지.
#include "pch.h"

#include "Core/Foundation/BuildInfo.h"
#include "Core/Testing/TestFramework.h"

using namespace sw;

/**
 * @brief [BuildInfoTest] CMake 가 정한 구성 · 플랫폼 이름이 같은 빌드의 구성 · 플랫폼 매크로와 맞는다
 * @details 이름(`SW_BUILD_CONFIG_NAME` · `SW_PLATFORM_NAME`)과 분기용 매크로(`SW_DEBUG` · `SW_PLATFORM_*`)는 CMake 의 다른 줄에서
 *          정의되므로 한쪽만 바뀌면 크래시 리포트가 엉뚱한 구성을 적는다. 배포 구성은 테스트를 짓지 않는다.
 */
SW_TEST_CASE( BuildInfoTest, NamesAgreeWithTheBuildMacros )
{
    const string_view configName{ build::kConfigName };
#if defined( SW_DEBUG )
    SW_EXPECT_EQUAL( string_view( "Debug" ), configName );
#else
    SW_EXPECT_TRUE( configName != "Debug" && configName != "Shipping" );
    SW_EXPECT_FALSE( configName.empty() );
#endif

#if defined( SW_PLATFORM_WINDOWS )
    SW_EXPECT_EQUAL( string_view( "Windows" ), string_view( build::kPlatformName ) );
#elif defined( SW_PLATFORM_LINUX )
    SW_EXPECT_EQUAL( string_view( "Linux" ), string_view( build::kPlatformName ) );
#endif
}

/** @brief [BuildInfoTest] 빌드 타깃 이름과 코드 매크로가 맞물린다 — Game 은 둘 다, Client 는 클라이언트만, Server 는 서버만 */
SW_TEST_CASE( BuildInfoTest, TargetNameMatchesTheCodeMacros )
{
    // 분기 없이 비교한다 — 이름이 컴파일 시간 상수라 if 사슬은 쓰지 않는 분기가 "닿지 않는 코드" 경고가 된다.
    const string_view targetName{ build::kTargetName };
    SW_EXPECT_TRUE_MSG( targetName == "Game" || targetName == "Client" || targetName == "Server", build::kTargetName );
    SW_EXPECT_EQUAL( targetName != "Server", build::kWithClientCode );
    SW_EXPECT_EQUAL( targetName != "Client", build::kWithServerCode );
}
