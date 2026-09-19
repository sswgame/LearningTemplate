#include "pch.h"

#include "Engine/Physics/AABB.h"
#include "Engine/Physics/CCD.h"
#include "Engine/Physics/CollisionLayers.h"
#include "Engine/Physics/PhysicsWorld.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

SW_TEST_CASE( PhysicsTest, AabbIntersectsAndContains )
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

SW_TEST_CASE( PhysicsTest, PhysicsWorldOverlapAndGeneration )
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

SW_TEST_CASE( PhysicsTest, SpatialGridMultiCellQuery )
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
        SW_EXPECT_EQUAL( hNear, hits[0] );
}

SW_TEST_CASE( PhysicsTest, BodyAabbDynamicRelocation )
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
        SW_EXPECT_EQUAL( handle, hits[0] );
}

SW_TEST_CASE( PhysicsTest, SpatialGridMassiveBodiesStressTest )
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
 * @brief [PhysicsTest] CCD Swept AABB 초고속 투사체 벽 관통(Tunneling) 방지 검증
 */
SW_TEST_CASE( PhysicsTest, CCD_SweptAABBTunnelingPrevention )
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
    bool     bCollided = CCD::sweepAabb( bulletBox, bulletDisplacement, wallBox, hit );

    SW_EXPECT_TRUE( bCollided );
    SW_EXPECT_TRUE( hit._bHit );
    // 충돌 시각 t는 약 49.3 / 100 = 0.493
    SW_EXPECT_NEAR_EQUAL( 0.493f, hit._time, 0.01f );
    SW_EXPECT_NEAR_EQUAL( -1.0f, hit._hitNormal._z, 1e-3f );
}

/**
 * @brief [PhysicsTest] CCD Swept Sphere 검증
 */
SW_TEST_CASE( PhysicsTest, CCD_SweptSphere )
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
 * @brief [PhysicsTest] PhysicsWorld sweepTest 브로드페이즈 & 최단 충돌체 선별 검증
 */
SW_TEST_CASE( PhysicsTest, CCD_PhysicsWorldSweepTest )
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
 * @brief [PhysicsTest] CCD 모서리 스침(Corner Grazing) 및 평행 궤적 빗나감(Parallel Miss) 정밀 판별 검증
 */
SW_TEST_CASE( PhysicsTest, CCD_CornerGrazingAndParallelMiss )
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
    bool     bMiss = CCD::sweepAabb( missBox, missDisp, targetBox, missHit );
    SW_EXPECT_FALSE( bMiss );
    SW_EXPECT_FALSE( missHit._bHit );

    // 2) 대각선 코너를 관통하는 궤적
    AABB diagBox{
        float3{0.0f, 0.0f, 0.0f},
        float3{1.0f, 1.0f, 1.0f}
    };
    float3   diagDisp{ 30.0f, 30.0f, 30.0f };
    SweepHit diagHit{};
    bool     bDiagHit = CCD::sweepAabb( diagBox, diagDisp, targetBox, diagHit );
    SW_EXPECT_TRUE( bDiagHit );
    SW_EXPECT_TRUE( diagHit._bHit );
    // min corner (10, 10, 10)에 max (1, 1, 1)이 닿는 시각: (10 - 1) / 30 = 9 / 30 = 0.3
    SW_EXPECT_NEAR_EQUAL( 0.3f, diagHit._time, 0.01f );
}

