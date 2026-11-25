#include "pch.h"

#include "Games/Playground/Shooter3D/ShooterEffectComponent.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/Tick/TickContext.h"
#include "Engine/Visual/Component/MeshComponent.h"

namespace sw
{
    ShooterEffectComponent::ShooterEffectComponent()
        : _remaining{}
        , _bShowing{ SW_FALSE }
        , _reserved{ 0 }
    {
        setCanEverTick( true );
    }

    void ShooterEffectComponent::activate( float32 lifetime )
    {
        _remaining.start( lifetime );
        _bShowing = SW_TRUE;
    }

    void ShooterEffectComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        if ( _bShowing == SW_FALSE )
            return;
        (void)_remaining.update( context.getDeltaTime() );
        if ( _remaining.isActive() )
            return;
        hide();
    }

    SpawnStateReset ShooterEffectComponent::resetToSpawnState()
    {
        if ( _bShowing == SW_TRUE )
            hide();
        _remaining.clear();
        return SpawnStateReset::Reset;
    }

    void ShooterEffectComponent::hide()
    {
        _bShowing             = SW_FALSE;
        GameObject*    pOwner = getOwner();
        MeshComponent* pMesh  = pOwner != nullptr ? pOwner->getComponent<MeshComponent>() : nullptr;
        if ( pMesh != nullptr )
            pMesh->setVisible( false );
    }
} // namespace sw
