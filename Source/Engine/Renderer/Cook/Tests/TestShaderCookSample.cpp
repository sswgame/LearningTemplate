// ShaderCookSampleTest — 표본 굽기(`-cook-shaders-sample`)가 규칙의 축마다 모든 값을 한 번씩 덮는 요청만 고르고, 규칙 파일의 틀린 값은 오류다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/Shader/Compile/ShaderCooker.h"
#include "Engine/Renderer/Cook/ShaderCookDriver.h"

using namespace sw;

namespace
{
    ShaderCookRequest makeRequest( string_view path, string_view entryPoint, ShaderStage stage, vector<string> listPermutation )
    {
        ShaderCookRequest request;
        request._shaderPath      = string( path );
        request._entryPoint      = string( entryPoint );
        request._stage           = stage;
        request._listPermutation = std::move( listPermutation );
        return request;
    }
} // namespace

/** @brief [ShaderCookSampleTest] 앞 요청이 이미 덮은 값만 가진 요청은 빠지고, 새 소스 · 진입점 · 스테이지 · define(퍼뮤테이션 없음 포함)을 가진 요청은 남는다. 순서는 요청 순서 그대로다 */
SW_TEST_CASE( ShaderCookSampleTest, KeepsRequestsThatAddAnUncoveredValue )
{
    const vector<ShaderCookRequest> listRequest{
        makeRequest( "engine/shaders/lit.hlsl", "VSMain", ShaderStage::Vertex, {} ),              // 모두 새 값
        makeRequest( "engine/shaders/lit.hlsl", "VSMain", ShaderStage::Vertex, { "A=1" } ),       // define A
        makeRequest( "engine/shaders/lit.hlsl", "VSMain", ShaderStage::Vertex, { "A=1" } ),       // 덮인 값뿐 — 빠진다
        makeRequest( "engine/shaders/lit.hlsl", "PSMain", ShaderStage::Pixel, { "A=1" } ),        // 진입점 · 스테이지
        makeRequest( "engine/shaders/lit.hlsl", "PSMain", ShaderStage::Pixel, { "A=1", "B=1" } ), // define B
        makeRequest( "engine/shaders/cull.hlsl", "CSMain", ShaderStage::Compute, {} ),            // 소스 · 컴퓨트
        makeRequest( "engine/shaders/lit.hlsl", "PSMain", ShaderStage::Pixel, { "B=1", "A=1" } ), // 덮인 값뿐 — 빠진다
    };
    const vector<ShaderCookSampleAxis> listAll{ ShaderCookSampleAxis::Source, ShaderCookSampleAxis::EntryPoint, ShaderCookSampleAxis::Stage, ShaderCookSampleAxis::PermutationDefine };
    const vector<ShaderCookRequest>    listSample = ShaderCookDriver::selectSample( listRequest, listAll );
    SW_REQUIRE_EQUAL( size_t{ 5 }, listSample.size() );
    SW_EXPECT_EQUAL( string( "VSMain" ), listSample[0]._entryPoint );
    SW_EXPECT_TRUE( listSample[1]._listPermutation == vector<string>{ "A=1" } );
    SW_EXPECT_EQUAL( string( "PSMain" ), listSample[2]._entryPoint );
    SW_EXPECT_EQUAL( size_t{ 2 }, listSample[3]._listPermutation.size() );
    SW_EXPECT_EQUAL( string( "engine/shaders/cull.hlsl" ), listSample[4]._shaderPath );

    // 소스 축 하나면 소스마다 첫 요청 하나다.
    const vector<ShaderCookRequest> listBySource = ShaderCookDriver::selectSample( listRequest, { ShaderCookSampleAxis::Source } );
    SW_REQUIRE_EQUAL( size_t{ 2 }, listBySource.size() );
    SW_EXPECT_EQUAL( string( "engine/shaders/cull.hlsl" ), listBySource[1]._shaderPath );
}

/** @brief [ShaderCookSampleTest] 규칙은 비지 않은 _listCoverAxis 배열이고 축 이름은 넷 가운데 하나씩이다 — 빠진 칸 · 빈 배열 · 모르는 · 겹친 이름은 오류다 */
SW_TEST_CASE( ShaderCookSampleTest, RuleNeedsKnownAxesOnce )
{
    const expected<vector<ShaderCookSampleAxis>> rule = ShaderCookDriver::parseSampleRule( R"({ "_listCoverAxis": [ "stage", "source" ] })", "test" );
    SW_REQUIRE_TRUE( rule.has_value() );
    SW_EXPECT_TRUE( *rule == ( vector<ShaderCookSampleAxis>{ ShaderCookSampleAxis::Stage, ShaderCookSampleAxis::Source } ) );
    SW_EXPECT_FALSE( ShaderCookDriver::parseSampleRule( R"({})", "test" ).has_value() );
    SW_EXPECT_FALSE( ShaderCookDriver::parseSampleRule( R"({ "_listCoverAxis": [] })", "test" ).has_value() );
    SW_EXPECT_FALSE( ShaderCookDriver::parseSampleRule( R"({ "_listCoverAxis": [ "format" ] })", "test" ).has_value() );
    SW_EXPECT_FALSE( ShaderCookDriver::parseSampleRule( R"({ "_listCoverAxis": [ "stage", "stage" ] })", "test" ).has_value() );
}

/** @brief [ShaderCookSampleTest] 저장소의 규칙 파일(`Config/Dev/ShaderCookSample.json`)이 읽힌다 — 표본 굽기 테스트(ShaderCache.RebuildFromEmpty)가 기대는 파일이다 */
SW_TEST_CASE( ShaderCookSampleTest, ProjectRuleFileLoads )
{
    const expected<vector<ShaderCookSampleAxis>> rule = ShaderCookDriver::loadSampleRule();
    SW_EXPECT_TRUE( rule.has_value() );
}
