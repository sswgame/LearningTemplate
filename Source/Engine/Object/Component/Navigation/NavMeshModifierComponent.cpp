#include "pch.h"

#include "Engine/Object/Component/Navigation/NavMeshModifierComponent.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/SceneNavigation.h"

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
        if ( pOwner != nullptr && pOwner->getManager() != nullptr && pOwner->getWorldBox( box ) )
            pOwner->getManager()->getSceneNavigation().invalidateArea( box, true );
    }
} // namespace sw
