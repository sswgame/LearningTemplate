#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Visual/Component/DirectionalLightComponent.h"

// 방향광 고르기 — 주광(`findActive`) · 그림자 빛(`findShadowCasting`)은 등록 순서가 아니라 세기로 고른다(nogpu, 헤드리스).

using namespace sw;

/** @brief [DirectionalLightSelectionTest] 해와 달처럼 방향광이 둘이면 먼저 등록된 쪽이 아니라 더 밝은 쪽이 주광이고, 그림자 빛은 그림자를 드리우는 것 가운데 가장 밝은 것이다 */
SW_TEST_CASE( DirectionalLightSelectionTest, BrightestLightWinsOverRegistrationOrder )
{
    GameObjectManager          manager;
    DirectionalLightComponent* pMoon = manager.createGameObject( hashed_string( "Moon" ) )->addComponent<DirectionalLightComponent>();
    DirectionalLightComponent* pSun  = manager.createGameObject( hashed_string( "Sun" ) )->addComponent<DirectionalLightComponent>();
    SW_REQUIRE_NOT_NULL( pMoon );
    SW_REQUIRE_NOT_NULL( pSun );
    pMoon->setIntensity( 0.2f );
    pMoon->setCastShadow( true );
    pSun->setIntensity( 3.0f );
    pSun->setCastShadow( false );

    SW_EXPECT_TRUE( DirectionalLightComponent::findActive( &manager ) == pSun );
    SW_EXPECT_TRUE( DirectionalLightComponent::findShadowCasting( &manager ) == pMoon ); // 그림자를 드리우는 것은 달뿐이다

    pSun->setCastShadow( true );
    SW_EXPECT_TRUE( DirectionalLightComponent::findShadowCasting( &manager ) == pSun );
    pSun->setActive( false );
    SW_EXPECT_TRUE( DirectionalLightComponent::findActive( &manager ) == pMoon ); // 꺼진 빛은 고르지 않는다

    pSun->setActive( true );
    pMoon->setIntensity( 3.0f );
    SW_EXPECT_TRUE( DirectionalLightComponent::findActive( &manager ) == ( pMoon->getComponentID() < pSun->getComponentID() ? pMoon : pSun ) ); // 같은 세기는 번호가 작은 쪽
}
