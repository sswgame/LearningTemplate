/**
 * @file PhysicsCarComponent.h
 * @brief 물리 차 폰의 이동입니다. 의도를 같은 오브젝트의 `WheeledVehicleComponent` 운전 입력(페달, 조향, 브레이크, 핸드브레이크)으로 바꿉니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct ControlIntent;

    /** @brief 의도에서 만든 바퀴 차 운전 입력입니다. */
    struct PhysicsCarInput
    {
        float32 _forward{ 0.0f };   ///< −1..1 이며 음수이면 후진이다
        float32 _right{ 0.0f };     ///< −1..1
        float32 _brake{ 0.0f };     ///< 0..1
        float32 _handBrake{ 0.0f }; ///< 0..1
    };
} // namespace sw

namespace sw
{
    /**
     * @class PhysicsCarComponent
     * @brief 물리 차(동적 차체, `WheeledVehicleComponent`, 폰, 좌석)에 붙습니다. 의도는 아케이드 차와 말처럼 "가고 싶은 월드 방향" 이고, 차 방향으로
     *        투영해서 앞 성분은 페달, 옆 성분은 조향으로 씁니다. 앞으로 가는 중에 뒤를 원하면 먼저 브레이크를 밟고, 거의 멈췄으면 후진합니다(`toCarInput`).
     * @details PrePhysics 틱에서 운전 입력을 기록해 두면 이번 물리 프레임에 반영된다. 핸드브레이크는 폰 스키마의 버튼(`_handBrakeButton`)으로 받는다.
     */
    REFLECT( Category = "Vehicle", DisplayName = "Physics Car", Tooltip = "Control intent -> wheeled vehicle driver input: throttle, steering, brake, hand brake" )
    class SW_GF_API PhysicsCarComponent : public Component
    {
    public:
        REFLECT_BODY();

        PhysicsCarComponent();
        ~PhysicsCarComponent() override = default;

        void onBeginPlay() override;
        void onTick( float32 deltaTime ) override;

        /**
         * @brief 의도를 운전 입력으로 바꿉니다. @p carForward 는 차체의 월드 앞 방향(XZ)이고, @p forwardSpeed 는 앞 방향 속도입니다.
         * @param reverseSpeed 이 값보다 빠르게 앞으로 가는 중이면 뒤쪽 의도를 브레이크로 처리합니다(m/s).
         */
        static PhysicsCarInput toCarInput( const ControlIntent& intent, const float3& carForward, float32 forwardSpeed, float32 reverseSpeed, int32 handBrakeIndex );

    private:
        PROPERTY( Category = "Vehicle", DisplayName = "Hand Brake Button", Tooltip = "Intent button held for the hand brake" )
        hashed_string _handBrakeButton;
        PROPERTY( Category = "Vehicle", DisplayName = "Reverse Speed", Min = 0.0, Tooltip = "Backward intent brakes above this forward speed and reverses below it", Units = "m/s" )
        float32 _reverseSpeed;

        int32 _handBrakeIndex;
    };
} // namespace sw
