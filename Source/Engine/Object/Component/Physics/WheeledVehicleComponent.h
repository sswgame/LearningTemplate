/**
 * @file WheeledVehicleComponent.h
 * @brief 바퀴 넷 차 — 같은 오브젝트의 동적 강체(차체)에 바퀴 · 서스펜션 · 엔진 · 변속을 붙입니다(Jolt `VehicleConstraint`). 운전 입력은 틱 어디서든 넣습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/SpinLock.h"
#include "Core/Math/Math.h"

#include "Engine/Object/Component/Physics/PhysicsComponent.h"
#include "Engine/Physics/IPhysicsScene.h"
#include "Engine/Physics/PhysicsTypes.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /** @brief 어느 바퀴가 엔진 힘을 받는가입니다. */
    ENUM()
    enum class WheelDrive : uint8
    {
        Front = 0, ///< 앞바퀴 굴림
        Rear,      ///< 뒷바퀴 굴림
        All,       ///< 네 바퀴 굴림(차축마다 반씩)
    };
} // namespace sw

namespace sw
{
    /**
     * @class WheeledVehicleComponent
     * @brief 물리 차입니다(언리얼 Chaos Vehicle · 유니티 WheelCollider 의 자리). 바퀴는 넷 — 앞 왼쪽 · 앞 오른쪽 · 뒤 왼쪽 · 뒤 오른쪽 — 이고 자리는
     *        차축 앞뒤(`_frontAxle` · `_rearAxle`) · 반 폭(`_halfTrack`) · 붙는 높이(`_wheelMountHeight`)로 정합니다(차체 기준, 앞 +Z). 앞바퀴만 조향하고
     *        핸드브레이크는 뒷바퀴에 겁니다.
     * @details 차체 바디가 생긴 뒤 첫 물리 프레임에 차를 만들고, 바디가 다시 지어지면 차도 다시 짓습니다. 운전 입력(`setDriverInput`)은 어느 틱에서든
     *          불러도 되고(잠금 아래 적어 둔다) 물리 프레임 시작에 백엔드로 넘어갑니다. 2D 물리에는 없습니다. 바퀴 메시를 바퀴 자세로 옮기는 것은 아직 없다.
     */
    REFLECT( Category = "Physics", DisplayName = "Wheeled Vehicle", Tooltip = "Four wheels, suspension, engine and gearbox on this object's dynamic rigid body" )
    class SW_API WheeledVehicleComponent : public PhysicsComponent
    {
    public:
        REFLECT_BODY();

        WheeledVehicleComponent();
        ~WheeledVehicleComponent() override = default;

        /** @brief 운전 입력입니다 — 앞 −1..1(음수 후진) · 오른쪽 −1..1 · 브레이크 0..1 · 핸드브레이크 0..1. 다시 부를 때까지 유지합니다. 아무 스레드에서 불러도 됩니다. */
        void setDriverInput( float32 forward, float32 right, float32 brake, float32 handBrake );
        /** @brief 마지막 물리 프레임의 상태입니다(차가 없으면 0). */
        PhysicsVehicleState getVehicleState() const;
        /** @brief 차가 만들어졌는지입니다. */
        bool isVehicleCreated() const { return _vehicle.isValid(); }
        /** @brief 이 컴포넌트의 수치로 만든 바퀴 차 서술자입니다(시험 · 도구가 본다). */
        PhysicsWheeledVehicleDesc makeVehicleDesc() const;

    protected:
        void beginPhysicsFrame( ScenePhysics& physics ) override;
        /** @brief 스텝이 끝난 상태(속도 · 기어 · 접지)를 읽어 둡니다. */
        void endPhysicsFrame( ScenePhysics& physics, float32 alpha ) override;
        void releasePhysics( ScenePhysics& physics ) override;

    private:
        PROPERTY( Category = "Wheels", DisplayName = "Wheel Radius", Min = 0.05, Units = m )
        float32 _wheelRadius;
        PROPERTY( Category = "Wheels", DisplayName = "Wheel Width", Min = 0.01, Units = m )
        float32 _wheelWidth;
        PROPERTY( Category = "Wheels", DisplayName = "Half Track", Min = 0.0, Tooltip = "Sideways distance from the center to each wheel", Units = m )
        float32 _halfTrack;
        PROPERTY( Category = "Wheels", DisplayName = "Front Axle", Tooltip = "Forward position of the front wheels in chassis space", Units = m )
        float32 _frontAxle;
        PROPERTY( Category = "Wheels", DisplayName = "Rear Axle", Tooltip = "Forward position of the rear wheels in chassis space (negative: behind)", Units = m )
        float32 _rearAxle;
        PROPERTY( Category = "Wheels", DisplayName = "Wheel Mount Height", Tooltip = "Height of the suspension top in chassis space", Units = m )
        float32 _wheelMountHeight;
        PROPERTY( Category = "Suspension", DisplayName = "Min Length", Min = 0.0, Units = m )
        float32 _suspensionMinLength;
        PROPERTY( Category = "Suspension", DisplayName = "Max Length", Min = 0.0, Units = m )
        float32 _suspensionMaxLength;
        PROPERTY( Category = "Suspension", DisplayName = "Frequency", Min = 0.0, Tooltip = "Spring natural frequency", Units = Hz )
        float32 _suspensionFrequency;
        PROPERTY( Category = "Suspension", DisplayName = "Damping", Min = 0.0, Tooltip = "Damping ratio" )
        float32 _suspensionDamping;
        PROPERTY( Category = "Steering", DisplayName = "Max Steer Angle", Min = 0.0, Max = 1.5, Units = rad )
        float32 _maxSteerAngle;
        PROPERTY( Category = "Brakes", DisplayName = "Max Brake Torque", Min = 0.0, Meta = "Units=N*m" )
        float32 _maxBrakeTorque;
        PROPERTY( Category = "Brakes", DisplayName = "Max Hand Brake Torque", Min = 0.0, Tooltip = "Rear wheels only", Meta = "Units=N*m" )
        float32 _maxHandBrakeTorque;
        PROPERTY( Category = "Engine", DisplayName = "Max Torque", Min = 0.0, Meta = "Units=N*m" )
        float32 _engineMaxTorque;
        PROPERTY( Category = "Engine", DisplayName = "Max RPM", Min = 0.0 )
        float32 _engineMaxRpm;
        PROPERTY( Category = "Engine", DisplayName = "Drive", Tooltip = "Which wheels the engine drives" )
        WheelDrive _drive;

        mutable SpinLock     _inputLock;
        float32              _inputForward;   ///< `_inputLock`
        float32              _inputRight;     ///< `_inputLock`
        float32              _inputBrake;     ///< `_inputLock`
        float32              _inputHandBrake; ///< `_inputLock`
        PhysicsVehicleState  _state;          ///< 마지막 물리 프레임의 끝(게임 스레드가 쓴다)
        PhysicsVehicleHandle _vehicle;
        PhysicsBodyHandle    _vehicleChassis; ///< 차를 지은 차체 바디(다시 지어졌는지 본다)
    };
} // namespace sw
