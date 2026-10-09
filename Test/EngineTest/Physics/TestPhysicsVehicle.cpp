/**
 * @file TestPhysicsVehicle.cpp
 * @brief 바퀴 차(Jolt `VehicleConstraint`) — 평평한 바닥에서 가속 · 브레이크 · 조향, 같은 입력이면 같은 궤적(같은 실행 안).
 */
#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/Physics/RigidBodyComponent.h"
#include "Engine/Object/Component/Physics/WheeledVehicleComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/ScenePhysics.h"
#include "Engine/Physics/IPhysicsScene.h"

#include "TestFramework/TestFramework.h"
#include "TestFramework/TestTick.h"

#include <string>

namespace
{
    struct PhysicsVehicleTestInternal
    {
        static constexpr float32 kDeltaTime = 1.0f / 60.0f;

        static void spawnFloor( sw::GameObjectManager& manager )
        {
            sw::GameObject*         pFloor = manager.createGameObject( sw::hashed_string( "Floor" ) );
            sw::RigidBodyComponent* pBody  = pFloor->addComponent<sw::RigidBodyComponent>();
            sw::PhysicsShapeDesc3D  box;
            box._halfExtents = sw::float3{ 200.0f, 0.5f, 200.0f };
            pBody->setShape( box );
            pBody->setBodyType( sw::PhysicsBodyType::Static );
            pBody->setLocalPosition( sw::float3{ 0.0f, -0.5f, 0.0f } );
        }

        /** @brief 상자 차체(1.8 × 0.8 × 4 m, 1500 kg) + 바퀴 넷입니다. 바닥 위 0.9 m. */
        static sw::GameObject* spawnCar( sw::GameObjectManager& manager, const utf8* pName, const sw::float3& position )
        {
            sw::GameObject*         pCar  = manager.createGameObject( sw::hashed_string( pName ) );
            sw::RigidBodyComponent* pBody = pCar->addComponent<sw::RigidBodyComponent>();
            sw::PhysicsShapeDesc3D  box;
            box._halfExtents = sw::float3{ 0.9f, 0.4f, 2.0f };
            pBody->setShape( box );
            pBody->setBodyType( sw::PhysicsBodyType::Dynamic );
            pBody->setMass( 1500.0f );
            pBody->setLocalPosition( position );
            pCar->addComponent<sw::WheeledVehicleComponent>();
            return pCar;
        }

        static sw::float3 findPosition( const sw::GameObject& car ) { return car.getComponent<sw::RigidBodyComponent>()->getWorldPosition(); }
    };
} // namespace

/**
 * @brief [PhysicsVehicleTest] 평평한 바닥의 바퀴 차 — 앞 1 × 2 초면 5 m/s 넘게, 브레이크면 거의 선다, 조향을 걸고 가면 진행 방향이 돈다
 */
SW_TEST_CASE( PhysicsVehicleTest, CarAcceleratesStopsAndSteers )
{
    using Internal = PhysicsVehicleTestInternal;
    sw::GameObjectManager manager;
    Internal::spawnFloor( manager );
    sw::GameObject*              pCar     = Internal::spawnCar( manager, "Car", sw::float3{ 0.0f, 0.9f, 0.0f } );
    sw::WheeledVehicleComponent* pVehicle = pCar->getComponent<sw::WheeledVehicleComponent>();
    manager.beginPlay();
    test::tickFrames( manager, 30 ); // 서스펜션이 내려앉는다
    SW_ASSERT_TRUE( pVehicle->isVehicleCreated() );
    SW_EXPECT_EQUAL( 4u, pVehicle->getVehicleState()._groundedWheelCount );

    pVehicle->setDriverInput( 1.0f, 0.0f, 0.0f, 0.0f );
    test::tickFrames( manager, 120 );
    const float32 cruiseSpeed = pVehicle->getVehicleState()._forwardSpeed;
    SW_EXPECT_TRUE_MSG( cruiseSpeed > 5.0f, ( "speed after 2 s " + std::to_string( cruiseSpeed ) ).c_str() );
    SW_EXPECT_TRUE( Internal::findPosition( *pCar )._z > 3.0f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, Internal::findPosition( *pCar )._x, 0.2f );

    pVehicle->setDriverInput( 0.0f, 0.0f, 1.0f, 0.0f );
    test::tickFrames( manager, 180 );
    SW_EXPECT_TRUE_MSG( sw::MathUtil::abs( pVehicle->getVehicleState()._forwardSpeed ) < 0.5f,
                        ( "speed after braking " + std::to_string( pVehicle->getVehicleState()._forwardSpeed ) ).c_str() );

    // 오른쪽으로 꺾고 가면 +X 로 휜다.
    const sw::float3 before = Internal::findPosition( *pCar );
    pVehicle->setDriverInput( 0.6f, 1.0f, 0.0f, 0.0f );
    test::tickFrames( manager, 150 );
    const sw::float3 after = Internal::findPosition( *pCar );
    SW_EXPECT_TRUE_MSG( after._x - before._x > 1.0f, ( "x moved " + std::to_string( after._x - before._x ) ).c_str() );
    manager.endPlay();
}

/**
 * @brief [PhysicsVehicleTest] 같은 실행 안에서 같은 장면 · 같은 입력이면 같은 궤적이다 — 결정성(기계 사이는 보장하지 않는다)
 */
SW_TEST_CASE( PhysicsVehicleTest, SameInputSameTrackInOneRun )
{
    using Internal = PhysicsVehicleTestInternal;
    sw::vector<sw::float3> arrTrack[2];
    for ( sw::vector<sw::float3>& track : arrTrack )
    {
        sw::GameObjectManager manager;
        Internal::spawnFloor( manager );
        sw::GameObject* pCar = Internal::spawnCar( manager, "Car", sw::float3{ 0.0f, 0.9f, 0.0f } );
        manager.beginPlay();
        test::tickFrames( manager, 30 );
        pCar->getComponent<sw::WheeledVehicleComponent>()->setDriverInput( 1.0f, 0.3f, 0.0f, 0.0f );
        for ( uint32 frame = 0; frame < 120; ++frame )
        {
            manager.tick( Internal::kDeltaTime );
            track.push_back( Internal::findPosition( *pCar ) );
        }
        manager.endPlay();
    }
    SW_ASSERT_EQUAL( arrTrack[0].size(), arrTrack[1].size() );
    for ( size_t frame = 0; frame < arrTrack[0].size(); ++frame )
    {
        const sw::float3& first  = arrTrack[0][frame];
        const sw::float3& second = arrTrack[1][frame];
        SW_EXPECT_TRUE_MSG( first._x == second._x && first._y == second._y && first._z == second._z, ( "frame " + std::to_string( frame ) ).c_str() );
    }
    SW_EXPECT_TRUE( arrTrack[0].back()._z > 3.0f );
}
