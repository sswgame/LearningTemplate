#include "pch.h"

#include "Games/AbilityArena/ArenaEnemyControllerComponent.h"

#include "GameFramework/Ability/AbilitySystemComponent.h"

#include "Games/AbilityArena/ArenaDirectorComponent.h"

namespace sw
{
    ArenaEnemyControllerComponent::ArenaEnemyControllerComponent()
        : _kind{ ArenaUnitKind::Grunt }
        , _meleeReach{ 1.4f }
        , _preferredMin{ 5.0f }
        , _preferredMax{ 9.0f }
        , _fireRange{ 12.0f }
        , _unitRadius{ 0.5f }
    {
    }

    void ArenaEnemyControllerComponent::tickController( float32 deltaTime, const ArenaDirectorComponent& director, AbilitySystemComponent& abilitySystem,
                                                        float3& inoutPosition )
    {
        const ArenaUnitView* pTarget = director.findNearestHostileView( getOwner()->getHandle(), 100.0f );
        if ( pTarget != nullptr )
        {
            const float3  toTarget = pTarget->_position - inoutPosition;
            const float32 distance = toTarget.getLength();
            setFacing( flattenDirection( toTarget, getFacing() ) );
            if ( _kind == ArenaUnitKind::Caster )
            {
                // 원거리 — 적당한 거리를 지키며 쏜다. 물러나도 대상을 본다.
                if ( distance > _preferredMax )
                    moveTowards( abilitySystem, toTarget, deltaTime, inoutPosition );
                else if ( distance < _preferredMin )
                    moveTowards( abilitySystem, float3{ 0.0f, 0.0f, 0.0f } - toTarget, deltaTime, inoutPosition );
                setFacing( flattenDirection( toTarget, getFacing() ) );
                if ( distance <= _fireRange )
                    tapInput( abilitySystem, ArenaDirectorComponent::kInputFireball );
            }
            else if ( distance > _meleeReach )
            {
                moveTowards( abilitySystem, toTarget, deltaTime, inoutPosition );
            }
            else
            {
                tapInput( abilitySystem, ArenaDirectorComponent::kInputMelee );
            }
        }
        separateFromEnemies( director, inoutPosition );
    }

    void ArenaEnemyControllerComponent::separateFromEnemies( const ArenaDirectorComponent& director, float3& inoutPosition ) const
    {
        const GameObjectHandle       self     = getOwner()->getHandle();
        const vector<ArenaUnitView>& listView = director.getUnitViews();
        const ArenaUnitView*         pView    = listView.data();
        for ( size_t viewIndex = 0; viewIndex < listView.size(); ++viewIndex )
        {
            const ArenaUnitView& other = pView[viewIndex];
            if ( other._object == self || other._kind == ArenaUnitKind::Player )
                continue;
            const float3  apart    = inoutPosition - other._position;
            const float32 distance = apart.getLength();
            const float32 overlap  = _unitRadius * 2.0f - distance;
            if ( overlap <= 0.0f || distance < 1.0e-4f )
                continue;
            inoutPosition = inoutPosition + apart * ( 0.5f * overlap / distance );
        }
    }
} // namespace sw
