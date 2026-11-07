#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Runtime/Gameplay/Ability/Ability.h"
#include "GameFramework/Runtime/Gameplay/Ability/AbilityComponent.h"
#include "GameFramework/Runtime/Gameplay/Ability/AbilityEffect.h"
#include "GameFramework/Runtime/Gameplay/Ability/AttributeSet.h"
#include "GameFramework/Runtime/Gameplay/Ability/TagCountTable.h"

using namespace sw;

namespace
{
    /** @brief 불린 횟수를 세는 어빌리티입니다. */
    class ProbeAbility final : public Ability
    {
    public:
        uint32 _activatedCount{ 0 };
        uint32 _endedCount{ 0 };
        uint32 _cancelledCount{ 0 };
        uint32 _pressedCount{ 0 };
        uint32 _releasedCount{ 0 };
        uint32 _tickCount{ 0 };
        uint32 _grantedCount{ 0 };
        uint32 _removedCount{ 0 };
        uint8  _bEndAtOnce        : 1;
        uint8  _bCommitOnActivate : 1;
        uint8  _bReject           : 1;

        ProbeAbility()
            : _bEndAtOnce{ SW_FALSE }
            , _bCommitOnActivate{ SW_FALSE }
            , _bReject{ SW_FALSE }
        {
        }

        bool canActivateAbility( const AbilityEventData* pTriggerEvent ) const override
        {
            (void)pTriggerEvent;
            return _bReject == SW_FALSE;
        }
        void activateAbility( const AbilityEventData* pTriggerEvent ) override
        {
            (void)pTriggerEvent;
            ++_activatedCount;
            if ( _bCommitOnActivate == SW_TRUE )
                (void)commitAbility();
            if ( _bEndAtOnce == SW_TRUE )
                endAbility();
        }
        void onEndAbility( bool bWasCancelled ) override
        {
            ++_endedCount;
            _cancelledCount += bWasCancelled ? 1u : 0u;
        }
        void onTickAbility( float32 deltaTime ) override
        {
            (void)deltaTime;
            ++_tickCount;
        }
        void onGranted() override { ++_grantedCount; }
        void onRemoved() override { ++_removedCount; }
        void onInputPressed() override { ++_pressedCount; }
        void onInputReleased() override { ++_releasedCount; }
    };

    /** @brief 어트리뷰트 세트를 붙인 오브젝트 하나입니다. */
    struct AbilityFixture
    {
        GameObjectManager _manager;
        AbilityComponent* _pAbilities{ nullptr };
        AttributeSet*     _pSet{ nullptr };

        static inline const hashed_string kHealth{ "Health" };
        static inline const hashed_string kMaxHealth{ "MaxHealth" };
        static inline const hashed_string kMana{ "Mana" };
        static inline const hashed_string kPower{ "Power" };

        AbilityFixture()
        {
            _pAbilities                   = _manager.createGameObject( hashed_string( "Hero" ) )->addComponent<AbilityComponent>();
            unique_ptr<AttributeSet> pSet = make_unique<AttributeSet>();
            pSet->defineAttribute( kHealth, 100.0f );
            pSet->defineAttribute( kMaxHealth, 100.0f );
            pSet->defineAttribute( kMana, 50.0f );
            pSet->defineAttribute( kPower, 10.0f );
            _pSet = _pAbilities->addAttributeSet( std::move( pSet ) );
        }

        float32 current( const hashed_string& name ) const { return _pAbilities->getAttributeValue( name ).value_or( -9999.0f ); }
        float32 base( const hashed_string& name ) const { return _pAbilities->getAttributeBaseValue( name ).value_or( -9999.0f ); }

        ActiveEffectHandle apply( const shared_ptr<AbilityEffectDef>& pDef, int32 level = 1 ) { return _pAbilities->applyGameplayEffectSpecToSelf( _pAbilities->makeOutgoingSpec( pDef, level ) ); }

        ProbeAbility& give( const AbilityConfig& config = {}, int32 inputID = AbilityComponent::kNoInputID )
        {
            unique_ptr<ProbeAbility> pAbility = make_unique<ProbeAbility>();
            ProbeAbility&            ability  = *pAbility;
            ability.setConfig( config );
            (void)_pAbilities->giveAbility( std::move( pAbility ), 1, inputID );
            return ability;
        }

        static shared_ptr<AbilityEffectDef> makeEffect( EffectDurationPolicy policy, float32 duration = 0.0f )
        {
            shared_ptr<AbilityEffectDef> pDef = make_shared<AbilityEffectDef>();
            pDef->_durationPolicy             = policy;
            pDef->_duration._baseValue        = duration;
            return pDef;
        }

        static void addModifier( AbilityEffectDef& def, const hashed_string& attribute, AttributeModOp op, float32 magnitude, float32 perLevel = 0.0f )
        {
            AbilityEffectModifier modifier;
            modifier._attribute                    = attribute;
            modifier._op                           = op;
            modifier._scalableMagnitude._baseValue = magnitude;
            modifier._scalableMagnitude._perLevel  = perLevel;
            def._listModifier.push_back( modifier );
        }
    };

    struct AbilityTestUtil
    {
        static TagContainer makeTags( std::initializer_list<TagID> listTag )
        {
            TagContainer tags;
            for ( const TagID& tag : listTag )
            {
                tags.addTag( tag );
            }
            return tags;
        }
    };
    using Rig  = AbilityFixture;
    using Util = AbilityTestUtil;
} // namespace

// ---------------------------------------------------------------------- 어트리뷰트

SW_TEST_CASE( AbilityAttributeTest, UnknownAttributesHaveNoValue )
{
    Rig rig;
    SW_EXPECT_TRUE( rig._pAbilities->hasAttribute( Rig::kHealth ) );
    SW_EXPECT_FALSE( rig._pAbilities->hasAttribute( hashed_string( "Nope" ) ) );
    SW_EXPECT_FALSE( rig._pAbilities->getAttributeValue( hashed_string( "Nope" ) ).has_value() );
    SW_EXPECT_FALSE( rig._pAbilities->setAttributeBaseValue( hashed_string( "Nope" ), 1.0f ) );
}