/**
 * @brief [PhysicsTest] 빗나간 스윕이 결과 구조체에 이전 충돌을 남기지 않는지 검증
 * @details 결과 구조체를 재사용해 여러 대상을 훑는 것은 자연스러운 쓰임이다. 그런데
 *          `sweepAabb` 만 시작할 때 결과를 비우지 않아서, 빗나가고도 **이전 호출의 `_bHit` 이
 *          그대로 남았다** — 형제 함수 `sweepSphere` 는 처음부터 비우고 있었다. 둘이 다른 약속을
 *          하고 있으면 어느 쪽 관례로 쓰는지가 호출자마다 달라진다.
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
    SW_EXPECT_TRUE( sw::CCD::sweepAabb( hittingBox, sw::float3{ 30.0f, 0.0f, 0.0f }, targetBox, hit ) );
    SW_EXPECT_TRUE( hit._bHit );

    // 2) **같은 구조체로** 완전히 빗나가는 스윕을 한다.
    const sw::AABB missingBox{
        sw::float3{-100.0f, -100.0f, -100.0f},
        sw::float3{ -99.0f,  -99.0f,  -99.0f}
    };
    SW_EXPECT_FALSE( sw::CCD::sweepAabb( missingBox, sw::float3{ 0.0f, -10.0f, 0.0f }, targetBox, hit ) );
    SW_EXPECT_FALSE( hit._bHit );
    SW_EXPECT_NEAR_EQUAL( 1.0f, hit._time, 1e-4f );

    // 3) 구 스윕도 같은 약속이다.
    SW_EXPECT_TRUE( sw::CCD::sweepSphere( sw::float3{ 0.0f, 10.5f, 10.5f }, 0.5f, sw::float3{ 30.0f, 0.0f, 0.0f }, targetBox, hit ) );
    SW_EXPECT_TRUE( hit._bHit );
    SW_EXPECT_FALSE( sw::CCD::sweepSphere( sw::float3{ -100.0f, -100.0f, -100.0f }, 0.5f, sw::float3{ 0.0f, -10.0f, 0.0f }, targetBox, hit ) );
    SW_EXPECT_FALSE( hit._bHit );
}

/**
 * @brief [PhysicsTest] 여러 셀에 걸친 바디는 **걸친 셀 어디서든** 찾아진다
 * @details 그리드 셀은 64 단위인데 이 스위트의 기존 바디는 전부 10~20 단위였다 — 즉 **한 셀 안에만**
 *          있었고, "AABB 가 여러 셀을 덮을 때" 의 범위 계산은 한 번도 태워지지 않았다.
 *
 *          삽입이 범위를 덜 훑으면 바디가 실제로 겹치는 셀에 등록되지 않고, 그 셀을 보는 질의가
 *          **바디를 못 찾는다** — 충돌을 놓치는 쪽이라 틀린 답이 조용히 나온다. 작은 질의 박스를
 *          쓰는 것이 중요하다: 넓은 박스는 셀 수가 바디 수를 넘어 **전수 검사 갈래**로 새기 때문에
 *          그리드를 아예 보지 않는다.
 *
 *          옮긴 뒤를 같이 보는 이유는 `setAabb` 의 "덮는 셀이 그대로면 그리드를 안 건드린다" 지름길이
 *          삽입과 **같은 범위 계산**을 써야 하기 때문이다. 어긋나면 새 셀에 등록되지 않은 채 넘어간다.
 */
