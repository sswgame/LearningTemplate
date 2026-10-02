#include "pch.h"

#include "GameFramework/Kits/ActionCombat/UnitStatsComponent.h"

#include "Core/Event/EventDispatcher.h"

#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/GameEvents.h"
#include "GameFramework/Base/GameService.h"
#include "GameFramework/Kits/ActionCombat/ActionCombatEvents.h"

namespace sw
{
    namespace
    {
        struct UnitStatsComponentInternal
        {
            /** @brief 틱 중의 피해 · 회복을 틱 직후로 미룹니다. 피해는 @p instigator 를 들고 간다 — 미룬 피해의 이벤트도 누가 냈는지 안다. */
            static void enqueueStatsMutation( GameObject* pOwner, int32 amount, bool bHeal, GameObjectHandle instigator )
            {
                if ( pOwner == nullptr )
                    return;

                GameObjectManager* pManager = pOwner->getManager();
                if ( pManager == nullptr )
                    return;

                const uint64 objectId = pOwner->getObjectId();
                pManager->deferPostTick( [pManager, objectId, amount, bHeal, instigator]()
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
                        pStats->takeDamage( amount, instigator );
                } );
            }

            /** @brief 깎인 피해를 "game" 채널 큐에 싣습니다. 이벤트 버스가 붙지 않은 프로세스(도구 · 시험)에서는 아무것도 하지 않습니다. */
            static void pushDamageApplied( const DamageAppliedEvent& event )
            {
                EventDispatcher* pDispatcher = game::getService<EventDispatcher>();
                if ( pDispatcher == nullptr )
                    return;
                pDispatcher->push( gameEventChannel(), event );
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

    void UnitStatsComponent::takeDamage( int32 amount, GameObjectHandle instigator )
    {
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager != nullptr && pManager->isStructuralMutationFrozen() )
        {
            UnitStatsComponentInternal::enqueueStatsMutation( pOwner, amount, false, instigator );
            return;
        }
        applyTakeDamage( amount, instigator );
    }

    void UnitStatsComponent::heal( int32 amount )
    {
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager != nullptr && pManager->isStructuralMutationFrozen() )
        {
            UnitStatsComponentInternal::enqueueStatsMutation( pOwner, amount, true, GameObjectHandle{} );
            return;
        }
        applyHeal( amount );
    }

    void UnitStatsComponent::applyTakeDamage( int32 amount, GameObjectHandle instigator )
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
        {
            _invincibilityTime = _maxInvincibilityTime;
        }

        // 피해가 HP 에 닿는 자리는 여기 하나다 — 이벤트도 여기서 한 번 낸다(깎인 값 · 남은 HP 를 아는 곳이 여기뿐이다).
        GameObject*        pOwner = getOwner();
        DamageAppliedEvent event;
        event._instigator  = instigator;
        event._target      = ( pOwner != nullptr ) ? pOwner->getHandle() : GameObjectHandle{};
        event._amount      = actualDamage;
        event._remainingHp = _hp;
        event._bKilled     = _bIsDead ? SW_TRUE : SW_FALSE;
        UnitStatsComponentInternal::pushDamageApplied( event );
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
