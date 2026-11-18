// HealthBarSpawnStateResetTest — 풀 반환(`HealthBarComponent::resetToSpawnState`): 쓰러져 비고 보이던 HP 바가 시작할 때의 비율(흔적 없이) · 보임으로 돌아온다(nogpu, 마커 없음).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Client/Marker/HealthBarComponent.h"

using namespace sw;

/** @brief [HealthBarSpawnStateResetTest] 시작 전(갓 스폰)이면 되돌려도 그대로이고, 시작 뒤 쓰러진 바는 시작할 때의 비율 · 보임으로 돌아온다 */
SW_TEST_CASE( HealthBarSpawnStateResetTest, DeadBarReturnsToItsBeginPlayState )
{
    SW_EXPECT_LOG( "HealthBarComponent.HPBarNoWidgetComponent" ); // 마커 없이 계산만 본다
    GameObjectManager   manager;
    HealthBarComponent* pBar = manager.createGameObject( hashed_string( "Enemy" ) )->addComponent<HealthBarComponent>();
    SW_REQUIRE_NOT_NULL( pBar );
    pBar->resetRatio( 1.0f );
    SW_EXPECT_TRUE( pBar->resetToSpawnState() == SpawnStateReset::Reset );
    SW_EXPECT_NEAR( 1.0f, pBar->getTargetRatio(), 1.0e-6f );

    manager.beginPlay();
    manager.update( 1.0f / 60.0f );
    SW_REQUIRE_TRUE( pBar->hasBegunPlay() );
    const bool bSpawnVisible = pBar->isVisible();
    pBar->setVisible( bSpawnVisible == false ); // 맞아서 보였다(또는 쓰러져 숨었다)
    pBar->setTargetRatio( 0.0f );
    manager.update( 1.0f / 60.0f );
    SW_REQUIRE_TRUE( pBar->getHPRatio() < 0.5f );

    SW_EXPECT_TRUE( pBar->resetToSpawnState() == SpawnStateReset::Reset );
    SW_EXPECT_EQUAL( bSpawnVisible, pBar->isVisible() );
    SW_EXPECT_NEAR( 1.0f, pBar->getTargetRatio(), 1.0e-6f );
    SW_EXPECT_NEAR( 1.0f, pBar->getHPRatio(), 1.0e-6f );
    SW_EXPECT_NEAR( 1.0f, pBar->getRemainRatio(), 1.0e-6f );
}
