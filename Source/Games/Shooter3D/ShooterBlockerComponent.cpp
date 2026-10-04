#include "pch.h"

#include "Games/Shooter3D/ShooterBlockerComponent.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"

namespace sw
{
    ShooterBlockerComponent::ShooterBlockerComponent()
        : _halfSize{ 0.5f, 0.5f, 0.5f }
    {
    }

    ShooterArenaBox ShooterBlockerComponent::computeBox() const
    {
        const GameObject*     pOwner = getOwner();
        const SceneComponent* pScene = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
        const float3          center = pScene != nullptr ? pScene->getWorldPosition() : float3{ 0.0f, 0.0f, 0.0f };
        ShooterArenaBox       box{};
        box._min = center - _halfSize;
        box._max = center + _halfSize;
        return box;
    }
} // namespace sw
