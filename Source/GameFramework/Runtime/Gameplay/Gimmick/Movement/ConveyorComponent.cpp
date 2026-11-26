#include "pch.h"

#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Tick/TickContext.h"
#include "Engine/Physics/Component/CharacterControllerComponent.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Gameplay/Gimmick/Trigger/GimmickSensorComponent.h"

namespace sw
{
    /**
     * @class ConveyorComponent
     * @brief 컨베이어와 흐르는 물입니다. 같은 오브젝트의 센서에 겹친 것을 `_velocity`(월드, m/s)로 옮깁니다. 틱 쓰기 큐를 거쳐 다른 오브젝트의 위치를 세터로 씁니다.
     *        이동 몸이 스스로 움직이는 게임은 `getSurfaceVelocity` 를 읽어서 더하고 `_bMoveOccupants` 를 끕니다.
     */
    REFLECT( Category = "Gimmick", DisplayName = "Conveyor", Tooltip = "Carries overlapping objects at a surface velocity" )
    class ConveyorComponent : public Component
    {
    public:
        REFLECT_BODY();

        ConveyorComponent();
        virtual ~ConveyorComponent() override = default;

        void onTick( TickContext& context ) override;

        const float3& getSurfaceVelocity() const { return _velocity; }

    private:
        PROPERTY( Category = "Conveyor", DisplayName = "Velocity", Units = "m/s" )
        float3 _velocity;
        PROPERTY( Category = "Conveyor", DisplayName = "Move Occupants", Tooltip = "Move overlapping objects directly" )
        bool _bMoveOccupants;

        SW_TRANSIENT float32 _pendingSeconds; ///< 틱이 쌓고 틱 뒤 옮기기가 쓰는 시간
    };

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
        _pendingSeconds += context.getDeltaTime().count();
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

#include "ConveyorComponent.cpp.gen.inl"
