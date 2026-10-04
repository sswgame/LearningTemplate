#include "pch.h"

#include "GameFramework/Gimmick/GimmickDamageUtil.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Framework/GameEventUtil.h"
#include "GameFramework/Gimmick/GimmickEvents.h"
#include "GameFramework/Gimmick/GimmickSensorComponent.h"

namespace sw
{
    void GimmickDamageUtil::applyDamage( GameObject& target, const GameObject* pSource, const hashed_string& kind, float32 amount )
    {
        if ( amount <= 0.0f )
            return;
        GimmickDamageEvent event;
        event._target               = target.getHandle();
        event._source               = pSource != nullptr ? pSource->getHandle() : GameObjectHandle{};
        event._kind                 = kind;
        event._amount               = amount;
        GameObjectManager* pManager = target.getManager();
        if ( pManager == nullptr )
        {
            GimmickSensorComponent* pSensor = target.getComponent<GimmickSensorComponent>();
            if ( pSensor != nullptr )
                pSensor->applyDamage( amount );
            GameEventUtil::send( event );
            return;
        }
        pManager->executeOrDeferPostTick( [pManager, event]()
        {
            GameObject*             pTarget = pManager->resolveGameObject( event._target );
            GimmickSensorComponent* pSensor = pTarget != nullptr ? pTarget->getComponent<GimmickSensorComponent>() : nullptr;
            if ( pSensor != nullptr )
                pSensor->applyDamage( event._amount );
            GameEventUtil::send( event );
        } );
    }
} // namespace sw