SW_TEST_CASE( AbilityAttributeTest, BaseChangesReachCurrentAndListeners )
{
    Rig          rig;
    float32      seenOld      = 0.0f;
    float32      seenNew      = 0.0f;
    uint32       count        = 0;
    Subscription subscription = rig._pAbilities->registerAttributeChanged( Rig::kMana, SW_DELEGATE_LAMBDA( AbilityComponent::AttributeChangedDelegate, [&]( const AttributeChangeData& data )
    {
        seenOld = data._oldValue;
        seenNew = data._newValue;
        ++count;
    } ) );
    SW_EXPECT_TRUE( rig._pAbilities->setAttributeBaseValue( Rig::kMana, 30.0f ) );
    SW_EXPECT_EQUAL( 30.0f, rig.current( Rig::kMana ) );
    SW_EXPECT_EQUAL( 1u, count );
    SW_EXPECT_EQUAL( 50.0f, seenOld );
    SW_EXPECT_EQUAL( 30.0f, seenNew );
    SW_EXPECT_TRUE( rig._pAbilities->setAttributeBaseValue( Rig::kMana, 30.0f ) );
    SW_EXPECT_EQUAL( 1u, count ); // 값이 그대로면 알리지 않는다
}

SW_TEST_CASE( AbilityAttributeTest, RangeClampsBothValues )
{
    Rig rig;
    rig._pSet->setAttributeRange( Rig::kMana, 0.0f, 60.0f );
    SW_EXPECT_TRUE( rig._pAbilities->setAttributeBaseValue( Rig::kMana, 500.0f ) );
    SW_EXPECT_EQUAL( 60.0f, rig.base( Rig::kMana ) );
    SW_EXPECT_TRUE( rig._pAbilities->setAttributeBaseValue( Rig::kMana, -5.0f ) );
    SW_EXPECT_EQUAL( 0.0f, rig.base( Rig::kMana ) );
}

SW_TEST_CASE( AbilityAttributeTest, HealthReadingNeedsBothAttributes )
{
    Rig           rig;
    HealthReading reading = rig._pAbilities->getHealthReading();
    SW_EXPECT_TRUE( reading._bHasHealth == SW_TRUE );
    SW_EXPECT_EQUAL( 100.0f, reading._health );
    SW_EXPECT_TRUE( reading._bDead == SW_FALSE );
    SW_EXPECT_TRUE( rig._pAbilities->setAttributeBaseValue( Rig::kHealth, 0.0f ) );
    SW_EXPECT_TRUE( rig._pAbilities->getHealthReading()._bDead == SW_TRUE );

    rig._pAbilities->setHealthAttributes( Rig::kHealth, hashed_string( "NoSuchMax" ) );
    SW_EXPECT_TRUE( rig._pAbilities->getHealthReading()._bHasHealth == SW_FALSE );
}

// ---------------------------------------------------------------------- 즉시 이펙트

SW_TEST_CASE( AbilityEffectTest, InstantEffectsWriteTheBase )
{
    Rig  rig;
    auto pAdd = Rig::makeEffect( EffectDurationPolicy::Instant );
    Rig::addModifier( *pAdd, Rig::kHealth, AttributeModOp::Add, -10.0f, -5.0f );
    (void)rig.apply( pAdd, 3 ); // 레벨 3 = -10 + -5 × 2
    SW_EXPECT_EQUAL( 80.0f, rig.base( Rig::kHealth ) );
    SW_EXPECT_EQUAL( 80.0f, rig.current( Rig::kHealth ) );
    SW_EXPECT_EQUAL( 0u, rig._pAbilities->getActiveEffectCount() ); // 즉시 이펙트는 남지 않는다

    auto pMultiply = Rig::makeEffect( EffectDurationPolicy::Instant );
    Rig::addModifier( *pMultiply, Rig::kMana, AttributeModOp::Multiply, 2.0f );
    (void)rig.apply( pMultiply );
    SW_EXPECT_EQUAL( 100.0f, rig.base( Rig::kMana ) );

    auto pDivide = Rig::makeEffect( EffectDurationPolicy::Instant );
    Rig::addModifier( *pDivide, Rig::kMana, AttributeModOp::Divide, 4.0f );
    (void)rig.apply( pDivide );
    SW_EXPECT_EQUAL( 25.0f, rig.base( Rig::kMana ) );

    auto pOverride = Rig::makeEffect( EffectDurationPolicy::Instant );
    Rig::addModifier( *pOverride, Rig::kPower, AttributeModOp::Override, 7.0f );
    (void)rig.apply( pOverride );
    SW_EXPECT_EQUAL( 7.0f, rig.base( Rig::kPower ) );
}

SW_TEST_CASE( AbilityEffectTest, MissingAttributeIsWarnedAndSkipped )
{
    SW_EXPECT_LOG( "AbilityComponent.EffectModifiesWhichObject" );
    Rig  rig;
    auto pDef = Rig::makeEffect( EffectDurationPolicy::Instant );
    Rig::addModifier( *pDef, hashed_string( "Nope" ), AttributeModOp::Add, 1.0f );
    (void)rig.apply( pDef );
}

