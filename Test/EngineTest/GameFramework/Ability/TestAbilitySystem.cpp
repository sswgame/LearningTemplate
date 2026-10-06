#include "pch.h"

#include "Core/Event/EventDispatcher.h"
#include "Core/Module/ModuleUnloadListener.h"

#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Reflection/TypeRegistry.h"

#include "EngineTest/TestGameObjectMocks.h"

#include "GameFramework/Base/Ability/AbilityCatalog.h"
#include "GameFramework/Base/Ability/AbilitySystemComponent.h"
#include "GameFramework/Base/Ability/AbilitySystemEvents.h"
#include "GameFramework/Base/Ability/AbilityTask.h"
#include "GameFramework/Base/Ability/CombatAttributeSet.h"
#include "GameFramework/Base/Ability/GameplayAbility.h"
#include "GameFramework/Base/Framework/GameEvents.h"
#include "GameFramework/Base/Framework/GameService.h"
#include "GameFramework/Base/UI/HealthBarComponent.h"

#include "TestFramework/TestFramework.h"

// 어빌리티 시스템(GameFramework/Base/Ability) — 언리얼 GAS 와 같은 규칙(집계 공식 · 지속 · 주기 · 스택 · 태그 개수 · 발동 조건 · 비용 · 쿨다운 ·
// 트리거 · 작업)을 컴포넌트 시간(`advanceTime`)으로 돌려 확인한다. 틱 중의 다른 오브젝트 적용과 핫 리로드 정리는 매니저 틱 · 보유자 훑기로 본다.

namespace sw
{
    /** @brief 커밋하고 바로 끝나는 시험 어빌리티입니다 — 발동 수 · 받은 트리거 크기를 셉니다. */
    class AbilityTestInstantAbility : public GameplayAbility
    {
    public:
        int32   _activateCount{ 0 };
        int32   _endCount{ 0 };
        float32 _lastTriggerMagnitude{ -1.0f };

        void activateAbility( const GameplayEventData* pTriggerEvent ) override
        {
            if ( commitAbility() == false )
            {
                cancelAbility();
                return;
            }
            ++_activateCount;
            if ( pTriggerEvent != nullptr )
                _lastTriggerMagnitude = pTriggerEvent->_magnitude;
            endAbility();
        }

        void onEndAbility( bool bWasCancelled ) override
        {
            (void)bWasCancelled;
            ++_endCount;
        }
    };
} // namespace sw

namespace sw
{
    /** @brief 1 초를 기다렸다가 끝나는 시험 어빌리티입니다 — 입력 · 취소를 셉니다. */
    class AbilityTestLatentAbility : public GameplayAbility
    {
    public:
        int32 _activateCount{ 0 };
        int32 _finishedCount{ 0 };
        int32 _cancelledCount{ 0 };
        int32 _inputPressedCount{ 0 };

        void activateAbility( const GameplayEventData* pTriggerEvent ) override
        {
            (void)pTriggerEvent;
            if ( commitAbility() == false )
            {
                cancelAbility();
                return;
            }
            ++_activateCount;
            (void)waitDelay( 1.0f, SW_DELEGATE_METHOD( Delegate<void()>, &AbilityTestLatentAbility::handleDelayFinished, this ) );
        }

        void onEndAbility( bool bWasCancelled ) override
        {
            if ( bWasCancelled )
                ++_cancelledCount;
        }

        void onInputPressed() override { ++_inputPressedCount; }

    private:
        void handleDelayFinished()
        {
            ++_finishedCount;
            endAbility();
        }
    };
} // namespace sw

namespace sw
{
    /** @brief "Event.Combo" 이벤트를 두 번 받으면 끝나는 시험 어빌리티입니다. */
    class AbilityTestComboAbility : public GameplayAbility
    {
    public:
        int32   _comboCount{ 0 };
        float32 _lastMagnitude{ 0.0f };

        void activateAbility( const GameplayEventData* pTriggerEvent ) override
        {
            (void)pTriggerEvent;
            (void)waitGameplayEvent( "Event.Combo"_tag, SW_DELEGATE_METHOD( Delegate<void( const GameplayEventData& )>, &AbilityTestComboAbility::handleCombo, this ),
                                     false );
        }

    private:
        void handleCombo( const GameplayEventData& payload )
        {
            ++_comboCount;
            _lastMagnitude = payload._magnitude;
            if ( _comboCount >= 2 )
                endAbility();
        }
    };
} // namespace sw

namespace sw
{
    /** @brief 틱 **안에서** 다른 오브젝트에 스펙을 거는 컴포넌트입니다 — 틱 직후로 미뤄지는지 봅니다. */
    class AbilityTestStrikeInTickComponent : public Component
    {
    public:
        REFLECT_BODY();

        AbilitySystemComponent*             _pSource{ nullptr };
        AbilitySystemComponent*             _pTarget{ nullptr };
        shared_ptr<const GameplayEffectDef> _pEffect{};
        float32                             _healthSeenInTick{ -1.0f };
        uint8                               _bHandleWasValid{ SW_FALSE };

        const TypeInfo* getTypeInfo() const override { return StaticType(); }
        void            onTick( float32 deltaTime ) override
        {
            Component::onTick( deltaTime );
            if ( _pTarget == nullptr || _pSource == nullptr )
                return;
            const ActiveEffectHandle handle = _pSource->applyGameplayEffectSpecToTarget( _pSource->makeOutgoingSpec( _pEffect ), _pTarget );
            _bHandleWasValid                = handle.isValid() ? SW_TRUE : SW_FALSE;
            _healthSeenInTick               = _pTarget->getAttributeValue( CombatAttributes::health() );
            _pTarget                        = nullptr;
        }
    };

    inline const TypeInfo* AbilityTestStrikeInTickComponent::StaticType()
    {
        return makeMockComponentTypeInfo( &GameObject::addComponentTo<AbilityTestStrikeInTickComponent>, hashed_string( "AbilityTestStrikeInTickComponent" ),
                                          hashed_string( "sw::AbilityTestStrikeInTickComponent" ), sizeof( AbilityTestStrikeInTickComponent ) );
    }
} // namespace sw

using namespace sw;

namespace
{
    /** @brief 이 시험 동안만 이벤트 버스를 게임 서비스로 겁니다. */
    struct AbilityTestScopedDispatcher
    {
        explicit AbilityTestScopedDispatcher( EventDispatcher& dispatcher ) { game::bindLocalService<EventDispatcher>( &dispatcher ); }
        ~AbilityTestScopedDispatcher() { game::unbindLocalService<EventDispatcher>(); }

        AbilityTestScopedDispatcher( const AbilityTestScopedDispatcher& )            = delete;
        AbilityTestScopedDispatcher& operator=( const AbilityTestScopedDispatcher& ) = delete;
    };

    /** @brief 어빌리티 시스템을 단 오브젝트를 만듭니다. */
    AbilitySystemComponent* spawnAbilityOwner( GameObjectManager& manager, const utf8* pName )
    {
        GameObject* pObject = manager.createGameObject( hashed_string( pName ) );
        return pObject != nullptr ? pObject->addComponent<AbilitySystemComponent>() : nullptr;
    }

    /** @brief 어빌리티 시스템 + 전투 어트리뷰트(체력 @p health · 방어 @p armor · 공격력 @p attackPower)를 단 오브젝트를 만듭니다. */
    AbilitySystemComponent* spawnCombatant( GameObjectManager& manager, const utf8* pName, float32 health, float32 armor, float32 attackPower )
    {
        AbilitySystemComponent* pAbilitySystem = spawnAbilityOwner( manager, pName );
        if ( pAbilitySystem == nullptr )
            return nullptr;
        unique_ptr<CombatAttributeSet> pSet = make_unique<CombatAttributeSet>();
        pSet->defineAttribute( CombatAttributes::health(), health );
        pSet->defineAttribute( CombatAttributes::maxHealth(), health );
        pSet->defineAttribute( CombatAttributes::armor(), armor );
        pSet->defineAttribute( CombatAttributes::attackPower(), attackPower );
        (void)pAbilitySystem->addAttributeSet( unique_ptr<AttributeSet>( std::move( pSet ) ) );
        return pAbilitySystem;
    }

