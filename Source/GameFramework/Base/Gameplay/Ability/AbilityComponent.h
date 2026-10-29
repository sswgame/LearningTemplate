/**
 * @file AbilityComponent.h
 * @brief 어트리뷰트, 이펙트, 어빌리티, 태그를 한 오브젝트에서 함께 돌리는 컴포넌트입니다(언리얼의 `UAbilitySystemComponent` 에 해당).
 */
#pragma once
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"
#include "Core/String/TagID.h"
#include "Core/String/hashed_string.h"

#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Character/Combat/Health/HealthSourceComponent.h"
#include "GameFramework/Base/Gameplay/Ability/AbilityEffect.h"
#include "GameFramework/Base/Gameplay/Ability/AbilityTypes.h"
#include "GameFramework/Base/Gameplay/Ability/AttributeSet.h"
#include "GameFramework/Base/Gameplay/Ability/TagCountTable.h"
#include "GameFramework/GameFrameworkExports.h"

#include <optional>

namespace sw
{
    struct AbilityPresentationEvent;
    struct AbilityEventData;

    class AbilityCatalog;
    class Ability;

    /** @brief 어트리뷰트의 current 값이 한 번 바뀐 내용입니다(`AbilityComponent::registerAttributeChanged` 가 전달). */
    struct AttributeChangeData
    {
        AbilityComponent* _pAbilitySystem{ nullptr }; ///< 바뀐 컴포넌트
        hashed_string     _attribute{};               ///< 바뀐 어트리뷰트
        float32           _oldValue{ 0.0f };          ///< 바뀌기 전 current
        float32           _newValue{ 0.0f };          ///< 바뀐 뒤 current
    };
} // namespace sw

namespace sw
{
    /**
     * @class AbilityComponent
     * @brief 오브젝트 하나의 어빌리티 시스템으로, 어트리뷰트 세트와 적용된 이펙트, 부여된 어빌리티, 태그 개수를 보관하고 시간을 진행합니다.
     * @details **쓰는 법**(언리얼 GAS 와 같은 순서):
     *          1. 오브젝트에 붙이고 어트리뷰트 세트를 추가한다. 코드(`addAttributeSet<CombatAttributeSet>()`)로 하거나 데이터로 할 수 있다.
     *             데이터로 하면 `_abilitySetID` 가 가리키는 카탈로그의 `<AbilitySet>` 이 플레이 시작에 어트리뷰트 세트, 어빌리티, 시작 이펙트, 태그를 한 번에 준다.
     *          2. 어빌리티를 준다(`giveAbility`, `giveAbilityByID`). 입력 번호를 주면 `abilityInputPressed( 번호 )` 가 어빌리티를 발동한다.
     *          3. 어빌리티는 이펙트 스펙(`makeOutgoingSpec`)을 만들어 자기 자신이나 다른 오브젝트에 적용한다(`applyGameplayEffectSpecToTarget`).
     *          4. 게임은 델리게이트(어트리뷰트, 태그, 큐, 이벤트)나 "game" 채널(`AbilityPresentationEvent`, `AbilityOwnerDiedEvent`)로 반응한다.
     *
     *          **시간**: 컴포넌트 틱이 `advanceTime( 프레임 시간 )` 을 부른다. 턴제 게임은 틱을 끄고(`setCanEverTick( false )`) 턴마다
     *          `advanceTime( 1 )` 을 부르면 된다. 그러면 지속 시간, 주기, 쿨다운이 턴 단위가 된다. 테스트도 이 함수로 시간을 진행한다.
     *
     *          **스레드**: 컴포넌트 틱은 오브젝트마다 병렬 워커에서 돈다. 자기 상태(시간, 자기 이펙트, 자기 어빌리티)는 바로 바꾼다.
     *          **다른 오브젝트에** 이펙트를 걸거나 이벤트를 보내면(`applyGameplayEffectSpecToTarget`, `sendGameplayEventToTarget`) 틱이 끝난 직후로 미룬다.
     *          `UnitStatsComponent::takeDamage` 도 같은 규칙을 따른다. 체력이 깎일 때 띄우는 피해 숫자도 틱 직후에 만든다.
     *
     *          **연결된 UI**: 이 컴포넌트는 체력 원천(`HealthSourceComponent`)이다. 체력이나 최대 체력이 바뀔 때마다 같은 오브젝트의
     *          `HealthListenerComponent`(HP 바)에 비율을 알리고, 체력이 0 이면 쓰러짐(`Died`)을 알린다. 이 컴포넌트는 HP 바의 존재를 알지 못한다.
     *          `_bShowDamageNumbers` 가 켜져 있으면 체력이 깎일 때 `DamageNumberComponent` 로 숫자를 띄운다.
     *          체력 어트리뷰트의 이름은 `_healthAttribute` 와 `_maxHealthAttribute` 로 바꿀 수 있다.
     *
     *          **저장**: PROPERTY 만 저장한다(어빌리티 세트 id, 표시 설정). 런타임 상태(어트리뷰트 값, 이펙트, 어빌리티)는 저장하지 않고
     *          플레이를 시작할 때 세트에서 다시 만든다. 세이브가 필요한 값은 게임의 `SaveGame` 이 `AbilityAttributeSaveUtil` 로 base 값을 담는다.
     */
    REFLECT( Category = "Gameplay", DisplayName = "Ability System Component", Tooltip = "Attributes, gameplay effects, abilities and gameplay tags (Unreal GAS style)" )
    class SW_GF_API AbilityComponent : public HealthSourceComponent
    {
        friend class Ability;

