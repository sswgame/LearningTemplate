// 씬 뷰 배치의 표면 찾기 — 콜라이더 면 · 바닥 평면 · 카메라 뒤 거절 · 먼 표면 거절 · 격자 스냅(높이는 그대로).
#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Workspace/Asset/EditorPlacementPose.h"
#include "Editor/Viewport/Model/EditorAssetPlacement.h"

#include "Engine/Physics/Collision/PhysicsQuery.h"

namespace sw
{
    namespace
    {
        /** @brief 높이 2 m 의 무한한 단(위를 향한다) 하나입니다. 레이캐스트만 답한다. */
        class PlatformPhysicsScene final : public IPhysicsQueryScene3D
        {
        public:
            static constexpr float32 kHeight = 2.0f;

            bool raycast( const float3& origin, const float3& direction, float32 maxDistance, const PhysicsQueryFilter& /*filter*/, CastHit& outHit ) const override
            {
                const bool bAway = origin._y <= kHeight || direction._y >= 0.0f;
                if ( bAway )
                    return false;
                const float32 distance = ( origin._y - kHeight ) / -direction._y;
                if ( distance > maxDistance )
                    return false;
                outHit           = CastHit{};
                outHit._point    = float3{ origin._x + direction._x * distance, kHeight, origin._z + direction._z * distance };
                outHit._normal   = float3{ 0.0f, 1.0f, 0.0f };
                outHit._distance = distance;
                outHit._fraction = distance / maxDistance;
                return true;
            }
            bool   shapeCast( const ShapeDesc&, const float3&, const quaternion&, const float3&, float32, const PhysicsQueryFilter&, CastHit& ) const override { return false; }
            uint32 raycastAll( const float3&, const float3&, float32, const PhysicsQueryFilter&, vector<CastHit>& ) const override { return 0; }
            uint32 shapeCastAll( const ShapeDesc&, const float3&, const quaternion&, const float3&, float32, const PhysicsQueryFilter&, vector<CastHit>& ) const override
            {
                return 0;
            }
            uint32 overlapShape( const ShapeDesc&, const float3&, const quaternion&, const PhysicsQueryFilter&, vector<PhysicsBodyHandle>& ) const override { return 0; }
        };

        /** @brief 높이 10 m 에서 아래로 쏘는 레이입니다. */
        const Ray kDownRay{
            float3{1.3f, 10.0f, -2.6f},
            float3{0.0f, -1.0f,  0.0f}
        };
    } // namespace
} // namespace sw

/**
 * @brief [EditorAssetPlacementTest] 콜라이더가 있으면 그 면에 놓고(높이 2), 없으면 바닥 평면(높이 0)에 놓는다
 */
SW_TEST_CASE( EditorAssetPlacementTest, ColliderBeforeGroundPlane )
{
    const sw::PlatformPhysicsScene   physics;
    sw::editor::EditorPlacementQuery query;
    query._maxDistance = 100.0f;
    query._pPhysics    = &physics;
    sw::editor::EditorPlacementHit hit{};
    SW_REQUIRE_TRUE( sw::editor::EditorAssetPlacement::findSurface( query, sw::kDownRay, hit ) );
    SW_EXPECT_TRUE( hit._surface == sw::editor::EditorPlacementSurface::Collider );
    SW_EXPECT_NEAR( 2.0f, hit._position._y, 1e-5f );
    SW_EXPECT_NEAR( 8.0f, hit._distance, 1e-5f );

    query._pPhysics = nullptr;
    SW_REQUIRE_TRUE( sw::editor::EditorAssetPlacement::findSurface( query, sw::kDownRay, hit ) );
    SW_EXPECT_TRUE( hit._surface == sw::editor::EditorPlacementSurface::GroundPlane );
    SW_EXPECT_NEAR( 0.0f, hit._position._y, 1e-6f );
    SW_EXPECT_NEAR( 1.3f, hit._position._x, 1e-5f );
}

/**
 * @brief [EditorAssetPlacementTest] 표면이 없으면 놓지 않는다 — 카메라 뒤의 바닥 평면, 너무 먼 바닥, 평면과 평행한 레이. 원점으로 가지 않는다
 */
SW_TEST_CASE( EditorAssetPlacementTest, RefusesWithoutVisibleSurface )
{
    sw::editor::EditorPlacementQuery query;
    query._maxDistance = 100.0f;
    sw::editor::EditorPlacementHit hit{};
    const sw::Ray                  upRay{
        sw::float3{0.0f, 10.0f, 0.0f},
        sw::float3{0.0f,  1.0f, 0.0f}
    };
    SW_EXPECT_FALSE( sw::editor::EditorAssetPlacement::findSurface( query, upRay, hit ) );
    SW_EXPECT_TRUE( hit._surface == sw::editor::EditorPlacementSurface::None );

    const sw::Ray farRay{
        sw::float3{0.0f,       10.0f,      0.0f},
        sw::float3{0.0f, -0.0499376f, 0.998752f}
    }; // 바닥까지 200 m
    SW_EXPECT_FALSE( sw::editor::EditorAssetPlacement::findSurface( query, farRay, hit ) );

    const sw::Ray flatRay{
        sw::float3{0.0f, 10.0f, 0.0f},
        sw::float3{1.0f,  0.0f, 0.0f}
    };
    SW_EXPECT_FALSE( sw::editor::EditorAssetPlacement::findSurface( query, flatRay, hit ) );

    query._maxDistance = 0.0f; // 거리 상한이 없으면 찾지 않는다(설정이 비었다)
    SW_EXPECT_FALSE( sw::editor::EditorAssetPlacement::findSurface( query, sw::kDownRay, hit ) );
}

