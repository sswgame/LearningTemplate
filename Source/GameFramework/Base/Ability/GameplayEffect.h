/**
 * @file GameplayEffect.h
 * @brief 게임플레이 이펙트 — 정의(데이터) · 스펙(적용 한 번의 인스턴스) · 실행 계산입니다(언리얼 `UGameplayEffect` · `FGameplayEffectSpec`).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"

#include "Engine/Object/Component/TagSystem.h"

#include "GameFramework/Base/Ability/AbilitySystemTypes.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class AbilitySystemComponent;
    class IGameplayEffectExecution;

    // ------------------------------------------------------------------------------
    // 1) 모디파이어 — 어트리뷰트 하나를 어떻게 바꿀지(언리얼 `FGameplayModifierInfo`)
    // ------------------------------------------------------------------------------
    /**
     * @brief 이펙트가 어트리뷰트 하나에 거는 변경입니다.
     * @details 크기는 `_magnitudeSource` 가 정합니다.
     *          - `ScalableFloat`: `_scalableMagnitude.compute( 레벨 )`
     *          - `AttributeBased`: `(어트리뷰트 + _preMultiplyAdditive) × _coefficient + _postMultiplyAdditive`. 어트리뷰트는 `_bFromSource` 면 쏜 쪽
     *            (스펙을 만든 순간의 값), 아니면 맞는 쪽(적용하는 순간의 값)입니다. 언리얼 `FAttributeBasedFloat` 의 식 그대로입니다.
     *          - `SetByCaller`: 스펙에 코드가 넣은 수(`_setByCallerName`). 없으면 0 이고 경고합니다.
     */
    struct GameplayEffectModifier
    {
        hashed_string         _attribute{};               ///< 바꿀 어트리뷰트
        AttributeModOp        _op{ AttributeModOp::Add }; ///< 바꾸는 방식
        EffectMagnitudeSource _magnitudeSource{ EffectMagnitudeSource::ScalableFloat };
        ScalableFloat         _scalableMagnitude{};          ///< `ScalableFloat` 일 때의 크기
        hashed_string         _backingAttribute{};           ///< `AttributeBased` 일 때 읽을 어트리뷰트
        float32               _coefficient{ 1.0f };          ///< `AttributeBased` 곱
        float32               _preMultiplyAdditive{ 0.0f };  ///< `AttributeBased` 곱하기 전 더하기
        float32               _postMultiplyAdditive{ 0.0f }; ///< `AttributeBased` 곱한 뒤 더하기
        hashed_string         _setByCallerName{};            ///< `SetByCaller` 일 때의 이름
        uint8                 _bFromSource{ SW_TRUE };       ///< `AttributeBased` 가 쏜 쪽을 읽는지(아니면 맞는 쪽)
    };
} // namespace sw

namespace sw
{
    /** @brief 크기까지 계산을 마친 변경 하나입니다 — 실행 계산(`IGameplayEffectExecution`)의 출력이자 활성 이펙트가 들고 있는 값입니다. */
    struct EvaluatedModifier
    {
        hashed_string  _attribute{};
        AttributeModOp _op{ AttributeModOp::Add };
        float32        _magnitude{ 0.0f }; ///< 스택 하나의 크기
    };
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // 2) 정의 — 데이터(언리얼 `UGameplayEffect`). 스펙이 공유 포인터로 붙든다
    // ------------------------------------------------------------------------------
    /**
     * @brief 이펙트 하나의 정의입니다. 만든 뒤에는 바꾸지 않습니다 — 스펙 · 활성 이펙트가 `shared_ptr<const ...>` 로 함께 씁니다.
     * @details 태그는 다섯 가지입니다(언리얼과 같은 뜻).
     *          - `_assetTags`: 이 이펙트 **자체**를 설명합니다("Effect.Damage.Fire"). `removeActiveEffectsWithTags` 가 이것으로 고릅니다.
     *          - `_grantedTags`: 걸려 있는 동안 **대상에게** 붙습니다("State.Burning", 쿨다운이면 "Cooldown.Fireball").
     *          - `_applicationRequiredTags` · `_applicationBlockedTags`: 대상이 이것을 모두 가져야 · 하나라도 가지면 안 걸립니다(면역).
     *          - `_removeEffectsWithTags`: 걸리는 순간 대상의 활성 이펙트 중 에셋 · 부여 태그가 이것에 맞는 것을 지웁니다(정화).
     *          `_cueTags` 는 연출입니다(`GameplayCueEvent`).
     */
    struct SW_GF_API GameplayEffectDef
    {
        hashed_string                                      _id{}; ///< 카탈로그 id("GE_Fireball_Damage")
        EffectDurationPolicy                               _durationPolicy{ EffectDurationPolicy::Instant };
        ScalableFloat                                      _duration{};                               ///< `HasDuration` 의 길이(초 — 턴제는 턴)
        float32                                            _period{ 0.0f };                           ///< 0 보다 크면 그 주기마다 모디파이어를 즉시처럼 실행한다(도트 · 리젠)
        uint8                                              _bExecutePeriodicOnApplication{ SW_TRUE }; ///< 주기 이펙트가 걸리는 순간 한 번 실행할지
        EffectStackingPolicy                               _stackingPolicy{ EffectStackingPolicy::None };
        EffectStackExpirationPolicy                        _stackExpirationPolicy{ EffectStackExpirationPolicy::ClearEntireStack };
        uint8                                              _bRefreshDurationOnStack{ SW_TRUE }; ///< 스택이 오를 때 시간을 다시 채울지
        int32                                              _stackLimit{ 1 };                    ///< `AggregateByTarget` 의 상한(1 이상)
        vector<GameplayEffectModifier>                     _listModifier{};
        vector<shared_ptr<const IGameplayEffectExecution>> _listExecution{}; ///< 즉시 · 주기 실행에서만 돈다(언리얼과 같다)
        TagContainer                                       _assetTags{};
        TagContainer                                       _grantedTags{};
        TagContainer                                       _applicationRequiredTags{};
        TagContainer                                       _applicationBlockedTags{};
        TagContainer                                       _removeEffectsWithTags{};
        TagContainer                                       _cueTags{};