    public:
        REFLECT_BODY();

        using AttributeChangedDelegate    = Delegate<void( const AttributeChangeData& )>;
        using EffectDelegate              = Delegate<void( const ActiveGameplayEffect& )>;
        using AbilityActivatedDelegate    = Delegate<void( const Ability& )>;
        using AbilityEndedDelegate        = Delegate<void( const Ability&, bool )>;
        using AbilityEventDelegate        = Delegate<void( const AbilityEventData& )>;
        using AbilityPresentationDelegate = Delegate<void( const AbilityPresentationEvent& )>;

        /** @brief 입력에 묶이지 않은 어빌리티의 입력 번호입니다. */
        static constexpr int32 kNoInputID = -1;

        AbilityComponent();
        ~AbilityComponent() override;

        void onBeginPlay() override;
        void onEndPlay() override;
        /**
         * @brief 모듈 @p moduleName 을 내리기 전에 그 모듈 타입(`getTypeInfo` 의 모듈 이름)의 어빌리티 · 어트리뷰트 세트와, 그 모듈의 실행 계산을 쓰는 이펙트를 놓습니다.
         * @details 모듈을 다시 올리면 `_abilitySetID` 가 어빌리티를 다시 준다. 게임이 건 델리게이트는 게임이 든 구독이 게임과 함께 푼다.
         */
        void onModuleUnloading( string_view moduleName ) override;
        void onTick( float32 deltaTime ) override;

        // --------------------------------------------------------------------------
        // 1) 시간, 카탈로그, 세트
        // --------------------------------------------------------------------------
        /** @brief 시간을 @p deltaTime 만큼 진행합니다. 주기 이펙트 실행, 지속 이펙트 만료, 어빌리티와 작업의 틱을 처리합니다. 0 이하이면 아무것도 하지 않습니다. */
        void advanceTime( float32 deltaTime );
        /** @brief 이 컴포넌트가 지금까지 진행한 시간입니다. */
        float32 getTime() const { return _time; }

        /** @brief id 로 이펙트, 어빌리티, 세트를 찾을 카탈로그를 정합니다. nullptr 이면 게임 서비스(`game::findSubsystem<AbilityCatalog>()`)를 씁니다. */
        void setCatalog( const AbilityCatalog* pCatalog ) { _pCatalog = pCatalog; }
        /** @brief 쓸 카탈로그입니다. 정해 둔 카탈로그가 없으면 게임 서비스를 쓰고, 그것도 없으면 nullptr 입니다. */
        const AbilityCatalog* findCatalog() const;

        /** @brief 플레이를 시작할 때 줄 어빌리티 세트의 id 입니다(카탈로그의 `<AbilitySet id="...">`). */
        void                 setAbilitySetID( const hashed_string& setID ) { _abilitySetID = setID; }
        const hashed_string& getAbilitySetID() const { return _abilitySetID; }
        /**
         * @brief 카탈로그의 어빌리티 세트 하나를 지금 줍니다. 어트리뷰트 세트와 기본값, 어빌리티(입력 번호 포함), 시작 이펙트, 태그가 들어 있습니다.
         * @return 세트를 찾아 줬으면 true. 카탈로그나 세트가 없으면 경고를 남기고 false 입니다.
         */
        bool grantAbilitySet( const hashed_string& setID );

