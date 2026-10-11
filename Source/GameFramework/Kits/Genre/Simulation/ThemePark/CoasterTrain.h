/**
 * @file CoasterTrain.h
 * @brief 롤러코스터 열차 물리(중력, 구름 저항, 공기 저항, 체인, 부스터, 브레이크)와 승차 평가(흥분, 강도, 멀미)를 구현합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Math/Math.h"

#include "Engine/Physics/PhysicsSystem.h"

#include "GameFramework/Base/Foundation/Utility/Time/FixedStepTimer.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Simulation/ThemePark/CoasterTrack.h"

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) 물리 매개변수
    // ------------------------------------------------------------------------------
    /** @brief 열차 물리의 상수입니다. 단위는 m, s, m/s, m/s^2 입니다. */
    struct CoasterPhysicsParams
    {
        float32 _gravity{ PhysicsSystem::getConfiguredGravityMagnitude() }; ///< 기본은 설정된 물리 중력이다
        float32 _rollingResistance{ 0.012f };                               ///< 구름 저항 계수 μ 이다. 감속은 μg 이다
        float32 _dragCoefficient{ 0.0004f };                                ///< 공기 저항이다. 감속은 k·v^2 이다
        float32 _liftSpeed{ 4.0f };                                         ///< 체인이 끄는 속도이다. 열차가 그보다 느리면 이 속도로 끈다
        float32 _stationSpeed{ 3.0f };                                      ///< 스테이션이 맞추는 속도(출발과 도착)
        float32 _boosterSpeed{ 22.0f };                                     ///< 부스터가 올려 주는 속도 상한
        float32 _boosterAcceleration{ 9.0f };
        float32 _brakeSpeed{ 5.0f }; ///< 브레이크가 내리는 속도
        float32 _brakeDeceleration{ 10.0f };
        float32 _fixedStep{ 1.0f / 240.0f };  ///< 적분 간격이다. `step` 은 프레임 시간을 이 간격으로 나눠 돈다
        float32 _stallSpeed{ 0.3f };          ///< 승차 평가에서 이 속도보다 느리면 "거의 멈춤" 으로 본다(m/s)
        float32 _stallSeconds{ 3.0f };        ///< 승차 평가에서 체인, 스테이션, 부스터 밖에서 이 시간 동안 거의 멈춰 있으면 멈춤으로 본다(s)
        float32 _gForceWarmupSeconds{ 0.5f }; ///< 승차 평가에서 출발 직후 이 시간 동안은 G 를 측정하지 않는다(첫 차분이 없기 때문이다, s)
    };
} // namespace sw

namespace sw
{
    /** @brief 탑승자가 느끼는 G 입니다(중력 포함이며 1 은 서 있을 때). 수직은 좌석 위 방향이 +, 수평은 오른쪽이 + 입니다. */
    struct CoasterGForce
    {
        float32 _vertical{ 1.0f };
        float32 _lateral{ 0.0f };
        float32 _longitudinal{ 0.0f };
    };
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // 2) 열차
    // ------------------------------------------------------------------------------
    /**
     * @class CoasterTrain
     * @brief 트랙 위 거리 `s` 와 속도 `v` 하나로 도는 열차입니다. 차량은 같은 속도로 뒤따릅니다(`getCarFrame`).
     * @details 적분은 고정 간격의 반암시적 오일러이다. `a = -g·forward.y - μg·sign(v) - k·v|v|` 로 가속한 다음, 구간 표시(체인, 부스터, 브레이크, 스테이션)가
     *          속도를 고친다. 속도가 음수가 될 수 있다(언덕을 못 넘으면 뒤로 굴러 내려오며, 실제 코스터의 롤백과 같다). G 는 속도 벡터의 차분과 중력으로 구한다.
     *          트랙은 빌려 쓰므로 열차보다 오래 살아야 한다.
     */
    class SW_GF_API CoasterTrain
    {
    public:
        CoasterTrain();

        /** @brief 트랙과 매개변수를 정하고 @p startDistance 에 멈춘 채로 둡니다. */
        void initialize( const CoasterTrack* pTrack, const CoasterPhysicsParams& params, float32 startDistance = 0.0f );
        /** @brief @p deltaTime 만큼 진행합니다(고정 간격으로 나눠 돕니다). */
        void step( float32 deltaTime );

