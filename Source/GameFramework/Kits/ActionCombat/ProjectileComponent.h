#pragma once
#include "Core/Math/Math.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    namespace generated
    {
        struct sw_ProjectileComponent_Registrar;
    } // namespace generated

    REFLECT()
    class SW_GF_API ProjectileComponent : public Component
    {
        friend struct ::sw::generated::sw_ProjectileComponent_Registrar;

    public:
        REFLECT_BODY();
        ProjectileComponent();
        virtual ~ProjectileComponent() override                          = default;
        ProjectileComponent( ProjectileComponent&& ) noexcept            = default;
        ProjectileComponent& operator=( ProjectileComponent&& ) noexcept = default;

        void onBeginPlay() override;
        void onEndPlay() override;
        void onTick( float32 deltaTime ) override;

        void setVelocity( const float2& velocity );
        void setDamage( int32 damage );
        void setLifeTime( float32 lifeTime );

    private:
        PROPERTY( Alias = "velocity" )
        float2 _velocity;
        PROPERTY( Alias = "damage" )
        int32 _damage;
        PROPERTY( Alias = "lifeTime" )
        float32 _lifeTime;
        PROPERTY( Alias = "currentLife" )
        float32 _currentLife;
    };
} // namespace sw