        // --------------------------------------------------------------------------
        // 2) 어트리뷰트
        // --------------------------------------------------------------------------
        /** @brief 어트리뷰트 세트를 붙입니다. 다른 세트와 이름이 겹치는 어트리뷰트는 먼저 붙은 쪽이 우선하고, 경고를 남깁니다. */
        AttributeSet* addAttributeSet( unique_ptr<AttributeSet> pAttributeSet );
        /** @brief T 를 만들어 붙이고 반환합니다. */
        template <typename T>
        T* addAttributeSet()
        {
            unique_ptr<T> pAttributeSet = make_unique<T>();
            T*            pRaw          = pAttributeSet.get();
            return addAttributeSet( unique_ptr<AttributeSet>( std::move( pAttributeSet ) ) ) != nullptr ? pRaw : nullptr;
        }
        /** @brief 붙은 어트리뷰트 세트 목록입니다(붙인 순서). */
        const vector<unique_ptr<AttributeSet>>& getAttributeSets() const { return _listAttributeSet; }
        /** @brief 어트리뷰트 @p name 을 가진 세트입니다. 없으면 nullptr 입니다. */
        AttributeSet* findAttributeSetFor( const hashed_string& name ) const;
        /** @brief 어트리뷰트가 있으면 true 입니다. */
        bool hasAttribute( const hashed_string& name ) const;
        /** @brief current 값입니다. 이 컴포넌트에 없는 어트리뷰트면 비어 있습니다 — 부르는 쪽이 없음을 정한다(0 을 지어내지 않는다). */
        [[nodiscard]] std::optional<float32> getAttributeValue( const hashed_string& name ) const;
        /** @brief base 값입니다. 없는 어트리뷰트면 비어 있습니다. */
        [[nodiscard]] std::optional<float32> getAttributeBaseValue( const hashed_string& name ) const;
        /**
         * @brief base 값을 정합니다. 세트의 훅으로 클램프하고, current 를 다시 집계한 뒤 알립니다. 어트리뷰트가 없으면 false 입니다.
         * @details 즉시 이펙트가 아니라 코드에서 직접 바꿀 때 씁니다. 레벨 업, 세이브 불러오기, 디버그가 해당합니다.
         */
        bool setAttributeBaseValue( const hashed_string& name, float32 value );
        /**
         * @brief current 가 바뀔 때 부를 델리게이트를 겁니다(언리얼 `GetGameplayAttributeValueChangeDelegate`).
         * @param name 빈 이름이면 모든 어트리뷰트
         * @return 등록을 소유하는 구독. 사라지면 뗀다(이 컴포넌트가 먼저 사라지면 무효가 된다)
         */
        [[nodiscard]] Subscription registerAttributeChanged( const hashed_string& name, const AttributeChangedDelegate& delegate );

        // --------------------------------------------------------------------------
        // 3) 태그. 개수로 센다(이펙트 둘이 같은 태그를 주면 둘 다 해제되어야 사라진다)
        // --------------------------------------------------------------------------
        /**
         * @brief 가진 태그와 개수입니다. 이펙트 · 어빌리티가 준 태그와 이펙트 없이 붙인 태그(언리얼 Loose Tag, `updateCount`)를 함께 셉니다.
         * @details 태그가 생기거나 사라질 때의 알림은 `registerChanged` 입니다. 조회는 `hasMatchingTag`, `hasAllMatchingTags`, `hasAnyMatchingTags`, `getCount` 입니다.
         */
        TagCountTable&       getOwnedTagCounts() { return _ownedTagCounts; }
        const TagCountTable& getOwnedTagCounts() const { return _ownedTagCounts; }
        /**
         * @brief 무적 태그("State.Invulnerable")입니다. 주인이 이 태그를 가진 동안 체력 어트리뷰트의 base 는 내려가지 않습니다.
         * @details 막는 곳은 base 를 쓰는 두 경로(이펙트 모디파이어 `applyModifierToBase`, 직접 쓰기 `setAttributeBaseValue`)가 함께 지나는
         *          `guardHealthBaseChange` 하나다. `IncomingDamage` 같은 메타 어트리뷰트도 끝내 `setAttributeBaseValue` 로 체력을 쓰므로 함께 막힌다.
         *          이펙트가 부여 태그로 주면 무적 시간(구르기 · 부활 직후)이고, 치트(`cheat.god`)는 이펙트 없는 태그로 단다(언리얼 Lyra 의
         *          `Cheat.GodMode`, `Gameplay.DamageImmunity` 에 해당한다).
         */
        static TagID getInvulnerableTag();
        /** @brief 주인이 무적 태그를 가졌으면 true 입니다. */
        bool isInvulnerable() const { return _ownedTagCounts.hasMatchingTag( getInvulnerableTag() ); }

