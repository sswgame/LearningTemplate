#include "pch.h"

#include "GameFramework/Base/Movement/ArcadeVehicleMotor.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Utility/StateArchiveUtil.h"

namespace sw
{
    namespace
    {
        struct ArcadeVehicleMotorInternal
        {
            static constexpr float32 kStopEpsilon = 0.05f; ///< 이보다 느리면 멈춘 것으로 본다(브레이크 → 후진 전환)

            static float32 approach( float32 value, float32 target, float32 step )
            {
                return value < target ? MathUtil::min( target, value + step ) : MathUtil::max( target, value - step );
            }

            static int32 signOf( float32 value ) { return value > 0.0f ? 1 : ( value < 0.0f ? -1 : 0 ); }
        };
    } // namespace
} // namespace sw

namespace sw
{
    ArcadeVehicleMotor::ArcadeVehicleMotor()
        : _settings{}
        , _eventBuffer{}
        , _timer{ 1.0f / 60.0f, 0.25f }
        , _pGround{ nullptr }
        , _position{}
        , _velocity{}
        , _yaw{ 0.0f }
        , _driftCharge{ 0.0f }
        , _boost{}
        , _nitroGauge{ 0.0f }
        , _nitroCount{ 0 }
        , _driftDirection{ 0 }
        , _bAirborne{ SW_FALSE }
    {
    }

    void ArcadeVehicleMotor::reset( const float3& position, float32 yaw )
    {
        _position    = position;
        _velocity    = float3{};
        _yaw         = yaw;
        _driftCharge = 0.0f;
        _boost.clear();
        _nitroGauge     = 0.0f;
        _nitroCount     = 0;
        _driftDirection = 0;
        _bAirborne      = SW_FALSE;
        _timer.reset();
        _eventBuffer.clear();
    }

    void ArcadeVehicleMotor::addImpulse( const float3& impulse )
    {
        _velocity._x += impulse._x;
        _velocity._y += impulse._y;
        _velocity._z += impulse._z;
        if ( isDrifting() )
            endDrift( false );
    }

    void ArcadeVehicleMotor::startBoost( float32 duration, int32 source )
    {
        if ( duration <= 0.0f )
            return;
        if ( _boost.isActive() == false )
        {
            const float3 forward = computeForward();
            _velocity._x += forward._x * _settings._boostImpulse;
            _velocity._z += forward._z * _settings._boostImpulse;
        }
        _boost.extendTo( duration );
        pushEvent( ArcadeVehicleEvent::Kind::BoostStarted, source );
    }

