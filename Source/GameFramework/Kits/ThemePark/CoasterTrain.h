/**
 * @file CoasterTrain.h
 * @brief 롤러코스터 열차 물리(중력 · 구름 저항 · 공기 저항 · 체인 · 부스터 · 브레이크)와 승차 평가(흥분 · 강도 · 멀미)입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Math/Math.h"

#include "GameFramework/Base/FixedStepTimer.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/ThemePark/CoasterTrack.h"

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) 물리 매개변수
    // ------------------------------------------------------------------------------
    /** @brief 열차 물리의 상수입니다. 단위는 m · s · m/s · m/s². */
    struct CoasterPhysicsParams
    {
        float32 _gravity{ 9.81f };
        float32 _rollingResistance{ 0.012f }; ///< 구름 저항 계수 μ — 감속 μg
        float32 _dragCoefficient{ 0.0004f };  ///< 공기 저항 — 감속 k·v²
        float32 _liftSpeed{ 4.0f };           ///< 체인이 끄는 속도 — 그보다 느리면 이 속도로 끈다
        float32 _stationSpeed{ 3.0f };        ///< 스테이션이 맞추는 속도(출발 · 도착)
        float32 _boosterSpeed{ 22.0f };       ///< 부스터가 올려 주는 속도 상한
        float32 _boosterAcceleration{ 9.0f };
        float32 _brakeSpeed{ 5.0f }; ///< 브레이크가 내리는 속도
        float32 _brakeDeceleration{ 10.0f };
        float32 _fixedStep{ 1.0f / 240.0f }; ///< 적분 간격 — `step` 은 프레임 시간을 이 간격으로 나눠 돈다
    };

    /** @brief 탑승자가 느끼는 G(중력 포함, 1 = 서 있을 때)입니다. 수직은 좌석 위 방향이 +, 수평은 오른쪽이 + 입니다. */
    struct CoasterGForce
    {
        float32 _vertical{ 1.0f };
        float32 _lateral{ 0.0f };
        float32 _longitudinal{ 0.0f };
    };

    // ------------------------------------------------------------------------------
    // 2) 열차
    // ------------------------------------------------------------------------------
    /**
     * @class CoasterTrain
     * @brief 트랙 위 거리 `s` 와 속도 `v` 하나로 도는 열차입니다(차량은 같은 속도로 뒤따른다 — `getCarFrame`).
     * @details 적분은 고정 간격의 반암시적 오일러입니다: `a = −g·forward.y − μg·sign(v) − k·v|v|`, 그다음 구간 표시(체인 · 부스터 · 브레이크 · 스테이션)가
     *          속도를 고칩니다. 속도가 음수가 될 수 있습니다(언덕을 못 넘으면 뒤로 굴러 내려온다 — 실제 코스터의 롤백). G 는 속도 벡터의 차분 + 중력으로 구합니다.
     *          트랙은 빌려 씁니다 — 열차보다 오래 살아야 합니다.
     */
    class SW_GF_API CoasterTrain
    {
    public:
        CoasterTrain();

        /** @brief 트랙 · 매개변수를 정하고 @p startDistance 에 멈춘 채로 둡니다. */
        void initialize( const CoasterTrack* pTrack, const CoasterPhysicsParams& params, float32 startDistance = 0.0f );
        /** @brief @p deltaTime 만큼 진행합니다(고정 간격으로 나눠 돈다). */
        void step( float32 deltaTime );

        /** @brief 위치를 바꿉니다(속도는 그대로, 바퀴 수는 0). */
        void setDistance( float32 distance );
        void setSpeed( float32 speed ) { _speed = speed; }

        float32 getDistance() const { return _distance; }
        float32 getSpeed() const { return _speed; }
        float32 getElapsedTime() const { return _elapsedTime; }
        /** @brief 시작점을 앞으로 지난 횟수입니다(닫힌 회로). 뒤로 지나면 하나 줄어든다. */
        int32                getLapCount() const { return _lapCount; }
        const CoasterGForce& getGForce() const { return _gForce; }
        /** @brief 앞 차량의 좌표계입니다. */
        CoasterTrackFrame getFrame() const;
        /** @brief @p carIndex 번째 차량(0 이 맨 앞)의 좌표계입니다 — 앞 차량에서 @p carSpacing × 번호만큼 뒤입니다. */
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

    // ------------------------------------------------------------------------------
    // 3) 승차 평가 — 롤러코스터 타이쿤의 세 축
    // ------------------------------------------------------------------------------
    /**
     * @brief 한 바퀴를 탄 결과입니다. 평가 셋은 0..10 입니다(롤러코스터 타이쿤과 같은 축 — 흥분 · 강도 · 멀미. 식은 단순하게 따로 정했다).
     * @details 에어타임은 수직 G 가 0 아래인 시간, 뒤집힘은 좌석 위가 땅을 향했다가 돌아온 횟수, 낙하는 2 m 넘게 내려간 구간 수입니다.
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
        uint8   _bCompleted{ SW_FALSE }; ///< 한 바퀴를 돌아 시작점을 지났는가
        uint8   _bStalled{ SW_FALSE };   ///< 어디선가 멈춰 버렸는가(언덕을 못 넘었다)
    };

    /**
     * @class CoasterRideAnalyzer
     * @brief 열차를 스테이션에서 띄워 한 바퀴를 시뮬레이션하고 `CoasterRideStats` 를 냅니다(타이쿤의 "시험 운행").
     * @details 실시간 열차와 같은 물리를 씁니다. 시작점은 거리 0 이고 속도 0 에서 출발합니다(스테이션 · 체인이 끌어 준다). @p maxTime 안에
     *          한 바퀴를 못 돌거나 3 초 넘게 거의 멈춰 있으면(체인 · 스테이션 · 부스터 밖) 멈춤으로 보고 끝냅니다.
     */
    struct SW_GF_API CoasterRideAnalyzer
    {
        static CoasterRideStats analyze( const CoasterTrack& track, const CoasterPhysicsParams& params, float32 maxTime = 600.0f );
        /** @brief 측정값으로 세 평가를 채웁니다(`analyze` 가 부른다 — 측정을 따로 모은 쪽도 쓸 수 있게 공개). */
        static void computeRatings( CoasterRideStats& inoutStats );
    };
} // namespace sw
