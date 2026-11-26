// HitReceiverTest — 맞음 전달이 피해 받는 쪽(IHitReceiver)을 한 번 먼저 부르고 그 다음 반응(맞음 사건)에 알리는지 본다(nogpu, 헤드리스).
#include "pch.h"

#include "Core/Container/string.h"
#include "Core/Delegate/Subscription.h"
#include "Core/TestFramework/TestFramework.h"

#include "Engine/Character/Hit/CharacterHit.h"
#include "Engine/Character/Hit/IHitReceiver.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectEvents.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    REFLECT( HideInMenu )
    struct HitReceiverTestComponent : public Component
    {
        REFLECT_BODY();
        REFLECT_INTERFACE( IHitReceiver, _hitReceiverPort );

        /** @brief 피해를 받으면 `R` 을 적는 포트입니다. */
        struct HitReceiverPort final : IHitReceiver
        {
            void receiveHit( const HitInfo& /*hit*/ ) override { getPortOwner<HitReceiverTestComponent>( *this, offsetof( HitReceiverTestComponent, _hitReceiverPort ) )._order.push_back( 'R' ); }
        };

        void onRegister( GameObjectManager& manager ) override
        {
            Component::onRegister( manager );
            _hitSubscription = GameObjectEvents::listen<&HitReceiverTestComponent::handleHit>( *this, &GameObjectEvents::_hitReceived );
        }
        /** @brief 반응(맞음 사건)을 받으면 `E` 를 적습니다. */
        void handleHit( const HitInfo& /*hit*/ ) { _order.push_back( 'E' ); }

        string          _order;
        Subscription    _hitSubscription;
        HitReceiverPort _hitReceiverPort;
    };
} // namespace sw

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

#include "TestHitReceiver.cpp.gen.inl"
