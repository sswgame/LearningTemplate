#include "pch.h"

#include "Core/String/StringUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Config/EngineConfig.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Shader/Compile/ShaderCompiler.h"

// 셰이더가 읽는 렌더 정책 값(RN-16) — 설정 파일(EngineConfig `_render`)이 정본이고, 엔진이 모든 컴파일에 넣는 define 한 경로로만 셰이더에 간다.

using namespace sw;

/** @brief [ShaderEngineDefinesTest] 엔진 define 에 TAA 기록 비율이 설정에서 뜬 사본 그대로 한 번 든다(셰이더는 기본값 없이 이 define 을 요구한다) */
SW_TEST_CASE( ShaderEngineDefinesTest, TAAHistoryWeightComesFromEngineConfig )
{
    vector<ShaderMacroDefine> listDefine;
    ShaderCompiler::appendEngineDefines( listDefine, ShaderEnginePolicy::capture() );
    uint32 foundCount = 0;
    for ( const ShaderMacroDefine& define : listDefine )
    {
        if ( define._name != "SW_TAA_HISTORY_WEIGHT" )
            continue;
        ++foundCount;
        SW_EXPECT_EQUAL( to_string( engine::getSubsystem<EngineConfig>()._render._taaHistoryWeight ), define._value );
    }
    SW_EXPECT_EQUAL( 1u, foundCount );
}

/**
 * @brief [ShaderEngineDefinesTest] 엔진 설정 사본을 싣지 않은 요청은 컴파일하지 않고 진다 — 컴파일 워커가 설정을 직접 읽어 채우지 않는다
 * @details 설정 다시 읽기는 제자리 덮기이고 그 울타리는 렌더 스레드만 기다린다. 요청을 만드는 쪽(`ShaderCache` · 쿠킹 시작)이 사본을 싣는다.
 */
SW_TEST_CASE( ShaderEngineDefinesTest, CompileWithoutEnginePolicyFails )
{
    ShaderCompileDesc desc{};
    desc._entryPoint = "PSMain";
    desc._stage      = ShaderStage::Pixel;
    SW_EXPECT_LOG( "ShaderCompiler.EnginePolicyMissing" );
    const ShaderCompileResult result = ShaderCompiler::compileHLSL( desc );
    SW_EXPECT_FALSE( result._bSuccess );
    SW_EXPECT_TRUE( StringUtil::contains( result._errorMessage, "engine policy" ) );
}

/**
 * @brief [ShaderEngineDefinesTest] 단계마다 컴파일러가 넣는 매크로가 있고 모두 다르다(R-15 — 머티리얼 샘플이 픽셀 단계에서만 밉 바이어스를 더한다)
 * @details 변이: 두 단계가 같은 매크로를 쓰면 정점 셰이더에도 `SampleBias` 가 들어가 컴파일이 깨진다.
 */
SW_TEST_CASE( ShaderEngineDefinesTest, StageMacroIsUniquePerStage )
{
    SW_EXPECT_TRUE( StringUtil::equals( string_view( getShaderStageInfo( ShaderStage::Pixel )._pStageMacro ), string_view( "SW_SHADER_STAGE_PIXEL" ) ) );
    for ( uint32 first = 0; first < static_cast<uint32>( ShaderStage::Count ); ++first )
    {
        const utf8* pFirst = getShaderStageInfo( static_cast<ShaderStage>( first ) )._pStageMacro;
        SW_REQUIRE_TRUE( pFirst != nullptr );
        for ( uint32 second = first + 1; second < static_cast<uint32>( ShaderStage::Count ); ++second )
            SW_EXPECT_FALSE( StringUtil::equals( string_view( pFirst ), string_view( getShaderStageInfo( static_cast<ShaderStage>( second ) )._pStageMacro ) ) );
    }
}
