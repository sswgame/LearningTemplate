/**
 * @file AbilitySystemComponent.h
 * @brief 어트리뷰트 · 이펙트 · 어빌리티 · 태그를 한 오브젝트에 모아 돌리는 컴포넌트입니다(언리얼 `UAbilitySystemComponent`).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"
#include "Core/String/TagID.h"
#include "Core/String/hashed_string.h"

#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Ability/AbilitySystemTypes.h"
#include "GameFramework/Ability/AttributeSet.h"
#include "GameFramework/Ability/GameplayEffect.h"
#include "GameFramework/Combat/HealthSourceComponent.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct GameplayCueEvent;
    struct GameplayEventData;

    class AbilityCatalog;
    class AbilitySystemModuleUnloadGuard;
    class GameplayAbility;

    /** @brief 어트리뷰트 current 가 바뀐 한 번입니다(`AbilitySystemComponent::registerAttributeChanged`). */
    struct AttributeChangeData
    {
        AbilitySystemComponent* _pAbilitySystem{ nullptr }; ///< 바뀐 컴포넌트
        hashed_string           _attribute{};               ///< 바뀐 어트리뷰트
        float32                 _oldValue{ 0.0f };          ///< 바뀌기 전 current
        float32                 _newValue{ 0.0f };          ///< 바뀐 뒤 current
    };
} // namespace sw

namespace sw
{
    /**
     * @class AbilitySystemComponent
     * @brief 오브젝트 하나의 어빌리티 시스템입니다 — 어트리뷰트 묶음, 걸린 이펙트, 부여된 어빌리티, 태그 개수를 들고 시간을 돌립니다.
     * @details **쓰는 법**(언리얼 GAS 와 같은 순서):
     *          1. 오브젝트에 붙이고 어트리뷰트 묶음을 더합니다 — 코드(`addAttributeSet<CombatAttributeSet>()`) 또는 데이터(`_abilitySetId` →
     *             카탈로그의 `<AbilitySet>` 이 플레이 시작에 묶음 · 어빌리티 · 시작 이펙트 · 태그를 한 번에 줍니다).
     *          2. 어빌리티를 줍니다(`giveAbility` · `giveAbilityById`). 입력 번호를 주면 `abilityInputPressed( 번호 )` 가 발동합니다.
     *          3. 어빌리티는 이펙트 스펙(`makeOutgoingSpec`)을 만들어 자기에게 · 남에게 겁니다(`applyGameplayEffectSpecToTarget`).
     *          4. 게임은 델리게이트(어트리뷰트 · 태그 · 큐 · 이벤트)나 "game" 채널(`GameplayCueEvent` · `AbilityOwnerDiedEvent`)로 반응합니다.
     *
     *          **시간**: 컴포넌트 틱이 `advanceTime( 프레임 시간 )` 을 부릅니다. 턴제는 틱을 끄고(`setCanEverTick( false )`) 턴마다 `advanceTime( 1 )`
     *          을 부르면 지속 · 주기 · 쿨다운이 턴 단위가 됩니다. 시험도 이 길로 시간을 돌립니다.
     *
     *          **스레드**: 컴포넌트 틱은 오브젝트마다 병렬 워커에서 돕니다. 자기 상태(시간 · 자기 이펙트 · 자기 어빌리티)는 그 자리에서 바꾸고,
     *          **다른 오브젝트에** 이펙트를 걸거나 이벤트를 보내면(`applyGameplayEffectSpecToTarget` · `sendGameplayEventToTarget`) 틱 직후로
     *          미룹니다(`UnitStatsComponent::takeDamage` 와 같은 규칙). 체력이 깎일 때 띄우는 피해 숫자도 틱 직후에 만듭니다.
     *
     *          **연결된 UI**: 체력 원천(`HealthSourceComponent`)입니다 — 체력 · 최대 체력이 바뀔 때마다 같은 오브젝트의 `HealthListenerComponent`(HP 바)에 비율을 알리고, 체력이 0 이면 쓰러짐(`Died`)으로 알립니다(바를 모른다). `_bShowDamageNumbers` 면
     *          체력이 깎일 때 `DamageNumberComponent` 숫자를 띄웁니다. 어트리뷰트 이름은 `_healthAttribute` · `_maxHealthAttribute` 로 바꿀 수 있습니다.
     *
     *          **저장**: PROPERTY 만 저장됩니다(어빌리티 세트 id · 표시 설정). 런타임 상태(어트리뷰트 값 · 이펙트 · 어빌리티)는 저장하지 않고
     *          플레이 시작에 세트에서 다시 만듭니다 — 세이브가 필요한 값은 게임의 `SaveGame` 이 어트리뷰트를 읽어 담습니다.
     */
    REFLECT( Category = "Gameplay", DisplayName = "Ability System Component", Tooltip = "Attributes, gameplay effects, abilities and gameplay tags (Unreal GAS style)" )
    class SW_GF_API AbilitySystemComponent : public HealthSourceComponent
    {
        friend class AbilitySystemModuleUnloadGuard;
        friend class GameplayAbility;

