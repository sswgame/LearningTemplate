#include "pch.h"

#include "Games/Shooter3D/ShooterEffectComponent.h"

#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"

namespace sw
{
    ShooterEffectComponent::ShooterEffectComponent()
        : _remaining{ 0.0f }
        , _bShowing{ SW_FALSE }
        , _reserved{ 0 }
    {
        setCanEverTick( true );
    }

    void ShooterEffectComponent::activate( float32 lifetime )
    {
        _remaining = lifetime;
        _bShowing  = SW_TRUE;
    }

    void ShooterEffectComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        if ( _bShowing == SW_FALSE )
            return;
        _remaining -= deltaTime;
        if ( _remaining > 0.0f )
            return;
        _bShowing             = SW_FALSE;
        GameObject*    pOwner = getOwner();
        MeshComponent* pMesh  = pOwner != nullptr ? pOwner->getComponent<MeshComponent>() : nullptr;
        if ( pMesh != nullptr )
            pMesh->setVisible( false );
    }
} // namespace sw
