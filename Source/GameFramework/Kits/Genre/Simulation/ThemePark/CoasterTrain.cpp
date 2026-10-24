#include "pch.h"

#include "GameFramework/Kits/Genre/Simulation/ThemePark/CoasterTrain.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    namespace
    {
        struct CoasterTrainInternal
        {
            static constexpr float32 kDropThreshold = 2.0f;  ///< 이만큼 넘게 내려가야 낙하로 센다(m)
            static constexpr float32 kDropRecovery  = 1.0f;  ///< 바닥에서 이만큼 올라오면 낙하가 끝난 것이다(m)
            static constexpr float32 kInversionUpY  = -0.2f; ///< 좌석 위의 y 가 이보다 작으면 뒤집힌 것이다
            static constexpr float32 kUprightUpY    = 0.2f;  ///< 이보다 크면 바로 선 것이다
            static constexpr uint8   kDrivenFlags   = CoasterSegmentFlag::kLift | CoasterSegmentFlag::kStation | CoasterSegmentFlag::kBooster;

            /** @brief 매개변수가 범위 안인지 봅니다. 밖이면 오류를 남기고 false 입니다(자르지 않는다). 적분 간격은 1/2000..1/30 초여야 한 프레임이 수천 번의 적분이 되지 않고 적분이 거칠어지지 않는다. */
            [[nodiscard]] static bool acceptParams( const CoasterPhysicsParams& params )
            {
                const float32 minFixedStep = 1.0f / 2000.0f;
                const float32 maxFixedStep = 1.0f / 30.0f;
                const bool    bStepValid   = minFixedStep <= params._fixedStep && params._fixedStep <= maxFixedStep;
                const bool    bNonNegative = 0.0f <= params._gravity && 0.0f <= params._rollingResistance && 0.0f <= params._dragCoefficient && 0.0f <= params._liftSpeed &&
                                          0.0f <= params._stationSpeed && 0.0f <= params._boosterSpeed && 0.0f <= params._boosterAcceleration && 0.0f <= params._brakeSpeed &&
                                          0.0f <= params._brakeDeceleration && 0.0f <= params._stallSpeed && 0.0f <= params._stallSeconds && 0.0f <= params._gForceWarmupSeconds;
                if ( bStepValid && bNonNegative )
                    return true;
                SW_LOG_ERROR( "CoasterPhysicsParams: refused - fixedStep %# (needs %#..%#), the other constants need 0 or more", params._fixedStep, minFixedStep, maxFixedStep );
                return false;
            }

            /** @brief 낙하 세기입니다. 꼭대기에서 `kDropThreshold` 넘게 내려가기 시작해 바닥을 찍고 `kDropRecovery` 올라오면 하나로 셉니다. */
            struct DropTracker
            {
                float32 _peakHeight{ 0.0f };
                float32 _valleyHeight{ 0.0f };
                bool    _bFalling{ false };

                /** @brief 이번 걸음의 높이를 보탭니다. 낙하가 끝났으면 @p inoutStats 에 셉니다. */
                void observe( float32 height, CoasterRideStats& inoutStats )
                {
                    if ( _bFalling == false )
                    {
                        if ( height > _peakHeight )
                        {
                            _peakHeight = height;
                        }
                        else if ( _peakHeight - height > kDropThreshold )
                        {
                            _bFalling     = true;
                            _valleyHeight = height;
                        }
                        return;
                    }
                    if ( height < _valleyHeight )
                    {
                        _valleyHeight = height;
                    }
                    else if ( height - _valleyHeight > kDropRecovery )
                    {
                        countDrop( inoutStats );
                        _bFalling   = false;
                        _peakHeight = height;
                    }
                }

                /** @brief 끝날 때 아직 떨어지는 중이면 그 낙하도 셉니다. */
                void finish( CoasterRideStats& inoutStats ) const
                {
                    if ( _bFalling )
                        countDrop( inoutStats );
                }

                void countDrop( CoasterRideStats& inoutStats ) const
                {
                    ++inoutStats._dropCount;
                    inoutStats._maxDropHeight = MathUtil::max( inoutStats._maxDropHeight, _peakHeight - _valleyHeight );
                }
            };
        };
    } // namespace
} // namespace sw

