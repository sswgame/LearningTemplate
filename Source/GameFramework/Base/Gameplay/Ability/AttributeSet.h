/**
 * @file AttributeSet.h
 * @brief 어트리뷰트(숫자 상태) 세트와 그 변경 훅입니다(언리얼의 `UAttributeSet` 에 해당).
 */
#pragma once
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Gameplay/Ability/AbilityTypes.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct AbilityEffectSpec;

    class AbilityComponent;

    // ------------------------------------------------------------------------------
    // 1) 값. base 와 current 로 구성한다(언리얼의 `FGameplayAttributeData` 에 해당)
    // ------------------------------------------------------------------------------
    /**
     * @brief 어트리뷰트 하나의 값입니다.
     * @details **base** 는 즉시 이펙트나 코드가 영구히 바꾸는 값이고, **current** 는 base 에 지금 적용된 지속 이펙트를 모두 반영한 값이다.
     *          게임 규칙(이동 속도, 피해 공식)은 current 를 읽는다. 버프가 해제되면 current 는 base 로 돌아온다.
     */
    struct AttributeData
    {
        float32 _baseValue{ 0.0f };    ///< 영구 값(즉시 이펙트가 쓴다)
        float32 _currentValue{ 0.0f }; ///< base 에 지속 이펙트를 반영한 값(게임 규칙이 읽는 값)
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 즉시 이펙트나 주기 이펙트가 어트리뷰트 하나의 base 를 바꾼 직후에 훅으로 넘기는 정보입니다(언리얼의 `FGameplayEffectModCallbackData` 에 해당).
     * @details 메타 어트리뷰트를 처리할 때 쓴다. 메타 어트리뷰트는 `IncomingDamage` 처럼 받자마자 다른 어트리뷰트로 옮기는 값이다.
     */
    struct AttributeModCallbackData
    {
        const AbilityEffectSpec* _pSpec{ nullptr };   ///< 실행한 스펙(쏜 쪽, 레벨, SetByCaller)
        AbilityComponent*        _pTarget{ nullptr }; ///< 바뀐 쪽
        hashed_string            _attribute{};        ///< 바뀐 어트리뷰트
        float32                  _magnitude{ 0.0f };  ///< 적용한 크기(스택을 곱한 값)
        float32                  _deltaBase{ 0.0f };  ///< base 가 실제로 바뀐 양(클램프 뒤)
        AttributeModOp           _op{ AttributeModOp::Add };
    };
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // 2) AttributeSet. 어트리뷰트 세트와 변경 훅
    // ------------------------------------------------------------------------------
    /**
     * @class AttributeSet
     * @brief 이름(`hashed_string`)으로 찾는 어트리뷰트 세트입니다. 클램프나 메타 어트리뷰트 같은 규칙은 파생 클래스가 훅으로 구현합니다.
     * @details 언리얼에서는 `UPROPERTY` 필드 하나가 어트리뷰트 하나이다. 여기서는 **어트리뷰트의 개수와 이름을 코드가 정하지 않는다**(키트 규칙, README 의 "개수를 코드가 정하지 않는다").
     *          데이터(`AbilityCatalog` XML 의 `<AttributeSet>`)가 어트리뷰트를 추가하고, 규칙이 필요한 세트만 파생 클래스를 만든다(`CombatAttributeSet`).
     *          이름별 범위(`setAttributeRange`)만 있으면 되는 세트는 이 클래스를 그대로 쓴다.
     *
     *          값을 바꾸는 경로는 **`AbilityComponent` 하나**뿐이다. 훅, 집계, 델리게이트가 그 안에서 차례로 실행된다. 그래서 값 쓰기는
     *          공개하지 않는다(`AbilityComponent::setAttributeBaseValue` 를 쓴다).
     */
    REFLECT( Abstract )
    class SW_GF_API AttributeSet
    {
        friend class AbilityComponent;

    public:
        REFLECT_BODY();

        AttributeSet();
        virtual ~AttributeSet() = default;

        /** @brief 동적 리플렉션 타입입니다. 파생 클래스는 `REFLECT( Abstract )` 를 붙이고 자기 `StaticType()` 을 반환하도록 재정의합니다(모듈을 내릴 때 타입의 모듈 이름으로 고른다). */
        virtual const TypeInfo* getTypeInfo() const;

        AttributeSet( const AttributeSet& )            = delete;
        AttributeSet& operator=( const AttributeSet& ) = delete;

        /**
         * @brief 어트리뷰트를 추가합니다. 이미 있으면 base 와 current 를 @p baseValue 로 되돌립니다.
         * @details 컴포넌트에 붙이기 **전에** 부른다. 붙인 뒤에 추가할 때는 `AbilityComponent::defineAttribute` 를 쓴다. 이 함수는 집계와 알림을 거친다.
         */
        void defineAttribute( const hashed_string& name, float32 baseValue );
        /** @brief 어트리뷰트 값이 머물 범위를 정합니다. 기본 훅(`preAttributeChange`, `preAttributeBaseChange`)이 이 범위로 값을 자릅니다. */
        void setAttributeRange( const hashed_string& name, float32 minValue, float32 maxValue );
        /** @brief 어트리뷰트가 있으면 true 입니다. */
        bool hasAttribute( const hashed_string& name ) const;
        /** @brief 어트리뷰트 값을 찾습니다. 없으면 nullptr 입니다. */
        const AttributeData* findAttribute( const hashed_string& name ) const;
        /** @brief 추가한 순서대로 나열한 어트리뷰트 이름입니다. UI 와 로그가 같은 순서로 보여 줍니다. */
        const vector<hashed_string>& getAttributeNames() const { return _listAttributeName; }
        /** @brief 이 세트가 붙은 컴포넌트입니다. 붙기 전이면 nullptr 입니다. */
        AbilityComponent* getOwningAbilitySystem() const { return _pOwningAbilitySystem; }

        /**
         * @brief current 값이 바뀌기 직전에 불립니다. @p inoutNewValue 를 고쳐 클램프합니다(언리얼의 `PreAttributeChange` 에 해당).
         * @details 기본 구현은 `setAttributeRange` 의 범위로 자른다. 집계 도중에 불리므로 이 훅에서 다른 어트리뷰트를 바꾸지 않는다.
         */
        virtual void preAttributeChange( const hashed_string& name, float32& inoutNewValue );
        /** @brief base 값이 바뀌기 직전에 불립니다(언리얼의 `PreAttributeBaseChange` 에 해당). 기본 구현은 `preAttributeChange` 와 같은 범위로 자릅니다. */
        virtual void preAttributeBaseChange( const hashed_string& name, float32& inoutNewBase );
        /**
         * @brief current 값이 바뀐 직후에 불립니다(언리얼의 `PostAttributeChange` 에 해당). 여기서는 다른 어트리뷰트를 바꿔도 됩니다.
         * @details 최대치가 줄면 현재치도 따라서 줄이는 규칙(`CombatAttributeSet`)이 이 훅에 있다.
         */
        virtual void postAttributeChange( const hashed_string& name, float32 oldValue, float32 newValue );
        /**
         * @brief 즉시 이펙트나 주기 이펙트가 base 를 바꾼 직후에 불립니다(언리얼의 `PostGameplayEffectExecute` 에 해당). 메타 어트리뷰트를 다른 어트리뷰트로 옮길 때 씁니다.
         * @details 여기서 다른 어트리뷰트를 바꿔도 된다(`getOwningAbilitySystem()->setAttributeBaseValue`).
         */
        virtual void postGameplayEffectExecute( const AttributeModCallbackData& data );

    private:
        /** @brief 쓸 수 있는 값을 찾습니다. 없으면 nullptr 입니다. 값을 쓰는 쪽은 `AbilityComponent` 뿐입니다. */
        AttributeData* findAttributeMutable( const hashed_string& name );
        /** @brief 붙은 컴포넌트를 기록합니다(`AbilityComponent::addAttributeSet` 이 부름). */
        void setOwningAbilitySystem( AbilityComponent* pAbilitySystem ) { _pOwningAbilitySystem = pAbilitySystem; }
        /** @brief @p name 의 범위로 @p inoutValue 를 자릅니다. 범위가 없으면 값을 그대로 둡니다. */
        void clampToRange( const hashed_string& name, float32& inoutValue ) const;

        /** @brief 어트리뷰트 하나의 값이 머물 범위입니다. */
        struct AttributeRange
        {
            float32 _minValue;
            float32 _maxValue;
        };

        unordered_map<hashed_string, AttributeData>  _mapAttribute;
        unordered_map<hashed_string, AttributeRange> _mapRange;
        vector<hashed_string>                        _listAttributeName;
        AbilityComponent*                            _pOwningAbilitySystem;
    };
} // namespace sw
