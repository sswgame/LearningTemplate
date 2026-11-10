#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Vehicle/PhysicsCarComponent.h"

#include "Core/Foundation/Constants.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Physics/Component/WheeledVehicleComponent.h"

#include "GameFramework/Runtime/Character/Control/Intent/ControlIntent.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"

namespace sw
{
    PhysicsCarComponent::PhysicsCarComponent()
        : _handBrakeButton{}
        , _reverseSpeed{ 0.5f }
        , _handBrakeIndex{}
    {
        setCanEverTick( true );
    }

    void PhysicsCarComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        setTickGroup( TickGroup::PrePhysics );
        const GameObject*    pOwner = getOwner();
        const PawnComponent* pPawn  = pOwner != nullptr ? pOwner->getComponent<PawnComponent>() : nullptr;
        _handBrakeIndex             = pPawn != nullptr ? pPawn->findButton( _handBrakeButton ) : nullopt;
    }

    PhysicsCarInput PhysicsCarComponent::toCarInput( const ControlIntent& intent, const float3& carForward, float32 forwardSpeed, float32 reverseSpeed,
                                                     optional<int32> handBrakeIndex )
    {
        const float2    localMove = intent.computeLocalMove( carForward );
        const float32   pedal     = localMove._y;
        PhysicsCarInput input{};
        input._right     = localMove._x;
        input._handBrake = intent.isDown( handBrakeIndex ) ? 1.0f : 0.0f;
        if ( pedal < 0.0f && forwardSpeed > reverseSpeed )
            input._brake = -pedal; // 앞으로 가는 중이므로 먼저 멈춘다
        else
            input._forward = pedal;
        return input;
    }

    void PhysicsCarComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        GameObject*              pOwner           = getOwner();
        const PawnComponent*     pPawn            = pOwner != nullptr ? pOwner->getComponent<PawnComponent>() : nullptr;
        WheeledVehicleComponent* pVehicle         = pOwner != nullptr ? pOwner->getComponent<WheeledVehicleComponent>() : nullptr;
        const SceneComponent*    pRoot            = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
        const bool               bMissingCarParts = pPawn == nullptr || pVehicle == nullptr || pRoot == nullptr;
        if ( bMissingCarParts )
            return;
        const float3          forward = float3::transformVector( float3{ 0.0f, 0.0f, 1.0f }, pRoot->getWorldMatrix() );
        const PhysicsCarInput input   = toCarInput( pPawn->getIntent(), forward, pVehicle->getVehicleState()._forwardSpeed, _reverseSpeed, _handBrakeIndex );
        pVehicle->setDriverInput( input._forward, input._right, input._brake, input._handBrake );
    }
} // namespace sw
