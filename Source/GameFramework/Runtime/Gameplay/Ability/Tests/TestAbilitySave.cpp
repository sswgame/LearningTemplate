// AbilitySystemTest(저장) — 어빌리티 컴포넌트의 상태 스냅샷: base 를 훅 없이 다 쓴 뒤 한 번에 자르기, 걸린 이펙트의 남은 시간 · 주기, 이펙트 밖 태그, 깨진 바이트 거절.
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Runtime/Gameplay/Ability/AbilityCatalog.h"
#include "GameFramework/Runtime/Gameplay/Ability/AbilityComponent.h"
#include "GameFramework/Runtime/Gameplay/Ability/AbilityEffect.h"
#include "GameFramework/Runtime/Gameplay/Ability/CombatAttributeSet.h"
#include "GameFramework/Runtime/Gameplay/Ability/TagCountTable.h"

using namespace sw;

namespace
{
    /** @brief 같은 카탈로그를 쓰는 전투 어트리뷰트 오브젝트 둘(저장하는 쪽 · 받는 쪽)입니다. */
    struct AbilitySaveFixture
    {
        GameObjectManager _manager;
        AbilityCatalog    _catalog;
        AbilityComponent* _pSource{ nullptr };
        AbilityComponent* _pTarget{ nullptr };

        AbilitySaveFixture()
        {
            _pSource = makeHero( "Source" );
            _pTarget = makeHero( "Target" );
        }

        AbilityComponent* makeHero( const utf8* pName )
        {
            AbilityComponent* pAbilities = _manager.createGameObject( hashed_string( pName ) )->addComponent<AbilityComponent>();
            pAbilities->setCatalog( &_catalog );
            (void)pAbilities->addAttributeSet<CombatAttributeSet>();
            return pAbilities;
        }

        void addEffect( const utf8* pID, EffectDurationPolicy policy, float32 duration, float32 period, const hashed_string& attribute, float32 magnitude, TagID grantedTag )
        {
            AbilityEffectDef def;
            def._id                            = hashed_string( pID );
            def._durationPolicy                = policy;
            def._duration._baseValue           = duration;
            def._period                        = period;
            def._bExecutePeriodicOnApplication = SW_FALSE;
            if ( attribute.empty() == false )
            {
                AbilityEffectModifier modifier;
                modifier._attribute                    = attribute;
                modifier._op                           = AttributeModOp::Add;
                modifier._scalableMagnitude._baseValue = magnitude;
                def._listModifier.push_back( modifier );
            }
            if ( grantedTag.isValid() )
                def._grantedTags.addTag( grantedTag );
            (void)_catalog.addEffect( def );
        }

        /** @brief 저장하는 쪽을 써서 받는 쪽에 읽습니다. */
        bool roundTrip()
        {
            Archive written;
            StateIO::writeObject( *_pSource, written );
            Archive reading( written.getData(), written.getSize() );
            return StateIO::readInPlace( *_pTarget, reading );
        }

        static float32 base( const AbilityComponent& abilities, const hashed_string& name ) { return abilities.getAttributeBaseValue( name ).value_or( -9999.0f ); }
        static float32 current( const AbilityComponent& abilities, const hashed_string& name ) { return abilities.getAttributeValue( name ).value_or( -9999.0f ); }
    };
    using Rig = AbilitySaveFixture;
} // namespace

SW_TEST_CASE( AbilitySystemTest, SaveRoundTripKeepsHealthAboveDefaultMax )
{
    Rig rig;
    rig.addEffect( "GE_ManaBuff", EffectDurationPolicy::Infinite, 0.0f, 0.0f, CombatAttributes::maxMana(), 100.0f, TagID{} );
    SW_REQUIRE_TRUE( rig._pSource->setAttributeBaseValue( CombatAttributes::maxHealth(), 200.0f ) ); // 레벨 업으로 오른 base
    SW_REQUIRE_TRUE( rig._pSource->setAttributeBaseValue( CombatAttributes::health(), 180.0f ) );
    SW_REQUIRE_TRUE( rig._pSource->applyGameplayEffectToSelf( hashed_string( "GE_ManaBuff" ) ).isValid() ); // 버프로 오른 최대치
    SW_REQUIRE_TRUE( rig._pSource->setAttributeBaseValue( CombatAttributes::mana(), 170.0f ) );

    SW_REQUIRE_TRUE( rig.roundTrip() );
    // 하나씩 쓰면 체력이 받는 쪽의 그 순간 최대 체력(기본 100)으로 잘린다(180/200 → 100/200).
    SW_EXPECT_NEAR( 180.0f, Rig::base( *rig._pTarget, CombatAttributes::health() ), 1.0e-4f );
    SW_EXPECT_NEAR( 180.0f, Rig::current( *rig._pTarget, CombatAttributes::health() ), 1.0e-4f );
    SW_EXPECT_NEAR( 200.0f, Rig::current( *rig._pTarget, CombatAttributes::maxHealth() ), 1.0e-4f );
    SW_EXPECT_NEAR( 170.0f, Rig::base( *rig._pTarget, CombatAttributes::mana() ), 1.0e-4f );
    SW_EXPECT_NEAR( 100.0f, Rig::base( *rig._pTarget, CombatAttributes::maxMana() ), 1.0e-4f );
    SW_EXPECT_NEAR( 200.0f, Rig::current( *rig._pTarget, CombatAttributes::maxMana() ), 1.0e-4f );
    SW_EXPECT_EQUAL( 1u, rig._pTarget->getActiveEffectCount() );
}

