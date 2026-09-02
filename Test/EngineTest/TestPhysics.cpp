#include "pch.h"

#include "Engine/Physics/AABB.h"
#include "Engine/Physics/CollisionLayers.h"
#include "Engine/Physics/PhysicsWorld.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

SW_TEST_CASE( Physics, AabbIntersectsAndContains )
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

SW_TEST_CASE( Physics, CollisionLayersFilter )
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

SW_TEST_CASE( Physics, PhysicsWorldOverlapAndGeneration )
{
    PhysicsWorld world;
    AABB         boxA;
    boxA._min = float3( 0.0f, 0.0f, 0.0f );
    boxA._max = float3( 1.0f, 1.0f, 1.0f );
    AABB boxB = boxA;
    boxB._min = float3( 0.5f, 0.5f, 0.5f );
    boxB._max = float3( 2.0f, 2.0f, 2.0f );

    const PhysicsWorld::BodyHandle a = world.addBody( boxA, 0 );
    const PhysicsWorld::BodyHandle b = world.addBody( boxB, 1 );
    SW_EXPECT_TRUE( a.isValid() );
    SW_EXPECT_TRUE( world.overlaps( a, b ) );

    world.removeBody( a );
    PhysicsBody removed{};
    SW_EXPECT_FALSE( world.tryGetBody( a, removed ) );

    const PhysicsWorld::BodyHandle reused = world.addBody( boxA, 0 );
    SW_EXPECT_EQUAL( a.index(), reused.index() );
    SW_EXPECT_NOT_EQUAL( a, reused );
    SW_EXPECT_FALSE( world.tryGetBody( a, removed ) );
    SW_EXPECT_TRUE( world.overlaps( reused, b ) );

    vector<PhysicsWorld::BodyHandle> hits;
    world.queryAabb( boxA, 0, hits );
    SW_EXPECT_TRUE( hits.size() >= 1u );
}

SW_TEST_CASE( Physics, SpatialGridMultiCellQuery )
{
    PhysicsWorld world;
    AABB         nearBox;
    nearBox._min = float3( 10.0f, 10.0f, 10.0f );
    nearBox._max = float3( 20.0f, 20.0f, 20.0f );

    AABB farBox;
    farBox._min = float3( 1000.0f, 1000.0f, 1000.0f );
    farBox._max = float3( 1010.0f, 1010.0f, 1010.0f );

    auto hNear = world.addBody( nearBox, 0 );
    auto hFar  = world.addBody( farBox, 0 );
    (void)hFar;

    vector<PhysicsWorld::BodyHandle> hits;
    AABB                             queryBox;
    queryBox._min = float3( 5.0f, 5.0f, 5.0f );
    queryBox._max = float3( 15.0f, 15.0f, 15.0f );
    world.queryAabb( queryBox, 0, hits );

    SW_EXPECT_EQUAL( static_cast<size_t>( 1 ), hits.size() );
    if ( hits.empty() == false )
    {
        SW_EXPECT_EQUAL( hNear, hits[0] );
    }
}

SW_TEST_CASE( Physics, BodyAabbDynamicRelocation )
{
    PhysicsWorld world;
    AABB         initialBox;
    initialBox._min = float3( 0.0f, 0.0f, 0.0f );
    initialBox._max = float3( 10.0f, 10.0f, 10.0f );

    auto handle = world.addBody( initialBox, 0 );

    // 원래 위치 질의
    vector<PhysicsWorld::BodyHandle> hits;
    world.queryAabb( initialBox, 0, hits );
    SW_EXPECT_EQUAL( 1u, hits.size() );

    // AABB를 아주 먼 곳으로 동적 이동
    AABB movedBox;
    movedBox._min = float3( 500.0f, 500.0f, 500.0f );
    movedBox._max = float3( 510.0f, 510.0f, 510.0f );
    world.setAabb( handle, movedBox );

    // 이전 위치에서는 더 이상 검색되지 않아야 함
    hits.clear();
    world.queryAabb( initialBox, 0, hits );
    SW_EXPECT_EQUAL( 0u, hits.size() );

    // 새 위치에서는 정상 검색되어야 함
    hits.clear();
    world.queryAabb( movedBox, 0, hits );
    SW_EXPECT_EQUAL( 1u, hits.size() );
    if ( hits.empty() == false )
    {
        SW_EXPECT_EQUAL( handle, hits[0] );
    }
}

SW_TEST_CASE( Physics, SpatialGridMassiveBodiesStressTest )
{
    PhysicsWorld                         world;
    constexpr int32                      kGridDim = 10; // 10x10x10 = 1000개 바디
    sw::vector<PhysicsWorld::BodyHandle> handles;
    handles.reserve( kGridDim * kGridDim * kGridDim );

    for ( int32 gridX = 0; gridX < kGridDim; ++gridX )
    {
        for ( int32 gridY = 0; gridY < kGridDim; ++gridY )
        {
            for ( int32 gridZ = 0; gridZ < kGridDim; ++gridZ )
            {
                const float32 fx = static_cast<float32>( gridX ) * 100.0f;
                const float32 fy = static_cast<float32>( gridY ) * 100.0f;
                const float32 fz = static_cast<float32>( gridZ ) * 100.0f;
                AABB          box;
                box._min = float3( fx, fy, fz );
                box._max = float3( fx + 10.0f, fy + 10.0f, fz + 10.0f );
                handles.push_back( world.addBody( box, 0 ) );
            }
        }
    }

    // (0, 0, 0) ~ (150, 150, 150) 영역 질의 -> (0,0,0), (0,0,1), (0,1,0), (0,1,1), (1,0,0), (1,0,1), (1,1,0), (1,1,1) = 총 8개 바디
    AABB queryBox;
    queryBox._min = float3( -10.0f, -10.0f, -10.0f );
    queryBox._max = float3( 120.0f, 120.0f, 120.0f );

    vector<PhysicsWorld::BodyHandle> hits;
    world.queryAabb( queryBox, 0, hits );
    SW_EXPECT_EQUAL( 8u, hits.size() );
}

