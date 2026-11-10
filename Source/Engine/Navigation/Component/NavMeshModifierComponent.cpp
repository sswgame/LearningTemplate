#include "pch.h"

#include "Engine/Navigation/Component/NavMeshModifierComponent.h"

#include "Engine/Navigation/Scene/NavAreaNameValidation.h"
#include "Engine/Navigation/Scene/SceneNavigation.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Reflection/ReflectionValidation.h"

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
            pOwner->getManager()->getSubsystem<SceneNavigation>().invalidateArea( box, true );
    }
} // namespace sw

namespace sw
{
    void NavMeshModifierComponent::validateAreaName( ValidationContext& context ) const
    {
        NavAreaNameValidation::validate( context, "_areaName", _areaName );
    }
} // namespace sw
