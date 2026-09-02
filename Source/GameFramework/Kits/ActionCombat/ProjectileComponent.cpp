#include "pch.h"

#include "GameFramework/Kits/ActionCombat/ProjectileComponent.h"

#include "Engine/Object/Component/TagSystem.h"

namespace sw
{
    ProjectileComponent::ProjectileComponent()
        : _velocity{ 0.0f, 0.0f }
        , _damage{ 0 }
        , _lifeTime{ 0.0f }
        , _currentLife{ 0.0f }
    {
    }

    void ProjectileComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        setTickGroup( TickGroup::DuringPhysics );

        GameObject* pOwner = getOwner();
        if ( pOwner != nullptr )
            pOwner->addTag( "Bullet"_tag );

        _currentLife = 0.0f;
    }

    void ProjectileComponent::onEndPlay()
    {
        Component::onEndPlay();
    }

    void ProjectileComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );

        _currentLife += deltaTime;
        GameObject* pOwner = getOwner();
        if ( pOwner == nullptr )
            return;

        if ( _lifeTime > 0.0f && _currentLife >= _lifeTime )
        {
            pOwner->markPendingKill();
            return;
        }

        SceneComponent* pSceneComp = pOwner->getPrimarySceneComponent();
        if ( pSceneComp == nullptr )
            return;

        float3 pos = pSceneComp->getLocalPosition();
        pos._x += _velocity._x * deltaTime;
        pos._y += _velocity._y * deltaTime;
        pSceneComp->setLocalPosition( pos );
    }

    void ProjectileComponent::setVelocity( const float2& velocity )
    {
        _velocity = velocity;
    }

    void ProjectileComponent::setDamage( int32 damage )
    {
        _damage = damage;
    }

    void ProjectileComponent::setLifeTime( float32 lifeTime )
    {
        _lifeTime = lifeTime;
    }
} // namespace sw