namespace sw
{
    CoasterTrain::CoasterTrain()
        : _params{}
        , _gForce{}
        , _previousVelocity{}
        , _pTrack{ nullptr }
        , _distance{ 0.0f }
        , _speed{ 0.0f }
        , _elapsedTime{ 0.0f }
        , _stepTimer{}
        , _lapCount{ 0 }
        , _bHasPreviousVelocity{ SW_FALSE }
    {
    }

    bool CoasterTrain::initialize( const CoasterTrack* pTrack, const CoasterPhysicsParams& params, float32 startDistance )
    {
        if ( CoasterTrainInternal::acceptParams( params ) == false )
            return false;
        _pTrack               = pTrack;
        _params               = params;
        _gForce               = CoasterGForce{};
        _previousVelocity     = float3{};
        _speed                = 0.0f;
        _elapsedTime          = 0.0f;
        _stepTimer            = FixedStepTimer( _params._fixedStep, 0.25f ); // 큰 프레임(디버거 정지)이 수천 번의 적분이 되지 않게 한다
        _lapCount             = 0;
        _bHasPreviousVelocity = SW_FALSE;
        setDistance( startDistance );
        return true;
    }

    void CoasterTrain::step( float32 deltaTime )
    {
        if ( _pTrack == nullptr || deltaTime <= 0.0f )
            return;
        const int32 stepCount = _stepTimer.consume( deltaTime );
        for ( int32 stepIndex = 0; stepIndex < stepCount; ++stepIndex )
        {
            integrate( _stepTimer.getStep() );
        }
    }

    void CoasterTrain::setDistance( float32 distance )
    {
        _distance = _pTrack != nullptr ? _pTrack->wrapDistance( distance ) : 0.0f;
        _lapCount = 0;
    }

    CoasterTrackFrame CoasterTrain::getFrame() const
    {
        return _pTrack != nullptr ? _pTrack->sample( _distance ) : CoasterTrackFrame{};
    }

    CoasterTrackFrame CoasterTrain::getCarFrame( uint32 carIndex, float32 carSpacing ) const
    {
        return _pTrack != nullptr ? _pTrack->sample( _distance - static_cast<float32>( carIndex ) * carSpacing ) : CoasterTrackFrame{};
    }