    /** @brief HP 바의 보이기 정책 칸을 넣습니다 — 세터가 없는 PROPERTY 다. */
    bool setAbilityBarFlag( HealthBarComponent& bar, const utf8* pName, bool bValue )
    {
        const TypeInfo*     pTypeInfo = bar.getTypeInfo();
        const PropertyInfo* pProperty = ( pTypeInfo != nullptr ) ? pTypeInfo->findPropertyInHierarchy( hashed_string( pName ) ) : nullptr;
        if ( pProperty == nullptr )
            return false;
        pProperty->setValue<bool>( &bar, bValue );
        return true;
    }

    /** @brief 어트리뷰트 하나에 모디파이어 하나를 거는 이펙트 정의입니다. */
    shared_ptr<GameplayEffectDef> makeModifierEffect( const utf8* pId, EffectDurationPolicy policy, float32 seconds, const hashed_string& attribute,
                                                      AttributeModOp op, float32 magnitude )
    {
        shared_ptr<GameplayEffectDef> pDef = make_shared<GameplayEffectDef>();
        pDef->_id                          = hashed_string( pId );
        pDef->_durationPolicy              = policy;
        pDef->_duration._baseValue         = seconds;
        GameplayEffectModifier modifier;
        modifier._attribute                    = attribute;
        modifier._op                           = op;
        modifier._scalableMagnitude._baseValue = magnitude;
        pDef->_listModifier.push_back( modifier );
        return pDef;
    }

    /** @brief "Power" 하나(기본값 @p baseValue)를 가진 일반 묶음을 붙입니다. */
    void addPowerAttribute( AbilitySystemComponent& abilitySystem, float32 baseValue )
    {
        unique_ptr<AttributeSet> pSet = make_unique<AttributeSet>();
        pSet->defineAttribute( "Power", baseValue );
        (void)abilitySystem.addAttributeSet( std::move( pSet ) );
    }

    /** @brief 같은 시간을 @p count 번 나눠 돌립니다. */
    void advanceRepeatedly( AbilitySystemComponent& abilitySystem, float32 deltaTime, int32 count )
    {
        for ( int32 stepIndex = 0; stepIndex < count; ++stepIndex )
            abilitySystem.advanceTime( deltaTime );
    }

    /** @brief 시험 카탈로그 — 비용 · 쿨다운 · 피해 · 회복 · 세트. */
    constexpr const utf8* kAbilityTestCatalogXml = R"(
<AbilityCatalog>
  <Ability id="GA_Heal" class="ApplyEffects" cost="GE_Cost_Mana20" cooldown="GE_Cooldown_Heal">
    <AbilityTag tag="Ability.Skill.Heal"/>
    <BlockedTag tag="State.Dead"/>
    <Param name="selfEffect" text="GE_Heal"/>
  </Ability>
  <GameplayEffect id="GE_Cost_Mana20" duration="Instant">
    <Modifier attribute="Mana" op="Add" magnitude="-20"/>
  </GameplayEffect>
  <GameplayEffect id="GE_Cooldown_Heal" duration="HasDuration" seconds="3">
    <GrantedTag tag="Cooldown.Skill.Heal"/>
  </GameplayEffect>
  <GameplayEffect id="GE_Heal" duration="Instant">
    <Modifier attribute="IncomingHealing" op="Add" magnitude="25" perLevel="5"/>
    <Cue tag="GameplayCue.Heal"/>
  </GameplayEffect>
  <GameplayEffect id="GE_Damage" duration="Instant">
    <Execution class="Damage" setByCaller="Damage" attackPowerCoefficient="1" base="0"/>
  </GameplayEffect>
  <GameplayEffect id="GE_Regen" duration="Infinite" period="1" executeOnApplication="false">
    <Modifier attribute="Mana" op="Add" magnitude="2"/>
    <AssetTag tag="Effect.Regen"/>
  </GameplayEffect>
  <AbilitySet id="Hero">
    <AttributeSet class="Combat">
      <Attribute name="Health" base="80"/>
      <Attribute name="MaxHealth" base="120"/>
      <Attribute name="Mana" base="50"/>
    </AttributeSet>
    <AttributeSet class="Generic">
      <Attribute name="Stamina" base="150" min="0" max="100"/>
    </AttributeSet>
    <Ability id="GA_Heal" level="2" input="3"/>
    <Effect id="GE_Regen"/>
    <Tag tag="Team.Hero"/>
  </AbilitySet>
</AbilityCatalog>
)";
} // namespace

/**
 * @brief [AbilitySystemTest] 지속 이펙트의 집계는 언리얼 공식이다 — ((base + ΣAdd) × (1 + Σ(곱 − 1))) ÷ (1 + Σ(나누기 − 1)), 덮어쓰기가 이긴다
 * @details 곱은 보너스를 더한다(×1.5 와 ×1.2 는 ×1.7). 이펙트가 풀리면 그 몫만 빠지고, 다 풀리면 current 가 base 로 돌아온다. base 는 지속 이펙트로 바뀌지 않는다.
 */
SW_TEST_CASE( AbilitySystemTest, DurationEffectsAggregateWithUnrealFormulaAndRevertOnRemoval )
{
    GameObjectManager       manager;
    AbilitySystemComponent* pAbilitySystem = spawnAbilityOwner( manager, "Owner" );
    SW_ASSERT_NOT_NULL( pAbilitySystem );
    addPowerAttribute( *pAbilitySystem, 10.0f );

    const hashed_string      power{ "Power" };
    const ActiveEffectHandle addHandle = pAbilitySystem->applyGameplayEffectSpecToSelf(
        pAbilitySystem->makeOutgoingSpec( makeModifierEffect( "Add5", EffectDurationPolicy::Infinite, 0.0f, power, AttributeModOp::Add, 5.0f ) ) );
    const ActiveEffectHandle mulHandle = pAbilitySystem->applyGameplayEffectSpecToSelf(
        pAbilitySystem->makeOutgoingSpec( makeModifierEffect( "Mul15", EffectDurationPolicy::Infinite, 0.0f, power, AttributeModOp::Multiply, 1.5f ) ) );
    (void)pAbilitySystem->applyGameplayEffectSpecToSelf(
        pAbilitySystem->makeOutgoingSpec( makeModifierEffect( "Mul12", EffectDurationPolicy::Infinite, 0.0f, power, AttributeModOp::Multiply, 1.2f ) ) );
    const ActiveEffectHandle divHandle = pAbilitySystem->applyGameplayEffectSpecToSelf(
        pAbilitySystem->makeOutgoingSpec( makeModifierEffect( "Div2", EffectDurationPolicy::Infinite, 0.0f, power, AttributeModOp::Divide, 2.0f ) ) );
    SW_ASSERT_TRUE( addHandle.isValid() && mulHandle.isValid() && divHandle.isValid() );
    SW_EXPECT_EQUAL( 4u, pAbilitySystem->getActiveEffectCount() );

    SW_EXPECT_NEAR_EQUAL( 12.75f, pAbilitySystem->getAttributeValue( power ), 0.001f ); // (10 + 5) × 1.7 ÷ 2
    SW_EXPECT_NEAR_EQUAL( 10.0f, pAbilitySystem->getAttributeBaseValue( power ), 0.001f );

    SW_EXPECT_TRUE( pAbilitySystem->removeActiveEffect( divHandle ) );
    SW_EXPECT_NEAR_EQUAL( 25.5f, pAbilitySystem->getAttributeValue( power ), 0.001f );

    const ActiveEffectHandle overrideHandle = pAbilitySystem->applyGameplayEffectSpecToSelf(
        pAbilitySystem->makeOutgoingSpec( makeModifierEffect( "Set3", EffectDurationPolicy::Infinite, 0.0f, power, AttributeModOp::Override, 3.0f ) ) );
    SW_EXPECT_NEAR_EQUAL( 3.0f, pAbilitySystem->getAttributeValue( power ), 0.001f );
    SW_EXPECT_TRUE( pAbilitySystem->removeActiveEffect( overrideHandle ) );

    SW_EXPECT_TRUE( pAbilitySystem->removeActiveEffect( addHandle ) );
    SW_EXPECT_TRUE( pAbilitySystem->removeActiveEffect( mulHandle ) );
    SW_EXPECT_NEAR_EQUAL( 12.0f, pAbilitySystem->getAttributeValue( power ), 0.001f ); // ×1.2 하나만 남았다
    SW_EXPECT_FALSE( pAbilitySystem->removeActiveEffect( mulHandle ) );                // 이미 풀렸다
}