    public:
        REFLECT_BODY();

        using AttributeChangedDelegate = Delegate<void( const AttributeChangeData& )>;
        using TagChangedDelegate       = Delegate<void( TagID, int32 )>;
        using EffectDelegate           = Delegate<void( const ActiveGameplayEffect& )>;
        using AbilityActivatedDelegate = Delegate<void( const GameplayAbility& )>;
        using AbilityEndedDelegate     = Delegate<void( const GameplayAbility&, bool )>;
        using GameplayEventDelegate    = Delegate<void( const GameplayEventData& )>;
        using GameplayCueDelegate      = Delegate<void( const GameplayCueEvent& )>;

        /** @brief 입력에 묶이지 않은 어빌리티의 입력 번호입니다. */
        static constexpr int32 kNoInputId = -1;

        AbilitySystemComponent();
        ~AbilitySystemComponent() override;

        void onBeginPlay() override;
        void onEndPlay() override;
        void onTick( float32 deltaTime ) override;

        // --------------------------------------------------------------------------
        // 1) 시간 · 카탈로그 · 세트
        // --------------------------------------------------------------------------
        /** @brief 시간을 @p deltaTime 만큼 돌립니다 — 주기 실행, 지속 만료, 어빌리티 · 작업 틱. 음수 · 0 은 아무것도 하지 않습니다. */
        void advanceTime( float32 deltaTime );
        /** @brief 이 컴포넌트가 지금까지 돌린 시간입니다. */
        float32 getTime() const { return _time; }

        /** @brief id 로 이펙트 · 어빌리티 · 세트를 찾을 카탈로그를 정합니다. nullptr 이면 게임 서비스(`game::getService<AbilityCatalog>()`)를 씁니다. */
        void setCatalog( const AbilityCatalog* pCatalog ) { _pCatalog = pCatalog; }
        /** @brief 쓸 카탈로그입니다 — 정한 것, 없으면 게임 서비스, 그것도 없으면 nullptr 입니다. */
        const AbilityCatalog* findCatalog() const;

        /** @brief 플레이 시작에 줄 어빌리티 세트 id 입니다(카탈로그 `<AbilitySet id="...">`). */
        void                 setAbilitySetId( const hashed_string& setId ) { _abilitySetId = setId; }
        const hashed_string& getAbilitySetId() const { return _abilitySetId; }
        /**
         * @brief 카탈로그의 어빌리티 세트 하나를 지금 줍니다 — 어트리뷰트 묶음 · 기본값, 어빌리티(입력 번호 포함), 시작 이펙트, 태그.
         * @return 세트를 찾아 줬으면 true. 카탈로그 · 세트가 없으면 경고하고 false 입니다.
         */
        bool grantAbilitySet( const hashed_string& setId );