    void CoasterTrain::integrate( float32 deltaTime )
    {
        const CoasterTrackFrame frame   = _pTrack->sample( _distance );
        const float32           gravity = _params._gravity;

        // 경사 방향의 중력 성분과 저항을 구한다. 멈춰 있고 경사가 마찰을 못 이기면 그대로 선다(정지 마찰).
        const float32 slopeAcceleration = -gravity * frame._forward._y;
        const float32 friction          = _params._rollingResistance * gravity;
        float32       acceleration      = slopeAcceleration;
        if ( MathUtil::abs( _speed ) > 1.0e-3f )
        {
            acceleration -= ( _speed > 0.0f ? 1.0f : -1.0f ) * friction;
            acceleration -= _params._dragCoefficient * _speed * MathUtil::abs( _speed );
        }
        else if ( MathUtil::abs( slopeAcceleration ) <= friction )
        {
            acceleration = 0.0f;
        }
        float32 speed = _speed + acceleration * deltaTime;

        // 구간 장치: 체인은 아래로 못 가게 끌고, 스테이션은 정한 속도로 맞추고, 부스터는 올리고, 브레이크는 내린다.
        const uint8 flags = frame._flags;
        if ( ( flags & CoasterSegmentFlag::kLift ) != 0 && speed < _params._liftSpeed )
            speed = _params._liftSpeed;
        if ( ( flags & CoasterSegmentFlag::kStation ) != 0 )
        {
            if ( speed < _params._stationSpeed )
                speed = MathUtil::min( _params._stationSpeed, speed + _params._boosterAcceleration * deltaTime );
            else
                speed = MathUtil::max( _params._stationSpeed, speed - _params._brakeDeceleration * deltaTime );
        }
        if ( ( flags & CoasterSegmentFlag::kBooster ) != 0 && speed < _params._boosterSpeed )
            speed = MathUtil::min( _params._boosterSpeed, speed + _params._boosterAcceleration * deltaTime );
        if ( ( flags & CoasterSegmentFlag::kBrake ) != 0 && speed > _params._brakeSpeed )
            speed = MathUtil::max( _params._brakeSpeed, speed - _params._brakeDeceleration * deltaTime );

        // 거리: 닫힌 회로는 감고 바퀴를 세며, 열린 트랙은 끝에서 선다.
        const float32 length      = _pTrack->getLength();
        float32       newDistance = _distance + speed * deltaTime;
        if ( _pTrack->isClosed() && length > 0.0f )
        {
            while ( newDistance >= length )
            {
                newDistance -= length;
                ++_lapCount;
            }
            while ( newDistance < 0.0f )
            {
                newDistance += length;
                --_lapCount;
            }
        }
        else if ( newDistance <= 0.0f || newDistance >= length )
        {
            newDistance = MathUtil::clamp( newDistance, 0.0f, length );
            speed       = 0.0f;
        }
        _distance = newDistance;
        _speed    = speed;
        _elapsedTime += deltaTime;

        // G: 속도 벡터의 변화(가속도)에 중력을 되돌려 더한 것이 탑승자가 느끼는 힘이다(서 있으면 위로 1G).
        const CoasterTrackFrame newFrame = _pTrack->sample( _distance );
        const float3            velocity = newFrame._forward * _speed;
        if ( _bHasPreviousVelocity == SW_TRUE )
        {
            const float3 felt     = ( velocity - _previousVelocity ) * ( 1.0f / deltaTime ) + float3{ 0.0f, gravity, 0.0f };
            _gForce._vertical     = felt.dot( newFrame._up ) / gravity;
            _gForce._lateral      = felt.dot( newFrame._right ) / gravity;
            _gForce._longitudinal = felt.dot( newFrame._forward ) / gravity;
        }
        _previousVelocity     = velocity;
        _bHasPreviousVelocity = SW_TRUE;
    }