SW_TEST_CASE( AbilityEffectTest, MagnitudeComesFromAttributesAndCallers )
{
    Rig                   rig;
    auto                  pDef = Rig::makeEffect( EffectDurationPolicy::Instant );
    AbilityEffectModifier fromSource;
    fromSource._attribute            = Rig::kHealth;
    fromSource._magnitudeSource      = EffectMagnitudeSource::AttributeBased;
    fromSource._backingAttribute     = Rig::kPower;
    fromSource._preMultiplyAdditive  = 2.0f;
    fromSource._coefficient          = -1.0f;
    fromSource._postMultiplyAdditive = -3.0f; // ( 10 + 2 ) × -1 - 3 = -15
    pDef->_listModifier.push_back( fromSource );
    AbilityEffectModifier byCaller;
    byCaller._attribute       = Rig::kMana;
    byCaller._magnitudeSource = EffectMagnitudeSource::SetByCaller;
    byCaller._setByCallerName = hashed_string( "Spent" );
    pDef->_listModifier.push_back( byCaller );

    AbilityEffectSpec spec = rig._pAbilities->makeOutgoingSpec( pDef );
    spec.setSetByCallerMagnitude( hashed_string( "Spent" ), -20.0f );
    SW_EXPECT_TRUE( rig._pAbilities->setAttributeBaseValue( Rig::kPower, 999.0f ) ); // 쏜 쪽 값은 스펙을 만든 순간의 것이다
    (void)rig._pAbilities->applyGameplayEffectSpecToSelf( spec );
    SW_EXPECT_EQUAL( 85.0f, rig.base( Rig::kHealth ) );
    SW_EXPECT_EQUAL( 30.0f, rig.base( Rig::kMana ) );
}

SW_TEST_CASE( AbilityEffectTest, UnsetCallerMagnitudeIsWarnedAsZero )
{
    SW_EXPECT_LOG( "AbilityComponent.EffectSetByCallerNotSet" );
    Rig                   rig;
    auto                  pDef = Rig::makeEffect( EffectDurationPolicy::Instant );
    AbilityEffectModifier byCaller;
    byCaller._attribute       = Rig::kMana;
    byCaller._magnitudeSource = EffectMagnitudeSource::SetByCaller;
    byCaller._setByCallerName = hashed_string( "Unset" );
    pDef->_listModifier.push_back( byCaller );
    (void)rig.apply( pDef );
    SW_EXPECT_EQUAL( 50.0f, rig.base( Rig::kMana ) );
}

// ---------------------------------------------------------------------- 지속 이펙트

SW_TEST_CASE( AbilityEffectTest, DurationEffectChangesCurrentUntilItExpires )
{
    Rig  rig;
    auto pDef = Rig::makeEffect( EffectDurationPolicy::HasDuration, 1.0f );
    Rig::addModifier( *pDef, Rig::kPower, AttributeModOp::Add, 5.0f );
    const ActiveEffectHandle handle = rig.apply( pDef );
    SW_EXPECT_EQUAL( 15.0f, rig.current( Rig::kPower ) );
    SW_EXPECT_EQUAL( 10.0f, rig.base( Rig::kPower ) );
    SW_EXPECT_NEAR( 1.0f, rig._pAbilities->getActiveEffectRemainingTime( handle ), 1e-4f );

    rig._pAbilities->advanceTime( 0.6f );
    SW_EXPECT_NEAR( 0.4f, rig._pAbilities->getActiveEffectRemainingTime( handle ), 1e-4f );
    SW_EXPECT_TRUE( rig._pAbilities->isActiveEffect( handle ) );
    rig._pAbilities->advanceTime( 0.6f );
    SW_EXPECT_FALSE( rig._pAbilities->isActiveEffect( handle ) );
    SW_EXPECT_EQUAL( 10.0f, rig.current( Rig::kPower ) );
    SW_EXPECT_EQUAL( 0u, rig._pAbilities->getActiveEffectCount() );
}

SW_TEST_CASE( AbilityEffectTest, InfiniteEffectLastsUntilRemoved )
{
    Rig  rig;
    auto pDef = Rig::makeEffect( EffectDurationPolicy::Infinite );
    Rig::addModifier( *pDef, Rig::kPower, AttributeModOp::Add, 5.0f );
    const ActiveEffectHandle handle = rig.apply( pDef );
    rig._pAbilities->advanceTime( 1000.0f );
    SW_EXPECT_EQUAL( 15.0f, rig.current( Rig::kPower ) );
    SW_EXPECT_TRUE( rig._pAbilities->removeActiveEffect( handle ) );
    SW_EXPECT_EQUAL( 10.0f, rig.current( Rig::kPower ) );
    SW_EXPECT_FALSE( rig._pAbilities->removeActiveEffect( handle ) );
}

// 더하기 → 곱하기 → 나누기 순이고, 덮어쓰기가 있으면 그 값이다.
SW_TEST_CASE( AbilityEffectTest, ModifiersAggregateInAFixedOrder )
{
    Rig  rig;
    auto pMultiply = Rig::makeEffect( EffectDurationPolicy::Infinite );
    Rig::addModifier( *pMultiply, Rig::kPower, AttributeModOp::Multiply, 2.0f );
    auto pAdd = Rig::makeEffect( EffectDurationPolicy::Infinite );
    Rig::addModifier( *pAdd, Rig::kPower, AttributeModOp::Add, 5.0f );
    (void)rig.apply( pMultiply );
    (void)rig.apply( pAdd );
    SW_EXPECT_EQUAL( 30.0f, rig.current( Rig::kPower ) ); // ( 10 + 5 ) × 2

    auto pDivide = Rig::makeEffect( EffectDurationPolicy::Infinite );
    Rig::addModifier( *pDivide, Rig::kPower, AttributeModOp::Divide, 3.0f );
    (void)rig.apply( pDivide );
    SW_EXPECT_EQUAL( 10.0f, rig.current( Rig::kPower ) );

    auto pOverride = Rig::makeEffect( EffectDurationPolicy::Infinite );
    Rig::addModifier( *pOverride, Rig::kPower, AttributeModOp::Override, 1.0f );
    const ActiveEffectHandle handle = rig.apply( pOverride );
    SW_EXPECT_EQUAL( 1.0f, rig.current( Rig::kPower ) );
    SW_EXPECT_TRUE( rig._pAbilities->removeActiveEffect( handle ) );
    SW_EXPECT_EQUAL( 10.0f, rig.current( Rig::kPower ) );
}

