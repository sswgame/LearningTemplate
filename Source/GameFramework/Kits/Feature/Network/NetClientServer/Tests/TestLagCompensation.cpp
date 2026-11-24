#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Kits/Feature/Network/NetClientServer/LagCompensation.h"

using namespace sw;

namespace
{
    struct LagCompensationTestUtil
    {
        static constexpr uint32 kWalkerID = 7;

        /** @brief 틱 0 ~ 9 동안 틱마다 X 로 1 씩 걷는 구 하나의 기록입니다. */
        static void recordWalker( LagCompensationHistory& history, float32 radius, float32 height )
        {
            for ( uint32 tick = 0; tick < 10; ++tick )
            {
                vector<LagRecord> listRecord;
                LagRecord&        record = listRecord.emplace_back();
                record._position         = float3{ static_cast<float32>( tick ), 0.0f, 10.0f };
                record._radius           = radius;
                record._height           = height;
                record._entityID         = kWalkerID;
                history.record( tick, listRecord );
            }
        }
    };
} // namespace

/** @brief [LagCompensationTest] 소수 틱은 앞뒤 틱 사이를 보간하고, 기억 범위 밖은 가장 가까운 끝 틱이다. */
SW_TEST_CASE( LagCompensationTest, SampleInterpolatesAndClampsToTheRememberedRange )
{
    LagCompensationHistory history( 4 );
    LagCompensationTestUtil::recordWalker( history, 0.5f, 0.0f );
    LagRecord record;
    SW_REQUIRE_TRUE( history.sampleAt( 7.5f, LagCompensationTestUtil::kWalkerID, record ) );
    SW_EXPECT_NEAR( 7.5f, record._position._x, 1e-4f );
    // 네 틱만 기억한다 — 틱 6 이 가장 오래된 틱이다.
    SW_REQUIRE_TRUE( history.sampleAt( 1.0f, LagCompensationTestUtil::kWalkerID, record ) );
    SW_EXPECT_NEAR( 6.0f, record._position._x, 1e-4f );
    SW_REQUIRE_TRUE( history.sampleAt( 100.0f, LagCompensationTestUtil::kWalkerID, record ) );
    SW_EXPECT_NEAR( 9.0f, record._position._x, 1e-4f );
    SW_EXPECT_FALSE( history.sampleAt( 8.0f, 99, record ) );
}

/** @brief [LagCompensationTest] 지금은 비켜 있어도 보던 시각의 자리를 쏘면 맞고, 지금 자리를 쏘면 맞지 않는다. */
SW_TEST_CASE( LagCompensationTest, RaycastHitsTheRewoundBodyNotTheCurrentOne )
{
    LagCompensationHistory history( 16 );
    LagCompensationTestUtil::recordWalker( history, 0.5f, 0.0f );
    const float3 forward{ 0.0f, 0.0f, 1.0f };
    float32      distance = 0.0f;

    SW_EXPECT_EQUAL( LagCompensationTestUtil::kWalkerID, history.raycastAt( 4.0f, float3{ 4.0f, 0.0f, 0.0f }, forward, 100.0f, 0, distance ) );
    SW_EXPECT_NEAR( 9.5f, distance, 1e-3f );
    SW_EXPECT_EQUAL( 0u, history.raycastAt( 4.0f, float3{ 9.0f, 0.0f, 0.0f }, forward, 100.0f, 0, distance ) );
    SW_EXPECT_NEAR( 100.0f, distance, 1e-3f );
    SW_EXPECT_EQUAL( LagCompensationTestUtil::kWalkerID, history.raycastAt( 9.0f, float3{ 9.0f, 0.0f, 0.0f }, forward, 100.0f, 0, distance ) );
}

/** @brief [LagCompensationTest] 거리 안의 가까운 몸이 답이고, 빼라는 몸과 사거리 밖은 맞지 않는다. */
SW_TEST_CASE( LagCompensationTest, RaycastTakesTheNearestAndHonoursIgnoreAndRange )
{
    LagCompensationHistory history( 4 );
    vector<LagRecord>      listRecord;
    listRecord.push_back( LagRecord{
        float3{ 0.0f, 0.0f, 10.0f },
        0.5f, 0.0f, 1
    } );
    listRecord.push_back( LagRecord{
        float3{ 0.0f, 0.0f, 5.0f },
        0.5f, 0.0f, 2
    } );
    history.record( 0, listRecord );
    const float3 origin{ 0.0f, 0.0f, 0.0f };
    const float3 forward{ 0.0f, 0.0f, 1.0f };
    float32      distance = 0.0f;
    SW_EXPECT_EQUAL( 2u, history.raycastAt( 0.0f, origin, forward, 100.0f, 0, distance ) );
    SW_EXPECT_EQUAL( 1u, history.raycastAt( 0.0f, origin, forward, 100.0f, 2, distance ) );
    SW_EXPECT_EQUAL( 0u, history.raycastAt( 0.0f, origin, forward, 4.0f, 0, distance ) );
}