        // --------------------------------------------------------------------------
        // 2) 어트리뷰트
        // --------------------------------------------------------------------------
        /** @brief 어트리뷰트 묶음을 붙입니다. 다른 묶음과 이름이 겹치는 어트리뷰트는 먼저 붙은 쪽이 이깁니다(경고). */
        AttributeSet* addAttributeSet( unique_ptr<AttributeSet> pAttributeSet );
        /** @brief T 를 만들어 붙이고 돌려줍니다. */
        template <typename T>
        T* addAttributeSet()
        {
            unique_ptr<T> pAttributeSet = make_unique<T>();
            T*            pRaw          = pAttributeSet.get();
            return addAttributeSet( unique_ptr<AttributeSet>( std::move( pAttributeSet ) ) ) != nullptr ? pRaw : nullptr;
        }
        /** @brief 붙은 묶음들입니다(붙인 순서). */
        const vector<unique_ptr<AttributeSet>>& getAttributeSets() const { return _listAttributeSet; }
        /** @brief 어트리뷰트 @p name 을 가진 묶음입니다. 없으면 nullptr 입니다. */
        AttributeSet* findAttributeSetFor( const hashed_string& name ) const;
        /** @brief 어트리뷰트를 가졌으면 true 입니다. */
        bool hasAttribute( const hashed_string& name ) const;
        /** @brief current 값입니다. 없으면 @p fallback 입니다. */
        float32 getAttributeValue( const hashed_string& name, float32 fallback = 0.0f ) const;
        /** @brief base 값입니다. 없으면 @p fallback 입니다. */
        float32 getAttributeBaseValue( const hashed_string& name, float32 fallback = 0.0f ) const;
        /**
         * @brief base 값을 정합니다(묶음의 훅으로 클램프 → current 다시 집계 → 알림). 어트리뷰트가 없으면 false 입니다.
         * @details 즉시 이펙트가 아닌 코드 변경(레벨 업 · 세이브 불러오기 · 디버그)이 쓰는 길입니다.
         */
        bool setAttributeBaseValue( const hashed_string& name, float32 value );
        /**
         * @brief current 가 바뀔 때 부를 델리게이트를 겁니다(언리얼 `GetGameplayAttributeValueChangeDelegate`).
         * @param name 빈 이름이면 모든 어트리뷰트
         */
        DelegateHandle registerAttributeChanged( const hashed_string& name, const AttributeChangedDelegate& delegate );
        void           unregisterAttributeChanged( const hashed_string& name, DelegateHandle handle );

        // --------------------------------------------------------------------------
        // 3) 태그 — 개수로 센다(이펙트 둘이 같은 태그를 주면 둘 다 풀려야 사라진다)
        // --------------------------------------------------------------------------
        /** @brief 지금 가진 태그(개수 1 이상)입니다. */
        const TagContainer& getOwnedTags() const { return _ownedTags; }
        /** @brief @p tag 나 그 하위 태그를 가졌으면 true 입니다("Cooldown" 은 "Cooldown.Fireball" 에 맞는다). */
        bool hasMatchingTag( TagID tag ) const;
        /** @brief @p tags 를 모두 가졌으면 true 입니다(비었으면 true). */
        bool hasAllMatchingTags( const TagContainer& tags ) const;
        /** @brief @p tags 중 하나라도 가졌으면 true 입니다(비었으면 false). */
        bool hasAnyMatchingTags( const TagContainer& tags ) const;
        /** @brief @p tag 와 **같은** 태그의 개수입니다(하위는 세지 않는다). */
        int32 getTagCount( TagID tag ) const;
        /** @brief 이펙트 없이 태그를 붙입니다(언리얼 Loose Tag). 개수만큼 오른다. */
        void addLooseTag( TagID tag, int32 count = 1 );
        /** @brief 붙인 태그를 뗍니다. 0 아래로 내려가지 않습니다. */
        void removeLooseTag( TagID tag, int32 count = 1 );
        /** @brief 태그가 새로 생기거나(개수 0→1) 사라질 때(1→0) 부를 델리게이트입니다. 인자는 태그와 새 개수입니다. */
        DelegateHandle registerTagChanged( const TagChangedDelegate& delegate );
        void           unregisterTagChanged( DelegateHandle handle );

