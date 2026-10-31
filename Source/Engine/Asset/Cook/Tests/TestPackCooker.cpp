#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Asset/Cook/PackCooker.h"
#include "Engine/FileSystem/Pack/PackConfig.h"

using namespace sw;

// PackCookerTest — 팩 쿠커가 무엇을 담을지: 도메인의 팩 파일 이름, 원본 파일의 제외(전역 폴더 · 패턴, 규칙의 최상위 · 재귀 폴더 · 마디 패턴,
// 셰이더 원본, 다른 백엔드의 셰이더 바이너리, 빌드 타깃이 빼는 종류), 스테이징 산출물의 제외. 파일을 읽지 않는다(nogpu).

namespace
{
    constexpr string_view kShaderFolder = "dx12";

    PackConfig makeConfig()
    {
        PackConfig config;
        config._bExcludeRawShaderSource = true;
        config._listGlobalExcludeDirectory.push_back( "temp" );
        config._listGlobalExcludePattern.push_back( "*.psd" );

        PackExcludeRule& topRule = config._listRule.emplace_back();
        topRule._name            = "editor";
        topRule._listExcludeDirectory.push_back( "editor" );
        topRule._bRecursive = false;

        PackExcludeRule& deepRule = config._listRule.emplace_back();
        deepRule._name            = "sources";
        deepRule._listExcludeDirectory.push_back( "source" );
        deepRule._listExcludePattern.push_back( "docs/*.md" );
        deepRule._bRecursive = true;
        return config;
    }

    bool includes( const PackConfig& config, string_view relativePath, string_view buildTarget = {} )
    {
        return PackCooker::shouldIncludeFile( relativePath, config, kShaderFolder, buildTarget );
    }
} // namespace

/**
 * @brief [PackCookerTest] 팩 파일 이름은 정규화(소문자)한 도메인의 `/` 를 `_` 로 바꾸고 `.pack` 을 붙인다(끝 `/` 는 뗀다)
 */
SW_TEST_CASE( PackCookerTest, PackFileNameFlattensTheDomain )
{
    SW_EXPECT_EQUAL( string( "engine.pack" ), PackCooker::makePackFileName( "engine" ) );
    SW_EXPECT_EQUAL( string( "game_empty.pack" ), PackCooker::makePackFileName( "game/empty" ) );
    SW_EXPECT_EQUAL( string( "game_empty.pack" ), PackCooker::makePackFileName( "Game\\Empty/" ) );
}

/**
 * @brief [PackCookerTest] 전역 제외 폴더는 어느 깊이든, 전역 패턴은 파일 이름에, 비재귀 규칙은 도메인 바로 아래 폴더만, 재귀 규칙은 어느 깊이든 뺀다
 */
SW_TEST_CASE( PackCookerTest, ExcludeRulesFollowDepthAndPatterns )
{
    const PackConfig config = makeConfig();
    SW_EXPECT_TRUE( includes( config, "textures/rock.dds" ) );
    SW_EXPECT_FALSE( includes( config, "levels/temp/autosave.txt" ) );
    SW_EXPECT_FALSE( includes( config, "art/Concept.PSD" ) ); // 대소문자 무시
    SW_EXPECT_FALSE( includes( config, "editor/icons.txt" ) );
    SW_EXPECT_TRUE( includes( config, "ui/editor/icons.txt" ) ); // 비재귀 규칙은 최상위만
    SW_EXPECT_FALSE( includes( config, "models/hero/source/hero.blend" ) );
    SW_EXPECT_FALSE( includes( config, "docs/readme.md" ) );
    SW_EXPECT_TRUE( includes( config, "docs/guide/readme.md" ) ); // 마디 `*` 는 폴더를 넘지 않는다
}

/**
 * @brief [PackCookerTest] 셰이더 원본(`.hlsl` · `.hlsli`)은 설정이 빼라면 빼고, 셰이더 바이너리는 담을 백엔드 폴더 것만 담는다
 */
SW_TEST_CASE( PackCookerTest, ShaderSourcesAndOtherBackendsAreExcluded )
{
    PackConfig config = makeConfig();
    SW_EXPECT_FALSE( includes( config, "shaders/lit.hlsl" ) );
    SW_EXPECT_FALSE( includes( config, "shaders/common.HLSLI" ) );
    SW_EXPECT_TRUE( includes( config, "shaders/bin/dx12/lit.cso" ) );
    SW_EXPECT_FALSE( includes( config, "shaders/bin/vulkan/lit.spv" ) );
    SW_EXPECT_FALSE( includes( config, "shaders/bin/dx11/lit.cso" ) );

    config._bExcludeRawShaderSource = false;
    SW_EXPECT_TRUE( includes( config, "shaders/lit.hlsl" ) );
}

/**
 * @brief [PackCookerTest] 빌드 타깃이 빼는 종류(전용 서버의 `Texture`)는 원본 · 스테이징 모두 빼고, 다른 타깃 · 빈 타깃은 담는다
 */
SW_TEST_CASE( PackCookerTest, BuildTargetExcludesItsAssetKinds )
{
    const PackConfig config = makeConfig();
    SW_EXPECT_FALSE( includes( config, "textures/rock.dds", "Server" ) );
    SW_EXPECT_TRUE( includes( config, "textures/rock.dds", "Game" ) );
    SW_EXPECT_FALSE( PackCooker::shouldIncludeStagedFile( "textures/rock.dds", kShaderFolder, "Server" ) );
    SW_EXPECT_TRUE( PackCooker::shouldIncludeStagedFile( "textures/rock.dds", kShaderFolder, "" ) );
}

/**
 * @brief [PackCookerTest] 스테이징 산출물은 설정의 제외를 보지 않고 다른 백엔드의 셰이더 바이너리만 뺀다
 */
SW_TEST_CASE( PackCookerTest, StagedFilesOnlyDropOtherBackendShaders )
{
    SW_EXPECT_TRUE( PackCooker::shouldIncludeStagedFile( "scenes/main.scene", kShaderFolder, "" ) );
    SW_EXPECT_TRUE( PackCooker::shouldIncludeStagedFile( "editor/temp/x.txt", kShaderFolder, "" ) );
    SW_EXPECT_TRUE( PackCooker::shouldIncludeStagedFile( "shaders/bin/dx12/lit.cso", kShaderFolder, "" ) );
    SW_EXPECT_FALSE( PackCooker::shouldIncludeStagedFile( "shaders/bin/opengl/lit.spv", kShaderFolder, "" ) );
}
