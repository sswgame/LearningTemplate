#include "pch.h"

#include "Core/String/StringBuilder.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Object/Component/Physics/RigidBodyComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Physics/IPhysicsScene.h"
#include "Engine/Physics/PhysicsSettings.h"
#include "Engine/Physics/PhysicsSystem.h"

#include "TestFramework/Harness/TestBench.h"
#include "TestFramework/TestFramework.h"

SW_TEST_LOAD_SUITE( PhysicsBenchTest, "physics step cost by Box2D worker count and with Jolt cloth" );
SW_TEST_TIME_BUDGET( PhysicsBenchTest, 2500, "Release 케이스 최대 577 ms(2026-10-15, Ryzen 7 6800H, 세 번 중 최대)의 네 배 — CI 러너 여유" );

SW_LOG_CALLER( "PhysicsBench" );

namespace
{
    constexpr uint32  kBox2DBodyCount         = 3000; ///< Box2D 벤치의 동적 바디 수(60 열로 쌓은 더미)
    constexpr uint32  kBox2DColumnCount       = 60;
    constexpr uint32  kBox2DWarmupStepCount   = 60; ///< 더미가 자리 잡을 때까지(1 초)
    constexpr uint32  kBox2DMeasuredStepCount = 60;
    constexpr float32 kBox2DStep              = 1.0f / 60.0f;

