/**
 * @file LaunchVelocityComponent.h
 * @brief 스폰된 투사체(화살 · 탄피 · 던진 물건)를 같은 오브젝트의 동적 강체로 날려 보냅니다. 운동 모델은 물리다 — 처음 속도만 주고, 중력 · 충돌 · 연속 충돌은 강체가 맡습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Math/Math.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @class LaunchVelocityComponent
     * @brief 첫 틱에 오브젝트의 월드 회전으로 돌린 `_localDirection` 쪽으로 `_speed` 의 선속도를 같은 오브젝트의 `RigidBodyComponent`(동적)에 겁니다(언리얼
     *        ProjectileMovement 의 InitialSpeed 를 물리 바디로 한 것).
     * @details 스폰한 쪽(알림 `SpawnPrefab`)은 스폰한 뒤에 월드 자세를 정하므로, 방향은 시작(`onBeginPlay`)이 아니라 첫 틱에 읽는다. 강체가 없거나 동적이 아니면
     *          오류로 알리고 날리지 않는다(조용히 제자리에 두지 않는다). 날아간 뒤의 자세는 물리가 정하되 `_alignGain` 이 0 보다 크면 날아가는 동안
     *          `_localDirection` 이 속도 쪽을 보도록 각속도를 건다(언리얼 ProjectileMovement 의 `bRotationFollowsVelocity` — 화살 머리가 처음 자세 그대로 날지 않게 한다).
     */
    REFLECT( Category = "Physics", DisplayName = "Launch Velocity", Tooltip = "Gives this object's dynamic rigid body an initial velocity along a local direction on its first tick (projectiles)" )
    class SW_API LaunchVelocityComponent : public Component
    {
    public:
        REFLECT_BODY();

        LaunchVelocityComponent();
        ~LaunchVelocityComponent() override = default;

        void onBeginPlay() override;
        /** @brief 첫 틱에 한 번 선속도를 겁니다. 그 뒤로는 할 일이 없습니다. */
        void onTick( float32 deltaTime ) override;

        /** @brief 이미 날렸으면 true 입니다(테스트와 진단). */
        bool hasLaunched() const { return _bLaunched == SW_TRUE; }

    private:
        /** @brief 날린 뒤 `_alignGain` 이 0 보다 크면 앞 방향(`_localDirection`)을 속도 쪽으로 돌리는 각속도를 건다. */
        void steerTowardVelocity();

        PROPERTY( Category = "Launch", DisplayName = "Speed", Tooltip = "Initial speed along the direction", Min = 0.0, Units = "m/s" )
        float32 _speed;
        PROPERTY( Category = "Launch", DisplayName = "Local Direction", Tooltip = "Launch direction in the object's local space (normalized when launching)" )
        float3 _localDirection;
        PROPERTY( Category = "Launch", DisplayName = "Align Gain", Tooltip = "After launch, steers the body's local direction toward its velocity (angular velocity = axis x gain; arrows point along their flight). 0 leaves the rotation to the physics", Min = 0.0, Meta = "Units=1/s" )
        float32 _alignGain;
        uint8   _bLaunched : 1;
        uint8   _bPending  : 1; ///< 플레이가 시작돼 다음 틱에 날린다
        uint8   _reserved  : 6;
    };
} // namespace sw
