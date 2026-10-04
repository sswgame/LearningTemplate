#include "pch.h"

#include "GameFramework/Kits/ActionCombat/MeleeHitboxComponent.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Container/VectorUtil.h"

#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Kits/ActionCombat/UnitStatsComponent.h"

namespace sw
{
    MeleeHitboxComponent::MeleeHitboxComponent()
        : _listHitTarget{}
        , _listOverlapping{}
        , _damage{ 0 }
        , _duration{ 0.0f }
        , _currentDuration{ 0.0f }
        , _bIsAttacking{ false }
    {
    }

    void MeleeHitboxComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        setTickGroup( TickGroup::DuringPhysics );

        _currentDuration = 0.0f;
    }

    void MeleeHitboxComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );

        if ( _bIsAttacking == false )
            return;

        _currentDuration += deltaTime;
        if ( _duration > 0.0f && _currentDuration >= _duration )
        {
            _bIsAttacking    = false;
            _currentDuration = 0.0f;
        }
    }

    void MeleeHitboxComponent::onOverlapBegin( const OverlapInfo& overlap )
    {
        Component::onOverlapBegin( overlap );
        // 상대의 감지 범위(트리거)에 닿은 것은 맞은 것이 아니다 — 몸(막는 콜라이더)에 닿아야 한다.
        if ( overlap._pOther == nullptr || overlap._bOtherTrigger == SW_TRUE )
            return;

        // 접촉마다 하나씩 든다 — 상대가 콜라이더 둘로 겹쳤다가 하나만 떨어져도 남은 접촉이 남는다.
        _listOverlapping.push_back( overlap._pOther->getHandle() );
        if ( _bIsAttacking )
            deliverHit( overlap._pOther );
    }

    void MeleeHitboxComponent::onOverlapEnd( const OverlapInfo& overlap )
    {
        Component::onOverlapEnd( overlap );
        if ( overlap._bOtherTrigger == SW_TRUE )
            return; // 시작에서 들지 않았다
        if ( overlap._pOther != nullptr )
        {
            (void)VectorUtil::removeSingleSwap( _listOverlapping, overlap._pOther->getHandle() ); // 없으면 잊을 것도 없다
            return;
        }

        // 상대가 사라져 누구인지 모른다 — 더는 풀리지 않는 것을 모두 덜어 낸다.
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = ( pOwner != nullptr ) ? pOwner->getManager() : nullptr;
        if ( pManager == nullptr )
            return;
        for ( size_t index = _listOverlapping.size(); index > 0; --index )
        {
            if ( pManager->resolveGameObject( _listOverlapping[index - 1] ) == nullptr )
                VectorUtil::removeAtSwap( _listOverlapping, index - 1 );
        }
    }

    void MeleeHitboxComponent::beginAttack( int32 damage, float32 duration )
    {
        _damage          = damage;
        _duration        = duration;
        _currentDuration = 0.0f;
        _bIsAttacking    = true;
        _listHitTarget.clear();

        // 휘두르기 전부터 판정 안에 서 있던 유닛도 맞는다 — 그 겹침의 시작 이벤트는 이미 지나갔다(언리얼 `GetOverlappingActors`).
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = ( pOwner != nullptr ) ? pOwner->getManager() : nullptr;
        if ( pManager == nullptr )
            return;
        for ( const GameObjectHandle overlapping : _listOverlapping )
        {
            GameObject* pTarget = pManager->resolveGameObject( overlapping );
            if ( pTarget != nullptr )
                deliverHit( pTarget );
        }
    }

    GameObject* MeleeHitboxComponent::findAttackerRoot() const
    {
        GameObject* pRoot = getOwner();
        while ( pRoot != nullptr && pRoot->getParent() != nullptr )
            pRoot = pRoot->getParent();
        return pRoot;
    }

    void MeleeHitboxComponent::deliverHit( GameObject* pTarget )
    {
        // 공격자 자신과 그 계층(몸 · 다른 판정)은 맞지 않는다.
        GameObject* pAttacker = findAttackerRoot();
        if ( pTarget->isDescendantOf( pAttacker ) )
            return;
        UnitStatsComponent* pStats = pTarget->getComponent<UnitStatsComponent>();
        if ( pStats == nullptr )
            return;

        // 한 번 휘두를 때 유닛마다 한 번 — 판정을 나갔다 다시 들어와도 다시 맞지 않는다.
        const GameObjectHandle target = pTarget->getHandle();
        if ( std::find( _listHitTarget.begin(), _listHitTarget.end(), target ) != _listHitTarget.end() )
            return;
        _listHitTarget.push_back( target );
        pStats->takeDamage( _damage, ( pAttacker != nullptr ) ? pAttacker->getHandle() : GameObjectHandle{} );
    }
} // namespace sw
