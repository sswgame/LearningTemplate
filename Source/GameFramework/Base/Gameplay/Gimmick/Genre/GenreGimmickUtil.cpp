#include "pch.h"

#include "GameFramework/Base/Gameplay/Gimmick/Genre/GenreGimmickUtil.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Gameplay/Interaction/InteractableComponent.h"

namespace sw
{
    int32 GenreGimmickUtil::toSteps( float32 seconds, int32 minSteps ) { return MathUtil::max( minSteps, static_cast<int32>( MathUtil::round( seconds / kStepTime ) ) ); }

    void GenreGimmickUtil::setBodyActive( GameObject& object, bool bActive, const Component* pKeep )
    {
        GameObjectManager* pManager = object.getManager();
        if ( pManager == nullptr )
            return;
        // 몸 = 씬 컴포넌트(콜라이더 · 메시 · 스프라이트 · 빛). 논리 컴포넌트(센서 · 회로 · 이 기믹)는 켜 둔 채로 시간을 센다.
        const GameObjectHandle handle    = object.getHandle();
        const uint64           keepId    = pKeep != nullptr ? pKeep->getComponentId() : 0;
        GameObjectManager*     pResolver = pManager;
        pManager->executeOrDeferPostTick( [pResolver, handle, keepId, bActive]()
        {
            GameObject* pObject = pResolver->resolveGameObject( handle );
            if ( pObject == nullptr )
                return;
            for ( Component* pComp : pObject->getComponents() )
            {
                if ( pComp != nullptr && pComp->isSceneComponent() && pComp->getComponentId() != keepId )
                    pComp->setActive( bActive );
            }
        } );
    }

    void GenreGimmickUtil::setInteractableEnabled( GameObject& object, bool bEnabled )
    {
        GameObjectManager* pManager = object.getManager();
        if ( pManager == nullptr )
            return;
        const GameObjectHandle handle = object.getHandle();
        pManager->executeOrDeferPostTick( [pManager, handle, bEnabled]()
        {
            GameObject*            pObject       = pManager->resolveGameObject( handle );
            InteractableComponent* pInteractable = pObject != nullptr ? pObject->getComponent<InteractableComponent>() : nullptr;
            if ( pInteractable != nullptr )
                pInteractable->setEnabled( bEnabled );
        } );
    }
} // namespace sw
