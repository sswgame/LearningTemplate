// InteractorEffectTest — 진행 동안만의 시작 효과: 대상이 진행 중에 사라져도 취소가 시작 효과를 지운다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Runtime/Gameplay/Ability/AbilityCatalog.h"
#include "GameFramework/Runtime/Gameplay/Ability/AbilitySystemComponent.h"
#include "GameFramework/Runtime/Gameplay/Interaction/InteractableComponent.h"
#include "GameFramework/Runtime/Gameplay/Interaction/InteractionCatalog.h"
#include "GameFramework/Runtime/Gameplay/Interaction/InteractorComponent.h"

using namespace sw;

/** @brief [InteractorEffectTest] 누르고 있기 도중 대상이 파괴되면 상호작용이 취소되고 시작 효과(이동 금지)가 지워진다 — 대상의 정의를 다시 찾던 예전에는 영영 남았다 */
SW_TEST_CASE( InteractorEffectTest, StartEffectEndsWhenTheTargetIsDestroyed )
{
    GameObjectManager manager;
    AbilityCatalog    catalog;
    AbilityEffectDef  lock;
    lock._id             = hashed_string( "GE_Test_ChannelLock" );
    lock._durationPolicy = EffectDurationPolicy::Infinite;
    SW_REQUIRE_TRUE( catalog.addEffect( lock ) );

    GameObject& hero = *manager.createGameObject( hashed_string( "Hero" ) );
    (void)hero.addComponent<SceneComponent>();
    AbilitySystemComponent* pAbility = hero.addComponent<AbilitySystemComponent>();
    pAbility->setCatalog( &catalog );
    InteractorComponent* pInteractor = hero.addComponent<InteractorComponent>();

    GameObject* pChest = manager.createGameObject( hashed_string( "Chest" ) );
    pChest->addComponent<SceneComponent>()->setWorldPosition( float3{ 0.0f, 0.0f, 1.0f } );
    InteractionDef def;
    def._id              = hashed_string( "open" );
    def._maxDistance     = 5.0f;
    def._startEffect     = lock._id;
    def._bLineOfSight    = SW_FALSE;
    def._bEndStartEffect = SW_TRUE;
    InteractionStepDef step;
    step._mode     = InteractionInputMode::Hold;
    step._duration = 10.0f;
    def._listStep.push_back( step );
    pChest->addComponent<InteractableComponent>()->setDefinition( def );
    manager.beginPlay();

    constexpr float32 kFrame = 1.0f / 60.0f;
    pInteractor->setInput( true );
    manager.update( kFrame );
    SW_REQUIRE_TRUE( pInteractor->isInteracting() );
    SW_EXPECT_EQUAL( 1u, pAbility->getActiveEffectCount() );

    manager.destroyGameObject( pChest );
    manager.update( kFrame );
    manager.update( kFrame );
    SW_EXPECT_FALSE( pInteractor->isInteracting() );
    SW_EXPECT_EQUAL( 0u, pAbility->getActiveEffectCount() );
}