/** @brief [LagCompensationTest] 몸은 발에서 높이까지의 세로 캡슐이다 — 옆면 · 머리 위 · 발아래를 가른다. */
SW_TEST_CASE( LagCompensationTest, BodyIsAVerticalCapsule )
{
    LagCompensationHistory history( 4 );
    vector<LagRecord>      listRecord;
    listRecord.push_back( LagRecord{
        float3{ 0.0f, 0.0f, 10.0f },
        0.5f, 2.0f, 1
    } );
    history.record( 0, listRecord );
    const float3 forward{ 0.0f, 0.0f, 1.0f };
    float32      distance = 0.0f;
    // 가슴 높이는 옆면이다.
    SW_EXPECT_EQUAL( 1u, history.raycastAt( 0.0f, float3{ 0.0f, 1.0f, 0.0f }, forward, 100.0f, 0, distance ) );
    SW_EXPECT_NEAR( 9.5f, distance, 1e-3f );
    // 꼭대기 근처는 위 반구다 — 옆면보다 멀다.
    SW_EXPECT_EQUAL( 1u, history.raycastAt( 0.0f, float3{ 0.0f, 1.9f, 0.0f }, forward, 100.0f, 0, distance ) );
    SW_EXPECT_TRUE( distance > 9.5f );
    // 머리 위와 옆으로 비켜난 광선은 맞지 않는다.
    SW_EXPECT_EQUAL( 0u, history.raycastAt( 0.0f, float3{ 0.0f, 2.1f, 0.0f }, forward, 100.0f, 0, distance ) );
    SW_EXPECT_EQUAL( 0u, history.raycastAt( 0.0f, float3{ 0.6f, 1.0f, 0.0f }, forward, 100.0f, 0, distance ) );
    // 위에서 곧장 내려오는 광선(원기둥과 평행)은 위 반구에 닿는다.
    SW_EXPECT_EQUAL( 1u, history.raycastAt( 0.0f, float3{ 0.0f, 5.0f, 10.0f }, float3{ 0.0f, -1.0f, 0.0f }, 100.0f, 0, distance ) );
    SW_EXPECT_NEAR( 3.0f, distance, 1e-3f );
}

/** @brief [LagCompensationTest] 두꺼운 선분은 보던 시각의 캡슐에 닿은 몸을 모두 모은다 — 몸 안에서 시작해도 닿고, 지금 자리 · 빼라는 몸 · 두께 밖은 닿지 않는다. */
SW_TEST_CASE( LagCompensationTest, ThickSegmentCollectsEveryRewoundBodyItTouches )
{
    LagCompensationHistory history( 16 );
    LagCompensationTestUtil::recordWalker( history, 0.5f, 2.0f ); // 틱 n 에 (n, 0, 10)
    vector<uint32> listTouched;
    history.collectTouchedAt( 4.0f, float3{ 3.0f, 1.0f, 10.0f }, float3{ 3.4f, 1.0f, 10.0f }, 0.15f, 0, listTouched ); // 옆에서 0.6 + 두께
    SW_REQUIRE_EQUAL( size_t{ 1 }, listTouched.size() );
    SW_EXPECT_EQUAL( LagCompensationTestUtil::kWalkerID, listTouched[0] );
    history.collectTouchedAt( 4.0f, float3{ 4.0f, 1.0f, 10.0f }, float3{ 4.0f, 1.0f, 12.0f }, 0.1f, 0, listTouched ); // 몸 안에서 시작한다
    SW_EXPECT_EQUAL( size_t{ 2 }, listTouched.size() );
    history.collectTouchedAt( 9.0f, float3{ 3.0f, 1.0f, 10.0f }, float3{ 3.4f, 1.0f, 10.0f }, 0.15f, 0, listTouched );                                  // 지금은 비켜 있다
    history.collectTouchedAt( 4.0f, float3{ 3.0f, 1.0f, 10.0f }, float3{ 3.4f, 1.0f, 10.0f }, 0.05f, 0, listTouched );                                  // 두께가 모자란다
    history.collectTouchedAt( 4.0f, float3{ 3.0f, 2.5f, 10.0f }, float3{ 5.0f, 2.5f, 10.0f }, 0.15f, 0, listTouched );                                  // 머리 위로 지난다
    history.collectTouchedAt( 4.0f, float3{ 3.0f, 1.0f, 10.0f }, float3{ 5.0f, 1.0f, 10.0f }, 0.15f, LagCompensationTestUtil::kWalkerID, listTouched ); // 빼라는 몸
    SW_EXPECT_EQUAL( size_t{ 2 }, listTouched.size() );
    history.collectTouchedAt( 4.0f, float3{ 4.0f, 1.0f, 9.2f }, float3{ 4.0f, 1.0f, 9.2f }, 0.4f, 0, listTouched ); // 점(움직이지 않은 칼날)
    SW_EXPECT_EQUAL( size_t{ 3 }, listTouched.size() );
}

