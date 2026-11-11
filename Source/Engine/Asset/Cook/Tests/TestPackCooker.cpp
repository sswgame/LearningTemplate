#include "pch.h"

#include "Core/Container/unordered_map.h"
#include "Core/Container/unordered_set.h"
#include "Core/String/StringUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Asset/Cook/PackCooker.h"
#include "Engine/Content/Pack/PackConfig.h"

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
        topRule._bRecursive = SW_FALSE;

        PackExcludeRule& deepRule = config._listRule.emplace_back();
        deepRule._name            = "sources";
        deepRule._listExcludeDirectory.push_back( "source" );
        deepRule._listExcludePattern.push_back( "docs/*.md" );
        deepRule._bRecursive = SW_TRUE;
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
 * @brief [PackCookerTest] 팩 쿠킹 기록 id 는 스테이징 폴더(`--cooked-dir`)마다 다르다 — 프리셋이 나눠 쓰는 기록 자리에서 다른 프리셋의 스테이징을 입력으로 적은 기록을 되살리지 않는다
 * @details 같은 폴더는 표기(구분자 · 대소문자)가 달라도 같은 id 다. 타깃 · 팩 이름도 id 를 가른다.
 */
SW_TEST_CASE( PackCookerTest, RecordIDSeparatesStagingFolders )
{
    const string release  = PackCooker::makeRecordID( "Client-dx12", "D:/Repo/build/Ninja-Release/Cooked", "engine.pack" );
    const string shipping = PackCooker::makeRecordID( "Client-dx12", "D:/Repo/build/Ninja-Shipping/Cooked", "engine.pack" );
    SW_EXPECT_NOT_EQUAL( release, shipping );
    SW_EXPECT_EQUAL( release, PackCooker::makeRecordID( "Client-dx12", "D:\\Repo\\build\\Ninja-Release\\Cooked", "engine.pack" ) );
    SW_EXPECT_NOT_EQUAL( release, PackCooker::makeRecordID( "Server-dx12", "D:/Repo/build/Ninja-Release/Cooked", "engine.pack" ) );
    SW_EXPECT_NOT_EQUAL( release, PackCooker::makeRecordID( "Client-dx12", "D:/Repo/build/Ninja-Release/Cooked", "common.pack" ) );
    SW_EXPECT_TRUE( StringUtil::startsWith( release, "packs" ) );
    SW_EXPECT_TRUE( StringUtil::endsWith( release, "engine.pack" ) );
}

/**
 * @brief [PackCookerTest] 버린 셰이더 바이너리는 출처 묶음 하나의 에셋이 모두 닿았을 때만(빈 묶음은 늘) 필요한 것이다 — 닿지 않은 개발 파이프라인이 요청한 것은 경고가 아니다
 */
SW_TEST_CASE( PackCookerTest, DroppedShaderIsNeededOnlyWhenAnOriginIsReached )
{
    const unordered_map<string, vector<vector<string>>> mapRequested = {
        {  "engine/shaders/bin/dx12/gtao_ps.dxil",                           { { "engine/pipeline/deferredpipeline_gi.xml" } }},
        {   "engine/shaders/bin/dx12/lit_ps.dxil", { { "engine/pipeline/forward.xml", "game/empty/materials/rock.material" } }},
        {"engine/shaders/bin/dx12/sprite_ps.dxil",                                                                      { {} }}
    };
    const unordered_set<string> setIncluded = { "engine/pipeline/forward.xml" };
    SW_EXPECT_FALSE( PackCooker::isRequestedByReachedOrigin( mapRequested, setIncluded, "engine/shaders/bin/dx12/gtao_ps.dxil" ) );    // 출처가 닿지 않았다
    SW_EXPECT_FALSE( PackCooker::isRequestedByReachedOrigin( mapRequested, setIncluded, "engine/shaders/bin/dx12/lit_ps.dxil" ) );     // 머티리얼이 닿지 않았다
    SW_EXPECT_TRUE( PackCooker::isRequestedByReachedOrigin( mapRequested, setIncluded, "engine/shaders/bin/dx12/sprite_ps.dxil" ) );   // 늘 필요하다
    SW_EXPECT_FALSE( PackCooker::isRequestedByReachedOrigin( mapRequested, setIncluded, "engine/shaders/bin/dx12/unknown_ps.dxil" ) ); // 요청 밖
    const unordered_set<string> setBoth = { "engine/pipeline/forward.xml", "game/empty/materials/rock.material" };
    SW_EXPECT_TRUE( PackCooker::isRequestedByReachedOrigin( mapRequested, setBoth, "engine/shaders/bin/dx12/lit_ps.dxil" ) );
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
