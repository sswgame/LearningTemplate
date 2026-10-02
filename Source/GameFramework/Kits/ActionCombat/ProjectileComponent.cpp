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
            // 표시만 하면 파괴 목록에 들어가지 않아 오브젝트가 풀로 돌아오지 않는다.
            pOwner->destroy();
            return;
        }

        SceneComponent* pSceneComp = pOwner->getPrimarySceneComponent();
        if ( pSceneComp == nullptr )
            return;

        // 속도는 월드 값이다 — 월드 자리로 옮긴다(돌아간 부모 아래에서 로컬로 더하면 부모의 축을 따라 엉뚱한 방향으로 날았다).
        float3 pos = pSceneComp->getWorldPosition();
        pos._x += _velocity._x * deltaTime;
        pos._y += _velocity._y * deltaTime;
        pSceneComp->setWorldPosition( pos );
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
