#include "pch.h"

#include "Core/String/StringUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Config/EngineConfig.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Shader/Compile/ShaderCache.h"
#include "Engine/Graphics/Shader/Compile/ShaderCompiler.h"

// 셰이더가 읽는 렌더 정책 값(RN-16) — 설정 파일(EngineConfig `_render`)이 정본이고, 엔진이 모든 컴파일에 넣는 define 한 경로로만 셰이더에 간다.

using namespace sw;

namespace
{
    struct ShaderEngineDefinesTestUtil
    {
        /** @brief @p listDefine 에서 이름이 @p pName 인 define 의 수를 세고, 하나라도 있으면 마지막 값을 @p outValue 에 둡니다. */
        static uint32 countDefine( const vector<ShaderMacroDefine>& listDefine, const utf8* pName, string& outValue )
        {
            uint32 foundCount = 0;
            for ( const ShaderMacroDefine& define : listDefine )
            {
                if ( define._name != pName )
                    continue;
                ++foundCount;
                outValue = define._value;
            }
            return foundCount;
        }
    };
} // namespace

/** @brief [ShaderEngineDefinesTest] 엔진 define 에 렌더 정책 값(TAA · 자동 노출)이 설정에서 뜬 사본 그대로 한 번씩 든다(셰이더는 기본값 없이 이 define 을 요구한다) */
SW_TEST_CASE( ShaderEngineDefinesTest, RenderPolicyDefinesComeFromEngineConfig )
{
    vector<ShaderMacroDefine> listDefine;
    ShaderCompiler::appendEngineDefines( listDefine, ShaderEnginePolicy::capture() );
    const RenderConfig& render = engine::getSubsystem<EngineConfig>()._render;
    struct Expected
    {
        const utf8* _pName;
        float32     _value;
    };
    const Expected arrExpected[] = {
        {             "SW_TAA_HISTORY_WEIGHT",            render._taaHistoryWeight},
        {         "SW_TAA_MIN_HISTORY_WEIGHT",         render._taaMinHistoryWeight},
        {        "SW_TAA_VARIANCE_CLIP_GAMMA",        render._taaVarianceClipGamma},
        {     "SW_TAA_VELOCITY_REJECT_PIXELS",     render._taaVelocityRejectPixels},
        {      "SW_AUTO_EXPOSURE_LOW_PERCENT",      render._autoExposureLowPercent},
        {     "SW_AUTO_EXPOSURE_HIGH_PERCENT",     render._autoExposureHighPercent},
        {"SW_AUTO_EXPOSURE_HISTOGRAM_LOG_MIN", render._autoExposureHistogramLogMin},
        {"SW_AUTO_EXPOSURE_HISTOGRAM_LOG_MAX", render._autoExposureHistogramLogMax},
    };
    for ( const Expected& expected : arrExpected )
    {
        string value;
        SW_EXPECT_EQUAL( 1u, ShaderEngineDefinesTestUtil::countDefine( listDefine, expected._pName, value ) );
        SW_EXPECT_EQUAL( to_string( expected._value ), value );
    }
}

/** @brief [ShaderEngineDefinesTest] 프로브 GI 빛샘 막기 계수(lighting.hlsli 가 기본값 없이 요구한다)가 설정에서 뜬 사본 그대로 하나씩 든다 */
SW_TEST_CASE( ShaderEngineDefinesTest, ProbeVisibilityComesFromEngineConfig )
{
    const ProbeVisibilityConfig& config = engine::getSubsystem<EngineConfig>()._render._probeVisibility;
    vector<ShaderMacroDefine>    listDefine;
    ShaderCompiler::appendEngineDefines( listDefine, ShaderEnginePolicy::capture() );
    const struct
    {
        const utf8* _pName;
        float32     _value;
    } arrExpected[] = {
        {       "SW_PROBE_NORMAL_BIAS",        config._normalBias},
        {         "SW_PROBE_VIEW_BIAS",          config._viewBias},
        {    "SW_PROBE_BACKFACE_FLOOR",     config._backfaceFloor},
        {"SW_PROBE_CHEBYSHEV_EXPONENT", config._chebyshevExponent},
    };
    for ( const auto& expected : arrExpected )
    {
        uint32 foundCount = 0;
        for ( const ShaderMacroDefine& define : listDefine )
        {
            if ( define._name != expected._pName )
                continue;
            ++foundCount;
            SW_EXPECT_EQUAL( to_string( expected._value ), define._value );
        }
        SW_EXPECT_EQUAL( 1u, foundCount );
    }
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
 * @brief [ShaderEngineDefinesTest] 실시간 컴파일의 코드젠은 넘겨받은 깃발(기동에서 뜬 진단 정책 사본)이 정한다 — 컴파일 워커가 진단 정책을 직접 읽지 않는다
 * @details 깃발이 꺼졌으면 요청 그대로(쿠킹과 같은 최적화)이고, 켜졌으면 디버그 코드젠이다. 요청이 이미 디버그면 깃발이 꺼도 그대로다.
 */
SW_TEST_CASE( ShaderEngineDefinesTest, LiveCodegenComesFromTheCarriedFlag )
{
    ShaderCompileDesc desc{};
    desc._entryPoint = "PSMain";
    desc._stage      = ShaderStage::Pixel;
    SW_EXPECT_EQUAL( static_cast<uint8>( SW_FALSE ), ShaderCache::makeLiveCompileDesc( desc, false )._bDebugCodegen );
    SW_EXPECT_EQUAL( static_cast<uint8>( SW_TRUE ), ShaderCache::makeLiveCompileDesc( desc, true )._bDebugCodegen );
    desc._bDebugCodegen = SW_TRUE;
    SW_EXPECT_EQUAL( static_cast<uint8>( SW_TRUE ), ShaderCache::makeLiveCompileDesc( desc, false )._bDebugCodegen );
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

/**
 * @brief [ShaderEngineDefinesTest] 전처리한 글의 정규형은 `#line` 줄 · 빈칸 · 빈 줄만 걷어 낸다 — 주석을 고치거나 꺼진 갈래가 다른 변형은 같은 글이고, 낱말이 하나라도 다르면 다른 글이다
 * @details 변이: 줄바꿈까지 빈칸으로 합치면 `#pragma` 줄과 다음 줄이 이어지고, 빈칸을 아예 지우면 `return x` 와 `returnx` 가 같아진다.
 */
SW_TEST_CASE( ShaderEngineDefinesTest, PreprocessedTextIgnoresLineNumbersAndSpacing )
{
    const string base = ShaderCompiler::normalizePreprocessedText( "#line 1 \"a.hlsl\"\nfloat4 main()\n{\n    return x;\n}\n" );
    SW_EXPECT_EQUAL( string( "float4 main()\n{\nreturn x;\n}" ), base );
    SW_EXPECT_EQUAL( base, ShaderCompiler::normalizePreprocessedText( "\r\n  #line 77 \"b.hlsli\"\r\n\r\nfloat4\tmain()\r\n{\r\n\r\n  return  x;\r\n}" ) );
    SW_EXPECT_TRUE( base != ShaderCompiler::normalizePreprocessedText( "float4 main()\n{\n    return y;\n}\n" ) );
    SW_EXPECT_TRUE( base != ShaderCompiler::normalizePreprocessedText( "float4 main()\n{\n    returnx;\n}\n" ) );
    SW_EXPECT_TRUE( ShaderCompiler::normalizePreprocessedText( "#pragma a\nb" ) != ShaderCompiler::normalizePreprocessedText( "#pragma a b" ) );
}
