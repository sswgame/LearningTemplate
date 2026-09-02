#pragma once
#include "Core/Math/Math.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    namespace generated
    {
        struct sw_UnitStatsComponent_Registrar;
    } // namespace generated

    REFLECT( Category = "Gameplay", DisplayName = "Unit Stats Component", Tooltip = "Manages HP, Attack, Defense, Movement Speed, and Invincibility" )
    class SW_GF_API UnitStatsComponent : public Component
    {
        friend struct ::sw::generated::sw_UnitStatsComponent_Registrar;

    public:
        REFLECT_BODY();
        UnitStatsComponent();
        virtual ~UnitStatsComponent() override                         = default;
        UnitStatsComponent( UnitStatsComponent&& ) noexcept            = default;
        UnitStatsComponent& operator=( UnitStatsComponent&& ) noexcept = default;

        void onBeginPlay() override;
        void onEndPlay() override;
        void onTick( float32 deltaTime ) override;

        void takeDamage( int32 amount );
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
        void applyTakeDamage( int32 amount );
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