        // --------------------------------------------------------------------------
        // 4) 게임플레이 이펙트
        // --------------------------------------------------------------------------
        /** @brief 이 오브젝트를 이펙트의 시전자로 하는 컨텍스트를 만듭니다. */
        AbilityEffectContext makeEffectContext() const;
        /** @brief 정의로 이 오브젝트가 거는 스펙을 만듭니다. 컨텍스트, 레벨, 어트리뷰트 스냅샷이 채워집니다. */
        AbilityEffectSpec makeOutgoingSpec( const shared_ptr<const AbilityEffectDef>& pDef, int32 level = 1 ) const;
        /** @brief 카탈로그의 이펙트 id 로 스펙을 만듭니다. id 가 없으면 경고를 남기고 빈 스펙을 반환합니다. */
        AbilityEffectSpec makeOutgoingSpecByID( const hashed_string& effectID, int32 level = 1 ) const;
        /**
         * @brief 스펙을 이 오브젝트에 겁니다(언리얼 `ApplyGameplayEffectSpecToSelf`).
         * @return 걸렸으면 유효한 핸들(즉시 이펙트도 포함). 대상 태그 조건에 막혔거나 스펙이 비었으면 무효 핸들입니다.
         * @details 다른 오브젝트의 틱 안에서 부르지 마십시오. 그 경우에는 적용을 틱 직후로 미루는 `applyGameplayEffectSpecToTarget` 을 씁니다.
         */
        ActiveEffectHandle applyGameplayEffectSpecToSelf( const AbilityEffectSpec& spec );
        /**
         * @brief 스펙을 @p pTarget 에 겁니다. 틱 중이고 @p pTarget 이 지금 틱하는 오브젝트가 아니면 적용을 틱 직후로 미루고 무효 핸들을 반환합니다.
         * @details 미룬 적용은 대상을 핸들로 다시 찾기 때문에, 그 사이에 대상이 사라져도 안전합니다.
         */
        ActiveEffectHandle applyGameplayEffectSpecToTarget( const AbilityEffectSpec& spec, AbilityComponent* pTarget );
        /** @brief 카탈로그의 이펙트 id 로 스펙을 만들어 자기에게 겁니다. 시작 이펙트와 디버그에 씁니다. */
        ActiveEffectHandle applyGameplayEffectToSelf( const hashed_string& effectID, int32 level = 1 );
        /**
         * @brief 적용된 이펙트를 지웁니다. @p stacksToRemove 가 0 이하이면 통째로 지우고, 아니면 그만큼 스택을 줄입니다. 스택이 0 이 되면 이펙트를 지웁니다.
         * @return 무엇이든 바뀌었으면 true 입니다.
         */
        [[nodiscard]] bool removeActiveEffect( ActiveEffectHandle handle, int32 stacksToRemove = -1 );
        /** @brief 에셋 태그나 부여 태그가 @p tags 와 일치하는 이펙트를 모두 지우고, 지운 수를 반환합니다. 정화와 디스펠에 씁니다. */
        int32 removeActiveEffectsWithTags( const TagContainer& tags );
        /** @brief 적용된 이펙트를 찾습니다. 즉시 이펙트나 이미 해제된 이펙트는 nullptr 입니다. */
        const ActiveGameplayEffect* findActiveEffect( ActiveEffectHandle handle ) const;
        /** @brief 이펙트가 아직 걸려 있으면 true 입니다. */
        bool isActiveEffect( ActiveEffectHandle handle ) const { return findActiveEffect( handle ) != nullptr; }
        /** @brief 걸려 있는 이펙트 수입니다. */
        uint32 getActiveEffectCount() const;
        /** @brief 적용된 이펙트 목록입니다(적용 순서). 지우는 중인 항목(`_bPendingRemove`)도 들어 있으므로 읽는 쪽이 거릅니다. 디버그 창이 씁니다. */
        const vector<unique_ptr<ActiveGameplayEffect>>& getActiveEffects() const { return _listActiveEffect; }
        /** @brief 적용된 이펙트의 남은 시간입니다. 무한 이펙트이거나 이펙트가 없으면 0 입니다. */
        float32 getActiveEffectRemainingTime( ActiveEffectHandle handle ) const;
        /** @brief 적용된 이펙트의 스택 수입니다. 없으면 0 입니다. */
        int32 getActiveEffectStackCount( ActiveEffectHandle handle ) const;
        /** @brief 부여 태그가 @p tags 와 일치하는 지속 이펙트 중 가장 늦게 끝나는 것의 남은 시간입니다. 쿨다운 표시에 씁니다. 해당하는 이펙트가 없으면 0 입니다. */
        float32 findLongestRemainingTimeWithGrantedTags( const TagContainer& tags ) const;
        /**
         * @brief 스펙을 지금 적용해도 어떤 어트리뷰트도 0 아래로 내려가지 않으면 true 입니다. 비용 검사에 씁니다(언리얼의 `CanApplyAttributeModifiers` 에 해당).
         * @details `Add` 모디파이어만 검사합니다. 없는 어트리뷰트를 깎는 비용은 지불할 수 없으므로 false 입니다.
         */
        bool canApplyAttributeModifiers( const AbilityEffectSpec& spec ) const;
        /** @brief 이펙트가 적용된 뒤(지속, 무한, 스택 증가) 호출할 델리게이트를 등록합니다. 돌려준 구독이 사라지면 뗍니다. */
        [[nodiscard]] Subscription registerEffectApplied( const EffectDelegate& delegate );
        /** @brief 이펙트가 해제된 뒤 호출할 델리게이트를 등록합니다. 돌려준 구독이 사라지면 뗍니다. */
        [[nodiscard]] Subscription registerEffectRemoved( const EffectDelegate& delegate );