/**
 * @brief [Physics] CCD Swept AABB 초고속 투사체 벽 관통(Tunneling) 방지 검증
 */
SW_TEST_CASE( Physics, CCD_SweptAABBTunnelingPrevention )
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
    bool     bCollided = CCD::sweepAABB( bulletBox, bulletDisplacement, wallBox, hit );

    SW_EXPECT_TRUE( bCollided );
    SW_EXPECT_TRUE( hit._bHit );
    // 충돌 시각 t는 약 49.3 / 100 = 0.493
    SW_EXPECT_NEAR_EQUAL( 0.493f, hit._time, 0.01f );
    SW_EXPECT_NEAR_EQUAL( -1.0f, hit._hitNormal._z, 1e-3f );
}

/**
 * @brief [Physics] CCD Swept Sphere 검증
 */
SW_TEST_CASE( Physics, CCD_SweptSphere )
{
    AABB targetBox{
        float3{20.0f,  0.0f,  0.0f},
        float3{30.0f, 10.0f, 10.0f}
    };

    float3   startCenter{ 0.0f, 5.0f, 5.0f };
    float32  radius = 1.0f;
    float3   disp{ 40.0f, 0.0f, 0.0f };
    SweepHit hit{};

    bool bHit = CCD::sweepSphere( startCenter, radius, disp, targetBox, hit );
    SW_EXPECT_TRUE( bHit );
    SW_EXPECT_TRUE( hit._bHit );
    // 구 앞면이 targetBox minX(20.0)에 닿을 때 center = 19.0 -> t = 19.0 / 40.0 = 0.475
    SW_EXPECT_NEAR_EQUAL( 0.475f, hit._time, 0.01f );
    SW_EXPECT_NEAR_EQUAL( -1.0f, hit._hitNormal._x, 1e-3f );
}

/**
 * @brief [Physics] PhysicsWorld sweepTest 브로드페이즈 & 최단 충돌체 선별 검증
 */
SW_TEST_CASE( Physics, CCD_PhysicsWorldSweepTest )
{
    PhysicsWorld world;

    AABB nearObstacle{
        float3{-5.0f, -5.0f, 30.0f},
        float3{ 5.0f,  5.0f, 32.0f}
    };
    AABB farObstacle{
        float3{-5.0f, -5.0f, 70.0f},
        float3{ 5.0f,  5.0f, 72.0f}
    };

    auto hNear = world.addBody( nearObstacle, 0 );
    auto hFar  = world.addBody( farObstacle, 0 );
    (void)hFar;

    AABB projectile{
        float3{-0.5f, -0.5f, 0.0f},
        float3{ 0.5f,  0.5f, 1.0f}
    };
    float3 disp{ 0.0f, 0.0f, 100.0f };

    SweepHit hit{};
    bool     bHit = world.sweepTest( projectile, disp, 0, hit );

    SW_EXPECT_TRUE( bHit );
    SW_EXPECT_TRUE( hit._bHit );
    SW_EXPECT_EQUAL( hNear, hit._hitBody );
    // near obstacle에 먼저 닿음 (Z near ~ 30.0 -> t ~ 0.29)
    SW_EXPECT_NEAR_EQUAL( 0.29f, hit._time, 0.02f );
}

/**
 * @brief [Physics] CCD 모서리 스침(Corner Grazing) 및 평행 궤적 빗나감(Parallel Miss) 정밀 판별 검증
 */
SW_TEST_CASE( Physics, CCD_CornerGrazingAndParallelMiss )
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
    bool     bMiss = CCD::sweepAABB( missBox, missDisp, targetBox, missHit );
    SW_EXPECT_FALSE( bMiss );
    SW_EXPECT_FALSE( missHit._bHit );

    // 2) 대각선 코너를 관통하는 궤적
    AABB diagBox{
        float3{0.0f, 0.0f, 0.0f},
        float3{1.0f, 1.0f, 1.0f}
    };
    float3   diagDisp{ 30.0f, 30.0f, 30.0f };
    SweepHit diagHit{};
    bool     bDiagHit = CCD::sweepAABB( diagBox, diagDisp, targetBox, diagHit );
    SW_EXPECT_TRUE( bDiagHit );
    SW_EXPECT_TRUE( diagHit._bHit );
    // min corner (10, 10, 10)에 max (1, 1, 1)이 닿는 시각: (10 - 1) / 30 = 9 / 30 = 0.3
    SW_EXPECT_NEAR_EQUAL( 0.3f, diagHit._time, 0.01f );
}