        /** @brief 주기 이펙트인지입니다(즉시 이펙트는 주기를 갖지 않는다). */
        bool isPeriodic() const { return _period > 0.0f && _durationPolicy != EffectDurationPolicy::Instant; }
    };
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // 3) 컨텍스트 · 스펙 — 적용 한 번(언리얼 `FGameplayEffectContext` · `FGameplayEffectSpec`)
    // ------------------------------------------------------------------------------
    /** @brief 누가 무엇으로 걸었는지입니다. 핸들이라 프레임을 넘겨 들어도 됩니다. */
    struct GameplayEffectContext
    {
        GameObjectHandle _instigator{};   ///< 건 쪽(어빌리티의 주인)
        GameObjectHandle _effectCauser{}; ///< 실제로 맞힌 것(투사체 · 함정). 모르면 건 쪽과 같다
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 이펙트를 **한 번 적용할** 묶음입니다 — 정의 + 레벨 + 컨텍스트 + 코드가 넣은 수 + 쏜 쪽 어트리뷰트 스냅샷.
     * @details `AbilitySystemComponent::makeOutgoingSpec` 이 만듭니다. 그 순간 쏜 쪽 어트리뷰트를 통째로 찍어 두므로(`_mapSourceAttribute`),
     *          투사체가 날아가는 동안 쏜 쪽이 죽거나 사라져도 피해 공식은 쏜 순간의 공격력을 씁니다(언리얼의 Source 캡처 · 스냅샷).
     *          값 타입이라 복사해 여러 대상에 걸어도 됩니다.
     */
    struct SW_GF_API GameplayEffectSpec
    {
        shared_ptr<const GameplayEffectDef>   _pDef{};
        GameplayEffectContext                 _context{};
        unordered_map<hashed_string, float32> _mapSetByCaller{};     ///< 코드가 이름으로 넣은 수
        unordered_map<hashed_string, float32> _mapSourceAttribute{}; ///< 만든 순간 쏜 쪽 어트리뷰트(current)
        TagContainer                          _dynamicGrantedTags{}; ///< 정의의 부여 태그에 더할 것(이 스펙만)
        int32                                 _level{ 1 };

        /** @brief 정의가 있으면 true 입니다. */
        bool isValid() const { return _pDef != nullptr; }
        /** @brief 이 스펙이 걸릴 시간입니다(`HasDuration` 이 아니면 0). */
        float32 computeDuration() const;
        /** @brief SetByCaller 수를 넣습니다(같은 이름이면 덮어쓴다). */
        void setSetByCallerMagnitude( const hashed_string& name, float32 magnitude ) { _mapSetByCaller.insert_or_assign( name, magnitude ); }
        /** @brief SetByCaller 수를 찾습니다. 없으면 @p fallback 입니다. */
        float32 getSetByCallerMagnitude( const hashed_string& name, float32 fallback = 0.0f ) const;
        /** @brief 쏜 쪽 어트리뷰트 스냅샷을 찾습니다. 없으면 false 이고 @p outValue 는 그대로입니다. */
        bool findSourceAttribute( const hashed_string& name, float32& outValue ) const;
    };
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // 4) 실행 계산 — 여러 어트리뷰트를 읽어 결과를 내는 공식(언리얼 `UGameplayEffectExecutionCalculation`)
    // ------------------------------------------------------------------------------
    /** @brief 실행 계산이 읽는 것입니다 — 스펙(쏜 쪽 스냅샷 포함)과 맞는 쪽 컴포넌트. */
    struct GameplayEffectExecutionParams
    {
        const GameplayEffectSpec*     _pSpec{ nullptr };
        const AbilitySystemComponent* _pTarget{ nullptr };
        int32                         _stackCount{ 1 };
    };
} // namespace sw

namespace sw
{
    /**
     * @class IGameplayEffectExecution
     * @brief 즉시 · 주기 이펙트가 실행될 때 도는 공식입니다. 결과는 base 에 쓰일 모디파이어 목록입니다.
     * @details 상태를 갖지 않습니다 — 정의들이 한 인스턴스를 함께 씁니다(`shared_ptr<const ...>`). 데이터에서 매개변수를 받는 공식은
     *          카탈로그가 만들 때 넣습니다(`DamageExecution` 의 SetByCaller 이름 · 계수).
     */
    class SW_GF_API IGameplayEffectExecution
    {
    public:
        IGameplayEffectExecution()          = default;
        virtual ~IGameplayEffectExecution() = default;