/**
 * @brief [AbilitySystemTest] 즉시 이펙트는 base 를 바꾸고 활성 목록에 남지 않는다 · 지속 이펙트는 시간이 다 되면 풀린다
 * @details 즉시 이펙트도 적용에 성공하면 유효한 핸들을 받지만 `isActiveEffect` 는 false 다. 지속 이펙트의 남은 시간은 컴포넌트 시간으로 준다.
 */
SW_TEST_CASE( AbilitySystemTest, InstantEffectWritesBaseAndDurationEffectExpires )
{
    GameObjectManager       manager;
    AbilitySystemComponent* pAbilitySystem = spawnAbilityOwner( manager, "Owner" );
    SW_ASSERT_NOT_NULL( pAbilitySystem );
    addPowerAttribute( *pAbilitySystem, 10.0f );
    const hashed_string power{ "Power" };

    const ActiveEffectHandle instantHandle = pAbilitySystem->applyGameplayEffectSpecToSelf(
        pAbilitySystem->makeOutgoingSpec( makeModifierEffect( "Plus10", EffectDurationPolicy::Instant, 0.0f, power, AttributeModOp::Add, 10.0f ) ) );
    SW_EXPECT_TRUE( instantHandle.isValid() );
    SW_EXPECT_FALSE( pAbilitySystem->isActiveEffect( instantHandle ) );
    SW_EXPECT_NEAR_EQUAL( 20.0f, pAbilitySystem->getAttributeBaseValue( power ), 0.001f );

    const ActiveEffectHandle buffHandle = pAbilitySystem->applyGameplayEffectSpecToSelf(
        pAbilitySystem->makeOutgoingSpec( makeModifierEffect( "Buff", EffectDurationPolicy::HasDuration, 2.0f, power, AttributeModOp::Add, 5.0f ) ) );
    SW_EXPECT_NEAR_EQUAL( 25.0f, pAbilitySystem->getAttributeValue( power ), 0.001f );
    pAbilitySystem->advanceTime( 1.5f );
    SW_EXPECT_TRUE( pAbilitySystem->isActiveEffect( buffHandle ) );
    SW_EXPECT_NEAR_EQUAL( 0.5f, pAbilitySystem->getActiveEffectRemainingTime( buffHandle ), 0.001f );
    pAbilitySystem->advanceTime( 0.5f );
    SW_EXPECT_FALSE( pAbilitySystem->isActiveEffect( buffHandle ) );
    SW_EXPECT_NEAR_EQUAL( 20.0f, pAbilitySystem->getAttributeValue( power ), 0.001f );
    SW_EXPECT_EQUAL( 0u, pAbilitySystem->getActiveEffectCount() );
}

/**
 * @brief [AbilitySystemTest] 주기 이펙트는 주기마다 base 에 쓰고, 만료 뒤의 주기는 실행하지 않는다 — 한 프레임이 주기 여럿을 넘어도
 * @details 3 초 · 1 초 주기 · 걸 때 실행 안 함이면 1 · 2 · 3 초에 세 번이다. 0.5 초씩 여섯 번이든 10 초 한 번이든 같다.
 */
SW_TEST_CASE( AbilitySystemTest, PeriodicEffectExecutesEachPeriodUntilItExpires )
{
    GameObjectManager       manager;
    AbilitySystemComponent* pSmallSteps = spawnAbilityOwner( manager, "SmallSteps" );
    AbilitySystemComponent* pBigStep    = spawnAbilityOwner( manager, "BigStep" );
    SW_ASSERT_TRUE( pSmallSteps != nullptr && pBigStep != nullptr );
    addPowerAttribute( *pSmallSteps, 100.0f );
    addPowerAttribute( *pBigStep, 100.0f );
    const hashed_string power{ "Power" };

    shared_ptr<GameplayEffectDef> pDot   = makeModifierEffect( "Dot", EffectDurationPolicy::HasDuration, 3.0f, power, AttributeModOp::Add, -10.0f );
    pDot->_period                        = 1.0f;
    pDot->_bExecutePeriodicOnApplication = SW_FALSE;
    const ActiveEffectHandle smallHandle = pSmallSteps->applyGameplayEffectSpecToSelf( pSmallSteps->makeOutgoingSpec( pDot ) );
    const ActiveEffectHandle bigHandle   = pBigStep->applyGameplayEffectSpecToSelf( pBigStep->makeOutgoingSpec( pDot ) );
    SW_ASSERT_TRUE( smallHandle.isValid() && bigHandle.isValid() );
    SW_EXPECT_NEAR_EQUAL( 100.0f, pSmallSteps->getAttributeValue( power ), 0.001f ); // 주기 이펙트는 current 에 얹지 않는다

    advanceRepeatedly( *pSmallSteps, 0.5f, 6 );
    pBigStep->advanceTime( 10.0f );
    SW_EXPECT_NEAR_EQUAL( 70.0f, pSmallSteps->getAttributeBaseValue( power ), 0.001f );
    SW_EXPECT_NEAR_EQUAL( 70.0f, pBigStep->getAttributeBaseValue( power ), 0.001f );
    SW_EXPECT_FALSE( pSmallSteps->isActiveEffect( smallHandle ) );
    SW_EXPECT_FALSE( pBigStep->isActiveEffect( bigHandle ) );

    // 걸 때 실행하면 0 · 1 · 2 · 3 초 네 번이다.
    pDot->_bExecutePeriodicOnApplication = SW_TRUE;
    (void)pSmallSteps->applyGameplayEffectSpecToSelf( pSmallSteps->makeOutgoingSpec( pDot ) );
    SW_EXPECT_NEAR_EQUAL( 60.0f, pSmallSteps->getAttributeBaseValue( power ), 0.001f );
    pSmallSteps->advanceTime( 5.0f );
    SW_EXPECT_NEAR_EQUAL( 30.0f, pSmallSteps->getAttributeBaseValue( power ), 0.001f );
}

/**
 * @brief [AbilitySystemTest] 대상 기준 스택은 하나의 인스턴스에 상한까지 쌓고 시간을 다시 채운다 · 하나씩 내리는 만료는 스택을 하나씩 내린다
 */
SW_TEST_CASE( AbilitySystemTest, StackingAggregatesUpToTheLimitAndExpiresOneStackAtATime )
{
    GameObjectManager       manager;
    AbilitySystemComponent* pAbilitySystem = spawnAbilityOwner( manager, "Owner" );
    SW_ASSERT_NOT_NULL( pAbilitySystem );
    addPowerAttribute( *pAbilitySystem, 0.0f );
    const hashed_string power{ "Power" };

    shared_ptr<GameplayEffectDef> pStack = makeModifierEffect( "Stack", EffectDurationPolicy::HasDuration, 2.0f, power, AttributeModOp::Add, 2.0f );
    pStack->_stackingPolicy              = EffectStackingPolicy::AggregateByTarget;
    pStack->_stackLimit                  = 3;
    pStack->_stackExpirationPolicy       = EffectStackExpirationPolicy::RemoveSingleStackAndRefreshDuration;

    const ActiveEffectHandle firstHandle = pAbilitySystem->applyGameplayEffectSpecToSelf( pAbilitySystem->makeOutgoingSpec( pStack ) );
    pAbilitySystem->advanceTime( 1.5f );
    for ( int32 applyIndex = 0; applyIndex < 3; ++applyIndex )
        SW_EXPECT_TRUE( pAbilitySystem->applyGameplayEffectSpecToSelf( pAbilitySystem->makeOutgoingSpec( pStack ) ) == firstHandle );

    SW_EXPECT_EQUAL( 1u, pAbilitySystem->getActiveEffectCount() );
    SW_EXPECT_EQUAL( 3, pAbilitySystem->getActiveEffectStackCount( firstHandle ) );
    SW_EXPECT_NEAR_EQUAL( 6.0f, pAbilitySystem->getAttributeValue( power ), 0.001f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, pAbilitySystem->getActiveEffectRemainingTime( firstHandle ), 0.001f ); // 쌓을 때 다시 채웠다

    pAbilitySystem->advanceTime( 2.0f );
    SW_EXPECT_EQUAL( 2, pAbilitySystem->getActiveEffectStackCount( firstHandle ) );
    SW_EXPECT_NEAR_EQUAL( 4.0f, pAbilitySystem->getAttributeValue( power ), 0.001f );

    SW_EXPECT_TRUE( pAbilitySystem->removeActiveEffect( firstHandle, 1 ) );
    SW_EXPECT_EQUAL( 1, pAbilitySystem->getActiveEffectStackCount( firstHandle ) );
    pAbilitySystem->advanceTime( 2.0f );
    SW_EXPECT_FALSE( pAbilitySystem->isActiveEffect( firstHandle ) );
    SW_EXPECT_NEAR_EQUAL( 0.0f, pAbilitySystem->getAttributeValue( power ), 0.001f );
}

