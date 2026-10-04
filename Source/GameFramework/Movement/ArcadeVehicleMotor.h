/**
 * @file ArcadeVehicleMotor.h
 * @brief 아케이드 차량 몸 — 물리 엔진 없이 XZ 평면 + 높이로 가속 · 브레이크 · 후진 · 속도에 따라 둔해지는 조향 · 옆 미끄러짐(접지) · 드리프트 ·
 *        미니터보 단계 · 부스트 · 니트로 게이지 · 오프로드 감속 · 점프와 착지입니다.
 * @details 카트라이더 · 마리오카트 · 배틀로얄 차량 · 레드 데드 마차가 같은 몸을 씁니다. 좌표는 +Y 위, 진행 방향(yaw) 0 이 +Z, 양의 yaw 가 오른쪽(+X) 회전입니다.
 *          **벽 · 차끼리 충돌은 하지 않습니다** — 게임(또는 물리)이 위치 · 속도를 고쳐 넣습니다(`setPosition` · `setVelocity` · `addImpulse`).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Utility/Countdown.h"
#include "GameFramework/Utility/EventBuffer.h"
#include "GameFramework/Utility/FixedStepTimer.h"

namespace sw
{
    /**
     * @class IVehicleGround
     * @brief 차 밑의 땅입니다 — 높이와 그 자리의 속도 배율(길 1, 풀밭 · 모래 < 1). 게임이 지형 · 높이맵 · 트랙 메시로 답합니다.
     */
    class SW_GF_API IVehicleGround
    {
    public:
        IVehicleGround()          = default;
        virtual ~IVehicleGround() = default;

        IVehicleGround( const IVehicleGround& )            = default;
        IVehicleGround& operator=( const IVehicleGround& ) = default;

        virtual float32 sampleHeight( float32 x, float32 z ) const = 0;
        /** @brief 그 자리의 최고 속도 배율입니다(1 = 길, 0.5 = 풀밭 — 오프로드). */
        virtual float32 sampleSpeedScale( float32 x, float32 z ) const
        {
            (void)x;
            (void)z;
            return 1.0f;
        }
    };
} // namespace sw

namespace sw
{
    /** @brief 미니터보 단계 수(마리오카트의 파랑 · 주황 · 보라)입니다. */
    constexpr int32 kVehicleMiniTurboTierCount = 3;

    /** @brief 차 한 종류의 수치입니다. 속도는 월드 단위/초, 조향은 라디안/초입니다. */
    struct ArcadeVehicleSettings
    {
        float32 _maxSpeed{ 30.0f };
        float32 _acceleration{ 18.0f };
        float32 _brakeDeceleration{ 40.0f };
        float32 _reverseMaxSpeed{ 8.0f };
        float32 _coastDrag{ 6.0f };                                                 ///< 가속 페달을 떼면 줄어드는 속도(/초)
        float32 _overSpeedDeceleration{ 25.0f };                                    ///< 상한을 넘으면(부스트 끝 · 오프로드) 이만큼씩 줄인다
        float32 _steerRate{ 2.4f };                                                 ///< 느릴 때의 조향 속도
        float32 _steerRateAtMaxSpeed{ 1.1f };                                       ///< 최고 속도의 조향 속도(빠를수록 크게 돈다)
        float32 _steerMinSpeed{ 2.0f };                                             ///< 이보다 느리면 조향이 0 쪽으로 준다(제자리에서 돌지 않는다)
        float32 _grip{ 10.0f };                                                     ///< 옆 미끄러짐 감쇠(/초) — 클수록 레일 위처럼
        float32 _driftGrip{ 4.0f };                                                 ///< 드리프트 중 접지(낮을수록 크게 미끄러진다)
        float32 _driftBaseSteer{ 0.7f };                                            ///< 드리프트 중 조향 입력이 없을 때의 회전(드리프트 방향으로)
        float32 _driftSteerBonus{ 0.5f };                                           ///< 드리프트 방향으로 꺾으면 더하고 반대로 꺾으면 뺀다(조이기 · 풀기)
        float32 _driftMinSpeed{ 10.0f };                                            ///< 드리프트를 시작 · 유지하는 최소 속도
        float32 _arrMiniTurboTime[kVehicleMiniTurboTierCount]{ 0.6f, 1.3f, 2.2f };  ///< 단계까지 드리프트 시간
        float32 _arrMiniTurboBoost[kVehicleMiniTurboTierCount]{ 0.5f, 1.0f, 1.6f }; ///< 단계의 부스트 시간
        float32 _boostSpeedBonus{ 8.0f };                                           ///< 부스트 중 최고 속도에 더한다
        float32 _boostAcceleration{ 45.0f };                                        ///< 부스트 중 가속(페달과 상관없이)
        float32 _boostImpulse{ 3.0f };                                              ///< 부스트를 켤 때 바로 더하는 앞 속도
        float32 _nitroBoostTime{ 2.0f };                                            ///< 니트로 한 개의 부스트 시간
        float32 _nitroFillPerSecond{ 0.35f };                                       ///< 드리프트 1 초에 차는 게이지(1 이면 니트로 하나)
        int32   _maxNitroCount{ 2 };
        float32 _offroadSensitivity{ 1.0f }; ///< 땅의 속도 배율을 얼마나 받는가(1 = 그대로, 0 = 무시 — 오프로드 차 · 말)
        float32 _jumpSpeed{ 7.0f };
        float32 _gravity{ 25.0f };
        float32 _airSteerScale{ 0.3f };           ///< 공중 조향 약화
        float32 _groundSnapDistance{ 0.3f };      ///< 땅이 이보다 더 꺼지면 떠오른다(턱에서 날기)
        uint8   _bBoostIgnoresOffroad{ SW_TRUE }; ///< 부스트 중에는 오프로드 감속이 없다(마리오카트 버섯)
    };
} // namespace sw

