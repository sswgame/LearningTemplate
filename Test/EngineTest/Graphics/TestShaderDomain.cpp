#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Engine/Graphics/Shader/Compile/ShaderCooker.h"

#include "EngineTest/ShaderCookTestUtil.h"

#include "TestFramework/TestFramework.h"

// 셰이더 도메인 발견 — 도메인 목록의 정본은 설정(`EngineConfig._listResourcePriority`)이고 코드는 종류(engine · common)만 안다.
// 전부 임시 리소스 트리와 순수 계산만 본다. 실제 컴파일은 TestShaderDomainCompile.cpp(DXC 가 필요해 호스트 전용).

/**
 * @brief [ShaderDomainTest] 게임 도메인은 이름을 코드가 알지 않고 `game/` 아래 폴더를 훑어 찾는다 — 우선순위에 `game` 이 있을 때만
 */
SW_TEST_CASE( ShaderDomainTest, GameDomainsAreDiscoveredFromTheFolderTree )
{
    const test::ScopedTempResourceRoot resourceRoot;
    const sw::string&                  root = resourceRoot.getRootFolderPath();
    SW_ASSERT_TRUE( resourceRoot.writeTextFile( "game/zeta/shaders/z.hlsl", "float4 PSMain() : SV_Target { return 0; }\n" ) );
    SW_ASSERT_TRUE( resourceRoot.writeTextFile( "game/alpha/shaders/a.hlsl", "float4 PSMain() : SV_Target { return 0; }\n" ) );

    sw::vector<sw::string> listDomain;
    SW_ASSERT_TRUE( sw::ShaderCooker::collectShaderDomains( root, test::makeShaderConfigPriority(), listDomain ) );
    const sw::vector<sw::string> expected{ "game/alpha", "game/zeta", "common", "engine", "editor" };
    SW_EXPECT_TRUE( listDomain == expected );

    // 설정에 game 이 없으면 게임 도메인도 없다 — 폴더가 있다고 코드가 끼워 넣지 않는다.
    SW_ASSERT_TRUE( sw::ShaderCooker::collectShaderDomains( root, { "common", "engine" }, listDomain ) );
    const sw::vector<sw::string> expectedNoGame{ "common", "engine" };
    SW_EXPECT_TRUE( listDomain == expectedNoGame );

    // include 루트는 종류 둘뿐이다 — 어느 게임 도메인도 들어가지 않는다.
    sw::vector<sw::string> listIncludeRoot;
    SW_ASSERT_TRUE( sw::ShaderCooker::collectIncludeRootDomains( test::makeShaderConfigPriority(), listIncludeRoot ) );
    const sw::vector<sw::string> expectedRoot{ "engine", "common" };
    SW_EXPECT_TRUE( listIncludeRoot == expectedRoot );
}

/**
 * @brief [ShaderDomainTest] 설정에 도메인 목록이 없거나 도메인 종류가 빠지면 기본값으로 넘어가지 않고 오류로 멈춘다
 */
SW_TEST_CASE( ShaderDomainTest, MissingConfigIsAnErrorNotADefault )
{
    test::ScopedLogSuppressor suppressor;

    const test::ScopedTempResourceRoot resourceRoot;
    const sw::string&                  root = resourceRoot.getRootFolderPath();
    SW_ASSERT_TRUE( resourceRoot.writeTextFile( "engine/shaders/probe.hlsl", "float4 PSMain() : SV_Target { return 0; }\n" ) );

    sw::vector<sw::string> listDomain{ "stale" };
    SW_EXPECT_FALSE( sw::ShaderCooker::collectShaderDomains( root, {}, listDomain ) );
    SW_EXPECT_TRUE( listDomain.empty() ); // 이전 결과를 남기지 않는다
    SW_EXPECT_FALSE( sw::ShaderCooker::collectShaderDomains( root, { "game", "engine" }, listDomain ) );
    SW_EXPECT_FALSE( sw::ShaderCooker::collectShaderDomains( root, { "game", "common" }, listDomain ) );
    SW_EXPECT_FALSE( sw::ShaderCooker::collectIncludeRootDomains( {}, listDomain ) );
    SW_EXPECT_FALSE( sw::ShaderCooker::collectIncludeRootDomains( { "game", "common" }, listDomain ) );

    // 판정은 문제 하나로 알린다. 쿠킹 스탬프는 쓰지 않는다(없는 스탬프는 낡음이라 다음 판정이 다시 막는다).
    sw::vector<sw::string> listProblem;
    sw::ShaderCooker::verifyCookedShaders( root, {}, "dx12", listProblem );
    SW_EXPECT_EQUAL( size_t( 1 ), listProblem.size() );
}

/**
 * @brief [ShaderDomainTest] 임의의 게임 도메인이 쿠킹 스탬프에서 자기 소스와 engine · common 헤더를 보고, 다른 게임의 헤더는 보지 않는다
 */
