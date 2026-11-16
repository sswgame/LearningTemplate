/**
 * @file VehicleSeatComponent.h
 * @brief 탈것의 좌석 하나입니다. 소켓, 하차 위치, 탑승 중 자세를 가집니다. 운전석이면 앉은 쪽의 조종자가 탈것 폰을 쥡니다.
 *        좌석은 탈것 가속도(앞뒤 · 옆 G · 수직 충격)를 재어 탑승 가속감(탑승자 기울임 · 카메라 · 소리)을 냅니다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Foundation/ComponentHandle.h"
#include "Engine/Foundation/GameObjectHandle.h"
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

    /**
     * @brief 탑승 가속감의 반응 곡선입니다. 좌석 가속도(탈것 기준 — x 오른쪽, y 위, z 앞, m/s²)를 탑승자 기울임 · 카메라 · 소리로 바꿉니다.
     * @details 탈것 종류와 상관없이 같다(바디 없는 탈것 · 바퀴 차 · 말 · 배). 좌석이 자기 월드 위치의 2 차 차분으로 재므로 탈것의 이동 방식을 모른다.
     *          탑승자는 가속도를 거슬러 기운다 — 급정거면 앞으로, 가속이면 뒤로, 오른쪽으로 돌면(가속도가 안쪽 +x) 바깥(왼쪽)으로.
     *          기울임은 좌석 소켓 변환에 더하는 회전(가산 — 애니메이션 위에 얹힌다)이고, 애니메이터 파라미터로도 낸다(가산 기울임 클립을 섞는 그래프용).
     *          언리얼 Chaos Vehicle 의 운전자 IK 기울임 · Cinemachine Impulse 의 착지 충격과 같은 자리다(기억에 의존).
     */
    REFLECT()
    struct SW_GF_API RideFeelSettings
    {
        REFLECT_BODY();

        PROPERTY( Min = 0.0, Units = s, Tooltip = "Smoothing time of the seat acceleration (filters frame noise)" )
        float32 _smoothingTime{ 0.12f };
        PROPERTY( Min = 0.0, Tooltip = "Rider lean per m/s2 of seat acceleration (rad per m/s2); the rider leans against the acceleration" )
        float32 _leanPerAcceleration{ 0.025f };
        PROPERTY( Min = 0.0, Units = rad, Tooltip = "Largest rider lean" )
        float32 _maxLean{ 0.35f };
        PROPERTY( Tooltip = "Animator float on the rider set to the forward lean (-1 back .. 1 forward); empty = none" )
        hashed_string _leanForwardParameter;
        PROPERTY( Tooltip = "Animator float on the rider set to the side lean (-1 left .. 1 right); empty = none" )
        hashed_string _leanSideParameter;
        PROPERTY( Min = 0.0, Units = "m/s2", Tooltip = "Forward acceleration that kicks the camera field of view wider once (0 = never)" )
        float32 _fieldOfViewKickAcceleration{ 12.0f };
        PROPERTY( Tooltip = "Field of view kick amount (ratio; + wider)" )
        float32 _fieldOfViewKickAmount{ 0.08f };
        PROPERTY( Min = 0.0, Units = "m/s2", Tooltip = "Vertical acceleration that shakes the camera once (landing, bump; 0 = never)" )
        float32 _bumpAcceleration{ 25.0f };
        PROPERTY( Min = 0.0, Tooltip = "Camera shake amplitude (m) per m/s2 above the bump threshold" )
        float32 _bumpShakePerAcceleration{ 0.004f };
        PROPERTY( Tooltip = "Sound parameter on the vehicle's emitters set to the seat speed (m/s — wind); empty = none" )
        hashed_string _speedSoundParameter;
        PROPERTY( Tooltip = "Sound parameter on the vehicle's emitters set to the side acceleration size (m/s2 — skid, runner friction); empty = none" )
        hashed_string _slipSoundParameter;

        /** @brief 가속도 @p localAcceleration(탈것 기준)에 대한 기울임입니다. x 는 앞으로 숙임(rad, + 앞), y 는 옆 기울임(rad, + 왼쪽)이고 `_maxLean` 으로 자릅니다. */
        float2 computeLean( const float3& localAcceleration ) const;
    };
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
        /** @brief 탑승자 오브젝트가 사라지는 중입니다. 앉았던 좌석을 비우고 탈것의 빙의를 정리합니다(`DismountOnDeathComponent` 가 부릅니다). */
        static void releaseGoneOccupant( GameObjectManager& manager, const GameObjectHandle& rider );
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
        /** @brief PostPhysics 에서 틱합니다(탈것이 움직인 뒤). */
        void onBeginPlay() override;
        /** @brief 앉은 쪽이 있으면 좌석 가속도를 재고 탑승 가속감을 냅니다. 비어 있으면 잰 값을 비웁니다. */
        void onTick( TickContext& context ) override;
        /** @brief 지난 틱이 잰 탑승 반응(탑승자 기울임 · 카메라 킥 · 소리)을 겁니다. 깃발이 없으면 아무것도 하지 않습니다. 게임 스레드, 틱 밖(`VehicleSeatSystem`). */
        void applyPendingRide( GameObjectManager& manager );

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
        /** @brief 매끄럽게 한 좌석 가속도입니다(탈것 기준 — x 오른쪽, y 위, z 앞, m/s²). 비었거나 아직 두 걸음을 재지 않았으면 0 입니다. */
        const float3& getRideAcceleration() const { return _rideAcceleration; }
        /** @brief 지금 탑승자 기울임입니다(x 앞으로 숙임, y 왼쪽 기울임, rad). */
        const float2&           getRiderLean() const { return _riderLean; }
        const RideFeelSettings& getRideFeel() const { return _rideFeel; }
        void                    setRideFeel( const RideFeelSettings& rideFeel ) { _rideFeel = rideFeel; }

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
        PROPERTY( Category = "Seat", DisplayName = "Ride Feel", Tooltip = "How the seat acceleration leans the rider, kicks the camera and drives the ride sounds" )
        RideFeelSettings _rideFeel;

        GameObjectHandle                      _occupant;                  ///< 앉은 탑승자의 오브젝트
        TComponentHandle<ControllerComponent> _occupantController;        ///< 탈 때 탑승자를 쥐고 있던 조종자. 운전석에서 쓰며 내릴 때 탑승자에게 돌려준다
        TComponentHandle<ControllerComponent> _previousVehicleController; ///< 탈 때 탈것에 빙의해 있던 조종자. 주인을 따라오던 말 AI 가 해당하며, 내리면 다시 빙의한다
        float4x4                              _seatInVehicle;             ///< 탈 때 붙인 소켓 변환(탈것 기준). 기울임은 이 위에 얹는다
        float3                                _lastSeatPosition;          ///< 지난 틱의 좌석 월드 위치
        float3                                _lastSeatVelocity;          ///< 지난 틱의 좌석 월드 속도
        float3                                _rideAcceleration;          ///< 매끄럽게 한 좌석 가속도(탈것 기준)
        float2                                _riderLean;                 ///< 지금 탑승자 기울임
        float32                               _pendingFieldOfViewKick;    ///< 틱 뒤 카메라에 걸 시야각 킥(0 이면 없음)
        float32                               _pendingBumpShake;          ///< 틱 뒤 카메라에 걸 충격 크기(0 이면 없음)
        float32                               _pendingFeedbackSpeed;      ///< 틱 뒤 소리에 넘길 탈것 속력(m/s)
        float32                               _pendingFeedbackSlip;       ///< 틱 뒤 소리에 넘길 옆 미끄럼 가속(m/s²)
        int32                                 _rideSampleCount;           ///< 잰 걸음 수(0..2). 위치 · 속도가 모두 있어야 가속도를 낸다
        uint8                                 _bFieldOfViewKickArmed : 1; ///< 앞 가속이 문턱 아래로 내려가야 다음 킥을 건다
        uint8                                 _bBumpArmed            : 1; ///< 수직 가속이 문턱 아래로 내려가야 다음 충격을 건다
        uint8                                 _bRidePending          : 1; ///< 틱이 반응을 쟀고 `VehicleSeatSystem` 이 아직 걸지 않았다
        [[maybe_unused]] uint8                _reservedRideFlags     : 5;

        /** @brief 카메라 킥 · 충격과 소리 파라미터를 겁니다(오디오 · 카메라 디렉터는 게임 스레드에서만 부른다). */
        void applyRideFeedback();
        /** @brief 탑승자의 소켓 변환에 기울임을 얹고 애니메이터 파라미터를 씁니다. */
        void applyRiderLean( GameObjectManager& manager );
    };
} // namespace sw