/**
 * @brief [AbilitySystemTest] 부여 태그는 개수로 센다 — 같은 태그를 주는 이펙트 둘이 다 풀려야 사라지고, 알림은 생길 때와 사라질 때 한 번씩이다
 * @details 대상이 막는 태그(면역)를 가졌으면 이펙트가 걸리지 않고, 정화(`_removeEffectsWithTags`)는 부여 태그로 걸린 것을 지운다.
 */
SW_TEST_CASE( AbilitySystemTest, GrantedTagsAreCountedAndGateApplicationAndCleanse )
{
    GameObjectManager       manager;
    AbilitySystemComponent* pAbilitySystem = spawnAbilityOwner( manager, "Owner" );
    SW_ASSERT_NOT_NULL( pAbilitySystem );
    addPowerAttribute( *pAbilitySystem, 0.0f );
    const hashed_string power{ "Power" };

    int32 tagEventCount = 0;
    (void)pAbilitySystem->registerTagChanged( SW_DELEGATE_LAMBDA( AbilitySystemComponent::TagChangedDelegate, [&tagEventCount]( TagID tag, int32 newCount )
    {
        (void)newCount;
        if ( tag == "State.Burning"_tag )
            ++tagEventCount;
    } ) );

    shared_ptr<GameplayEffectDef> pBurn = makeModifierEffect( "Burn", EffectDurationPolicy::Infinite, 0.0f, power, AttributeModOp::Add, 1.0f );
    pBurn->_grantedTags.addTag( "State.Burning.Hot"_tag );
    pBurn->_grantedTags.addTag( "State.Burning"_tag );
    pBurn->_applicationBlockedTags.addTag( "State.Immune.Fire"_tag );

    const ActiveEffectHandle firstHandle  = pAbilitySystem->applyGameplayEffectSpecToSelf( pAbilitySystem->makeOutgoingSpec( pBurn ) );
    const ActiveEffectHandle secondHandle = pAbilitySystem->applyGameplayEffectSpecToSelf( pAbilitySystem->makeOutgoingSpec( pBurn ) );
    SW_EXPECT_EQUAL( 2, pAbilitySystem->getTagCount( "State.Burning"_tag ) );
    SW_EXPECT_TRUE( pAbilitySystem->hasMatchingTag( "State"_tag ) ); // 조상 태그로도 맞는다
    SW_EXPECT_EQUAL( 1, tagEventCount );

    SW_EXPECT_TRUE( pAbilitySystem->removeActiveEffect( firstHandle ) );
    SW_EXPECT_TRUE( pAbilitySystem->hasMatchingTag( "State.Burning"_tag ) );
    SW_EXPECT_TRUE( pAbilitySystem->removeActiveEffect( secondHandle ) );
    SW_EXPECT_FALSE( pAbilitySystem->hasMatchingTag( "State.Burning"_tag ) );
    SW_EXPECT_EQUAL( 2, tagEventCount );

    pAbilitySystem->addLooseTag( "State.Immune.Fire"_tag );
    SW_EXPECT_FALSE( pAbilitySystem->applyGameplayEffectSpecToSelf( pAbilitySystem->makeOutgoingSpec( pBurn ) ).isValid() );
    pAbilitySystem->removeLooseTag( "State.Immune.Fire"_tag );
    SW_EXPECT_TRUE( pAbilitySystem->applyGameplayEffectSpecToSelf( pAbilitySystem->makeOutgoingSpec( pBurn ) ).isValid() );

    shared_ptr<GameplayEffectDef> pCleanse = makeModifierEffect( "Cleanse", EffectDurationPolicy::Instant, 0.0f, power, AttributeModOp::Add, 0.0f );
    pCleanse->_removeEffectsWithTags.addTag( "State.Burning"_tag );
    (void)pAbilitySystem->applyGameplayEffectSpecToSelf( pAbilitySystem->makeOutgoingSpec( pCleanse ) );
    SW_EXPECT_EQUAL( 0u, pAbilitySystem->getActiveEffectCount() );
    SW_EXPECT_FALSE( pAbilitySystem->hasMatchingTag( "State.Burning"_tag ) );
}

/**
 * @brief [AbilitySystemTest] 피해 공식은 방어로 감쇠하고 메타 어트리뷰트를 거쳐 체력을 깎는다 · 쓰러짐은 처음 한 번만 알린다
 * @details 원피해 = SetByCaller 30 + 쏜 쪽 공격력 20 = 50, 방어 100 이면 ÷2 = 25. 체력이 0 에 닿으면 `State.Dead` 가 붙고 `AbilityOwnerDiedEvent` 가
 *          쏜 쪽을 싣고 한 번 나간다. 그 뒤의 피해는 무시하고, 메타 어트리뷰트는 늘 0 으로 돌아온다. HP 바가 체력 비율을 따른다.
 */
SW_TEST_CASE( AbilitySystemTest, DamageExecutionMitigatesByArmorAndKillsExactlyOnce )
{
    EventDispatcher                   dispatcher;
    const AbilityTestScopedDispatcher scopedDispatcher{ dispatcher };
    vector<AbilityOwnerDiedEvent>     listDied;
    dispatcher.subscribe<AbilityOwnerDiedEvent>( gameEventChannel(), SW_DELEGATE_LAMBDA( Delegate<void( const AbilityOwnerDiedEvent& )>, [&listDied]( const AbilityOwnerDiedEvent& event )
    { listDied.push_back( event ); } ) );

    GameObjectManager       manager;
    AbilitySystemComponent* pAttacker = spawnCombatant( manager, "Attacker", 100.0f, 0.0f, 20.0f );
    AbilitySystemComponent* pTarget   = spawnCombatant( manager, "Target", 100.0f, 100.0f, 0.0f );
    SW_ASSERT_TRUE( pAttacker != nullptr && pTarget != nullptr );
    HealthBarComponent* pBar = pTarget->getOwner()->addComponent<HealthBarComponent>();
    SW_ASSERT_NOT_NULL( pBar );

    shared_ptr<GameplayEffectDef> pDamage  = make_shared<GameplayEffectDef>();
    pDamage->_id                           = hashed_string( "Damage" );
    shared_ptr<DamageExecution> pExecution = make_shared<DamageExecution>();
    pExecution->setParameters( "Damage", 1.0f, 0.0f );
    pDamage->_listExecution.push_back( pExecution );

    GameplayEffectSpec spec = pAttacker->makeOutgoingSpec( pDamage );
    spec.setSetByCallerMagnitude( "Damage", 30.0f );

    int32 hitCount = 0;
    (void)pTarget->registerGameplayEvent( CombatAttributeSet::getHitEventTag(), SW_DELEGATE_LAMBDA( AbilitySystemComponent::GameplayEventDelegate, [&hitCount]( const GameplayEventData& payload )
    {
        (void)payload;
        ++hitCount;
    } ) );

    (void)pAttacker->applyGameplayEffectSpecToTarget( spec, pTarget );
    SW_EXPECT_NEAR_EQUAL( 75.0f, pTarget->getAttributeValue( CombatAttributes::health() ), 0.001f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, pTarget->getAttributeBaseValue( CombatAttributes::incomingDamage() ), 0.001f );
    SW_EXPECT_NEAR_EQUAL( 0.75f, pBar->getTargetRatio(), 0.001f );
    SW_EXPECT_EQUAL( 1, hitCount );

    for ( int32 hitIndex = 0; hitIndex < 5; ++hitIndex )
        (void)pAttacker->applyGameplayEffectSpecToTarget( spec, pTarget );
    dispatcher.processEvents();

    SW_EXPECT_NEAR_EQUAL( 0.0f, pTarget->getAttributeValue( CombatAttributes::health() ), 0.001f );
    SW_EXPECT_TRUE( pTarget->hasMatchingTag( CombatAttributeSet::getDeadTag() ) );
    SW_EXPECT_EQUAL( 1, pTarget->getTagCount( CombatAttributeSet::getDeadTag() ) );
    SW_ASSERT_EQUAL( static_cast<size_t>( 1 ), listDied.size() );
    SW_EXPECT_TRUE( listDied[0]._target == pTarget->getOwner()->getHandle() );
    SW_EXPECT_TRUE( listDied[0]._instigator == pAttacker->getOwner()->getHandle() );
    SW_EXPECT_EQUAL( 4, hitCount ); // 75 → 50 → 25 → 0, 그 뒤 둘은 무시
}

