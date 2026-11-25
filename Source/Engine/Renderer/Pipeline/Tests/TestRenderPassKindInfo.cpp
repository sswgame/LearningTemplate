// RenderPassKindInfo — 패스 표의 PSO 기본 블렌드(끔 · 곧은 알파 · 미리 곱한 알파)가 그 패스가 그리는 방식과 맞는다(nogpu).
#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "Engine/Renderer/Pipeline/RenderPassKindInfo.h"

using namespace sw;

/**
 * @brief [RenderPassKindInfoTest] 위에 섞는 패스는 블렌드가 켜져 있고(UI · 안개 · 반사 · HDR UI 합성은 미리 곱한 알파), 덮어쓰는 출력 패스는 블렌드가 없다.
 *        결함이면 HDR UI 합성이 장면 출력 RT 를 덮어써 HDR 모니터에 장면이 검고 UI 만 남는다(블렌드 깃발 없이 프리멀티플라이 깃발만 적었던 결함)
 */
SW_TEST_CASE( RenderPassKindInfoTest, BlendMatchesHowThePassDraws )
{
    SW_EXPECT_TRUE( getRenderPassKindInfo( RenderPassKind::DisplayCompositeUI )._blend == RenderPassBlend::Premultiplied );
    SW_EXPECT_TRUE( getRenderPassKindInfo( RenderPassKind::Canvas )._blend == RenderPassBlend::Premultiplied );
    SW_EXPECT_TRUE( getRenderPassKindInfo( RenderPassKind::Fog )._blend == RenderPassBlend::Premultiplied );
    SW_EXPECT_TRUE( getRenderPassKindInfo( RenderPassKind::ScreenSpaceReflection )._blend == RenderPassBlend::Premultiplied );
    SW_EXPECT_TRUE( getRenderPassKindInfo( RenderPassKind::Transparent )._blend == RenderPassBlend::Straight );
    SW_EXPECT_TRUE( getRenderPassKindInfo( RenderPassKind::Decal )._blend == RenderPassBlend::Straight );
    SW_EXPECT_TRUE( getRenderPassKindInfo( RenderPassKind::DisplayEncodeHDR10 )._blend == RenderPassBlend::None );
    SW_EXPECT_TRUE( getRenderPassKindInfo( RenderPassKind::DisplayEncodeScRGB )._blend == RenderPassBlend::None );
    SW_EXPECT_TRUE( getRenderPassKindInfo( RenderPassKind::Present )._blend == RenderPassBlend::None );
}
