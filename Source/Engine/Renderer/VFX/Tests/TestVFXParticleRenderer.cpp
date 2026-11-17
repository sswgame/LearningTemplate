#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Renderer/VFX/VFXParticleRenderer.h"
#include "Engine/VFX/VFXEffectDef.h"

// 입자 그리기의 셰이더 계약(PS-2) — 순서 목록 슬롯이 띠(t4..)와 겹치지 않고, 플래그 칸의 순서 구간 비트가 C++ 과 particle.hlsl 에서 같다.

using namespace sw;

namespace
{
    /** @brief @p text 에서 @p name 뒤의 값(공백 다음 첫 토큰 — `#define 이름 값` · `이름 = 값u;`)을 읽습니다. 정렬 공백은 포매터가 바꾸므로 토큰만 본다. */
    string findTokenAfter( const string& text, const string& name )
    {
        const size_t namePosition = text.find( name );
        if ( namePosition == string::npos )
            return string{};
        size_t cursor = namePosition + name.size();
        while ( cursor < text.size() && ( text[cursor] == ' ' || text[cursor] == '=' ) )
        {
            ++cursor;
        }
        size_t end = cursor;
        while ( end < text.size() && text[end] > ' ' && text[end] != ';' ) // 공백 · 줄 끝(제어 문자)에서 멈춘다
        {
            ++end;
        }
        return text.substr( cursor, end - cursor );
    }

    string readEngineShader( const utf8* pFileName )
    {
        const string projectRoot = FileUtil::normalizeSeparators( ProcessRoots::get()._project.str() );
        string       text;
        if ( projectRoot.empty() == false )
            std::ignore = FileUtil::readTextFile( FileUtil::joinPath( FileUtil::joinPath( projectRoot, "Content/engine/shaders" ), pFileName ), text );
        return text;
    }
} // namespace

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

/** @brief [VFXParticleRendererTest] GPU 정렬 한도(C++ 로드 검사)와 정렬 셰이더의 워크그룹 크기가 같고, 셰이더의 모드 값이 VFXSortMode 와 같다(PS-3 계약). */
SW_TEST_CASE( VFXParticleRendererTest, GPUSortLimitMatchesShader )
{
    const string sortText = readEngineShader( "vfx_particle_sort.hlsl" );
    SW_REQUIRE_FALSE( sortText.empty() );
    SW_EXPECT_TRUE( findTokenAfter( sortText, "#define SW_SORT_MAX_ELEMENTS" ) == to_string( VFXEmitterDef::kMaxGPUSortedParticles ) );
    SW_EXPECT_TRUE( findTokenAfter( sortText, "static const uint kVFXSortViewDepth" ) == to_string( static_cast<uint32>( VFXSortMode::ViewDepth ) ) + "u" );
    SW_EXPECT_TRUE( findTokenAfter( sortText, "static const uint kVFXSortOldestFirst" ) == to_string( static_cast<uint32>( VFXSortMode::OldestFirst ) ) + "u" );
    SW_EXPECT_TRUE( findTokenAfter( sortText, "static const uint kVFXSortYoungestFirst" ) == to_string( static_cast<uint32>( VFXSortMode::YoungestFirst ) ) + "u" );
    // 입자 번호 칸(9 비트)이 한도를 담는다.
    SW_EXPECT_TRUE( VFXEmitterDef::kMaxGPUSortedParticles <= ( 1u << 9u ) );
}
