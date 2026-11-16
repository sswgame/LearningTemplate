/**
 * @file KinematicVehicleMotor.h
 * @brief 물리 바디 없이 힘을 적분하는 탈것의 몸(관성 이동)과 그 튜닝 데이터 · 조작감 모듈을 정의합니다.
 * @details 한 걸음은 구동력(페달 · 걸음 · 밀기) + 경사 중력(중력의 지면 접선 성분) − 구름 저항(디딘 표면) − 공기 저항(속도²) 를 질량으로 적분한다.
 *          조작감은 모듈 조합이다 — 그립 · 드리프트 · 부스트 · 공중 제어(점프) · 호버 · 레일. 카트 = 그립 · 드리프트 · 부스트 · 공중 제어,
 *          썰매 = 약한 그립 · 구동 없음, 반중력 = 호버 · 부스트, 광산 수레 = 레일. 모듈은 켜고 끄는 칸과 매개변수이고 처리는 모터의 한 걸음 안에 있다
 *          (상태가 StateIO 한 벌로 실려 결정적 리플레이 · 롤백이 깨지지 않게 — 모듈마다 가상 객체를 두면 상태가 흩어진다).
 *          물리 바디가 있는 탈것은 `WheeledVehicleComponent`(Jolt 바퀴) 몫이고 두 모터를 섞지 않는다 — 표면 마찰만 같은 표면 종류에서 읽는다.
 *          언리얼 Chaos Vehicle(바디) 대 단순 이동 컴포넌트, 유니티 Rigidbody 대 CharacterController 의 경계와 같다.
 *          좌표는 +Y 가 위이고, 진행 방향(yaw) 0 이 +Z 이며, 양의 yaw 가 오른쪽(+X) 회전이다.
 *          **벽이나 차끼리의 충돌은 하지 않는다.** 게임(또는 물리)이 위치와 속도를 고쳐 넣는다(`setPosition`, `setVelocity`, `addImpulse`).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Event/EventBuffer.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Time/Countdown.h"
#include "Core/Time/FixedStepTimer.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Content/AssetID.h"
#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Character/Movement/InertiaSettings.h"

namespace sw
{
    class Archive;
    class SplinePath;

    /** @brief 차 밑 한 점의 표면입니다. 땅(`IVehicleGround`)이 답합니다. */
    struct VehicleSurface
    {
        float32 _speedScale{ 1.0f };        ///< 최고 속도 배율(길은 1, 풀밭 · 모래는 1 미만)
        float32 _rollingResistance{ 0.0f }; ///< 구름 저항 계수(무차원 — 감속 = 계수 × 중력 × cos 경사). 빙판 낮고 모래 · 눈 높다
        float32 _gripScale{ 1.0f };         ///< 옆 접지 배율(빙판은 1 미만)
    };
} // namespace sw

namespace sw
{
    /**
     * @class IVehicleGround
     * @brief 차 밑의 땅입니다. 높이와 표면을 게임이 지형, 높이맵, 트랙 메시, 물리 광선으로 답합니다.
     */
    class SW_GF_API IVehicleGround
    {
    public:
        IVehicleGround()          = default;
        virtual ~IVehicleGround() = default;

        IVehicleGround( const IVehicleGround& )            = default;
        IVehicleGround& operator=( const IVehicleGround& ) = default;

        virtual float32        sampleHeight( float32 x, float32 z ) const  = 0;
        virtual VehicleSurface sampleSurface( float32 x, float32 z ) const = 0;
    };
} // namespace sw

namespace sw
{
    /** @brief 미니터보 단계 수입니다(파랑 · 주황 · 보라). */
    constexpr int32 kVehicleMiniTurboTierCount = 3;

    /** @brief 그립 모듈 — 옆 미끄러짐을 앞으로 돌립니다. 끄면 옆 속도가 그대로 남습니다(호버 · 빙판 썰매). */
    REFLECT()
    struct VehicleGripModule
    {
        REFLECT_BODY();

        PROPERTY()
        bool _bEnabled{ true };
        PROPERTY( Min = 0.0, Units = Hz, Tooltip = "Side slip damping; higher runs like on rails" )
        float32 _lateralGrip{ 10.0f };
    };

    /** @brief 드리프트 모듈 — 버튼을 누른 채 조향하면 미끄러지며 돌고, 시간이 쌓이면 미니터보 단계와 니트로 게이지가 찹니다. 미니터보 보상은 부스트 모듈이 켜져야 듭니다. */
    REFLECT()
    struct VehicleDriftModule
    {
        REFLECT_BODY();