// ---------------------------------------------------------------------- 스택

SW_TEST_CASE( AbilityEffectTest, StacksAggregateUpToTheLimit )
{
    Rig  rig;
    auto pDef             = Rig::makeEffect( EffectDurationPolicy::HasDuration, 2.0f );
    pDef->_id             = hashed_string( "Buff" );
    pDef->_stackingPolicy = EffectStackingPolicy::AggregateByTarget;
    pDef->_stackLimit     = 2;
    Rig::addModifier( *pDef, Rig::kPower, AttributeModOp::Add, 5.0f );
    const ActiveEffectHandle first = rig.apply( pDef );
    rig._pAbilities->advanceTime( 1.0f );
    const ActiveEffectHandle second = rig.apply( pDef );
    SW_EXPECT_TRUE( first == second );
    SW_EXPECT_EQUAL( 2, rig._pAbilities->getActiveEffectStackCount( first ) );
    SW_EXPECT_EQUAL( 20.0f, rig.current( Rig::kPower ) );
    SW_EXPECT_NEAR( 2.0f, rig._pAbilities->getActiveEffectRemainingTime( first ), 1e-4f ); // 시간을 새로 잰다

    (void)rig.apply( pDef );
    SW_EXPECT_EQUAL( 2, rig._pAbilities->getActiveEffectStackCount( first ) ); // 상한
    SW_EXPECT_EQUAL( 1u, rig._pAbilities->getActiveEffectCount() );

    SW_EXPECT_TRUE( rig._pAbilities->removeActiveEffect( first, 1 ) );
    SW_EXPECT_EQUAL( 15.0f, rig.current( Rig::kPower ) );
}

SW_TEST_CASE( AbilityEffectTest, SingleStackExpiryKeepsTheRest )
{
    Rig  rig;
    auto pDef                    = Rig::makeEffect( EffectDurationPolicy::HasDuration, 1.0f );
    pDef->_stackingPolicy        = EffectStackingPolicy::AggregateByTarget;
    pDef->_stackLimit            = 3;
    pDef->_stackExpirationPolicy = EffectStackExpirationPolicy::RemoveSingleStackAndRefreshDuration;
    Rig::addModifier( *pDef, Rig::kPower, AttributeModOp::Add, 5.0f );
    const ActiveEffectHandle handle = rig.apply( pDef );
    (void)rig.apply( pDef );
    rig._pAbilities->advanceTime( 1.1f );
    SW_EXPECT_EQUAL( 1, rig._pAbilities->getActiveEffectStackCount( handle ) );
    SW_EXPECT_EQUAL( 15.0f, rig.current( Rig::kPower ) );
    rig._pAbilities->advanceTime( 1.1f );
    SW_EXPECT_FALSE( rig._pAbilities->isActiveEffect( handle ) );
}

// ---------------------------------------------------------------------- 주기

SW_TEST_CASE( AbilityEffectTest, PeriodicEffectWritesTheBaseEachPeriod )
{
    Rig  rig;
    auto pDef     = Rig::makeEffect( EffectDurationPolicy::HasDuration, 1.0f );
    pDef->_period = 0.25f;
    Rig::addModifier( *pDef, Rig::kHealth, AttributeModOp::Add, -1.0f );
    (void)rig.apply( pDef );
    SW_EXPECT_EQUAL( 99.0f, rig.base( Rig::kHealth ) ); // 걸리는 순간 한 번
    rig._pAbilities->advanceTime( 0.26f );
    SW_EXPECT_EQUAL( 98.0f, rig.base( Rig::kHealth ) );
    // 한 프레임이 주기를 여럿 넘어도 순서대로 돌고, 만료 뒤의 주기는 돌지 않는다.
    rig._pAbilities->advanceTime( 100.0f );
    SW_EXPECT_TRUE( rig.base( Rig::kHealth ) >= 95.0f );
    SW_EXPECT_EQUAL( 0u, rig._pAbilities->getActiveEffectCount() );
}

SW_TEST_CASE( AbilityEffectTest, PeriodicEffectCanWaitForTheFirstPeriod )
{
    Rig  rig;
    auto pDef                            = Rig::makeEffect( EffectDurationPolicy::Infinite );
    pDef->_period                        = 1.0f;
    pDef->_bExecutePeriodicOnApplication = SW_FALSE;
    Rig::addModifier( *pDef, Rig::kHealth, AttributeModOp::Add, -1.0f );
    (void)rig.apply( pDef );
    SW_EXPECT_EQUAL( 100.0f, rig.base( Rig::kHealth ) );
    rig._pAbilities->advanceTime( 2.5f );
    SW_EXPECT_EQUAL( 98.0f, rig.base( Rig::kHealth ) );
}

// ---------------------------------------------------------------------- 태그 조건

SW_TEST_CASE( AbilityEffectTest, ApplicationTagsDecideWhetherItApplies )
{
    Rig         rig;
    const TagID wet                  = "Test.State.Wet"_tag;
    auto        pNeeds               = Rig::makeEffect( EffectDurationPolicy::Infinite );
    pNeeds->_applicationRequiredTags = Util::makeTags( { wet } );
    SW_EXPECT_FALSE( rig.apply( pNeeds ).isValid() );
    auto pBlocked                     = Rig::makeEffect( EffectDurationPolicy::Infinite );
    pBlocked->_applicationBlockedTags = Util::makeTags( { wet } );
    SW_EXPECT_TRUE( rig.apply( pBlocked ).isValid() );

    rig._pAbilities->getOwnedTagCounts().updateCount( wet, 1 );
    SW_EXPECT_TRUE( rig.apply( pNeeds ).isValid() );
    SW_EXPECT_FALSE( rig.apply( pBlocked ).isValid() );
}