        // --------------------------------------------------------------------------
        // 5) 어빌리티
        // --------------------------------------------------------------------------
        /**
         * @brief 어빌리티를 줍니다(언리얼 `GiveAbility`). 설정에 `_bActivateOnGranted` 가 있으면 바로 발동을 시도합니다.
         * @param inputID `abilityInputPressed` 로 발동할 번호(`kNoInputID` 면 입력 없음)
         */
        AbilitySpecHandle giveAbility( unique_ptr<Ability> pAbility, int32 level = 1, int32 inputID = kNoInputID );
        /** @brief 카탈로그의 어빌리티 id 로 만들어 줍니다. id 가 없으면 경고를 남기고 무효 핸들을 반환합니다. */
        AbilitySpecHandle giveAbilityByID( const hashed_string& abilityID, int32 level = 1, int32 inputID = kNoInputID );
        /** @brief 어빌리티를 제거합니다. 실행 중이면 취소한 뒤 제거합니다. */
        bool clearAbility( AbilitySpecHandle handle );
        /** @brief 모든 어빌리티를 제거합니다. */
        void clearAllAbilities();
        /** @brief 조건을 검사한 뒤 발동합니다. 실패한 이유는 반환값에 담습니다. */
        AbilityActivationResult tryActivateAbility( AbilitySpecHandle handle, const AbilityEventData* pTriggerEvent = nullptr );
        /** @brief 발동할 수 있는지만 검사합니다. 상태를 바꾸지 않으므로 UI 버튼의 비활성 표시에 쓸 수 있습니다. */
        AbilityActivationResult canActivateAbility( AbilitySpecHandle handle, const AbilityEventData* pTriggerEvent = nullptr ) const;
        /** @brief 어빌리티 태그가 @p tags 와 일치하는 어빌리티를 모두 발동해 봅니다. 하나라도 발동했으면 true 입니다. */
        [[nodiscard]] bool tryActivateAbilitiesByTag( const TagContainer& tags );
        /** @brief 카탈로그 id(`AbilityConfig::_id`)로 부여된 어빌리티의 핸들을 찾습니다. 없으면 무효 핸들입니다. */
        AbilitySpecHandle findAbilitySpecHandle( const hashed_string& abilityID ) const;
        /** @brief 부여된 어빌리티를 찾습니다. 없거나 제거하는 중이면 nullptr 입니다. */
        Ability* findAbility( AbilitySpecHandle handle ) const;
        /** @brief 어빌리티가 실행 중이면 true 입니다. */
        bool isAbilityActive( AbilitySpecHandle handle ) const;
        /** @brief 어빌리티를 취소합니다. */
        void cancelAbility( AbilitySpecHandle handle );
        /** @brief 어빌리티 태그가 @p tags 와 일치하는 실행 중 어빌리티를 취소합니다. @p pIgnore 는 취소하지 않습니다. */
        void cancelAbilitiesWithTags( const TagContainer& tags, const Ability* pIgnore = nullptr );
        /** @brief 실행 중인 어빌리티를 모두 취소합니다. */
        void cancelAllAbilities();
        /** @brief 부여된 어빌리티 수입니다. 제거하는 중인 것은 세지 않습니다. */
        uint32 getAbilityCount() const;
        /** @brief 부여된 어빌리티의 핸들을 부여한 순서대로 채웁니다. */
        void getAbilitySpecHandles( vector<AbilitySpecHandle>& outListHandle ) const;
        /** @brief 입력 @p inputID 가 눌렸을 때 부릅니다. 그 번호의 어빌리티가 실행 중이면 알리고, 아니면 발동을 시도합니다. */
        void abilityInputPressed( int32 inputID );
        /** @brief 입력 @p inputID 를 뗐을 때 부릅니다. */
        void abilityInputReleased( int32 inputID );
        /** @brief 어빌리티가 발동한 뒤 호출할 델리게이트를 등록합니다. 돌려준 구독이 사라지면 뗍니다. */
        [[nodiscard]] Subscription registerAbilityActivated( const AbilityActivatedDelegate& delegate );
        /** @brief 어빌리티가 끝난 뒤 호출할 델리게이트를 등록합니다. 두 번째 인자는 취소로 끝났는지 여부입니다. 돌려준 구독이 사라지면 뗍니다. */
        [[nodiscard]] Subscription registerAbilityEnded( const AbilityEndedDelegate& delegate );