        PROPERTY()
        bool _bEnabled{ true };
        PROPERTY( Min = 0.0, Units = Hz, Tooltip = "Side grip while drifting (lower slides more)" )
        float32 _driftGrip{ 4.0f };
        PROPERTY( Tooltip = "Turn while drifting with no steer input (toward the drift side)" )
        float32 _baseSteer{ 0.7f };
        PROPERTY( Tooltip = "Steering toward the drift side adds, away subtracts (tighten / loosen)" )
        float32 _steerBonus{ 0.5f };
        PROPERTY( Min = 0.0, Units = "m/s", Tooltip = "Least speed to start and keep a drift" )
        float32 _minSpeed{ 10.0f };
        PROPERTY( Units = s, Tooltip = "Drift time to reach each mini-turbo tier" )
        float32 _arrMiniTurboTime[kVehicleMiniTurboTierCount]{ 0.6f, 1.3f, 2.2f };
        PROPERTY( Units = s, Tooltip = "Boost time each tier gives" )
        float32 _arrMiniTurboBoost[kVehicleMiniTurboTierCount]{ 0.5f, 1.0f, 1.6f };
        PROPERTY( Min = 0.0, Tooltip = "Nitro gauge filled per drift second (1 = one nitro)" )
        float32 _nitroFillPerSecond{ 0.35f };
        PROPERTY( Min = 0 )
        int32 _maxNitroCount{ 2 };
    };

    /** @brief 부스트 모듈 — 최고 속도를 올리고 페달과 상관없이 밉니다(니트로 · 미니터보 · 대시 판). */
    REFLECT()
    struct VehicleBoostModule
    {
        REFLECT_BODY();

        PROPERTY()
        bool _bEnabled{ true };
        PROPERTY( Min = 0.0, Units = "m/s", Tooltip = "Added to the top speed while boosting" )
        float32 _speedBonus{ 8.0f };
        PROPERTY( Min = 0.0, Units = "m/s2", Tooltip = "Push while boosting, regardless of the pedal" )
        float32 _acceleration{ 45.0f };
        PROPERTY( Min = 0.0, Units = "m/s", Tooltip = "Forward speed added at once when a boost starts" )
        float32 _impulse{ 3.0f };
        PROPERTY( Min = 0.0, Units = s, Tooltip = "Boost time of one nitro" )
        float32 _nitroBoostTime{ 2.0f };
        PROPERTY( Tooltip = "No off-road slowdown while boosting" )
        bool _bIgnoresOffroad{ true };
    };

    /** @brief 공중 제어 모듈 — 점프와 공중 조향, 공중에서의 낙하 중력(게임 손맛 — 경사 중력과 따로)입니다. 끄면 점프가 없고 공중 조향이 0 이며 낙하는 몸의 중력입니다. */
    REFLECT()
    struct VehicleAirControlModule
    {
        REFLECT_BODY();

        PROPERTY()
        bool _bEnabled{ true };
        PROPERTY( Min = 0.0, Units = "m/s" )
        float32 _jumpSpeed{ 7.0f };
        PROPERTY( Min = 0.0, Max = 1.0, Tooltip = "Steering scale in the air" )
        float32 _airSteerScale{ 0.3f };
        PROPERTY( Min = 0.0, Units = "m/s2", Tooltip = "Fall gravity in the air" )
        float32 _fallGravity{ 25.0f };
    };

    /** @brief 호버 모듈 — 땅 위 일정 높이에 용수철로 뜹니다. 바퀴가 닿지 않으므로 구름 저항이 없고 경사의 당김은 `_slopeScale` 만큼입니다. */
    REFLECT()
    struct VehicleHoverModule
    {
        REFLECT_BODY();

        PROPERTY()
        bool _bEnabled{ false };
        PROPERTY( Min = 0.0, Units = m )
        float32 _height{ 0.8f };
        PROPERTY( Min = 0.0, Tooltip = "Spring stiffness toward the hover height" )
        float32 _stiffness{ 60.0f };
        PROPERTY( Min = 0.0, Units = Hz, Tooltip = "Spring damping" )
        float32 _damping{ 10.0f };
        PROPERTY( Min = 0.0, Tooltip = "Slope pull scale while hovering" )
        float32 _slopeScale{ 0.3f };
    };

    /** @brief 레일 모듈 — 곡선(`setRail`) 위 거리만 움직입니다. 조향 · 옆 미끄러짐이 없고 경사는 레일의 기울기이며, 열린 레일 끝에서 멈춥니다(광산 수레 · 롤러코스터). */
    REFLECT()
    struct VehicleRailModule
    {
        REFLECT_BODY();

