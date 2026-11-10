#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Gimmick/GimmickDamageUtil.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Runtime/Foundation/Framework/GameEventUtil.h"
#include "GameFramework/Runtime/Gameplay/Gimmick/GimmickEvents.h"
#include "GameFramework/Runtime/Gameplay/Gimmick/GimmickSensorComponent.h"

namespace sw
{
    void GimmickDamageUtil::applyDamage( const GameObject& target, const GameObject* pSource, const hashed_string& kind, float32 amount )
    {
        if ( amount <= 0.0f )
            return;
        GimmickDamageEvent event;
        event._target                     = target.getHandle();
        event._source                     = pSource != nullptr ? pSource->getHandle() : GameObjectHandle{};
        event._kind                       = kind;
        event._amount                     = amount;
        const GameObjectManager* pManager = target.getManager(); // 틱에서도 부른다 — 피해는 명령 버퍼로 틱 뒤에 넣는다
        if ( pManager == nullptr )
        {
            GimmickSensorComponent* pSensor = target.getComponent<GimmickSensorComponent>();
            if ( pSensor != nullptr )
                pSensor->applyDamage( amount );
            GameEventUtil::send( event );
            return;
        }
        pManager->executeOrDeferPostTickOnScene( [event]( GameObjectManager& sceneManager )
        {
            GameObject*             pTarget = sceneManager.resolveGameObject( event._target );
            GimmickSensorComponent* pSensor = pTarget != nullptr ? pTarget->getComponent<GimmickSensorComponent>() : nullptr;
            if ( pSensor != nullptr )
                pSensor->applyDamage( event._amount );
            GameEventUtil::send( event );
        } );
    }
} // namespace sw
