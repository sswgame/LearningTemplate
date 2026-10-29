/**
 * @file CombatAttributeSet.h
 * @brief 전투 게임이 흔히 쓰는 어트리뷰트 세트와 그 규칙입니다. 체력, 마나, 공격력, 방어, 이동 속도가 들어 있습니다.
 */
#pragma once
#include "Core/String/TagID.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Gameplay/Ability/AttributeSet.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) 이름. 어트리뷰트, 태그, 이벤트 이름을 한곳에 모은다
    // ------------------------------------------------------------------------------
    /**
     * @struct CombatAttributes
     * @brief `CombatAttributeSet` 의 어트리뷰트 이름입니다. 데이터(XML)도 같은 철자를 씁니다.
     * @details `IncomingDamage` 와 `IncomingHealing` 은 **메타 어트리뷰트**이다. 이펙트가 여기에 값을 더하면 세트가 즉시 그 값을 체력으로 옮기고
     *          0 으로 되돌린다(언리얼 Lyra 의 `Damage`, `Healing` 에 해당). 그래서 피해 공식(`DamageExecution`)은 체력을 직접 깎지 않는다.
     *          죽음, 피해 숫자, 무적 같은 규칙은 체력을 바꾸는 한 곳(`CombatAttributeSet::postGameplayEffectExecute`)에 모인다.
     */
    struct SW_GF_API CombatAttributes
    {
        static const hashed_string& health();
        static const hashed_string& maxHealth();
        static const hashed_string& mana();
        static const hashed_string& maxMana();
        static const hashed_string& attackPower();
        static const hashed_string& armor();
        static const hashed_string& moveSpeed();
        static const hashed_string& incomingDamage();
        static const hashed_string& incomingHealing();
    };
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // 2) CombatAttributeSet
    // ------------------------------------------------------------------------------
    /**
     * @class CombatAttributeSet
     * @brief 체력, 마나, 공격력, 방어, 이동 속도와 메타 어트리뷰트 두 개를 가진 어트리뷰트 세트입니다.
     * @details 규칙은 다음과 같다.
     *          - 체력은 [0, 최대 체력], 마나는 [0, 최대 마나] 범위에 머물고, 이동 속도와 최대치는 0 이상이다. 최대치가 줄면 현재치도 따라서 줄인다.
     *          - `IncomingDamage` 를 받으면 체력에서 뺀다. 체력이 0 에 닿은 **처음 한 번만** 주인에게 `State.Dead` 태그를 붙이고 모든 어빌리티를
     *            취소한 뒤, `Event.Death` 게임플레이 이벤트와 `AbilityOwnerDiedEvent`("game" 채널)를 낸다. 쓰러진 뒤의 피해와 회복은 무시한다.
     *          - 맞을 때마다 주인에게 `Event.Hit` 게임플레이 이벤트를 보낸다. 이벤트의 크기는 깎인 체력이고, 피격 반응 어빌리티가 이 이벤트로 발동한다.
     *          기본값은 체력과 최대 체력 100, 마나와 최대 마나 100, 공격력 10, 방어 0, 이동 속도 5 이다. 카탈로그 XML 이 이 값을 덮어쓴다.
     */
    REFLECT( Abstract )
    class SW_GF_API CombatAttributeSet : public AttributeSet
    {
    public:
        REFLECT_BODY();

        const TypeInfo* getTypeInfo() const override { return StaticType(); }
        CombatAttributeSet();

        /** @brief 체력이 0 에 닿을 때 붙는 태그("State.Dead")입니다. */
        static TagID getDeadTag();
        /** @brief 체력이 0 에 닿을 때 보내는 게임플레이 이벤트 태그("Event.Death")입니다. */
        static TagID getDeathEventTag();
        /** @brief 피해를 받을 때마다 보내는 게임플레이 이벤트 태그("Event.Hit")입니다. */
        static TagID getHitEventTag();

        void preAttributeChange( const hashed_string& name, float32& inoutNewValue ) override;
        void preAttributeBaseChange( const hashed_string& name, float32& inoutNewBase ) override;
        void postAttributeChange( const hashed_string& name, float32 oldValue, float32 newValue ) override;
        void postGameplayEffectExecute( const AttributeModCallbackData& data ) override;

    private:
        /** @brief 체력과 마나를 최대치 안으로, 최대치와 이동 속도를 0 이상으로 자릅니다. current 와 base 에 똑같이 적용합니다. */
        void clampCombatValue( const hashed_string& name, float32& inoutValue ) const;
        /** @brief 메타 피해를 체력으로 옮깁니다. */
        void applyIncomingDamage( const AttributeModCallbackData& data );
        /** @brief 메타 회복을 체력으로 옮깁니다. */
        void applyIncomingHealing( const AttributeModCallbackData& data );
        /** @brief 체력이 0 에 닿은 처음 한 번을 처리합니다. 태그를 붙이고, 어빌리티를 취소하고, 이벤트를 냅니다. */
        void handleOutOfHealth( const AttributeModCallbackData& data );
    };
} // namespace sw