        // --------------------------------------------------------------------------
        // 6) 게임플레이 이벤트와 큐
        // --------------------------------------------------------------------------
        /**
         * @brief 게임플레이 이벤트를 이 오브젝트에 보냅니다(언리얼의 `HandleGameplayEvent` 에 해당). 트리거 태그가 일치하는 어빌리티가 발동하고, 구독자가 이벤트를 받습니다.
         * @return 이 이벤트로 발동한 어빌리티 수
         */
        int32 handleGameplayEvent( TagID eventTag, const AbilityEventData& payload );
        /**
         * @brief @p pTarget 에 게임플레이 이벤트를 보냅니다(언리얼의 `SendGameplayEventToActor` 에 해당). 틱 중이고 대상이 지금 틱하는 오브젝트가 아니면 틱 직후로 미룹니다.
         */
        static void sendGameplayEventToTarget( AbilityComponent* pTarget, TagID eventTag, const AbilityEventData& payload );
        /** @brief @p eventTag 나 그 하위 태그의 이벤트가 오면 호출할 델리게이트를 등록합니다. 돌려준 구독이 사라지면 뗍니다. 태그가 무효면 빈 구독입니다. */
        [[nodiscard]] Subscription registerGameplayEvent( TagID eventTag, const AbilityEventDelegate& delegate );
        /** @brief 이 오브젝트의 이펙트가 큐를 낼 때마다 호출할 델리게이트를 등록합니다. 채널 이벤트보다 먼저 호출됩니다. 돌려준 구독이 사라지면 뗍니다. */
        [[nodiscard]] Subscription registerGameplayCue( const AbilityPresentationDelegate& delegate );

        // --------------------------------------------------------------------------
        // 7) 표시 설정
        // --------------------------------------------------------------------------
        void                 setShowDamageNumbers( bool bShow ) { _bShowDamageNumbers = bShow; }
        bool                 showsDamageNumbers() const { return _bShowDamageNumbers; }
        void                 setHealthAttributes( const hashed_string& health, const hashed_string& maxHealth );
        const hashed_string& getHealthAttribute() const { return _healthAttribute; }
        const hashed_string& getMaxHealthAttribute() const { return _maxHealthAttribute; }
        /** @brief 체력 원천의 읽기 값입니다. `_healthAttribute` 와 `_maxHealthAttribute` 의 current 를 읽고, 체력이 0 이하이면 쓰러진 것으로 봅니다. 체력 어트리뷰트가 없으면 `_bHasHealth` 가 SW_FALSE 입니다. */
        HealthReading getHealthReading() const override;

    private:
        /** @brief 부여된 어빌리티 하나입니다(언리얼의 `FGameplayAbilitySpec` 에 해당). 주소가 바뀌지 않도록 `unique_ptr` 로 보관한다. */
        struct AbilitySpec
        {
            AbilitySpecHandle   _handle{};
            unique_ptr<Ability> _pAbility{};
            int32               _level{ 1 };
            int32               _inputID{ kNoInputID };
            uint8               _bPendingRemove{ SW_FALSE };
        };

        /** @brief 스코프가 끝날 때까지 활성 이펙트와 스펙 목록에서 원소를 빼지 않게 합니다. 제거 요청은 표시만 해 두고 스코프가 끝날 때 실제로 지웁니다. */
        class ScopedListLock
        {
        public:
            explicit ScopedListLock( AbilityComponent& owner );
            ~ScopedListLock();
            ScopedListLock( const ScopedListLock& )            = delete;
            ScopedListLock& operator=( const ScopedListLock& ) = delete;

        private:
            AbilityComponent& _owner;
        };

