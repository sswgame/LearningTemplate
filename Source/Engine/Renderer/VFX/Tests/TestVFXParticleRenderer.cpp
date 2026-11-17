#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Renderer/VFX/VFXParticleRenderer.h"

// 입자 그리기의 셰이더 계약(PS-2) — 순서 목록 슬롯이 띠(t4..)와 겹치지 않고, 플래그 칸의 순서 구간 비트가 C++ 과 particle.hlsl 에서 같다.

using namespace sw;

/** @brief [VFXParticleRendererTest] 순서 목록은 비어 있던 t0..t3 의 칸이고, particle.hlsl 이 그 슬롯 · 이름으로 선언하고 같은 비트에서 구간을 읽는다. */
SW_TEST_CASE( VFXParticleRendererTest, ShaderReadsOrderRangeFromFlags )
{
    SW_EXPECT_TRUE( shaderslot::kParticleOrderBuffer < shaderslot::kInstanceBuffer );
    SW_EXPECT_TRUE( shaderslot::kParticleOrderBuffer < shaderslot::kSRVSlotCount );

    const string projectRoot = FileUtil::normalizeSeparators( ProcessRoots::get()._project.str() );
    SW_REQUIRE_FALSE( projectRoot.empty() );
    string particleText;
    SW_REQUIRE_TRUE( FileUtil::readTextFile( FileUtil::joinPath( projectRoot, "Content/engine/shaders/particle.hlsl" ), particleText ) );
    const string declaration = string( "SW_DECLARE_STRUCTURED_BUFFER( uint, " ) + shaderslot::resname::kParticleOrder + ", SW_SLOT_PARTICLE_ORDER_SRV );";
    SW_EXPECT_TRUE( particleText.find( declaration ) != string::npos );
    // 정렬 공백은 포매터가 바꾸므로 이름 뒤 `=` 다음 값만 본다.
    const size_t namePosition = particleText.find( "static const uint kParticleOrderShift" );
    SW_REQUIRE_TRUE( namePosition != string::npos );
    const size_t equalPosition = particleText.find( '=', namePosition );
    const size_t endPosition   = particleText.find( ';', namePosition );
    SW_REQUIRE_TRUE( equalPosition != string::npos && endPosition != string::npos && equalPosition < endPosition );
    string value = particleText.substr( equalPosition + 1, endPosition - equalPosition - 1 );
    value.erase( 0, value.find_first_not_of( ' ' ) );
    SW_EXPECT_TRUE_MSG( value == to_string( VFXParticleRenderer::kRootFlagOrderShift ) + "u", "particle.hlsl kParticleOrderShift must equal VFXParticleRenderer::kRootFlagOrderShift" );
}