SW_TEST_CASE( PhysicsTest, MultiCellBodyIsFoundInEveryCellItSpans )
{
    PhysicsWorld world;

    // 셀 크기는 64 — 0..200 은 축마다 셀 4개(0,1,2,3)라 합쳐서 64개 셀을 덮는다.
    AABB wideBox;
    wideBox._min = float3( 0.0f, 0.0f, 0.0f );
    wideBox._max = float3( 200.0f, 200.0f, 200.0f );

    const PhysicsWorld::BodyHandle handle = world.addBody( wideBox, 0 );

    /** @brief 한 셀 안에 들어가는 작은 질의 — 그리드 경로를 확실히 태웁니다. */
    const auto findAtPoint = [&world]( float32 x, float32 y, float32 z )
    {
        AABB probe;
        probe._min = float3( x - 1.0f, y - 1.0f, z - 1.0f );
        probe._max = float3( x + 1.0f, y + 1.0f, z + 1.0f );

        vector<PhysicsWorld::BodyHandle> listHit;
        world.queryAabb( probe, 0, listHit );
        return listHit.size();
    };

    BLOCK( "걸친 셀의 네 모서리 어디서든 찾아진다" )
    {
        SW_EXPECT_EQUAL( size_t( 1 ), findAtPoint( 10.0f, 10.0f, 10.0f ) );    // 첫 셀
        SW_EXPECT_EQUAL( size_t( 1 ), findAtPoint( 190.0f, 10.0f, 10.0f ) );   // x 끝 셀
        SW_EXPECT_EQUAL( size_t( 1 ), findAtPoint( 10.0f, 190.0f, 10.0f ) );   // y 끝 셀
        SW_EXPECT_EQUAL( size_t( 1 ), findAtPoint( 10.0f, 10.0f, 190.0f ) );   // z 끝 셀
        SW_EXPECT_EQUAL( size_t( 1 ), findAtPoint( 190.0f, 190.0f, 190.0f ) ); // 반대 모서리 셀
    }

    BLOCK( "멀리 옮기면 새 셀 전부에서 찾아지고 옛 셀에서는 안 찾아진다" )
    {
        AABB movedBox;
        movedBox._min = float3( 5000.0f, 5000.0f, 5000.0f );
        movedBox._max = float3( 5200.0f, 5200.0f, 5200.0f );
        world.setAabb( handle, movedBox );

        SW_EXPECT_EQUAL( size_t( 1 ), findAtPoint( 5010.0f, 5010.0f, 5010.0f ) );
        SW_EXPECT_EQUAL( size_t( 1 ), findAtPoint( 5190.0f, 5190.0f, 5190.0f ) );

        SW_EXPECT_EQUAL( size_t( 0 ), findAtPoint( 10.0f, 10.0f, 10.0f ) );
        SW_EXPECT_EQUAL( size_t( 0 ), findAtPoint( 190.0f, 190.0f, 190.0f ) );
    }
}

/**
 * @brief [PhysicsTest] 큰 바디 하나가 셀 표를 불리지 않는지, 그러면서도 여전히 찾아지는지 검증
 * @details 질의 쪽에는 셀 상한(`kMaxQueryCellCount`)이 있는데 **삽입 쪽에는 없었다.**
 *          `insertBodyToGrid` 는 AABB 가 덮는 모든 셀에 핸들을 적으므로, 20,000 유닛짜리 바닥
 *          콜라이더 하나면 64 유닛 셀 기준으로 셀 표에 **십만 칸 가까이** 생긴다 — 바디는
 *          하나인데. `setAabb` 로 움직이면 그만큼을 매번 지웠다 다시 적는다.
 *
 *          큰 바디는 그리드에 흩뿌리는 대신 목록 하나에 모으고, 그리드로 가는 질의가 그것을
 *          항상 함께 본다. 그래서 이 케이스는 **둘 다** 본다: 표가 작게 남는가, 그리고
 *          그럼에도 질의가 그 바디를 찾아내는가.
 */