        // ---- 이펙트 ----
        /** @brief 태그 조건상 지금 대상에게 걸 수 있으면 true 입니다. */
        bool canApplySpec( const AbilityEffectSpec& spec ) const;
        /** @brief 모디파이어 하나의 크기를 계산합니다. 계산하지 못하면 false 입니다. */
        bool computeModifierMagnitude( const AbilityEffectModifier& modifier, const AbilityEffectSpec& spec, float32& outMagnitude ) const;
        /** @brief 스펙의 모디파이어와 실행 계산을 돌려, base 에 쓸 변경 목록을 만듭니다. 스택 하나에 해당하는 양입니다. */
        void evaluateExecutionModifiers( const AbilityEffectSpec& spec, int32 stackCount, vector<EvaluatedModifier>& outListModifier ) const;
        /** @brief 즉시 이펙트나 주기 이펙트를 한 번 실행합니다. base 를 바꾸고 세트의 훅과 큐를 부릅니다. */
        void executeSpec( const AbilityEffectSpec& spec, int32 stackCount );
        /** @brief base 에 변경 하나를 씁니다. 클램프, 집계, 훅 호출까지 처리합니다. */
        void applyModifierToBase( const EvaluatedModifier& modifier, float32 stackScale, const AbilityEffectSpec& spec );
        /** @brief 지속 이펙트나 무한 이펙트를 새로 걸거나 스택을 올립니다. */
        ActiveEffectHandle applyDurationSpec( const AbilityEffectSpec& spec );
        /** @brief 적용된 이펙트를 활성 목록에서 해제합니다. 태그, 집계, 큐, 알림을 함께 정리합니다. */
        void removeActiveEffectInternal( ActiveGameplayEffect& activeEffect );
        /**
         * @brief 걸린 이펙트가 대상에 주는 것(부여 태그 · 부여 어빌리티 · 모디파이어 집계)을 붙입니다. `disengageActiveEffect` 와 짝입니다.
         * @details 걸 때 · 해제할 때 · 걸린 동안의 태그 조건이 바뀔 때가 모두 이 짝 하나를 지나므로, 주는 것이 하나 늘어도 빠뜨릴 곳이 없습니다.
         */
        void engageActiveEffect( ActiveGameplayEffect& activeEffect );
        /** @brief `engageActiveEffect` 가 붙인 것을 뗍니다. */
        void disengageActiveEffect( ActiveGameplayEffect& activeEffect );
        /** @brief 걸린 동안의 태그 조건(`_ongoingRequiredTags`)을 지금 태그로 다시 보고, 멈춤 · 재개가 바뀐 이펙트를 떼고 붙입니다. */
        void refreshOngoingTagRequirements();
        /** @brief 가진 태그가 생기거나 사라질 때 부릅니다(`_ownedTagCounts` 구독). 걸린 동안의 조건을 다시 보고, 생긴 태그로 발동하는 어빌리티를 발동합니다. */
        void onOwnedTagChanged( TagID tag, int32 newCount );
        /** @brief 적용된 이펙트를 찾습니다. 지우는 중인 것은 제외합니다. */
        ActiveGameplayEffect* findActiveEffectMutable( ActiveEffectHandle handle );
        /** @brief 같은 정의로 걸려 있는 스택 이펙트를 찾습니다. */
        ActiveGameplayEffect* findStackableEffect( const AbilityEffectDef& def );
        /** @brief 적용된 이펙트들의 지속 시간과 주기를 @p deltaTime 만큼 진행합니다. */
        void updateActiveEffects( float32 deltaTime );
        /** @brief 이펙트 하나의 큐를 내보냅니다. */
        void dispatchCues( const AbilityEffectSpec& spec, AbilityPresentationPhase phase, float32 magnitude );
        /** @brief 카탈로그의 큐 정의(`<AbilityPresentation>`)가 있으면 그 연출(프리팹 · 소리)을 틱 뒤에 냅니다. 클라이언트 코드가 없는 빌드에서는 아무것도 하지 않습니다. */
        void playCueDef( TagID cueTag, AbilityPresentationPhase phase ) const;
        /** @brief 지우라고 표시된 이펙트와 스펙을 실제로 지웁니다. 잠금이 풀렸을 때만 실행합니다. */
        void compactPendingRemovals();

        // ---- 어트리뷰트 ----
        /** @brief 어트리뷰트 하나의 current 를 base 와 적용된 이펙트로 다시 집계하고, 바뀌었으면 알립니다. */
        void recomputeAttribute( const hashed_string& name );
        /** @brief 세트 안의 값을 찾습니다. 쓰기용입니다. */
        AttributeData* findAttributeDataMutable( const hashed_string& name, AttributeSet** ppOutSet );
        /** @brief current 가 바뀐 것을 알립니다. 세트의 훅, 델리게이트, 체력 UI 순서로 알립니다. */
        void notifyAttributeChanged( AttributeSet& attributeSet, const hashed_string& name, float32 oldValue, float32 newValue );