/**
 * @brief [AbilitySystemTest] 체력이 0 에 닿으면 HP 바가 쓰러짐을 받는다 — `_bHideWhenDead` 인 바는 숨는다
 * @details 어빌리티 시스템은 체력 원천(`HealthSourceComponent`)이고 알림 종류를 읽기(체력 0 = 쓰러짐)에서 정한다. 바뀜(`Changed`)으로만 알리면 쓰러져도 숨지 않는다.
 */
SW_TEST_CASE( AbilitySystemTest, HealthBarHearsTheOwnerDie )
{
    GameObjectManager       manager;
    AbilitySystemComponent* pTarget = spawnCombatant( manager, "Target", 100.0f, 0.0f, 0.0f );
    SW_ASSERT_NOT_NULL( pTarget );
    HealthBarComponent* pBar = pTarget->getOwner()->addComponent<HealthBarComponent>();
    SW_ASSERT_NOT_NULL( pBar );
    SW_ASSERT_TRUE( setAbilityBarFlag( *pBar, "_bHideWhenDead", true ) );
    pBar->setVisible( true );

    SW_ASSERT_TRUE( pTarget->setAttributeBaseValue( CombatAttributes::health(), 60.0f ) );
    SW_EXPECT_NEAR_EQUAL( 0.6f, pBar->getTargetRatio(), 0.001f );
    SW_EXPECT_TRUE( pBar->isVisible() );

    SW_ASSERT_TRUE( pTarget->setAttributeBaseValue( CombatAttributes::health(), 0.0f ) );
    SW_EXPECT_NEAR_EQUAL( 0.0f, pBar->getTargetRatio(), 0.001f );
    SW_EXPECT_FALSE( pBar->isVisible() );
}

/**
 * @brief [AbilitySystemTest] 커밋은 비용 · 쿨다운을 걸고, 쿨다운 · 비용 부족은 발동을 막는다 · 쿨다운이 끝나면 다시 발동한다
 */
SW_TEST_CASE( AbilitySystemTest, CommitAppliesCostAndCooldownWhichGateTheNextActivation )
{
    GameObjectManager       manager;
    AbilitySystemComponent* pAbilitySystem = spawnCombatant( manager, "Caster", 100.0f, 0.0f, 0.0f );
    SW_ASSERT_NOT_NULL( pAbilitySystem );

    shared_ptr<GameplayEffectDef> pCost     = makeModifierEffect( "Cost", EffectDurationPolicy::Instant, 0.0f, CombatAttributes::mana(), AttributeModOp::Add, -40.0f );
    shared_ptr<GameplayEffectDef> pCooldown = make_shared<GameplayEffectDef>();
    pCooldown->_id                          = hashed_string( "Cooldown" );
    pCooldown->_durationPolicy              = EffectDurationPolicy::HasDuration;
    pCooldown->_duration._baseValue         = 2.0f;
    pCooldown->_grantedTags.addTag( "Cooldown.Test"_tag );

    unique_ptr<AbilityTestInstantAbility> pAbility = make_unique<AbilityTestInstantAbility>();
    pAbility->getConfig()._pCostEffect             = pCost;
    pAbility->getConfig()._pCooldownEffect         = pCooldown;
    AbilityTestInstantAbility* pRaw                = pAbility.get();
    const AbilitySpecHandle    handle              = pAbilitySystem->giveAbility( std::move( pAbility ) );
    SW_ASSERT_TRUE( handle.isValid() );

    SW_EXPECT_TRUE( pAbilitySystem->tryActivateAbility( handle ) == AbilityActivationResult::Activated );
    SW_EXPECT_NEAR_EQUAL( 60.0f, pAbilitySystem->getAttributeValue( CombatAttributes::mana() ), 0.001f );
    SW_EXPECT_TRUE( pRaw->isOnCooldown() );
    SW_EXPECT_NEAR_EQUAL( 2.0f, pRaw->getCooldownRemaining(), 0.001f );
    SW_EXPECT_TRUE( pAbilitySystem->tryActivateAbility( handle ) == AbilityActivationResult::OnCooldown );

    pAbilitySystem->advanceTime( 2.0f );
    SW_EXPECT_TRUE( pAbilitySystem->tryActivateAbility( handle ) == AbilityActivationResult::Activated );
    SW_EXPECT_NEAR_EQUAL( 20.0f, pAbilitySystem->getAttributeValue( CombatAttributes::mana() ), 0.001f );

    pAbilitySystem->advanceTime( 2.0f );
    SW_EXPECT_TRUE( pAbilitySystem->canActivateAbility( handle ) == AbilityActivationResult::CannotAffordCost );
    SW_EXPECT_TRUE( pAbilitySystem->tryActivateAbility( handle ) == AbilityActivationResult::CannotAffordCost );
    SW_EXPECT_EQUAL( 2, pRaw->_activateCount );
    SW_EXPECT_EQUAL( 2, pRaw->_endCount );
    SW_EXPECT_FALSE( pRaw->isActive() );
}

/**
 * @brief [AbilitySystemTest] 도는 어빌리티의 태그가 다른 어빌리티를 막고 · 취소하며, 주인의 태그가 발동을 막는다
 * @details 시전(A)은 도는 동안 `State.Casting` 을 붙이고 `Ability.Movement` 를 막는다. 이동(B)은 그동안 막힌다. 끊기(C)는 발동하며 `Ability.Skill`
 *          (A)을 취소하고, 그러면 A 가 붙였던 태그 · 막기가 풀린다. 주인이 `State.Dead` 를 가지면 그것을 막는 태그로 둔 어빌리티는 발동하지 못한다.
 */
