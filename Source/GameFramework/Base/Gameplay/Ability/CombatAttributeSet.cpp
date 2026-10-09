#include "pch.h"

#include "GameFramework/Base/Gameplay/Ability/CombatAttributeSet.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"

#include "GameFramework/Base/Foundation/Framework/GameEventUtil.h"
#include "GameFramework/Base/Gameplay/Ability/AbilitySystemComponent.h"
#include "GameFramework/Base/Gameplay/Ability/AbilitySystemEvents.h"

namespace sw
{
    const hashed_string& CombatAttributes::health()
    {
        static const hashed_string kName{ "Health" };
        return kName;
    }

    const hashed_string& CombatAttributes::maxHealth()
    {
        static const hashed_string kName{ "MaxHealth" };
        return kName;
    }

    const hashed_string& CombatAttributes::mana()
    {
        static const hashed_string kName{ "Mana" };
        return kName;
    }

    const hashed_string& CombatAttributes::maxMana()
    {
        static const hashed_string kName{ "MaxMana" };
        return kName;
    }

    const hashed_string& CombatAttributes::attackPower()
    {
        static const hashed_string kName{ "AttackPower" };
        return kName;
    }

    const hashed_string& CombatAttributes::armor()
    {
        static const hashed_string kName{ "Armor" };
        return kName;
    }

    const hashed_string& CombatAttributes::moveSpeed()
    {
        static const hashed_string kName{ "MoveSpeed" };
        return kName;
    }

    const hashed_string& CombatAttributes::incomingDamage()
    {
        static const hashed_string kName{ "IncomingDamage" };
        return kName;
    }

    const hashed_string& CombatAttributes::incomingHealing()
    {
        static const hashed_string kName{ "IncomingHealing" };
        return kName;
    }

    CombatAttributeSet::CombatAttributeSet()
    {
        defineAttribute( CombatAttributes::health(), 100.0f );
        defineAttribute( CombatAttributes::maxHealth(), 100.0f );
        defineAttribute( CombatAttributes::mana(), 100.0f );
        defineAttribute( CombatAttributes::maxMana(), 100.0f );
        defineAttribute( CombatAttributes::attackPower(), 10.0f );
        defineAttribute( CombatAttributes::armor(), 0.0f );
        defineAttribute( CombatAttributes::moveSpeed(), 5.0f );
        defineAttribute( CombatAttributes::incomingDamage(), 0.0f );
        defineAttribute( CombatAttributes::incomingHealing(), 0.0f );
    }

    TagID CombatAttributeSet::getDeadTag()
    {
        return "State.Dead"_tag;
    }

    TagID CombatAttributeSet::getDeathEventTag()
    {
        return "Event.Death"_tag;
    }

    TagID CombatAttributeSet::getHitEventTag()
    {
        return "Event.Hit"_tag;
    }

    void CombatAttributeSet::preAttributeChange( const hashed_string& name, float32& inoutNewValue )
    {
        AttributeSet::preAttributeChange( name, inoutNewValue );
        clampCombatValue( name, inoutNewValue );
    }

    void CombatAttributeSet::preAttributeBaseChange( const hashed_string& name, float32& inoutNewBase )
    {
        AttributeSet::preAttributeBaseChange( name, inoutNewBase );
        clampCombatValue( name, inoutNewBase );
    }

    void CombatAttributeSet::postAttributeChange( const hashed_string& name, float32 oldValue, float32 newValue )
    {
        AttributeSet::postAttributeChange( name, oldValue, newValue );

        // 최대치가 줄면(버프가 풀리면) 현재치를 따라 줄인다 — 현재치의 base 를 자르면 current 도 다시 집계된다. 늘 때는 그대로 둔다
        // (최대 체력 버프로 체력이 차오르지 않는다 — 언리얼 Lyra 와 같다).
        AbilitySystemComponent* pAbilitySystem = getOwningAbilitySystem();
        if ( pAbilitySystem == nullptr || newValue >= oldValue )
            return;

        hashed_string currentName{};
        if ( name == CombatAttributes::maxHealth() )
            currentName = CombatAttributes::health();
        else if ( name == CombatAttributes::maxMana() )
            currentName = CombatAttributes::mana();
        if ( currentName.empty() )
            return;

        const float32 currentBase = pAbilitySystem->getAttributeBaseValue( currentName );
        if ( currentBase > newValue )
            (void)pAbilitySystem->setAttributeBaseValue( currentName, newValue ); // 이 묶음이 가진 어트리뷰트라 실패하지 않는다
    }

    void CombatAttributeSet::postGameplayEffectExecute( const AttributeModCallbackData& data )
    {
        AttributeSet::postGameplayEffectExecute( data );

        if ( data._attribute == CombatAttributes::incomingDamage() )
            applyIncomingDamage( data );
        else if ( data._attribute == CombatAttributes::incomingHealing() )
            applyIncomingHealing( data );
    }

