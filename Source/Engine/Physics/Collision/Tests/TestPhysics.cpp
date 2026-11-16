#include "pch.h"

#include "Core/Math/AABB.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Physics/Collision/CollisionLayers.h"
#include "Engine/Physics/Collision/PhysicsContact.h"
#include "Engine/Physics/Collision/SweepHit.h"

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
 * @brief [PhysicsTest] SweepHit Swept AABB 초고속 투사체 벽 관통(Tunneling) 방지 검증
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
    bool     bCollided = SweepHit::sweepAABB( bulletBox, bulletDisplacement, wallBox, hit );

    SW_EXPECT_TRUE( bCollided );
    SW_EXPECT_TRUE( hit._bHit );
    // 충돌 시각 t는 약 49.3 / 100 = 0.493
    SW_EXPECT_NEAR( 0.493f, hit._hitFraction, 0.01f );
    SW_EXPECT_NEAR( -1.0f, hit._hitNormal._z, 1e-3f );
}

/**
 * @brief [PhysicsTest] SweepHit Swept Sphere 검증
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

    bool bHit = SweepHit::sweepSphere( startCenter, radius, disp, targetBox, hit );
    SW_EXPECT_TRUE( bHit );
    SW_EXPECT_TRUE( hit._bHit );
    // 구 앞면이 targetBox minX(20.0)에 닿을 때 center = 19.0 -> t = 19.0 / 40.0 = 0.475
    SW_EXPECT_NEAR( 0.475f, hit._hitFraction, 0.01f );
    SW_EXPECT_NEAR( -1.0f, hit._hitNormal._x, 1e-3f );
}

/**
 * @brief [PhysicsTest] SweepHit 모서리 스침(Corner Grazing) 및 평행 궤적 빗나감(Parallel Miss) 정밀 판별 검증
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
    bool     bMiss = SweepHit::sweepAABB( missBox, missDisp, targetBox, missHit );
    SW_EXPECT_FALSE( bMiss );
    SW_EXPECT_FALSE( missHit._bHit );

    // 2) 대각선 코너를 관통하는 궤적
    AABB diagBox{
        float3{0.0f, 0.0f, 0.0f},
        float3{1.0f, 1.0f, 1.0f}
    };
    float3   diagDisp{ 30.0f, 30.0f, 30.0f };
    SweepHit diagHit{};
    bool     bDiagHit = SweepHit::sweepAABB( diagBox, diagDisp, targetBox, diagHit );
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
    SW_EXPECT_TRUE( sw::SweepHit::sweepAABB( hittingBox, sw::float3{ 30.0f, 0.0f, 0.0f }, targetBox, hit ) );
    SW_EXPECT_TRUE( hit._bHit );

    // 2) **같은 구조체로** 완전히 빗나가는 스윕을 한다.
    const sw::AABB missingBox{
        sw::float3{-100.0f, -100.0f, -100.0f},
        sw::float3{ -99.0f,  -99.0f,  -99.0f}
    };
    SW_EXPECT_FALSE( sw::SweepHit::sweepAABB( missingBox, sw::float3{ 0.0f, -10.0f, 0.0f }, targetBox, hit ) );
    SW_EXPECT_FALSE( hit._bHit );
    SW_EXPECT_NEAR( 1.0f, hit._hitFraction, 1e-4f );

    // 3) 구 스윕도 같은 약속이다.
    SW_EXPECT_TRUE( sw::SweepHit::sweepSphere( sw::float3{ 0.0f, 10.5f, 10.5f }, 0.5f, sw::float3{ 30.0f, 0.0f, 0.0f }, targetBox, hit ) );
    SW_EXPECT_TRUE( hit._bHit );
    SW_EXPECT_FALSE( sw::SweepHit::sweepSphere( sw::float3{ -100.0f, -100.0f, -100.0f }, 0.5f, sw::float3{ 0.0f, -10.0f, 0.0f }, targetBox, hit ) );
    SW_EXPECT_FALSE( hit._bHit );
}

/**
 * @brief [PhysicsContactTrackerTest] 지운 바디가 같은 step 간격 안에서 다시 닿으면 끝과 시작을 함께 내고 쌍을 잊지 않는다(감사 S-13 — 예전에는 끝만 내고 잊어 다음 step 부터 접촉이 사라졌다)
 */
