#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Gimmick/Genre/GenreGimmickUtil.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Runtime/Gameplay/Interaction/InteractableComponent.h"

namespace sw
{
    int32 GenreGimmickUtil::toSteps( float32 seconds, int32 minSteps )
    {
        return MathUtil::max( minSteps, static_cast<int32>( MathUtil::round( seconds / constant::time::kDefaultFixedStepSeconds ) ) );
    }

    void GenreGimmickUtil::setBodyActive( GameObject& object, bool bActive, const Component* pKeep )
    {
        const GameObjectManager* pManager = object.getManager(); // 틱에서도 부른다 — 켜고 끄기는 명령 버퍼로 틱 뒤에 한다
        if ( pManager == nullptr )
            return;
        // 몸은 씬 컴포넌트(콜라이더, 메시, 스프라이트, 라이트)이다. 논리 컴포넌트(센서, 회로, 이 기믹)는 켜 둔 채로 시간을 센다.
        const GameObjectHandle handle = object.getHandle();
        const uint64           keepID = pKeep != nullptr ? pKeep->getComponentID() : 0;
        pManager->executeOrDeferPostTickOnScene( [handle, keepID, bActive]( GameObjectManager& sceneManager )
        {
            GameObject* pObject = sceneManager.resolveGameObject( handle );
            if ( pObject == nullptr )
                return;
            for ( Component* pComp : pObject->getComponents() )
            {
                const bool bToggleSceneComponent = pComp != nullptr && pComp->isSceneComponent() && pComp->getComponentID() != keepID;
                if ( bToggleSceneComponent )
                    pComp->setActive( bActive );
            }
        } );
    }

    void GenreGimmickUtil::setInteractableEnabled( GameObject& object, bool bEnabled )
    {
        const GameObjectManager* pManager = object.getManager(); // 틱에서도 부른다 — 명령 버퍼로 틱 뒤에 한다
        if ( pManager == nullptr )
            return;
        const GameObjectHandle handle = object.getHandle();
        pManager->executeOrDeferPostTickOnScene( [handle, bEnabled]( GameObjectManager& sceneManager )
        {
            GameObject*            pObject       = sceneManager.resolveGameObject( handle );
            InteractableComponent* pInteractable = pObject != nullptr ? pObject->getComponent<InteractableComponent>() : nullptr;
            if ( pInteractable != nullptr )
                pInteractable->setEnabled( bEnabled );
        } );
    }
} // namespace sw