SW_TEST_CASE( ShaderDomainTest, ArbitraryDomainStampTracksItsIncludeRootsOnly )
{
    const test::ScopedTempResourceRoot resourceRoot;
    const sw::string&                  root = resourceRoot.getRootFolderPath();
    SW_ASSERT_TRUE( resourceRoot.writeTextFile( "engine/shaders/domaininc.hlsli", "#define SW_DOMAIN_PROBE 1\n" ) );
    SW_ASSERT_TRUE( resourceRoot.writeTextFile( "game/domaintest/shaders/probe.hlsl", "#include \"domaininc.hlsli\"\nfloat4 PSMain() : SV_Target { return 0; }\n" ) );
    SW_ASSERT_TRUE( resourceRoot.writeTextFile( "game/other/shaders/other.hlsli", "// another game\n" ) );
    const sw::string shaderPath = resourceRoot.makePath( "game/domaintest/shaders/probe.hlsl" );
    const sw::string binDir     = resourceRoot.makePath( "game/domaintest/shaders/bin/dx12" );
    SW_ASSERT_TRUE( sw::FileUtil::ensureDirectoryExists( binDir ) );

    sw::ShaderCooker::writeCookStamp( root, test::makeShaderConfigPriority(), binDir, nullptr );
    sw::ShaderCooker::invalidateSharedHeaderCache();
    SW_EXPECT_TRUE( sw::ShaderCooker::isCookedOutputCurrent( root, test::makeShaderConfigPriority(), binDir, shaderPath ) );

    sw::string stampText;
    SW_ASSERT_TRUE( sw::FileUtil::readTextFile( sw::FileUtil::joinPath( binDir, "cook.stamp" ), stampText ) );
    SW_EXPECT_TRUE( stampText.find( " probe.hlsl" ) != sw::string::npos );
    SW_EXPECT_TRUE( stampText.find( " engine:domaininc.hlsli" ) != sw::string::npos );
    SW_EXPECT_TRUE( stampText.find( "other.hlsli" ) == sw::string::npos ); // 다른 게임은 include 루트가 아니다

    // engine 헤더가 바뀌면 낡음이다. 다른 게임의 헤더가 바뀌어도 이 게임은 그대로다.
    SW_ASSERT_TRUE( resourceRoot.writeTextFile( "game/other/shaders/other.hlsli", "// another game, edited\n" ) );
    sw::ShaderCooker::invalidateSharedHeaderCache();
    SW_EXPECT_TRUE( sw::ShaderCooker::isCookedOutputCurrent( root, test::makeShaderConfigPriority(), binDir, shaderPath ) );
    SW_ASSERT_TRUE( resourceRoot.writeTextFile( "engine/shaders/domaininc.hlsli", "#define SW_DOMAIN_PROBE 2\n" ) );
    sw::ShaderCooker::invalidateSharedHeaderCache();
    SW_EXPECT_FALSE( sw::ShaderCooker::isCookedOutputCurrent( root, test::makeShaderConfigPriority(), binDir, shaderPath ) );
}

/**
 * @brief [ShaderDomainTest] 게임 폴더의 같은 이름 헤더가 엔진 헤더를 가리면 조용히 고르지 않고 오류로 멈춘다 — 스탬프도 쓰지 않는다
 */
SW_TEST_CASE( ShaderDomainTest, SameNameHeaderCannotHideAnEngineHeader )
{
    test::ScopedLogSuppressor suppressor;

    const test::ScopedTempResourceRoot resourceRoot;
    const sw::string&                  root = resourceRoot.getRootFolderPath();
    SW_ASSERT_TRUE( resourceRoot.writeTextFile( "engine/shaders/common.hlsli", "#define SW_COMMON 1\n" ) );
    SW_ASSERT_TRUE( resourceRoot.writeTextFile( "game/domaintest/shaders/probe.hlsl", "#include \"common.hlsli\"\nfloat4 PSMain() : SV_Target { return 0; }\n" ) );
    const sw::string shaderPath = resourceRoot.makePath( "game/domaintest/shaders/probe.hlsl" );

    // 가림이 없으면 통과다. 도메인 밖의 소스(임시 폴더의 파일)도 가릴 헤더가 없다.
    sw::vector<sw::string> listProblem;
    SW_EXPECT_TRUE( sw::ShaderCooker::findShadowedHeaders( root, test::makeShaderConfigPriority(), shaderPath, listProblem ) );
    SW_EXPECT_TRUE( listProblem.empty() );

    SW_ASSERT_TRUE( resourceRoot.writeTextFile( "game/domaintest/shaders/common.hlsli", "#define SW_COMMON 2\n" ) );
    SW_EXPECT_FALSE( sw::ShaderCooker::findShadowedHeaders( root, test::makeShaderConfigPriority(), shaderPath, listProblem ) );
    SW_ASSERT_EQUAL( size_t( 1 ), listProblem.size() );
    SW_EXPECT_TRUE( listProblem.front().find( "common.hlsli" ) != sw::string::npos );

    const sw::string binDir = resourceRoot.makePath( "game/domaintest/shaders/bin/dx12" );
    SW_ASSERT_TRUE( sw::FileUtil::ensureDirectoryExists( binDir ) );
    sw::ShaderCooker::writeCookStamp( root, test::makeShaderConfigPriority(), binDir, nullptr );
    SW_EXPECT_FALSE( sw::FileUtil::exists( sw::FileUtil::joinPath( binDir, "cook.stamp" ) ) );

    sw::vector<sw::string> listVerifyProblem;
    sw::ShaderCooker::verifyCookedShaders( root, test::makeShaderConfigPriority(), "dx12", listVerifyProblem );
    bool bReported = false;
    for ( const sw::string& problem : listVerifyProblem )
    {
        bReported = bReported || problem.find( "would hide" ) != sw::string::npos;
    }
    SW_EXPECT_TRUE( bReported );
}
