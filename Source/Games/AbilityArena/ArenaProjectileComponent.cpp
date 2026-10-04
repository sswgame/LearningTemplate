#include "pch.h"

#include "Games/AbilityArena/ArenaProjectileComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Ability/AbilitySystemComponent.h"
#include "GameFramework/Framework/GameSound.h"

#include "Games/AbilityArena/ArenaDirectorComponent.h"

namespace sw
{
    ArenaProjectileComponent::ArenaProjectileComponent()
        : _director{}
        , _radius{ 0.25f }
        , _unitRadius{ 0.5f }
        , _visualLift{ 0.9f }
        , _spec{}
        , _extraSpec{}
        , _position{ 0.0f, 0.0f, 0.0f }
        , _velocity{ 0.0f, 0.0f, 0.0f }
        , _remainingRange{ 0.0f }
        , _bFromPlayer{ SW_FALSE }
        , _bLaunched{ SW_FALSE }
        , _bFinished{ SW_FALSE }
        , _reserved{ 0 }
    {
        setCanEverTick( true );
    }

    void ArenaProjectileComponent::launch( GameObjectHandle director, const float3& position, const float3& velocity, float32 range, const GameplayEffectSpec& spec,
                                           const GameplayEffectSpec& extraSpec, bool bFromPlayer )
    {
        _director             = director;
        _position             = position;
        _velocity             = velocity;
        _remainingRange       = range;
        _spec                 = spec;
        _extraSpec            = extraSpec;
        _bFromPlayer          = bFromPlayer ? SW_TRUE : SW_FALSE;
        _bLaunched            = SW_TRUE;
        _bFinished            = SW_FALSE;
        GameObject*    pOwner = getOwner();
        MeshComponent* pMesh  = pOwner != nullptr ? pOwner->getComponent<MeshComponent>() : nullptr;
        if ( pMesh != nullptr )
            pMesh->setLocalPosition( _position + float3{ 0.0f, _visualLift, 0.0f } );
    }

    void ArenaProjectileComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        if ( _bLaunched == SW_FALSE || _bFinished == SW_TRUE || deltaTime <= 0.0f )
            return;
        GameObject*                   pOwner    = getOwner();
        GameObjectManager*            pManager  = pOwner != nullptr ? pOwner->getManager() : nullptr;
        const ArenaDirectorComponent* pDirector = pManager != nullptr ? ArenaDirectorComponent::resolveDirector( *pManager, _director ) : nullptr;
        if ( pDirector == nullptr )
        {
            finish();
            return;
        }

        const float3 step = _velocity * deltaTime;
        _position         = _position + step;
        _remainingRange -= step.getLength();
        MeshComponent* pMesh = pOwner->getComponent<MeshComponent>();
        if ( pMesh != nullptr )
            pMesh->setLocalPosition( _position + float3{ 0.0f, _visualLift, 0.0f } );

        // 처음 닿은 적대 유닛 하나. 같은 편 · 쓰러진 유닛은 지나간다. 같은 그룹의 다른 투사체와 함께 읽으므로 첨자 대신 포인터로.
        const vector<ArenaUnitView>& listView = pDirector->getUnitViews();
        const ArenaUnitView*         pView    = listView.data();
        const float32                reach    = _unitRadius + _radius;
        for ( size_t viewIndex = 0; viewIndex < listView.size(); ++viewIndex )
        {
            const ArenaUnitView& view      = pView[viewIndex];
            const bool           bSameTeam = ( view._kind == ArenaUnitKind::Player ) == ( _bFromPlayer == SW_TRUE );
            if ( bSameTeam || view._bAlive == SW_FALSE || float3::getDistance( view._position, _position ) > reach )
                continue;
            applyHit( view._object );
            finish();
            return;
        }
        const float32 halfSize    = pDirector->getArenaHalfSize() + 2.0f;
        const bool    bOutOfArena = MathUtil::abs( _position._x ) > halfSize || MathUtil::abs( _position._z ) > halfSize;
        if ( _remainingRange <= 0.0f || bOutOfArena )
            finish();
    }

    void ArenaProjectileComponent::applyHit( GameObjectHandle target ) const
    {
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager == nullptr )
            return;
        // 대상은 다른 워커가 틱하고 있을 수 있다 — 틱 뒤(게임 스레드)에 건다. 무적이면 이펙트의 BlockedTag 가 막는다. 효과음도 게임 스레드에서.
        const GameplayEffectSpec spec      = _spec;
        const GameplayEffectSpec extraSpec = _extraSpec;
        pManager->executeOrDeferPostTick( SW_DELEGATE_LAMBDA( GameObjectManager::PostTickDelegate, [pManager, target, spec, extraSpec]()
        {
            GameObject*             pTarget        = pManager->resolveGameObject( target );
            AbilitySystemComponent* pAbilitySystem = pTarget != nullptr ? pTarget->getComponent<AbilitySystemComponent>() : nullptr;
            if ( pAbilitySystem == nullptr )
                return;
            if ( spec.isValid() )
                (void)pAbilitySystem->applyGameplayEffectSpecToSelf( spec );
            if ( extraSpec.isValid() )
                (void)pAbilitySystem->applyGameplayEffectSpecToSelf( extraSpec );
            (void)GameSound::play( "game/abilityarena/sounds/impact_punch_medium_000.ogg" );
        } ) );
    }

    void ArenaProjectileComponent::finish()
    {
        _bFinished                  = SW_TRUE;
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager != nullptr )
            pManager->destroyObject( pOwner ); // 지연 삭제 — 틱 안에서도 된다
    }
} // namespace sw