SW_TEST_CASE( AbilityEffectTest, GrantedTagsLastAsLongAsTheEffect )
{
    Rig         rig;
    const TagID slow   = "Test.State.Slow"_tag;
    auto        pDef   = Rig::makeEffect( EffectDurationPolicy::HasDuration, 1.0f );
    pDef->_grantedTags = Util::makeTags( { slow } );
    (void)rig.apply( pDef );
    SW_EXPECT_TRUE( rig._pAbilities->getOwnedTagCounts().hasMatchingTag( slow ) );
    SW_EXPECT_NEAR( 1.0f, rig._pAbilities->findLongestRemainingTimeWithGrantedTags( Util::makeTags( { slow } ) ), 1e-4f );
    rig._pAbilities->advanceTime( 2.0f );
    SW_EXPECT_FALSE( rig._pAbilities->getOwnedTagCounts().hasMatchingTag( slow ) );
    SW_EXPECT_EQUAL( 0.0f, rig._pAbilities->findLongestRemainingTimeWithGrantedTags( Util::makeTags( { slow } ) ) );
}

SW_TEST_CASE( AbilityEffectTest, EffectsCanRemoveOthersByTag )
{
    Rig         rig;
    const TagID poison  = "Test.Effect.Poison"_tag;
    auto        pPoison = Rig::makeEffect( EffectDurationPolicy::Infinite );
    pPoison->_assetTags = Util::makeTags( { poison } );
    (void)rig.apply( pPoison );
    SW_EXPECT_EQUAL( 1u, rig._pAbilities->getActiveEffectCount() );

    auto pCure                    = Rig::makeEffect( EffectDurationPolicy::Instant );
    pCure->_removeEffectsWithTags = Util::makeTags( { poison } );
    (void)rig.apply( pCure );
    SW_EXPECT_EQUAL( 0u, rig._pAbilities->getActiveEffectCount() );
    SW_EXPECT_EQUAL( 0, rig._pAbilities->removeActiveEffectsWithTags( TagContainer{} ) );
}

// 걸린 동안의 조건이 깨지면 효과만 멈췄다가, 조건이 돌아오면 다시 든다.
SW_TEST_CASE( AbilityEffectTest, OngoingTagsInhibitAndResume )
{
    Rig         rig;
    const TagID rage           = "Test.State.Rage"_tag;
    const TagID hot            = "Test.State.Hot"_tag;
    auto        pDef           = Rig::makeEffect( EffectDurationPolicy::Infinite );
    pDef->_ongoingRequiredTags = Util::makeTags( { rage } );
    pDef->_grantedTags         = Util::makeTags( { hot } );
    Rig::addModifier( *pDef, Rig::kPower, AttributeModOp::Add, 5.0f );
    const ActiveEffectHandle handle = rig.apply( pDef );
    SW_REQUIRE_TRUE( rig._pAbilities->isActiveEffect( handle ) ); // 조건이 없어도 멈춘 채로 걸린다
    SW_EXPECT_EQUAL( 10.0f, rig.current( Rig::kPower ) );
    SW_EXPECT_FALSE( rig._pAbilities->getOwnedTagCounts().hasMatchingTag( hot ) );

    rig._pAbilities->getOwnedTagCounts().updateCount( rage, 1 );
    SW_EXPECT_EQUAL( 15.0f, rig.current( Rig::kPower ) );
    SW_EXPECT_TRUE( rig._pAbilities->getOwnedTagCounts().hasMatchingTag( hot ) );

    rig._pAbilities->getOwnedTagCounts().updateCount( rage, -1 );
    SW_EXPECT_EQUAL( 10.0f, rig.current( Rig::kPower ) );
    SW_EXPECT_FALSE( rig._pAbilities->getOwnedTagCounts().hasMatchingTag( hot ) );
    SW_EXPECT_TRUE( rig._pAbilities->isActiveEffect( handle ) );
}

// 무적이 체력 내림을 막으면 맞은 것이 아니다 — 큐도 나가지 않는다. 회복은 막지 않는다.
SW_TEST_CASE( AbilityEffectTest, InvulnerabilityBlocksHealthLossAndItsCue )
{
    Rig          rig;
    const TagID  cue          = "Test.Cue.Hit"_tag;
    uint32       cues         = 0;
    Subscription subscription = rig._pAbilities->registerGameplayCue( SW_DELEGATE_LAMBDA( AbilityComponent::AbilityPresentationDelegate, [&]( const AbilityPresentationEvent& )
    { ++cues; } ) );
    auto         pHit         = Rig::makeEffect( EffectDurationPolicy::Instant );
    pHit->_cueTags            = Util::makeTags( { cue } );
    Rig::addModifier( *pHit, Rig::kHealth, AttributeModOp::Add, -10.0f );
    (void)rig.apply( pHit );
    SW_EXPECT_EQUAL( 1u, cues );

    rig._pAbilities->getOwnedTagCounts().updateCount( AbilityComponent::getInvulnerableTag(), 1 );
    SW_EXPECT_TRUE( rig._pAbilities->isInvulnerable() );
    (void)rig.apply( pHit );
    SW_EXPECT_EQUAL( 90.0f, rig.base( Rig::kHealth ) );
    SW_EXPECT_EQUAL( 1u, cues );

    auto pHeal = Rig::makeEffect( EffectDurationPolicy::Instant );
    Rig::addModifier( *pHeal, Rig::kHealth, AttributeModOp::Add, 5.0f );
    (void)rig.apply( pHeal );
    SW_EXPECT_EQUAL( 95.0f, rig.base( Rig::kHealth ) );
}

