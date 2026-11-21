#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Gimmick/Genre/ConveyorComponent.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Tick/TickContext.h"
#include "Engine/Physics/Component/CharacterControllerComponent.h"

#include "GameFramework/Runtime/Gameplay/Gimmick/GimmickSensorComponent.h"

namespace sw
{
    ConveyorComponent::ConveyorComponent()
        : _velocity{ 2.0f, 0.0f, 0.0f }
        , _bMoveOccupants{ true }
        , _pendingSeconds{ 0.0f }
    {
    }

    void ConveyorComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        const GameObject*             pOwner             = getOwner();
        const GimmickSensorComponent* pSensor            = pOwner != nullptr ? pOwner->getComponent<GimmickSensorComponent>() : nullptr;
        const bool                    bNoOccupantsToMove = _bMoveOccupants == false || pSensor == nullptr;
        if ( bNoOccupantsToMove )
            return;
        // 겹친 것이 자기 틱에서 쓴 위치 위에 더한다. 틱 뒤의 게임 스레드에서 처리한다. 같은 틱에서 쓰면 그쪽의 쓰기와 순서가 정해지지 않기 때문이다.
        _pendingSeconds += context.getDeltaTime();
        context.deferPostTickOnComponent( *this, []( ConveyorComponent& conveyor, GameObjectManager& manager )
        {
            const float3 offset                       = conveyor._velocity * conveyor._pendingSeconds;
            const float3 velocity                     = conveyor._velocity;
            conveyor._pendingSeconds                  = 0.0f;
            const GimmickSensorComponent* pBeltSensor = conveyor.getOwner()->getComponent<GimmickSensorComponent>();
            if ( pBeltSensor == nullptr )
                return;
            for ( const GameObjectHandle& handle : pBeltSensor->getOccupants() )
            {
                GameObject*                   pOccupant   = manager.resolveGameObject( handle );
                CharacterControllerComponent* pController = pOccupant != nullptr ? pOccupant->getComponent<CharacterControllerComponent>() : nullptr;
                SceneComponent*               pScene      = pOccupant != nullptr ? pOccupant->getPrimarySceneComponent() : nullptr;
                // 컨트롤러에는 면 속도로 넘긴다. 트랜스폼을 직접 옮기면 순간이동이 되어 벽을 통과하기 때문이다.
                if ( pController != nullptr )
                    pController->addSurfaceVelocity( velocity );
                else if ( pScene != nullptr )
                    pScene->setWorldPosition( pScene->getWorldPosition() + offset );
            }
        } );
    }
} // namespace sw
