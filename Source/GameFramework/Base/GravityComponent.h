#pragma once
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    REFLECT()
    class SW_GF_API GravityComponent : public Component
    {
    public:
        REFLECT_BODY();
        GravityComponent();
        virtual ~GravityComponent() override = default;

        void onBeginPlay() override;
        void onEndPlay() override;
        void onTick( float32 deltaTime ) override;

        /** @brief 중력 가속도(초당 속도 변화)를 설정합니다. 보통 음수입니다. */
        void setGravity( float32 gravity ) { _gravity = gravity; }
        /** @brief 바닥 높이를 설정합니다. 이 아래로는 내려가지 않습니다. */
        void setGroundY( float32 groundY ) { _groundY = groundY; }
        /**
         * @brief 위로 튕겨 냅니다. 땅에 붙어 있던 상태를 **뗍니다.**
         * @details 이 창구가 없었습니다. `_bIsGrounded` 는 리플렉션 프로퍼티일 뿐이어서, 한 번
         *          땅에 닿으면 코드로는 다시 떨어뜨릴 방법이 없었습니다.
         */
        void jump( float32 speed )
        {
            _velocityY   = speed;
            _bIsGrounded = false;
        }
        /** @brief 땅에 붙어 있는지 여부입니다. */
        bool isGrounded() const { return _bIsGrounded; }
        /** @brief 현재 수직 속도입니다. */
        float32 getVelocityY() const { return _velocityY; }

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