SW_TEST_CASE( AbilityEffectTest, CuesFollowThePhases )
{
    Rig                              rig;
    vector<AbilityPresentationPhase> listPhase;
    Subscription                     subscription = rig._pAbilities->registerGameplayCue( SW_DELEGATE_LAMBDA( AbilityComponent::AbilityPresentationDelegate, [&]( const AbilityPresentationEvent& event )
                        { listPhase.push_back( event._phase ); } ) );
    auto                             pDef         = Rig::makeEffect( EffectDurationPolicy::Infinite );
    pDef->_cueTags                                = Util::makeTags( { "Test.Cue.Aura"_tag } );
    const ActiveEffectHandle handle               = rig.apply( pDef );
    SW_EXPECT_TRUE( rig._pAbilities->removeActiveEffect( handle ) );
    SW_REQUIRE_EQUAL( size_t{ 2 }, listPhase.size() );
    SW_EXPECT_TRUE( listPhase[0] == AbilityPresentationPhase::Added );
    SW_EXPECT_TRUE( listPhase[1] == AbilityPresentationPhase::Removed );
}

// ---------------------------------------------------------------------- 비용

SW_TEST_CASE( AbilityEffectTest, CostChecksTheCurrentValue )
{
    Rig  rig;
    auto pCost = Rig::makeEffect( EffectDurationPolicy::Instant );
    Rig::addModifier( *pCost, Rig::kMana, AttributeModOp::Add, -60.0f );
    SW_EXPECT_FALSE( rig._pAbilities->canApplyAttributeModifiers( rig._pAbilities->makeOutgoingSpec( pCost ) ) );
    SW_EXPECT_TRUE( rig._pAbilities->setAttributeBaseValue( Rig::kMana, 60.0f ) );
    SW_EXPECT_TRUE( rig._pAbilities->canApplyAttributeModifiers( rig._pAbilities->makeOutgoingSpec( pCost ) ) );
    auto pMissing = Rig::makeEffect( EffectDurationPolicy::Instant );
    Rig::addModifier( *pMissing, hashed_string( "Nope" ), AttributeModOp::Add, -1.0f );
    SW_EXPECT_FALSE( rig._pAbilities->canApplyAttributeModifiers( rig._pAbilities->makeOutgoingSpec( pMissing ) ) );
}

// 같은 어트리뷰트에 비용이 둘이면 합으로 본다. 하나씩 보면 통과한 뒤 음수가 된다.
SW_TEST_CASE( AbilityEffectTest, CostSumsModifiersOfTheSameAttribute )
{
    Rig  rig;
    auto pCost = Rig::makeEffect( EffectDurationPolicy::Instant );
    Rig::addModifier( *pCost, Rig::kMana, AttributeModOp::Add, -30.0f );
    Rig::addModifier( *pCost, Rig::kMana, AttributeModOp::Add, -30.0f );
    SW_EXPECT_FALSE( rig._pAbilities->canApplyAttributeModifiers( rig._pAbilities->makeOutgoingSpec( pCost ) ) );
    SW_EXPECT_TRUE( rig._pAbilities->setAttributeBaseValue( Rig::kMana, 60.0f ) );
    SW_EXPECT_TRUE( rig._pAbilities->canApplyAttributeModifiers( rig._pAbilities->makeOutgoingSpec( pCost ) ) );
}

// ---------------------------------------------------------------------- 어빌리티

SW_TEST_CASE( AbilityTest, GrantActivateEndAndClear )
{
    Rig           rig;
    ProbeAbility& ability = rig.give();
    SW_EXPECT_EQUAL( 1u, ability._grantedCount );
    SW_EXPECT_EQUAL( 1u, rig._pAbilities->getAbilityCount() );
    const AbilitySpecHandle handle = ability.getSpecHandle();
    SW_EXPECT_TRUE( rig._pAbilities->tryActivateAbility( handle ) == AbilityActivationResult::Activated );
    SW_EXPECT_TRUE( rig._pAbilities->isAbilityActive( handle ) );
    SW_EXPECT_TRUE( rig._pAbilities->tryActivateAbility( handle ) == AbilityActivationResult::AlreadyActive );

    rig._pAbilities->advanceTime( 0.1f );
    SW_EXPECT_EQUAL( 1u, ability._tickCount );

    ability.endAbility();
    SW_EXPECT_EQUAL( 1u, ability._endedCount );
    SW_EXPECT_EQUAL( 0u, ability._cancelledCount );
    rig._pAbilities->advanceTime( 0.1f );
    SW_EXPECT_EQUAL( 1u, ability._tickCount ); // 끝나면 돌지 않는다

    SW_EXPECT_TRUE( rig._pAbilities->tryActivateAbility( AbilitySpecHandle{} ) == AbilityActivationResult::InvalidSpec );
    SW_EXPECT_TRUE( rig._pAbilities->clearAbility( handle ) );
    SW_EXPECT_FALSE( rig._pAbilities->clearAbility( handle ) );
    SW_EXPECT_EQUAL( 0u, rig._pAbilities->getAbilityCount() );
}

SW_TEST_CASE( AbilityTest, ClearingARunningAbilityCancelsIt )
{
    Rig                     rig;
    uint32                  endedLater   = 0;
    Subscription            subscription = rig._pAbilities->registerAbilityEnded( SW_DELEGATE_LAMBDA( AbilityComponent::AbilityEndedDelegate, [&]( const Ability&, bool bCancelled )
               { endedLater += bCancelled ? 1u : 0u; } ) );
    ProbeAbility&           ability      = rig.give();
    const AbilitySpecHandle handle       = ability.getSpecHandle();
    SW_REQUIRE_TRUE( rig._pAbilities->tryActivateAbility( handle ) == AbilityActivationResult::Activated );
    SW_EXPECT_TRUE( rig._pAbilities->clearAbility( handle ) );
    SW_EXPECT_EQUAL( 1u, endedLater );
}

SW_TEST_CASE( AbilityTest, RetriggerRestartsARunningAbility )
{
    Rig           rig;
    AbilityConfig config;
    config._bRetriggerInstancedAbility = SW_TRUE;
    ProbeAbility&           ability    = rig.give( config );
    const AbilitySpecHandle handle     = ability.getSpecHandle();
    SW_REQUIRE_TRUE( rig._pAbilities->tryActivateAbility( handle ) == AbilityActivationResult::Activated );
    SW_REQUIRE_TRUE( rig._pAbilities->tryActivateAbility( handle ) == AbilityActivationResult::Activated );
    SW_EXPECT_EQUAL( 2u, ability._activatedCount );
    SW_EXPECT_EQUAL( 1u, ability._cancelledCount );
}

