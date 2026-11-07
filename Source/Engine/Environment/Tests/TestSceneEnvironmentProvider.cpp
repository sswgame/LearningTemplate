#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Environment/Sky/SkyComponent.h"
#include "Engine/Environment/Sky/SkyGradient.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

using namespace sw;

/**
 * @brief [SceneEnvironmentProviderTest] 렌더러가 보는 서비스 창구가 하늘 찾기와 같은 답을 낸다 — 하늘이 없으면 false, 활성 하늘이 있으면 그 값이다
 */
SW_TEST_CASE( SceneEnvironmentProviderTest, ServiceSkyMatchesTheComponentLookup )
{
    GameObjectManager manager;
    SkyGradient       gradient;
    SW_EXPECT_FALSE( manager.findServiceSky( gradient ) );

    GameObject*   pObject = manager.createGameObject( hashed_string( "Sky" ) );
    SkyComponent* pSky    = pObject != nullptr ? pObject->addComponent<SkyComponent>() : nullptr;
    SW_REQUIRE_NOT_NULL( pSky );
    pSky->setIntensity( 3.0f );

    SW_EXPECT_TRUE( manager.findServiceSky( gradient ) );
    SW_EXPECT_NEAR( 3.0f, gradient._intensity, 1.0e-6f );
}
