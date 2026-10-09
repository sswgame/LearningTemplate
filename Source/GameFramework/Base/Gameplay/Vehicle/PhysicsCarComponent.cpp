#include "pch.h"

#include "GameFramework/Base/Gameplay/Vehicle/PhysicsCarComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/Physics/WheeledVehicleComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"

#include "GameFramework/Base/Actor/Control/ControlIntent.h"
#include "GameFramework/Base/Actor/Control/PawnComponent.h"

namespace sw
{
    PhysicsCarComponent::PhysicsCarComponent()
        : _handBrakeButton{ "HandBrake" }
        , _reverseSpeed{ 0.5f }
        , _handBrakeIndex{ -1 }
    {
        setCanEverTick( true );
    }

    void PhysicsCarComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        setTickGroup( TickGroup::PrePhysics );
        const GameObject*    pOwner = getOwner();
        const PawnComponent* pPawn  = pOwner != nullptr ? pOwner->getComponent<PawnComponent>() : nullptr;
        _handBrakeIndex             = pPawn != nullptr ? pPawn->findButton( _handBrakeButton ) : -1;
    }

    PhysicsCarInput PhysicsCarComponent::toCarInput( const ControlIntent& intent, const float3& carForward, float32 forwardSpeed, float32 reverseSpeed,
                                                     int32 handBrakeIndex )
    {
        // 차 앞(XZ)과 오른쪽(앞을 시계 방향으로 90°) — 요 0 이면 앞 +Z · 오른쪽 +X.
        const float32   forwardLength = MathUtil::sqrt( carForward._x * carForward._x + carForward._z * carForward._z );
        const float32   forwardX      = forwardLength > 0.0f ? carForward._x / forwardLength : 0.0f;
        const float32   forwardZ      = forwardLength > 0.0f ? carForward._z / forwardLength : 1.0f;
        const float3    worldMove     = intent.computeWorldMove();
        const float32   pedal         = MathUtil::clamp( worldMove._x * forwardX + worldMove._z * forwardZ, -1.0f, 1.0f );
        PhysicsCarInput input{};
        input._right     = MathUtil::clamp( worldMove._x * forwardZ - worldMove._z * forwardX, -1.0f, 1.0f );
        input._handBrake = intent.isDown( handBrakeIndex ) ? 1.0f : 0.0f;
        if ( pedal < 0.0f && forwardSpeed > reverseSpeed )
            input._brake = -pedal; // 앞으로 가는 중 — 먼저 선다
        else
            input._forward = pedal;
        return input;
    }

    void PhysicsCarComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        GameObject*              pOwner   = getOwner();
        const PawnComponent*     pPawn    = pOwner != nullptr ? pOwner->getComponent<PawnComponent>() : nullptr;
        WheeledVehicleComponent* pVehicle = pOwner != nullptr ? pOwner->getComponent<WheeledVehicleComponent>() : nullptr;
        const SceneComponent*    pRoot    = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
        if ( pPawn == nullptr || pVehicle == nullptr || pRoot == nullptr )
            return;
        const float3          forward = float3::transformVector( float3{ 0.0f, 0.0f, 1.0f }, pRoot->getWorldMatrix() );
        const PhysicsCarInput input   = toCarInput( pPawn->getIntent(), forward, pVehicle->getVehicleState()._forwardSpeed, _reverseSpeed, _handBrakeIndex );
        pVehicle->setDriverInput( input._forward, input._right, input._brake, input._handBrake );
    }
} // namespace sw
