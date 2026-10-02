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

/**
 * @brief [PhysicsTest] 셀 번호가 int32 끝에 닿는 바디도 등록 · 질의가 끝난다
 * @details 셀 번호는 int32 끝으로 접히는데(아주 먼 좌표 · +inf), 셀 순회가 int32 로 돌아 `++` 가 넘치고 `<= INT32_MAX` 가 영원히
 *          참이었다 — 게임 스레드가 락을 쥔 채 멈추고 셀 표가 끝없이 자랐다. 이 케이스가 다시 멈추면 CTest 시간 초과로 드러난다.
 */
SW_TEST_CASE( PhysicsTest, BodyAtTheCellRangeLimitDoesNotHang )
{
    PhysicsWorld world;
    AABB         farBox;
    farBox._min = float3( 1.0e12f, 0.0f, 0.0f );
    farBox._max = float3( 1.0e12f, 0.5f, 0.5f );

    const PhysicsWorld::BodyHandle handle = world.addBody( farBox, 0 );
    SW_ASSERT_TRUE( handle.isValid() );

    vector<PhysicsWorld::BodyHandle> hits;
    world.queryAabb( farBox, 0, hits );
    bool bFound{ false };
    for ( const PhysicsWorld::BodyHandle& hit : hits )
        bFound = bFound || hit == handle;
    SW_EXPECT_TRUE( bFound );
    world.removeBody( handle );
}

namespace
{
    /** @brief (0..1)³ 를 @p shift 만큼 옮긴 상자입니다. */
    AABB makeUnitBox( float32 shift )
    {
        AABB box;
        box._min = float3( shift, 0.0f, 0.0f );
        box._max = float3( shift + 1.0f, 1.0f, 1.0f );
        return box;
    }

    /** @brief (x, y) 에서 시작하는 크기 1 상자입니다(z 0..1). */
    AABB makeBoxAt( float32 x, float32 y )
    {
        AABB box;
        box._min = float3( x, y, 0.0f );
        box._max = float3( x + 1.0f, y + 1.0f, 1.0f );
        return box;
    }

    /** @brief x 에서 두께 0.1 인 세로 벽입니다(y -5..15) — 한 step 에 10 을 가는 상자가 통째로 건너뛴다. */
    AABB makeWallBox( float32 x )
    {
        AABB box;
        box._min = float3( x, -5.0f, 0.0f );
        box._max = float3( x + 0.1f, 15.0f, 1.0f );
        return box;
    }

    /** @brief 이벤트 목록에서 두 오브젝트 쌍의 시작(true) · 끝(false) 이벤트 수를 셉니다(순서 무관). */
    uint32 countOverlapEvent( const vector<PhysicsOverlapEvent>& listEvent, uint64 objectA, uint64 objectB, bool bBegin )
    {
        uint32 count = 0;
        for ( const PhysicsOverlapEvent& event : listEvent )
        {
            const bool bPair = ( event._objectA == objectA && event._objectB == objectB ) || ( event._objectA == objectB && event._objectB == objectA );
            if ( bPair && ( event._bBegin == SW_TRUE ) == bBegin )
                ++count;
        }
        return count;
    }
} // namespace

/**
 * @brief [PhysicsTest] step 은 겹침을 다시 재고 지난 step 과 달라진 쌍을 시작 · 끝 이벤트로 낸다(유니티 OnTriggerEnter2D/Exit2D · 언리얼 Begin/EndOverlap)
 * @details 예전 `step` 은 빈 함수였고 부르는 곳도 없었다 — 겹침은 매번 물어야만 알 수 있었고 "들어왔다 · 나갔다" 를 알려 주는 곳이 없었다.
 *          바디가 사라지면 그 쌍은 끝난다(언리얼은 컴포넌트를 내릴 때 EndOverlap 을 낸다). 계속 겹친 쌍은 다시 내지 않는다.
 */
