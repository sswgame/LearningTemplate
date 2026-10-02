#pragma once
#include "Core/Container/GameObjectHandle.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Math/Math.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/ActionCombat/ActionCombatEvents.h"

namespace sw
{
    REFLECT( Category = "Gameplay", DisplayName = "Unit Stats Component", Tooltip = "Manages HP, Attack, Defense, Movement Speed, and Invincibility" )
    class SW_GF_API UnitStatsComponent : public Component
    {
    public:
        REFLECT_BODY();
        /** @brief 피해가 HP 에 닿은 그 자리에서 불리는 델리게이트입니다(`registerDamageApplied`). */
        using DamageAppliedDelegate = Delegate<void( const DamageAppliedEvent& )>;

        UnitStatsComponent();
        virtual ~UnitStatsComponent() override = default;

        void onBeginPlay() override;
        void onEndPlay() override;
        void onTick( float32 deltaTime ) override;

        /**
         * @brief 피해를 줍니다 — 투사체 · 공격 판정 · 게임 코드가 모두 이 하나를 지납니다(언리얼 `AActor::TakeDamage`).
         * @details 방어력을 빼고 최소 1 을 깎은 뒤 무적 시간을 겁니다. 죽었거나 무적이면 아무것도 하지 않습니다. HP 가 깎였으면 그 자리에서
         *          `registerDamageApplied` 의 델리게이트를 부르고 `DamageAppliedEvent` 를 "game" 채널에 냅니다(`GameEventUtil::send`). 틱 중(구조 동결)이면
         *          틱 직후로 미루고, 미룬 것도 @p instigator 를 들고 갑니다.
         * @param instigator 피해를 낸 쪽(쏜 · 휘두른 오브젝트). 이벤트에 그대로 실립니다. 모르면 무효 핸들
         */
        void takeDamage( int32 amount, GameObjectHandle instigator = GameObjectHandle{} );
        void heal( int32 amount );

        /**
         * @brief HP 가 깎인 그 자리에서 부를 델리게이트를 겁니다(언리얼 `OnTakeAnyDamage` — 액터에 붙은 멀티캐스트).
         * @details 피해는 게임 스레드에서 적용됩니다 — 틱 중의 피해는 틱 직후(같은 프레임)로 미뤄지고, 겹침 전달 · 게임 코드도 게임 스레드에서 돈다.
         *          그래서 구독자는 같은 프레임에 받습니다. 구독은 저장되지 않습니다 — 컴포넌트가 다시 만들어지면(되돌리기 · 핫 리로드) 다시 겁니다.
         * @return 뗄 때 쓰는 핸들
         */
        DelegateHandle registerDamageApplied( const DamageAppliedDelegate& delegate );
        /** @brief `registerDamageApplied` 로 건 델리게이트를 뗍니다. */
        void unregisterDamageApplied( DelegateHandle handle );

        FUNCTION( Category = "Actions", DisplayName = "Heal 20 HP", CallInEditor )
        void heal20() { heal( 20 ); }

        int32   getHp() const { return _hp; }
        int32   getMaxHp() const { return _maxHp; }
        int32   getAttack() const { return _attack; }
        int32   getDefense() const { return _defense; }
        float32 getMoveSpeed() const { return _moveSpeed; }
        bool    isDead() const { return _bIsDead; }

        /** @brief 스탯을 한 번에 정합니다. 같은 오브젝트의 HP 바는 새 비율로 다시 맞춥니다(흔적 없이). */
        void setStats( int32 hp, int32 maxHp, int32 attack, int32 defense, float32 moveSpeed, float32 maxInvincibilityTime );

