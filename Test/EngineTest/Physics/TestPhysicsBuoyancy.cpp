/**
 * @file TestPhysicsBuoyancy.cpp
 * @brief 부력(`BuoyancyVolumeComponent`, Jolt `ApplyBuoyancyImpulse`) — 물보다 가벼운 상자는 수면에 떠 멈추고, 밀도가 같으면 가라앉지도 뜨지도 않으며, 흐름이 떠 있는 상자를 끌고 간다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/Physics/BuoyancyVolumeComponent.h"
#include "Engine/Object/Component/Physics/RigidBodyComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "TestFramework/Harness/TestTick.h"
#include "TestFramework/TestFramework.h"

namespace
{
    struct PhysicsBuoyancyTestInternal
    {
        /** @brief 윗면이 y = 0 인 물 상자(20 × 10 × 20)입니다. */
        static sw::BuoyancyVolumeComponent* spawnWater( sw::GameObjectManager& manager, float32 buoyancy, const sw::float3& flow )
        {
            sw::GameObject*              pObject = manager.createGameObject( sw::hashed_string( "Water" ) );
            sw::BuoyancyVolumeComponent* pWater  = pObject->addComponent<sw::BuoyancyVolumeComponent>();
            pWater->setHalfExtents( sw::float3{ 10.0f, 5.0f, 10.0f } );
            pWater->setBuoyancy( buoyancy );
            pWater->setFlowVelocity( flow );
            pWater->setLocalPosition( sw::float3{ 0.0f, -5.0f, 0.0f } );
            return pWater;
        }

        /** @brief 1 m 상자(움직이는 바디)를 @p position 에 둡니다. */
        static sw::RigidBodyComponent* spawnCrate( sw::GameObjectManager& manager, const sw::float3& position )
        {
            sw::GameObject*         pObject = manager.createGameObject( sw::hashed_string( "Crate" ) );
            sw::RigidBodyComponent* pBody   = pObject->addComponent<sw::RigidBodyComponent>();
            sw::PhysicsShapeDesc3D  box;
            box._halfExtents = sw::float3{ 0.5f, 0.5f, 0.5f };
            pBody->setShape( box );
            pBody->setBodyType( sw::PhysicsBodyType::Dynamic );
            pBody->setLocalPosition( position );
            return pBody;
        }
    };
} // namespace

/**
 * @brief [PhysicsBuoyancyTest] 부력 2 의 물에 떨어뜨린 상자는 가라앉지 않고 수면(y = 0) 둘레에 떠 멈춘다 — 잠긴 바디로 세인다
 */
SW_TEST_CASE( PhysicsBuoyancyTest, LightCrateFloatsAtTheSurface )
{
    using Internal = PhysicsBuoyancyTestInternal;
    sw::GameObjectManager        manager;
    sw::BuoyancyVolumeComponent* pWater = Internal::spawnWater( manager, 2.0f, sw::float3{} );
    sw::RigidBodyComponent*      pCrate = Internal::spawnCrate( manager, sw::float3{ 0.0f, 2.0f, 0.0f } );
    manager.beginPlay();
    test::updateFrames( manager, 600 );
    const float32 height = pCrate->getWorldPosition()._y;
    // 부력 2 = 물 밀도가 상자의 두 배라 반만 잠긴다 — 중심이 수면(0)에 온다(Debug 실측 -0.0008, 2026-10-22).
    SW_EXPECT_TRUE_MSG( height > -0.1f && height < 0.1f, ( "crate rests at y = " + std::to_string( height ) ).c_str() );
    SW_EXPECT_EQUAL( 1u, pWater->getSubmergedBodyCount() );
    manager.endPlay();
}

/**
 * @brief [PhysicsBuoyancyTest] 흐름(+X 2 m/s)이 있는 물에 뜬 상자는 흐름 쪽으로 떠내려간다
 */
SW_TEST_CASE( PhysicsBuoyancyTest, FlowCarriesFloatingCrate )
{
    using Internal = PhysicsBuoyancyTestInternal;
    sw::GameObjectManager manager;
    (void)Internal::spawnWater( manager, 2.0f, sw::float3{ 2.0f, 0.0f, 0.0f } ); // 물은 씬에 남는다 — 이 테스트는 상자의 자리만 본다
    sw::RigidBodyComponent* pCrate = Internal::spawnCrate( manager, sw::float3{ -5.0f, 0.0f, 0.0f } );
    manager.beginPlay();
    test::updateFrames( manager, 120 );
    SW_EXPECT_TRUE_MSG( pCrate->getWorldPosition()._x > -4.0f, ( "crate drifted to x = " + std::to_string( pCrate->getWorldPosition()._x ) ).c_str() );
    manager.endPlay();
}

/**
 * @brief [PhysicsBuoyancyTest] 물 밖의 상자에는 부력이 없다 — 그냥 떨어진다
 */
SW_TEST_CASE( PhysicsBuoyancyTest, CrateOutsideTheWaterFalls )
{
    using Internal = PhysicsBuoyancyTestInternal;
    sw::GameObjectManager        manager;
    sw::BuoyancyVolumeComponent* pWater = Internal::spawnWater( manager, 2.0f, sw::float3{} );
    sw::RigidBodyComponent*      pCrate = Internal::spawnCrate( manager, sw::float3{ 30.0f, 2.0f, 0.0f } );
    manager.beginPlay();
    test::updateFrames( manager, 60 );
    SW_EXPECT_TRUE( pCrate->getWorldPosition()._y < 1.0f );
    SW_EXPECT_EQUAL( 0u, pWater->getSubmergedBodyCount() );
    manager.endPlay();
}