SW_TEST_CASE( PhysicsContactTrackerTest, ReenabledBodyGetsEndThenBegin )
{
    PhysicsContactTracker<PhysicsDimension3D> tracker;
    const PhysicsContactBody                  bodyA{ PhysicsBodyHandle::fromPacked( 0x100000001ull ), 1 };
    const PhysicsContactBody                  bodyB{ PhysicsBodyHandle::fromPacked( 0x100000002ull ), 2 };
    vector<PhysicsContactEvent3D>             listEvent;
    tracker.setStayListener( bodyA._userData, true ); // 끝의 유지까지 본다(유지는 opt-in — 감사 S-1)
    tracker.beginSubContact( bodyA, bodyB, {}, {}, 0.0f );
    tracker.finishStep( listEvent );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listEvent.size() );
    SW_EXPECT_TRUE( listEvent[0]._phase == PhysicsContactPhase::Begin );

    const PhysicsBodyHandle removed[] = { bodyA._body };
    tracker.removeBodies( span<const PhysicsBodyHandle>( removed ) );
    tracker.beginSubContact( bodyA, bodyB, {}, {}, 0.0f ); // 같은 핸들이 다시 켜져 닿았다
    listEvent.clear();
    tracker.finishStep( listEvent );
    SW_REQUIRE_EQUAL( size_t{ 2 }, listEvent.size() );
    SW_EXPECT_TRUE( listEvent[0]._phase == PhysicsContactPhase::End );
    SW_EXPECT_TRUE( listEvent[1]._phase == PhysicsContactPhase::Begin );
    SW_EXPECT_EQUAL( 1u, tracker.getActivePairCount() );

    listEvent.clear();
    tracker.finishStep( listEvent ); // 다음 step 은 이어서 닿아 있다
    SW_REQUIRE_EQUAL( size_t{ 1 }, listEvent.size() );
    SW_EXPECT_TRUE( listEvent[0]._phase == PhysicsContactPhase::Stay );
}

/**
 * @brief [PhysicsContactTrackerTest] 셰이프를 간 바디(`resetSubContacts`)는 새 셰이프가 다시 닿으면 쌍이 이어지고(끝 · 시작 없음), 닿지 않으면 끝난다(감사 S-4)
 * @details 옛 셰이프 쌍 둘이 닿아 있다가 셰이프를 간다 — 백엔드는 옛 셰이프의 끝 알림을 버리고 새 셰이프의 시작만 넣는다.
 */
SW_TEST_CASE( PhysicsContactTrackerTest, ReshapedBodyKeepsTouchingPair )
{
    PhysicsContactTracker<PhysicsDimension2D> tracker;
    const PhysicsContactBody                  bodyA{ PhysicsBodyHandle::fromPacked( 0x100000001ull ), 1 };
    const PhysicsContactBody                  bodyB{ PhysicsBodyHandle::fromPacked( 0x100000002ull ), 2 };
    vector<PhysicsContactEvent2D>             listEvent;
    tracker.beginSubContact( bodyA, bodyB, {}, {}, 0.0f );
    tracker.beginSubContact( bodyA, bodyB, {}, {}, 0.0f ); // 셰이프 쌍 둘
    tracker.finishStep( listEvent );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listEvent.size() );

    tracker.resetSubContacts( bodyB._body );
    tracker.beginSubContact( bodyB, bodyA, {}, {}, 0.0f ); // 새 셰이프 하나가 다시 닿았다
    listEvent.clear();
    tracker.finishStep( listEvent );
    SW_EXPECT_EQUAL( size_t{ 0 }, listEvent.size() ); // 유지는 듣지 않으니 아무것도 없다
    SW_EXPECT_EQUAL( 1u, tracker.getActivePairCount() );

    tracker.endSubContact( bodyA._body, bodyB._body ); // 새 셰이프 쌍의 끝 하나로 끝난다(옛 쌍 수가 남지 않았다)
    listEvent.clear();
    tracker.finishStep( listEvent );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listEvent.size() );
    SW_EXPECT_TRUE( listEvent[0]._phase == PhysicsContactPhase::End );

    tracker.beginSubContact( bodyA, bodyB, {}, {}, 0.0f );
    listEvent.clear();
    tracker.finishStep( listEvent );
    tracker.resetSubContacts( bodyA._body ); // 셰이프를 갈았는데 새 셰이프가 닿지 않는다
    listEvent.clear();
    tracker.finishStep( listEvent );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listEvent.size() );
    SW_EXPECT_TRUE( listEvent[0]._phase == PhysicsContactPhase::End );
    SW_EXPECT_EQUAL( 0u, tracker.getActivePairCount() );
}

/**
 * @brief [PhysicsContactTrackerTest] 잠들어 쉰 셰이프 쌍(`suspendSubContact`)은 끝을 내지 않고, 깨어 다시 닿으면 이어지며, 깨었는데 닿지 않으면 그때 끝난다
 */