        IGameplayEffectExecution( const IGameplayEffectExecution& )            = delete;
        IGameplayEffectExecution& operator=( const IGameplayEffectExecution& ) = delete;

        /** @brief 공식을 돌려 base 에 쓸 변경을 @p outListModifier 에 더합니다. 크기는 스택 하나 몫입니다(호출부가 스택 수를 곱한다). */
        virtual void execute( const GameplayEffectExecutionParams& params, vector<EvaluatedModifier>& outListModifier ) const = 0;
    };
} // namespace sw

namespace sw
{
    /**
     * @class DamageExecution
     * @brief 기본 피해 공식입니다 — `원피해 × 100 ÷ (100 + 방어)` 를 맞는 쪽의 `IncomingDamage` 에 더합니다.
     * @details 원피해는 `SetByCaller( _damageName )` + `쏜 쪽 공격력 × _attackPowerCoefficient` + `_baseDamage` 입니다. 방어는 맞는 쪽의
     *          `Armor`(current)이고 음수는 0 으로 봅니다. 결과가 0 이하면 아무것도 내지 않습니다. 이름은 `CombatAttributeSet` 의 것을 씁니다 —
     *          다른 어트리뷰트 이름으로 같은 공식을 쓰려면 이 클래스를 본떠 하나 더 만듭니다.
     */
    class SW_GF_API DamageExecution final : public IGameplayEffectExecution
    {
    public:
        DamageExecution();

        void execute( const GameplayEffectExecutionParams& params, vector<EvaluatedModifier>& outListModifier ) const override;

        /** @brief 공식의 매개변수를 정합니다(카탈로그 XML `<Execution class="Damage" setByCaller="Damage" attackPowerCoefficient="1" base="0"/>`). */
        void setParameters( const hashed_string& damageName, float32 attackPowerCoefficient, float32 baseDamage );

        const hashed_string& getDamageName() const { return _damageName; }
        float32              getAttackPowerCoefficient() const { return _attackPowerCoefficient; }
        float32              getBaseDamage() const { return _baseDamage; }

    private:
        hashed_string _damageName;
        float32       _attackPowerCoefficient;
        float32       _baseDamage;
    };
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // 5) 활성 이펙트 — 대상에 걸려 있는 지속 · 무한 이펙트 하나(언리얼 `FActiveGameplayEffect`)
    // ------------------------------------------------------------------------------
    /**
     * @brief 대상에 걸려 있는 이펙트 하나입니다. `AbilitySystemComponent` 가 들고 시간을 돌립니다.
     * @details 지속 모디파이어는 걸린 순간 크기를 계산해 `_listEvaluatedModifier` 에 둡니다(스냅샷). 현재값 집계는 이 목록 × 스택 수입니다.
     */
    struct ActiveGameplayEffect
    {
        ActiveEffectHandle        _handle{};
        GameplayEffectSpec        _spec{};
        vector<EvaluatedModifier> _listEvaluatedModifier{};
        TagContainer              _grantedTags{};         ///< 실제로 대상에 붙인 태그(정의 + 스펙의 동적 태그)
        float32                   _duration{ 0.0f };      ///< 걸릴 시간(무한이면 0)
        float32                   _remainingTime{ 0.0f }; ///< 남은 시간(무한이면 의미 없음)
        float32                   _periodTimer{ 0.0f };   ///< 다음 주기 실행까지 남은 시간
        float32                   _startTime{ 0.0f };     ///< 걸린 순간의 컴포넌트 시각
        int32                     _stackCount{ 1 };
        uint8                     _bPendingRemove{ SW_FALSE }; ///< 지우는 중(알림 도중 다시 지우지 않는다)
    };
} // namespace sw