namespace sw
{
    /** @brief 한 걸음의 입력입니다. */
    struct ArcadeVehicleInput
    {
        float32 _throttle{ 0.0f }; ///< −1(브레이크 · 후진)..1
        float32 _steer{ 0.0f };    ///< −1(왼쪽)..1(오른쪽)
        uint8   _bDriftHeld{ SW_FALSE };
        uint8   _bBoostPressed{ SW_FALSE }; ///< 이번에 눌렀다(니트로 쓰기)
        uint8   _bJumpPressed{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /** @brief 차에 일어난 일입니다. */
    struct ArcadeVehicleEvent
    {
        enum class Kind : uint8
        {
            DriftStarted = 0, ///< _value: 방향(−1 · 1)
            DriftTierReached, ///< _value: 단계(1..3) — 불꽃 색
            DriftEnded,       ///< _value: 끝난 단계(0 이면 보상 없음)
            MiniTurbo,        ///< _value: 단계
            BoostStarted,     ///< _value: 0 니트로 · 1..3 미니터보 · −1 게임이 준 것
            BoostEnded,
            NitroCharged, ///< _value: 가진 니트로 수
            Jumped,
            LeftGround,
            Landed
        };
        int32 _value{ 0 };
        Kind  _kind{ Kind::DriftStarted };
    };
} // namespace sw

namespace sw
{
    /**
     * @class ArcadeVehicleMotor
     * @brief 고정 걸음(권장 1/60)으로 `update` 를 부르거나 `advance` 에 프레임 시간을 넘깁니다(안의 `FixedStepTimer` 가 나눈다 — 프레임과 상관없이 같은 결과).
     * @details 한 걸음: 땅 배율 → 앞 속도(가속 · 브레이크 · 후진 · 관성 · 부스트 · 상한) → 드리프트 → 조향(yaw) → 새 방향으로 속도를 나눠 옆 성분을 접지로 줄인다
     *          → 점프 · 중력 · 착지 → 위치. 드리프트는 꾹 누른 채 조향하면 그 방향으로 시작하고, 시간이 쌓이면 미니터보 단계가 오르며, 떼면 단계만큼 부스트합니다.
     *          드리프트하는 동안 니트로 게이지가 차서 1 이 될 때마다 니트로 하나(최대 `_maxNitroCount`)가 되고 `_bBoostPressed` 로 씁니다.
     */
    class SW_GF_API ArcadeVehicleMotor
    {
    public:
        ArcadeVehicleMotor();