SW_TEST_CASE( AbilitySystemTest, ActivationTagsBlockAndCancelOtherAbilities )
{
    GameObjectManager       manager;
    AbilitySystemComponent* pAbilitySystem = spawnAbilityOwner( manager, "Owner" );
    SW_ASSERT_NOT_NULL( pAbilitySystem );

    unique_ptr<AbilityTestLatentAbility> pCast = make_unique<AbilityTestLatentAbility>();
    pCast->getConfig()._abilityTags.addTag( "Ability.Skill.Cast"_tag );
    pCast->getConfig()._activationOwnedTags.addTag( "State.Casting"_tag );
    pCast->getConfig()._blockAbilitiesWithTags.addTag( "Ability.Movement"_tag );
    AbilityTestLatentAbility* pCastRaw = pCast.get();

    unique_ptr<AbilityTestInstantAbility> pMove = make_unique<AbilityTestInstantAbility>();
    pMove->getConfig()._abilityTags.addTag( "Ability.Movement.Dash"_tag );
    pMove->getConfig()._activationBlockedTags.addTag( "State.Dead"_tag );

    unique_ptr<AbilityTestInstantAbility> pInterrupt = make_unique<AbilityTestInstantAbility>();
    pInterrupt->getConfig()._cancelAbilitiesWithTags.addTag( "Ability.Skill"_tag );

    const AbilitySpecHandle castHandle      = pAbilitySystem->giveAbility( std::move( pCast ) );
    const AbilitySpecHandle moveHandle      = pAbilitySystem->giveAbility( std::move( pMove ) );
    const AbilitySpecHandle interruptHandle = pAbilitySystem->giveAbility( std::move( pInterrupt ) );
    SW_EXPECT_EQUAL( 3u, pAbilitySystem->getAbilityCount() );

    SW_EXPECT_TRUE( pAbilitySystem->tryActivateAbility( castHandle ) == AbilityActivationResult::Activated );
    SW_EXPECT_TRUE( pAbilitySystem->hasMatchingTag( "State.Casting"_tag ) );
    SW_EXPECT_TRUE( pAbilitySystem->tryActivateAbility( castHandle ) == AbilityActivationResult::AlreadyActive );
    SW_EXPECT_TRUE( pAbilitySystem->tryActivateAbility( moveHandle ) == AbilityActivationResult::BlockedByTag );

    SW_EXPECT_TRUE( pAbilitySystem->tryActivateAbility( interruptHandle ) == AbilityActivationResult::Activated );
    SW_EXPECT_FALSE( pAbilitySystem->isAbilityActive( castHandle ) );
    SW_EXPECT_EQUAL( 1, pCastRaw->_cancelledCount );
    SW_EXPECT_FALSE( pAbilitySystem->hasMatchingTag( "State.Casting"_tag ) );
    SW_EXPECT_TRUE( pAbilitySystem->tryActivateAbility( moveHandle ) == AbilityActivationResult::Activated );

    pAbilitySystem->addLooseTag( "State.Dead"_tag );
    SW_EXPECT_TRUE( pAbilitySystem->tryActivateAbility( moveHandle ) == AbilityActivationResult::BlockedByTag );
}

/**
 * @brief [AbilitySystemTest] 시간이 걸리는 어빌리티는 작업이 끝낼 때까지 돌고, 입력이 다시 눌리면 어빌리티가 받으며, 거두면 취소된다
 */
SW_TEST_CASE( AbilitySystemTest, LatentAbilityWaitsForItsTaskAndReceivesInput )
{
    GameObjectManager       manager;
    AbilitySystemComponent* pAbilitySystem = spawnAbilityOwner( manager, "Owner" );
    SW_ASSERT_NOT_NULL( pAbilitySystem );

    unique_ptr<AbilityTestLatentAbility> pAbility = make_unique<AbilityTestLatentAbility>();
    AbilityTestLatentAbility*            pRaw     = pAbility.get();
    const AbilitySpecHandle              handle   = pAbilitySystem->giveAbility( std::move( pAbility ), 1, 7 );

    pAbilitySystem->abilityInputPressed( 7 );
    SW_EXPECT_TRUE( pRaw->isActive() );
    SW_EXPECT_EQUAL( 1u, pRaw->getActiveTaskCount() );
    SW_EXPECT_EQUAL( 0, pRaw->_inputPressedCount ); // 발동시킨 눌림은 알림이 아니다

    pAbilitySystem->abilityInputReleased( 7 );
    pAbilitySystem->abilityInputPressed( 7 );
    SW_EXPECT_EQUAL( 1, pRaw->_inputPressedCount );
    SW_EXPECT_EQUAL( 1, pRaw->_activateCount );

    pAbilitySystem->advanceTime( 0.6f );
    SW_EXPECT_TRUE( pRaw->isActive() );
    pAbilitySystem->advanceTime( 0.6f );
    SW_EXPECT_FALSE( pRaw->isActive() );
    SW_EXPECT_EQUAL( 1, pRaw->_finishedCount );
    SW_EXPECT_EQUAL( 0u, pRaw->getActiveTaskCount() );

    // 도는 중에 거두면 취소된 뒤 사라진다.
    SW_EXPECT_TRUE( pAbilitySystem->tryActivateAbility( handle ) == AbilityActivationResult::Activated );
    SW_EXPECT_TRUE( pAbilitySystem->clearAbility( handle ) );
    SW_EXPECT_EQUAL( 0u, pAbilitySystem->getAbilityCount() );
    SW_EXPECT_NULL( pAbilitySystem->findAbility( handle ) );
}

/**
 * @brief [AbilitySystemTest] 게임플레이 이벤트가 트리거 태그(또는 그 하위)로 어빌리티를 발동하고 페이로드를 넘기며, 대기 작업은 같은 이벤트를 받는다
 */
SW_TEST_CASE( AbilitySystemTest, GameplayEventsTriggerAbilitiesAndWakeWaitingTasks )
{
    GameObjectManager       manager;
    AbilitySystemComponent* pAbilitySystem = spawnAbilityOwner( manager, "Owner" );
    SW_ASSERT_NOT_NULL( pAbilitySystem );

    unique_ptr<AbilityTestInstantAbility> pReaction = make_unique<AbilityTestInstantAbility>();
    pReaction->getConfig()._triggerEventTags.addTag( "Event.Hit"_tag );
    AbilityTestInstantAbility* pReactionRaw = pReaction.get();
    (void)pAbilitySystem->giveAbility( std::move( pReaction ) );

    unique_ptr<AbilityTestComboAbility> pCombo      = make_unique<AbilityTestComboAbility>();
    AbilityTestComboAbility*            pComboRaw   = pCombo.get();
    const AbilitySpecHandle             comboHandle = pAbilitySystem->giveAbility( std::move( pCombo ) );

    GameplayEventData payload;
    payload._magnitude = 12.0f;
    SW_EXPECT_EQUAL( 1, pAbilitySystem->handleGameplayEvent( "Event.Hit.Fire"_tag, payload ) );
    SW_EXPECT_EQUAL( 1, pReactionRaw->_activateCount );
    SW_EXPECT_NEAR_EQUAL( 12.0f, pReactionRaw->_lastTriggerMagnitude, 0.001f );
    SW_EXPECT_EQUAL( 0, pAbilitySystem->handleGameplayEvent( "Event.Heal"_tag, payload ) );

    SW_EXPECT_TRUE( pAbilitySystem->tryActivateAbility( comboHandle ) == AbilityActivationResult::Activated );
    payload._magnitude = 1.0f;
    (void)pAbilitySystem->handleGameplayEvent( "Event.Combo.Light"_tag, payload );
    SW_EXPECT_TRUE( pComboRaw->isActive() );
    payload._magnitude = 2.0f;
    (void)pAbilitySystem->handleGameplayEvent( "Event.Combo"_tag, payload );
    SW_EXPECT_FALSE( pComboRaw->isActive() );
    SW_EXPECT_EQUAL( 2, pComboRaw->_comboCount );
    SW_EXPECT_NEAR_EQUAL( 2.0f, pComboRaw->_lastMagnitude, 0.001f );

    // 끝난 뒤의 이벤트는 작업이 받지 않는다(구독이 떨어졌다).
    (void)pAbilitySystem->handleGameplayEvent( "Event.Combo"_tag, payload );
    SW_EXPECT_EQUAL( 2, pComboRaw->_comboCount );
}

/**
 * @brief [AbilitySystemTest] 어빌리티 · 어트리뷰트 셋 클래스 이름은 만들 때 풀린다 — 데이터를 클래스 등록보다 먼저 읽어도 조용하다
 * @details 클래스는 게임 모듈이 등록한다. 읽을 때 "아직 등록되지 않았다" 고 경고하면 데이터만 읽는 도구 · 시험과 모듈이 나중에 오르는 순서에서
 *          맞는 데이터가 경고를 낸다(언리얼 GAS 도 클래스 참조는 쓸 때 푼다). 없는 클래스는 만들 때 nullptr 과 경고다.
 */
