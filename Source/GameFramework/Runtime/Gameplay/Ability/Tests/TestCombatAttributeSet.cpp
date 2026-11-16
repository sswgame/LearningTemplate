// CombatAttributeSetTest — 전투 어트리뷰트 규칙: 범위 자르기, 최대치가 줄 때 현재치 따라 줄이기, 메타 피해 · 회복 옮기기, 피격 · 쓰러짐 이벤트와 한 번만 죽기.
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Runtime/Gameplay/Ability/Ability.h"
#include "GameFramework/Runtime/Gameplay/Ability/AbilityComponent.h"
#include "GameFramework/Runtime/Gameplay/Ability/AbilityEffect.h"
#include "GameFramework/Runtime/Gameplay/Ability/CombatAttributeSet.h"
#include "GameFramework/Runtime/Gameplay/Ability/TagCountTable.h"

using namespace sw;

namespace
{
    /** @brief 발동한 채 머무는 어빌리티입니다. 끝난 까닭을 셉니다. */
    class CombatProbeAbility final : public Ability
    {
    public:
        uint32 _activatedCount{ 0 };
        uint32 _cancelledCount{ 0 };

        void activateAbility( const AbilityEventData* pTriggerEvent ) override
        {
            (void)pTriggerEvent;
            ++_activatedCount;
        }
        void onEndAbility( bool bWasCancelled ) override { _cancelledCount += bWasCancelled ? 1u : 0u; }
    };

    /** @brief `CombatAttributeSet` 을 붙인 오브젝트 하나와 피격 · 쓰러짐 이벤트 기록입니다. */
    struct CombatFixture
    {
        GameObjectManager _manager;
        AbilityComponent* _pAbilities{ nullptr };
        vector<TagID>     _listEventTag{};
        Subscription      _hitSubscription{};
        Subscription      _deathSubscription{};
        float32           _lastHitMagnitude{ 0.0f };
        GameObjectHandle  _lastHitInstigator{};
        GameObjectHandle  _lastHitTarget{};

        CombatFixture()
        {
            _pAbilities = _manager.createGameObject( hashed_string( "Fighter" ) )->addComponent<AbilityComponent>();
            (void)_pAbilities->addAttributeSet<CombatAttributeSet>();
            _hitSubscription   = _pAbilities->registerGameplayEvent( CombatAttributeSet::getHitEventTag(), SW_DELEGATE_LAMBDA( AbilityComponent::AbilityEventDelegate, [this]( const AbilityEventData& event )
              {
                _listEventTag.push_back( CombatAttributeSet::getHitEventTag() );
                _lastHitMagnitude  = event._magnitude;
                _lastHitInstigator = event._instigator;
                _lastHitTarget     = event._target;
            } ) );
            _deathSubscription = _pAbilities->registerGameplayEvent( CombatAttributeSet::getDeathEventTag(), SW_DELEGATE_LAMBDA( AbilityComponent::AbilityEventDelegate, [this]( const AbilityEventData& )
            { _listEventTag.push_back( CombatAttributeSet::getDeathEventTag() ); } ) );
        }

        float32 current( const hashed_string& name ) const { return _pAbilities->getAttributeValue( name ).value_or( -9999.0f ); }
        float32 base( const hashed_string& name ) const { return _pAbilities->getAttributeBaseValue( name ).value_or( -9999.0f ); }
        bool    isDead() const { return _pAbilities->getOwnedTagCounts().hasMatchingTag( CombatAttributeSet::getDeadTag() ); }

        /** @brief @p name 에 @p amount 를 더하는 즉시 이펙트를 겁니다. */
        void applyInstant( const hashed_string& name, float32 amount ) { (void)apply( EffectDurationPolicy::Instant, name, amount ); }

        /** @brief @p name 에 @p amount 를 더하는 이펙트를 겁니다. */
        ActiveEffectHandle apply( EffectDurationPolicy policy, const hashed_string& name, float32 amount )
        {
            shared_ptr<AbilityEffectDef> pDef = make_shared<AbilityEffectDef>();
            pDef->_durationPolicy             = policy;
            AbilityEffectModifier modifier;
            modifier._attribute                    = name;
            modifier._op                           = AttributeModOp::Add;
            modifier._scalableMagnitude._baseValue = amount;
            pDef->_listModifier.push_back( modifier );
            return _pAbilities->applyGameplayEffectSpecToSelf( _pAbilities->makeOutgoingSpec( pDef ) );
        }
    };
} // namespace

