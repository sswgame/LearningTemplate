/**
 * @file ShooterDroneComponent.h
 * @brief 드론(떠다니는 과녁) 하나 — 플레이어 눈 쪽으로 다가오며 이웃과 떨어지고 상자를 돌아가며, 닿으면 플레이어를 때립니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Math/Math.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @class ShooterDroneComponent
     * @brief 드론 프리팹의 컴포넌트입니다. 디렉터가 틱 뒤에 세우고 `launch` 로 체력 · 빠르기를 줍니다.
     * @details 기본 틱 그룹(`DuringPhysics`)에서 디렉터가 적은 플레이어 눈 · 드론 자리 · 막는 상자를 읽고 자기 오브젝트만 씁니다(자리 · 회전 · 맞은 색 ·
     *          HP 바). 플레이어를 때리는 것은 다른 오브젝트에 쓰는 일이라 틱 뒤로 미룹니다. 맞음(`applyDamage`)은 플레이어의 사격이 틱 뒤에 겁니다.
     *          체력이 바닥나면 디렉터가 다음 틱에 걷는다(쓰러뜨린 수 · 터짐 효과).
     */
    REFLECT( Category = "Shooter3D", DisplayName = "Shooter Drone", Tooltip = "Hovering target drone that homes in on the player" )
    class ShooterDroneComponent : public Component
    {
    public:
        REFLECT_BODY();

        ShooterDroneComponent();
        virtual ~ShooterDroneComponent() override = default;

        void onTick( float32 deltaTime ) override;

        /** @brief 세웁니다 — 디렉터 · 자리 · 체력 · 빠르기 · 흔들림 위상. */
        void launch( GameObjectHandle director, const float3& position, float32 health, float32 speed, float32 bobPhase );
        /** @brief 맞았다 — 체력을 깎고 잠깐 번쩍인다(틱 밖 · 게임 스레드). */
        void          applyDamage( float32 amount );
        bool          isDead() const { return _bLaunched == SW_TRUE && _health <= 0.0f; }
        const float3& getPosition() const { return _position; }
        float32       getRadius() const { return _radius; }

    private:
        /** @brief 플레이어를 때리는 것을 틱 뒤로 미룹니다. */
        void requestPlayerDamage( GameObjectHandle player ) const;

    private:
        PROPERTY( Category = "Drone", DisplayName = "Director", Tooltip = "Object with the ShooterDirectorComponent" )
        GameObjectHandle _director;
        PROPERTY( Category = "Drone", DisplayName = "Radius", Tooltip = "Hit sphere and body radius", Min = 0.0, Meta = "Units=m" )
        float32 _radius;
        PROPERTY( Category = "Drone", DisplayName = "Reach", Tooltip = "Strikes the player inside this distance", Min = 0.0, Meta = "Units=m" )
        float32 _reach;
        PROPERTY( Category = "Drone", DisplayName = "Damage", Tooltip = "Damage per strike", Min = 0.0 )
        float32 _damage;
        PROPERTY( Category = "Drone", DisplayName = "Attack Interval", Min = 0.0, Meta = "Units=s" )
        float32 _attackInterval;
        PROPERTY( Category = "Drone", DisplayName = "Hover Height", Tooltip = "Centre height above the floor", Meta = "Units=m" )
        float32 _hoverHeight;
        PROPERTY( Category = "Drone", DisplayName = "Bob Amplitude", Meta = "Units=m" )
        float32 _bobAmplitude;
        PROPERTY( Category = "Drone", DisplayName = "Model Yaw Offset", Tooltip = "Turns the model so its face (+X) looks at the player", Meta = "Units=rad" )
        float32 _modelYawOffset;

        float3  _position;
        float32 _health;
        float32 _maxHealth;
        float32 _speed;
        float32 _attackCooldown;
        float32 _flashTimer;
        float32 _bobPhase;
        uint8   _bLaunched : 1;
        uint8   _bFlashing : 1; ///< 지금 맞은 색을 입고 있다(바뀔 때만 다시 입힌다)
        uint8   _reserved  : 6;
    };
} // namespace sw
