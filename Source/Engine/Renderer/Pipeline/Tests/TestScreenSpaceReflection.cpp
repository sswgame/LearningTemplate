#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/View/PostProcessValues.h"
#include "Engine/Renderer/Pipeline/RenderPassTypeInfo.h"

// 화면 공간 반사 — 패스 종류 줄(전용 실행 · 블렌드 · 입력 계약)과 기본값(꺼짐).

using namespace sw;

/**
 * @brief [ScreenSpaceReflectionTest] 표 줄 — 장면 색 사본 · 깊이 · G버퍼 노멀을 읽고 장면 색 위에 프리멀티플라이로 블렌드하는 전용 실행이며, 파이프라인 XML 에 쓸 수 있다.
 *        원본을 가리키는 끄기(kBypassesToSource)는 쓰지 않는다 — 출력이 반투명도 그리는 장면 색이다
 */
SW_TEST_CASE( ScreenSpaceReflectionTest, PassTypeRowDeclaresBlendedDrawOverSceneColor )
{
    const RenderPassTypeInfo& info = getRenderPassTypeInfo( RenderPassType::ScreenSpaceReflection );
    SW_EXPECT_TRUE( isPipelinePassType( RenderPassType::ScreenSpaceReflection ) );
    SW_EXPECT_TRUE( info.hasFlag( RenderPassTraitFlag::kBlend ) );
    SW_EXPECT_TRUE( info.hasFlag( RenderPassTraitFlag::kPremultipliedAlpha ) );
    SW_EXPECT_FALSE( info.hasFlag( RenderPassTraitFlag::kGenericFullscreen ) );
    SW_EXPECT_FALSE( info.hasFlag( RenderPassTraitFlag::kBypassesToSource ) );
    SW_EXPECT_TRUE( info._inputContract.reads( RenderPassInputRole::SceneColorCopy ) );
    SW_EXPECT_TRUE( info._inputContract.reads( RenderPassInputRole::SceneDepth ) );
    SW_EXPECT_TRUE( info._inputContract.reads( RenderPassInputRole::GBufferNormal ) );
    SW_EXPECT_TRUE( info._inputContract.reads( RenderPassInputRole::GBufferMaterial ) );
    SW_EXPECT_FALSE( info._inputContract.reads( RenderPassInputRole::SourceColor ) ); // 쓰는 장면 색을 읽지 않는다 — 사본을 읽는다
}

/** @brief [ScreenSpaceReflectionTest] 기본 후처리 값은 반사를 끈다(세기 0) — 패스가 그리지 않아 지금 그림이 그대로다 */
SW_TEST_CASE( ScreenSpaceReflectionTest, DefaultValuesTurnReflectionsOff )
{
    const PostProcessValues values;
    SW_EXPECT_NEAR( 0.0f, values._screenSpaceReflectionIntensity, 0.0f );
    SW_EXPECT_TRUE( values._screenSpaceReflectionMaxRoughness > 0.0f );
}