        void setSettings( const ArcadeVehicleSettings& settings ) { _settings = settings; }
        /** @brief 땅입니다(빌려 쓴다). nullptr 이면 높이 0 의 평평한 길입니다. */
        void setGround( const IVehicleGround* pGround ) { _pGround = pGround; }
        /** @brief 위치 · 방향을 두고 멈춘 상태로 돌립니다(드리프트 · 부스트 · 니트로도 비운다). */
        void reset( const float3& position, float32 yaw );
        void setPosition( const float3& position ) { _position = position; }
        void setYaw( float32 yaw ) { _yaw = yaw; }
        void setVelocity( const float3& velocity ) { _velocity = velocity; }
        /** @brief 부딪힘 · 튕기기 — 속도에 더하고 드리프트를 끊습니다(보상 없음). */
        void addImpulse( const float3& impulse );
        /** @brief 게임이 주는 부스트(대시 판 · 버섯)입니다. 남은 시간보다 길면 바꿉니다. */
        void startBoost( float32 duration, int32 source = -1 );

        /** @brief 한 걸음 나아갑니다. */
        void update( const ArcadeVehicleInput& input, float32 deltaTime );
        /** @brief 프레임 시간을 고정 걸음으로 나눠 나아갑니다. 눌림 입력은 첫 걸음에만 줍니다. 걸음 수를 돌려줍니다. */
        int32 advance( const ArcadeVehicleInput& input, float32 frameTime );
        void  drainEvents( vector<ArcadeVehicleEvent>& outListEvent );
        /** @brief 쌓인 알림을 꺼내지 않고 버립니다(쓰지 않는 쪽 — 받을 목록을 만들어 복사하지 않는다). */
        void discardEvents() { _eventBuffer.clear(); }

        /** @brief 앞 속도 @p speed 에서 조향 1 의 회전 속도(라디안/초)입니다 — 조향 반경 = 속도 / 이 값. */
        float32 computeSteerRate( float32 speed ) const;
        /** @brief 지금 자리 · 부스트로 정해지는 최고 속도입니다. */
        float32 computeSpeedCap() const;
        float3  computeForward() const;

        const float3&                getPosition() const { return _position; }
        const float3&                getVelocity() const { return _velocity; }
        float32                      getYaw() const { return _yaw; }
        float32                      getForwardSpeed() const;
        float32                      getLateralSpeed() const;
        bool                         isDrifting() const { return _driftDirection != 0; }
        int32                        getDriftDirection() const { return _driftDirection; }
        float32                      getDriftCharge() const { return _driftCharge; }
        int32                        getDriftTier() const;
        float32                      getBoostTime() const { return _boost.getRemaining(); }
        bool                         isBoosting() const { return _boost.isActive(); }
        bool                         isAirborne() const { return _bAirborne != SW_FALSE; }
        float32                      getNitroGauge() const { return _nitroGauge; }
        int32                        getNitroCount() const { return _nitroCount; }
        const ArcadeVehicleSettings& getSettings() const { return _settings; }
        FixedStepTimer&              getTimer() { return _timer; }

    private:
        float32 sampleHeight( float32 x, float32 z ) const;
        float32 sampleSurfaceScale() const;
        void    updateForwardSpeed( const ArcadeVehicleInput& input, float32 deltaTime, float32& inoutForwardSpeed ) const;
        void    updateDrift( const ArcadeVehicleInput& input, float32 forwardSpeed, float32 deltaTime );
        void    endDrift( bool bReward );
        void    updateVertical( const ArcadeVehicleInput& input, float32 deltaTime );
        void    pushEvent( ArcadeVehicleEvent::Kind kind, int32 value );

        ArcadeVehicleSettings           _settings;
        EventBuffer<ArcadeVehicleEvent> _eventBuffer;
        FixedStepTimer                  _timer;
        const IVehicleGround*           _pGround;
        float3                          _position;
        float3                          _velocity; ///< 월드 속도(_y 는 위아래)
        float32                         _yaw;
        float32                         _driftCharge; ///< 이번 드리프트의 시간
        Countdown                       _boost;
        float32                         _nitroGauge; ///< 0..1
        int32                           _nitroCount;
        int32                           _driftDirection; ///< −1 왼쪽 · 1 오른쪽 · 0 드리프트 아님
        uint8                           _bAirborne;
    };
} // namespace sw
