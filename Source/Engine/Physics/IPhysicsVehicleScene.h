/**
 * @file IPhysicsVehicleScene.h
 * @brief 물리 씬의 바퀴 차 쪽 인터페이스입니다. 3D 만 있습니다(차원 트레이트의 `kSupportsVehicle`).
 * @details 언리얼의 Chaos Vehicle, 유니티의 `WheelCollider` 에 해당한다. 2D 씬은 이 인터페이스를 상속하지 않고 빈 `PhysicsNoVehicleScene` 을 상속한다.
 *          핸들 · 스레드 · 단위 규칙은 `IPhysicsScene.h` 머리말과 같다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "Engine/Physics/PhysicsTypes.h"

namespace sw
{
    struct PhysicsVehicleState;
    struct PhysicsWheeledVehicleDesc;
} // namespace sw

namespace sw
{
    /** @brief 바퀴 차를 다룹니다. */
    class IPhysicsVehicleScene
    {
    public:
        virtual ~IPhysicsVehicleScene() = default;

        /**
         * @brief 바퀴 차를 만듭니다. 이미 만든 동적 차체 바디 @p chassis 에 바퀴, 엔진, 변속을 붙입니다. 차체 바디를 지우면 함께 지워집니다.
         * @details 차는 스텝마다 바퀴 충돌, 서스펜션, 마찰을 푸는 구속이다(Jolt 는 스텝 리스너). 만들지 못하면 무효 핸들을 반환한다.
         */
        virtual PhysicsVehicleHandle createWheeledVehicle( PhysicsBodyHandle chassis, const PhysicsWheeledVehicleDesc& desc ) = 0;
        virtual void                 destroyVehicle( PhysicsVehicleHandle vehicle )                                           = 0;
        virtual bool                 isVehicleValid( PhysicsVehicleHandle vehicle ) const                                     = 0;
        /** @brief 운전 입력입니다(앞 -1..1, 오른쪽 -1..1, 브레이크 0..1, 핸드브레이크 0..1). 다음 스텝에 적용하고, 자고 있는 차체를 깨웁니다. */
        virtual void setVehicleInput( PhysicsVehicleHandle vehicle, float32 forward, float32 right, float32 brake, float32 handBrake ) = 0;
        virtual bool getVehicleState( PhysicsVehicleHandle vehicle, PhysicsVehicleState& outState ) const                              = 0;
    };
} // namespace sw

namespace sw
{
    /** @brief 바퀴 차가 없는 차원(2D)의 씬이 `IPhysicsVehicleScene` 대신 상속하는 빈 자리입니다. 함수가 없으므로 2D 씬에 차를 부르면 컴파일 오류입니다. */
    struct PhysicsNoVehicleScene
    {
    };
} // namespace sw
