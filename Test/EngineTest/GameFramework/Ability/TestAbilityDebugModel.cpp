// 어빌리티 디버그 창의 행 모델 — 어트리뷰트(base · current), 걸린 이펙트(이름 · 남은 시간 · 스택 · 무한), 소유 태그와 개수, 어빌리티 쿨다운이
// 컴포넌트 상태 그대로 행이 되는지 본다.
#include "pch.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Gameplay/Ability/AbilitySystemComponent.h"
#include "GameFramework/Base/Gameplay/Ability/GameplayAbility.h"
#include "GameFramework/PlugIn/Editor/Base/Ability/AbilityDebugModel.h"

#include "TestFramework/TestFramework.h"

namespace sw
{
    /** @brief 커밋만 하고 끝나는 테스트 어빌리티입니다 — 쿨다운을 겁니다. */
    class AbilityDebugTestAbility : public GameplayAbility
    {
    public:
        void activateAbility( const GameplayEventData* pTriggerEvent ) override
        {
            (void)pTriggerEvent;
            (void)commitAbility();
            endAbility();
        }
    };
} // namespace sw

using namespace sw;
using namespace sw::editor;

SW_TEST_CASE( AbilityDebugModelTest, CollectTurnsComponentStateIntoRows )
{
    GameObjectManager manager;
    GameObject*       pObject = manager.createGameObject( hashed_string( "Hero" ) );
    SW_ASSERT_NOT_NULL( pObject );
    AbilitySystemComponent* pAbilitySystem = pObject->addComponent<AbilitySystemComponent>();
    SW_ASSERT_NOT_NULL( pAbilitySystem );

    unique_ptr<AttributeSet> pSet = make_unique<AttributeSet>();
    pSet->defineAttribute( "Power", 10.0f );
    (void)pAbilitySystem->addAttributeSet( std::move( pSet ) );

    // 지속 버프(Power +5, 3 초, 태그 State.Buffed)와 무한 이펙트(같은 태그)
    shared_ptr<GameplayEffectDef> pBuff = make_shared<GameplayEffectDef>();
    pBuff->_id                          = hashed_string( "GE_Buff" );
    pBuff->_durationPolicy              = EffectDurationPolicy::HasDuration;
    pBuff->_duration._baseValue         = 3.0f;
    pBuff->_grantedTags.addTag( "State.Buffed"_tag );
    GameplayEffectModifier modifier;
    modifier._attribute                    = hashed_string( "Power" );
    modifier._op                           = AttributeModOp::Add;
    modifier._scalableMagnitude._baseValue = 5.0f;
    pBuff->_listModifier.push_back( modifier );

    shared_ptr<GameplayEffectDef> pAura = make_shared<GameplayEffectDef>();
    pAura->_id                          = hashed_string( "GE_Aura" );
    pAura->_durationPolicy              = EffectDurationPolicy::Infinite;
    pAura->_grantedTags.addTag( "State.Buffed"_tag );

    SW_EXPECT_TRUE( pAbilitySystem->applyGameplayEffectSpecToSelf( pAbilitySystem->makeOutgoingSpec( pBuff ) ).isValid() );
    SW_EXPECT_TRUE( pAbilitySystem->applyGameplayEffectSpecToSelf( pAbilitySystem->makeOutgoingSpec( pAura ) ).isValid() );

    // 쿨다운 2 초 어빌리티를 한 번 쓴다
    shared_ptr<GameplayEffectDef> pCooldown = make_shared<GameplayEffectDef>();
    pCooldown->_id                          = hashed_string( "GE_Cooldown" );
    pCooldown->_durationPolicy              = EffectDurationPolicy::HasDuration;
    pCooldown->_duration._baseValue         = 2.0f;
    pCooldown->_grantedTags.addTag( "Cooldown.Strike"_tag );
    unique_ptr<AbilityDebugTestAbility> pAbility = make_unique<AbilityDebugTestAbility>();
    pAbility->getConfig()._id                    = hashed_string( "GA_Strike" );
    pAbility->getConfig()._pCooldownEffect       = pCooldown;
    const AbilitySpecHandle handle               = pAbilitySystem->giveAbility( std::move( pAbility ) );
    SW_EXPECT_TRUE( pAbilitySystem->tryActivateAbility( handle ) == AbilityActivationResult::Activated );

    pAbilitySystem->advanceTime( 1.0f );

    AbilityDebugSnapshot snapshot;
    AbilityDebugModel::collect( *pAbilitySystem, snapshot );

    SW_EXPECT_NEAR_EQUAL( 1.0f, snapshot._time, 0.001f );

    SW_ASSERT_EQUAL( 1, static_cast<int32>( snapshot._listAttribute.size() ) );
    SW_EXPECT_TRUE( snapshot._listAttribute[0]._name == "Power" );
    SW_EXPECT_NEAR_EQUAL( 10.0f, snapshot._listAttribute[0]._baseValue, 0.001f );
    SW_EXPECT_NEAR_EQUAL( 15.0f, snapshot._listAttribute[0]._currentValue, 0.001f );

    // 건 순서: 버프, 오라, 쿨다운
    SW_ASSERT_EQUAL( 3, static_cast<int32>( snapshot._listEffect.size() ) );
    SW_EXPECT_TRUE( snapshot._listEffect[0]._name == "GE_Buff" );
    SW_EXPECT_FALSE( snapshot._listEffect[0]._bInfinite );
    SW_EXPECT_NEAR_EQUAL( 2.0f, snapshot._listEffect[0]._remainingTime, 0.001f );
    SW_EXPECT_EQUAL( 1, snapshot._listEffect[0]._stackCount );
    SW_EXPECT_TRUE( snapshot._listEffect[1]._name == "GE_Aura" );
    SW_EXPECT_TRUE( snapshot._listEffect[1]._bInfinite );
    SW_EXPECT_TRUE( snapshot._listEffect[2]._name == "GE_Cooldown" );

    // 같은 태그를 두 이펙트가 주면 개수 2
    bool bFoundBuffed = false;
    for ( const AbilityDebugTagRow& row : snapshot._listTag )
    {
        if ( row._tag == "State.Buffed" )
        {
            bFoundBuffed = true;
            SW_EXPECT_EQUAL( 2, row._count );
        }
    }
    SW_EXPECT_TRUE( bFoundBuffed );

    SW_ASSERT_EQUAL( 1, static_cast<int32>( snapshot._listAbility.size() ) );
    SW_EXPECT_TRUE( snapshot._listAbility[0]._name == "GA_Strike" );
    SW_EXPECT_FALSE( snapshot._listAbility[0]._bActive );
    SW_EXPECT_NEAR_EQUAL( 1.0f, snapshot._listAbility[0]._cooldownRemaining, 0.001f );

    // 버프가 끝나면 행에서 빠지고 current 가 base 로 돌아온다
    pAbilitySystem->advanceTime( 2.5f );
    AbilityDebugModel::collect( *pAbilitySystem, snapshot );
    SW_ASSERT_EQUAL( 1, static_cast<int32>( snapshot._listEffect.size() ) );
    SW_EXPECT_TRUE( snapshot._listEffect[0]._name == "GE_Aura" );
    SW_EXPECT_NEAR_EQUAL( 10.0f, snapshot._listAttribute[0]._currentValue, 0.001f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, snapshot._listAbility[0]._cooldownRemaining, 0.001f );
}
