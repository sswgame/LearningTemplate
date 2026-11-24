// HidingSpotTest — 숨는 곳: 들어가면 숨음 태그, 나오면 뗌. 걸어 나가거나 숨는 곳이 사라져도 태그가 남지 않는다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Runtime/Gameplay/Interaction/HidingSpotComponent.h"
#include "GameFramework/Runtime/Gameplay/Interaction/InteractableComponent.h"
#include "GameFramework/Runtime/Gameplay/Interaction/InteractionCatalog.h"
#include "GameFramework/Runtime/Gameplay/Interaction/SmartObjectComponent.h"

using namespace sw;

namespace
{
    /** @brief 슬롯 하나인 사물함과 그 앞의 플레이어입니다. */
    struct HidingSpotFixture
    {
        static constexpr float32 kFrame = 1.0f / 60.0f;

        GameObjectManager    _manager;
        GameObject*          _pLocker{ nullptr };
        GameObject*          _pPlayer{ nullptr };
        HidingSpotComponent* _pSpot{ nullptr };

        HidingSpotFixture()
        {
            _pLocker = _manager.createGameObject( hashed_string( "Locker" ) );
            (void)_pLocker->addComponent<SceneComponent>();
            SmartObjectDef     smart;
            SmartObjectSlotDef slot;
            slot._tags.addTag( "Activity.Hide"_tag );
            smart._id = hashed_string( "locker" );
            smart._listSlot.push_back( slot );
            _pLocker->addComponent<SmartObjectComponent>()->setDefinition( smart );
            InteractionDef     def;
            InteractionStepDef step;
            def._id          = hashed_string( "hide" );
            def._maxDistance = 2.0f;
            def._listStep.push_back( step );
            _pLocker->addComponent<InteractableComponent>()->setDefinition( def );
            _pSpot   = _pLocker->addComponent<HidingSpotComponent>();
            _pPlayer = _manager.createGameObject( hashed_string( "Player" ) );
            (void)_pPlayer->addComponent<SceneComponent>();
            _manager.beginPlay();
        }
    };
} // namespace

/** @brief [HidingSpotTest] 숨은 채 걸어 나가면 저절로 나온다 — 자리도 비고 다시 숨을 수 있다 */
SW_TEST_CASE( HidingSpotTest, WalkingAwayLeavesTheSpot )
{
    HidingSpotFixture rig;
    SW_REQUIRE_TRUE( rig._pSpot->enter( *rig._pPlayer ) );
    rig._manager.update( rig.kFrame );
    SW_EXPECT_TRUE( HidingSpotComponent::isHidden( *rig._pPlayer ) ); // 자리에 있으면 그대로다
    rig._pPlayer->getPrimarySceneComponent()->setWorldPosition( float3{ 10.0f, 0.0f, 0.0f } );
    rig._manager.update( rig.kFrame );
    SW_EXPECT_FALSE( HidingSpotComponent::isHidden( *rig._pPlayer ) );
    SW_EXPECT_EQUAL( 1, rig._pLocker->getComponent<SmartObjectComponent>()->countFreeSlots() );
    SW_EXPECT_FALSE( rig._pSpot->exit( *rig._pPlayer ) );
    SW_EXPECT_TRUE( rig._pSpot->enter( *rig._pPlayer ) );
}

/** @brief [HidingSpotTest] 숨는 곳이 사라지면(부서짐 · 스트리밍) 숨었던 쪽은 나온 것이다 */
SW_TEST_CASE( HidingSpotTest, DestroyedSpotRevealsItsOccupant )
{
    HidingSpotFixture rig;
    SW_REQUIRE_TRUE( rig._pSpot->enter( *rig._pPlayer ) );
    rig._manager.destroyGameObject( rig._pLocker );
    rig._manager.update( rig.kFrame );
    rig._manager.update( rig.kFrame );
    SW_EXPECT_FALSE( HidingSpotComponent::isHidden( *rig._pPlayer ) );
}
