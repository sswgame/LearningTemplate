#include "pch.h"

#include "Engine/Object/Component/Physics/LaunchVelocityComponent.h"
#include "Engine/Object/Component/Physics/RigidBodyComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

// LaunchVelocityTest — 투사체의 처음 속도(LaunchVelocityComponent): 첫 틱에 동적 강체에 한 번 걸고, 강체가 없거나 동적이 아니면 오류로 날리지 않는다.

/**
 * @brief [LaunchVelocityTest] 동적 강체가 있으면 첫 틱에 한 번 날리고, 강체가 키네마틱이면 오류로 알리고 날리지 않는다
 */
SW_TEST_CASE( LaunchVelocityTest, LaunchesOnlyADynamicBodyOnTheFirstTick )
{
    GameObjectManager        manager;
    GameObject*              pArrow  = manager.createGameObject( hashed_string( "Arrow" ) );
    RigidBodyComponent*      pBody   = pArrow != nullptr ? pArrow->addComponent<RigidBodyComponent>() : nullptr;
    LaunchVelocityComponent* pLaunch = pArrow != nullptr ? pArrow->addComponent<LaunchVelocityComponent>() : nullptr;
    SW_ASSERT_TRUE( pBody != nullptr && pLaunch != nullptr );
    pBody->setBodyType( PhysicsBodyType::Dynamic );
    pLaunch->dispatchBeginPlay();
    SW_EXPECT_FALSE( pLaunch->hasLaunched() ); // 스폰한 쪽이 자세를 정한 뒤(첫 틱)에 읽는다
    pLaunch->onTick( 0.016f );
    SW_EXPECT_TRUE( pLaunch->hasLaunched() );

    GameObject*              pStuck       = manager.createGameObject( hashed_string( "Stuck" ) );
    RigidBodyComponent*      pStuckBody   = pStuck != nullptr ? pStuck->addComponent<RigidBodyComponent>() : nullptr;
    LaunchVelocityComponent* pStuckLaunch = pStuck != nullptr ? pStuck->addComponent<LaunchVelocityComponent>() : nullptr;
    SW_ASSERT_TRUE( pStuckBody != nullptr && pStuckLaunch != nullptr );
    pStuckBody->setBodyType( PhysicsBodyType::Kinematic );
    pStuckLaunch->dispatchBeginPlay();
    {
        SW_TEST_DEFENSIVE_SCOPE( "a launch on a kinematic body" );
        pStuckLaunch->onTick( 0.016f );
    }
    SW_EXPECT_FALSE( pStuckLaunch->hasLaunched() );
}