/**
 * @brief [CombatAttributeSetTest] 기본값은 문서대로다 - 체력 · 마나 100, 공격력 10, 방어 0, 이동 속도 5, 메타 0
 */
SW_TEST_CASE( CombatAttributeSetTest, DefaultsMatchTheDocumentedValues )
{
    const CombatFixture rig;
    SW_EXPECT_EQUAL( 100.0f, rig.current( CombatAttributes::health() ) );
    SW_EXPECT_EQUAL( 100.0f, rig.current( CombatAttributes::maxHealth() ) );
    SW_EXPECT_EQUAL( 100.0f, rig.current( CombatAttributes::mana() ) );
    SW_EXPECT_EQUAL( 100.0f, rig.current( CombatAttributes::maxMana() ) );
    SW_EXPECT_EQUAL( 10.0f, rig.current( CombatAttributes::attackPower() ) );
    SW_EXPECT_EQUAL( 0.0f, rig.current( CombatAttributes::armor() ) );
    SW_EXPECT_EQUAL( 5.0f, rig.current( CombatAttributes::moveSpeed() ) );
    SW_EXPECT_EQUAL( 0.0f, rig.current( CombatAttributes::incomingDamage() ) );
    SW_EXPECT_EQUAL( 0.0f, rig.current( CombatAttributes::incomingHealing() ) );
    SW_EXPECT_FALSE( rig.isDead() );
}

/**
 * @brief [CombatAttributeSetTest] 컴포넌트에 붙기 전에는 최대치를 읽지 않아 아래쪽만 자른다. 범위 규칙이 없는 어트리뷰트(방어)는 음수도 그대로다
 */
SW_TEST_CASE( CombatAttributeSetTest, UnattachedSetClampsOnlyTheFloor )
{
    CombatAttributeSet set;
    float32            value = -5.0f;
    set.preAttributeChange( CombatAttributes::health(), value );
    SW_EXPECT_EQUAL( 0.0f, value );
    value = 500.0f;
    set.preAttributeBaseChange( CombatAttributes::mana(), value );
    SW_EXPECT_EQUAL( 500.0f, value );
    value = -1.0f;
    set.preAttributeChange( CombatAttributes::moveSpeed(), value );
    SW_EXPECT_EQUAL( 0.0f, value );
    value = -3.0f;
    set.preAttributeChange( CombatAttributes::armor(), value );
    SW_EXPECT_EQUAL( -3.0f, value );
}

/**
 * @brief [CombatAttributeSetTest] 체력과 마나는 [0, 최대치] 안에, 이동 속도와 최대치는 0 이상에 머문다 - base 와 current 모두
 */
SW_TEST_CASE( CombatAttributeSetTest, ValuesStayWithinTheirRange )
{
    CombatFixture rig;
    SW_EXPECT_TRUE( rig._pAbilities->setAttributeBaseValue( CombatAttributes::health(), 500.0f ) );
    SW_EXPECT_EQUAL( 100.0f, rig.base( CombatAttributes::health() ) );
    SW_EXPECT_TRUE( rig._pAbilities->setAttributeBaseValue( CombatAttributes::mana(), -5.0f ) );
    SW_EXPECT_EQUAL( 0.0f, rig.base( CombatAttributes::mana() ) );
    SW_EXPECT_EQUAL( 0.0f, rig.current( CombatAttributes::mana() ) );
    SW_EXPECT_TRUE( rig._pAbilities->setAttributeBaseValue( CombatAttributes::moveSpeed(), -1.0f ) );
    SW_EXPECT_EQUAL( 0.0f, rig.current( CombatAttributes::moveSpeed() ) );
    SW_EXPECT_TRUE( rig._pAbilities->setAttributeBaseValue( CombatAttributes::maxHealth(), -1.0f ) );
    SW_EXPECT_EQUAL( 0.0f, rig.base( CombatAttributes::maxHealth() ) );
    SW_EXPECT_EQUAL( 0.0f, rig.current( CombatAttributes::health() ) ); // 최대치를 따라 내려왔다
}

/**
 * @brief [CombatAttributeSetTest] 최대치가 줄면 현재치가 따라 줄고, 최대치가 늘 때는 차오르지 않는다 - 마나도 같다
 */
