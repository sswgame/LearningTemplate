#include "pch.h"

#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"

#include "Engine/UI/World/WorldWidgetPick.h"

#include "TestFramework/TestFramework.h"

// WorldWidgetPickTest — 화면 점 → 광선 → World 위젯 사각형의 위젯 좌표(앞면 · 뒷면 · 밖 · 평행 · 뒤). 디바이스 없음(nogpu).

namespace
{
    struct WorldWidgetPickTestUtil
    {
        static constexpr float32 kWidth  = 1920.0f;
        static constexpr float32 kHeight = 1080.0f;

        /** @brief 원점에서 +z 를 보는 카메라(세로 시야 1 rad, 16:9)의 뷰 · 투영입니다. */
        static sw::float4x4 makeViewProjection()
        {
            const sw::float4x4 view = sw::float4x4::makeLookAt( sw::float3{ 0.0f, 0.0f, 0.0f }, sw::float3{ 0.0f, 0.0f, 1.0f }, sw::float3{ 0.0f, 1.0f, 0.0f } );
            return view * sw::float4x4::makePerspectiveFieldOfView( 1.0f, kWidth / kHeight, 0.1f, 100.0f );
        }

        /** @brief 폭 2 · 높이 1, 원점에서 +z 로 5 m 앞에 놓은 사각형(회전 없음)의 월드 행렬입니다. */
        static sw::float4x4 makeQuadWorld() { return sw::float4x4::makeScale( 2.0f, 1.0f, 1.0f ) * sw::float4x4::makeTranslation( 0.0f, 0.0f, 5.0f ); }

        static sw::WorldWidgetRay makeRay( const sw::float3& origin, const sw::float3& direction )
        {
            sw::WorldWidgetRay ray{};
            ray._origin    = origin;
            ray._direction = direction.normalize();
            return ray;
        }
    };
} // namespace

/** @brief [WorldWidgetPickTest] 사각형 가운데를 지나는 광선은 위젯 좌표 가운데이고, 오른쪽 위쪽 4분의 1 지점은 u 0.75 · v 0.25 다 — 앞면, 거리는 5 */
SW_TEST_CASE( WorldWidgetPickTest, FrontFaceMapsToWidgetPixels )
{
    using Util = WorldWidgetPickTestUtil;
    const sw::float2   textureSize{ 256.0f, 128.0f };
    sw::WorldWidgetHit hit{};
    SW_ASSERT_TRUE( sw::WorldWidgetPick::intersectQuad( Util::makeQuadWorld(), Util::makeRay( sw::float3{}, sw::float3{ 0.0f, 0.0f, 1.0f } ), textureSize, true, hit ) );
    SW_EXPECT_NEAR_EQUAL( 128.0f, hit._point._x, 1e-3f );
    SW_EXPECT_NEAR_EQUAL( 64.0f, hit._point._y, 1e-3f );
    SW_EXPECT_NEAR_EQUAL( 5.0f, hit._distance, 1e-4f );
    SW_EXPECT_TRUE( hit._bBackFace == SW_FALSE );
    SW_EXPECT_TRUE( hit._bInsideQuad == SW_TRUE );

    // 월드 (0.5, 0.25) = 로컬 (0.25, 0.25) → u 0.75, v 0.25.
    SW_ASSERT_TRUE( sw::WorldWidgetPick::intersectQuad( Util::makeQuadWorld(), Util::makeRay( sw::float3{ 0.5f, 0.25f, 0.0f }, sw::float3{ 0.0f, 0.0f, 1.0f } ), textureSize, true, hit ) );
    SW_EXPECT_NEAR_EQUAL( 192.0f, hit._point._x, 1e-3f );
    SW_EXPECT_NEAR_EQUAL( 32.0f, hit._point._y, 1e-3f );
}

/**
 * @brief [WorldWidgetPickTest] 반대편에서 보면 뒷면이고 u 가 뒤집힌다 — 같은 월드 점 (0.5, 0.25) 이 u 0.25 다
 * @details 변이: 뒷면에서 u 를 뒤집지 않으면(메시 UV 와 달라지면) 뒤에서 누른 단추가 좌우로 어긋나 진다.
 */
SW_TEST_CASE( WorldWidgetPickTest, BackFaceMirrorsU )
{
    using Util = WorldWidgetPickTestUtil;
    sw::WorldWidgetHit hit{};
    SW_ASSERT_TRUE( sw::WorldWidgetPick::intersectQuad( Util::makeQuadWorld(), Util::makeRay( sw::float3{ 0.5f, 0.25f, 10.0f }, sw::float3{ 0.0f, 0.0f, -1.0f } ),
                                                        sw::float2{ 256.0f, 128.0f }, true, hit ) );
    SW_EXPECT_TRUE( hit._bBackFace == SW_TRUE );
    SW_EXPECT_NEAR_EQUAL( 64.0f, hit._point._x, 1e-3f );
    SW_EXPECT_NEAR_EQUAL( 32.0f, hit._point._y, 1e-3f );
    SW_EXPECT_NEAR_EQUAL( 5.0f, hit._distance, 1e-4f );
}

