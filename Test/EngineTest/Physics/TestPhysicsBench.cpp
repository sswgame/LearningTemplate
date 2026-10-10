#include "pch.h"

#include "Core/Time/MonotonicClock.h"

#include "Engine/Physics/Collision/AABB.h"
#include "Engine/Physics/PhysicsWorld.h"

#include "TestFramework/TestBench.h"
#include "TestFramework/TestFramework.h"

SW_LOG_CALLER( "PhysicsBench" );

namespace
{
    constexpr uint32  kStaticRowCount    = 55; ///< 정지 바디는 55 x 55 격자(간격 20 — 64 셀 하나에 열 개 남짓)
    constexpr float32 kStaticSpacing     = 20.0f;
    constexpr uint32  kContinuousCount   = 150;
    constexpr float32 kContinuousStep    = 0.5f;    ///< 연속 바디가 step 마다 가는 거리
    constexpr float32 kFarMoverDistance  = 1000.0f; ///< 먼 이동 바디가 step 마다 오가는 거리(에디터 드래그 · 셀 여러 개)
    constexpr uint32  kMeasuredStepCount = 30;

    sw::AABB makeBoxAt( float32 x, float32 y )
    {
        sw::AABB box;
        box._min = sw::float3( x, y, 0.0f );
        box._max = sw::float3( x + 1.0f, y + 1.0f, 1.0f );
        return box;
    }

    /**
     * @brief 정지 바디 격자 위에서 연속 바디들을 조금씩 움직이며 step 마다 걸린 시간(us)을 모읍니다.
     * @param bWithFarMover true 면 연속 아닌 바디 하나가 step 마다 `kFarMoverDistance` 를 오갑니다.
     */
    void measureSteps( bool bWithFarMover, sw::vector<int64>& outListSample )
    {
        sw::PhysicsWorld world;
        uint64           objectID = 1;
        for ( uint32 row = 0; row < kStaticRowCount; ++row )
        {
            for ( uint32 column = 0; column < kStaticRowCount; ++column )
            {
                world.addBody( makeBoxAt( static_cast<float32>( column ) * kStaticSpacing, static_cast<float32>( row ) * kStaticSpacing ), 0, objectID++ );
            }
        }

        sw::vector<sw::PhysicsWorld::BodyHandle> listContinuousBody;
        sw::vector<sw::float2>                   listPosition;
        for ( uint32 index = 0; index < kContinuousCount; ++index )
        {
            // 정지 바디 사이(격자 칸의 가운데)를 지나간다 — 겹침 이벤트가 아니라 후보 모으기를 잰다.
            const sw::float2     position{ static_cast<float32>( index % 50 ) * kStaticSpacing + 5.0f, static_cast<float32>( index / 50 ) * 300.0f + 5.0f };
            sw::PhysicsBodyState state;
            state._aabb        = makeBoxAt( position._x, position._y );
            state._bContinuous = SW_TRUE;
            listContinuousBody.push_back( world.addBody( state, objectID++ ) );
            listPosition.push_back( position );
        }
        const sw::PhysicsWorld::BodyHandle farMover = world.addBody( makeBoxAt( -50.0f, -50.0f ), 0, objectID++ );
        world.step( 0.016f );

        outListSample.clear();
        for ( uint32 stepIndex = 0; stepIndex < kMeasuredStepCount; ++stepIndex )
        {
            for ( uint32 index = 0; index < kContinuousCount; ++index )
            {
                listPosition[index]._x += ( stepIndex % 2 == 0 ) ? kContinuousStep : -kContinuousStep;
                sw::PhysicsBodyState state;
                state._aabb        = makeBoxAt( listPosition[index]._x, listPosition[index]._y );
                state._bContinuous = SW_TRUE;
                world.updateBody( listContinuousBody[index], state );
            }
            if ( bWithFarMover )
                world.setAABB( farMover, makeBoxAt( ( stepIndex % 2 == 0 ) ? kFarMoverDistance : -50.0f, -50.0f ) );

            const sw::Stopwatch stopwatch;
            world.step( 0.016f );
            outListSample.push_back( stopwatch.getElapsedMicroseconds() );
        }
    }
} // namespace

/**
 * @brief [PhysicsBenchTest] 연속 바디 150 개 · 정지 바디 3025 개의 step 시간 — 먼 이동 바디가 하나 있을 때와 없을 때
 * @details 연속 바디의 후보 범위는 그 step 에 다른 바디가 움직인 거리만큼 넓어진다. 한 바디가 셀 여러 개를 건너뛰면(에디터 드래그) 그 거리로 넓히면
 *          모든 연속 바디가 월드 전체를 훑는다. 두 줄의 p50 이 비슷해야 한다. 값은 Release 로 읽는다.
 */
SW_TEST_CASE( PhysicsBenchTest, ContinuousStepWithAFarMover )
{
    sw::vector<int64> listCalm;
    sw::vector<int64> listFar;
    measureSteps( false, listCalm );
    measureSteps( true, listFar );
    SW_EXPECT_EQUAL( static_cast<size_t>( kMeasuredStepCount ), listCalm.size() );
    SW_EXPECT_EQUAL( static_cast<size_t>( kMeasuredStepCount ), listFar.size() );
    test::logBenchSamples( "PhysicsWorld::step  150 continuous / 3025 static", listCalm );
    test::logBenchSamples( "PhysicsWorld::step  + one body moving 1000 per step", listFar );
}
