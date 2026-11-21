/**
 * @file OverlapRecorderTestComponent.h
 * @brief 트리거 계약 테스트(`TestTriggerOverlapContract.cpp`)가 트리거 오브젝트에 다는 기록 컴포넌트입니다 — 개발 구성만 코드젠하고 "Add Component" 메뉴에 나오지 않는다.
 * @details 받은 진입 · 이탈 수와 차례(`B` · `E`)를 남긴다. 처리 안에서 상대를 지우게 하면(`_bDestroyOtherOnBegin`) 반복 중 변경이 안전한지 본다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectEvents.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /** @brief 겹침 진입 · 이탈을 세고 차례를 남깁니다(파일 머리말). 상대의 트리거 겹침은 세지 않는다(몸만). */
    REFLECT( HideInMenu )
    struct OverlapRecorderTestComponent : public Component
    {
        REFLECT_BODY();

        uint32        _beginCount{ 0 };
        uint32        _endCount{ 0 };
        uint32        _collisionBeginCount{ 0 };      ///< 막는 접촉 시작 수(바디 쌍마다 — 충격 단위)
        vector<uint8> _listOrder{};                   ///< 'B' · 'E' 와 상대 오브젝트 id 의 낮은 바이트가 번갈아 든다
        bool          _bDestroyOtherOnBegin{ false }; ///< NOLINT(sw-bool-flag): 테스트 칸 — 비트필드는 기본값을 둘 수 없다
        Subscription  _overlapBeginSubscription;
        Subscription  _overlapEndSubscription;
        Subscription  _collisionBeginSubscription;

        void onRegister( GameObjectManager& manager ) override
        {
            Component::onRegister( manager );
            _overlapBeginSubscription   = GameObjectEvents::listen<&OverlapRecorderTestComponent::handleOverlapBegin>( *this, &GameObjectEvents::_overlapBegin );
            _overlapEndSubscription     = GameObjectEvents::listen<&OverlapRecorderTestComponent::handleOverlapEnd>( *this, &GameObjectEvents::_overlapEnd );
            _collisionBeginSubscription = GameObjectEvents::listen<&OverlapRecorderTestComponent::handleCollisionBegin>( *this, &GameObjectEvents::_collisionBegin );
        }

        void handleOverlapBegin( const OverlapInfo& overlap )
        {
            if ( overlap._bOtherTrigger == SW_TRUE )
                return;
            ++_beginCount;
            record( 'B', overlap );
            if ( _bDestroyOtherOnBegin && overlap._pOther != nullptr && overlap._pOther->getMutableManager() != nullptr )
                overlap._pOther->getMutableManager()->destroyGameObject( overlap._pOther );
        }

        void handleOverlapEnd( const OverlapInfo& overlap )
        {
            if ( overlap._bOtherTrigger == SW_TRUE )
                return;
            ++_endCount;
            record( 'E', overlap );
        }

        void handleCollisionBegin( const CollisionInfo& collision )
        {
            (void)collision;
            ++_collisionBeginCount;
        }

    private:
        void record( uint8 phase, const OverlapInfo& overlap )
        {
            _listOrder.push_back( phase );
            _listOrder.push_back( overlap._pOther != nullptr ? static_cast<uint8>( overlap._pOther->getObjectID() & 0xFFu ) : uint8{ 0 } );
        }
    };
} // namespace sw
