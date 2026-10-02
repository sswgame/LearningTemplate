#pragma once
#include "Core/Container/GameObjectHandle.h"
#include "Core/Math/Math.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    REFLECT( Category = "Gameplay", DisplayName = "Unit Stats Component", Tooltip = "Manages HP, Attack, Defense, Movement Speed, and Invincibility" )
    class SW_GF_API UnitStatsComponent : public Component
    {
    public:
        REFLECT_BODY();
        UnitStatsComponent();
        virtual ~UnitStatsComponent() override = default;

        void onBeginPlay() override;
        void onEndPlay() override;
        void onTick( float32 deltaTime ) override;

        /**
         * @brief 피해를 줍니다 — 투사체 · 공격 판정 · 게임 코드가 모두 이 하나를 지납니다(언리얼 `AActor::TakeDamage`).
         * @details 방어력을 빼고 최소 1 을 깎은 뒤 무적 시간을 겁니다. 죽었거나 무적이면 아무것도 하지 않습니다. HP 가 깎였으면
         *          `DamageAppliedEvent` 를 "game" 채널 큐에 싣습니다. 틱 중(구조 동결)이면 틱 직후로 미루고, 미룬 것도 @p instigator 를 들고 갑니다.
         * @param instigator 피해를 낸 쪽(쏜 · 휘두른 오브젝트). 이벤트에 그대로 실립니다. 모르면 무효 핸들
         */
        void takeDamage( int32 amount, GameObjectHandle instigator = GameObjectHandle{} );
        void heal( int32 amount );

        FUNCTION( Category = "Actions", DisplayName = "Heal 20 HP", CallInEditor )
        void heal20() { heal( 20 ); }

        int32   getHp() const { return _hp; }
        int32   getMaxHp() const { return _maxHp; }
        int32   getAttack() const { return _attack; }
        int32   getDefense() const { return _defense; }
        float32 getMoveSpeed() const { return _moveSpeed; }
        bool    isDead() const { return _bIsDead; }

        void setStats( int32 hp, int32 maxHp, int32 attack, int32 defense, float32 moveSpeed, float32 maxInvincibilityTime )
        {
            _hp                   = hp;
            _maxHp                = maxHp;
            _attack               = attack;
            _defense              = defense;
            _moveSpeed            = moveSpeed;
            _maxInvincibilityTime = maxInvincibilityTime;
        }

    private:
        /** @brief 피해를 지금 적용하고 깎였으면 `DamageAppliedEvent` 를 냅니다. 피해가 HP 에 닿는 유일한 자리입니다. */
        void applyTakeDamage( int32 amount, GameObjectHandle instigator );
        void applyHeal( int32 amount );

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
    };
} // namespace sw
