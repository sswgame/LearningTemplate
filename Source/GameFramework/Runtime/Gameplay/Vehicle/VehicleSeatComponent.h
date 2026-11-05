/**
 * @file VehicleSeatComponent.h
 * @brief 탈것의 좌석 하나입니다. 소켓, 하차 위치, 탑승 중 자세를 가집니다. 운전석이면 앉은 쪽의 조종자가 탈것 폰을 쥡니다.
 */
#pragma once
#include "Core/Container/ComponentHandle.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 타기의 결과입니다. */
    ENUM()
    enum class MountResult : uint8
    {
        Mounted = 0,  ///< 탔다
        SeatTaken,    ///< 누가 앉아 있거나, 탑승자가 이미 어딘가에 앉아 있다
        TooFar,       ///< 좌석의 타는 거리 밖이다
        NoSocket,     ///< 탈것 외형에 좌석 소켓이 없거나 붙일 수 없다
        NotPossessed, ///< 운전석인데 탑승자를 쥔 조종자가 없다. 넘겨줄 빙의가 없는 경우이다
        NoVehiclePawn ///< 운전석인데 탈것에 폰이 없다
    };
} // namespace sw

namespace sw
{
    class PawnComponent;
    class VehicleSeatComponent;

} // namespace sw

namespace sw
{
    class ControllerComponent;

    /**
     * @class VehicleSeatComponent
     * @brief 탈것 오브젝트의 좌석 하나입니다. 운전석 하나와 승객석 여럿을 둘 수 있습니다. 운전석이 있으면 탈것 오브젝트에 `PawnComponent` 가 있어야 합니다.
     * @details 타기와 내리기는 `VehicleSeatComponent` 이 처리한다. 탑승자는 소켓에 붙고(`SocketBindingComponent`, 붙어 있는 동안 비용이 0), 이동이 멈추며, 운전석이면
     *          탑승자의 조종자가 빙의를 탈것으로 옮긴다. 말이든 차든 같은 구조이고, 의도를 탈것의 이동으로 바꾸는 규칙만 다르다.
     *          빈 좌석을 찾을 때 씬을 순회하지 않도록 레지스트리(`ComponentRegistry`)에 등록한다.
     */
    REFLECT( Category = "Vehicle", DisplayName = "Vehicle Seat", Tooltip = "Seat on a vehicle: socket, exit spot, rider pose; the driver seat hands the vehicle pawn to the rider's controller" )
    class SW_GF_API VehicleSeatComponent : public Component
    {
    public:
        /** @brief @p riderPawn 을 @p seat 에 태웁니다. */
        static MountResult mount( PawnComponent& riderPawn, VehicleSeatComponent& seat );
        /**
         * @brief 내립니다. 하차 위치(막혀 있으면 좌석 둘레 여덟 방향 중 빈 곳)로 분리하고, 이동을 켜고, 운전석이면 조종자가 탑승자 폰을 다시 쥐게 합니다.
         * @param bForced 강제 하차(쓰러짐, 말이 떨어뜨림)이면 하차 위치를 찾지 않고 지금 위치에서 분리합니다.
         * @return 앉아 있지 않으면 false 입니다.
         */
        static bool dismount( PawnComponent& riderPawn, bool bForced );
        /** @brief 탑승자가 앉은 좌석입니다. 없으면 nullptr 입니다. */
        static VehicleSeatComponent* findSeatOf( const PawnComponent& riderPawn );
        /** @brief @p riderPawn 이 타는 거리 안에 있는 빈 좌석입니다. 운전석을 먼저 보고, 그다음은 가까운 순입니다. 없으면 nullptr 입니다. */
        static VehicleSeatComponent* findNearestFreeSeat( const PawnComponent& riderPawn );

        REFLECT_BODY();

        VehicleSeatComponent();
        ~VehicleSeatComponent() override = default;

        void onRegister( GameObjectManager& manager ) override;
        /** @brief 앉은 쪽이 있으면 내려 줍니다. 탈것이 사라질 때 부릅니다. */
        void onUnregister( GameObjectManager& manager ) override;

        /** @brief 앉은 탑승자의 오브젝트입니다. 비어 있으면 무효 핸들입니다. */
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

        GameObjectHandle                      _occupant;                  ///< 앉은 탑승자의 오브젝트
        TComponentHandle<ControllerComponent> _occupantController;        ///< 탈 때 탑승자를 쥐고 있던 조종자. 운전석에서 쓰며 내릴 때 탑승자에게 돌려준다
        TComponentHandle<ControllerComponent> _previousVehicleController; ///< 탈 때 탈것에 빙의해 있던 조종자. 주인을 따라오던 말 AI 가 해당하며, 내리면 다시 빙의한다
    };
} // namespace sw
