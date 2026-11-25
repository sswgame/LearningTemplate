// HealthBarSpawnStateResetTest — 풀 반환(`HealthBarComponent::resetToSpawnState`): 쓰러져 비고 보이던 HP 바가 시작할 때의 보임으로 돌아오고, 비율은 다시 켜진 첫 틱에 몸의 지금 체력에서 흔적 없이 다시 읽는다(nogpu, 마커 없음).
#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Client/Marker/HealthBarComponent.h"
#include "GameFramework/Runtime/Foundation/GameMode/PrefabPool.h"
#include "GameFramework/Runtime/Foundation/GameMode/Tests/PrefabPoolComponentSpawner.h"
#include "GameFramework/Runtime/Gameplay/Ability/AbilitySystemComponent.h"
#include "GameFramework/Runtime/Gameplay/Ability/CombatAttributeSet.h"

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

/** @brief [HealthBarSpawnStateResetTest] 풀에서 다시 꺼낸 몸의 체력을 깎은 값(30/100)으로 정하면 첫 틱의 HP 바가 그 비율이다 — 처음 잡아 둔 비율 · 지난 삶의 흔적이 아니다 */
SW_TEST_CASE( HealthBarSpawnStateResetTest, ReacquiredBodyShowsItsCurrentHealth )
{
    SW_EXPECT_LOG( "HealthBarComponent.HPBarNoWidgetComponent" ); // 마커 없이 계산만 본다
    GameObjectManager manager;
    PrefabPool        pool = PrefabPoolComponentSpawner<AbilitySystemComponent, HealthBarComponent>::makePool( 2 );
    manager.beginPlay();
    GameObject* pObject = pool.acquire( manager, "Grunt" );
    SW_REQUIRE_NOT_NULL( pObject );
    AbilitySystemComponent* pAbility = pObject->getComponent<AbilitySystemComponent>();
    HealthBarComponent*     pBar     = pObject->getComponent<HealthBarComponent>();
    SW_REQUIRE_TRUE( pAbility != nullptr && pBar != nullptr );
    manager.update( 1.0f / 60.0f );
    SW_REQUIRE_TRUE( pBar->hasBegunPlay() );

    // 가득 찬 채 싸우다 쓰러졌다.
    SW_REQUIRE_NOT_NULL( pAbility->addAttributeSet<CombatAttributeSet>() );
    SW_REQUIRE_TRUE( pAbility->setAttributeBaseValue( CombatAttributes::maxHealth(), 100.0f ) );
    SW_REQUIRE_TRUE( pAbility->setAttributeBaseValue( CombatAttributes::health(), 100.0f ) );
    manager.update( 1.0f / 60.0f );
    SW_REQUIRE_TRUE( pAbility->setAttributeBaseValue( CombatAttributes::health(), 0.0f ) );
    manager.update( 1.0f / 60.0f );

    pool.release( manager, pObject->getHandle() );
    SW_REQUIRE_TRUE( pool.acquire( manager, "Grunt" ) == pObject );
    // 꺼낸 쪽(디렉터)이 세트를 다시 주고 이번 몸의 체력을 정한다.
    SW_REQUIRE_NOT_NULL( pAbility->addAttributeSet<CombatAttributeSet>() );
    SW_REQUIRE_TRUE( pAbility->setAttributeBaseValue( CombatAttributes::maxHealth(), 100.0f ) );
    SW_REQUIRE_TRUE( pAbility->setAttributeBaseValue( CombatAttributes::health(), 30.0f ) );
    manager.update( 1.0f / 60.0f );

    SW_EXPECT_NEAR( 0.3f, pBar->getTargetRatio(), 1.0e-4f );
    SW_EXPECT_NEAR( 0.3f, pBar->getHPRatio(), 1.0e-4f );
    SW_EXPECT_NEAR( 0.3f, pBar->getRemainRatio(), 1.0e-4f ); // 흔적 없이 시작한다
}