        PROPERTY()
        bool _bEnabled{ false };
        PROPERTY( Min = 0.0, Tooltip = "Rolling resistance of the wheels on the rail (steel on steel is about 0.002)" )
        float32 _rollingResistance{ 0.002f };
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 탈것 한 종류의 튜닝 데이터입니다(`*.vehicle.xml`, 루트 `KinematicVehicleSettings`). 속도는 m/s, 조향은 rad/s, 힘은 N 입니다.
     * @details 프리셋 · 변형은 데이터 상속(`_base` — 무거운 썰매 = `_inertia` 의 `_mass` 한 칸), 배치는 변형 파일을 가리킨다(`KinematicVehicleComponent::_tuning`).
     *          기본값은 카트(그립 · 드리프트 · 부스트 · 공중 제어)다.
     */
    REFLECT()
    struct SW_GF_API KinematicVehicleSettings
    {
        REFLECT_BODY();
        friend struct AssetTypeLoader;

        /** @brief 이 데이터 종류입니다(`*.vehicle.xml`). 등록은 `KinematicVehicleMotor.cpp` 의 공유 캐시 줄이 로더를 더한다. */
        static constexpr AssetTypeDef kAssetType = AssetTypeDef::makeDataFile( "kinematicvehicle", AssetPathRule{ {}, {}, ".vehicle.xml" }, {} );

        PROPERTY( Min = 0.0, Units = "m/s", Tooltip = "Top speed the drive pushes to" )
        float32 _maxSpeed{ 30.0f };
        PROPERTY( Min = 0.0, Units = N, Tooltip = "Drive force at full pedal (0: no drive — sleds, boards)" )
        float32 _maxDriveForce{ 3600.0f };
        PROPERTY( Min = 0.0, Units = N )
        float32 _brakeForce{ 8000.0f };
        PROPERTY( Min = 0.0, Units = "m/s" )
        float32 _reverseMaxSpeed{ 8.0f };
        PROPERTY( Min = 0.0, Units = "m/s2", Tooltip = "Engine braking with the pedal released" )
        float32 _coastDrag{ 6.0f };
        PROPERTY( Min = 0.0, Units = "m/s2", Tooltip = "Slowdown above the top speed (boost end, entering off-road)" )
        float32 _overSpeedDeceleration{ 25.0f };
        PROPERTY( Min = 0.0, Units = "rad/s", Tooltip = "Turn rate at low speed" )
        float32 _steerRate{ 2.4f };
        PROPERTY( Min = 0.0, Units = "rad/s", Tooltip = "Turn rate at top speed (faster turns wider)" )
        float32 _steerRateAtMaxSpeed{ 1.1f };
        PROPERTY( Min = 0.0, Units = "m/s", Tooltip = "Below this speed steering fades to 0 (no turning in place)" )
        float32 _steerMinSpeed{ 2.0f };
        PROPERTY( Min = 0.0, Max = 1.0, Tooltip = "How much the ground speed scale applies (1 fully, 0 ignores it)" )
        float32 _offroadSensitivity{ 1.0f };
        PROPERTY( Min = 0.0, Units = m, Tooltip = "Ground dropping more than this lifts the vehicle off (ledges)" )
        float32 _groundSnapDistance{ 0.3f };
        PROPERTY( Tooltip = "Body: mass, gravity, slope pull, rolling resistance, air drag" )
        InertiaSettings _inertia;
        PROPERTY()
        VehicleGripModule _grip;
        PROPERTY()
        VehicleDriftModule _drift;
        PROPERTY()
        VehicleBoostModule _boost;
        PROPERTY()
        VehicleAirControlModule _airControl;
        PROPERTY()
        VehicleHoverModule _hover;
        PROPERTY()
        VehicleRailModule _rail;

        /** @brief 구동 가속도(m/s²)입니다. 최대 구동력 ÷ 질량입니다. */
        float32 computeDriveAcceleration() const { return _maxDriveForce / _inertia._mass; }
        /** @brief 브레이크 감속도(m/s²)입니다. */
        float32 computeBrakeDeceleration() const { return _brakeForce / _inertia._mass; }
        /** @brief 칸 사이의 규칙을 봅니다(미니터보 단계 시간 오름차순, 호버와 레일을 함께 켜지 않음). 어기면 오류를 남기고 false 입니다. 칸 범위는 PROPERTY 가 본다. */
        [[nodiscard]] bool validate( string_view sourceName ) const;

    private:
        /** @brief 리소스 파일에서 읽습니다(상속 `_base` 를 펼친다). 종류의 로더(공유 캐시)만 부른다. */
        [[nodiscard]] bool loadFromAsset( const AssetID& assetID );
    };