SW_TEST_CASE( AbilitySystemTest, ClassNamesResolveWhenCreatedNotWhenLoaded )
{
    constexpr const utf8* kLateClassXml = R"(<AbilityCatalog>
  <Ability id="GA_Late" class="LateClass"/>
  <AbilitySet id="LateSet"><AttributeSet class="LateAttributes"/></AbilitySet>
</AbilityCatalog>)";

    AbilityCatalog catalog;
    {
        test::ScopedLogCollector logs;
        SW_ASSERT_TRUE( catalog.loadFromXmlText( kLateClassXml, "AbilitySystemTest.Late" ) );
        SW_EXPECT_TRUE_MSG( logs.joined().empty(), ( "등록 전 클래스 이름으로 읽기가 경고했다:" + logs.joined() ).c_str() );
    }
    {
        SW_TEST_DEFENSIVE_SCOPE( "an ability and an attribute set whose classes are not registered" );
        SW_EXPECT_NULL( catalog.createAbility( "GA_Late" ).get() );
        SW_EXPECT_NULL( catalog.createAttributeSet( "LateAttributes" ).get() );
    }
    catalog.registerAbilityClass<ApplyEffectsAbility>( "LateClass" );
    SW_EXPECT_NOT_NULL( catalog.createAbility( "GA_Late" ).get() );
}

/**
 * @brief [AbilitySystemTest] 카탈로그 XML 의 세트가 어트리뷰트 · 범위 · 어빌리티(입력 번호) · 시작 이펙트 · 태그를 한 번에 주고, 데이터만의 어빌리티가 돈다
 * @details 세트의 체력 80 은 최대 체력 120 보다 먼저 적혀 있어도 잘리지 않는다(붙이기 전에 정한다). 범위 [0, 100] 의 150 은 처음 바뀔 때 잘린다.
 *          "ApplyEffects" 회복(레벨 2 → 30)은 마나 20 을 쓰고 3 초 쿨다운을 건다. 주기 리젠은 1 초마다 마나 2 다.
 */
SW_TEST_CASE( AbilitySystemTest, CatalogXmlGrantsAnAbilitySetAndRunsADataOnlyAbility )
{
    AbilityCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( kAbilityTestCatalogXml, "AbilitySystemTest" ) );
    SW_EXPECT_EQUAL( 5u, catalog.getEffectCount() );
    SW_EXPECT_EQUAL( 1u, catalog.getAbilityCount() );
    SW_EXPECT_EQUAL( 1u, catalog.getAbilitySetCount() );
    const GameplayAbilityDef* pHealDef = catalog.findAbility( "GA_Heal" );
    SW_ASSERT_NOT_NULL( pHealDef );
    SW_EXPECT_TRUE( pHealDef->_config._pCostEffect != nullptr ); // 이펙트가 뒤에 적혀 있어도 풀렸다

    GameObjectManager       manager;
    AbilitySystemComponent* pAbilitySystem = spawnAbilityOwner( manager, "Hero" );
    SW_ASSERT_NOT_NULL( pAbilitySystem );
    pAbilitySystem->setCatalog( &catalog );
    SW_ASSERT_TRUE( pAbilitySystem->grantAbilitySet( "Hero" ) );

    SW_EXPECT_NEAR_EQUAL( 80.0f, pAbilitySystem->getAttributeValue( CombatAttributes::health() ), 0.001f );
    SW_EXPECT_NEAR_EQUAL( 120.0f, pAbilitySystem->getAttributeValue( CombatAttributes::maxHealth() ), 0.001f );
    SW_EXPECT_NEAR_EQUAL( 10.0f, pAbilitySystem->getAttributeValue( CombatAttributes::attackPower() ), 0.001f ); // 묶음 기본값
    SW_EXPECT_TRUE( pAbilitySystem->hasMatchingTag( "Team.Hero"_tag ) );
    SW_EXPECT_EQUAL( 1u, pAbilitySystem->getAbilityCount() );
    SW_EXPECT_EQUAL( 1u, pAbilitySystem->getActiveEffectCount() );
    SW_EXPECT_TRUE( pAbilitySystem->setAttributeBaseValue( "Stamina", 150.0f ) );
    SW_EXPECT_NEAR_EQUAL( 100.0f, pAbilitySystem->getAttributeValue( "Stamina" ), 0.001f );

    int32 healCueCount = 0;
    (void)pAbilitySystem->registerGameplayCue( SW_DELEGATE_LAMBDA( AbilitySystemComponent::GameplayCueDelegate, [&healCueCount]( const GameplayCueEvent& cue )
    {
        if ( cue._cueTag == "GameplayCue.Heal"_tag && cue._phase == GameplayCuePhase::Executed )
            ++healCueCount;
    } ) );

    pAbilitySystem->abilityInputPressed( 3 );
    SW_EXPECT_NEAR_EQUAL( 110.0f, pAbilitySystem->getAttributeValue( CombatAttributes::health() ), 0.001f );
    SW_EXPECT_NEAR_EQUAL( 30.0f, pAbilitySystem->getAttributeValue( CombatAttributes::mana() ), 0.001f );
    SW_EXPECT_TRUE( pAbilitySystem->hasMatchingTag( "Cooldown.Skill"_tag ) );
    SW_EXPECT_EQUAL( 1, healCueCount );

    pAbilitySystem->advanceTime( 1.0f );
    SW_EXPECT_NEAR_EQUAL( 32.0f, pAbilitySystem->getAttributeValue( CombatAttributes::mana() ), 0.001f );
    pAbilitySystem->abilityInputReleased( 3 );
    pAbilitySystem->abilityInputPressed( 3 );
    SW_EXPECT_NEAR_EQUAL( 110.0f, pAbilitySystem->getAttributeValue( CombatAttributes::health() ), 0.001f ); // 쿨다운 중
    SW_EXPECT_EQUAL( 1, healCueCount );

    // 정화는 에셋 태그로도 고른다.
    TagContainer regenTags;
    regenTags.addTag( "Effect.Regen"_tag );
    SW_EXPECT_EQUAL( 1, pAbilitySystem->removeActiveEffectsWithTags( regenTags ) );
}

/**
 * @brief [AbilitySystemTest] 데이터만의 가시(피격 트리거 + `targetEffect`)는 때린 쪽에 걸린다 — 맞은 쪽 자신이 아니라
 * @details 피격 이벤트(`Event.Hit`)의 `_target` 은 맞은 쪽 자신이라, "상대" 는 일으킨 쪽(`_instigator`)이다. 가시 피해는 메타 어트리뷰트에 바로 더해
 *          방어를 지나지 않는다. 때린 쪽에는 가시가 없으니 되갚기가 이어지지 않는다.
 */
SW_TEST_CASE( AbilitySystemTest, TriggeredApplyEffectsHitsTheOtherPartyOfTheEvent )
{
    constexpr const utf8* kThornsXml = R"(
<AbilityCatalog>
  <GameplayEffect id="GE_Thorns" duration="Instant">
    <Modifier attribute="IncomingDamage" op="Add" magnitude="2"/>
  </GameplayEffect>
  <Ability id="GA_Thorns" class="ApplyEffects">
    <TriggerTag tag="Event.Hit"/>
    <Param name="targetEffect" text="GE_Thorns"/>
  </Ability>
</AbilityCatalog>
)";
    AbilityCatalog        catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( kThornsXml, "AbilitySystemTest.Thorns" ) );

    GameObjectManager       manager;
    AbilitySystemComponent* pAttacker = spawnCombatant( manager, "Attacker", 100.0f, 50.0f, 10.0f );
    AbilitySystemComponent* pDefender = spawnCombatant( manager, "Defender", 100.0f, 0.0f, 0.0f );
    SW_ASSERT_TRUE( pAttacker != nullptr && pDefender != nullptr );
    pAttacker->setCatalog( &catalog );
    pDefender->setCatalog( &catalog );
    SW_ASSERT_TRUE( pDefender->giveAbilityById( "GA_Thorns" ).isValid() );

    shared_ptr<GameplayEffectDef> pDamage  = make_shared<GameplayEffectDef>();
    pDamage->_id                           = hashed_string( "Hit" );
    shared_ptr<DamageExecution> pExecution = make_shared<DamageExecution>();
    pExecution->setParameters( hashed_string{}, 1.0f, 0.0f );
    pDamage->_listExecution.push_back( pExecution );

    (void)pAttacker->applyGameplayEffectSpecToTarget( pAttacker->makeOutgoingSpec( pDamage ), pDefender );
    SW_EXPECT_NEAR_EQUAL( 90.0f, pDefender->getAttributeValue( CombatAttributes::health() ), 0.001f );
    SW_EXPECT_NEAR_EQUAL( 98.0f, pAttacker->getAttributeValue( CombatAttributes::health() ), 0.001f ); // 방어 50 을 지나지 않았다
}