SW_TEST_CASE( CombatAttributeSetTest, LoweringMaximumPullsCurrentDownRaisingDoesNotFill )
{
    CombatFixture rig;
    SW_EXPECT_TRUE( rig._pAbilities->setAttributeBaseValue( CombatAttributes::maxHealth(), 60.0f ) );
    SW_EXPECT_EQUAL( 60.0f, rig.base( CombatAttributes::health() ) );
    SW_EXPECT_EQUAL( 60.0f, rig.current( CombatAttributes::health() ) );
    SW_EXPECT_TRUE( rig._pAbilities->setAttributeBaseValue( CombatAttributes::maxHealth(), 200.0f ) );
    SW_EXPECT_EQUAL( 60.0f, rig.current( CombatAttributes::health() ) );

    SW_EXPECT_TRUE( rig._pAbilities->setAttributeBaseValue( CombatAttributes::maxMana(), 30.0f ) );
    SW_EXPECT_EQUAL( 30.0f, rig.current( CombatAttributes::mana() ) );
}

/**
 * @brief [CombatAttributeSetTest] 최대 체력 버프가 풀리면 그 사이 차오른 체력이 원래 최대치로 내려온다
 */
SW_TEST_CASE( CombatAttributeSetTest, MaximumBuffRemovalPullsHealthBack )
{
    CombatFixture            rig;
    const ActiveEffectHandle handle = rig.apply( EffectDurationPolicy::Infinite, CombatAttributes::maxHealth(), 50.0f );
    SW_EXPECT_EQUAL( 150.0f, rig.current( CombatAttributes::maxHealth() ) );
    SW_EXPECT_EQUAL( 100.0f, rig.current( CombatAttributes::health() ) ); // 버프만으로는 차오르지 않는다
    SW_EXPECT_TRUE( rig._pAbilities->setAttributeBaseValue( CombatAttributes::health(), 150.0f ) );
    SW_EXPECT_EQUAL( 150.0f, rig.current( CombatAttributes::health() ) );

    SW_EXPECT_TRUE( rig._pAbilities->removeActiveEffect( handle ) );
    SW_EXPECT_EQUAL( 100.0f, rig.current( CombatAttributes::maxHealth() ) );
    SW_EXPECT_EQUAL( 100.0f, rig.base( CombatAttributes::health() ) );
    SW_EXPECT_EQUAL( 100.0f, rig.current( CombatAttributes::health() ) );
}

/**
 * @brief [CombatAttributeSetTest] 결함: 최대치가 줄 때 base 만 보아, base 는 새 최대치 안인데 지속 이펙트로 오른 current 가 최대치 위에 남았다. 다시 집계해 자른다
 */
SW_TEST_CASE( CombatAttributeSetTest, LoweringMaximumClampsCurrentRaisedByAnEffect )
{
    CombatFixture rig;
    SW_EXPECT_TRUE( rig._pAbilities->setAttributeBaseValue( CombatAttributes::health(), 80.0f ) );
    (void)rig.apply( EffectDurationPolicy::Infinite, CombatAttributes::health(), 30.0f );
    SW_REQUIRE_TRUE( rig.current( CombatAttributes::health() ) == 100.0f ); // 80 + 30 이 최대치에 잘렸다

    SW_EXPECT_TRUE( rig._pAbilities->setAttributeBaseValue( CombatAttributes::maxHealth(), 90.0f ) );
    SW_EXPECT_EQUAL( 80.0f, rig.base( CombatAttributes::health() ) ); // base 는 범위 안이라 그대로다
    SW_EXPECT_EQUAL( 90.0f, rig.current( CombatAttributes::health() ) );
}

/**
 * @brief [CombatAttributeSetTest] 메타 피해는 체력으로 옮겨지고 바로 비워진다 - 다음 피해가 쌓이지 않고, 맞을 때마다 깎인 체력을 실은 피격 이벤트가 나간다
 */