    void ArcadeVehicleMotor::update( const ArcadeVehicleInput& input, float32 deltaTime )
    {
        if ( deltaTime <= 0.0f )
            return;
        const float32      throttle = MathUtil::clamp( input._throttle, -1.0f, 1.0f );
        const float32      steer    = MathUtil::clamp( input._steer, -1.0f, 1.0f );
        ArcadeVehicleInput clamped  = input;
        clamped._throttle           = throttle;
        clamped._steer              = steer;

        // 1) 지금 방향으로 속도를 앞 · 옆으로 나눈다.
        float32 forwardSpeed = getForwardSpeed();
        float32 lateralSpeed = getLateralSpeed();

        // 2) 니트로 — 눌렀고 하나라도 있으면 쓴다.
        if ( input._bBoostPressed != SW_FALSE && _nitroCount > 0 )
        {
            --_nitroCount;
            startBoost( _settings._nitroBoostTime, 0 );
            forwardSpeed = getForwardSpeed();
        }

        // 3) 앞 속도 — 바퀴가 땅에 있을 때만 페달이 듣는다(공중은 관성).
        if ( isAirborne() == false )
            updateForwardSpeed( clamped, deltaTime, forwardSpeed );

        // 4) 드리프트 — 시작 · 충전 · 끝.
        updateDrift( clamped, forwardSpeed, deltaTime );

        // 5) 조향 — 드리프트 중이면 드리프트 방향으로 늘 돈다(조향은 조이기 · 풀기). 후진은 반대로 돈다.
        float32 steerAmount = steer;
        if ( isDrifting() )
        {
            const float32 direction = static_cast<float32>( _driftDirection );
            steerAmount             = direction * ( _settings._driftBaseSteer + _settings._driftSteerBonus * steer * direction );
        }
        float32 yawRate = computeSteerRate( MathUtil::abs( forwardSpeed ) ) * steerAmount;
        if ( forwardSpeed < 0.0f )
            yawRate = -yawRate;
        if ( isAirborne() )
            yawRate *= _settings._airSteerScale;
        const float32 oldYaw = _yaw;
        _yaw += yawRate * deltaTime;
        if ( _yaw > MathUtil::Pi )
            _yaw -= 2.0f * MathUtil::Pi;
        else if ( _yaw < -MathUtil::Pi )
            _yaw += 2.0f * MathUtil::Pi;

        // 6) 옛 방향의 앞 · 옆 속도를 새 방향으로 다시 나눈다 — 돈 만큼 옆 성분이 생기고, 접지가 그것을 줄인다(미끄러짐).
        const float32 oldSin   = MathUtil::sin( oldYaw );
        const float32 oldCos   = MathUtil::cos( oldYaw );
        const float32 worldX   = oldSin * forwardSpeed + oldCos * lateralSpeed;
        const float32 worldZ   = oldCos * forwardSpeed - oldSin * lateralSpeed;
        const float32 newSin   = MathUtil::sin( _yaw );
        const float32 newCos   = MathUtil::cos( _yaw );
        float32       newAhead = worldX * newSin + worldZ * newCos;
        float32       newSide  = worldX * newCos - worldZ * newSin;
        if ( isAirborne() == false )
        {
            // 접지는 옆 성분을 앞으로 돌린다(빠르기는 그대로 — 아케이드 손맛: 돌거나 미끄러져도 속도를 잃지 않는다).
            const float32 grip        = isDrifting() ? _settings._driftGrip : _settings._grip;
            const float32 speedSquare = newAhead * newAhead + newSide * newSide;
            newSide /= 1.0f + grip * deltaTime;
            const float32 aheadSquare = MathUtil::max( 0.0f, speedSquare - newSide * newSide );
            newAhead                  = newAhead < 0.0f ? -MathUtil::sqrt( aheadSquare ) : MathUtil::sqrt( aheadSquare );
        }
        _velocity._x = newSin * newAhead + newCos * newSide;
        _velocity._z = newCos * newAhead - newSin * newSide;

        // 7) 부스트 시간.
        if ( _boost.tick( deltaTime ) )
            pushEvent( ArcadeVehicleEvent::Kind::BoostEnded, 0 );

        // 8) 위치 · 위아래.
        _position._x += _velocity._x * deltaTime;
        _position._z += _velocity._z * deltaTime;
        updateVertical( clamped, deltaTime );
    }

    int32 ArcadeVehicleMotor::advance( const ArcadeVehicleInput& input, float32 frameTime )
    {
        const int32        stepCount = _timer.consume( frameTime );
        ArcadeVehicleInput stepInput = input;
        for ( int32 stepIndex = 0; stepIndex < stepCount; ++stepIndex )
        {
            update( stepInput, _timer.getStep() );
            stepInput._bBoostPressed = SW_FALSE;
            stepInput._bJumpPressed  = SW_FALSE;
        }
        return stepCount;
    }

    void ArcadeVehicleMotor::drainEvents( vector<ArcadeVehicleEvent>& outListEvent )
    {
        _eventBuffer.drainTo( outListEvent );
    }

    float32 ArcadeVehicleMotor::computeSteerRate( float32 speed ) const
    {
        const float32 absSpeed   = MathUtil::abs( speed );
        const float32 speedRatio = _settings._maxSpeed > 0.0f ? MathUtil::saturate( absSpeed / _settings._maxSpeed ) : 0.0f;
        float32       rate       = MathUtil::lerp( _settings._steerRate, _settings._steerRateAtMaxSpeed, speedRatio );
        if ( _settings._steerMinSpeed > 0.0f && absSpeed < _settings._steerMinSpeed )
            rate *= absSpeed / _settings._steerMinSpeed;
        return rate;
    }

    float32 ArcadeVehicleMotor::computeSpeedCap() const
    {
        const bool bBoosting = isBoosting();
        float32    cap       = _settings._maxSpeed;
        if ( bBoosting == false || _settings._bBoostIgnoresOffroad == SW_FALSE )
        {
            const float32 surface = sampleSurfaceScale();
            cap *= 1.0f - ( 1.0f - surface ) * MathUtil::saturate( _settings._offroadSensitivity );
        }
        if ( bBoosting )
            cap += _settings._boostSpeedBonus;
        return cap;
    }

    float3 ArcadeVehicleMotor::computeForward() const
    {
        return float3{ MathUtil::sin( _yaw ), 0.0f, MathUtil::cos( _yaw ) };
    }

    float32 ArcadeVehicleMotor::getForwardSpeed() const
    {
        return _velocity._x * MathUtil::sin( _yaw ) + _velocity._z * MathUtil::cos( _yaw );
    }