    CoasterRideStats CoasterRideAnalyzer::analyze( const CoasterTrack& track, const CoasterPhysicsParams& params, float32 maxTime )
    {
        CoasterRideStats stats;
        stats._length    = track.getLength();
        stats._maxHeight = track.getMaxHeight() - track.getMinHeight();
        if ( track.getPoints().size() < 2 )
            return stats;

        CoasterTrain train;
        if ( train.initialize( &track, params, 0.0f ) == false )
            return stats;                            // 매개변수가 거절되면 분석하지 않는다(`_bCompleted` 가 꺼진 채다 — 이유는 initialize 가 남겼다)
        const float32 deltaTime = params._fixedStep; // initialize 가 범위 안임을 봤다. 한 번에 한 번만 적분한다

        float32                           travelled = 0.0f;
        float32                           stillTime = 0.0f;
        CoasterTrainInternal::DropTracker drop;
        drop._peakHeight   = train.getFrame()._position._y;
        drop._valleyHeight = drop._peakHeight;
        bool    bInverted  = false;
        float32 time       = 0.0f;
        while ( time < maxTime )
        {
            train.step( deltaTime );
            time += deltaTime;
            const float32           speed  = train.getSpeed();
            const CoasterTrackFrame frame  = train.getFrame();
            const float32           height = frame._position._y;
            travelled += MathUtil::abs( speed ) * deltaTime;
            stats._maxSpeed = MathUtil::max( stats._maxSpeed, MathUtil::abs( speed ) );

            if ( time > params._gForceWarmupSeconds )
            {
                const CoasterGForce& gForce = train.getGForce();
                stats._maxVerticalG         = MathUtil::max( stats._maxVerticalG, gForce._vertical );
                stats._minVerticalG         = MathUtil::min( stats._minVerticalG, gForce._vertical );
                stats._maxLateralG          = MathUtil::max( stats._maxLateralG, MathUtil::abs( gForce._lateral ) );
                if ( gForce._vertical < 0.0f && MathUtil::abs( speed ) > 1.0f )
                    stats._airtime += deltaTime;
            }

            // 뒤집힘: 좌석 위가 땅을 향했다가(임계값 아래) 바로 서면(임계값 위) 하나로 센다.
            if ( bInverted == false && frame._up._y < CoasterTrainInternal::kInversionUpY )
            {
                bInverted = true;
                ++stats._inversionCount;
            }
            else if ( bInverted && frame._up._y > CoasterTrainInternal::kUprightUpY )
            {
                bInverted = false;
            }

            drop.observe( height, stats );

            const bool bOpenEnd = track.isClosed() == false && train.getDistance() >= track.getLength() - 0.01f;
            if ( train.getLapCount() >= 1 || bOpenEnd )
            {
                stats._bCompleted = SW_TRUE;
                break;
            }

            const bool bDriven = ( frame._flags & CoasterTrainInternal::kDrivenFlags ) != 0;
            stillTime          = ( MathUtil::abs( speed ) < params._stallSpeed && bDriven == false ) ? stillTime + deltaTime : 0.0f;
            if ( stillTime > params._stallSeconds )
            {
                stats._bStalled = SW_TRUE;
                break;
            }
        }
        drop.finish( stats );

        stats._lapTime      = time;
        stats._averageSpeed = time > 0.0f ? travelled / time : 0.0f;
        computeRatings( stats );
        return stats;
    }

    void CoasterRideAnalyzer::computeRatings( CoasterRideStats& inoutStats )
    {
        // 강도는 누르는 G, 띄우는 G, 옆 G, 속도로 정한다. 멀미는 옆 G, 뒤집힘, 강도로 정한다. 흥분은 속도, 에어타임, 뒤집힘, 낙하, 길이로 정하고
        // 너무 센 강도(8 넘음)는 깎는다(타이쿤처럼 "너무 무서우면 안 탄다").
        CoasterRideStats& stats      = inoutStats;
        const float32     positiveG  = MathUtil::max( 0.0f, stats._maxVerticalG - 1.0f );
        const float32     negativeG  = MathUtil::max( 0.0f, -stats._minVerticalG );
        const float32     inversions = static_cast<float32>( stats._inversionCount );
        const float32     drops      = static_cast<float32>( stats._dropCount );
        const float32     intensity  = positiveG * 0.9f + negativeG * 1.5f + stats._maxLateralG * 1.2f + stats._maxSpeed / 15.0f;
        stats._intensity             = MathUtil::clamp( intensity, 0.0f, 10.0f );
        const float32 nausea         = stats._maxLateralG * 1.5f + inversions * 1.0f + stats._airtime * 0.3f + stats._intensity * 0.25f;
        stats._nausea                = MathUtil::clamp( nausea, 0.0f, 10.0f );
        float32 excitement           = stats._maxSpeed / 10.0f + stats._airtime * 0.6f + inversions * 0.8f + drops * 0.3f + stats._maxDropHeight / 15.0f +
                             stats._length / 400.0f;
        if ( intensity > 8.0f )
            excitement -= intensity - 8.0f;
        if ( stats._bCompleted == SW_FALSE )
            excitement = 0.0f; // 한 바퀴를 못 도는 코스터는 운행할 수 없다
        stats._excitement = MathUtil::clamp( excitement, 0.0f, 10.0f );
    }
} // namespace sw
