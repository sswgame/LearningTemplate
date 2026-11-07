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