        /** @brief 위치를 바꿉니다(속도는 그대로이고 바퀴 수는 0 이 됩니다). */
        void setDistance( float32 distance );
        void setSpeed( float32 speed ) { _speed = speed; }

        float32 getDistance() const { return _distance; }
        float32 getSpeed() const { return _speed; }
        float32 getElapsedTime() const { return _elapsedTime; }
        /** @brief 시작점을 앞으로 지난 횟수를 반환합니다(닫힌 회로). 뒤로 지나면 하나 줄어듭니다. */
        int32                getLapCount() const { return _lapCount; }
        const CoasterGForce& getGForce() const { return _gForce; }
        /** @brief 앞 차량의 좌표계를 반환합니다. */
        CoasterTrackFrame getFrame() const;
        /** @brief @p carIndex 번째 차량(0 이 맨 앞)의 좌표계를 반환합니다. 앞 차량에서 @p carSpacing x 번호만큼 뒤에 있습니다. */
        CoasterTrackFrame   getCarFrame( uint32 carIndex, float32 carSpacing ) const;
        const CoasterTrack* getTrack() const { return _pTrack; }

    private:
        void integrate( float32 deltaTime );

        CoasterPhysicsParams _params;
        CoasterGForce        _gForce;
        float3               _previousVelocity;
        const CoasterTrack*  _pTrack;
        float32              _distance;
        float32              _speed;
        float32              _elapsedTime;
        FixedStepTimer       _stepTimer;
        int32                _lapCount;
        uint8                _bHasPreviousVelocity;
    };
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // 3) 승차 평가: 롤러코스터 타이쿤의 세 평가 축
    // ------------------------------------------------------------------------------
    /**
     * @brief 한 바퀴를 탄 결과입니다. 평가 셋은 0..10 이며 롤러코스터 타이쿤과 같은 축(흥분, 강도, 멀미)입니다. 식은 단순하게 따로 정했습니다.
     * @details 에어타임은 수직 G 가 0 아래인 시간이고, 뒤집힘은 좌석 위가 땅을 향했다가 돌아온 횟수이며, 낙하는 2 m 넘게 내려간 구간 수이다.
     */
    struct CoasterRideStats
    {
        float32 _lapTime{ 0.0f };
        float32 _length{ 0.0f };
        float32 _maxSpeed{ 0.0f };
        float32 _averageSpeed{ 0.0f };
        float32 _maxHeight{ 0.0f };
        float32 _maxDropHeight{ 0.0f };
        float32 _maxVerticalG{ 1.0f };
        float32 _minVerticalG{ 1.0f };
        float32 _maxLateralG{ 0.0f };
        float32 _airtime{ 0.0f };
        float32 _excitement{ 0.0f };
        float32 _intensity{ 0.0f };
        float32 _nausea{ 0.0f };
        uint32  _dropCount{ 0 };
        uint32  _inversionCount{ 0 };
        uint8   _bCompleted{ SW_FALSE }; ///< 한 바퀴를 돌아 시작점을 지났는지 나타낸다
        uint8   _bStalled{ SW_FALSE };   ///< 어디선가 멈춰 버렸는지 나타낸다(언덕을 못 넘은 경우)
    };
} // namespace sw

namespace sw
{
    /**
     * @class CoasterRideAnalyzer
     * @brief 열차를 스테이션에서 띄워 한 바퀴를 시뮬레이션하고 `CoasterRideStats` 를 만듭니다(타이쿤의 "테스트 운행" 에 해당합니다).
     * @details 실시간 열차와 같은 물리를 쓴다. 시작점은 거리 0 이고 속도 0 에서 출발한다(스테이션과 체인이 끌어 준다). @p maxTime 안에
     *          한 바퀴를 못 돌거나 3초 넘게 거의 멈춰 있으면(체인, 스테이션, 부스터 밖) 멈춤으로 보고 끝낸다.
     */
    struct SW_GF_API CoasterRideAnalyzer
    {
        static CoasterRideStats analyze( const CoasterTrack& track, const CoasterPhysicsParams& params, float32 maxTime = 600.0f );
        /** @brief 측정값으로 세 평가를 채웁니다. `analyze` 가 호출하며, 측정을 따로 모은 쪽도 쓸 수 있게 공개합니다. */
        static void computeRatings( CoasterRideStats& inoutStats );
    };
} // namespace sw
