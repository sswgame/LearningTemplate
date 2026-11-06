#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Environment/Sky/SkyComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

// 등록 미루기 — 씬을 짓는 배치(`Scene::instantiate`)는 등록(`onRegister`)을 배치 끝으로, 비동기 로드는 씬을 받는 게임 스레드로 미룬다.
// 하늘 컴포넌트는 `onRegister` 에서 타입별 레지스트리에 들어가므로 `findSky` 로 등록 여부를 본다.

using namespace sw;

/**
 * @brief [DeferredRegistrationTest] 미루는 동안 붙은 컴포넌트는 등록되지 않고, 바깥 짝이 끝날 때 등록된다(중첩은 바깥 짝까지 쥔다)
 */
SW_TEST_CASE( DeferredRegistrationTest, RegistersWhenOutermostDeferralEnds )
{
    GameObjectManager manager;
    SkyGradient       gradient;
    manager.beginDeferredRegistration();
    manager.beginDeferredRegistration();
    GameObject*   pObject = manager.createGameObject( hashed_string( "Sky" ) );
    SkyComponent* pSky    = pObject != nullptr ? pObject->addComponent<SkyComponent>() : nullptr;
    SW_REQUIRE_NOT_NULL( pSky );
    SW_EXPECT_FALSE( pSky->isRegistered() );
    SW_EXPECT_FALSE( SkyComponent::findSky( manager, gradient ) );

    manager.endDeferredRegistration();
    SW_EXPECT_FALSE( pSky->isRegistered() );
    manager.endDeferredRegistration();
    SW_EXPECT_TRUE( pSky->isRegistered() );
    SW_EXPECT_TRUE( SkyComponent::findSky( manager, gradient ) );
}

/**
 * @brief [DeferredRegistrationTest] 미룬 등록을 받기 전에 해체된 컴포넌트는 등록도 해제도 불리지 않는다(버린 비동기 로드 씬)
 */
SW_TEST_CASE( DeferredRegistrationTest, DestroyedBeforeRegistrationSkipsBothHooks )
{
    GameObjectManager manager;
    SkyGradient       gradient;
    manager.beginDeferredRegistration();
    GameObject* pObject = manager.createGameObject( hashed_string( "Sky" ) );
    SW_REQUIRE_NOT_NULL( pObject );
    SW_REQUIRE_NOT_NULL( pObject->addComponent<SkyComponent>() );
    manager.destroyGameObject( pObject );
    manager.endDeferredRegistration();
    SW_EXPECT_FALSE( SkyComponent::findSky( manager, gradient ) );
}
