/**
 * @file GameplayAbility.h
 * @brief 게임플레이 어빌리티 — 발동 조건(태그 · 비용 · 쿨다운)을 지나 실행되는 행동입니다(언리얼 `UGameplayAbility`).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"

#include "Engine/Object/Component/TagSystem.h"

#include "GameFramework/Ability/AbilitySystemEvents.h"
#include "GameFramework/Ability/AbilitySystemTypes.h"
#include "GameFramework/Ability/GameplayEffect.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class AbilitySystemComponent;
    class AbilityTask;
    class GameObject;

    // ------------------------------------------------------------------------------
    // 1) 설정 — 어빌리티 클래스 기본값(언리얼 `UGameplayAbility` 의 CDO 프로퍼티)
    // ------------------------------------------------------------------------------
    /**
     * @brief 어빌리티 하나의 설정입니다. 카탈로그 XML 의 `<Ability>` 가 채우거나, 코드가 생성자에서 채웁니다.
     * @details 태그 여섯은 언리얼과 같은 뜻입니다.
     *          - `_abilityTags`: 이 어빌리티를 설명합니다("Ability.Skill.Fireball"). 다른 어빌리티의 취소 · 막기 · `tryActivateAbilitiesByTag` 가 이것으로 고릅니다.
     *          - `_cancelAbilitiesWithTags`: 발동하는 순간 이 태그를 가진 다른 활성 어빌리티를 취소합니다.
     *          - `_blockAbilitiesWithTags`: 도는 동안 이 태그를 가진 어빌리티는 발동하지 못합니다.
     *          - `_activationOwnedTags`: 도는 동안 주인에게 붙습니다("State.Casting").
     *          - `_activationRequiredTags` · `_activationBlockedTags`: 주인이 이것을 모두 가져야 · 하나라도 가지면 발동하지 못합니다("State.Dead" · "State.Stunned").
     *          `_triggerEventTags` 의 게임플레이 이벤트가 오면 스스로 발동합니다(피격 반응 · 콤보).
     */
    struct SW_GF_API GameplayAbilityConfig
    {
        hashed_string                               _id{}; ///< 카탈로그 id("GA_Fireball"). 로그 · UI 가 쓴다
        TagContainer                                _abilityTags{};
        TagContainer                                _cancelAbilitiesWithTags{};
        TagContainer                                _blockAbilitiesWithTags{};
        TagContainer                                _activationOwnedTags{};
        TagContainer                                _activationRequiredTags{};
        TagContainer                                _activationBlockedTags{};
        TagContainer                                _triggerEventTags{};
        shared_ptr<const GameplayEffectDef>         _pCostEffect{};                          ///< 발동 비용(마나 −20). `commitAbility` 가 건다
        shared_ptr<const GameplayEffectDef>         _pCooldownEffect{};                      ///< 쿨다운(부여 태그가 쿨다운 태그). `commitAbility` 가 건다
        unordered_map<hashed_string, float32>       _mapParameter{};                         ///< 데이터 매개변수(피해 · 사거리 · 속도)
        unordered_map<hashed_string, hashed_string> _mapNameParameter{};                     ///< 데이터 이름 매개변수(걸 이펙트 id · 프리팹)
        uint8                                       _bRetriggerInstancedAbility{ SW_FALSE }; ///< 도는 중에 다시 발동하면 끝내고 새로 시작할지(아니면 실패)
        uint8                                       _bActivateOnGranted{ SW_FALSE };         ///< 부여되는 순간 발동할지(패시브)
    };

    // ------------------------------------------------------------------------------
    // 2) GameplayAbility
    // ------------------------------------------------------------------------------
    /**
     * @class GameplayAbility
     * @brief 어빌리티 하나입니다 — 부여된 스펙마다 인스턴스 하나(언리얼 `InstancedPerActor`)를 컴포넌트가 들고 있습니다.
     * @details 흐름은 언리얼과 같습니다:
     *          1. `AbilitySystemComponent::tryActivateAbility` 가 조건(태그 · 쿨다운 · 비용 · `canActivateAbility`)을 보고 `activateAbility` 를 부릅니다.
     *          2. 구현은 `commitAbility()` 로 비용 · 쿨다운을 걸고(실패하면 `endAbility()`), 일을 합니다. 바로 끝나면 그 자리에서 `endAbility()`,
     *             시간이 걸리면 작업(`waitDelay` · `waitGameplayEvent`)이나 `onTickAbility` 로 기다렸다가 끝냅니다.
     *          3. 끝나면(`endAbility` · `cancelAbility`) 작업이 모두 정리되고 `onEndAbility( bWasCancelled )` 가 불립니다.
     *
     *          **스레드**: 컴포넌트의 틱(병렬 워커)에서 `onTickAbility` · 작업이 돕니다. 그 안에서 **다른 오브젝트**를 바꾸는 일은
     *          `applyEffectSpecToTarget` 처럼 프레임워크 길로 합니다(틱 중이면 틱 직후로 미룹니다). 오브젝트 생성은 `GameObjectManager::executeOrDeferPostTick` 로 합니다.
     *
     *          **핫 리로드**: 게임 모듈의 어빌리티 인스턴스는 모듈이 내려가기 전에 컴포넌트가 정리합니다(`IModuleCodeHolder`). 다시 올라온 뒤에는
     *          컴포넌트의 `_abilitySetId` 가 다시 부여합니다 — 코드로 준 어빌리티는 게임이 다시 줍니다.
     */
    class SW_GF_API GameplayAbility
    {
        friend class AbilitySystemComponent;

    public:
        GameplayAbility();
        virtual ~GameplayAbility();

        GameplayAbility( const GameplayAbility& )            = delete;
        GameplayAbility& operator=( const GameplayAbility& ) = delete;

        /**
         * @brief 발동 조건을 하나 더 봅니다(언리얼 `CanActivateAbility` 의 파생 부분). 기본은 true 입니다.
         * @details 태그 · 쿨다운 · 비용은 컴포넌트가 이미 봤습니다. 사거리 · 대상 유무처럼 그 밖의 조건만 둡니다. 상태를 바꾸지 않습니다.
         */
        virtual bool canActivateAbility( const GameplayEventData* pTriggerEvent ) const;
        /**
         * @brief 발동했습니다(언리얼 `ActivateAbility`). 구현은 언젠가 `endAbility()` 를 불러야 합니다.
         * @param pTriggerEvent 이벤트로 발동했으면 그 이벤트, 입력 · 코드로 발동했으면 nullptr
         */
        virtual void activateAbility( const GameplayEventData* pTriggerEvent ) = 0;
        /** @brief 끝났습니다(작업은 이미 정리됨). 취소였으면 @p bWasCancelled 입니다. */
        virtual void onEndAbility( bool bWasCancelled );
        /** @brief 도는 동안 매 틱 불립니다(작업 다음). 기본은 아무것도 하지 않습니다. */
        virtual void onTickAbility( float32 deltaTime );
        /** @brief 부여된 직후 불립니다. */
        virtual void onGranted();
        /** @brief 회수되기 직전 불립니다(도는 중이었으면 취소 뒤). */
        virtual void onRemoved();
        /** @brief 이 어빌리티에 묶인 입력이 눌렸습니다. 도는 중일 때만 불립니다(차지 · 콤보). */
        virtual void onInputPressed();
        /** @brief 이 어빌리티에 묶인 입력이 떼어졌습니다. 도는 중일 때만 불립니다(차지 해제). */
        virtual void onInputReleased();

        /**
         * @brief 비용 · 쿨다운을 겁니다(언리얼 `CommitAbility`). 그 사이 비용을 치를 수 없게 됐거나 쿨다운이 걸렸으면 아무것도 걸지 않고 false 입니다.
         * @details 발동과 커밋을 나누는 이유: 차지 · 조준처럼 "발동은 했지만 아직 쓰지 않은" 구간에는 마나 · 쿨다운을 걸지 않습니다.
         */
        bool commitAbility();
        /** @brief 비용을 치를 수 있으면 true 입니다(비용이 없으면 true). */
        bool canAffordCost() const;
        /** @brief 쿨다운 태그가 주인에게 걸려 있으면 true 입니다. */
        bool isOnCooldown() const;
        /** @brief 쿨다운이 남은 시간입니다(없으면 0). */
        float32 getCooldownRemaining() const;
        /** @brief 끝냅니다. 도는 중이 아니면 아무것도 하지 않습니다. */
        void endAbility();
        /** @brief 취소로 끝냅니다(`onEndAbility( true )`). */
        void cancelAbility();

        /** @brief 설정입니다. 부여 뒤에 바꾸면 다음 발동부터 적용됩니다. */
        const GameplayAbilityConfig& getConfig() const { return _config; }
        GameplayAbilityConfig&       getConfig() { return _config; }
        /** @brief 설정을 통째로 바꿉니다(카탈로그가 만들 때). */
        void setConfig( const GameplayAbilityConfig& config ) { _config = config; }
        /** @brief 데이터 매개변수입니다. 없으면 @p fallback 입니다. */
        float32 getParameter( const hashed_string& name, float32 fallback = 0.0f ) const;
        /** @brief 데이터 이름 매개변수입니다. 없으면 빈 이름입니다. */
        hashed_string getNameParameter( const hashed_string& name ) const;

        /** @brief 이 어빌리티를 가진 컴포넌트입니다. 부여 전이면 nullptr 입니다. */
        AbilitySystemComponent* getAbilitySystem() const { return _pAbilitySystem; }
        /** @brief 이 어빌리티를 가진 오브젝트입니다(언리얼의 Avatar). */
        GameObject* getAvatar() const;
        /** @brief 부여된 스펙의 핸들입니다. */
        AbilitySpecHandle getSpecHandle() const { return _specHandle; }
        /** @brief 부여된 레벨입니다. */
        int32 getLevel() const { return _level; }
        /** @brief 도는 중이면 true 입니다. */
        bool isActive() const { return _bActive == SW_TRUE; }
        /** @brief 지금 도는 발동이 이벤트로 시작됐으면 true 이고 그 이벤트는 `getTriggerEventData` 입니다. */
        bool wasTriggeredByEvent() const { return _bTriggeredByEvent == SW_TRUE; }
        /** @brief 지금 도는 발동을 시작한 이벤트입니다(`wasTriggeredByEvent` 가 false 면 빈 값). */
        const GameplayEventData& getTriggerEventData() const { return _triggerEventData; }
        /** @brief 이 어빌리티의 입력이 지금 눌려 있으면 true 입니다. */
        bool isInputPressed() const { return _bInputPressed == SW_TRUE; }

        // --------------------------------------------------------------------------
        // 이펙트 — 레벨 · 컨텍스트가 채워진 스펙을 만들고 건다
        // --------------------------------------------------------------------------
        /** @brief 카탈로그의 이펙트 @p effectId 로 이 어빌리티 레벨의 스펙을 만듭니다. 없으면 빈 스펙입니다(경고). */
        GameplayEffectSpec makeOutgoingSpec( const hashed_string& effectId ) const;
        /** @brief 주인에게 스펙을 겁니다. */
        ActiveEffectHandle applyEffectSpecToOwner( const GameplayEffectSpec& spec );
        /** @brief 대상에게 스펙을 겁니다. 틱 중이면 틱 직후로 미룹니다(`AbilitySystemComponent::applyGameplayEffectSpecToTarget`). */
        ActiveEffectHandle applyEffectSpecToTarget( const GameplayEffectSpec& spec, AbilitySystemComponent* pTarget );

        // --------------------------------------------------------------------------
        // 작업 — 도는 동안만 산다. 끝나면 모두 정리된다
        // --------------------------------------------------------------------------
        /** @brief 작업을 붙이고 시작합니다. 어빌리티가 도는 중이 아니면 붙이지 않고 nullptr 입니다. */
        AbilityTask* addTask( unique_ptr<AbilityTask> pTask );
        /** @brief @p seconds 뒤에 @p onFinished 를 부르는 작업입니다(언리얼 `UAbilityTask_WaitDelay`). */
        AbilityTask* waitDelay( float32 seconds, const Delegate<void()>& onFinished );
        /**
         * @brief 주인에게 @p eventTag(또는 그 하위)의 게임플레이 이벤트가 오면 @p onEvent 를 부르는 작업입니다(언리얼 `UAbilityTask_WaitGameplayEvent`).
         * @param bOnlyTriggerOnce 처음 한 번만 받고 끝낼지
         */
        AbilityTask* waitGameplayEvent( TagID eventTag, const Delegate<void( const GameplayEventData& )>& onEvent, bool bOnlyTriggerOnce );
        /** @brief 살아 있는(끝나지 않은) 작업 수입니다. */
        uint32 getActiveTaskCount() const;

    private:
        /** @brief 컴포넌트가 부여할 때 적습니다. */
        void bindToAbilitySystem( AbilitySystemComponent* pAbilitySystem, AbilitySpecHandle specHandle, int32 level );
        /** @brief 컴포넌트가 발동을 확정한 뒤 부릅니다 — 상태를 세우고 `activateAbility` 를 부릅니다. */
        void beginActivation( const GameplayEventData* pTriggerEvent );
        /** @brief 끝냅니다 — 작업 정리, 컴포넌트에 알림, `onEndAbility`. */
        void finishActivation( bool bWasCancelled );
        /** @brief 작업 · `onTickAbility` 를 돌립니다(컴포넌트 시간 진행). */
        void tickAbility( float32 deltaTime );
        /** @brief 입력 상태를 적고 도는 중이면 알립니다. */
        void setInputPressed( bool bPressed );
        /** @brief 끝난 작업을 지웁니다. 작업을 도는 중이면 미룹니다. */
        void removeFinishedTasks();
        /** @brief 모든 작업을 끝내고 지웁니다(어빌리티가 끝날 때). */
        void endAllTasks();

        GameplayAbilityConfig           _config;
        GameplayEventData               _triggerEventData;
        vector<unique_ptr<AbilityTask>> _listTask;
        AbilitySystemComponent*         _pAbilitySystem;
        AbilitySpecHandle               _specHandle;
        int32                           _level;
        uint32                          _activationSerial; ///< 발동마다 오른다 — 콜백 안에서 끝나고 다시 시작했는지 가린다
        uint32                          _taskTickDepth;    ///< 작업을 도는 중이면 0 보다 크다(그동안 작업을 지우지 않는다)
        uint8                           _bActive           : 1;
        uint8                           _bTriggeredByEvent : 1;
        uint8                           _bInputPressed     : 1;
        uint8                           _bEnding           : 1; ///< 끝내는 중(작업 정리 · 알림 도중 다시 끝내지 않는다)
        [[maybe_unused]] uint8          _reserved          : 4;
    };

    // ------------------------------------------------------------------------------
    // 3) ApplyEffectsAbility — 코드 없이 데이터만으로 쓰는 어빌리티
    // ------------------------------------------------------------------------------
    /**
     * @class ApplyEffectsAbility
     * @brief 커밋하고 이펙트를 건 뒤 바로 끝나는 어빌리티입니다(카탈로그 클래스 이름 "ApplyEffects").
     * @details 이름 매개변수:
     *          - `selfEffect`: 주인에게 걸 이펙트 id(회복 · 버프 · 패시브)
     *          - `targetEffect`: 발동 이벤트의 **상대**에게 걸 이펙트 id — 이벤트의 `_target` 이 주인 자신이거나 비었으면(피격 같은 받은 이벤트)
     *            일으킨 쪽(`_instigator`), 아니면 `_target`. 가시(맞으면 때린 쪽에 피해)가 이 길이다. 이벤트 없이 발동하면 걸지 않는다
     *          `_bActivateOnGranted` 와 무한 이펙트를 함께 쓰면 패시브가 됩니다.
     */
    class SW_GF_API ApplyEffectsAbility : public GameplayAbility
    {
    public:
        ApplyEffectsAbility() = default;

        void activateAbility( const GameplayEventData* pTriggerEvent ) override;
    };
} // namespace sw