SW_TEST_CASE( PhysicsContactTrackerTest, SuspendedPairWaitsForWake )
{
    PhysicsContactTracker<PhysicsDimension3D> tracker;
    const PhysicsContactBody                  bodyA{ PhysicsBodyHandle::fromPacked( 0x100000001ull ), 1 };
    const PhysicsContactBody                  bodyB{ PhysicsBodyHandle::fromPacked( 0x100000002ull ), 2 };
    vector<PhysicsContactEvent3D>             listEvent;
    tracker.setStayListener( bodyA._userData, true );
    tracker.beginSubContact( bodyA, bodyB, {}, {}, 0.0f );
    tracker.finishStep( listEvent );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listEvent.size() );

    bool       bAsleep  = true;
    const auto isAsleep = [&bAsleep]( PhysicsBodyHandle, PhysicsBodyHandle )
    { return bAsleep; };
    tracker.suspendSubContact( bodyA._body, bodyB._body ); // 잠들었다
    tracker.resumeSubContacts( isAsleep );
    listEvent.clear();
    tracker.finishStep( listEvent );
    SW_EXPECT_EQUAL( size_t{ 0 }, listEvent.size() ); // 끝도 유지도 없다
    SW_EXPECT_EQUAL( 1u, tracker.getActivePairCount() );

    bAsleep = false; // 깨어 다시 닿았다
    tracker.beginSubContact( bodyA, bodyB, {}, {}, 0.0f );
    tracker.resumeSubContacts( isAsleep );
    listEvent.clear();
    tracker.finishStep( listEvent );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listEvent.size() );
    SW_EXPECT_TRUE( listEvent[0]._phase == PhysicsContactPhase::Stay );

    bAsleep = true; // 다시 잠들었다가
    tracker.suspendSubContact( bodyA._body, bodyB._body );
    tracker.resumeSubContacts( isAsleep );
    listEvent.clear();
    tracker.finishStep( listEvent );
    bAsleep = false; // 깨었는데 닿지 않는다
    tracker.resumeSubContacts( isAsleep );
    listEvent.clear();
    tracker.finishStep( listEvent );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listEvent.size() );
    SW_EXPECT_TRUE( listEvent[0]._phase == PhysicsContactPhase::End );
    SW_EXPECT_EQUAL( 0u, tracker.getActivePairCount() );
}

/**
 * @brief [PhysicsContactTrackerTest] 유지는 듣는 사용자 값이 낀 쌍만 낸다 — 이미 닿은 쌍도 듣기를 켜고 끄면 다음 step 부터 따른다(감사 S-1)
 * @details A-B(A 가 듣는다)와 C-D(아무도 듣지 않는다)가 함께 닿아 있다. 시작 · 끝은 듣기와 상관없이 늘 나온다.
 */
SW_TEST_CASE( PhysicsContactTrackerTest, StayOnlyForListeners )
{
    PhysicsContactTracker<PhysicsDimension3D> tracker;
    const PhysicsContactBody                  bodyA{ PhysicsBodyHandle::fromPacked( 0x100000001ull ), 1 };
    const PhysicsContactBody                  bodyB{ PhysicsBodyHandle::fromPacked( 0x100000002ull ), 2 };
    const PhysicsContactBody                  bodyC{ PhysicsBodyHandle::fromPacked( 0x100000003ull ), 3 };
    const PhysicsContactBody                  bodyD{ PhysicsBodyHandle::fromPacked( 0x100000004ull ), 4 };
    vector<PhysicsContactEvent3D>             listEvent;
    tracker.setStayListener( bodyA._userData, true );
    tracker.beginSubContact( bodyA, bodyB, {}, {}, 0.0f );
    tracker.beginSubContact( bodyC, bodyD, {}, {}, 0.0f );
    tracker.finishStep( listEvent );
    SW_REQUIRE_EQUAL( size_t{ 2 }, listEvent.size() ); // 시작은 둘 다
    SW_EXPECT_TRUE( tracker.isStayReported( bodyB._body, bodyA._body ) );
    SW_EXPECT_FALSE( tracker.isStayReported( bodyC._body, bodyD._body ) );

    listEvent.clear();
    tracker.finishStep( listEvent );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listEvent.size() );
    SW_EXPECT_TRUE( listEvent[0]._phase == PhysicsContactPhase::Stay );
    SW_EXPECT_TRUE( listEvent[0]._bodyA == bodyA._body );

    tracker.setStayListener( bodyD._userData, true ); // 닿아 있는 C-D 가 듣기 시작한다
    tracker.setStayListener( bodyA._userData, false );
    listEvent.clear();
    tracker.finishStep( listEvent );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listEvent.size() );
    SW_EXPECT_TRUE( listEvent[0]._phase == PhysicsContactPhase::Stay );
    SW_EXPECT_TRUE( listEvent[0]._bodyA == bodyC._body );
    SW_EXPECT_FALSE( tracker.isStayReported( bodyA._body, bodyB._body ) );

    tracker.endSubContact( bodyA._body, bodyB._body ); // 듣지 않는 쌍도 끝은 낸다
    listEvent.clear();
    tracker.finishStep( listEvent );
    SW_REQUIRE_EQUAL( size_t{ 2 }, listEvent.size() );
    SW_EXPECT_TRUE( listEvent[0]._phase == PhysicsContactPhase::End );
    SW_EXPECT_TRUE( listEvent[1]._phase == PhysicsContactPhase::Stay );
}