SW_TEST_CASE( AbilityTest, PassiveActivatesWhenGranted )
{
    Rig           rig;
    AbilityConfig config;
    config._bActivateOnGranted = SW_TRUE;
    SW_EXPECT_EQUAL( 1u, rig.give( config )._activatedCount );
}

SW_TEST_CASE( AbilityTest, TagConditionsDecideActivation )
{
    Rig           rig;
    const TagID   stunned = "Test.State.Stunned"_tag;
    const TagID   armed   = "Test.State.Armed"_tag;
    AbilityConfig config;
    config._activationRequiredTags  = Util::makeTags( { armed } );
    config._activationBlockedTags   = Util::makeTags( { stunned } );
    ProbeAbility&           ability = rig.give( config );
    const AbilitySpecHandle handle  = ability.getSpecHandle();
    SW_EXPECT_TRUE( rig._pAbilities->canActivateAbility( handle ) == AbilityActivationResult::MissingRequiredTag );
    rig._pAbilities->getOwnedTagCounts().updateCount( armed, 1 );
    SW_EXPECT_TRUE( rig._pAbilities->canActivateAbility( handle ) == AbilityActivationResult::Activated );
    rig._pAbilities->getOwnedTagCounts().updateCount( stunned, 1 );
    SW_EXPECT_TRUE( rig._pAbilities->canActivateAbility( handle ) == AbilityActivationResult::BlockedByTag );
    rig._pAbilities->getOwnedTagCounts().updateCount( stunned, -1 );
    ability._bReject = SW_TRUE;
    SW_EXPECT_TRUE( rig._pAbilities->canActivateAbility( handle ) == AbilityActivationResult::RejectedByAbility );
}

SW_TEST_CASE( AbilityTest, RunningAbilityOwnsTagsBlocksAndCancelsOthers )
{
    Rig           rig;
    const TagID   melee   = "Test.Ability.Melee"_tag;
    const TagID   casting = "Test.State.Casting"_tag;
    AbilityConfig meleeConfig;
    meleeConfig._abilityTags = Util::makeTags( { melee } );
    ProbeAbility& swing      = rig.give( meleeConfig );

    AbilityConfig spellConfig;
    spellConfig._activationOwnedTags     = Util::makeTags( { casting } );
    spellConfig._blockAbilitiesWithTags  = Util::makeTags( { melee } );
    spellConfig._cancelAbilitiesWithTags = Util::makeTags( { melee } );
    ProbeAbility& spell                  = rig.give( spellConfig );

    SW_REQUIRE_TRUE( rig._pAbilities->tryActivateAbility( swing.getSpecHandle() ) == AbilityActivationResult::Activated );
    SW_REQUIRE_TRUE( rig._pAbilities->tryActivateAbility( spell.getSpecHandle() ) == AbilityActivationResult::Activated );
    SW_EXPECT_EQUAL( 1u, swing._cancelledCount );
    SW_EXPECT_TRUE( rig._pAbilities->getOwnedTagCounts().hasMatchingTag( casting ) );
    SW_EXPECT_TRUE( rig._pAbilities->canActivateAbility( swing.getSpecHandle() ) == AbilityActivationResult::BlockedByTag );

    spell.endAbility();
    SW_EXPECT_FALSE( rig._pAbilities->getOwnedTagCounts().hasMatchingTag( casting ) );
    SW_EXPECT_TRUE( rig._pAbilities->canActivateAbility( swing.getSpecHandle() ) == AbilityActivationResult::Activated );
}

SW_TEST_CASE( AbilityTest, CostAndCooldownApplyOnCommit )
{
    Rig  rig;
    auto pCost = Rig::makeEffect( EffectDurationPolicy::Instant );
    Rig::addModifier( *pCost, Rig::kMana, AttributeModOp::Add, -30.0f );
    auto pCooldown          = Rig::makeEffect( EffectDurationPolicy::HasDuration, 1.0f );
    pCooldown->_grantedTags = Util::makeTags( { "Test.Cooldown.Fire"_tag } );
    AbilityConfig config;
    config._pCostEffect            = pCost;
    config._pCooldownEffect        = pCooldown;
    ProbeAbility& ability          = rig.give( config );
    ability._bCommitOnActivate     = SW_TRUE;
    ability._bEndAtOnce            = SW_TRUE;
    const AbilitySpecHandle handle = ability.getSpecHandle();

    SW_REQUIRE_TRUE( rig._pAbilities->tryActivateAbility( handle ) == AbilityActivationResult::Activated );
    SW_EXPECT_EQUAL( 20.0f, rig.base( Rig::kMana ) );
    SW_EXPECT_TRUE( ability.isOnCooldown() );
    SW_EXPECT_NEAR( 1.0f, ability.getCooldownRemaining(), 1e-4f );
    SW_EXPECT_TRUE( rig._pAbilities->tryActivateAbility( handle ) == AbilityActivationResult::OnCooldown );

    rig._pAbilities->advanceTime( 1.5f );
    SW_EXPECT_TRUE( rig._pAbilities->tryActivateAbility( handle ) == AbilityActivationResult::CannotAffordCost );
    SW_EXPECT_EQUAL( 20.0f, rig.base( Rig::kMana ) );
}

