// MinigameRewardTest — 미니게임 결과 값 → 허브 캐릭터의 어빌리티 이펙트(`MinigameRewardComponent`): 표의 이펙트를 값의 수(SetByCaller Data.Amount)로 걸고, 표에 없는 이름 · 카탈로그에 없는 이펙트는 오류다.
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Runtime/Foundation/Framework/Minigame/MinigameStack.h"
#include "GameFramework/Runtime/Gameplay/Ability/AbilityCatalog.h"
#include "GameFramework/Runtime/Gameplay/Ability/AbilityComponent.h"
#include "GameFramework/Runtime/Gameplay/Ability/AbilityEffect.h"
#include "GameFramework/Runtime/Gameplay/Ability/CombatAttributeSet.h"
#include "GameFramework/Runtime/Gameplay/Ability/MinigameRewardComponent.h"
#include "GameFramework/Runtime/Gameplay/Ability/TagCountTable.h"

using namespace sw;

namespace
{
    /** @brief 전투 어트리뷰트 어빌리티 시스템과 보상 표를 단 허브 캐릭터 하나입니다. */
    struct MinigameRewardFixture
    {
        GameObjectManager        _manager;
        AbilityCatalog           _catalog;
        AbilityComponent*        _pAbility{ nullptr };
        MinigameRewardComponent* _pReward{ nullptr };

        MinigameRewardFixture()
        {
            GameObject* const pHero = _manager.createGameObject( hashed_string( "HubPlayer" ) );
            _pAbility               = pHero->addComponent<AbilityComponent>();
            _pReward                = pHero->addComponent<MinigameRewardComponent>();
            _pAbility->setCatalog( &_catalog );
            (void)_pAbility->addAttributeSet<CombatAttributeSet>();

            AbilityEffectDef heal;
            heal._id             = hashed_string( "GE_RewardHeal" );
            heal._durationPolicy = EffectDurationPolicy::Instant;
            AbilityEffectModifier healing;
            healing._attribute       = CombatAttributes::incomingHealing();
            healing._op              = AttributeModOp::Add;
            healing._magnitudeSource = EffectMagnitudeSource::SetByCaller;
            healing._setByCallerName = hashed_string( MinigameRewardComponent::kAmountName );
            heal._listModifier.push_back( healing );
            (void)_catalog.addEffect( heal );

            AbilityEffectDef haste;
            haste._id                  = hashed_string( "GE_RewardHaste" );
            haste._durationPolicy      = EffectDurationPolicy::HasDuration;
            haste._duration._baseValue = 60.0f;
            haste._grantedTags.addTag( "State.Hasted"_tag );
            (void)_catalog.addEffect( haste );

            vector<MinigameRewardEffect> listReward;
            listReward.push_back( makeReward( "heal", "GE_RewardHeal" ) );
            listReward.push_back( makeReward( "haste", "GE_RewardHaste" ) );
            listReward.push_back( makeReward( "ghost", "GE_NotInCatalog" ) );
            _pReward->setRewardTable( std::move( listReward ) );
        }

        static MinigameRewardEffect makeReward( const utf8* pValue, const utf8* pEffect )
        {
            MinigameRewardEffect reward;
            reward._value  = hashed_string( pValue );
            reward._effect = CatalogKey<AbilityEffectDef>( hashed_string( pEffect ) );
            return reward;
        }

        float32 health() const { return _pAbility->getAttributeValue( CombatAttributes::health() ).value_or( -9999.0f ); }

        static MinigameResult makeResult( std::initializer_list<MinigameResultValue> listValue )
        {
            MinigameResult result;
            result._minigame   = hashed_string( "arcade" );
            result._resultName = hashed_string( "win" );
            for ( const MinigameResultValue& value : listValue )
            {
                result._listValue.push_back( value );
            }
            return result;
        }
    };
} // namespace

/** @brief [MinigameRewardTest] 결과 값 `heal 30` 은 표의 회복 이펙트로 체력을 30 채우고, `haste` 는 버프 태그를 건다 — 미니게임 디렉터는 캐릭터를 직접 고치지 않는다 */
SW_TEST_CASE( MinigameRewardTest, ResultValuesApplyAsEffects )
{
    MinigameRewardFixture rig;
    SW_REQUIRE_TRUE( rig._pAbility != nullptr && rig._pReward != nullptr );
    SW_REQUIRE_TRUE( rig._pAbility->setAttributeBaseValue( CombatAttributes::health(), 40.0f ) );

    const MinigameResult result = MinigameRewardFixture::makeResult( {
        MinigameResultValue{ hashed_string( "heal" ), 30},
        MinigameResultValue{hashed_string( "haste" ),  1}
    } );
    SW_EXPECT_TRUE( rig._pReward->applyResult( result ) );
    SW_EXPECT_NEAR( 70.0f, rig.health(), 1.0e-4f );
    SW_EXPECT_TRUE( rig._pAbility->getOwnedTagCounts().hasMatchingTag( "State.Hasted"_tag ) );
    SW_EXPECT_EQUAL( 2u, rig._pReward->getAppliedCount() );
}

/** @brief [MinigameRewardTest] 표에 없는 이름은 오류이고 false 다 — 같은 결과의 다른 값(heal)은 그대로 걸린다 */
SW_TEST_CASE( MinigameRewardTest, UnknownValueNameIsError )
{
    SW_EXPECT_LOG( "MinigameReward.UnknownValue" );
    MinigameRewardFixture rig;
    SW_REQUIRE_TRUE( rig._pAbility != nullptr && rig._pReward != nullptr );
    SW_REQUIRE_TRUE( rig._pAbility->setAttributeBaseValue( CombatAttributes::health(), 40.0f ) );

    const MinigameResult result = MinigameRewardFixture::makeResult( {
        MinigameResultValue{hashed_string( "gold" ), 100},
        MinigameResultValue{hashed_string( "heal" ),  10}
    } );
    SW_EXPECT_FALSE( rig._pReward->applyResult( result ) );
    SW_EXPECT_NEAR( 50.0f, rig.health(), 1.0e-4f );
    SW_EXPECT_EQUAL( 1u, rig._pReward->getAppliedCount() );
}

/** @brief [MinigameRewardTest] 표의 이펙트가 카탈로그에 없으면 오류이고 아무것도 걸지 않는다 */
SW_TEST_CASE( MinigameRewardTest, EffectMissingFromCatalogIsError )
{
    SW_EXPECT_LOG( "MinigameReward.UnknownEffect" );
    MinigameRewardFixture rig;
    SW_REQUIRE_TRUE( rig._pAbility != nullptr && rig._pReward != nullptr );
    SW_EXPECT_FALSE( rig._pReward->applyResult( MinigameRewardFixture::makeResult( {
        MinigameResultValue{ hashed_string( "ghost" ), 1 }
    } ) ) );
    SW_EXPECT_EQUAL( 0u, rig._pReward->getAppliedCount() );
}

/** @brief [MinigameRewardTest] 값이 없는 결과(결과 없이 나옴)는 할 일이 없다 — 오류도 아니다 */
SW_TEST_CASE( MinigameRewardTest, EmptyResultIsNothing )
{
    MinigameRewardFixture rig;
    SW_REQUIRE_TRUE( rig._pReward != nullptr );
    SW_EXPECT_TRUE( rig._pReward->applyResult( MinigameRewardFixture::makeResult( {} ) ) );
    SW_EXPECT_EQUAL( 0u, rig._pReward->getAppliedCount() );
}
