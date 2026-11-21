#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Gimmick/Traversal/ClimbZoneComponent.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"

#include "GameFramework/Runtime/Gameplay/Gimmick/Trigger/GimmickSensorComponent.h"

namespace sw
{
    ClimbZoneComponent::ClimbZoneComponent()
        : _axis{ 0.0f, 1.0f, 0.0f }
        , _climbSpeed{ 3.0f }
        , _bSwing{ false }
    {
    }

    bool ClimbZoneComponent::contains( GameObjectHandle object ) const
    {
        const GameObject*             pOwner  = getOwner();
        const GimmickSensorComponent* pSensor = pOwner != nullptr ? pOwner->getComponent<GimmickSensorComponent>() : nullptr;
        if ( pSensor == nullptr )
            return false;
        for ( const GameObjectHandle& occupant : pSensor->getOccupants() )
        {
            if ( occupant == object )
                return true;
        }
        return false;
    }

    void ClimbZoneComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        manager.getComponentRegistry().add<ClimbZoneComponent>( this );
    }

    void ClimbZoneComponent::onUnregister( GameObjectManager& manager )
    {
        manager.getComponentRegistry().remove<ClimbZoneComponent>( this );
        Component::onUnregister( manager );
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
