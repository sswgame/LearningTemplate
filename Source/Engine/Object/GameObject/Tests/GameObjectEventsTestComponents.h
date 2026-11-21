/**
 * @file GameObjectEventsTestComponents.h
 * @brief 오브젝트 사건 구독 테스트(`TestGameObjectEvents.cpp`)가 다는 컴포넌트입니다 — 개발 구성만 코드젠하고 "Add Component" 메뉴에 나오지 않는다.
 * @details 등록 자리에서 맞음 · 겹침 진입을 구독하고, 받은 차례를 함께 쓰는 기록(`_pLog`)에 자기 번호로 남긴다. 처리 안에서 다른 구독을 풀게 할 수 있다(`_pReleaseOnHit`).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/Types.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectEvents.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /** @brief 맞음 · 겹침 진입을 구독해 받은 차례를 남깁니다(파일 머리말). */
    REFLECT( HideInMenu )
    struct EventListenerTestComponent : public Component
    {
        REFLECT_BODY();

        vector<uint32>* _pLog{ nullptr };          ///< 맞음을 받을 때마다 `_id` 를 넣는다(여러 컴포넌트가 함께 쓴다)
        Subscription*   _pReleaseOnHit{ nullptr }; ///< 맞음을 받으면 풀 구독(자기 것 · 남의 것)
        uint32          _id{ 0 };
        uint32          _overlapBeginCount{ 0 };
        Subscription    _hitSubscription;
        Subscription    _overlapBeginSubscription;

        void onRegister( GameObjectManager& manager ) override
        {
            Component::onRegister( manager );
            _hitSubscription          = GameObjectEvents::listen<&EventListenerTestComponent::handleHit>( *this, &GameObjectEvents::_hitReceived );
            _overlapBeginSubscription = GameObjectEvents::listen<&EventListenerTestComponent::handleOverlapBegin>( *this, &GameObjectEvents::_overlapBegin );
        }

        void handleHit( const HitInfo& hit )
        {
            (void)hit;
            if ( _pLog != nullptr )
                _pLog->push_back( _id );
            if ( _pReleaseOnHit != nullptr )
                _pReleaseOnHit->reset();
        }

        void handleOverlapBegin( const OverlapInfo& overlap )
        {
            (void)overlap;
            ++_overlapBeginCount;
        }
    };
} // namespace sw
