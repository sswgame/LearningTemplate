#include "pch.h"

#include "Core/String/StringUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Config/EngineConfig.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Shader/Compile/ShaderCompiler.h"

// 셰이더가 읽는 렌더 정책 값(RN-16) — 설정 파일(EngineConfig `_render`)이 정본이고, 엔진이 모든 컴파일에 넣는 define 한 경로로만 셰이더에 간다.

using namespace sw;

/** @brief [ShaderEngineDefinesTest] 엔진 define 에 TAA 기록 비율이 설정 값 그대로 한 번 든다(셰이더는 기본값 없이 이 define 을 요구한다) */
SW_TEST_CASE( ShaderEngineDefinesTest, TAAHistoryWeightComesFromEngineConfig )
{
    vector<ShaderMacroDefine> listDefine;
    ShaderCompiler::appendEngineDefines( listDefine );
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
