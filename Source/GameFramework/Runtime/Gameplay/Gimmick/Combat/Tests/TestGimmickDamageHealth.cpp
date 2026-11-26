// GimmickDamageHealthTest — 기믹 피해가 무기 · 근접과 같은 맞음 길(`CharacterRayHit::deliverHit` → `AbilitySystemComponent::receiveHit`)로 체력을 깎는다.
// 결함: 기믹 피해가 센서와 "game" 채널 이벤트에만 들어 가시 · 불 · 폭발 통이 캐릭터 체력에 닿지 않았다. (nogpu).
#include "pch.h"

#include "Core/String/Name/hashed_string.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Runtime/Gameplay/Ability/AbilitySystemComponent.h"
#include "GameFramework/Runtime/Gameplay/Ability/Tests/CombatAttributeTestUtil.h"
#include "GameFramework/Runtime/Gameplay/Gimmick/Combat/ExplosiveBarrelComponent.h"
#include "GameFramework/Runtime/Gameplay/Gimmick/Combat/GimmickDamageUtil.h"
#include "GameFramework/Runtime/Gameplay/Gimmick/Trigger/GimmickSensorComponent.h"

using namespace sw;

namespace
{
    struct TestGimmickDamageHealthInternal
    {
        /** @brief 체력 100, 방어 0 인 전투 세트를 가진 유닛을 @p position 에 세웁니다. */
        static AbilitySystemComponent* addUnit( GameObjectManager& manager, const float3& position )
        {
            GameObject* pObject = manager.createGameObject( hashed_string( "Unit" ) );
            pObject->addComponent<SceneComponent>()->setLocalPosition( position );
            AbilitySystemComponent* pStats = pObject->addComponent<AbilitySystemComponent>();
            return pStats != nullptr && pStats->addAttributeSet( test::makeCombatAttributeSet() ) != nullptr ? pStats : nullptr;
        }

        static float32 getHealth( const AbilitySystemComponent& stats ) { return stats.getAttributeValue( CombatAttributes::health() ).value_or( -1.0f ); }
    };
} // namespace

/**
 * @brief [GimmickDamageHealthTest] 틱 밖에서 준 기믹 피해가 체력 원천의 체력을 깎고, 센서 대상은 지금처럼 센서에 쌓인다
 */
SW_TEST_CASE( GimmickDamageHealthTest, HealthSourceAndSensorBothReceive )
{
    using Internal = TestGimmickDamageHealthInternal;
    GameObjectManager       manager;
    AbilitySystemComponent* pStats  = Internal::addUnit( manager, float3{} );
    GameObject*             pCrate  = manager.createGameObject( hashed_string( "Crate" ) );
    GimmickSensorComponent* pSensor = pCrate->addComponent<GimmickSensorComponent>();
    SW_REQUIRE_NOT_NULL( pStats );
    SW_REQUIRE_NOT_NULL( pSensor );
    manager.beginPlay();

    GimmickDamageUtil::applyDamage( *pStats->getOwner(), pCrate, hashed_string( "Hazard" ), 25.0f );
    SW_EXPECT_NEAR( 75.0f, Internal::getHealth( *pStats ), 1.0e-4f );

    GimmickDamageUtil::applyDamage( *pCrate, nullptr, hashed_string( "Hazard" ), 10.0f ); // 체력 원천이 없는 센서 대상 — 센서만 받고 조용히 지나간다
    SW_EXPECT_EQUAL( 10.0f, pSensor->consumeDamage() );
    SW_EXPECT_NEAR( 75.0f, Internal::getHealth( *pStats ), 1.0e-4f );
}

/**
 * @brief [GimmickDamageHealthTest] 폭발 통이 반경 안의 체력 원천을 대상으로 고른다(센서 · 파괴 컴포넌트가 없어도), 피해는 거리 감쇠
 * @details 기본 폭발은 반경 4, 중심 피해 80 이다. 거리 2 의 유닛은 80 × (1 − 2/4) = 40, 반경 밖(거리 5)은 그대로다.
 */
SW_TEST_CASE( GimmickDamageHealthTest, ExplosionReachesHealthSourceInRadius )
{
    using Internal = TestGimmickDamageHealthInternal;
    GameObjectManager manager;
    GameObject*       pBarrel = manager.createGameObject( hashed_string( "Barrel" ) );
    pBarrel->addComponent<SceneComponent>();
    GimmickSensorComponent*   pSensor = pBarrel->addComponent<GimmickSensorComponent>();
    ExplosiveBarrelComponent* pBlast  = pBarrel->addComponent<ExplosiveBarrelComponent>();
    AbilitySystemComponent*   pNear   = Internal::addUnit( manager, float3{ 2.0f, 0.0f, 0.0f } );
    AbilitySystemComponent*   pFar    = Internal::addUnit( manager, float3{ 5.0f, 0.0f, 0.0f } );
    SW_REQUIRE_NOT_NULL( pSensor );
    SW_REQUIRE_NOT_NULL( pBlast );
    manager.beginPlay();

    pSensor->applyDamage( 50.0f ); // 기본 체력 30 을 넘는다
    pBlast->stepOnce();
    SW_REQUIRE_TRUE( pBlast->hasExploded() );
    SW_EXPECT_NEAR( 60.0f, Internal::getHealth( *pNear ), 1.0e-4f );
    SW_EXPECT_NEAR( 100.0f, Internal::getHealth( *pFar ), 1.0e-4f );
}
