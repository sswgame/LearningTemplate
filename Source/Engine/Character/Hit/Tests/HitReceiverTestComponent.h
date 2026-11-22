/**
 * @file HitReceiverTestComponent.h
 * @brief 맞음 전달 순서 테스트(`TestHitReceiver.cpp`)가 다는 컴포넌트입니다 — 피해 받는 쪽 포트(`IHitReceiver`)와 반응 구독(`_hitReceived`)을 함께 두고 부른 차례를 적는다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Delegate/Subscription.h"

#include "Engine/Character/Hit/IHitReceiver.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObjectEvents.h"
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