    /**
     * @brief 동적 바디 `kBox2DBodyCount` 개를 바닥 위에 쌓고, 자리 잡은 뒤의 Box2D 스텝 시간(us)을 모읍니다.
     * @param workerCount 2D 일꾼 수(`PhysicsSettings::_workerCount2D`)입니다.
     */
    void measureBox2DSteps( uint32 workerCount, sw::vector<int64>& outListSample )
    {
        sw::PhysicsSettings settings               = sw::engine::getSubsystem<sw::PhysicsSystem>().getSettings(); // 엔진 설정 표(physicssettings.xml)
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
        (void)pScene->createBody( floorDesc ); // 바닥 핸들은 다시 쓰지 않는다 — 만들지 못하면 더미가 떨어져 측정한 값만 달라진다

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

    /** @brief 상자 강체 컴포넌트 하나를 가진 오브젝트를 만듭니다. */
    sw::RigidBodyComponent* spawnBenchBox( sw::GameObjectManager& manager, const sw::float3& position, sw::PhysicsBodyType type )
    {
        sw::GameObject*         pObject = manager.createGameObject( sw::hashed_string( "BenchBox" ) );
        sw::RigidBodyComponent* pBody   = pObject != nullptr ? pObject->addComponent<sw::RigidBodyComponent>() : nullptr;
        if ( pBody == nullptr )
            return nullptr;
        sw::PhysicsShapeDesc3D box;
        box._halfExtents = sw::float3{ 0.4f, 0.4f, 0.4f };
        pBody->setShape( box );
        pBody->setBodyType( type );
        pBody->setLocalPosition( position );
        return pBody;
    }
} // namespace

/**
 * @brief [PhysicsBenchTest] Box2D 스텝 시간 — 동적 바디 3000 개가 쌓인 더미를 일꾼 수 1 · 2 · 4 · 8 로(`PhysicsSettings::_workerCount2D`)
 * @details 전용 워커(`Box2DWorkerPool`)의 효과를 측정하는 벤치다. 같은 장면을 일꾼 수만 바꿔 따로 만들므로 서로의 상태를 나눠 쓰지 않는다.
 *          더미가 자리 잡은 뒤(데우기 스텝)의 스텝만 측정한다. 값은 Release 로 읽는다 — 일꾼 수가 늘수록 p50 이 줄면 이득이다.
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
 * @brief [PhysicsBenchTest] Jolt 스텝 시간 — 동적 상자 500 개 더미만 있을 때와, 윗변을 고정한 천(21 × 21 정점) 넷을 더했을 때
 * @details 소프트 바디는 강체와 같은 `PhysicsSystem::Update` 안에서 풀린다. 첫 줄은 강체 경로의 기준이고(천이 없는 씬은 소프트 바디 표가 비어 있다),
 *          둘째 줄과의 차가 천 넷의 값이다. 천은 매 스텝 고정 자세를 다시 준다(`ClothComponent` 처럼). 값은 Release 로 읽는다.
 */
SW_TEST_CASE( PhysicsBenchTest, JoltStepWithAndWithoutCloth )
{
    constexpr uint32  kBoxCount         = 500;
    constexpr uint32  kBoxColumnCount   = 25;
    constexpr uint32  kClothCount       = 4;
    constexpr uint32  kClothSide        = 21;
    constexpr uint32  kWarmupStepCount  = 60;
    constexpr uint32  kMeasureStepCount = 120;
    constexpr float32 kStep             = 1.0f / 60.0f;
    for ( const bool bWithCloth : { false, true } )
    {
        const sw::PhysicsSettings           settings = sw::engine::getSubsystem<sw::PhysicsSystem>().getSettings(); // 엔진 설정 표(physicssettings.xml)
        sw::unique_ptr<sw::IPhysicsScene3D> pScene   = sw::engine::getSubsystem<sw::PhysicsSystem>().createScene3D( settings );
        SW_ASSERT_TRUE( pScene != nullptr );
        sw::PhysicsBodyDesc3D  floorDesc;
        sw::PhysicsShapeDesc3D floorShape;
        floorShape._halfExtents = sw::float3{ 100.0f, 0.5f, 100.0f };
        floorDesc._listShape.push_back( floorShape );
        floorDesc._position = sw::float3{ 0.0f, -0.5f, 0.0f };
        floorDesc._type     = sw::PhysicsBodyType::Static;
        (void)pScene->createBody( floorDesc ); // 바닥 핸들은 다시 쓰지 않는다 — 만들지 못하면 상자가 떨어져 측정한 값만 달라진다
        for ( uint32 boxIndex = 0; boxIndex < kBoxCount; ++boxIndex )
        {
            sw::PhysicsBodyDesc3D  desc;
            sw::PhysicsShapeDesc3D shape;
            shape._halfExtents = sw::float3{ 0.4f, 0.4f, 0.4f };
            desc._listShape.push_back( shape );
            desc._position = sw::float3{ static_cast<float32>( boxIndex % kBoxColumnCount ) * 0.9f, 0.5f + static_cast<float32>( boxIndex / kBoxColumnCount ) * 0.9f, 0.0f };
            desc._type     = sw::PhysicsBodyType::Dynamic;
            (void)pScene->createBody( desc ); // 상자 핸들은 다시 쓰지 않는다 — 씬이 지워질 때 함께 지워진다
        }
        sw::vector<sw::PhysicsSoftBodyHandle> listCloth;
        sw::vector<sw::float3>                listClothPosition;
        if ( bWithCloth )
        {
            for ( uint32 clothIndex = 0; clothIndex < kClothCount; ++clothIndex )
            {
                sw::PhysicsSoftBodyDesc desc;
                for ( uint32 row = 0; row < kClothSide; ++row )
                {
                    for ( uint32 column = 0; column < kClothSide; ++column )
                    {
                        desc._listVertex.push_back( sw::float3{ static_cast<float32>( column ) * 0.1f - 1.0f, -static_cast<float32>( row ) * 0.1f, 0.0f } );
                        if ( row + 1 < kClothSide && column + 1 < kClothSide )
                        {
                            const uint32 topLeft = row * kClothSide + column;
                            desc._listTriangleIndex.insert( desc._listTriangleIndex.end(),
                                                            { topLeft, topLeft + kClothSide, topLeft + 1, topLeft + 1, topLeft + kClothSide, topLeft + kClothSide + 1 } );
                        }
                    }
                }
                for ( uint32 column = 0; column < kClothSide; ++column )
                {
                    desc._listPinnedVertex.push_back( column ); // 윗줄
                }
                desc._position = sw::float3{ static_cast<float32>( clothIndex ) * 3.0f - 5.0f, 6.0f, -3.0f };
                listClothPosition.push_back( desc._position );
                listCloth.push_back( pScene->createSoftBody( desc ) );
                SW_EXPECT_TRUE( listCloth.back().isValid() );
            }
        }
        sw::vector<int64> listSample;
        for ( uint32 stepIndex = 0; stepIndex < kWarmupStepCount + kMeasureStepCount; ++stepIndex )
        {
            const sw::Stopwatch stopwatch;
            for ( size_t clothIndex = 0; clothIndex < listCloth.size(); ++clothIndex )
            {
                pScene->setSoftBodyPinnedTransform( listCloth[clothIndex], listClothPosition[clothIndex], sw::quaternion{} );
            }
            pScene->step( kStep );
            if ( kWarmupStepCount <= stepIndex )
                listSample.push_back( stopwatch.getElapsedMicroseconds() );
        }
        test::logBenchSamples( bWithCloth ? "Jolt step  500 dynamic boxes + 4 pinned cloths (21 x 21)" : "Jolt step  500 dynamic boxes", listSample );
    }
}

/**
 * @brief [PhysicsBenchTest] 씬 물리 프레임(`ScenePhysics::step`, 컴포넌트 길 전체) — 정적 강체 10000 개 + 동적 강체 1000 개(900 개는 잠듦)
 * @details 정적 · 잠든 바디가 프레임마다 내는 비용을 잰다. 바깥 이동은 트랜스폼의 월드 변경 번호로 알고, 스텝 뒤 자세는 백엔드가 깨어 있다고 한
 *          바디만 읽으므로 이 비용은 바디 수가 아니라 깨어 있는 바디 수를 따라야 한다. 깨어 있는 100 개는 프레임마다 옆으로 미는 속도를 받는다.
 *          정적 바디는 위쪽(y = 50) 격자에 두어 동적 바디와 겹치지 않는다. 데우기 동안 바닥 위 900 개가 잠든다. 값은 Release 로 읽는다(측정은 통합).
 */
SW_TEST_CASE( PhysicsBenchTest, SceneFrameWithManyStaticAndSleepingBodies )
{
    constexpr uint32  kStaticSide        = 100; // 100 × 100 = 10000
    constexpr uint32  kSleeperSide       = 30;  // 30 × 30 = 900
    constexpr uint32  kAwakeCount        = 100;
    constexpr uint32  kWarmupFrameCount  = 90; // 바닥 위 상자가 잠들 때까지(잠들기 0.5 초 + 여유)
    constexpr uint32  kMeasureFrameCount = 60;
    constexpr float32 kFrame             = 1.0f / 60.0f;

    sw::GameObjectManager   manager;
    sw::RigidBodyComponent* pFloor = spawnBenchBox( manager, sw::float3{ 0.0f, -0.5f, 0.0f }, sw::PhysicsBodyType::Static );
    SW_ASSERT_NOT_NULL( pFloor );
    sw::PhysicsShapeDesc3D floorShape;
    floorShape._halfExtents = sw::float3{ 200.0f, 0.5f, 200.0f };
    pFloor->setShape( floorShape );
    for ( uint32 index = 0; index < kStaticSide * kStaticSide; ++index )
    {
        const sw::float3 position{ static_cast<float32>( index % kStaticSide ) * 2.0f - 100.0f, 50.0f, static_cast<float32>( index / kStaticSide ) * 2.0f - 100.0f };
        SW_ASSERT_NOT_NULL( spawnBenchBox( manager, position, sw::PhysicsBodyType::Static ) );
    }
    for ( uint32 index = 0; index < kSleeperSide * kSleeperSide; ++index )
    {
        const sw::float3 position{ static_cast<float32>( index % kSleeperSide ) * 2.0f - 30.0f, 0.4f, static_cast<float32>( index / kSleeperSide ) * 2.0f - 30.0f };
        SW_ASSERT_NOT_NULL( spawnBenchBox( manager, position, sw::PhysicsBodyType::Dynamic ) );
    }
    sw::vector<sw::RigidBodyComponent*> listAwake;
    for ( uint32 index = 0; index < kAwakeCount; ++index )
    {
        const sw::float3 position{ static_cast<float32>( index ) * 2.0f - 100.0f, 0.4f, 60.0f };
        listAwake.push_back( spawnBenchBox( manager, position, sw::PhysicsBodyType::Dynamic ) );
        SW_ASSERT_NOT_NULL( listAwake.back() );
    }

    manager.beginPlay();
    sw::vector<int64> listSample;
    for ( uint32 frameIndex = 0; frameIndex < kWarmupFrameCount + kMeasureFrameCount; ++frameIndex )
    {
        for ( sw::RigidBodyComponent* pAwake : listAwake )
        {
            pAwake->setLinearVelocity( sw::float3{ 0.0f, 0.0f, frameIndex % 120 < 60 ? 1.0f : -1.0f } );
        }
        const sw::Stopwatch stopwatch;
        manager.update( kFrame );
        if ( kWarmupFrameCount <= frameIndex )
            listSample.push_back( stopwatch.getElapsedMicroseconds() );
    }
    manager.endPlay();
    SW_EXPECT_EQUAL( static_cast<size_t>( kMeasureFrameCount ), listSample.size() );
    test::logBenchSamples( "Scene frame  10000 static + 1000 dynamic rigid bodies (900 asleep)", listSample );
}