/**
 * @brief [EditorAssetPlacementTest] 격자 스냅은 바닥 두 축만 맞춘다 — 3D 는 X · Z, 2D 는 X · Y. 높이(표면)는 그대로다
 */
SW_TEST_CASE( EditorAssetPlacementTest, SnapKeepsSurfaceHeight )
{
    const sw::float3 snapped3D = sw::editor::EditorAssetPlacement::snapToGrid( sw::float3{ 1.3f, 2.37f, -2.6f }, 0.5f, false );
    SW_EXPECT_NEAR( 1.5f, snapped3D._x, 1e-6f );
    SW_EXPECT_NEAR( 2.37f, snapped3D._y, 1e-6f );
    SW_EXPECT_NEAR( -2.5f, snapped3D._z, 1e-6f );

    const sw::float3 snapped2D = sw::editor::EditorAssetPlacement::snapToGrid( sw::float3{ 1.3f, 2.37f, 0.0f }, 1.0f, true );
    SW_EXPECT_NEAR( 1.0f, snapped2D._x, 1e-6f );
    SW_EXPECT_NEAR( 2.0f, snapped2D._y, 1e-6f );

    const sw::float3 unsnapped = sw::editor::EditorAssetPlacement::snapToGrid( sw::float3{ 1.3f, 2.37f, -2.6f }, 0.0f, false );
    SW_EXPECT_NEAR( 1.3f, unsnapped._x, 1e-6f );

    sw::editor::EditorPlacementQuery query;
    query._maxDistance = 100.0f;
    query._snapStep    = 1.0f;
    sw::editor::EditorPlacementHit hit{};
    SW_REQUIRE_TRUE( sw::editor::EditorAssetPlacement::findSurface( query, sw::kDownRay, hit ) );
    SW_EXPECT_NEAR( 1.0f, hit._position._x, 1e-6f );
    SW_EXPECT_NEAR( -3.0f, hit._position._z, 1e-6f );
}

/**
 * @brief [EditorAssetPlacementTest] 2D 는 Z = 0 평면에 놓는다 — 콜라이더 사본은 보지 않는다
 */
SW_TEST_CASE( EditorAssetPlacementTest, TwoDimensionalUsesZPlane )
{
    const sw::PlatformPhysicsScene   physics;
    sw::editor::EditorPlacementQuery query;
    query._maxDistance = 100.0f;
    query._pPhysics    = &physics;
    query._b2DMode     = SW_TRUE;
    const sw::Ray ray{
        sw::float3{3.0f, 4.0f, -10.0f},
        sw::float3{0.0f, 0.0f,   1.0f}
    };
    sw::editor::EditorPlacementHit hit{};
    SW_REQUIRE_TRUE( sw::editor::EditorAssetPlacement::findSurface( query, ray, hit ) );
    SW_EXPECT_TRUE( hit._surface == sw::editor::EditorPlacementSurface::GroundPlane );
    SW_EXPECT_NEAR( 0.0f, hit._position._z, 1e-6f );
    SW_EXPECT_NEAR( 3.0f, hit._position._x, 1e-6f );
}

// 놓기 전 회전: 걸음의 배수로 돌고 0 ~ 2π 로 감는다(직각 걸음 네 번이면 처음 자세), 반대쪽은 2π 아래로 감는다. 자세는 3D 요 · 2D 롤에 더한다.
SW_TEST_CASE( EditorAssetPlacementTest, RotateStepsWrapAndMapToTheUpAxis )
{
    using sw::editor::EditorAssetPlacement;
    const sw::float32 step = sw::MathUtil::kHalfPi;
    sw::float32       yaw  = 0.0f;
    yaw                    = EditorAssetPlacement::stepYaw( yaw, step, 1 );
    SW_EXPECT_NEAR( step, yaw, 1e-5f );
    for ( sw::int32 index = 0; index < 3; ++index )
        yaw = EditorAssetPlacement::stepYaw( yaw, step, 1 );
    SW_EXPECT_NEAR( 0.0f, yaw, 1e-6f ); // 네 번 — 한 바퀴
    yaw = EditorAssetPlacement::stepYaw( 0.0f, step, -1 );
    SW_EXPECT_NEAR( 3.0f * step, yaw, 1e-5f );
    SW_EXPECT_NEAR( 0.7f, EditorAssetPlacement::stepYaw( 0.7f, 0.0f, 1 ), 1e-6f ); // 걸음이 없으면 그대로

    sw::editor::EditorPlacementPose pose{};
    pose._yaw                   = step;
    const sw::float3 rotation3D = pose.makeRotationOffset();
    SW_EXPECT_NEAR( step, rotation3D._y, 1e-6f );
    SW_EXPECT_NEAR( 0.0f, rotation3D._z, 1e-6f );
    pose._b2DMode               = true;
    const sw::float3 rotation2D = pose.makeRotationOffset();
    SW_EXPECT_NEAR( 0.0f, rotation2D._y, 1e-6f );
    SW_EXPECT_NEAR( step, rotation2D._z, 1e-6f );
}