    /** @brief 튜닝 파일을 경로마다 한 벌 나눠 줍니다(인스턴스화는 `KinematicVehicleMotor.cpp`). */
    SW_DECLARE_SHARED_ASSET_CACHE( SW_GF_API_EXTERN_TEMPLATE, KinematicVehicleSettings );
} // namespace sw

namespace sw
{
    /** @brief 한 걸음의 입력입니다. */
    struct KinematicVehicleInput
    {
        float32 _throttle{ 0.0f }; ///< -1(브레이크, 후진)..1
        float32 _steer{ 0.0f };    ///< -1(왼쪽)..1(오른쪽). 썰매 · 보드는 기울임이다
        uint8   _bDriftHeld{ SW_FALSE };
        uint8   _bBoostPressed{ SW_FALSE }; ///< 이번에 눌렀다(니트로 쓰기)
        uint8   _bJumpPressed{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /** @brief 차에 일어난 일(이벤트)입니다. */
    struct KinematicVehicleEvent
    {
        enum class Kind : uint8
        {
            DriftStarted = 0, ///< _value: 방향(-1 또는 1)
            DriftTierReached, ///< _value: 단계(1..3). 불꽃 색을 정한다
            DriftEnded,       ///< _value: 끝난 단계(0 이면 보상 없음)
            MiniTurbo,        ///< _value: 단계
            BoostStarted,     ///< _value: 0 은 니트로, 1..3 은 미니터보, -1 은 게임이 준 부스트
            BoostEnded,
            NitroCharged, ///< _value: 가진 니트로 수
            Jumped,
            LeftGround,
            Landed,
            RailEnded ///< 열린 레일 끝에 닿았다
        };
        int32 _value{ 0 };
        Kind  _kind{ Kind::DriftStarted };
    };
} // namespace sw

namespace sw
{
    /**
     * @class KinematicVehicleMotor
     * @brief 관성 이동 탈것의 몸입니다. 고정 걸음(권장 1/60)으로 `update` 를 부르거나 `advance` 에 프레임 시간을 넘깁니다.
     * @details `advance` 는 안의 `FixedStepTimer` 가 프레임 시간을 고정 걸음으로 나누므로 프레임 속도와 상관없이 같은 결과가 나온다.
     *          한 걸음은 다음 순서로 진행한다(레일 모듈이면 `updateRail` 하나).
     *          땅 표면, 앞 속도(구동 · 브레이크 · 후진 · 부스트 · 상한), 바깥 힘(경사 · 구름 · 공기 — `InertiaSettings`), 드리프트, 조향(yaw)을 차례로 처리한다.
     *          그다음 새 방향으로 속도를 나눠 옆 성분을 접지(그립 × 표면 그립 배율)로 줄이고, 점프 · 중력 · 착지(호버면 용수철)를 처리한 뒤 위치를 갱신한다.
     *          드리프트는 버튼을 누른 채 조향하면 그 방향으로 시작한다. 시간이 쌓이면 미니터보 단계가 오르고, 버튼을 떼면 단계만큼 부스트한다.
     *          드리프트하는 동안 니트로 게이지가 차서 1 이 될 때마다 니트로가 하나 생기며(최대 `_maxNitroCount`), `_bBoostPressed` 로 쓴다.
     */
    class SW_GF_API KinematicVehicleMotor
    {
    public:
        /** @brief 경사를 재는 앞뒤 거리(m)입니다. 차 길이의 절반쯤 — 작은 요철에 흔들리지 않는다. */
        static constexpr float32 kSlopeProbeDistance = 0.5f;

        KinematicVehicleMotor();

        void setSettings( const KinematicVehicleSettings& settings ) { _settings = settings; }
        /** @brief 땅을 정합니다. 빌려 쓰며, nullptr 이면 높이 0 의 평평한 길(구름 저항 0)입니다. */
        void setGround( const IVehicleGround* pGround ) { _pGround = pGround; }
        /** @brief 레일 모듈의 곡선(월드)을 정합니다. 빌려 씁니다. 지금 위치에서 가장 가까운 거리로 올라탑니다. */
        void setRail( const SplinePath* pRail );
        /** @brief 위치와 방향을 두고 멈춘 상태로 돌립니다. 드리프트, 부스트, 니트로도 비웁니다. */
        void reset( const float3& position, float32 yaw );
        void setPosition( const float3& position ) { _position = position; }
        void setYaw( float32 yaw ) { _yaw = yaw; }
        void setVelocity( const float3& velocity ) { _velocity = velocity; }
        /** @brief 부딪힘이나 튕기기를 속도에 더하고 드리프트를 끊습니다. 드리프트 보상은 주지 않습니다. */
        void addImpulse( const float3& impulse );
        /** @brief 게임이 주는 부스트(대시 판, 버섯)를 겁니다. 남은 시간보다 길 때만 바꿉니다. 부스트 모듈이 꺼져 있으면 아무 일도 없습니다. */
        void startBoost( float32 duration, int32 source = -1 );