SW_TEST_CASE( PhysicsTest, OversizedBodyDoesNotInflateTheGrid )
{
    PhysicsWorld world;
    world.layers().setLayerCollision( 0, 0, true );

    // 20,000 x 10 x 20,000 — 흔한 지형/바닥 콜라이더 크기다.
    AABB ground;
    ground._min = float3( -10000.0f, -5.0f, -10000.0f );
    ground._max = float3( 10000.0f, 5.0f, 10000.0f );

    const PhysicsWorld::BodyHandle groundHandle = world.addBody( ground, 0, 1 );
    SW_ASSERT_TRUE( groundHandle.isValid() );

    // 셀 하나에 들어가는 평범한 바디도 하나 둔다.
    AABB crate;
    crate._min                                 = float3( 0.0f, 0.0f, 0.0f );
    crate._max                                 = float3( 2.0f, 2.0f, 2.0f );
    const PhysicsWorld::BodyHandle crateHandle = world.addBody( crate, 0, 2 );
    SW_ASSERT_TRUE( crateHandle.isValid() );

    // 고치기 전에는 여기가 십만 가까이 됐다. 지금은 상자가 차지한 셀들뿐이다.
    SW_EXPECT_TRUE( world.getGridCellCount() < 16 );

    // 그런데도 바닥은 여전히 찾아져야 한다 — 셀에 없다는 것이 답을 바꾸면 안 된다.
    AABB probe;
    probe._min = float3( 0.5f, -1.0f, 0.5f );
    probe._max = float3( 1.5f, 1.0f, 1.5f );

    vector<PhysicsWorld::BodyHandle> listHit;
    world.queryAabb( probe, 0, listHit );

    bool bFoundGround = false;
    bool bFoundCrate  = false;
    for ( const PhysicsWorld::BodyHandle& handle : listHit )
    {
        if ( handle == groundHandle )
            bFoundGround = true;
        if ( handle == crateHandle )
            bFoundCrate = true;
    }
    SW_EXPECT_TRUE( bFoundGround );
    SW_EXPECT_TRUE( bFoundCrate );

    // 큰 바디를 지우면 목록에서도 빠져야 한다 — 넣을 때와 뺄 때의 판단이 같아야 성립한다.
    world.removeBody( groundHandle );
    world.queryAabb( probe, 0, listHit );
    for ( const PhysicsWorld::BodyHandle& handle : listHit )
    {
        SW_EXPECT_TRUE( handle != groundHandle );
    }
}

/**
 * @brief [PhysicsTest] 셀 번호 범위를 넘는 좌표의 바디도 질의에 잡힌다
 * @details 셀 번호는 `floor(좌표 / 셀크기)` 를 int32 로 캐스팅해 구했다. 그 캐스팅은 값이 int32
 *          범위를 벗어나면 **정의되지 않은 동작**이고, x86 에서는 넘치든 모자라든 똑같이 int32
 *          최솟값으로 붙는다 — 그래서 아주 넓은 AABB 의 최소와 최대가 **같은 셀 번호**가 되어
 *          폭이 1 로 읽혔다. "너무 크다"(`kMaxBodyCellCount`) 판정을 통과해 버리고, 그 바디는
 *          원점과 아무 상관 없는 셀 하나에만 등록된다 — 겹치는 자리를 보는 질의가 바디를 **못
 *          찾는다.** 터지지 않고 답만 조용히 틀리는 종류다.
 */
SW_TEST_CASE( PhysicsTest, BodyBeyondCellCoordinateRangeIsStillFound )
{
    PhysicsWorld world;
    world.layers().setLayerCollision( 0, 0, true );

    // 셀 크기(64)로 나눠도 int32 를 한참 넘는 좌표다.
    AABB enormous;
    enormous._min = float3( -1.0e12f, -1.0e12f, -1.0e12f );
    enormous._max = float3( 1.0e12f, 1.0e12f, 1.0e12f );

    const PhysicsWorld::BodyHandle enormousHandle = world.addBody( enormous, 0, 1 );
    SW_ASSERT_TRUE( enormousHandle.isValid() );

    // 셀 표에 흩뿌려지지 않아야 한다 — 넘치는 바디는 목록으로 간다.
    SW_EXPECT_EQUAL( size_t( 0 ), world.getGridCellCount() );

    AABB probe;
    probe._min = float3( -1.0f, -1.0f, -1.0f );
    probe._max = float3( 1.0f, 1.0f, 1.0f );

    vector<PhysicsWorld::BodyHandle> listHit;
    world.queryAabb( probe, 0, listHit );

    bool bFound = false;
    for ( const PhysicsWorld::BodyHandle& handle : listHit )
    {
        if ( handle == enormousHandle )
            bFound = true;
    }
    SW_EXPECT_TRUE( bFound );

    world.removeBody( enormousHandle );
    world.queryAabb( probe, 0, listHit );
    for ( const PhysicsWorld::BodyHandle& handle : listHit )
    {
        SW_EXPECT_TRUE( handle != enormousHandle );
    }
}
