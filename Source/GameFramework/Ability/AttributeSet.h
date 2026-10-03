/**
 * @file AttributeSet.h
 * @brief 어트리뷰트(숫자 상태) 묶음과 그 변경 훅입니다(언리얼 `UAttributeSet`).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Ability/AbilitySystemTypes.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct GameplayEffectSpec;

    class AbilitySystemComponent;

    // ------------------------------------------------------------------------------
    // 1) 값 — base 와 current(언리얼 `FGameplayAttributeData`)
    // ------------------------------------------------------------------------------
    /**
     * @brief 어트리뷰트 하나의 값입니다.
     * @details **base** 는 즉시 이펙트 · 코드가 영구히 바꾸는 값이고, **current** 는 base 에 지금 걸린 지속 이펙트를 모두 더한 값입니다.
     *          게임 규칙(이동 속도 · 피해 공식)은 current 를 읽습니다. 버프가 풀리면 current 가 base 로 돌아옵니다.
     */
    struct AttributeData
    {
        float32 _baseValue{ 0.0f };    ///< 영구 값(즉시 이펙트가 쓴다)
        float32 _currentValue{ 0.0f }; ///< base + 지속 이펙트(읽는 값)
    };

    /**
     * @brief 즉시 · 주기 이펙트가 어트리뷰트 하나의 base 를 바꾼 직후 넘기는 정보입니다(언리얼 `FGameplayEffectModCallbackData`).
     * @details 메타 어트리뷰트(`IncomingDamage` 처럼 받자마자 다른 어트리뷰트로 옮기는 값)를 처리하는 자리입니다.
     */
    struct AttributeModCallbackData
    {
        const GameplayEffectSpec* _pSpec{ nullptr };   ///< 실행한 스펙(쏜 쪽 · 레벨 · SetByCaller)
        AbilitySystemComponent*   _pTarget{ nullptr }; ///< 바뀐 쪽
        hashed_string             _attribute{};        ///< 바뀐 어트리뷰트
        float32                   _magnitude{ 0.0f };  ///< 적용한 크기(스택을 곱한 값)
        float32                   _deltaBase{ 0.0f };  ///< base 가 실제로 바뀐 양(클램프 뒤)
        AttributeModOp            _op{ AttributeModOp::Add };
    };

    // ------------------------------------------------------------------------------
    // 2) AttributeSet — 어트리뷰트 묶음 + 변경 훅
    // ------------------------------------------------------------------------------
    /**
     * @class AttributeSet
     * @brief 이름(`hashed_string`)으로 찾는 어트리뷰트 묶음입니다. 규칙(클램프 · 메타 어트리뷰트)은 파생 클래스가 훅으로 둡니다.
     * @details 언리얼은 `UPROPERTY` 필드 하나가 어트리뷰트 하나입니다. 여기서는 **개수와 이름을 코드가 정하지 않습니다**(키트 규칙 — README
     *          "개수를 코드가 정하지 않는다"): 데이터(`AbilityCatalog` XML 의 `<AttributeSet>`)가 어트리뷰트를 더하고, 규칙이 필요한 묶음만
     *          파생 클래스를 만듭니다(`CombatAttributeSet`). 이름 범위(`setAttributeRange`)만 있으면 되는 묶음은 이 클래스 그대로 씁니다.
     *
     *          값을 바꾸는 길은 **`AbilitySystemComponent` 하나**입니다 — 훅 · 집계 · 델리게이트가 그 안에서 차례로 돕니다. 그래서 값 쓰기는
     *          공개하지 않습니다(`AbilitySystemComponent::setAttributeBaseValue`).
     */
    class SW_GF_API AttributeSet
    {
        friend class AbilitySystemComponent;

    public:
        AttributeSet();
        virtual ~AttributeSet() = default;

        AttributeSet( const AttributeSet& )            = delete;
        AttributeSet& operator=( const AttributeSet& ) = delete;

        /**
         * @brief 어트리뷰트를 더합니다. 이미 있으면 base · current 를 @p baseValue 로 되돌립니다.
         * @details 컴포넌트에 붙이기 **전에** 부릅니다. 붙인 뒤에 더하면 `AbilitySystemComponent::defineAttribute` 를 쓰십시오(집계 · 알림을 탑니다).
         */
        void defineAttribute( const hashed_string& name, float32 baseValue );
        /** @brief 어트리뷰트 값이 머물 범위입니다. 기본 훅(`preAttributeChange` · `preAttributeBaseChange`)이 이 범위로 자릅니다. */
        void setAttributeRange( const hashed_string& name, float32 minValue, float32 maxValue );
        /** @brief 어트리뷰트를 가졌으면 true 입니다. */
        bool hasAttribute( const hashed_string& name ) const;
        /** @brief 어트리뷰트 값을 찾습니다. 없으면 nullptr 입니다. */
        const AttributeData* findAttribute( const hashed_string& name ) const;
        /** @brief 더한 순서대로의 어트리뷰트 이름입니다(UI · 로그가 같은 순서로 보인다). */
        const vector<hashed_string>& getAttributeNames() const { return _listAttributeName; }
        /** @brief 이 묶음이 붙은 컴포넌트입니다. 붙기 전이면 nullptr 입니다. */
        AbilitySystemComponent* getOwningAbilitySystem() const { return _pOwningAbilitySystem; }

        /**
         * @brief current 값이 바뀌기 직전입니다. @p inoutNewValue 를 고쳐 클램프합니다(언리얼 `PreAttributeChange`).
         * @details 기본은 `setAttributeRange` 의 범위로 자릅니다. 이 훅에서는 다른 어트리뷰트를 바꾸지 않습니다 — 집계 도중입니다.
         */
        virtual void preAttributeChange( const hashed_string& name, float32& inoutNewValue );
        /** @brief base 값이 바뀌기 직전입니다(언리얼 `PreAttributeBaseChange`). 기본은 `preAttributeChange` 와 같은 범위로 자릅니다. */
        virtual void preAttributeBaseChange( const hashed_string& name, float32& inoutNewBase );
        /**
         * @brief current 값이 바뀐 직후입니다(언리얼 `PostAttributeChange`). 여기서는 다른 어트리뷰트를 바꿔도 됩니다.
         * @details 최대치가 줄면 현재치를 따라 줄이는 규칙(`CombatAttributeSet`)이 여기 있습니다.
         */
        virtual void postAttributeChange( const hashed_string& name, float32 oldValue, float32 newValue );
        /**
         * @brief 즉시 · 주기 이펙트가 base 를 바꾼 직후입니다(언리얼 `PostGameplayEffectExecute`). 메타 어트리뷰트를 옮기는 자리입니다.
         * @details 여기서 다른 어트리뷰트를 바꿔도 됩니다(`getOwningAbilitySystem()->setAttributeBaseValue`).
         */
        virtual void postGameplayEffectExecute( const AttributeModCallbackData& data );

    private:
        /** @brief 쓸 수 있는 값을 찾습니다. 없으면 nullptr 입니다. 쓰는 쪽은 `AbilitySystemComponent` 하나입니다. */
        AttributeData* findAttributeMutable( const hashed_string& name );
        /** @brief 붙은 컴포넌트를 적습니다(`AbilitySystemComponent::addAttributeSet`). */
        void setOwningAbilitySystem( AbilitySystemComponent* pAbilitySystem ) { _pOwningAbilitySystem = pAbilitySystem; }
        /** @brief @p name 의 범위로 @p inoutValue 를 자릅니다. 범위가 없으면 그대로입니다. */
        void clampToRange( const hashed_string& name, float32& inoutValue ) const;

        /** @brief 어트리뷰트 하나가 머물 범위입니다. */
        struct AttributeRange
        {
            float32 _minValue;
            float32 _maxValue;
        };

        unordered_map<hashed_string, AttributeData>  _mapAttribute;
        unordered_map<hashed_string, AttributeRange> _mapRange;
        vector<hashed_string>                        _listAttributeName;
        AbilitySystemComponent*                      _pOwningAbilitySystem;
    };
} // namespace sw