/**
 * @brief [AbilitySystemTest] 다른 오브젝트의 틱 안에서 건 이펙트는 그 틱 직후에 적용된다 — 그 자리에서는 대상을 건드리지 않는다
 * @details 컴포넌트 틱은 오브젝트마다 병렬 워커에서 돈다. 대상이 다른 워커에서 틱하고 있을 수 있으므로, 틱 중에 다른 오브젝트로 가는 적용은 미루고
 *          무효 핸들을 돌려준다. 미룬 적용도 쏜 순간의 공격력 스냅샷을 쓴다.
 */
SW_TEST_CASE( AbilitySystemTest, EffectAppliedToAnotherObjectDuringTickIsDeferredToAfterTheTick )
{
    GameObjectManager       manager;
    AbilitySystemComponent* pSource = spawnCombatant( manager, "Source", 100.0f, 0.0f, 15.0f );
    AbilitySystemComponent* pTarget = spawnCombatant( manager, "Target", 100.0f, 0.0f, 0.0f );
    SW_ASSERT_TRUE( pSource != nullptr && pTarget != nullptr );
    AbilityTestStrikeInTickComponent* pStriker = pSource->getOwner()->addComponent<AbilityTestStrikeInTickComponent>();
    SW_ASSERT_NOT_NULL( pStriker );

    shared_ptr<GameplayEffectDef> pDamage  = make_shared<GameplayEffectDef>();
    pDamage->_id                           = hashed_string( "TickDamage" );
    shared_ptr<DamageExecution> pExecution = make_shared<DamageExecution>();
    pExecution->setParameters( hashed_string{}, 1.0f, 0.0f );
    pDamage->_listExecution.push_back( pExecution );
    pStriker->_pSource = pSource;
    pStriker->_pTarget = pTarget;
    pStriker->_pEffect = pDamage;

    manager.beginPlay();
    manager.tick( 0.016f );
    SW_EXPECT_TRUE( pStriker->_bHandleWasValid == SW_FALSE );
    SW_EXPECT_NEAR_EQUAL( 100.0f, pStriker->_healthSeenInTick, 0.001f );
    SW_EXPECT_NEAR_EQUAL( 85.0f, pTarget->getAttributeValue( CombatAttributes::health() ), 0.001f );
    manager.endPlay();
}

/**
 * @brief [AbilitySystemTest] 모듈을 내리기 전 훑기는 그 범위의 vtable 을 가진 어빌리티만 거둔다(핫 리로드)
 * @details 범위는 이미지 전체가 아니라 한 클래스의 vtable 하나다 — 배포 구성은 엔진까지 한 실행 파일이라 넓은 범위는 다른 보유자까지 건드린다.
 */
SW_TEST_CASE( AbilitySystemTest, ModuleCodeReleaseRetiresOnlyAbilitiesFromThatModule )
{
    GameObjectManager       manager;
    AbilitySystemComponent* pAbilitySystem = spawnAbilityOwner( manager, "Owner" );
    SW_ASSERT_NOT_NULL( pAbilitySystem );

    unique_ptr<AbilityTestLatentAbility> pLatent    = make_unique<AbilityTestLatentAbility>();
    const void*                          pVtable    = IModuleUnloadListener::findVtableAddress( pLatent.get() );
    const AbilitySpecHandle              latentSpec = pAbilitySystem->giveAbility( std::move( pLatent ) );
    const AbilitySpecHandle              keptSpec   = pAbilitySystem->giveAbility( make_unique<AbilityTestInstantAbility>() );
    SW_ASSERT_TRUE( pAbilitySystem->tryActivateAbility( latentSpec ) == AbilityActivationResult::Activated );

    vector<IModuleUnloadListener::ReleaseResult> listResult;
    IModuleUnloadListener::releaseAllWithin( pVtable, static_cast<const uint8*>( pVtable ) + 1, listResult );

    SW_EXPECT_NULL( pAbilitySystem->findAbility( latentSpec ) );
    SW_EXPECT_NOT_NULL( pAbilitySystem->findAbility( keptSpec ) );
    SW_EXPECT_EQUAL( 1u, pAbilitySystem->getAbilityCount() );
}

/**
 * @brief [AbilitySystemTest] 데이터가 쓰는 열거자 이름은 리플렉션 이름표로 모두 왕복한다 — 손으로 쓴 표가 따로 없다
 * @details 카탈로그 XML 의 `op` · `source` · `duration` · `stacking` · `stackExpiration` 은 `ENUM()` 이름표(`TypeRegistry::enumFromString`)로 읽는다.
 *          열거자를 더하면 이름표도 함께 생긴다. 대소문자는 가리지 않고, 모르는 이름은 false 다.
 */
SW_TEST_CASE( AbilitySystemTest, EnumNamesRoundTrip )
{
    const TypeRegistry&  registry   = engine::getTypeRegistry();
    const AttributeModOp arrModOp[] = { AttributeModOp::Add, AttributeModOp::Multiply, AttributeModOp::Divide, AttributeModOp::Override };
    for ( const AttributeModOp value : arrModOp )
    {
        AttributeModOp parsed{ AttributeModOp::Add };
        const utf8*    pName = registry.enumToString( value );
        SW_ASSERT_NOT_NULL( pName );
        SW_EXPECT_TRUE( registry.enumFromString( pName, parsed ) && parsed == value );
    }
    const EffectDurationPolicy arrDuration[] = { EffectDurationPolicy::Instant, EffectDurationPolicy::HasDuration, EffectDurationPolicy::Infinite };
    for ( const EffectDurationPolicy value : arrDuration )
    {
        EffectDurationPolicy parsed{ EffectDurationPolicy::Instant };
        const utf8*          pName = registry.enumToString( value );
        SW_ASSERT_NOT_NULL( pName );
        SW_EXPECT_TRUE( registry.enumFromString( pName, parsed ) && parsed == value );
    }
    const EffectMagnitudeSource arrSource[] = { EffectMagnitudeSource::ScalableFloat, EffectMagnitudeSource::AttributeBased, EffectMagnitudeSource::SetByCaller };
    for ( const EffectMagnitudeSource value : arrSource )
    {
        EffectMagnitudeSource parsed{ EffectMagnitudeSource::ScalableFloat };
        const utf8*           pName = registry.enumToString( value );
        SW_ASSERT_NOT_NULL( pName );
        SW_EXPECT_TRUE( registry.enumFromString( pName, parsed ) && parsed == value );
    }
    EffectStackingPolicy stacking{ EffectStackingPolicy::None };
    SW_EXPECT_TRUE( registry.enumFromString( "aggregatebytarget", stacking ) && stacking == EffectStackingPolicy::AggregateByTarget );
    EffectStackExpirationPolicy expiration{ EffectStackExpirationPolicy::ClearEntireStack };
    SW_EXPECT_TRUE( registry.enumFromString( "RemoveSingleStackAndRefreshDuration", expiration ) &&
                    expiration == EffectStackExpirationPolicy::RemoveSingleStackAndRefreshDuration );
    SW_EXPECT_STREQ( "OnCooldown", registry.enumToString( AbilityActivationResult::OnCooldown ) );

    // 모르는 이름은 false 다.
    AttributeModOp unknown{ AttributeModOp::Divide };
    SW_EXPECT_FALSE( registry.enumFromString( "Subtract", unknown ) );
}
