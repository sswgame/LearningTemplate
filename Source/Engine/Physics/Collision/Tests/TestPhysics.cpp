#include "pch.h"

#include "Core/Math/AABB.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Physics/Collision/CollisionLayers.h"
#include "Engine/Physics/Collision/ContinuousCollision.h"

using namespace sw;

SW_TEST_CASE( PhysicsTest, AABBIntersectsAndContains )
{
    AABB box;
    box._min = float3( 0.0f, 0.0f, 0.0f );
    box._max = float3( 2.0f, 2.0f, 2.0f );
    SW_EXPECT_TRUE( box.isValid() );
    SW_EXPECT_TRUE( box.contains( float3( 1.0f, 1.0f, 1.0f ) ) );
    SW_EXPECT_FALSE( box.contains( float3( 3.0f, 1.0f, 1.0f ) ) );

    AABB other;
    other._min = float3( 1.5f, 1.5f, 1.5f );
    other._max = float3( 4.0f, 4.0f, 4.0f );
    SW_EXPECT_TRUE( box.intersects( other ) );

    AABB farBox;
    farBox._min = float3( 10.0f, 10.0f, 10.0f );
    farBox._max = float3( 11.0f, 11.0f, 11.0f );
    SW_EXPECT_FALSE( box.intersects( farBox ) );
}

/**
 * @brief [PhysicsTest] 빈 AABB(`AABB::empty`)의 `getExtents()` 는 무한대 차가 아니라 0 이다
 */
SW_TEST_CASE( PhysicsTest, EmptyAABBHasZeroExtents )
{
    const AABB emptyBox = AABB::empty();
    SW_EXPECT_FALSE( emptyBox.isValid() );

    const float3 extents = emptyBox.getExtents();
    SW_EXPECT_NEAR( 0.0f, extents._x, 1e-4f );
    SW_EXPECT_NEAR( 0.0f, extents._y, 1e-4f );
    SW_EXPECT_NEAR( 0.0f, extents._z, 1e-4f );
}

SW_TEST_CASE( PhysicsTest, CollisionLayersFilter )
{
    CollisionLayers layers;
    SW_EXPECT_TRUE( layers.shouldCollide( 0, 1 ) );

    layers.setLayerCollision( 0, 1, false );
    SW_EXPECT_FALSE( layers.shouldCollide( 0, 1 ) );
    SW_EXPECT_FALSE( layers.shouldCollide( 1, 0 ) );

    AABB a;
    a._min = float3( 0.0f, 0.0f, 0.0f );
    a._max = float3( 1.0f, 1.0f, 1.0f );
    AABB b = a;
    SW_EXPECT_FALSE( queryOverlaps( a, 0, b, 1, layers ) );

    layers.setLayerCollision( 0, 1, true );
    SW_EXPECT_TRUE( queryOverlaps( a, 0, b, 1, layers ) );
}

/**
 * @brief [PhysicsTest] ContinuousCollision Swept AABB 초고속 투사체 벽 관통(Tunneling) 방지 검증
 */
SW_TEST_CASE( PhysicsTest, ContinuousCollision_SweptAABBTunnelingPrevention )
{
    // 얇은 벽 (Z in [49.5, 50.5])
    AABB wallBox{
        float3{-10.0f, -10.0f, 49.5f},
        float3{ 10.0f,  10.0f, 50.5f}
    };

    // 초고속 총알 (한 프레임 이동 거리 100: Z from 0 to 100)
    AABB bulletBox{
        float3{-0.2f, -0.2f, -0.2f},
        float3{ 0.2f,  0.2f,  0.2f}
    };
    float3 bulletDisplacement{ 0.0f, 0.0f, 100.0f };

    SweepHit hit{};
    bool     bCollided = ContinuousCollision::sweepAABB( bulletBox, bulletDisplacement, wallBox, hit );

    SW_EXPECT_TRUE( bCollided );
    SW_EXPECT_TRUE( hit._bHit );
    // 충돌 시각 t는 약 49.3 / 100 = 0.493
    SW_EXPECT_NEAR( 0.493f, hit._hitFraction, 0.01f );
    SW_EXPECT_NEAR( -1.0f, hit._hitNormal._z, 1e-3f );
}

/**
 * @brief [PhysicsTest] ContinuousCollision Swept Sphere 검증
 */
SW_TEST_CASE( PhysicsTest, ContinuousCollision_SweptSphere )
{
    AABB targetBox{
        float3{20.0f,  0.0f,  0.0f},
        float3{30.0f, 10.0f, 10.0f}
    };

    float3   startCenter{ 0.0f, 5.0f, 5.0f };
    float32  radius = 1.0f;
    float3   disp{ 40.0f, 0.0f, 0.0f };
    SweepHit hit{};

    bool bHit = ContinuousCollision::sweepSphere( startCenter, radius, disp, targetBox, hit );
    SW_EXPECT_TRUE( bHit );
    SW_EXPECT_TRUE( hit._bHit );
    // 구 앞면이 targetBox minX(20.0)에 닿을 때 center = 19.0 -> t = 19.0 / 40.0 = 0.475
    SW_EXPECT_NEAR( 0.475f, hit._hitFraction, 0.01f );
    SW_EXPECT_NEAR( -1.0f, hit._hitNormal._x, 1e-3f );
}

