#include "pch.h"

#include "Core/Math/AABB.h"
#include "Core/String/StringBuilder.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Physics/IPhysicsScene.h"
#include "Engine/Physics/PhysicsOverlapWorld.h"
#include "Engine/Physics/PhysicsSettings.h"
#include "Engine/Physics/PhysicsSystem.h"

#include "TestFramework/Harness/TestBench.h"
#include "TestFramework/TestFramework.h"

SW_TEST_LOAD_SUITE( PhysicsBenchTest, "physics step cost with a far mover, many bodies and worker counts" );
SW_TEST_TIME_BUDGET( PhysicsBenchTest, 2500, "Release 케이스 최대 577 ms(2026-10-15, Ryzen 7 6800H, 세 번 중 최대)의 네 배 — CI 러너 여유" );

SW_LOG_CALLER( "PhysicsBench" );

namespace
{
    constexpr uint32  kStaticRowCount         = 55; ///< 정지 바디는 55 x 55 격자(간격 20 — 64 셀 하나에 열 개 남짓)
    constexpr float32 kStaticSpacing          = 20.0f;
    constexpr uint32  kContinuousCount        = 150;
    constexpr uint32  kLargeStaticRowCount    = 110; ///< 바디 수를 늘린 변형 — 정지 바디 110 x 110(12100 개)
    constexpr uint32  kLargeContinuousCount   = 600;
    constexpr float32 kContinuousStep         = 0.5f;    ///< 연속 바디가 step 마다 가는 거리
    constexpr float32 kFarMoverDistance       = 1000.0f; ///< 먼 이동 바디가 step 마다 오가는 거리(에디터 드래그 · 셀 여러 개)
    constexpr uint32  kMeasuredStepCount      = 30;
    constexpr uint32  kBox2DBodyCount         = 3000; ///< Box2D 벤치의 동적 바디 수(60 열로 쌓은 더미)
    constexpr uint32  kBox2DColumnCount       = 60;
    constexpr uint32  kBox2DWarmupStepCount   = 60; ///< 더미가 자리 잡을 때까지(1 초)
    constexpr uint32  kBox2DMeasuredStepCount = 60;
    constexpr float32 kBox2DStep              = 1.0f / 60.0f;

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
     * @param staticRowCount 정지 바디 격자 한 변의 수입니다.
     * @param continuousCount 연속 바디 수입니다.
     */
    void measureSteps( bool bWithFarMover, uint32 staticRowCount, uint32 continuousCount, sw::vector<int64>& outListSample )
    {
        sw::PhysicsOverlapWorld world;
        uint64                  objectID = 1;
        for ( uint32 row = 0; row < staticRowCount; ++row )
        {
            for ( uint32 column = 0; column < staticRowCount; ++column )
            {
                world.addBody( makeBoxAt( static_cast<float32>( column ) * kStaticSpacing, static_cast<float32>( row ) * kStaticSpacing ), 0, objectID++ );
            }
        }

        sw::vector<sw::PhysicsOverlapWorld::BodyHandle> listContinuousBody;
        sw::vector<sw::float2>                          listPosition;
        for ( uint32 index = 0; index < continuousCount; ++index )
        {
            // 정지 바디 사이(격자 칸의 가운데)를 지나간다 — 겹침 이벤트가 아니라 후보 모으기를 측정한다.
            const sw::float2     position{ static_cast<float32>( index % 50 ) * kStaticSpacing + 5.0f, static_cast<float32>( index / 50 ) * 300.0f + 5.0f };
            sw::PhysicsBodyState state;
            state._aabb        = makeBoxAt( position._x, position._y );
            state._bContinuous = SW_TRUE;
            listContinuousBody.push_back( world.addBody( state, objectID++ ) );
            listPosition.push_back( position );
        }
        const sw::PhysicsOverlapWorld::BodyHandle farMover = world.addBody( makeBoxAt( -50.0f, -50.0f ), 0, objectID++ );
        world.step( 0.016f );

        outListSample.clear();
        for ( uint32 stepIndex = 0; stepIndex < kMeasuredStepCount; ++stepIndex )
        {
            for ( uint32 index = 0; index < continuousCount; ++index )
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

    /**
     * @brief 동적 바디 `kBox2DBodyCount` 개를 바닥 위에 쌓고, 자리 잡은 뒤의 Box2D 스텝 시간(us)을 모읍니다.
     * @param workerCount 2D 일꾼 수(`PhysicsSettings::_workerCount2D`)입니다.
     */
    void measureBox2DSteps( uint32 workerCount, sw::vector<int64>& outListSample )
    {
        sw::PhysicsSettings settings;
        settings.ensureDefaults();
        settings._workerCount2D                    = workerCount;
        sw::unique_ptr<sw::IPhysicsScene2D> pScene = sw::engine::getSubsystem<sw::PhysicsSystem>().createScene2D( settings );
        outListSample.clear();
        if ( pScene == nullptr )
            return;

        sw::PhysicsBodyDesc2D  floorDesc;
        sw::PhysicsShapeDesc2D floorShape;
        floorShape._halfExtents = sw::float2{ 200.0f, 0.5f };
        floorDesc._listShape.push_back( floorShape );
        floorDesc._position = sw::float2{ 0.0f, -0.5f };
        floorDesc._type     = sw::PhysicsBodyType::Static;
        (void)pScene->createBody( floorDesc ); // 바닥 핸들은 다시 쓰지 않는다 — 만들지 못하면 더미가 떨어져 잰 값만 달라진다

        sw::vector<sw::PhysicsBodyDesc2D> listDesc;
        listDesc.reserve( kBox2DBodyCount );
        for ( uint32 bodyIndex = 0; bodyIndex < kBox2DBodyCount; ++bodyIndex )
        {
            sw::PhysicsBodyDesc2D  desc;
            sw::PhysicsShapeDesc2D shape;
            shape._type        = ( bodyIndex % 2 ) == 0 ? sw::PhysicsShapeType2D::Box : sw::PhysicsShapeType2D::Circle;
            shape._halfExtents = sw::float2{ 0.4f, 0.4f };
            shape._radius      = 0.4f;
            desc._listShape.push_back( shape );
            desc._position = sw::float2{ static_cast<float32>( bodyIndex % kBox2DColumnCount ) * 0.9f - 0.45f * static_cast<float32>( kBox2DColumnCount ),
                                         0.5f + static_cast<float32>( bodyIndex / kBox2DColumnCount ) * 0.9f };
            desc._type     = sw::PhysicsBodyType::Dynamic;
            listDesc.push_back( desc );
        }
        sw::vector<sw::PhysicsBodyHandle> listBody;
        pScene->createBodies( sw::span<const sw::PhysicsBodyDesc2D>{ listDesc.data(), listDesc.size() }, listBody );

        for ( uint32 stepIndex = 0; stepIndex < kBox2DWarmupStepCount; ++stepIndex )
        {
            pScene->step( kBox2DStep );
        }
        for ( uint32 stepIndex = 0; stepIndex < kBox2DMeasuredStepCount; ++stepIndex )
        {
            const sw::Stopwatch stopwatch;
            pScene->step( kBox2DStep );
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
    measureSteps( false, kStaticRowCount, kContinuousCount, listCalm );
    measureSteps( true, kStaticRowCount, kContinuousCount, listFar );
    SW_EXPECT_EQUAL( static_cast<size_t>( kMeasuredStepCount ), listCalm.size() );
    SW_EXPECT_EQUAL( static_cast<size_t>( kMeasuredStepCount ), listFar.size() );
    test::logBenchSamples( "PhysicsOverlapWorld::step  150 continuous / 3025 static", listCalm );
    test::logBenchSamples( "PhysicsOverlapWorld::step  + one body moving 1000 per step", listFar );
}

/**
 * @brief [PhysicsBenchTest] Box2D 스텝 시간 — 동적 바디 3000 개가 쌓인 더미를 일꾼 수 1 · 2 · 4 · 8 로(`PhysicsSettings::_workerCount2D`)
 * @details 전용 워커(`Box2DWorkerPool`)의 효과를 재는 벤치다. 같은 장면을 일꾼 수만 바꿔 따로 만들므로 서로의 상태를 나눠 쓰지 않는다.
 *          더미가 자리 잡은 뒤(데우기 스텝)의 스텝만 잰다. 값은 Release 로 읽는다 — 일꾼 수가 늘수록 p50 이 줄면 이득이다.
 */
SW_TEST_CASE( PhysicsBenchTest, Box2DStepByWorkerCount )
{
    const uint32 arrWorkerCount[] = { 1, 2, 4, 8 };
    for ( const uint32 workerCount : arrWorkerCount )
    {
        sw::vector<int64> listSample;
        measureBox2DSteps( workerCount, listSample );
        SW_EXPECT_EQUAL( static_cast<size_t>( kBox2DMeasuredStepCount ), listSample.size() );
        sw::StringBuilder<sw::constant::kMaxBuffer64> label;
        label.appendFormat( "Box2D step  3000 dynamic bodies / workers %#", workerCount );
        test::logBenchSamples( label.c_str(), listSample );
    }
}

/**
 * @brief [PhysicsBenchTest] 바디 수를 늘린 step 시간 — 연속 바디 600 개 · 정지 바디 12100 개
 * @details step 이 바디 사본 · 먼 이동 바디 · 후보 버퍼를 프레임마다 새로 할당하는지 A/B 로 견주는 변형이다. 바디가 늘수록 사본 할당이 커진다.
 *          값은 Release 로 읽는다(`PhysicsBenchTest.ContinuousStepWithAFarMover` 의 기준은 약 319~330 us).
 */
SW_TEST_CASE( PhysicsBenchTest, ContinuousStepWithManyBodies )
{
    sw::vector<int64> listSample;
    measureSteps( false, kLargeStaticRowCount, kLargeContinuousCount, listSample );
    SW_EXPECT_EQUAL( static_cast<size_t>( kMeasuredStepCount ), listSample.size() );
    test::logBenchSamples( "PhysicsOverlapWorld::step  600 continuous / 12100 static", listSample );
}
