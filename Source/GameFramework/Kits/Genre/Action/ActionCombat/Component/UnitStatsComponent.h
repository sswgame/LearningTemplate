#pragma once
#include "Core/Container/GameObjectHandle.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Math/Math.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Character/Combat/Health/HealthSourceComponent.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct DamageAppliedEvent;
    struct MonsterStatsDef;

    REFLECT( Category = "Gameplay", DisplayName = "Unit Stats Component", Tooltip = "Manages HP, Attack, Defense, Movement Speed, and Invincibility" )
    class SW_KIT_API UnitStatsComponent : public HealthSourceComponent
    {
    public:
        REFLECT_BODY();
        /** @brief 피해가 HP 에 닿는 시점에 호출되는 델리게이트입니다(`registerDamageApplied`). */
        using DamageAppliedDelegate = Delegate<void( const DamageAppliedEvent& )>;

        UnitStatsComponent();
        virtual ~UnitStatsComponent() override = default;

        void onBeginPlay() override;
        void onTick( float32 deltaTime ) override;

        /**
         * @brief 피해를 줍니다. 투사체, 공격 판정, 게임 코드가 모두 이 함수 하나를 지납니다(언리얼 `AActor::TakeDamage` 에 해당).
         * @details 방어력을 빼고 최소 1을 깎은 뒤 무적 시간을 건다(`DamageMath::applyArmor`). 죽었거나 무적이거나 @p amount 가 0 이하면 아무것도 하지 않는다. HP 가 깎였으면 바로
         *          `registerDamageApplied` 의 델리게이트를 부르고 `DamageAppliedEvent` 를 "game" 채널에 낸다(`GameEventUtil::send`). 틱 중(구조 동결)이면 틱 직후로 미루며, 미룬 피해도 @p instigator 를 유지한다.
         * @param instigator 피해를 낸 쪽(쏘거나 휘두른 오브젝트). 이벤트에 그대로 실린다. 모르면 무효 핸들
         */
        void takeDamage( int32 amount, GameObjectHandle instigator = GameObjectHandle{} );
        /** @brief 근접 판정의 맞음(`MeleeHitWindowUtil::sweep`)을 피해로 바꿉니다. 피해는 기본 피해 × 히트 존 배율을 반올림한 값이고 때린 쪽은 맞음의 공격자입니다. */
        void onHitReceived( const HitInfo& hit ) override;
        void heal( int32 amount );

        /**
         * @brief HP 가 깎일 때 호출할 델리게이트를 등록합니다(언리얼 `OnTakeAnyDamage` 처럼 액터에 붙는 멀티캐스트입니다).
         * @details 피해는 게임 스레드에서 적용된다. 틱 중의 피해는 틱 직후(같은 프레임)로 미뤄지고, 근접 판정의 전달과 게임 코드도 게임 스레드에서 돈다.
         *          그래서 구독자는 같은 프레임에 받는다. 구독은 저장되지 않으므로, 컴포넌트를 다시 만들면(되돌리기, 핫 리로드) 다시 등록해야 한다.
         * @return 등록을 소유하는 구독. 사라지면 해제된다
         */
        [[nodiscard]] Subscription registerDamageApplied( const DamageAppliedDelegate& delegate );

        FUNCTION( Category = "Actions", DisplayName = "Heal 20 HP", CallInEditor )
        void heal20() { heal( 20 ); }

        int32 getHp() const { return _hp; }
        int32 getMaxHp() const { return _maxHp; }
        int32 getAttack() const { return _attack; }
        int32 getDefense() const { return _defense; }
        /** @brief 기본 이동 속도입니다. 단위는 초당 월드 유닛(m/s)이며, 이동 코드는 프레임 시간을 곱해서 씁니다(`getMoveSpeed() * deltaTime`). */
        float32 getMoveSpeed() const { return _moveSpeed; }
        bool    isDead() const { return _bIsDead; }
        /** @brief 체력 원천의 읽기 뷰입니다. HP, 최대 HP, 죽음을 그대로 전달합니다. */
        HealthReading getHealthReading() const override;

        /** @brief 스탯을 한 번에 정합니다. 같은 오브젝트의 HP 바는 새 비율로 다시 맞춥니다(변화 연출 없이). */
        void setStats( int32 hp, int32 maxHp, int32 attack, int32 defense, float32 moveSpeed, float32 maxInvincibilityTime );
        /** @brief 몬스터 카탈로그의 한 종의 스탯(`MonsterDef::_stats`)으로 정합니다. 단위는 같습니다(이동 속도는 m/s, 무적 시간은 s). */
        void setStats( const MonsterStatsDef& monsterStats );

        /** @brief 깎인 피해를 데미지 숫자로 띄울지 정합니다(기본은 끔). 숫자는 `getDamageNumberOffset()` 만큼 위에 새 오브젝트로 뜹니다. */
        void setShowDamageNumbers( bool bShow ) { _bShowDamageNumbers = bShow; }
        /** @brief 깎인 피해를 데미지 숫자로 띄우는지 반환합니다. */
        bool showsDamageNumbers() const { return _bShowDamageNumbers; }
        /** @brief 데미지 숫자가 뜨는 위치입니다(유닛의 월드 위치 기준 오프셋). */
        const float3& getDamageNumberOffset() const { return _damageNumberOffset; }

    private:
        /** @brief 피해를 지금 적용하고, 깎였으면 `DamageAppliedEvent` 를 냅니다. 피해가 HP 에 닿는 유일한 경로입니다. */
        void applyTakeDamage( int32 amount, GameObjectHandle instigator );
        void applyHeal( int32 amount );
        /** @brief 깎인 피해 @p amount 를 데미지 숫자 오브젝트로 띄웁니다(`_bShowDamageNumbers` 가 true 일 때). */
        void spawnDamageNumber( int32 amount );

        /** @brief HP 가 깎일 때 호출할 구독자 목록입니다(`registerDamageApplied`). 코드가 등록하는 값이므로 저장하지 않습니다. */
        MulticastDelegate<void( const DamageAppliedEvent& )> _damageAppliedMulticast;
        PROPERTY( Category = "Stats", DisplayName = "HP", Tooltip = "Current Health Points", Min = 0.0, Meta = "Units=HP" )
        int32 _hp;
        PROPERTY( Category = "Stats", DisplayName = "Max HP", Tooltip = "Maximum Health Points", Min = 1.0, Meta = "Units=HP" )
        int32 _maxHp;
        PROPERTY( Category = "Stats", DisplayName = "Attack", Tooltip = "Attack power", Min = 0.0 )
        int32 _attack;
        PROPERTY( Category = "Stats", DisplayName = "Defense", Tooltip = "Defense rating", Min = 0.0 )
        int32 _defense;
        PROPERTY( Category = "Movement", DisplayName = "Move Speed", Tooltip = "Base movement speed in world units per second", Min = 0.0, Max = 50.0, Units = "m/s" )
        float32 _moveSpeed;
        PROPERTY( Category = "Combat", DisplayName = "Invincibility Timer", Tooltip = "Remaining invincibility time", Transient, ReadOnly, Units = s )
        float32 _invincibilityTime;
        PROPERTY( Category = "Combat", DisplayName = "Max Invincibility Time", Tooltip = "Duration of invincibility after taking damage", Min = 0.0, Max = 10.0, Units = s )
        float32 _maxInvincibilityTime;
        PROPERTY( Category = "State", DisplayName = "Is Dead", Tooltip = "Whether the unit is currently dead", ReadOnly )
        bool _bIsDead;
        PROPERTY( Category = "Feedback", DisplayName = "Show Damage Numbers", Tooltip = "Spawn a floating damage number for each hit" )
        bool _bShowDamageNumbers;
        PROPERTY( Category = "Feedback", DisplayName = "Damage Number Offset", Tooltip = "Where damage numbers appear, relative to the unit", Units = m )
        float3 _damageNumberOffset;
    };
} // namespace sw
