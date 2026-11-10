/**
 * @file MoveAnimationComponent.h
 * @brief 몸이 실제로 움직인 수평 속도를 애니메이터 매개변수로 넣고, 움직이는 쪽을 바라보게 돌립니다(걷기 · 뛰기 블렌드 공간이 그 값을 읽는다).
 * @details 몸을 옮기는 쪽(일정 · 내비 에이전트 · 디렉터)이 애니메이션을 모르게 한다 — 언리얼 애님 블루프린트가 폰 속도를 읽고, 유니티는 스크립트가
 *          `Animator.SetFloat("Speed", …)` 를 넣는 자리다. 속도는 주 메시의 월드 위치 차이에서 얻으므로 옮기는 방법을 가리지 않는다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @class MoveAnimationComponent
     * @brief 같은 오브젝트의 주 메시(`MeshComponent`)가 움직인 수평 속도를 `SkeletalAnimatorComponent` 의 매개변수(`_speedParameter`)로 넣습니다.
     * @details `TickGroup::PostUpdate` 에서 돈다(몸을 옮기는 쪽이 이번 프레임에 쓴 자리를 읽는다). 속도는 `_smoothingSeconds` 로 고르게 하고(한 프레임 튐이 걸음을 끊지 않게),
     *          `_faceMinSpeed` 보다 빠르면 주 메시의 요를 움직이는 쪽으로 `_turnRateDegrees` 만큼씩 돌린다. 처음 프레임과 순간이동(`_teleportDistance` 보다 큰 걸음)은 속도로 치지 않는다.
     */
    REFLECT( Category = "Character", DisplayName = "Move Animation", Tooltip = "Feeds the body's horizontal speed to the animator and faces where it moves" )
    class SW_GF_API MoveAnimationComponent : public Component
    {
    public:
        REFLECT_BODY();

        MoveAnimationComponent();
        virtual ~MoveAnimationComponent() override = default;

        void onBeginPlay() override;
        void onTick( TickContext& context ) override;

        /** @brief 고르게 한 지금 수평 속도(m/s)입니다. */
        float32 getSpeed() const { return _speed; }

        /**
         * @brief 한 걸음을 반영한 속도(m/s)를 돌려줍니다. 고르기는 지수 감쇠(`_smoothingSeconds` 가 0 이면 그 걸음의 속도 그대로)다.
         * @details 걸음 시간이 0 이하면 이전 속도 그대로다(멈춘 프레임이 속도를 0 으로 만들지 않게).
         */
        [[nodiscard]] static float32 smoothSpeed( float32 previousSpeed, float32 stepDistance, float32 deltaTime, float32 smoothingSeconds );
        /** @brief 요(라디안)를 목표 쪽으로 최대 @p maxStep 만큼 돌린 값입니다. 짧은 쪽(±π)으로 돈다. */
        [[nodiscard]] static float32 turnTowards( float32 yaw, float32 targetYaw, float32 maxStep );

    private:
        PROPERTY( Required, Category = "Animation", DisplayName = "Speed Parameter", Tooltip = "Animator float parameter that receives the horizontal speed (m/s)" )
        hashed_string _speedParameter;
        PROPERTY( Category = "Animation", DisplayName = "Smoothing", Tooltip = "Time constant of the speed smoothing; 0 uses each frame's speed", Min = 0.0, Units = s )
        float32 _smoothingSeconds;
        PROPERTY( Category = "Facing", DisplayName = "Face Min Speed", Tooltip = "Turns toward the motion above this speed; slower keeps the facing", Min = 0.0, Units = m )
        float32 _faceMinSpeed;
        PROPERTY( Category = "Facing", DisplayName = "Turn Rate", Tooltip = "Largest yaw change per second while turning toward the motion (degrees)", Min = 0.0 )
        float32 _turnRateDegrees;
        PROPERTY( Category = "Animation", DisplayName = "Teleport Distance", Tooltip = "A step longer than this is a teleport and not counted as speed", Min = 0.0, Units = m )
        float32 _teleportDistance;

        float3  _lastPosition; ///< 지난 틱의 주 메시 월드 위치(저장하지 않는다)
        float32 _speed;        ///< 고르게 한 수평 속도(저장하지 않는다)
        uint8   _bHasLast;     ///< `_lastPosition` 이 유효한지(저장하지 않는다)
    };
} // namespace sw
