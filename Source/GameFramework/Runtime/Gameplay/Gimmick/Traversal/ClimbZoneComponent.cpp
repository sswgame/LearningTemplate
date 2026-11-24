#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Gimmick/Traversal/ClimbZoneComponent.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"

#include "GameFramework/Runtime/Gameplay/Gimmick/Trigger/GimmickSensorComponent.h"

namespace sw
{
    ClimbZoneComponent::ClimbZoneComponent()
        : _axis{ 0.0f, 1.0f, 0.0f }
        , _climbSpeed{ 3.0f }
        , _lateralSpeed{ 0.0f }
        , _bSwing{ false }
    {
    }

    bool ClimbZoneComponent::contains( GameObjectHandle object ) const
    {
        const GameObject*             pOwner  = getOwner();
        const GimmickSensorComponent* pSensor = pOwner != nullptr ? pOwner->getTickSafeComponent<GimmickSensorComponent>() : nullptr;
        if ( pSensor == nullptr )
            return false;
        for ( const GameObjectHandle& occupant : pSensor->getOccupants() )
        {
            if ( occupant == object )
                return true;
        }
        return false;
    }

    void ClimbZoneComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        if ( _axis.getLengthSquared() <= 0.0f )
            SW_LOG_ERROR_ID( "ClimbZone.ZeroAxis", "Climb zone '%#' has a zero axis - nothing can climb it", getOwner() != nullptr ? getOwner()->getName().c_str() : "" );
    }

    float3 ClimbZoneComponent::computeAxis() const
    {
        return _axis.getLengthSquared() > 0.0f ? _axis.normalize() : float3{};
    }

    float3 ClimbZoneComponent::computeFacing() const
    {
        const GameObject*     pOwner  = getOwner();
        const SceneComponent* pScene  = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
        const float3          forward = pScene != nullptr ? float3::transformVector( float3{ 0.0f, 0.0f, 1.0f }, pScene->getWorldMatrix() ) : float3{ 0.0f, 0.0f, 1.0f };
        const float3          flat{ forward._x, 0.0f, forward._z };
        return flat.getLengthSquared() > 0.0f ? flat.normalize() : float3{ 0.0f, 0.0f, 1.0f };
    }

    const ClimbZoneComponent* ClimbZoneComponent::findClimbZone( const GameObjectManager& manager, GameObjectHandle object )
    {
        for ( const ClimbZoneComponent* pZone : manager.getComponentRegistry().getAll<ClimbZoneComponent>() )
        {
            const bool bUsableClimbZone = pZone->isPendingDestroy() == false && pZone->isActive() && pZone->contains( object );
            if ( bUsableClimbZone )
                return pZone;
        }
        return nullptr;
    }
} // namespace sw
