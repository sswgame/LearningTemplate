#include "pch.h"

#include "GameFramework/Kits/Action/ActionCombat/UnitStatsComponent.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Combat/DamageMath.h"
#include "GameFramework/Framework/GameEventUtil.h"
#include "GameFramework/Kits/Action/ActionCombat/ActionCombatEvents.h"
#include "GameFramework/Kits/Action/ActionCombat/MonsterCatalog.h"
#include "GameFramework/UI/DamageNumberComponent.h"

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
        };
    } // namespace
} // namespace sw

namespace sw
{
    UnitStatsComponent::UnitStatsComponent()
        : _damageAppliedMulticast{}
        , _hp{ 0 }
        , _maxHp{ 0 }
        , _attack{ 0 }
        , _defense{ 0 }
        , _moveSpeed{ 0.0f }
        , _invincibilityTime{ 0.0f }
        , _maxInvincibilityTime{ 0.0f }
        , _bIsDead{ false }
        , _bShowDamageNumbers{ false }
        , _damageNumberOffset{ 0.0f, 0.6f, 0.0f }
    {
    }

    void UnitStatsComponent::setStats( int32 hp, int32 maxHp, int32 attack, int32 defense, float32 moveSpeed, float32 maxInvincibilityTime )
    {
        _hp                   = hp;
        _maxHp                = maxHp;
        _attack               = attack;
        _defense              = defense;
        _moveSpeed            = moveSpeed;
        _maxInvincibilityTime = maxInvincibilityTime;
        notifyHealthChanged( true );
    }

    void UnitStatsComponent::setStats( const MonsterDef& monsterDef )
    {
        setStats( monsterDef._hp, monsterDef._maxHp, monsterDef._atk, monsterDef._def, monsterDef._speed, monsterDef._invincibility );
    }

    void UnitStatsComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        setTickGroup( TickGroup::DuringPhysics );
        notifyHealthChanged( true );
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

    DelegateHandle UnitStatsComponent::registerDamageApplied( const DamageAppliedDelegate& delegate )
    {
        return _damageAppliedMulticast.add( delegate );
    }

    void UnitStatsComponent::unregisterDamageApplied( DelegateHandle handle )
    {
        _damageAppliedMulticast.remove( handle );
    }

    void UnitStatsComponent::applyTakeDamage( int32 amount, GameObjectHandle instigator )
    {
        const bool bCannotTakeDamage = ( _bIsDead || _invincibilityTime > 0.0f );
        if ( bCannotTakeDamage )
            return;

        // 방어 식은 `DamageMath::applyArmor` 하나다(액션 룸의 적도 같은 식) — 고정 방어를 빼고 최소 1. 0 이하 피해는 맞지 않은 것이다(언리얼 `ApplyDamage` 가 0 을 버린다).
        const int32 actualDamage = static_cast<int32>( DamageMath::applyArmor( static_cast<float32>( amount ), static_cast<float32>( _defense ), 0.0f, 1.0f ) );
        if ( actualDamage <= 0 )
            return;

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

        // 피해가 HP 에 닿는 자리는 여기 하나다 — 알림도 여기서 한 번 낸다(깎인 값 · 남은 HP 를 아는 곳이 여기뿐이다). 컴포넌트의 구독자는 그 자리에서,
        // "game" 채널은 버스 스레드면 그 자리에서 · 아니면 다음 processEvents 에 받는다(`GameEventUtil::send`).
        GameObject*        pOwner = getOwner();
        DamageAppliedEvent event;
        event._instigator  = instigator;
        event._target      = ( pOwner != nullptr ) ? pOwner->getHandle() : GameObjectHandle{};
        event._amount      = actualDamage;
        event._remainingHp = _hp;
        event._bKilled     = _bIsDead ? SW_TRUE : SW_FALSE;
        _damageAppliedMulticast.broadcast( event );
        GameEventUtil::send( event );

        notifyHealthChanged( false );
        if ( _bShowDamageNumbers )
            spawnDamageNumber( actualDamage );
    }

    void UnitStatsComponent::applyHeal( int32 amount )
    {
        if ( _bIsDead )
            return;

        _hp += amount;
        if ( _hp > _maxHp )
            _hp = _maxHp;
        notifyHealthChanged( false );
    }

    HealthReading UnitStatsComponent::getHealthReading() const
    {
        HealthReading reading;
        reading._health    = static_cast<float32>( _hp );
        reading._maxHealth = static_cast<float32>( _maxHp );
        reading._bDead     = _bIsDead ? SW_TRUE : SW_FALSE;
        return reading;
    }

    void UnitStatsComponent::spawnDamageNumber( int32 amount )
    {
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = ( pOwner != nullptr ) ? pOwner->getManager() : nullptr;
        if ( pManager == nullptr )
            return;
        const SceneComponent* pRoot    = pOwner->getPrimarySceneComponent();
        const float3          position = ( pRoot != nullptr ? pRoot->getWorldPosition() : float3{} ) + _damageNumberOffset;

        DamageNumberComponent::spawnNumber( *pManager, position, amount );
    }
} // namespace sw