SW_TEST_CASE( PhysicsTest, StepReportsOverlapsThatBeginAndEnd )
{
    PhysicsWorld                   world;
    const PhysicsWorld::BodyHandle a = world.addBody( makeUnitBox( 0.0f ), 0, 11 );
    const PhysicsWorld::BodyHandle b = world.addBody( makeUnitBox( 0.5f ), 0, 22 );
    world.addBody( makeUnitBox( 50.0f ), 0, 33 );

    world.step( 0.016f );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( world.getOverlapEvents().size() ) );
    SW_EXPECT_EQUAL( 1u, countOverlapEvent( world.getOverlapEvents(), 11, 22, true ) );

    // 계속 겹친 쌍은 다시 내지 않는다.
    world.step( 0.016f );
    SW_EXPECT_TRUE( world.getOverlapEvents().empty() );

    // 떨어지면 끝, 다시 겹치면 시작.
    world.setAabb( b, makeUnitBox( 10.0f ) );
    world.step( 0.016f );
    SW_EXPECT_EQUAL( 1u, countOverlapEvent( world.getOverlapEvents(), 11, 22, false ) );
    world.setAabb( b, makeUnitBox( 0.5f ) );
    world.step( 0.016f );
    SW_EXPECT_EQUAL( 1u, countOverlapEvent( world.getOverlapEvents(), 11, 22, true ) );

    // 바디가 사라지면 그 쌍은 끝난다.
    world.removeBody( a );
    world.step( 0.016f );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( world.getOverlapEvents().size() ) );
    SW_EXPECT_EQUAL( 1u, countOverlapEvent( world.getOverlapEvents(), 11, 22, false ) );
}

/**
 * @brief [PhysicsTest] 레이어 행렬이 막은 쌍은 겹쳐도 이벤트를 내지 않는다
 */
SW_TEST_CASE( PhysicsTest, StepRespectsTheLayerMatrix )
{
    PhysicsWorld world;
    world.layers().setLayerCollision( 0, 1, false );
    world.addBody( makeUnitBox( 0.0f ), 0, 11 );
    world.addBody( makeUnitBox( 0.5f ), 1, 22 );
    world.step( 0.016f );
    SW_EXPECT_TRUE( world.getOverlapEvents().empty() );
}

/**
 * @brief [PhysicsTest] 연속 바디는 한 step 에 건너뛴 얇은 바디와도 겹친다 — 그 step 에 시작하고 다음 step 에 끝난다
 * @details 겹침 이벤트는 step 마다 끝 자리만 봤다. 한 프레임에 두께 0.1 벽보다 멀리 가는 총알은 벽과 한 번도 겹치지 않아 맞음 처리가 불리지
 *          않았다(터널링). 연속 바디는 지난 step 의 자리에서 지금 자리까지 쓸린다(`CCD::sweepAabb`). 같은 길을 간 이산 바디는 여전히 지나치고,
 *          레이어가 막은 벽은 쓸려도 닿지 않는다. 지나간 뒤 더 가도 다시 닿지 않는다 — 출발점은 지난 step 의 자리다(더한 자리가 아니다).
 */