/** @brief [LagCompensationTest] 쓰러진 틱 뒤 · 생기기 전의 시각에는 그 몸이 없고, 순간 이동 사이는 잇지 않는다 — 클라이언트 화면과 같은 규칙이다. */
SW_TEST_CASE( LagCompensationTest, AbsentAndTeleportedBodiesFollowTheDisplayRule )
{
    LagCompensationHistory  history( 16 );
    const vector<LagRecord> listNobody;
    vector<LagRecord>       listBody;
    LagRecord&              body = listBody.emplace_back();
    body._position               = float3{ 0.0f, 0.0f, 10.0f };
    body._entityID               = 1;
    history.record( 0, listNobody ); // 틱 1 에 생겨난다
    history.record( 1, listBody );
    history.record( 2, listBody );
    body._position      = float3{ 50.0f, 0.0f, 10.0f };
    body._teleportCount = 1;
    history.record( 3, listBody );   // 다시 태어났다
    history.record( 4, listNobody ); // 쓰러졌다
    LagRecord record;
    SW_EXPECT_FALSE( history.sampleAt( 0.5, 1, record ) );
    SW_REQUIRE_TRUE( history.sampleAt( 2.5, 1, record ) );
    SW_EXPECT_NEAR( 0.0f, record._position._x, 1e-4f ); // 25 m 의 허공이 아니다
    SW_REQUIRE_TRUE( history.sampleAt( 3.5, 1, record ) );
    SW_EXPECT_NEAR( 50.0f, record._position._x, 1e-4f ); // 화면에는 아직 서 있다
    SW_EXPECT_FALSE( history.sampleAt( 4.0, 1, record ) );
    vector<LagRecord> listPast;
    history.collectAt( 3.5, 0, listPast );
    SW_EXPECT_TRUE( listPast.empty() ); // 지금 쓰러진 몸은 맞지 않는다
}

/** @brief [LagCompensationTest] 히트박스 바디는 자세까지 되감는다 — 과거 자세의 바디를 쏜 광선이 지금 자세의 바디를 같은 자리로 쏜다. */
SW_TEST_CASE( LagCompensationTest, BodyPoseMovesTheRayToTheCurrentBody )
{
    LagBodyPose past;
    past._position = float3{ 2.0f, 1.0f, 10.0f };
    past._rotation = quaternion::Identity;
    LagBodyPose now;
    now._position = float3{ -3.0f, 1.5f, 8.0f };
    now._rotation = quaternion::makeFromAxisAngle( float3{ 0.0f, 1.0f, 0.0f }, MathUtil::kHalfPi );
    Ray ray;
    ray._origin     = float3{ 2.2f, 1.1f, 0.0f };
    ray._direction  = float3{ 0.0f, 0.0f, 1.0f };
    const Ray moved = past.moveRayTo( now, ray );
    // 바디 로컬에서 본 광선이 같다.
    const float3 pastLocal = float3::transform( ray.getPoint( 10.0f ) - past._position, std::as_const( past )._rotation.inverse() );
    const float3 nowLocal  = float3::transform( moved.getPoint( 10.0f ) - now._position, std::as_const( now )._rotation.inverse() );
    SW_EXPECT_TRUE( float3::getDistance( pastLocal, nowLocal ) < 1e-3f );
    SW_EXPECT_NEAR( 1.0f, moved._direction.getLength(), 1e-4f );
    SW_EXPECT_NEAR( 0.2f, nowLocal._x, 1e-3f );
}
