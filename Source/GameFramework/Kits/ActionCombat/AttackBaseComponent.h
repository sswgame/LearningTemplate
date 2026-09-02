#pragma once
#include "Core/Math/Math.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    namespace generated
    {
        struct sw_AttackBaseComponent_Registrar;
    } // namespace generated

    REFLECT()
    class SW_GF_API AttackBaseComponent : public Component
    {
        friend struct ::sw::generated::sw_AttackBaseComponent_Registrar;

    public:
        REFLECT_BODY();
        AttackBaseComponent();
        virtual ~AttackBaseComponent() override                          = default;
        AttackBaseComponent( AttackBaseComponent&& ) noexcept            = default;
        AttackBaseComponent& operator=( AttackBaseComponent&& ) noexcept = default;

        void onBeginPlay() override;
        void onEndPlay() override;
        void onTick( float32 deltaTime ) override;

        bool  isAttackActive() const { return _bIsAttacking; }
        int32 getDamage() const { return _damage; }
        void  beginAttack( int32 damage, float32 duration );

    private:
        PROPERTY( Alias = "damage" )
        int32 _damage;
        PROPERTY( Alias = "duration" )
        float32 _duration;
        PROPERTY( Alias = "currentDuration" )
        float32 _currentDuration;
        PROPERTY( Alias = "bIsAttacking" )
        bool _bIsAttacking;
    };
} // namespace sw