SW_TEST_CASE( CombatAttributeSetTest, IncomingDamageMovesToHealthAndClears )
{
    CombatFixture rig;
    rig.applyInstant( CombatAttributes::incomingDamage(), 30.0f );
    SW_EXPECT_EQUAL( 70.0f, rig.current( CombatAttributes::health() ) );
    SW_EXPECT_EQUAL( 0.0f, rig.base( CombatAttributes::incomingDamage() ) );
    SW_EXPECT_EQUAL( 0.0f, rig.current( CombatAttributes::incomingDamage() ) );
    SW_EXPECT_EQUAL( 30.0f, rig._lastHitMagnitude );
    SW_EXPECT_TRUE( rig._lastHitInstigator == rig._pAbilities->getOwner()->getHandle() ); // 스펙을 만든 쪽
    SW_EXPECT_TRUE( rig._lastHitTarget == rig._pAbilities->getOwner()->getHandle() );

    rig.applyInstant( CombatAttributes::incomingDamage(), 5.0f );
    SW_EXPECT_EQUAL( 65.0f, rig.current( CombatAttributes::health() ) );
    SW_EXPECT_EQUAL( size_t( 2 ), rig._listEventTag.size() );
    SW_EXPECT_FALSE( rig.isDead() );
}

/**
 * @brief [CombatAttributeSetTest] 0 이하의 메타 피해는 체력을 올리지 않고 맞은 것도 아니다. 메타는 그래도 비운다
 */
SW_TEST_CASE( CombatAttributeSetTest, NonPositiveDamageIsIgnored )
{
    CombatFixture rig;
    SW_EXPECT_TRUE( rig._pAbilities->setAttributeBaseValue( CombatAttributes::health(), 50.0f ) );
    rig.applyInstant( CombatAttributes::incomingDamage(), -10.0f );
    SW_EXPECT_EQUAL( 50.0f, rig.current( CombatAttributes::health() ) );
    SW_EXPECT_EQUAL( 0.0f, rig.base( CombatAttributes::incomingDamage() ) );
    SW_EXPECT_TRUE( rig._listEventTag.empty() );
}

/**
 * @brief [CombatAttributeSetTest] 치사 피해: 피격 이벤트는 실제로 깎인 체력을 싣고 먼저 나가며, 이어서 쓰러짐 태그 · 어빌리티 취소 · 쓰러짐 이벤트가 온다
 */
SW_TEST_CASE( CombatAttributeSetTest, LethalDamageReportsActualLossThenDies )
{
    CombatFixture                  rig;
    unique_ptr<CombatProbeAbility> pAbility = make_unique<CombatProbeAbility>();
    CombatProbeAbility&            ability  = *pAbility;
    const AbilitySpecHandle        handle   = rig._pAbilities->giveAbility( std::move( pAbility ) );
    SW_REQUIRE_TRUE( rig._pAbilities->tryActivateAbility( handle ) == AbilityActivationResult::Activated );

    SW_EXPECT_TRUE( rig._pAbilities->setAttributeBaseValue( CombatAttributes::health(), 20.0f ) );
    rig.applyInstant( CombatAttributes::incomingDamage(), 50.0f );

    SW_EXPECT_EQUAL( 0.0f, rig.current( CombatAttributes::health() ) );
    SW_EXPECT_EQUAL( 20.0f, rig._lastHitMagnitude );
    SW_EXPECT_TRUE( rig.isDead() );
    SW_EXPECT_EQUAL( 1u, ability._cancelledCount );
    SW_REQUIRE_EQUAL( size_t( 2 ), rig._listEventTag.size() );
    SW_EXPECT_TRUE( rig._listEventTag[0] == CombatAttributeSet::getHitEventTag() );
    SW_EXPECT_TRUE( rig._listEventTag[1] == CombatAttributeSet::getDeathEventTag() );
}

/**
 * @brief [CombatAttributeSetTest] 쓰러진 뒤의 피해와 회복은 무시한다 - 다시 죽지 않고 이벤트도 없으며, 메타는 비워진다
 */
SW_TEST_CASE( CombatAttributeSetTest, DeadOwnerIgnoresDamageAndHealing )
{
    CombatFixture rig;
    rig.applyInstant( CombatAttributes::incomingDamage(), 100.0f );
    SW_REQUIRE_TRUE( rig.isDead() );
    const size_t eventCount = rig._listEventTag.size();

    rig.applyInstant( CombatAttributes::incomingDamage(), 10.0f );
    rig.applyInstant( CombatAttributes::incomingHealing(), 40.0f );

    SW_EXPECT_EQUAL( 0.0f, rig.current( CombatAttributes::health() ) );
    SW_EXPECT_EQUAL( 0.0f, rig.base( CombatAttributes::incomingDamage() ) );
    SW_EXPECT_EQUAL( 0.0f, rig.base( CombatAttributes::incomingHealing() ) );
    SW_EXPECT_EQUAL( eventCount, rig._listEventTag.size() );
    SW_EXPECT_EQUAL( 1, rig._pAbilities->getOwnedTagCounts().getCount( CombatAttributeSet::getDeadTag() ) );
}

