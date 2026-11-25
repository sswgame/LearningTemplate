#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "GameFramework/Kits/Genre/Action/ActionPlatformer/ActionPlatformerRespawnWorld.h"
#include "GameFramework/Runtime/Character/Movement/PlatformerMotor2D.h"

// ActionPlatformerRespawnWorldTest — 칸 지형의 설 자리: 땅 위면 그 자리, 낭떠러지면 반경 안 가장 가까운 칸, 위험 칸 위 · 반경 밖은 없음. 키트 GF_ActionPlatformer(nogpu).

namespace
{
    constexpr float32 kWorldTolerance = 1.0e-4f;

    /** @brief 바닥(y 0 줄)이 x 0..3 · 6..9 이고 x 4..5 가 구덩이, x 9 위에 위험 칸이 있는 지형입니다(칸 1 m). */
    constexpr const utf8* kPitTerrain = "..........\n"
                                        "..........\n"
                                        ".........^\n"
                                        "####..####\n";

    const sw::float2 kHalfExtents{ 0.35f, 0.45f };
} // namespace

/**
 * @brief [ActionPlatformerRespawnWorldTest] 땅 위에 선 몸은 그 자리 그대로이고, 구덩이 바닥 아래에서는 반경 안 가장 가까운 칸 가운데(발이 바닥 윗면)에 선다
 */
SW_TEST_CASE( ActionPlatformerRespawnWorldTest, StandingStaysAndPitMovesToTheNearestEdge )
{
    sw::PlatformTileMap map;
    SW_REQUIRE_TRUE( map.loadFromXMLText( kPitTerrain, 1.0f, sw::float2{ 0.0f, 0.0f } ) );
    const sw::ActionPlatformerRespawnWorld world( map, kHalfExtents );

    const sw::optional<sw::float3> standing = world.findSafeLocation( sw::float3{ 1.3f, 1.45f, 0.0f }, 5.0f );
    SW_REQUIRE_TRUE( standing.has_value() );
    SW_EXPECT_NEAR( 1.3f, standing->_x, kWorldTolerance );
    SW_EXPECT_NEAR( 1.45f, standing->_y, kWorldTolerance );

    // 구덩이 왼쪽 벽에 붙어 떨어졌다(x 4.4) — 왼쪽 땅 끝 칸(x 3 → 가운데 3.5)이 오른쪽(6.5)보다 가깝다
    const sw::optional<sw::float3> fromPit = world.findSafeLocation( sw::float3{ 4.4f, -3.0f, 0.0f }, 5.0f );
    SW_REQUIRE_TRUE( fromPit.has_value() );
    SW_EXPECT_NEAR( 3.5f, fromPit->_x, kWorldTolerance );
    SW_EXPECT_NEAR( 1.45f, fromPit->_y, kWorldTolerance );

    // 공중(바닥에서 떠 있음)도 그 자리가 아니라 바로 아래 줄의 설 칸이다
    const sw::optional<sw::float3> inAir = world.findSafeLocation( sw::float3{ 7.2f, 3.0f, 0.0f }, 5.0f );
    SW_REQUIRE_TRUE( inAir.has_value() );
    SW_EXPECT_NEAR( 7.5f, inAir->_x, kWorldTolerance );
    SW_EXPECT_NEAR( 1.45f, inAir->_y, kWorldTolerance );
    SW_EXPECT_TRUE( world.isLocationReady( sw::float3{} ) );
}

/**
 * @brief [ActionPlatformerRespawnWorldTest] 위험 칸이 있는 칸에는 세우지 않고, 반경 안에 설 칸이 없으면 없음이다(죽은 자리에 세우지 않는다)
 */
SW_TEST_CASE( ActionPlatformerRespawnWorldTest, HazardAndOutOfRadiusGiveNoLocation )
{
    sw::PlatformTileMap map;
    SW_REQUIRE_TRUE( map.loadFromXMLText( kPitTerrain, 1.0f, sw::float2{ 0.0f, 0.0f } ) );
    const sw::ActionPlatformerRespawnWorld world( map, kHalfExtents );

    // x 9 칸은 위험 칸 아래라 설 칸이 아니다 — 가장 가까운 것은 x 8
    const sw::optional<sw::float3> nearHazard = world.findSafeLocation( sw::float3{ 9.5f, 1.45f, 0.0f }, 3.0f );
    SW_REQUIRE_TRUE( nearHazard.has_value() );
    SW_EXPECT_NEAR( 8.5f, nearHazard->_x, kWorldTolerance );

    SW_EXPECT_FALSE( world.findSafeLocation( sw::float3{ 5.0f, -20.0f, 0.0f }, 5.0f ).has_value() );
    SW_EXPECT_FALSE( world.findSafeLocation( sw::float3{ 4.4f, -3.0f, 0.0f }, 1.0f ).has_value() );
}