        // --------------------------------------------------------------------------
        // 4) 게임플레이 이펙트
        // --------------------------------------------------------------------------
        /** @brief 이 오브젝트가 건 쪽인 컨텍스트입니다. */
        GameplayEffectContext makeEffectContext() const;
        /** @brief 정의로 이 오브젝트가 거는 스펙을 만듭니다 — 컨텍스트 · 레벨 · 어트리뷰트 스냅샷이 채워집니다. */
        GameplayEffectSpec makeOutgoingSpec( const shared_ptr<const GameplayEffectDef>& pDef, int32 level = 1 ) const;
        /** @brief 카탈로그의 이펙트 id 로 스펙을 만듭니다. 없으면 빈 스펙입니다(경고). */
        GameplayEffectSpec makeOutgoingSpecById( const hashed_string& effectId, int32 level = 1 ) const;
        /**
         * @brief 스펙을 이 오브젝트에 겁니다(언리얼 `ApplyGameplayEffectSpecToSelf`).
         * @return 걸렸으면 유효한 핸들(즉시 이펙트도). 대상 태그 조건에 막혔거나 스펙이 비었으면 무효 핸들입니다.
         * @details 다른 오브젝트의 틱 안에서 부르지 마십시오 — 그 자리는 `applyGameplayEffectSpecToTarget` 입니다(미룹니다).
         */
        ActiveEffectHandle applyGameplayEffectSpecToSelf( const GameplayEffectSpec& spec );
        /**
         * @brief 스펙을 @p pTarget 에 겁니다. 틱 중이고 @p pTarget 이 지금 틱하는 오브젝트가 아니면 틱 직후로 미루고 무효 핸들을 돌려줍니다.
         * @details 미룬 적용은 대상을 핸들로 다시 찾으므로 그 사이 사라져도 안전합니다.
         */
        ActiveEffectHandle applyGameplayEffectSpecToTarget( const GameplayEffectSpec& spec, AbilitySystemComponent* pTarget );
        /** @brief 카탈로그 이펙트 id 로 스펙을 만들어 자기에게 겁니다(시작 이펙트 · 디버그). */
        ActiveEffectHandle applyGameplayEffectToSelf( const hashed_string& effectId, int32 level = 1 );
        /**
         * @brief 걸린 이펙트를 지웁니다. @p stacksToRemove 가 0 이하이면 통째로, 아니면 그만큼 스택을 내립니다(0 이 되면 지운다).
         * @return 무엇이든 바뀌었으면 true 입니다.
         */
        [[nodiscard]] bool removeActiveEffect( ActiveEffectHandle handle, int32 stacksToRemove = -1 );
        /** @brief 에셋 태그나 부여 태그가 @p tags 에 맞는 걸린 이펙트를 모두 지우고 그 수를 돌려줍니다(정화 · 디스펠). */
        int32 removeActiveEffectsWithTags( const TagContainer& tags );
        /** @brief 걸린 이펙트를 찾습니다. 즉시 이펙트 · 풀린 이펙트는 nullptr 입니다. */
        const ActiveGameplayEffect* findActiveEffect( ActiveEffectHandle handle ) const;
        /** @brief 아직 걸려 있으면 true 입니다. */
        bool isActiveEffect( ActiveEffectHandle handle ) const { return findActiveEffect( handle ) != nullptr; }
        /** @brief 걸려 있는 이펙트 수입니다. */
        uint32 getActiveEffectCount() const;
        /** @brief 걸린 이펙트의 남은 시간입니다. 무한이거나 없으면 0 입니다. */
        float32 getActiveEffectRemainingTime( ActiveEffectHandle handle ) const;
        /** @brief 걸린 이펙트의 스택 수입니다. 없으면 0 입니다. */
        int32 getActiveEffectStackCount( ActiveEffectHandle handle ) const;
        /** @brief 부여 태그가 @p tags 에 맞는 지속 이펙트 중 가장 늦게 끝나는 것의 남은 시간입니다(쿨다운 표시). 없으면 0 입니다. */
        float32 findLongestRemainingTimeWithGrantedTags( const TagContainer& tags ) const;
        /**
         * @brief 스펙을 지금 걸면 어떤 어트리뷰트도 0 아래로 내려가지 않는지입니다(비용 검사 — 언리얼 `CanApplyAttributeModifiers`).
         * @details `Add` 모디파이어만 봅니다. 없는 어트리뷰트를 깎는 비용은 치를 수 없습니다.
         */
        bool canApplyAttributeModifiers( const GameplayEffectSpec& spec ) const;
        /** @brief 이펙트가 걸린 뒤(지속 · 무한 · 스택 오름) 부를 델리게이트입니다. */
        DelegateHandle registerEffectApplied( const EffectDelegate& delegate );
        void           unregisterEffectApplied( DelegateHandle handle );
        /** @brief 이펙트가 풀린 뒤 부를 델리게이트입니다. */
        DelegateHandle registerEffectRemoved( const EffectDelegate& delegate );
        void           unregisterEffectRemoved( DelegateHandle handle );