SW_TEST_CASE( PhysicsTest, ContinuousBodyOverlapsWhatItPassedThroughInOneStep )
{
    PhysicsWorld world;
    world.layers().setLayerCollision( 0, 1, false );
    world.addBody( makeWallBox( 5.0f ), 0, 1 );
    world.addBody( makeWallBox( 7.0f ), 1, 2 ); // 레이어가 막은 벽
    const PhysicsWorld::BodyHandle fast = world.addBody( makeBoxAt( 0.0f, 0.0f ), 0, 3, true );
    const PhysicsWorld::BodyHandle slow = world.addBody( makeBoxAt( 0.0f, 10.0f ), 0, 4 );
    world.step( 0.016f );
    SW_EXPECT_TRUE( world.getOverlapEvents().empty() );

    // 한 step 에 x 0 → 10 — 두 바디 모두 벽 둘을 통째로 건너뛴다.
    world.setAabb( fast, makeBoxAt( 10.0f, 0.0f ) );
    world.setAabb( slow, makeBoxAt( 10.0f, 10.0f ) );
    world.step( 0.016f );
    SW_ASSERT_EQUAL( 1u, static_cast<uint32>( world.getOverlapEvents().size() ) );
    SW_EXPECT_EQUAL( 1u, countOverlapEvent( world.getOverlapEvents(), 3, 1, true ) );
    // 앞면에 닿은 때 — 중심 0.5 가 부푼 벽(4.5)에 닿을 때까지 10 가운데 4 를 갔다.
    SW_EXPECT_NEAR_EQUAL( 0.4f, world.getOverlapEvents()[0]._time, 1e-4f );

    // 이미 지나갔으니 다음 step 에 끝난다.
    world.step( 0.016f );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( world.getOverlapEvents().size() ) );
    SW_EXPECT_EQUAL( 1u, countOverlapEvent( world.getOverlapEvents(), 3, 1, false ) );

    // 더 가도 다시 닿지 않는다.
    world.setAabb( fast, makeBoxAt( 20.0f, 0.0f ) );
    world.step( 0.016f );
    SW_EXPECT_TRUE( world.getOverlapEvents().empty() );
}

/**
 * @brief [PhysicsTest] 겹쳐 있던 연속 바디가 떠나면 그 step 에 끝난다 — 출발점의 겹침(닿은 때 0)은 쓸림으로 치지 않는다
 * @details 쓸림 검사는 출발점이 이미 겹쳐 있으면 닿은 때 0 을 낸다. 그것까지 이번 step 의 겹침으로 치면 떠난 쌍이 한 step 더 겹친 채로 남아
 *          끝이 늦는다.
 */
SW_TEST_CASE( PhysicsTest, ContinuousBodyLeavingAnOverlapEndsItInThatStep )
{
    PhysicsWorld world;
    world.addBody( makeWallBox( 5.0f ), 0, 1 );
    const PhysicsWorld::BodyHandle fast = world.addBody( makeBoxAt( 4.6f, 0.0f ), 0, 3, true );
    world.step( 0.016f );
    SW_ASSERT_EQUAL( 1u, countOverlapEvent( world.getOverlapEvents(), 3, 1, true ) );

    world.setAabb( fast, makeBoxAt( 30.0f, 0.0f ) );
    world.step( 0.016f );
    SW_EXPECT_EQUAL( 1u, countOverlapEvent( world.getOverlapEvents(), 3, 1, false ) );
}

/**
 * @brief [PhysicsTest] 한 step 에 둘을 지나간 연속 바디의 겹침은 먼저 닿은 것부터 온다
 * @details 이벤트는 쌍(핸들) 순서로 나갔다. 총알이 한 step 에 적 둘을 지나가면 받는 쪽은 첫 이벤트에 반응해 사라지므로, 핸들이 앞선 **뒤의**
 *          적이 맞을 수 있었다. 이제 목록은 닿은 때(`_time`) 순서다. 먼 벽을 먼저 더해 핸들 순서와 거리 순서를 거꾸로 둔다.
 */
SW_TEST_CASE( PhysicsTest, SweptOverlapsComeInTheOrderTheyWereTouched )
{
    PhysicsWorld world;
    world.addBody( makeWallBox( 8.0f ), 0, 1 );
    world.addBody( makeWallBox( 4.0f ), 0, 2 );
    const PhysicsWorld::BodyHandle fast = world.addBody( makeBoxAt( 0.0f, 0.0f ), 0, 3, true );
    world.step( 0.016f );

    world.setAabb( fast, makeBoxAt( 10.0f, 0.0f ) );
    world.step( 0.016f );
    const vector<PhysicsOverlapEvent>& listEvent = world.getOverlapEvents();
    SW_ASSERT_EQUAL( 2u, static_cast<uint32>( listEvent.size() ) );
    const bool bNearWallFirst = listEvent[0]._objectA == 2ull || listEvent[0]._objectB == 2ull;
    SW_EXPECT_TRUE( bNearWallFirst );
    SW_EXPECT_TRUE( listEvent[0]._time < listEvent[1]._time );
}