SW_TEST_CASE( AbilitySystemTest, SaveRoundTripKeepsCooldownAndDot )
{
    Rig         rig;
    const TagID cooldownTag = "Cooldown.Fire"_tag;
    const TagID looseTag    = "Test.Loose"_tag;
    rig.addEffect( "GE_Cooldown", EffectDurationPolicy::HasDuration, 10.0f, 0.0f, hashed_string{}, 0.0f, cooldownTag );
    rig.addEffect( "GE_Burn", EffectDurationPolicy::HasDuration, 6.0f, 1.0f, CombatAttributes::health(), -5.0f, TagID{} );
    SW_REQUIRE_TRUE( rig._pSource->applyGameplayEffectToSelf( hashed_string( "GE_Cooldown" ) ).isValid() );
    SW_REQUIRE_TRUE( rig._pSource->applyGameplayEffectToSelf( hashed_string( "GE_Burn" ) ).isValid() );
    rig._pSource->getOwnedTagCounts().updateCount( looseTag, 1 );
    rig._pSource->advanceTime( 3.0f ); // 화상이 1 · 2 · 3 초에 5 씩
    SW_REQUIRE_TRUE( Rig::base( *rig._pSource, CombatAttributes::health() ) == 85.0f );

    SW_REQUIRE_TRUE( rig.roundTrip() );
    SW_EXPECT_NEAR( 85.0f, Rig::base( *rig._pTarget, CombatAttributes::health() ), 1.0e-4f );
    SW_EXPECT_EQUAL( 2u, rig._pTarget->getActiveEffectCount() );
    // 부여 태그는 다시 걸린 이펙트가 붙인 하나뿐이다(세이브의 이펙트 밖 몫에 섞이면 둘이 된다).
    SW_EXPECT_EQUAL( 1, rig._pTarget->getOwnedTagCounts().getCount( cooldownTag ) );
    SW_EXPECT_EQUAL( 1, rig._pTarget->getOwnedTagCounts().getCount( looseTag ) );
    TagContainer cooldownTags;
    cooldownTags.addTag( cooldownTag );
    SW_EXPECT_NEAR( 7.0f, rig._pTarget->findLongestRemainingTimeWithGrantedTags( cooldownTags ), 1.0e-3f );

    rig._pTarget->advanceTime( 1.0f );
    SW_EXPECT_NEAR( 80.0f, Rig::base( *rig._pTarget, CombatAttributes::health() ), 1.0e-3f );
    rig._pTarget->advanceTime( 7.0f ); // 화상은 6 초에 끝나고(5 · 6 초에 더), 쿨다운은 10 초에 끝난다
    SW_EXPECT_NEAR( 70.0f, Rig::base( *rig._pTarget, CombatAttributes::health() ), 1.0e-3f );
    SW_EXPECT_EQUAL( 0, rig._pTarget->getOwnedTagCounts().getCount( cooldownTag ) );
    SW_EXPECT_EQUAL( 0u, rig._pTarget->getActiveEffectCount() );
}

SW_TEST_CASE( AbilitySystemTest, SaveRejectsTruncatedOrUnknownStateWithoutChanges )
{
    Rig rig;
    rig.addEffect( "GE_Cooldown", EffectDurationPolicy::HasDuration, 10.0f, 0.0f, hashed_string{}, 0.0f, "Cooldown.Fire"_tag );
    SW_REQUIRE_TRUE( rig._pSource->setAttributeBaseValue( CombatAttributes::health(), 40.0f ) );
    SW_REQUIRE_TRUE( rig._pSource->applyGameplayEffectToSelf( hashed_string( "GE_Cooldown" ) ).isValid() );
    Archive written;
    StateIO::writeObject( *rig._pSource, written );

    Archive truncated( written.getData(), written.getSize() - 1 );
    SW_EXPECT_FALSE( StateIO::readInPlace( *rig._pTarget, truncated ) );
    SW_EXPECT_NEAR( 100.0f, Rig::base( *rig._pTarget, CombatAttributes::health() ), 1.0e-4f );
    SW_EXPECT_EQUAL( 0u, rig._pTarget->getActiveEffectCount() );

    // 카탈로그에 없는 이펙트는 조용히 빼지 않고 통째로 거절한다 — 체력도 바뀌지 않는다.
    AbilityCatalog emptyCatalog;
    rig._pTarget->setCatalog( &emptyCatalog );
    Archive reading( written.getData(), written.getSize() );
    SW_EXPECT_LOG( "AbilityComponent.SavedEffectUnknown" );
    SW_EXPECT_FALSE( StateIO::readInPlace( *rig._pTarget, reading ) );
    rig._pTarget->setCatalog( &rig._catalog ); // 지역 카탈로그를 가리킨 채 남기지 않는다
    SW_EXPECT_NEAR( 100.0f, Rig::base( *rig._pTarget, CombatAttributes::health() ), 1.0e-4f );
    SW_EXPECT_EQUAL( 0u, rig._pTarget->getActiveEffectCount() );
}