/**
 * @brief [PhysicsTest] ContinuousCollision 모서리 스침(Corner Grazing) 및 평행 궤적 빗나감(Parallel Miss) 정밀 판별 검증
 */
SW_TEST_CASE( PhysicsTest, ContinuousCollision_CornerGrazingAndParallelMiss )
{
    AABB targetBox{
        float3{10.0f, 10.0f, 10.0f},
        float3{20.0f, 20.0f, 20.0f}
    };

    // 1) 평행하게 완전히 빗겨나가는 궤적 (X in [0, 5], target X in [10, 20])
    AABB missBox{
        float3{0.0f, 0.0f, 0.0f},
        float3{2.0f, 2.0f, 2.0f}
    };
    float3   missDisp{ 0.0f, 0.0f, 50.0f };
    SweepHit missHit{};
    bool     bMiss = ContinuousCollision::sweepAABB( missBox, missDisp, targetBox, missHit );
    SW_EXPECT_FALSE( bMiss );
    SW_EXPECT_FALSE( missHit._bHit );

    // 2) 대각선 코너를 관통하는 궤적
    AABB diagBox{
        float3{0.0f, 0.0f, 0.0f},
        float3{1.0f, 1.0f, 1.0f}
    };
    float3   diagDisp{ 30.0f, 30.0f, 30.0f };
    SweepHit diagHit{};
    bool     bDiagHit = ContinuousCollision::sweepAABB( diagBox, diagDisp, targetBox, diagHit );
    SW_EXPECT_TRUE( bDiagHit );
    SW_EXPECT_TRUE( diagHit._bHit );
    // min corner (10, 10, 10)에 max (1, 1, 1)이 닿는 시각: (10 - 1) / 30 = 9 / 30 = 0.3
    SW_EXPECT_NEAR( 0.3f, diagHit._hitFraction, 0.01f );
}

/**
 * @brief [PhysicsTest] 빗나간 스윕이 결과 구조체에 이전 충돌을 남기지 않는지 검증
 * @details 결과 구조체를 재사용해 여러 대상을 훑는 것은 자연스러운 쓰임이다. `sweepAABB` 가 시작할 때 결과를
 *          비우지 않으면 빗나가고도 **이전 호출의 `_bHit` 이 그대로 남는다** — 형제 함수 `sweepSphere` 와 같은
 *          약속이어야 호출자마다 관례가 갈리지 않는다.
 */
SW_TEST_CASE( PhysicsTest, MissedSweepLeavesNoStaleHit )
{
    const sw::AABB targetBox{
        sw::float3{10.0f, 10.0f, 10.0f},
        sw::float3{11.0f, 11.0f, 11.0f}
    };
    const sw::AABB hittingBox{
        sw::float3{0.0f, 10.0f, 10.0f},
        sw::float3{1.0f, 11.0f, 11.0f}
    };

    sw::SweepHit hit{};

    // 1) 먼저 맞힌다 — 결과가 채워진다.
    SW_EXPECT_TRUE( sw::ContinuousCollision::sweepAABB( hittingBox, sw::float3{ 30.0f, 0.0f, 0.0f }, targetBox, hit ) );
    SW_EXPECT_TRUE( hit._bHit );

    // 2) **같은 구조체로** 완전히 빗나가는 스윕을 한다.
    const sw::AABB missingBox{
        sw::float3{-100.0f, -100.0f, -100.0f},
        sw::float3{ -99.0f,  -99.0f,  -99.0f}
    };
    SW_EXPECT_FALSE( sw::ContinuousCollision::sweepAABB( missingBox, sw::float3{ 0.0f, -10.0f, 0.0f }, targetBox, hit ) );
    SW_EXPECT_FALSE( hit._bHit );
    SW_EXPECT_NEAR( 1.0f, hit._hitFraction, 1e-4f );

    // 3) 구 스윕도 같은 약속이다.
    SW_EXPECT_TRUE( sw::ContinuousCollision::sweepSphere( sw::float3{ 0.0f, 10.5f, 10.5f }, 0.5f, sw::float3{ 30.0f, 0.0f, 0.0f }, targetBox, hit ) );
    SW_EXPECT_TRUE( hit._bHit );
    SW_EXPECT_FALSE( sw::ContinuousCollision::sweepSphere( sw::float3{ -100.0f, -100.0f, -100.0f }, 0.5f, sw::float3{ 0.0f, -10.0f, 0.0f }, targetBox, hit ) );
    SW_EXPECT_FALSE( hit._bHit );
}
