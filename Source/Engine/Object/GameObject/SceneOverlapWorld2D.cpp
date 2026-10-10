/**
 * @file SceneOverlapWorld2D.cpp
 * @brief 씬 하나의 겹침 월드 — 콜라이더 등록 · step · 겹침 이벤트 나눠 주기입니다.
 */
#include "pch.h"

#include "Engine/Object/GameObject/SceneOverlapWorld2D.h"

#include "Engine/Object/Component/2D/BoxCollider2DComponent.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

namespace sw
{
    SceneOverlapWorld2D::SceneOverlapWorld2D()
        : _physicsWorld{}
        , _listCollider{}
        , _listDelivered{}
        , _listTarget{}
    {
    }

    void SceneOverlapWorld2D::registerCollider( BoxCollider2DComponent* pCollider )
    {
        if ( pCollider == nullptr || pCollider->_colliderIndex != BoxCollider2DComponent::kNotRegistered )
            return;
        pCollider->_colliderIndex = static_cast<uint32>( _listCollider.size() );
        _listCollider.push_back( pCollider );
    }

    void SceneOverlapWorld2D::unregisterCollider( BoxCollider2DComponent* pCollider )
    {
        if ( pCollider == nullptr || pCollider->_colliderIndex >= _listCollider.size() || _listCollider[pCollider->_colliderIndex] != pCollider )
            return;
        BoxCollider2DComponent* pMoved           = _listCollider.back();
        _listCollider[pCollider->_colliderIndex] = pMoved;
        pMoved->_colliderIndex                   = pCollider->_colliderIndex;
        _listCollider.pop_back();
        pCollider->_colliderIndex = BoxCollider2DComponent::kNotRegistered;
    }

    void SceneOverlapWorld2D::step( GameObjectManager& manager, float32 deltaTime )
    {
        // 바디를 한 번에 맞춘다 — 틱 · 트랜스폼 적용이 끝난 뒤라 모두 같은 프레임의 자리를 본다. 꺼진 콜라이더는 빠진다(겹침이 끝난다).
        for ( BoxCollider2DComponent* pCollider : _listCollider )
        {
            pCollider->syncPhysicsBody();
        }
        _physicsWorld.step( deltaTime );

        const vector<PhysicsOverlapEvent>& listEvent = _physicsWorld.getOverlapEvents();
        if ( listEvent.empty() )
            return;
        // 이벤트 처리가 콜라이더를 만들거나 지워도 이 목록은 다음 step 까지 그대로지만, 같은 프레임에 다시 step 할 일은 없게 베껴 둔다.
        _listDelivered.assign( listEvent.begin(), listEvent.end() );
        // 한 쪽에서 본 겹침 — 상대 오브젝트와 어느 콜라이더끼리였는지(트리거 여부)를 함께 넘긴다.
        auto deliver = [this, &manager]( uint64 selfID, uint64 otherID, const PhysicsOverlapEvent& event, bool bSelfTrigger, bool bOtherTrigger )
        {
            GameObject* pSelf = manager.findGameObjectByID( selfID );
            if ( pSelf == nullptr || pSelf->isActiveInHierarchy() == false )
                return;
            OverlapInfo overlap;
            overlap._pOther        = manager.findGameObjectByID( otherID );
            overlap._hitFraction   = event._hitFraction;
            overlap._bSelfTrigger  = bSelfTrigger ? SW_TRUE : SW_FALSE;
            overlap._bOtherTrigger = bOtherTrigger ? SW_TRUE : SW_FALSE;
            // 처리가 컴포넌트를 붙이고 뗄 수 있으므로 목록을 베껴 돈다.
            _listTarget.assign( pSelf->getComponents().begin(), pSelf->getComponents().end() );
            for ( Component* pComp : _listTarget )
            {
                if ( pComp == nullptr || pComp->isPendingDestroy() || pComp->isSelfActive() == false )
                    continue;
                if ( event._bBegin == SW_TRUE )
                    pComp->onOverlapBegin( overlap );
                else
                    pComp->onOverlapEnd( overlap );
            }
        };
        for ( const PhysicsOverlapEvent& event : _listDelivered )
        {
            const bool bTriggerA = event._bTriggerA == SW_TRUE;
            const bool bTriggerB = event._bTriggerB == SW_TRUE;
            deliver( event._objectA, event._objectB, event, bTriggerA, bTriggerB );
            deliver( event._objectB, event._objectA, event, bTriggerB, bTriggerA );
        }
        _listDelivered.clear();
        _listTarget.clear();
    }
} // namespace sw