        // --------------------------------------------------------------------------
        // 5) 어빌리티
        // --------------------------------------------------------------------------
        /**
         * @brief 어빌리티를 줍니다(언리얼 `GiveAbility`). 설정에 `_bActivateOnGranted` 가 있으면 바로 발동을 시도합니다.
         * @param inputId `abilityInputPressed` 로 발동할 번호(`kNoInputId` 면 입력 없음)
         */
        AbilitySpecHandle giveAbility( unique_ptr<GameplayAbility> pAbility, int32 level = 1, int32 inputId = kNoInputId );
        /** @brief 카탈로그의 어빌리티 id 로 만들어 줍니다. 없으면 무효 핸들입니다(경고). */
        AbilitySpecHandle giveAbilityById( const hashed_string& abilityId, int32 level = 1, int32 inputId = kNoInputId );
        /** @brief 어빌리티를 거둡니다. 도는 중이면 취소한 뒤 거둡니다. */
        bool clearAbility( AbilitySpecHandle handle );
        /** @brief 모든 어빌리티를 거둡니다. */
        void clearAllAbilities();
        /** @brief 조건을 보고 발동합니다. 이유는 결과로 돌려줍니다. */
        AbilityActivationResult tryActivateAbility( AbilitySpecHandle handle, const GameplayEventData* pTriggerEvent = nullptr );
        /** @brief 발동할 수 있는지만 봅니다(상태를 바꾸지 않는다). UI 의 버튼 비활성에 씁니다. */
        AbilityActivationResult canActivateAbility( AbilitySpecHandle handle, const GameplayEventData* pTriggerEvent = nullptr ) const;
        /** @brief 어빌리티 태그가 @p tags 에 맞는 어빌리티를 모두 발동해 봅니다. 하나라도 발동했으면 true 입니다. */
        [[nodiscard]] bool tryActivateAbilitiesByTag( const TagContainer& tags );
        /** @brief 카탈로그 id(`GameplayAbilityConfig::_id`)로 부여된 어빌리티를 찾습니다. 없으면 무효 핸들입니다. */
        AbilitySpecHandle findAbilitySpecHandle( const hashed_string& abilityId ) const;
        /** @brief 부여된 어빌리티를 찾습니다. 없거나 거두는 중이면 nullptr 입니다. */
        GameplayAbility* findAbility( AbilitySpecHandle handle ) const;
        /** @brief 도는 중이면 true 입니다. */
        bool isAbilityActive( AbilitySpecHandle handle ) const;
        /** @brief 어빌리티를 취소합니다. */
        void cancelAbility( AbilitySpecHandle handle );
        /** @brief 어빌리티 태그가 @p tags 에 맞는 도는 어빌리티를 취소합니다(@p pIgnore 는 빼고). */
        void cancelAbilitiesWithTags( const TagContainer& tags, const GameplayAbility* pIgnore = nullptr );
        /** @brief 도는 어빌리티를 모두 취소합니다. */
        void cancelAllAbilities();
        /** @brief 부여된 어빌리티 수입니다(거두는 중 제외). */
        uint32 getAbilityCount() const;
        /** @brief 부여된 어빌리티 핸들을 채웁니다(부여 순서). */
        void getAbilitySpecHandles( vector<AbilitySpecHandle>& outListHandle ) const;
        /** @brief 입력 @p inputId 가 눌렸습니다 — 그 번호의 어빌리티가 돌고 있으면 알리고, 아니면 발동을 시도합니다. */
        void abilityInputPressed( int32 inputId );
        /** @brief 입력 @p inputId 가 떼어졌습니다. */
        void abilityInputReleased( int32 inputId );
        /** @brief 어빌리티가 발동한 뒤 부를 델리게이트입니다. */
        DelegateHandle registerAbilityActivated( const AbilityActivatedDelegate& delegate );
        void           unregisterAbilityActivated( DelegateHandle handle );
        /** @brief 어빌리티가 끝난 뒤 부를 델리게이트입니다(두 번째 인자 = 취소였는지). */
        DelegateHandle registerAbilityEnded( const AbilityEndedDelegate& delegate );
        void           unregisterAbilityEnded( DelegateHandle handle );