    float32 ArcadeVehicleMotor::getLateralSpeed() const
    {
        return _velocity._x * MathUtil::cos( _yaw ) - _velocity._z * MathUtil::sin( _yaw );
    }

    int32 ArcadeVehicleMotor::getDriftTier() const
    {
        int32 tier = 0;
        for ( int32 tierIndex = 0; tierIndex < kVehicleMiniTurboTierCount; ++tierIndex )
        {
            if ( _driftCharge >= _settings._arrMiniTurboTime[tierIndex] )
                tier = tierIndex + 1;
        }
        return tier;
    }

    float32 ArcadeVehicleMotor::sampleHeight( float32 x, float32 z ) const
    {
        return _pGround != nullptr ? _pGround->sampleHeight( x, z ) : 0.0f;
    }

    float32 ArcadeVehicleMotor::sampleSurfaceScale() const
    {
        if ( _pGround == nullptr )
            return 1.0f;
        return MathUtil::saturate( _pGround->sampleSpeedScale( _position._x, _position._z ) );
    }

    void ArcadeVehicleMotor::updateForwardSpeed( const ArcadeVehicleInput& input, float32 deltaTime, float32& inoutForwardSpeed ) const
    {
        const float32 cap      = computeSpeedCap();
        const float32 throttle = input._throttle;
        float32       speed    = inoutForwardSpeed;
        if ( isBoosting() && throttle >= 0.0f )
        {
            // 부스트는 페달과 상관없이 늘어난 상한까지 민다.
            if ( speed < cap )
                speed = MathUtil::min( cap, speed + _settings._boostAcceleration * deltaTime );
        }
        else if ( throttle > 0.0f )
        {
            if ( speed < -ArcadeVehicleMotorInternal::kStopEpsilon )
                speed = MathUtil::min( 0.0f, speed + _settings._brakeDeceleration * throttle * deltaTime );
            else if ( speed < cap * throttle )
                speed = MathUtil::min( cap * throttle, speed + _settings._acceleration * throttle * deltaTime );
            else if ( speed <= cap )
                speed = ArcadeVehicleMotorInternal::approach( speed, cap * throttle, _settings._coastDrag * deltaTime );
        }
        else if ( throttle < 0.0f )
        {
            if ( speed > ArcadeVehicleMotorInternal::kStopEpsilon )
                speed = MathUtil::max( 0.0f, speed + _settings._brakeDeceleration * throttle * deltaTime );
            else
            {
                const float32 reverseCap = -_settings._reverseMaxSpeed * MathUtil::min( 1.0f, cap / MathUtil::max( _settings._maxSpeed, 1.0e-3f ) );
                speed                    = ArcadeVehicleMotorInternal::approach( speed, reverseCap * -throttle, _settings._acceleration * -throttle * deltaTime );
            }
        }
        else
        {
            speed = ArcadeVehicleMotorInternal::approach( speed, 0.0f, _settings._coastDrag * deltaTime );
        }
        // 상한을 넘었으면(부스트 끝 · 오프로드에 들어섬) 천천히 줄인다.
        if ( speed > cap )
            speed = MathUtil::max( cap, speed - _settings._overSpeedDeceleration * deltaTime );
        inoutForwardSpeed = speed;
    }

    void ArcadeVehicleMotor::updateDrift( const ArcadeVehicleInput& input, float32 forwardSpeed, float32 deltaTime )
    {
        if ( isDrifting() == false )
        {
            const int32 steerSign = ArcadeVehicleMotorInternal::signOf( input._steer );
            if ( input._bDriftHeld != SW_FALSE && isAirborne() == false && steerSign != 0 && forwardSpeed >= _settings._driftMinSpeed )
            {
                _driftDirection = steerSign;
                _driftCharge    = 0.0f;
                pushEvent( ArcadeVehicleEvent::Kind::DriftStarted, steerSign );
            }
            return;
        }
        if ( input._bDriftHeld == SW_FALSE )
        {
            endDrift( true );
            return;
        }
        if ( forwardSpeed < _settings._driftMinSpeed * 0.5f )
        {
            endDrift( false ); // 너무 느려졌다(벽에 박음) — 보상 없음
            return;
        }
        const int32 previousTier = getDriftTier();
        _driftCharge += deltaTime;
        const int32 tier = getDriftTier();
        if ( tier > previousTier )
            pushEvent( ArcadeVehicleEvent::Kind::DriftTierReached, tier );

        _nitroGauge += _settings._nitroFillPerSecond * deltaTime;
        while ( _nitroGauge >= 1.0f )
        {
            if ( _nitroCount >= _settings._maxNitroCount )
            {
                _nitroGauge = 1.0f; // 가득 — 쓸 때까지 더 차지 않는다
                break;
            }
            _nitroGauge -= 1.0f;
            ++_nitroCount;
            pushEvent( ArcadeVehicleEvent::Kind::NitroCharged, _nitroCount );
        }
    }