    void CombatAttributeSet::clampCombatValue( const hashed_string& name, float32& inoutValue ) const
    {
        const AbilitySystemComponent* pAbilitySystem = getOwningAbilitySystem();
        if ( name == CombatAttributes::health() )
        {
            const float32 maxValue = pAbilitySystem != nullptr ? pAbilitySystem->getAttributeValue( CombatAttributes::maxHealth() ) : inoutValue;
            inoutValue             = MathUtil::clamp( inoutValue, 0.0f, MathUtil::max( 0.0f, maxValue ) );
        }
        else if ( name == CombatAttributes::mana() )
        {
            const float32 maxValue = pAbilitySystem != nullptr ? pAbilitySystem->getAttributeValue( CombatAttributes::maxMana() ) : inoutValue;
            inoutValue             = MathUtil::clamp( inoutValue, 0.0f, MathUtil::max( 0.0f, maxValue ) );
        }
        else
        {
            const bool bNonNegative = name == CombatAttributes::maxHealth() || name == CombatAttributes::maxMana() || name == CombatAttributes::moveSpeed();
            if ( bNonNegative )
                inoutValue = MathUtil::max( 0.0f, inoutValue );
        }
    }

    void CombatAttributeSet::applyIncomingDamage( const AttributeModCallbackData& data )
    {
        AbilitySystemComponent* pAbilitySystem = getOwningAbilitySystem();
        if ( pAbilitySystem == nullptr )
            return;

        // 메타 어트리뷰트는 받자마자 비운다 — 다음 피해가 이 값 위에 쌓이지 않는다.
        const float32 damage = pAbilitySystem->getAttributeBaseValue( CombatAttributes::incomingDamage() );
        (void)pAbilitySystem->setAttributeBaseValue( CombatAttributes::incomingDamage(), 0.0f );
        if ( damage <= 0.0f || pAbilitySystem->hasMatchingTag( getDeadTag() ) )
            return;

        const float32 oldHealth = pAbilitySystem->getAttributeBaseValue( CombatAttributes::health() );
        (void)pAbilitySystem->setAttributeBaseValue( CombatAttributes::health(), oldHealth - damage );
        const float32 newHealth = pAbilitySystem->getAttributeBaseValue( CombatAttributes::health() );

        GameObject* pOwner = pAbilitySystem->getOwner();
        if ( oldHealth > newHealth )
        {
            GameplayEventData hitEvent;
            hitEvent._eventTag   = getHitEventTag();
            hitEvent._instigator = data._pSpec != nullptr ? data._pSpec->_context._instigator : GameObjectHandle{};
            hitEvent._target     = pOwner != nullptr ? pOwner->getHandle() : GameObjectHandle{};
            hitEvent._magnitude  = oldHealth - newHealth;
            (void)pAbilitySystem->handleGameplayEvent( hitEvent._eventTag, hitEvent ); // 피격 반응이 없어도 된다
        }

        if ( newHealth <= 0.0f )
            handleOutOfHealth( data );
    }

    void CombatAttributeSet::applyIncomingHealing( const AttributeModCallbackData& data )
    {
        (void)data;
        AbilitySystemComponent* pAbilitySystem = getOwningAbilitySystem();
        if ( pAbilitySystem == nullptr )
            return;

        const float32 healing = pAbilitySystem->getAttributeBaseValue( CombatAttributes::incomingHealing() );
        (void)pAbilitySystem->setAttributeBaseValue( CombatAttributes::incomingHealing(), 0.0f );
        if ( healing <= 0.0f || pAbilitySystem->hasMatchingTag( getDeadTag() ) )
            return;

        const float32 health = pAbilitySystem->getAttributeBaseValue( CombatAttributes::health() );
        (void)pAbilitySystem->setAttributeBaseValue( CombatAttributes::health(), health + healing ); // 최대치는 훅이 자른다
    }

    void CombatAttributeSet::handleOutOfHealth( const AttributeModCallbackData& data )
    {
        AbilitySystemComponent* pAbilitySystem = getOwningAbilitySystem();
        if ( pAbilitySystem == nullptr || pAbilitySystem->hasMatchingTag( getDeadTag() ) )
            return;

        // 순서: 태그(그 뒤로 발동 · 피해가 막힌다) → 어빌리티 취소 → 컴포넌트 안의 반응(이벤트) → 바깥(채널).
        pAbilitySystem->addLooseTag( getDeadTag() );
        pAbilitySystem->cancelAllAbilities();

        GameObject*      pOwner     = pAbilitySystem->getOwner();
        GameObjectHandle target     = pOwner != nullptr ? pOwner->getHandle() : GameObjectHandle{};
        GameObjectHandle instigator = data._pSpec != nullptr ? data._pSpec->_context._instigator : GameObjectHandle{};

        GameplayEventData deathEvent;
        deathEvent._eventTag   = getDeathEventTag();
        deathEvent._instigator = instigator;
        deathEvent._target     = target;
        (void)pAbilitySystem->handleGameplayEvent( deathEvent._eventTag, deathEvent ); // 죽음 반응 어빌리티가 없어도 된다

        AbilityOwnerDiedEvent diedEvent;
        diedEvent._target     = target;
        diedEvent._instigator = instigator;
        GameEventUtil::send( diedEvent );
    }
} // namespace sw