/** @brief [WorldWidgetPickTest] 사각형 밖은 맞히지 않지만, 잡은 위젯의 끌기(`bRequireInside` 가 false)는 평면 점을 받는다. 평행 · 사각형이 뒤에 있는 광선 · 스케일 0 은 맞히지 않는다 */
SW_TEST_CASE( WorldWidgetPickTest, OutsideParallelAndBehindAreMisses )
{
    using Util = WorldWidgetPickTestUtil;
    const sw::float2         textureSize{ 256.0f, 128.0f };
    sw::WorldWidgetHit       hit{};
    const sw::WorldWidgetRay outside = Util::makeRay( sw::float3{ 1.5f, 0.0f, 0.0f }, sw::float3{ 0.0f, 0.0f, 1.0f } ); // 로컬 x 0.75 — 밖
    SW_EXPECT_FALSE( sw::WorldWidgetPick::intersectQuad( Util::makeQuadWorld(), outside, textureSize, true, hit ) );
    SW_ASSERT_TRUE( sw::WorldWidgetPick::intersectQuad( Util::makeQuadWorld(), outside, textureSize, false, hit ) );
    SW_EXPECT_TRUE( hit._bInsideQuad == SW_FALSE );
    SW_EXPECT_NEAR_EQUAL( 1.25f * 256.0f, hit._point._x, 1e-2f ); // u = 0.75 + 0.5

    SW_EXPECT_FALSE( sw::WorldWidgetPick::intersectQuad( Util::makeQuadWorld(), Util::makeRay( sw::float3{}, sw::float3{ 1.0f, 0.0f, 0.0f } ), textureSize, false, hit ) );
    SW_EXPECT_FALSE( sw::WorldWidgetPick::intersectQuad( Util::makeQuadWorld(), Util::makeRay( sw::float3{ 0.0f, 0.0f, 10.0f }, sw::float3{ 0.0f, 0.0f, 1.0f } ),
                                                         textureSize, false, hit ) ); // 사각형이 광선 뒤
    SW_EXPECT_FALSE( sw::WorldWidgetPick::intersectQuad( sw::float4x4::makeScale( 0.0f, 1.0f, 1.0f ), Util::makeRay( sw::float3{}, sw::float3{ 0.0f, 0.0f, 1.0f } ), textureSize, false, hit ) );
}

/** @brief [WorldWidgetPickTest] 화면 점으로 만든 광선은 그 점으로 투영되는 월드 점을 지난다 — 화면 가운데는 +z, (1, 0.5, 5) 를 투영한 점의 광선은 z = 5 에서 (1, 0.5) */
SW_TEST_CASE( WorldWidgetPickTest, RayThroughScreenPointPassesProjectedWorldPoint )
{
    using Util                        = WorldWidgetPickTestUtil;
    const sw::float4x4 viewProjection = Util::makeViewProjection();
    const sw::float4x4 inverse        = viewProjection.invert();
    const sw::float2   viewport{ Util::kWidth, Util::kHeight };

    sw::WorldWidgetRay center{};
    SW_ASSERT_TRUE( sw::WorldWidgetPick::makeRay( inverse, sw::float2{ Util::kWidth * 0.5f, Util::kHeight * 0.5f }, viewport, center ) );
    SW_EXPECT_NEAR_EQUAL( 1.0f, center._direction._z, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, center._direction._x, 1e-4f );

    const sw::float3   target{ 1.0f, 0.5f, 5.0f };
    const sw::float4   clip = sw::float4::transform( sw::float4{ target._x, target._y, target._z, 1.0f }, viewProjection );
    const sw::float2   screen{ ( clip._x / clip._w * 0.5f + 0.5f ) * Util::kWidth, ( 0.5f - clip._y / clip._w * 0.5f ) * Util::kHeight };
    sw::WorldWidgetRay ray{};
    SW_ASSERT_TRUE( sw::WorldWidgetPick::makeRay( inverse, screen, viewport, ray ) );
    const float32 t = ( target._z - ray._origin._z ) / ray._direction._z;
    SW_EXPECT_NEAR_EQUAL( target._x, ray._origin._x + ray._direction._x * t, 1e-3f );
    SW_EXPECT_NEAR_EQUAL( target._y, ray._origin._y + ray._direction._y * t, 1e-3f );

    SW_EXPECT_FALSE( sw::WorldWidgetPick::makeRay( inverse, screen, sw::float2{}, ray ) ); // 뷰포트가 비었다
}
