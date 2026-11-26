#include "pch.h"

#include "Engine/Navigation/Component/NavMeshModifierComponent.h"

#include "Engine/Navigation/Scene/SceneNavigation.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

namespace sw
{
    NavMeshModifierComponent::NavMeshModifierComponent()
        : _areaName{}
        , _bIgnoreFromBuild{ false }
    {
    }

    void NavMeshModifierComponent::onPropertyChanged( hashed_string propertyName )
    {
        Component::onPropertyChanged( propertyName );
        GameObject* pOwner = getOwner();
        AABB        box{};
        const bool  bHasWorldBox = pOwner != nullptr && pOwner->getManager() != nullptr && pOwner->getWorldBox( box );
        if ( bHasWorldBox )
            pOwner->getMutableManager()->getSubsystem<SceneNavigation>().invalidateArea( box, true );
    }
} // namespace sw