        /** @brief 한 걸음 나아갑니다. */
        void update( const KinematicVehicleInput& input, float32 deltaTime );
        /** @brief 프레임 시간을 고정 걸음으로 나눠 나아갑니다. 눌림 입력은 첫 걸음에만 주며, 진행한 걸음 수를 반환합니다. */
        int32 advance( const KinematicVehicleInput& input, float32 frameTime );
        void  pollEvents( vector<KinematicVehicleEvent>& outListEvent );
        /** @brief 쌓인 알림을 꺼내지 않고 버립니다. 알림을 쓰지 않는 쪽이 받을 목록을 만들어 복사하지 않도록 둔 함수입니다. */
        void discardEvents() { _eventBuffer.clear(); }

        /** @brief 앞 속도 @p speed 에서 조향 1 의 회전 속도(라디안/초)입니다. 조향 반경은 속도를 이 값으로 나눈 것입니다. */
        float32 computeSteerRate( float32 speed ) const;
        /** @brief 지금 위치와 부스트로 정해지는 최고 속도입니다. */
        float32 computeSpeedCap() const;
        float3  computeForward() const;
        /** @brief 지금 위치 · 방향의 오르막 사인입니다(+ 오르막). 땅의 앞뒤 높이 차로 잽니다. */
        float32 computeSlopeSin() const;

        const float3&                   getPosition() const { return _position; }
        const float3&                   getVelocity() const { return _velocity; }
        float32                         getYaw() const { return _yaw; }
        float32                         getForwardSpeed() const;
        float32                         getLateralSpeed() const;
        bool                            isDrifting() const { return _driftDirection != 0; }
        int32                           getDriftDirection() const { return _driftDirection; }
        float32                         getDriftCharge() const { return _driftCharge; }
        int32                           getDriftTier() const;
        float32                         getBoostTime() const { return _boost.getRemaining(); }
        bool                            isBoosting() const { return _boost.isActive(); }
        bool                            isAirborne() const { return _bAirborne != SW_FALSE; }
        float32                         getNitroGauge() const { return _nitroGauge; }
        int32                           getNitroCount() const { return _nitroCount; }
        float32                         getRailDistance() const { return _railDistance; }
        const KinematicVehicleSettings& getSettings() const { return _settings; }
        FixedStepTimer&                 getTimer() { return _timer; }

        /**
         * @brief 고정 스텝의 남은 시간, 위치, 속도, 요, 드리프트, 부스트, 니트로, 레일 거리, 공중 여부를 씁니다. 설정 · 지면 · 레일 포인터는 싣지 않고, 알림은 읽을 때 비웁니다.
         *        `writeState` 의 바이트로 상태를 바꿉니다. 바이트가 깨졌으면 false 를 반환하고 상태는 그대로입니다.
         */
        SW_DECLARE_STATE_IO();

    private:
        float32        sampleHeight( float32 x, float32 z ) const;
        VehicleSurface sampleSurface() const;
        void           updateForwardSpeed( const KinematicVehicleInput& input, float32 deltaTime, float32& inoutForwardSpeed ) const;
        void           updateDrift( const KinematicVehicleInput& input, float32 forwardSpeed, float32 deltaTime );
        void           endDrift( bool bReward );
        void           updateVertical( const KinematicVehicleInput& input, float32 deltaTime );
        void           updateHover( float32 deltaTime );
        void           updateRail( const KinematicVehicleInput& input, float32 deltaTime );
        void           pushEvent( KinematicVehicleEvent::Kind kind, int32 value );

    private:
        KinematicVehicleSettings           _settings;
        EventBuffer<KinematicVehicleEvent> _eventBuffer;
        FixedStepTimer                     _timer;
        const IVehicleGround*              _pGround;
        const SplinePath*                  _pRail;
        float3                             _position;
        float3                             _velocity; ///< 월드 속도(_y 는 위아래)
        float32                            _yaw;
        float32                            _driftCharge; ///< 이번 드리프트의 시간
        Countdown                          _boost;
        float32                            _nitroGauge;   ///< 0..1
        float32                            _railDistance; ///< 레일 위 거리(레일 모듈)
        int32                              _nitroCount;
        int32                              _driftDirection; ///< -1 은 왼쪽, 1 은 오른쪽, 0 은 드리프트 아님
        uint8                              _bAirborne;
    };
} // namespace sw
