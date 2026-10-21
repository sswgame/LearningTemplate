/**
 * @file GameplayAbility.h
 * @brief 게임플레이 어빌리티입니다. 태그, 비용, 쿨다운 같은 발동 조건을 통과하면 실행되는 행동입니다(언리얼의 `UGameplayAbility` 에 해당).
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

#include "GameFramework/Base/Gameplay/Ability/AbilitySystemEvents.h"
#include "GameFramework/Base/Gameplay/Ability/AbilitySystemTypes.h"
#include "GameFramework/Base/Gameplay/Ability/GameplayEffect.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class AbilitySystemComponent;
    class AbilityTask;
    class GameObject;

    // ------------------------------------------------------------------------------
    // 1) 설정. 어빌리티 클래스의 기본값입니다(언리얼 `UGameplayAbility` 의 CDO 프로퍼티에 해당)
    // ------------------------------------------------------------------------------
    /**
     * @brief 어빌리티 하나의 설정입니다. 카탈로그 XML 의 `<Ability>` 가 채우거나, 코드가 생성자에서 채웁니다.
     * @details 태그 여섯 개는 언리얼과 뜻이 같다.
     *          - `_abilityTags`: 이 어빌리티를 설명한다("Ability.Skill.Fireball"). 다른 어빌리티의 취소, 막기, `tryActivateAbilitiesByTag` 가 이 태그로 대상을 고른다.
     *          - `_cancelAbilitiesWithTags`: 발동하는 순간 이 태그를 가진 다른 활성 어빌리티를 취소한다.
     *          - `_blockAbilitiesWithTags`: 실행되는 동안 이 태그를 가진 어빌리티는 발동하지 못한다.
     *          - `_activationOwnedTags`: 실행되는 동안 주인에게 붙는다("State.Casting").
     *          - `_activationRequiredTags`: 주인이 이 태그를 모두 가져야 발동한다.
     *          - `_activationBlockedTags`: 주인이 이 태그를 하나라도 가지면 발동하지 못한다("State.Dead", "State.Stunned").
     *          `_triggerEventTags` 의 게임플레이 이벤트가 오면 스스로 발동한다. 피격 반응과 콤보에 쓴다.
     */
    struct SW_GF_API GameplayAbilityConfig
    {
        hashed_string                               _id{}; ///< 카탈로그 id("GA_Fireball"). 로그와 UI 가 쓴다
        TagContainer                                _abilityTags{};
        TagContainer                                _cancelAbilitiesWithTags{};
        TagContainer                                _blockAbilitiesWithTags{};
        TagContainer                                _activationOwnedTags{};
        TagContainer                                _activationRequiredTags{};
        TagContainer                                _activationBlockedTags{};
        TagContainer                                _triggerEventTags{};
        TagContainer                                _ownedTagTriggerTags{};                  ///< 주인에게 이 태그(나 그 하위 태그)가 생기는 순간 발동한다(언리얼 `OwnedTagAdded` 트리거)
        shared_ptr<const GameplayEffectDef>         _pCostEffect{};                          ///< 발동 비용(마나 -20). `commitAbility` 가 적용한다
        shared_ptr<const GameplayEffectDef>         _pCooldownEffect{};                      ///< 쿨다운(부여 태그가 쿨다운 태그). `commitAbility` 가 적용한다
        unordered_map<hashed_string, float32>       _mapParameter{};                         ///< 데이터 매개변수(피해, 사거리, 속도)
        unordered_map<hashed_string, hashed_string> _mapNameParameter{};                     ///< 데이터 이름 매개변수(걸 이펙트 id, 프리팹)
        uint8                                       _bRetriggerInstancedAbility{ SW_FALSE }; ///< 실행 중에 다시 발동하면 끝내고 새로 시작할지 여부(아니면 실패)
        uint8                                       _bActivateOnGranted{ SW_FALSE };         ///< 부여되는 순간 발동할지 여부(패시브)
    };
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // 2) GameplayAbility
    // ------------------------------------------------------------------------------
    /**
     * @class GameplayAbility
     * @brief 어빌리티 하나입니다. 부여된 스펙마다 인스턴스 하나를 컴포넌트가 보관합니다(언리얼의 `InstancedPerActor` 에 해당).
     * @details 흐름은 언리얼과 같다.
     *          1. `AbilitySystemComponent::tryActivateAbility` 가 조건(태그, 쿨다운, 비용, `canActivateAbility`)을 검사하고 `activateAbility` 를 부른다.
     *          2. 구현은 `commitAbility()` 로 비용과 쿨다운을 적용하고(실패하면 `endAbility()`), 일을 한다. 바로 끝나면 즉시 `endAbility()` 를 부른다.
     *             시간이 걸리면 작업(`waitDelay`, `waitGameplayEvent`)이나 `onTickAbility` 로 기다렸다가 끝낸다.
     *          3. 끝나면(`endAbility`, `cancelAbility`) 작업을 모두 정리하고 `onEndAbility( bWasCancelled )` 를 부른다.
     *
     *          **스레드**: `onTickAbility` 와 작업은 컴포넌트의 틱(병렬 워커)에서 돈다. 그 안에서 **다른 오브젝트**를 바꿀 때는
     *          `applyEffectSpecToTarget` 처럼 프레임워크가 제공하는 함수를 쓴다. 틱 중이면 이 함수들이 적용을 틱 직후로 미룬다. 오브젝트 생성은 `GameObjectManager::executeOrDeferPostTick` 로 한다.
     *
     *          **핫 리로드**: 게임 모듈의 어빌리티 인스턴스는 모듈을 언로드하기 전에 컴포넌트가 정리한다(`IModuleUnloadListener`). 모듈을 다시 로드하면
     *          컴포넌트의 `_abilitySetID` 가 어빌리티를 다시 부여한다. 코드로 준 어빌리티는 게임이 다시 줘야 한다.
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
         * @brief 발동 조건을 하나 더 검사합니다(언리얼 `CanActivateAbility` 의 파생 부분). 기본은 true 입니다.
         * @details 태그, 쿨다운, 비용은 컴포넌트가 이미 검사했다. 사거리나 대상 유무처럼 그 밖의 조건만 여기서 검사한다. 상태를 바꾸지 않는다.
         */
        virtual bool canActivateAbility( const GameplayEventData* pTriggerEvent ) const;
        /**
         * @brief 어빌리티가 발동했을 때 불립니다(언리얼의 `ActivateAbility` 에 해당). 구현은 언젠가 `endAbility()` 를 불러야 합니다.
         * @param pTriggerEvent 이벤트로 발동했으면 그 이벤트, 입력이나 코드로 발동했으면 nullptr
         */
        virtual void activateAbility( const GameplayEventData* pTriggerEvent ) = 0;
        /** @brief 어빌리티가 끝났을 때 불립니다. 작업은 이미 정리된 상태이고, 취소로 끝났으면 @p bWasCancelled 가 true 입니다. */
        virtual void onEndAbility( bool bWasCancelled );
        /** @brief 실행되는 동안 매 틱 불립니다. 작업 다음에 호출되며, 기본 구현은 아무것도 하지 않습니다. */
        virtual void onTickAbility( float32 deltaTime );
        /** @brief 부여된 직후에 불립니다. */
        virtual void onGranted();
        /** @brief 회수되기 직전에 불립니다. 실행 중이었으면 취소한 뒤에 불립니다. */
        virtual void onRemoved();
        /** @brief 이 어빌리티에 묶인 입력이 눌렸을 때 불립니다. 실행 중일 때만 불리며, 차지와 콤보에 씁니다. */
        virtual void onInputPressed();
        /** @brief 이 어빌리티에 바인딩된 입력을 뗐을 때 불립니다. 실행 중일 때만 불리며, 차지 해제에 씁니다. */
        virtual void onInputReleased();

        /**
         * @brief 비용과 쿨다운을 겁니다(언리얼의 `CommitAbility` 에 해당). 발동한 뒤 비용을 치를 수 없게 됐거나 쿨다운이 걸렸으면 아무것도 걸지 않고 false 입니다.
         * @details 발동과 커밋을 나눈 이유가 있다. 차지나 조준처럼 "발동은 했지만 아직 쓰지 않은" 구간에는 마나와 쿨다운을 걸지 않아야 하기 때문이다.
         */
        bool commitAbility();
        /** @brief 비용을 치를 수 있으면 true 입니다. 비용이 없어도 true 입니다. */
        bool canAffordCost() const;
        /** @brief 쿨다운 태그가 주인에게 걸려 있으면 true 입니다. */
        bool isOnCooldown() const;
        /** @brief 쿨다운이 남은 시간입니다. 쿨다운이 없으면 0 입니다. */
        float32 getCooldownRemaining() const;
        /** @brief 어빌리티를 끝냅니다. 실행 중이 아니면 아무것도 하지 않습니다. */
        void endAbility();
        /** @brief 어빌리티를 취소로 끝냅니다(`onEndAbility( true )` 를 부름). */
        void cancelAbility();

        /** @brief 설정입니다. 부여한 뒤에 바꾸면 다음 발동부터 적용됩니다. */
        const GameplayAbilityConfig& getConfig() const { return _config; }
        GameplayAbilityConfig&       getConfig() { return _config; }
        /** @brief 설정을 통째로 바꿉니다. 카탈로그가 어빌리티를 만들 때 씁니다. */
        void setConfig( const GameplayAbilityConfig& config ) { _config = config; }
        /** @brief 데이터 매개변수(`<Param name value>`) 값입니다. 데이터에 없으면 비어 있습니다 — 어빌리티가 없음을 오류로 다룬다(코드가 기본값을 정하지 않는다). */
        [[nodiscard]] std::optional<float32> getParameter( const hashed_string& name ) const;
        /** @brief 데이터 이름 매개변수 값입니다. 없으면 빈 이름입니다. */
        hashed_string getNameParameter( const hashed_string& name ) const;

        /** @brief 이 어빌리티를 보유한 컴포넌트입니다. 부여하기 전이면 nullptr 입니다. */
        AbilitySystemComponent* getAbilitySystem() const { return _pAbilitySystem; }
        /** @brief 이 어빌리티를 보유한 오브젝트입니다(언리얼의 Avatar 에 해당). */
        GameObject* getAvatar() const;
        /** @brief 부여된 스펙의 핸들입니다. */
        AbilitySpecHandle getSpecHandle() const { return _specHandle; }
        /** @brief 부여된 레벨입니다. */
        int32 getLevel() const { return _level; }
        /** @brief 실행 중이면 true 입니다. */
        bool isActive() const { return _bActive == SW_TRUE; }
        /** @brief 지금 실행 중인 발동이 이벤트로 시작됐으면 true 입니다. 그 이벤트는 `getTriggerEventData` 로 읽습니다. */
        bool wasTriggeredByEvent() const { return _bTriggeredByEvent == SW_TRUE; }
        /** @brief 지금 실행 중인 발동을 시작한 이벤트입니다. `wasTriggeredByEvent` 가 false 이면 빈 값입니다. */
        const GameplayEventData& getTriggerEventData() const { return _triggerEventData; }
        /** @brief 이 어빌리티의 입력이 지금 눌려 있으면 true 입니다. */
        bool isInputPressed() const { return _bInputPressed == SW_TRUE; }

        // --------------------------------------------------------------------------
        // 이펙트. 레벨과 컨텍스트가 채워진 스펙을 만들고 적용한다
        // --------------------------------------------------------------------------
        /** @brief 카탈로그의 이펙트 @p effectID 로 이 어빌리티 레벨의 스펙을 만듭니다. 이펙트가 없으면 경고를 남기고 빈 스펙을 반환합니다. */
        GameplayEffectSpec makeOutgoingSpec( const hashed_string& effectID ) const;
        /** @brief 주인에게 스펙을 겁니다. */
        ActiveEffectHandle applyEffectSpecToOwner( const GameplayEffectSpec& spec );
        /** @brief 대상에게 스펙을 겁니다. 틱 중이면 틱 직후로 미룹니다(`AbilitySystemComponent::applyGameplayEffectSpecToTarget` 과 같음). */
        ActiveEffectHandle applyEffectSpecToTarget( const GameplayEffectSpec& spec, AbilitySystemComponent* pTarget );

        // --------------------------------------------------------------------------
        // 작업. 어빌리티가 실행되는 동안만 살아 있고, 끝나면 모두 정리된다
        // --------------------------------------------------------------------------
        /** @brief 작업을 붙이고 시작합니다. 어빌리티가 실행 중이 아니면 붙이지 않고 nullptr 을 반환합니다. */
        AbilityTask* addTask( unique_ptr<AbilityTask> pTask );
        /** @brief @p seconds 뒤에 @p onFinished 를 부르는 작업입니다(언리얼의 `UAbilityTask_WaitDelay` 에 해당). */
        AbilityTask* waitDelay( float32 seconds, const Delegate<void()>& onFinished );
        /**
         * @brief 주인에게 @p eventTag 나 그 하위 태그의 게임플레이 이벤트가 오면 @p onEvent 를 부르는 작업입니다(언리얼의 `UAbilityTask_WaitGameplayEvent` 에 해당).
         * @param bOnlyTriggerOnce 처음 한 번만 받고 끝낼지 여부
         */
        AbilityTask* waitGameplayEvent( TagID eventTag, const Delegate<void( const GameplayEventData& )>& onEvent, bool bOnlyTriggerOnce );
        /** @brief 아직 끝나지 않은 작업의 수입니다. */
        uint32 getActiveTaskCount() const;

    private:
        /** @brief 컴포넌트가 부여할 때 기록합니다. */
        void bindToAbilitySystem( AbilitySystemComponent* pAbilitySystem, AbilitySpecHandle specHandle, int32 level );
        /** @brief 컴포넌트가 발동을 확정한 뒤 부릅니다. 상태를 갱신하고 `activateAbility` 를 부릅니다. */
        void beginActivation( const GameplayEventData* pTriggerEvent );
        /** @brief 어빌리티를 끝냅니다. 작업을 정리하고, 컴포넌트에 알린 뒤 `onEndAbility` 를 부릅니다. */
        void finishActivation( bool bWasCancelled );
        /** @brief 작업과 `onTickAbility` 를 진행합니다. 컴포넌트의 시간 진행에서 부릅니다. */
        void updateAbility( float32 deltaTime );
        /** @brief 입력 상태를 기록하고, 실행 중이면 알립니다. */
        void setInputPressed( bool bPressed );
        /** @brief 끝난 작업을 지웁니다. 작업을 순회하는 중이면 지우기를 미룹니다. */
        void removeFinishedTasks();
        /** @brief 모든 작업을 끝내고 지웁니다. 어빌리티가 끝날 때 부릅니다. */
        void endAllTasks();

        GameplayAbilityConfig           _config;
        GameplayEventData               _triggerEventData;
        vector<unique_ptr<AbilityTask>> _listTask;
        AbilitySystemComponent*         _pAbilitySystem;
        AbilitySpecHandle               _specHandle;
        int32                           _level;
        uint32                          _activationSerial; ///< 발동마다 늘어난다. 콜백 안에서 끝나고 다시 시작했는지 구분하는 데 쓴다
        uint32                          _taskTickDepth;    ///< 작업을 순회하는 중이면 0 보다 크다. 그동안에는 작업을 지우지 않는다
        uint8                           _bActive           : 1;
        uint8                           _bTriggeredByEvent : 1;
        uint8                           _bInputPressed     : 1;
        uint8                           _bEnding           : 1; ///< 끝내는 중이다. 작업을 정리하거나 알리는 도중에 다시 끝내지 않는다
        [[maybe_unused]] uint8          _reserved          : 4;
    };
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // 3) ApplyEffectsAbility. 코드 없이 데이터만으로 쓰는 어빌리티
    // ------------------------------------------------------------------------------
    /**
     * @class ApplyEffectsAbility
     * @brief 커밋하고 이펙트를 건 뒤 바로 끝나는 어빌리티입니다(카탈로그의 클래스 이름은 "ApplyEffects").
     * @details 이름 매개변수:
     *          - `selfEffect`: 주인에게 걸 이펙트 id. 회복, 버프, 패시브에 쓴다.
     *          - `targetEffect`: 발동 이벤트의 **상대**에게 걸 이펙트 id. 이벤트의 `_target` 이 주인 자신이거나 비어 있으면(피격처럼 받은 이벤트)
     *            일으킨 쪽(`_instigator`)에게 적용하고, 아니면 `_target` 에게 적용한다. 맞으면 때린 쪽에 피해를 주는 가시가 이 경로를 쓴다.
     *            이벤트 없이 발동하면 걸지 않는다.
     *          `_bActivateOnGranted` 와 무한 이펙트를 함께 쓰면 패시브가 된다.
     */
    class SW_GF_API ApplyEffectsAbility : public GameplayAbility
    {
    public:
        ApplyEffectsAbility() = default;

        void activateAbility( const GameplayEventData* pTriggerEvent ) override;
    };
} // namespace sw