        // --------------------------------------------------------------------------
        // 6) 게임플레이 이벤트 · 큐
        // --------------------------------------------------------------------------
        /**
         * @brief 게임플레이 이벤트를 이 오브젝트에 보냅니다(언리얼 `HandleGameplayEvent`). 트리거 태그가 맞는 어빌리티가 발동하고 구독자가 받습니다.
         * @return 이 이벤트로 발동한 어빌리티 수
         */
        int32 handleGameplayEvent( TagID eventTag, const GameplayEventData& payload );
        /**
         * @brief @p pTarget 에 게임플레이 이벤트를 보냅니다(언리얼 `SendGameplayEventToActor`). 틱 중이고 대상이 지금 틱하는 오브젝트가 아니면 틱 직후로 미룹니다.
         */
        static void sendGameplayEventToTarget( AbilitySystemComponent* pTarget, TagID eventTag, const GameplayEventData& payload );
        /** @brief @p eventTag(또는 그 하위)의 이벤트가 오면 부를 델리게이트를 겁니다. */
        DelegateHandle registerGameplayEvent( TagID eventTag, const GameplayEventDelegate& delegate );
        void           unregisterGameplayEvent( TagID eventTag, DelegateHandle handle );
        /** @brief 이 오브젝트의 이펙트가 큐를 낼 때마다 부를 델리게이트입니다(채널 이벤트보다 먼저). */
        DelegateHandle registerGameplayCue( const GameplayCueDelegate& delegate );
        void           unregisterGameplayCue( DelegateHandle handle );

        // --------------------------------------------------------------------------
        // 7) 표시 설정
        // --------------------------------------------------------------------------
        void                 setShowDamageNumbers( bool bShow ) { _bShowDamageNumbers = bShow; }
        bool                 showsDamageNumbers() const { return _bShowDamageNumbers; }
        void                 setHealthAttributes( const hashed_string& health, const hashed_string& maxHealth );
        const hashed_string& getHealthAttribute() const { return _healthAttribute; }
        const hashed_string& getMaxHealthAttribute() const { return _maxHealthAttribute; }
        /** @brief 체력 원천의 읽기입니다 — `_healthAttribute` · `_maxHealthAttribute` 의 current, 체력 0 이하면 쓰러짐. 체력 어트리뷰트가 없으면 `_bHasHealth` 가 SW_FALSE 입니다. */
        HealthReading getHealthReading() const override;

    private:
        /** @brief 부여된 어빌리티 하나입니다(언리얼 `FGameplayAbilitySpec`). 주소가 바뀌지 않게 `unique_ptr` 로 든다. */
        struct AbilitySpec
        {
            AbilitySpecHandle           _handle{};
            unique_ptr<GameplayAbility> _pAbility{};
            int32                       _level{ 1 };
            int32                       _inputId{ kNoInputId };
            uint8                       _bPendingRemove{ SW_FALSE };
        };

        /** @brief 태그 하나의 개수입니다. */
        struct TagCountEntry
        {
            TagID _tag{};
            int32 _count{ 0 };
        };

        /** @brief 스코프 동안 활성 이펙트 · 스펙 목록에서 원소를 빼지 않습니다(빼기는 표시만 하고 스코프가 끝날 때 지운다). */
        class ScopedListLock
        {
        public:
            explicit ScopedListLock( AbilitySystemComponent& owner );
            ~ScopedListLock();
            ScopedListLock( const ScopedListLock& )            = delete;
            ScopedListLock& operator=( const ScopedListLock& ) = delete;

        private:
            AbilitySystemComponent& _owner;
        };

