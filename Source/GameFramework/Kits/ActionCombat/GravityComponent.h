#pragma once
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    namespace generated
    {
        struct sw_GravityComponent_Registrar;
    } // namespace generated

    REFLECT()
    class SW_GF_API GravityComponent : public Component
    {
        friend struct ::sw::generated::sw_GravityComponent_Registrar;

    public:
        REFLECT_BODY();
        GravityComponent();
        virtual ~GravityComponent() override                       = default;
        GravityComponent( GravityComponent&& ) noexcept            = default;
        GravityComponent& operator=( GravityComponent&& ) noexcept = default;

        void onBeginPlay() override;
        void onEndPlay() override;
        void onTick( float32 deltaTime ) override;

    private:
        PROPERTY( Alias = "gravity" )
        float32 _gravity;
        PROPERTY( Alias = "velocityY" )
        float32 _velocityY;
        PROPERTY( Alias = "groundY" )
        float32 _groundY;
        PROPERTY( Alias = "bIsGrounded" )
        bool _bIsGrounded;
    };
} // namespace sw