        /** @brief 깎인 피해를 데미지 숫자로 띄울지 정합니다(기본 꺼짐). 숫자는 `getDamageNumberOffset()` 만큼 위에 새 오브젝트로 뜬다. */
        void setShowDamageNumbers( bool bShow ) { _bShowDamageNumbers = bShow; }
        /** @brief 깎인 피해를 데미지 숫자로 띄우는지 반환합니다. */
        bool showsDamageNumbers() const { return _bShowDamageNumbers; }
        /** @brief 데미지 숫자가 뜨는 자리(유닛의 월드 위치 기준 오프셋)입니다. */
        const float3& getDamageNumberOffset() const { return _damageNumberOffset; }

    private:
        /** @brief 피해를 지금 적용하고 깎였으면 `DamageAppliedEvent` 를 냅니다. 피해가 HP 에 닿는 유일한 자리입니다. */
        void applyTakeDamage( int32 amount, GameObjectHandle instigator );
        void applyHeal( int32 amount );
        /**
         * @brief 같은 오브젝트의 HP 바(`HPBarBaseComponent`)를 지금 HP 비율로 맞춥니다. @p bReset 이면 흔적 없이(시작 · 스탯 재설정), 아니면 목표만(피해 · 회복).
         * @details HP 가 바뀌는 자리(피해 · 회복 · 스탯 설정 · 시작)가 이것을 부른다 — 예전에는 HP 바를 아무도 움직이지 않았다(`setTargetRatio` 를 부르는 곳이 없었다).
         */
        void syncHealthBar( bool bReset );
        /** @brief 깎인 피해 @p amount 를 데미지 숫자 오브젝트로 띄웁니다(`_bShowDamageNumbers` 일 때). */
        void spawnDamageNumber( int32 amount );

        /** @brief HP 가 깎일 때 부를 구독자들입니다(`registerDamageApplied`). 저장하지 않는다 — 코드가 거는 것이다. */
        MulticastDelegate<void( const DamageAppliedEvent& )> _damageAppliedMulticast;
        PROPERTY( Category = "Stats", DisplayName = "HP", Tooltip = "Current Health Points", Min = 0.0, Meta = "Units=HP", Alias = "hp" )
        int32 _hp;
        PROPERTY( Category = "Stats", DisplayName = "Max HP", Tooltip = "Maximum Health Points", Min = 1.0, Meta = "Units=HP", Alias = "maxHp" )
        int32 _maxHp;
        PROPERTY( Category = "Stats", DisplayName = "Attack", Tooltip = "Attack power", Min = 0.0, Alias = "attack" )
        int32 _attack;
        PROPERTY( Category = "Stats", DisplayName = "Defense", Tooltip = "Defense rating", Min = 0.0, Alias = "defense" )
        int32 _defense;
        PROPERTY( Category = "Movement", DisplayName = "Move Speed", Tooltip = "Base movement speed in tiles/sec", Min = 0.0, Max = 50.0, Meta = "Units=m/s", Alias = "moveSpeed" )
        float32 _moveSpeed;
        PROPERTY( Category = "Combat", DisplayName = "Invincibility Timer", Tooltip = "Remaining invincibility time", Transient, ReadOnly, Meta = "Units=s", Alias = "invincibilityTime" )
        float32 _invincibilityTime;
        PROPERTY( Category = "Combat", DisplayName = "Max Invincibility Time", Tooltip = "Duration of invincibility after taking damage", Min = 0.0, Max = 10.0, Meta = "Units=s", Alias = "maxInvincibilityTime" )
        float32 _maxInvincibilityTime;
        PROPERTY( Category = "State", DisplayName = "Is Dead", Tooltip = "Whether the unit is currently dead", ReadOnly, Alias = "bIsDead" )
        bool _bIsDead;
        PROPERTY( Category = "Feedback", DisplayName = "Show Damage Numbers", Tooltip = "Spawn a floating damage number for each hit" )
        bool _bShowDamageNumbers;
        PROPERTY( Category = "Feedback", DisplayName = "Damage Number Offset", Tooltip = "Where damage numbers appear, relative to the unit", Meta = "Units=m" )
        float3 _damageNumberOffset;
    };
} // namespace sw