        // ---- 이펙트 ----
        /** @brief 지금 대상에게 걸 수 있는지(태그 조건)입니다. */
        bool canApplySpec( const GameplayEffectSpec& spec ) const;
        /** @brief 모디파이어 하나의 크기를 구합니다. 못 구하면 false 입니다. */
        bool computeModifierMagnitude( const GameplayEffectModifier& modifier, const GameplayEffectSpec& spec, float32& outMagnitude ) const;
        /** @brief 스펙의 모디파이어 · 실행 계산을 돌려 base 에 쓸 변경 목록을 만듭니다(스택 하나 몫). */
        void evaluateExecutionModifiers( const GameplayEffectSpec& spec, int32 stackCount, vector<EvaluatedModifier>& outListModifier ) const;
        /** @brief 즉시 · 주기 실행 한 번입니다 — base 를 바꾸고 묶음 훅 · 큐를 부릅니다. */
        void executeSpec( const GameplayEffectSpec& spec, int32 stackCount );
        /** @brief base 에 변경 하나를 씁니다(클램프 · 집계 · 훅). */
        void applyModifierToBase( const EvaluatedModifier& modifier, float32 stackScale, const GameplayEffectSpec& spec );
        /** @brief 지속 · 무한 이펙트를 새로 걸거나 스택을 올립니다. */
        ActiveEffectHandle applyDurationSpec( const GameplayEffectSpec& spec );
        /** @brief 걸린 이펙트를 활성 목록에서 풉니다(태그 · 집계 · 큐 · 알림). */
        void removeActiveEffectInternal( ActiveGameplayEffect& activeEffect );
        /** @brief 걸린 이펙트를 찾습니다(지우는 중 제외). */
        ActiveGameplayEffect* findActiveEffectMutable( ActiveEffectHandle handle );
        /** @brief 같은 정의로 걸린 스택 이펙트를 찾습니다. */
        ActiveGameplayEffect* findStackableEffect( const GameplayEffectDef& def );
        /** @brief 걸린 이펙트들의 지속 · 주기를 @p deltaTime 만큼 돌립니다. */
        void updateActiveEffects( float32 deltaTime );
        /** @brief 이펙트 하나의 큐를 냅니다. */
        void dispatchCues( const GameplayEffectSpec& spec, GameplayCuePhase phase, float32 magnitude );
        /** @brief 지우라고 표시된 이펙트 · 스펙을 실제로 지웁니다(잠금이 풀렸을 때만). */
        void compactPendingRemovals();

        // ---- 어트리뷰트 ----
        /** @brief 어트리뷰트 하나의 current 를 base + 걸린 이펙트로 다시 집계하고, 바뀌었으면 알립니다. */
        void recomputeAttribute( const hashed_string& name );
        /** @brief 묶음 안의 값을 찾습니다(쓰기용). */
        AttributeData* findAttributeDataMutable( const hashed_string& name, AttributeSet** ppOutSet );
        /** @brief current 가 바뀐 것을 알립니다(묶음 훅 → 델리게이트 → 체력 UI). */
        void notifyAttributeChanged( AttributeSet& attributeSet, const hashed_string& name, float32 oldValue, float32 newValue );

        // ---- 태그 ----
        /** @brief 태그 개수를 @p delta 만큼 바꿉니다. 0↔1 을 지나면 소유 태그 · 알림을 고칩니다. */
        void updateTagCount( TagID tag, int32 delta );
        /** @brief 컨테이너의 태그마다 `updateTagCount`. */
        void updateTagCounts( const TagContainer& tags, int32 delta );
        /** @brief 막힌 어빌리티 태그 개수를 바꿉니다. */
        void updateBlockedAbilityTags( const TagContainer& tags, int32 delta );
        /** @brief 어빌리티 태그 중 하나라도 막혀 있으면 true 입니다. */
        bool isAbilityBlocked( const TagContainer& abilityTags ) const;

        // ---- 어빌리티 ----
        AbilitySpec*       findAbilitySpecMutable( AbilitySpecHandle handle );
        const AbilitySpec* findAbilitySpec( AbilitySpecHandle handle ) const;
        /** @brief 발동이 확정된 어빌리티의 시작 처리(태그 · 취소 · 막기 · 알림)입니다. */
        void handleAbilityActivated( GameplayAbility& ability );
        /** @brief 어빌리티가 끝났을 때의 처리(태그 · 막기 해제 · 알림)입니다. */
        void handleAbilityEnded( GameplayAbility& ability, bool bWasCancelled );
        /** @brief 걸린 어빌리티들의 틱입니다. */
        void tickActiveAbilities( float32 deltaTime );
        /** @brief 거둘 스펙을 정리합니다 — 취소 · `onRemoved` · 표시. */
        void retireAbilitySpec( AbilitySpec& spec );

