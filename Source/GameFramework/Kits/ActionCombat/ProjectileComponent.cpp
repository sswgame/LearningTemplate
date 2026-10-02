#include "pch.h"

#include "GameFramework/Kits/ActionCombat/ProjectileComponent.h"

#include "Engine/Object/Component/2D/BoxCollider2DComponent.h"
#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Kits/ActionCombat/UnitStatsComponent.h"

namespace sw
{
    ProjectileComponent::ProjectileComponent()
        : _velocity{ 0.0f, 0.0f }
        , _instigator{}
        , _damage{ 0 }
        , _pierceCount{ 0 }
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
        {
            pOwner->addTag( "Bullet"_tag );
            // 투사체는 빠르다 — 한 프레임에 얇은 적을 건너뛰어도 지나간 길에서 맞게 콜라이더를 연속 충돌로 둔다(언리얼 투사체 이동이 늘 쓸며 가는 것과 같다).
            BoxCollider2DComponent* pCollider = pOwner->getComponent<BoxCollider2DComponent>();
            if ( pCollider != nullptr )
                pCollider->setContinuous( true );
        }

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

    void ProjectileComponent::onOverlapBegin( GameObject* pOther )
    {
        Component::onOverlapBegin( pOther );

        // 사라지기로 한 뒤의 겹침(같은 step 에서 더 늦게 닿은 것)은 오지 않는다 — 파괴가 컴포넌트마다 삭제 표시를 세우고 전달이 그것을 건너뛴다.
        GameObject* pOwner = getOwner();
        if ( pOwner == nullptr || pOther == nullptr )
            return;

        // 쏜 쪽과 거기 붙은 것(총 · 손)은 지나친다. 쏜 쪽이 이미 사라졌으면 풀리지 않으므로 지나칠 것도 없다.
        GameObjectManager* pManager    = pOwner->getManager();
        const GameObject*  pInstigator = ( pManager != nullptr ) ? pManager->resolveGameObject( _instigator ) : nullptr;
        if ( pOther->isDescendantOf( pInstigator ) )
            return;
        // 다른 투사체도 지나친다 — 한 자리에서 퍼지는 산탄은 첫 step 에 서로 겹쳐 있다.
        if ( pOther->getComponent<ProjectileComponent>() != nullptr )
            return;

        UnitStatsComponent* pStats = pOther->getComponent<UnitStatsComponent>();
        if ( pStats != nullptr )
        {
            pStats->takeDamage( _damage, _instigator );
            if ( _pierceCount > 0 )
            {
                --_pierceCount;
                return;
            }
        }
        // 유닛이든 벽이든, 레이어가 부딪히게 둔 것에 닿았으면 멈춘다. 파괴는 이 틱 끝(`processDeferredDestruction`)에 놓인다.
        pOwner->destroy();
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

    void ProjectileComponent::setInstigator( GameObjectHandle instigator )
    {
        _instigator = instigator;
    }

    void ProjectileComponent::setPierceCount( int32 pierceCount )
    {
        _pierceCount = pierceCount;
    }
} // namespace sw
