/**
 * @file TestRHIPipelineStateDesc.cpp
 * @brief 파이프라인 서술체 지문(`RHIPipelineStateDesc::computeHash`)이 PSO 를 가르는 칸마다 달라지는가입니다(nogpu). 코드가 짓는 PSO 저장소의 키입니다.
 */
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/RHI/RHIPipelineStateDesc.h"

using namespace sw;

/**
 * @brief [RHIPipelineStateDescTest] 타깃 포맷 · 셰이더 · define · 렌더 상태 하나만 달라도 지문이 다르고, 같은 서술은 같은 지문이다
 * @details 변이: 지문에서 타깃 포맷을 빼면(예전 쿠키 아틀라스 키처럼) 포맷만 다른 두 서술이 같은 PSO 를 받아 첫 단언이 진다.
 */
SW_TEST_CASE( RHIPipelineStateDescTest, HashSeparatesEveryPSOField )
{
    RHIPipelineStateDesc base{};
    base._vertexShaderPath = "shaders/blit.hlsl";
    base._pixelShaderPath  = "shaders/blit.hlsl";
    base._arrRTVFormat[0]  = RHIFormat::R8G8B8A8_UNORM;

    RHIPipelineStateDesc otherFormat = base;
    otherFormat._arrRTVFormat[0]     = RHIFormat::R16G16B16A16_FLOAT;
    SW_EXPECT_NOT_EQUAL( base.computeHash(), otherFormat.computeHash() );

    RHIPipelineStateDesc otherShader = base;
    otherShader._pixelShaderPath     = "shaders/other.hlsl";
    SW_EXPECT_NOT_EQUAL( base.computeHash(), otherShader.computeHash() );

    RHIPipelineStateDesc otherDefine = base;
    otherDefine._listShaderDefine.push_back( "USE_FOG=1" );
    SW_EXPECT_NOT_EQUAL( base.computeHash(), otherDefine.computeHash() );

    RHIPipelineStateDesc otherState            = base;
    otherState._renderState._bEnableDepthWrite = otherState._renderState._bEnableDepthWrite == SW_TRUE ? SW_FALSE : SW_TRUE;
    SW_EXPECT_NOT_EQUAL( base.computeHash(), otherState.computeHash() );

    // 쓰지 않는 타깃 칸(`_numRenderTargets` 밖)은 PSO 를 가르지 않는다.
    RHIPipelineStateDesc unusedSlot = base;
    unusedSlot._arrRTVFormat[1]     = RHIFormat::R16G16B16A16_FLOAT;
    SW_EXPECT_EQUAL( base.computeHash(), unusedSlot.computeHash() );

    const RHIPipelineStateDesc copy = base;
    SW_EXPECT_EQUAL( base.computeHash(), copy.computeHash() );
}