        // ---- 스레드 · 연결된 UI ----
        /**
         * @brief 이 컴포넌트를 바꾸는 일을 지금 해도 되는지입니다. 틱 중이고 지금 틱하는 오브젝트가 주인이 아니면 false 입니다(다른 워커가 이 오브젝트를
         *        틱할 수 있다).
         */
        bool canMutateNow() const;
        /** @brief 깎인 체력 @p amount 를 피해 숫자로 띄웁니다(틱 중이면 틱 직후). */
        void spawnDamageNumber( float32 amount );
        /** @brief [@p pBegin, @p pEnd) 안의 코드(어빌리티 · 묶음 · 실행 계산의 vtable, 델리게이트 스텁)를 뗍니다 — 모듈을 내리기 전 */
        uint32 onModuleUnloading( const void* pBegin, const void* pEnd );

        // ---- 런타임 상태(저장하지 않는다) ----
        vector<unique_ptr<AttributeSet>>                                                                _listAttributeSet;
        vector<unique_ptr<ActiveGameplayEffect>>                                                        _listActiveEffect;
        vector<unique_ptr<AbilitySpec>>                                                                 _listAbilitySpec;
        unordered_map<uint64, TagCountEntry>                                                            _mapTagCount;          ///< 태그 id → 개수
        unordered_map<uint64, TagCountEntry>                                                            _mapBlockedAbilityTag; ///< 막힌 어빌리티 태그 id → 개수
        TagContainer                                                                                    _ownedTags;            ///< 개수 1 이상인 태그
        unordered_map<hashed_string, unique_ptr<MulticastDelegate<void( const AttributeChangeData& )>>> _mapAttributeChangedMulticast;
        unordered_map<uint64, unique_ptr<MulticastDelegate<void( const GameplayEventData& )>>>          _mapGameplayEventMulticast;
        MulticastDelegate<void( const AttributeChangeData& )>                                           _anyAttributeChangedMulticast;
        MulticastDelegate<void( TagID, int32 )>                                                         _tagChangedMulticast;
        MulticastDelegate<void( const ActiveGameplayEffect& )>                                          _effectAppliedMulticast;
        MulticastDelegate<void( const ActiveGameplayEffect& )>                                          _effectRemovedMulticast;
        MulticastDelegate<void( const GameplayAbility& )>                                               _abilityActivatedMulticast;
        MulticastDelegate<void( const GameplayAbility&, bool )>                                         _abilityEndedMulticast;
        MulticastDelegate<void( const GameplayCueEvent& )>                                              _gameplayCueMulticast;
        unique_ptr<AbilitySystemModuleUnloadGuard>                                                      _pModuleCodeGuard; ///< 모듈을 내리기 전 그 모듈의 코드를 뗀다
        const AbilityCatalog*                                                                           _pCatalog;
        float32                                                                                         _time;
        uint64                                                                                          _nextHandleId;
        uint32                                                                                          _listLockDepth;
        uint8                                                                                           _bHasPendingRemoval;

        // ---- 저장되는 설정 ----
        PROPERTY( Category = "Ability System", DisplayName = "Ability Set", Tooltip = "Ability set id in the AbilityCatalog, granted on begin play" )
        hashed_string _abilitySetId;
        PROPERTY( Category = "Ability System", DisplayName = "Health Attribute", Tooltip = "Attribute that drives the health bar and damage numbers" )
        hashed_string _healthAttribute;
        PROPERTY( Category = "Ability System", DisplayName = "Max Health Attribute", Tooltip = "Attribute used as the health bar maximum" )
        hashed_string _maxHealthAttribute;
        PROPERTY( Category = "Feedback", DisplayName = "Damage Number Offset", Tooltip = "Where damage numbers appear, relative to the owner", Units = m )
        float3 _damageNumberOffset;
        PROPERTY( Category = "Feedback", DisplayName = "Show Damage Numbers", Tooltip = "Spawn a floating number whenever health goes down" )
        bool _bShowDamageNumbers;
    };
} // namespace sw
