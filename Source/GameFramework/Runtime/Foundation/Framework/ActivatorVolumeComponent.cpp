#include "pch.h"

#include "GameFramework/Runtime/Foundation/Framework/ActivatorVolumeComponent.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"

namespace sw
{
    ActivatorVolumeComponent::ActivatorVolumeComponent()
        : _activatorTag{}
        , _activator{}
        , _radius{ 0.0f }
        , _fireCount{ 0 }
        , _bOnce{ true }
        , _bActivatorInside{ false }
    {
        setCanEverTick( true );
    }

    void ActivatorVolumeComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        const bool bNothingToMeasure = _radius <= 0.0f || ( _bOnce && _fireCount > 0 );
        if ( bNothingToMeasure )
            return;
        const GameObject*     pOwner     = getOwner();
        GameObjectManager*    pManager   = pOwner != nullptr ? pOwner->getManager() : nullptr;
        const GameObject*     pActivator = pManager != nullptr ? pManager->resolveGameObject( _activator ) : nullptr;
        const SceneComponent* pSelf      = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
        const SceneComponent* pOther     = pActivator != nullptr ? pActivator->getPrimarySceneComponent() : nullptr;
        if ( pSelf == nullptr || pOther == nullptr )
            return;
        const bool bInside  = float3::getDistance( pSelf->getWorldPosition(), pOther->getWorldPosition() ) <= _radius;
        const bool bEntered = bInside && _bActivatorInside == false;
        _bActivatorInside   = bInside;
        if ( bEntered == false )
            return;
        pManager->executeOrDeferPostTickOnComponent( *this, []( ActivatorVolumeComponent& volume )
        {
            const GameObject*        pVolumeOwner = volume.getOwner();
            const GameObjectManager* pScene       = pVolumeOwner != nullptr ? pVolumeOwner->getManager() : nullptr;
            (void)volume.activate( pScene != nullptr ? pScene->resolveGameObject( volume._activator ) : nullptr );
        } );
    }

    void ActivatorVolumeComponent::onOverlapBegin( const OverlapInfo& overlap )
    {
        Component::onOverlapBegin( overlap );
        // 상대의 트리거(감지 범위)는 활성자가 아니다 — 몸이 들어와야 한다.
        if ( overlap._bOtherTrigger == SW_FALSE )
            (void)activate( overlap._pOther );
    }

    bool ActivatorVolumeComponent::activate( const GameObject* pActivator )
    {
        if ( pActivator == nullptr || ( _bOnce && _fireCount > 0 ) )
            return false;
        if ( _activatorTag.isValid() && pActivator->hasTag( _activatorTag ) == false )
            return false;
        if ( onVolumeActivated( *pActivator ) == false )
            return false;
        ++_fireCount;
        return true;
    }

    void ActivatorVolumeComponent::setProximity( GameObjectHandle activator, float32 radius )
    {
        _activator = activator;
        _radius    = radius;
    }

    void ActivatorVolumeComponent::resetActivation( TagID activatorTag )
    {
        _activatorTag     = activatorTag;
        _fireCount        = 0;
        _bActivatorInside = false;
    }
} // namespace sw
