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

/**
 * @brief [RenderPassKindInfoTest] 패스 셰이더가 읽어도 되는 역할은 입력 계약(필수 · 선택)과 기록 칸이다 — 쿠커 · 런타임 PSO 검사가 이 값과 `g_<Role>Index` 해석 하나를 본다
 * @details 변이: 표의 기본 기록 칸(`_defaultHistoryRole`, TAA 의 GBufferAlbedo)을 빼면 TAA 셰이더가 계약 밖을 읽는다고 쿠킹이 멈춰 둘째 단언이 진다. 이름 해석이 앞뒤 꼴을 보지 않으면 마지막 두 단언이 진다.
 */
SW_TEST_CASE( RenderPassKindInfoTest, ReadableRolesComeFromTheContract )
{
    const auto hasRole = []( uint64 mask, RenderPassInputRole role )
    { return ( ( mask >> static_cast<uint32>( role ) ) & 1u ) != 0; };
    const uint64 taa = getRenderPassKindInfo( RenderPassKind::TAA ).getReadableRoleMask();
    SW_EXPECT_TRUE( hasRole( taa, RenderPassInputRole::SourceColor ) );
    SW_EXPECT_TRUE( hasRole( taa, RenderPassInputRole::GBufferAlbedo ) );
    SW_EXPECT_FALSE( hasRole( taa, RenderPassInputRole::Distortion ) );
    SW_EXPECT_EQUAL( RenderPassKindInfo::kAnyRoleMask, getRenderPassKindInfo( RenderPassKind::Shadow ).getReadableRoleMask() ); // 계약 없는 메시 패스는 보지 않는다

    RenderPassInputRole role = RenderPassInputRole::Invalid;
    SW_EXPECT_TRUE( tryParseRoleIndexVariable( "g_SceneDepthIndex", role ) );
    SW_EXPECT_TRUE( role == RenderPassInputRole::SceneDepth );
    SW_EXPECT_FALSE( tryParseRoleIndexVariable( "g_PlasmaIndex", role ) );
    SW_EXPECT_FALSE( tryParseRoleIndexVariable( "SceneDepthIndex", role ) );
}
