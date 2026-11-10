#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"

// shaderslot::gl::checkTextureUnits — OpenGLRHIDevice 초기화가 GL 이 보고한 텍스처 유닛 수로 백엔드를 받는지 가르는 판정.
// 결합 유닛은 엔진 칸 끝(머티리얼 t16..t31 위 엔진 t32..)까지, 프래그먼트 유닛은 한 셰이더의 샘플러 예산만큼이다.

using namespace sw;
using shaderslot::gl::checkTextureUnits;
using shaderslot::gl::TextureUnitCheck;

SW_TEST_CASE( GLTextureUnitLimitsTest, CombinedCountCoversEngineSlots )
{
    // 엔진 칸이 머티리얼 칸 위에 있으므로 결합 유닛 요구치는 머티리얼 칸 끝보다 크다 — 예전 검사가 머티리얼 칸 끝(32)만 세던 틈.
    SW_EXPECT_EQUAL( shaderslot::kEngineTexture0 + shaderslot::kEngineTextureCount, shaderslot::gl::kCombinedTextureUnitCount );
    SW_EXPECT_TRUE( shaderslot::gl::kCombinedTextureUnitCount > shaderslot::kMaterialTexture0 + shaderslot::kMaterialTextureCount );
    SW_EXPECT_EQUAL( shaderslot::kMaterialTextureCount + shaderslot::kEngineTextureCount, shaderslot::gl::kFragmentTextureUnitCount );
}

SW_TEST_CASE( GLTextureUnitLimitsTest, DesktopAndMesaLimitsPass )
{
    // NVIDIA · AMD 데스크톱과 Mesa(d3d12 · llvmpipe)는 프래그먼트 32, 결합 96 이상이다.
    SW_EXPECT_EQUAL( TextureUnitCheck::None, checkTextureUnits( 32, 192 ) );
    SW_EXPECT_EQUAL( TextureUnitCheck::None, checkTextureUnits( 32, static_cast<int32>( shaderslot::gl::kCombinedTextureUnitCount ) ) );
}

SW_TEST_CASE( GLTextureUnitLimitsTest, CombinedBelowEngineSlotEndFails )
{
    // 머티리얼 칸 끝(32)까지만 되는 결합 유닛은 엔진 칸(t32..)을 묶지 못한다.
    const int32 materialEnd = static_cast<int32>( shaderslot::kMaterialTexture0 + shaderslot::kMaterialTextureCount );
    SW_EXPECT_EQUAL( TextureUnitCheck::Combined, checkTextureUnits( 32, materialEnd ) );
    SW_EXPECT_EQUAL( TextureUnitCheck::Combined, checkTextureUnits( 32, static_cast<int32>( shaderslot::gl::kCombinedTextureUnitCount ) - 1 ) );
}

SW_TEST_CASE( GLTextureUnitLimitsTest, FragmentBelowBudgetFails )
{
    // GL 스펙 최소(16)인 드라이버는 머티리얼 칸 16 개와 엔진 칸을 한 셰이더에 함께 걸 수 없다.
    SW_EXPECT_EQUAL( TextureUnitCheck::Fragment, checkTextureUnits( 16, 192 ) );
    SW_EXPECT_EQUAL( TextureUnitCheck::Fragment, checkTextureUnits( static_cast<int32>( shaderslot::gl::kFragmentTextureUnitBudget ) - 1, 192 ) );
}
