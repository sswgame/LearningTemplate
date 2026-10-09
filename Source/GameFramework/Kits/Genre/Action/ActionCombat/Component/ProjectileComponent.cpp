#include "pch.h"

#include "GameFramework/Kits/Genre/Action/ActionCombat/Component/ProjectileComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/2D/BoxCollider2DComponent.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Foundation/Utility/Time/LifeSpanUtil.h"
#include "GameFramework/Kits/Genre/Action/ActionCombat/Component/UnitStatsComponent.h"

namespace sw
{
    SW_LOG_CALLER( "ProjectileComponent" );

    ProjectileComponent::ProjectileComponent()
        : _velocity{ 0.0f, 0.0f }
        , _instigator{}
        , _damage{ 0 }
        , _pierceCount{ 0 }
        , _lifeTime{ 0.0f }
        , _currentLife{ 0.0f }
        , _bInterceptor{ false }
    {
    }

    void ProjectileComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        setTickGroup( TickGroup::DuringPhysics );

        GameObject* pOwner = getOwner();
        if ( pOwner != nullptr )
        {
            // 투사체는 빠르다 — 한 프레임에 얇은 적을 건너뛰어도 지나간 길에서 맞게 콜라이더를 연속 충돌로 둔다(언리얼 투사체 이동이 늘 쓸며 가는 것과 같다).
            // 콜라이더가 없으면 겹침이 오지 않는다 — 날기만 하고 아무것도 맞히지 못한다. 조용히 두면 "총알이 안 맞는다" 가 원인 없이 보인다.
            BoxCollider2DComponent* pCollider = pOwner->getComponent<BoxCollider2DComponent>();
            if ( pCollider != nullptr )
                pCollider->setContinuous( true );
            else
                SW_LOG_WARNING( "Projectile '%#' has no BoxCollider2DComponent on its object - it moves but can never hit anything", pOwner->getName().c_str() );
        }

        // 흐른 수명은 처음으로 되돌리지 않는다 — 날던 중에 상태를 다시 읽은 투사체(플레이 중 되돌리기 · 핫 리로드)가 수명을 다시 시작하지 않게.
        // 새로 만든 투사체는 생성자가 0 으로 둔다.
        _currentLife = MathUtil::max( _currentLife, 0.0f );
    }

    void ProjectileComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );

        const bool  bExpired = LifeSpanUtil::advance( _currentLife, _lifeTime, deltaTime );
        GameObject* pOwner   = getOwner();
        if ( pOwner == nullptr )
            return;

        if ( bExpired )
        {
            // 표시만 하면 파괴 목록에 들어가지 않아 오브젝트가 풀로 돌아오지 않는다.
            pOwner->destroy();
            return;
        }

        SceneComponent* pSceneComp = pOwner->getPrimarySceneComponent();
        if ( pSceneComp == nullptr )
            return;

        // 속도는 월드 값이다 — 월드 자리로 옮긴다(돌아간 부모 아래에서 로컬로 더하면 부모의 축을 따라 엉뚱한 방향으로 난다).
        float3 pos = pSceneComp->getWorldPosition();
        pos._x += _velocity._x * deltaTime;
        pos._y += _velocity._y * deltaTime;
        pSceneComp->setWorldPosition( pos );
    }

    void ProjectileComponent::onOverlapBegin( const OverlapInfo& overlap )
    {
        Component::onOverlapBegin( overlap );

        // 사라지기로 한 뒤의 겹침(같은 step 에서 더 늦게 닿은 것)은 오지 않는다 — 파괴가 컴포넌트마다 삭제 표시를 세우고 전달이 그것을 건너뛴다.
        GameObject* pOwner = getOwner();
        GameObject* pOther = overlap._pOther;
        if ( pOwner == nullptr || pOther == nullptr )
            return;
        // 상대가 트리거(감지 범위 · 구역 볼륨)면 막히지도 맞히지도 않는다 — 안 그러면 적의 감지 범위가 총알을 먹는다.
        if ( overlap._bOtherTrigger == SW_TRUE )
            return;

        // 쏜 쪽과 거기 붙은 것(총 · 손)은 지나친다. 쏜 쪽이 이미 사라졌으면 풀리지 않으므로 지나칠 것도 없다.
        GameObjectManager* pManager    = pOwner->getManager();
        const GameObject*  pInstigator = ( pManager != nullptr ) ? pManager->resolveGameObject( _instigator ) : nullptr;
        if ( pOther->isDescendantOf( pInstigator ) )
            return;

        // 다른 투사체는 요격탄만 맞힌다 — 맞힌 투사체는 지운다(그 투사체가 이 겹침을 받아도 요격탄이 아니면 지나친다).
        const ProjectileComponent* pOtherProjectile = pOther->getComponent<ProjectileComponent>();
        if ( pOtherProjectile != nullptr && canIntercept( *pOtherProjectile ) == false )
            return;
        if ( pOtherProjectile != nullptr )
            pOther->destroy();

        UnitStatsComponent* pStats = pOther->getComponent<UnitStatsComponent>();
        if ( pStats != nullptr )
            pStats->takeDamage( _damage, _instigator );
        // 맞힌 것(유닛 · 요격한 투사체)은 관통 수가 남았으면 하나 줄이고 계속 난다. 벽은 늘 멈춘다.
        const bool bHitTarget = pStats != nullptr || pOtherProjectile != nullptr;
        if ( bHitTarget && _pierceCount > 0 )
        {
            --_pierceCount;
            return;
        }
        // 레이어가 부딪히게 둔 것에 닿았으면 멈춘다. 파괴는 이 틱 끝(`processDeferredDestruction`)에 놓인다.
        pOwner->destroy();
    }

    bool ProjectileComponent::canIntercept( const ProjectileComponent& other ) const
    {
        if ( _bInterceptor == false )
            return false;
        // 같은 쪽이 쏜 것끼리는 맞지 않는다 — 쏜 쪽을 모르면(무효 핸들) 편도 모르므로 맞힌다.
        const bool bSameInstigator = _instigator.isValid() && other._instigator == _instigator;
        return bSameInstigator == false;
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

    void ProjectileComponent::setInterceptor( bool bInterceptor )
    {
        _bInterceptor = bInterceptor;
    }
} // namespace sw
