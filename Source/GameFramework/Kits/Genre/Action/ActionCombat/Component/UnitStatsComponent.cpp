#include "pch.h"

#include "GameFramework/Kits/Genre/Action/ActionCombat/Component/UnitStatsComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Kits/Genre/Action/ActionCombat/ActionCombatEvents.h"
#include "GameFramework/Kits/Genre/Action/ActionCombat/Catalog/MonsterCatalog.h"
#include "GameFramework/Runtime/Character/Combat/Damage/DamageMath.h"
#include "GameFramework/Runtime/Foundation/Framework/GameEventUtil.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/Framework/Presentation/IDamageNumberSink.h"
#include "GameFramework/Runtime/Foundation/GameData/DepletingValue.h"

namespace sw
{
    namespace
    {
        /** @brief 틱 중의 피해와 회복을 틱 직후로 미룹니다. 미룬 피해도 @p instigator 를 유지하므로, 미룬 피해의 이벤트도 누가 냈는지 알 수 있습니다. */
        void enqueueStatsMutation( GameObject* pOwner, int32 amount, bool bHeal, GameObjectHandle instigator )
        {
            if ( pOwner == nullptr )
                return;

            GameObjectManager* pManager = pOwner->getManager();
            if ( pManager == nullptr )
                return;

            const uint64 objectID = pOwner->getObjectID();
            pManager->deferPostTick( [pManager, objectID, amount, bHeal, instigator]()
            {
                GameObject* pObj = pManager->findGameObjectByID( objectID );
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

        /**
         * @brief 틱 중의 맞음을 틱 직후로 미룹니다. 맞음 정보(바디 · 방향 · 충격량)를 함께 넘겨, 미룬 맞음으로 쓰러져도 래그돌 · 절단에 치명 신호가 간다.
         * @details 때린 쪽 포인터는 틱 직후에 핸들로 다시 찾는다(그 사이 지워졌으면 nullptr).
         */
        void enqueueStatsHit( GameObject* pOwner, const HitInfo& hit )
        {
            GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
            if ( pManager == nullptr )
                return;
            const uint64           objectID   = pOwner->getObjectID();
            const GameObjectHandle instigator = hit._pInstigator != nullptr ? hit._pInstigator->getHandle() : GameObjectHandle{};
            HitInfo                deferred   = hit;
            deferred._pInstigator             = nullptr;
            pManager->deferPostTick( [pManager, objectID, instigator, deferred]()
            {
                GameObject*         pObj   = pManager->findGameObjectByID( objectID );
                UnitStatsComponent* pStats = pObj != nullptr ? pObj->getComponent<UnitStatsComponent>() : nullptr;
                if ( pStats == nullptr )
                    return;
                HitInfo resolved      = deferred;
                resolved._pInstigator = pManager->resolveGameObject( instigator );
                pStats->onHitReceived( resolved );
            } );
        }
    } // namespace
} // namespace sw

namespace sw
{
    UnitStatsComponent::UnitStatsComponent()
        : _damageAppliedMulticast{}
        , _hp{ 0 }
        , _maxHP{ 0 }
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

    void UnitStatsComponent::setStats( int32 hp, int32 maxHP, int32 attack, int32 defense, float32 moveSpeed, float32 maxInvincibilityTime )
    {
        _hp                   = hp;
        _maxHP                = maxHP;
        _attack               = attack;
        _defense              = defense;
        _moveSpeed            = moveSpeed;
        _maxInvincibilityTime = maxInvincibilityTime;
        notifyHealthChanged( true );
    }

    void UnitStatsComponent::setStats( const MonsterStatsDef& monsterStats )
    {
        setStats( monsterStats._hp, monsterStats._maxHP, monsterStats._atk, monsterStats._def, monsterStats._speed, monsterStats._invincibility );
    }

    void UnitStatsComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        setTickGroup( TickGroup::DuringPhysics );
        notifyHealthChanged( true );
    }

    void UnitStatsComponent::onTick( TickContext& context )
    {
        Component::onTick( context );

        if ( _invincibilityTime > 0.0f )
        {
            _invincibilityTime -= context.getDeltaTime();
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
            enqueueStatsMutation( pOwner, amount, false, instigator );
            return;
        }
        // 맞음 정보가 없는 피해(투사체 · 게임 코드)로 쓰러져도 치명 신호를 낸다 — 바디가 없어 래그돌은 충격량 없이 넘어가고 절단은 자르지 않는다.
        if ( applyTakeDamage( amount, instigator ) )
            deliverFatalHit( HitInfo{} );
    }

    void UnitStatsComponent::onHitReceived( const HitInfo& hit )
    {
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager != nullptr && pManager->isStructuralMutationFrozen() )
        {
            enqueueStatsHit( pOwner, hit ); // 틱 직후 같은 함수로 다시 들어온다
            return;
        }
        HealthSourceComponent::onHitReceived( hit );
        const int32 amount = static_cast<int32>( MathUtil::round( hit._damage * hit._damageMultiplier ) );
        // 이 맞음으로 쓰러졌으면 래그돌 · 절단에 치명 신호를 낸다.
        if ( applyTakeDamage( amount, hit._pInstigator != nullptr ? hit._pInstigator->getHandle() : GameObjectHandle{} ) )
            deliverFatalHit( hit );
    }

