#include "pch.h"

#include "GameFramework/Kits/ActionCombat/UnitStatsComponent.h"

#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

namespace sw
{
    namespace
    {
        struct UnitStatsComponentInternal
        {
            static void enqueueStatsMutation( GameObject* pOwner, int32 amount, bool bHeal )
            {
                if ( pOwner == nullptr )
                    return;

                GameObjectManager* pManager = pOwner->getManager();
                if ( pManager == nullptr )
                    return;

                const uint64 objectId = pOwner->getObjectId();
                pManager->deferPostTick( [pManager, objectId, amount, bHeal]()
                {
                    GameObject* pObj = pManager->findGameObjectById( objectId );
                    if ( pObj == nullptr )
                        return;

                    UnitStatsComponent* pStats = pObj->getComponent<UnitStatsComponent>();
                    if ( pStats == nullptr )
                        return;

                    if ( bHeal )
                        pStats->heal( amount );
                    else
                        pStats->takeDamage( amount );
                } );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    UnitStatsComponent::UnitStatsComponent()
        : _hp{ 0 }
        , _maxHp{ 0 }
        , _attack{ 0 }
        , _defense{ 0 }
        , _moveSpeed{ 0.0f }
        , _invincibilityTime{ 0.0f }
        , _maxInvincibilityTime{ 0.0f }
        , _bIsDead{ false }
    {
    }

    void UnitStatsComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        setTickGroup( TickGroup::DuringPhysics );

        GameObject* pOwner = getOwner();
        if ( pOwner != nullptr )
            pOwner->addTag( "Stats"_tag );
    }

    void UnitStatsComponent::onEndPlay()
    {
        Component::onEndPlay();
    }

    void UnitStatsComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );

        if ( _invincibilityTime > 0.0f )
        {
            _invincibilityTime -= deltaTime;
            if ( _invincibilityTime < 0.0f )
                _invincibilityTime = 0.0f;
        }
    }

    void UnitStatsComponent::takeDamage( int32 amount )
    {
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager != nullptr && pManager->isStructuralMutationFrozen() )
        {
            UnitStatsComponentInternal::enqueueStatsMutation( pOwner, amount, false );
            return;
        }
        applyTakeDamage( amount );
    }

    void UnitStatsComponent::heal( int32 amount )
    {
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager != nullptr && pManager->isStructuralMutationFrozen() )
        {
            UnitStatsComponentInternal::enqueueStatsMutation( pOwner, amount, true );
            return;
        }
        applyHeal( amount );
    }

    void UnitStatsComponent::applyTakeDamage( int32 amount )
    {
        const bool bCannotTakeDamage = ( _bIsDead || _invincibilityTime > 0.0f );
        if ( bCannotTakeDamage )
            return;

        int32 actualDamage = amount - _defense;
        if ( actualDamage < 1 )
            actualDamage = 1;

        _hp -= actualDamage;
        if ( _hp <= 0 )
        {
            _hp      = 0;
            _bIsDead = true;
        }
        else
            _invincibilityTime = _maxInvincibilityTime;
    }

    void UnitStatsComponent::applyHeal( int32 amount )
    {
        if ( _bIsDead )
            return;

        _hp += amount;
        if ( _hp > _maxHp )
            _hp = _maxHp;
    }
} // namespace sw
