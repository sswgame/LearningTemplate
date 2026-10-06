/**
 * @file VehicleSeatComponent.h
 * @brief 탈것의 좌석 하나 — 소켓 · 하차 자리 · 탑승 중 자세. 운전석이면 앉은 이의 조종자가 탈것 폰을 쥡니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/ComponentHandle.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @class VehicleSeatComponent
     * @brief 탈것 오브젝트의 좌석 하나입니다(운전석 하나 + 승객석 여럿). 운전석이 있으면 탈것 오브젝트에 `PawnComponent` 가 있어야 합니다.
     * @details 타기 · 내리기는 `MountUtil` 이 합니다 — 탑승자는 소켓에 붙고(`SocketBindingComponent`, 붙은 동안 비용 0) 이동이 멈추며, 운전석이면
     *          탑승자의 조종자가 빙의를 탈것으로 옮깁니다. 말이든 차든 같은 틀이고 의도 → 탈것 이동 규칙만 다릅니다.
     *          빈 좌석 찾기가 씬을 훑지 않게 등록부(`ComponentRegistry`)에 듭니다.
     */
    REFLECT( Category = "Vehicle", DisplayName = "Vehicle Seat", Tooltip = "Seat on a vehicle: socket, exit spot, rider pose; the driver seat hands the vehicle pawn to the rider's controller" )
    class SW_GF_API VehicleSeatComponent : public Component
    {
    public:
        REFLECT_BODY();

        VehicleSeatComponent();
        ~VehicleSeatComponent() override = default;

        void onRegister( GameObjectManager& manager ) override;
        /** @brief 앉은 이가 있으면 내려 둡니다(탈것이 사라진다). */
        void onUnregister( GameObjectManager& manager ) override;

        /** @brief 앉은 탑승자의 오브젝트입니다(비었으면 무효). */
        const GameObjectHandle& getOccupant() const { return _occupant; }
        bool                    isDriverSeat() const { return _bDriverSeat; }
        void                    setDriverSeat( bool bDriverSeat ) { _bDriverSeat = bDriverSeat; }
        bool                    isFree() const { return _occupant.isValid() == false; }
        const hashed_string&    getSocketName() const { return _socketName; }
        void                    setSocketName( const hashed_string& socketName ) { _socketName = socketName; }
        const float3&           getSeatOffset() const { return _seatOffset; }
        void                    setSeatOffset( const float3& offset ) { _seatOffset = offset; }
        const float3&           getExitOffset() const { return _exitOffset; }
        void                    setExitOffset( const float3& offset ) { _exitOffset = offset; }
        const hashed_string&    getRiderPoseParameter() const { return _riderPoseParameter; }
        float32                 getEnterDistance() const { return _enterDistance; }
        void                    setEnterDistance( float32 distance ) { _enterDistance = distance; }

    private:
        friend struct MountUtil;

        PROPERTY( Category = "Seat", DisplayName = "Socket", Tooltip = "Socket on the vehicle's appearance the rider binds to (empty: Seat Offset on the vehicle root)" )
        hashed_string _socketName;
        PROPERTY( Category = "Seat", DisplayName = "Rider Pose", Tooltip = "Animator float parameter set to 1 on the rider while seated (RideHorse, DriveCar)" )
        hashed_string _riderPoseParameter;
        PROPERTY( Category = "Seat", DisplayName = "Seat Offset", Tooltip = "Where the rider sits in vehicle space when there is no socket", Units = m )
        float3 _seatOffset;
        PROPERTY( Category = "Seat", DisplayName = "Exit Offset", Tooltip = "Where the rider stands after exiting, in vehicle space", Units = m )
        float3 _exitOffset;
        PROPERTY( Category = "Seat", DisplayName = "Enter Distance", Min = 0.0, Tooltip = "How close the rider must be to the vehicle to get on", Units = m )
        float32 _enterDistance;
        PROPERTY( Category = "Seat", DisplayName = "Driver Seat", Tooltip = "The rider here controls the vehicle" )
        bool _bDriverSeat;

        GameObjectHandle _occupant;                  ///< 앉은 탑승자의 오브젝트
        ComponentHandle  _occupantController;        ///< 탈 때 탑승자를 쥐고 있던 조종자(운전석 — 내릴 때 탑승자에게 돌려준다)
        ComponentHandle  _previousVehicleController; ///< 탈 때 탈것을 쥐고 있던 조종자(주인을 따라오던 말 AI — 내리면 다시 쥔다)
    };
} // namespace sw
