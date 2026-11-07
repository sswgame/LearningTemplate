#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Ability/AbilityEffectVolumeComponent.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Runtime/Gameplay/Ability/AbilityComponent.h"

namespace sw
{
    AbilityEffectVolumeComponent::AbilityEffectVolumeComponent()
        : _effectID{}
        , _exitPolicy{ EffectVolumeExitPolicy::RemoveOnExit }
        , _reapplyInterval{ 0.0f }
        , _occupantTag{}
        , _listOccupant{}
        , _reapplyTimer{ 0.0f }
    {
    }

    void AbilityEffectVolumeComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        _reapplyTimer = _reapplyInterval;
        // 체류 주기가 있을 때만 틱한다 — 진입 · 이탈만 쓰는 영역은 프레임 비용이 없다.
        setCanEverTick( _reapplyInterval > 0.0f );
    }

    void AbilityEffectVolumeComponent::onEndPlay()
    {
        // 영역이 사라지면 그 영역이 건 효과도 끝난다(정책이 지우기면) — 안에 있던 개체를 나간 것으로 친다.
        GameObject* const        pOwner   = getOwner();
        GameObjectManager* const pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager != nullptr )
        {
            const vector<GameObjectHandle> listOccupant = _listOccupant;
            for ( const GameObjectHandle& handle : listOccupant )
            {
                if ( GameObject* pOccupant = pManager->resolveGameObject( handle ) )
                    (void)exit( *pOccupant );
            }
        }
        _listOccupant.clear();
        Component::onEndPlay();
    }

    void AbilityEffectVolumeComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        if ( _reapplyInterval <= 0.0f || _listOccupant.empty() )
            return;
        _reapplyTimer -= deltaTime;
        if ( _reapplyTimer > 0.0f )
            return;
        _reapplyTimer += _reapplyInterval;
        GameObjectManager* const pManager = getOwner() != nullptr ? getOwner()->getManager() : nullptr;
        if ( pManager == nullptr )
            return;
        for ( const GameObjectHandle& handle : _listOccupant )
        {
            if ( GameObject* pOccupant = pManager->resolveGameObject( handle ) )
                (void)applyTo( *pOccupant );
        }
    }

    void AbilityEffectVolumeComponent::onOverlapBegin( const OverlapInfo& overlap )
    {
        Component::onOverlapBegin( overlap );
        if ( overlap._pOther != nullptr )
            (void)enter( *overlap._pOther );
    }

    void AbilityEffectVolumeComponent::onOverlapEnd( const OverlapInfo& overlap )
    {
        Component::onOverlapEnd( overlap );
        if ( overlap._pOther != nullptr )
            (void)exit( *overlap._pOther );
    }

    bool AbilityEffectVolumeComponent::enter( GameObject& occupant )
    {
        const bool bFiltered = _occupantTag.isValid() && occupant.getTags().hasTag( _occupantTag ) == false;
        if ( bFiltered || occupant.getComponent<AbilityComponent>() == nullptr )
            return false;
        const GameObjectHandle handle = occupant.getHandle();
        for ( const GameObjectHandle& inside : _listOccupant )
        {
            if ( inside == handle )
                return false; // 바디가 여럿이면 겹침이 여러 번 온다 — 한 번만 건다
        }
        _listOccupant.push_back( handle );
        return applyTo( occupant );
    }

    bool AbilityEffectVolumeComponent::exit( GameObject& occupant )
    {
        const GameObjectHandle handle = occupant.getHandle();
        for ( size_t index = 0; index < _listOccupant.size(); ++index )
        {
            if ( _listOccupant[index] != handle )
                continue;
            _listOccupant.erase( _listOccupant.begin() + static_cast<ptrdiff_t>( index ) );
            AbilityComponent* const pAbility = occupant.getComponent<AbilityComponent>();
            if ( _exitPolicy == EffectVolumeExitPolicy::RemoveOnExit && pAbility != nullptr )
                (void)pAbility->removeActiveEffectsFromSource( getSourceName() );
            return true;
        }
        return false;
    }

    void AbilityEffectVolumeComponent::configure( const hashed_string& effectID, EffectVolumeExitPolicy exitPolicy, float32 reapplyInterval, TagID occupantTag )
    {
        _effectID        = effectID;
        _exitPolicy      = exitPolicy;
        _reapplyInterval = reapplyInterval;
        _occupantTag     = occupantTag;
        _reapplyTimer    = reapplyInterval;
    }

    hashed_string AbilityEffectVolumeComponent::getSourceName() const
    {
        const GameObject* const pOwner = getOwner();
        return hashed_string( string( "volume:" ) + ( pOwner != nullptr ? pOwner->getName().c_str() : "?" ) );
    }

    bool AbilityEffectVolumeComponent::applyTo( GameObject& occupant ) const
    {
        AbilityComponent* const pAbility = occupant.getComponent<AbilityComponent>();
        if ( pAbility == nullptr )
            return false;
        AbilityEffectSpec spec = pAbility->makeOutgoingSpecByID( _effectID ); // 카탈로그에 없으면 경고를 남기고 빈 스펙이다(걸지 않는다)
        if ( spec.isValid() == false )
            return false;
        spec._context._sourceName = getSourceName();
        if ( getOwner() != nullptr )
            spec._context._effectCauser = getOwner()->getHandle();
        return pAbility->applyGameplayEffectSpecToTarget( spec, pAbility ).isValid();
    }
} // namespace sw