    void ArcadeVehicleMotor::endDrift( bool bReward )
    {
        const int32 tier = bReward ? getDriftTier() : 0;
        _driftDirection  = 0;
        _driftCharge     = 0.0f;
        pushEvent( ArcadeVehicleEvent::Kind::DriftEnded, tier );
        if ( tier > 0 )
        {
            pushEvent( ArcadeVehicleEvent::Kind::MiniTurbo, tier );
            startBoost( _settings._arrMiniTurboBoost[tier - 1], tier );
        }
    }

    void ArcadeVehicleMotor::updateVertical( const ArcadeVehicleInput& input, float32 deltaTime )
    {
        const float32 groundHeight = sampleHeight( _position._x, _position._z );
        if ( isAirborne() == false )
        {
            if ( input._bJumpPressed != SW_FALSE )
            {
                _velocity._y = _settings._jumpSpeed;
                _bAirborne   = SW_TRUE;
                pushEvent( ArcadeVehicleEvent::Kind::Jumped, 0 );
            }
            else if ( _position._y - groundHeight > _settings._groundSnapDistance )
            {
                // 턱 너머로 땅이 꺼졌다 — 앞으로 날아간다.
                _velocity._y = 0.0f;
                _bAirborne   = SW_TRUE;
                pushEvent( ArcadeVehicleEvent::Kind::LeftGround, 0 );
            }
            else
            {
                _position._y = groundHeight;
                _velocity._y = 0.0f;
                return;
            }
        }
        _velocity._y -= _settings._gravity * deltaTime;
        _position._y += _velocity._y * deltaTime;
        if ( _position._y <= groundHeight && _velocity._y <= 0.0f )
        {
            _position._y = groundHeight;
            _velocity._y = 0.0f;
            _bAirborne   = SW_FALSE;
            pushEvent( ArcadeVehicleEvent::Kind::Landed, 0 );
        }
    }

    void ArcadeVehicleMotor::pushEvent( ArcadeVehicleEvent::Kind kind, int32 value )
    {
        ArcadeVehicleEvent event;
        event._kind  = kind;
        event._value = value;
        _eventBuffer.push( event );
    }

    void ArcadeVehicleMotor::writeState( Archive& outArchive ) const
    {
        StateArchiveUtil::writeStepTimer( outArchive, _timer );
        outArchive << _position;
        outArchive << _velocity;
        outArchive << _yaw;
        outArchive << _driftCharge;
        StateArchiveUtil::writeCountdown( outArchive, _boost );
        outArchive << _nitroGauge;
        outArchive << _nitroCount;
        outArchive << _driftDirection;
        outArchive << _bAirborne;
    }

    bool ArcadeVehicleMotor::readState( Archive& archive )
    {
        FixedStepTimer timer          = _timer;
        float3         position       = {};
        float3         velocity       = {};
        float32        yaw            = 0.0f;
        float32        driftCharge    = 0.0f;
        Countdown      boost          = {};
        float32        nitroGauge     = 0.0f;
        int32          nitroCount     = 0;
        int32          driftDirection = 0;
        uint8          bAirborne      = SW_FALSE;
        const bool     bTimerRead     = StateArchiveUtil::readStepTimer( archive, timer );
        archive >> position;
        archive >> velocity;
        archive >> yaw;
        archive >> driftCharge;
        const bool bBoostRead = StateArchiveUtil::readCountdown( archive, boost );
        archive >> nitroGauge;
        archive >> nitroCount;
        archive >> driftDirection;
        archive >> bAirborne;
        const bool bValid = bTimerRead && bBoostRead && archive.isOk() && 0 <= nitroCount && -1 <= driftDirection && driftDirection <= 1 && bAirborne <= SW_TRUE;
        if ( bValid == false )
            return false;
        _timer          = timer;
        _position       = position;
        _velocity       = velocity;
        _yaw            = yaw;
        _driftCharge    = driftCharge;
        _boost          = boost;
        _nitroGauge     = nitroGauge;
        _nitroCount     = nitroCount;
        _driftDirection = driftDirection;
        _bAirborne      = bAirborne;
        _eventBuffer.clear();
        return true;
    }
} // namespace sw