        // ---- 태그 ----
        /** @brief 어빌리티 태그 중 하나라도 막혀 있으면 true 입니다. */
        bool isAbilityBlocked( const TagContainer& abilityTags ) const;

        // ---- 어빌리티 ----
        AbilitySpec*       findAbilitySpecMutable( AbilitySpecHandle handle );
        const AbilitySpec* findAbilitySpec( AbilitySpecHandle handle ) const;
        /** @brief 발동이 확정된 어빌리티의 시작을 처리합니다. 태그, 취소, 막기, 알림이 해당합니다. */
        void handleAbilityActivated( Ability& ability );
        /** @brief 어빌리티가 끝났을 때를 처리합니다. 태그, 막기 해제, 알림이 해당합니다. */
        void handleAbilityEnded( Ability& ability, bool bWasCancelled );
        /** @brief 실행 중인 어빌리티들을 틱합니다. */
        void updateActiveAbilities( float32 deltaTime );
        /** @brief 제거할 스펙을 정리합니다. 취소하고 `onRemoved` 를 부른 뒤 제거 표시를 합니다. */
        void retireAbilitySpec( AbilitySpec& spec );

        // ---- 스레드와 연결된 UI ----
        /**
         * @brief 이 컴포넌트를 지금 바꿔도 되면 true 입니다. 틱 중이고 지금 틱하는 오브젝트가 주인이 아니면 false 입니다.
         *        다른 워커가 이 오브젝트를 틱하고 있을 수 있기 때문입니다.
         */
        bool canMutateNow() const;
        /** @brief 깎인 체력 @p amount 를 피해 숫자로 띄웁니다. 틱 중이면 틱 직후에 띄웁니다. */
        void spawnDamageNumber( float32 amount );
        /** @brief base 를 쓰기 직전에 부릅니다. @p name 이 체력이고 무적이면 내려가는 @p inoutNewBase 를 @p oldBase 로 되돌립니다(`getInvulnerableTag`). */
        void guardHealthBaseChange( const hashed_string& name, float32 oldBase, float32& inoutNewBase );

        // ---- 런타임 상태(저장하지 않는다) ----
        vector<unique_ptr<AttributeSet>>                                                                _listAttributeSet;
        vector<unique_ptr<ActiveGameplayEffect>>                                                        _listActiveEffect;
        vector<unique_ptr<AbilitySpec>>                                                                 _listAbilitySpec;
        TagCountTable                                                                                   _ownedTagCounts;
        Subscription                                                                                    _ownedTagSubscription;    ///< `_ownedTagCounts` 의 변화를 자기가 듣는 구독(`onOwnedTagChanged`)
        TagCountTable                                                                                   _blockedAbilityTagCounts; ///< `_blockAbilitiesWithTags` 가 막은 어빌리티 태그(알림 없이 센다)
        unordered_map<hashed_string, unique_ptr<MulticastDelegate<void( const AttributeChangeData& )>>> _mapAttributeChangedMulticast;
        unordered_map<uint64, unique_ptr<MulticastDelegate<void( const AbilityEventData& )>>>           _mapGameplayEventMulticast;
        MulticastDelegate<void( const AttributeChangeData& )>                                           _anyAttributeChangedMulticast;
        MulticastDelegate<void( const ActiveGameplayEffect& )>                                          _effectAppliedMulticast;
        MulticastDelegate<void( const ActiveGameplayEffect& )>                                          _effectRemovedMulticast;
        MulticastDelegate<void( const Ability& )>                                                       _abilityActivatedMulticast;
        MulticastDelegate<void( const Ability&, bool )>                                                 _abilityEndedMulticast;
        MulticastDelegate<void( const AbilityPresentationEvent& )>                                      _gameplayCueMulticast;
        const AbilityCatalog*                                                                           _pCatalog;
        float32                                                                                         _time;
        uint64                                                                                          _nextHandleID;
        uint32                                                                                          _listLockDepth;
        uint32                                                                                          _blockedHealthDecreaseCount; ///< 무적이 체력 내림을 막은 횟수(`guardHealthBaseChange`) — `executeSpec` 이 앞뒤를 견줘 막힌 피격의 즉시 큐를 거른다
        uint8                                                                                           _bHasPendingRemoval;
        uint8                                                                                           _bRefreshingOngoingTags; ///< `refreshOngoingTagRequirements` 안이다 — 그 안에서 바뀐 태그로 다시 들어가지 않는다

        // ---- 저장되는 설정 ----
        PROPERTY( Category = "Ability System", DisplayName = "Ability Set", Tooltip = "Ability set id in the AbilityCatalog, granted on begin play" )
        hashed_string _abilitySetID;
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