/**
 * @brief [CombatAttributeSetTest] 메타 회복은 체력으로 옮겨지되 최대치에서 잘리고 비워진다. 0 이하는 무시한다
 */
SW_TEST_CASE( CombatAttributeSetTest, IncomingHealingMovesToHealthUpToMaximum )
{
    CombatFixture rig;
    SW_EXPECT_TRUE( rig._pAbilities->setAttributeBaseValue( CombatAttributes::health(), 50.0f ) );
    rig.applyInstant( CombatAttributes::incomingHealing(), 20.0f );
    SW_EXPECT_EQUAL( 70.0f, rig.current( CombatAttributes::health() ) );
    SW_EXPECT_EQUAL( 0.0f, rig.base( CombatAttributes::incomingHealing() ) );

    rig.applyInstant( CombatAttributes::incomingHealing(), 500.0f );
    SW_EXPECT_EQUAL( 100.0f, rig.current( CombatAttributes::health() ) );

    SW_EXPECT_TRUE( rig._pAbilities->setAttributeBaseValue( CombatAttributes::health(), 50.0f ) );
    rig.applyInstant( CombatAttributes::incomingHealing(), -20.0f );
    SW_EXPECT_EQUAL( 50.0f, rig.current( CombatAttributes::health() ) );
}

/**
 * @brief [CombatAttributeSetTest] 무적이면 치사 피해도 체력을 깎지 못해 피격 · 쓰러짐이 없다. 메타는 그래도 비워져 무적이 끝난 뒤 쏟아지지 않는다
 */
SW_TEST_CASE( CombatAttributeSetTest, InvulnerableOwnerTakesNoHitAndDoesNotDie )
{
    CombatFixture rig;
    rig._pAbilities->getOwnedTagCounts().updateCount( AbilityComponent::getInvulnerableTag(), 1 );
    rig.applyInstant( CombatAttributes::incomingDamage(), 500.0f );
    SW_EXPECT_EQUAL( 100.0f, rig.current( CombatAttributes::health() ) );
    SW_EXPECT_EQUAL( 0.0f, rig.base( CombatAttributes::incomingDamage() ) );
    SW_EXPECT_TRUE( rig._listEventTag.empty() );
    SW_EXPECT_FALSE( rig.isDead() );
}

/**
 * @brief [CombatAttributeSetTest] 메타 피해를 지나지 않고 체력이 0 이 돼도 한 번 쓰러진다 — 체력 직접 수정자, 최대 체력 디버프, 코드가 쓴 base(감사 G-5)
 */
SW_TEST_CASE( CombatAttributeSetTest, DirectHealthModifierToZeroRaisesDeath )
{
    {
        CombatFixture rig;
        rig.applyInstant( CombatAttributes::health(), -500.0f );
        SW_EXPECT_EQUAL( 0.0f, rig.current( CombatAttributes::health() ) );
        SW_EXPECT_TRUE( rig.isDead() );
        SW_REQUIRE_EQUAL( size_t( 1 ), rig._listEventTag.size() );
        SW_EXPECT_TRUE( rig._listEventTag[0] == CombatAttributeSet::getDeathEventTag() );
        rig.applyInstant( CombatAttributes::health(), -10.0f ); // 이미 쓰러졌다 — 다시 죽지 않는다
        SW_EXPECT_EQUAL( size_t( 1 ), rig._listEventTag.size() );
    }
    {
        CombatFixture rig;
        (void)rig.apply( EffectDurationPolicy::Infinite, CombatAttributes::maxHealth(), -100.0f );
        SW_EXPECT_EQUAL( 0.0f, rig.current( CombatAttributes::health() ) );
        SW_EXPECT_TRUE( rig.isDead() );
    }
    {
        CombatFixture rig;
        SW_EXPECT_TRUE( rig._pAbilities->setAttributeBaseValue( CombatAttributes::health(), 0.0f ) );
        SW_EXPECT_TRUE( rig.isDead() );
        SW_EXPECT_EQUAL( size_t( 1 ), rig._listEventTag.size() );
    }
}