SW_TEST_CASE( AbilityTest, InputPressActivatesAndReleaseIsDelivered )
{
    Rig           rig;
    ProbeAbility& ability = rig.give( AbilityConfig{}, 3 );
    rig._pAbilities->abilityInputPressed( 9 );
    SW_EXPECT_EQUAL( 0u, ability._activatedCount );
    rig._pAbilities->abilityInputPressed( AbilityComponent::kNoInputID );
    rig._pAbilities->abilityInputPressed( 3 );
    SW_EXPECT_EQUAL( 1u, ability._activatedCount );
    SW_EXPECT_TRUE( ability.isInputPressed() );
    rig._pAbilities->abilityInputPressed( 3 ); // 도는 중이면 다시 발동하지 않는다
    SW_EXPECT_EQUAL( 1u, ability._activatedCount );
    rig._pAbilities->abilityInputReleased( 3 );
    SW_EXPECT_FALSE( ability.isInputPressed() );
    SW_EXPECT_EQUAL( 1u, ability._releasedCount );
}

SW_TEST_CASE( AbilityTest, EventsTriggerByTagHierarchy )
{
    Rig           rig;
    AbilityConfig config;
    config._triggerEventTags = Util::makeTags( { "Test.Event.Hit"_tag } );
    ProbeAbility& ability    = rig.give( config );
    ability._bEndAtOnce      = SW_TRUE;

    SW_EXPECT_EQUAL( 1, rig._pAbilities->handleGameplayEvent( "Test.Event.Hit.Fire"_tag, AbilityEventData{} ) );
    SW_EXPECT_EQUAL( 1u, ability._activatedCount );
    SW_EXPECT_EQUAL( 0, rig._pAbilities->handleGameplayEvent( "Test.Event.Other"_tag, AbilityEventData{} ) );
    SW_EXPECT_EQUAL( 0, rig._pAbilities->handleGameplayEvent( TagID{}, AbilityEventData{} ) );
}

// 구독자는 그 태그와 조상 태그로 받는다. 형제 태그는 받지 않는다.
SW_TEST_CASE( AbilityTest, EventSubscribersHearDescendants )
{
    Rig          rig;
    uint32       parent              = 0;
    uint32       sibling             = 0;
    Subscription parentSubscription  = rig._pAbilities->registerGameplayEvent( "Test.Event.Hit"_tag, SW_DELEGATE_LAMBDA( AbilityComponent::AbilityEventDelegate, [&]( const AbilityEventData& )
     { ++parent; } ) );
    Subscription siblingSubscription = rig._pAbilities->registerGameplayEvent( "Test.Event.Heal"_tag, SW_DELEGATE_LAMBDA( AbilityComponent::AbilityEventDelegate, [&]( const AbilityEventData& )
    { ++sibling; } ) );
    (void)rig._pAbilities->handleGameplayEvent( "Test.Event.Hit.Fire"_tag, AbilityEventData{} );
    SW_EXPECT_EQUAL( 1u, parent );
    SW_EXPECT_EQUAL( 0u, sibling );
}

SW_TEST_CASE( AbilityTest, OwnedTagTriggersOnlyWhenItAppears )
{
    Rig           rig;
    const TagID   burning = "Test.State.Burning"_tag;
    AbilityConfig config;
    config._ownedTagTriggerTags = Util::makeTags( { burning } );
    ProbeAbility& ability       = rig.give( config );
    ability._bEndAtOnce         = SW_TRUE;
    rig._pAbilities->getOwnedTagCounts().updateCount( burning, 1 );
    SW_EXPECT_EQUAL( 1u, ability._activatedCount );
    rig._pAbilities->getOwnedTagCounts().updateCount( burning, 1 ); // 0 → 1 일 때만이다
    SW_EXPECT_EQUAL( 1u, ability._activatedCount );
}

SW_TEST_CASE( AbilityTest, ActivateByTagAndCancelAll )
{
    Rig           rig;
    const TagID   melee = "Test.Ability.Melee"_tag;
    AbilityConfig config;
    config._abilityTags   = Util::makeTags( { "Test.Ability.Melee.Swing"_tag } );
    ProbeAbility& ability = rig.give( config );
    SW_EXPECT_TRUE( rig._pAbilities->tryActivateAbilitiesByTag( Util::makeTags( { melee } ) ) );
    SW_EXPECT_TRUE( ability.isActive() );
    SW_EXPECT_FALSE( rig._pAbilities->tryActivateAbilitiesByTag( Util::makeTags( { "Test.Ability.Other"_tag } ) ) );
    rig._pAbilities->cancelAbilitiesWithTags( Util::makeTags( { melee } ), &ability ); // 빼 둔 것은 취소하지 않는다
    SW_EXPECT_TRUE( ability.isActive() );
    rig._pAbilities->cancelAllAbilities();
    SW_EXPECT_FALSE( ability.isActive() );
    SW_EXPECT_EQUAL( 1u, ability._cancelledCount );
}

// 어빌리티가 콜백 안에서 자기를 치워도 순회가 깨지지 않는다.
SW_TEST_CASE( AbilityTest, RemovalDuringCallbacksIsDeferred )
{
    Rig                     rig;
    AbilityComponent*       pAbilities   = rig._pAbilities;
    uint32                  ends         = 0;
    ProbeAbility&           first        = rig.give();
    ProbeAbility&           second       = rig.give();
    const AbilitySpecHandle firstHandle  = first.getSpecHandle();
    const AbilitySpecHandle secondHandle = second.getSpecHandle();
    Subscription            subscription = pAbilities->registerAbilityEnded( SW_DELEGATE_LAMBDA( AbilityComponent::AbilityEndedDelegate, [&, pAbilities, secondHandle]( const Ability&, bool )
               {
        ++ends;
        (void)pAbilities->clearAbility( secondHandle );
    } ) );
    SW_REQUIRE_TRUE( pAbilities->tryActivateAbility( firstHandle ) == AbilityActivationResult::Activated );
    SW_REQUIRE_TRUE( pAbilities->tryActivateAbility( secondHandle ) == AbilityActivationResult::Activated );
    pAbilities->cancelAllAbilities();
    SW_EXPECT_TRUE( ends >= 1u );
    SW_EXPECT_TRUE( pAbilities->findAbility( secondHandle ) == nullptr );
    SW_EXPECT_EQUAL( 1u, pAbilities->getAbilityCount() );
}