    void UnitStatsComponent::heal( int32 amount )
    {
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager != nullptr && pManager->isStructuralMutationFrozen() )
        {
            enqueueStatsMutation( pOwner, amount, true, GameObjectHandle{} );
            return;
        }
        applyHeal( amount );
    }

    Subscription UnitStatsComponent::registerDamageApplied( const DamageAppliedDelegate& delegate )
    {
        return _damageAppliedMulticast.add( delegate );
    }

    bool UnitStatsComponent::applyTakeDamage( int32 amount, GameObjectHandle instigator )
    {
        const bool bCannotTakeDamage = ( _bIsDead || _invincibilityTime > 0.0f );
        if ( bCannotTakeDamage )
            return false;

        // 방어 공식은 `DamageMath::applyArmor` 하나다(액션 룸의 적도 같은 공식을 쓴다). 고정 방어를 빼고 최소 1을 보장한다. 0 이하의 피해는 맞지 않은 것으로 본다(언리얼 `ApplyDamage` 도 0 을 버린다).
        const int32 actualDamage = static_cast<int32>( DamageMath::applyArmor( static_cast<float32>( amount ), static_cast<float32>( _defense ), 0.0f, 1.0f ) );
        if ( actualDamage <= 0 )
            return false;

        if ( DepletingValue::take( _hp, actualDamage )._bDepleted == SW_TRUE )
        {
            _bIsDead = true;
        }
        else
        {
            _invincibilityTime = _maxInvincibilityTime;
        }

        // 피해가 HP 에 닿는 곳은 여기 하나뿐이다. 알림도 여기서 한 번 낸다(깎인 값과 남은 HP 를 아는 곳이 여기뿐이다). 컴포넌트의 구독자는 이 시점에 받고,
        // "game" 채널은 버스 스레드면 이 시점에, 아니면 다음 processEvents 에 받는다(`GameEventUtil::send`).
        GameObject*        pOwner = getOwner();
        DamageAppliedEvent event;
        event._instigator  = instigator;
        event._target      = ( pOwner != nullptr ) ? pOwner->getHandle() : GameObjectHandle{};
        event._amount      = actualDamage;
        event._remainingHP = _hp;
        event._bKilled     = _bIsDead ? SW_TRUE : SW_FALSE;
        _damageAppliedMulticast.broadcast( event );
        GameEventUtil::send( event );

        notifyHealthChanged( false );
        if ( _bShowDamageNumbers )
            spawnDamageNumber( actualDamage );
        return _bIsDead;
    }

    void UnitStatsComponent::applyHeal( int32 amount )
    {
        if ( _bIsDead )
            return;

        _hp += amount;
        if ( _hp > _maxHP )
            _hp = _maxHP;
        notifyHealthChanged( false );
    }

    HealthReading UnitStatsComponent::getHealthReading() const
    {
        HealthReading reading;
        reading._health    = static_cast<float32>( _hp );
        reading._maxHealth = static_cast<float32>( _maxHP );
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

        // 숫자는 창구가 낸다(클라이언트 전용 구현 — 전용 서버는 창구가 없다).
        if ( IDamageNumberSink* pDamageNumbers = game::findSubsystem<IDamageNumberSink>() )
            pDamageNumbers->spawnDamageNumber( *pManager, position, amount );
    }
} // namespace sw

namespace sw
{
    namespace
    {
        struct UnitStatsWatchesInternal
        {
            /** @brief 감시 값 `UnitStats.HP` 입니다. 대상 유닛의 지금 HP 입니다. */
            [[nodiscard]] static bool readHP( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const GameObject*         pTarget = query.findTargetObject();
                const UnitStatsComponent* pStats  = pTarget != nullptr ? pTarget->getComponent<UnitStatsComponent>() : nullptr;
                if ( pStats == nullptr )
                {
                    outValue.setText( pTarget == nullptr ? query.getTargetProblem() : query.describeMissing( "unit stats" ) );
                    return false;
                }
                outValue.setNumber( static_cast<float64>( pStats->getHP() ) );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_QUERY_WATCH( unitStatsHP, "UnitStats.HP", "Current HP of the target's UnitStatsComponent", &UnitStatsWatchesInternal::readHP, true, false );
} // namespace sw
