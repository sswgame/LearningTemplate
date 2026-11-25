// HitReceiverTest — 맞음 전달이 피해 받는 쪽(IHitReceiver)을 한 번 먼저 부르고 그 다음 반응(맞음 사건)에 알리는지 본다(nogpu, 헤드리스).
#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "Engine/Character/Hit/CharacterHit.h"
#include "Engine/Character/Hit/Tests/HitReceiverTestComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

using namespace sw;

/**
 * @brief [HitReceiverTest] 피해는 받는 쪽이 반응보다 먼저 한 번 받는다(언리얼 TakeDamage → OnTakeAnyDamage)
 */
SW_TEST_CASE( HitReceiverTest, ReceiverTakesTheHitBeforeReactions )
{
    GameObjectManager         manager;
    GameObject*               pTarget    = manager.createGameObject( hashed_string( "Target" ) );
    HitReceiverTestComponent* pComponent = pTarget->addComponent<HitReceiverTestComponent>();
    manager.beginPlay();
    HitInfo hit;
    hit._damage = 5.0f;
    CharacterRayHit::deliverHit( *pTarget, hit );
    SW_EXPECT_TRUE( pComponent->_order == "RE" );
}
