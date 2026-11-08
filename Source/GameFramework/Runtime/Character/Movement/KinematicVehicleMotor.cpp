#include "pch.h"

#include "GameFramework/Runtime/Character/Movement/KinematicVehicleMotor.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Module/ModuleSharedAssetCache.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/ReflectedXMLFile.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Runtime/World/Spline/SplinePath.h"

namespace sw
{
    namespace
    {
        struct KinematicVehicleMotorInternal
        {
            static constexpr float32 kStopEpsilon = 0.05f; ///< 이보다 느리면 멈춘 것으로 본다(브레이크 → 후진 전환)

            static int32 signOf( float32 value ) { return value > 0.0f ? 1 : ( value < 0.0f ? -1 : 0 ); }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool KinematicVehicleSettings::loadFromAsset( const AssetID& assetID )
    {
        *this = KinematicVehicleSettings{};
        return ReflectedXMLFile::loadDesc( assetID, *this ) && validate( assetID.view() );
    }

    bool KinematicVehicleSettings::validate( string_view sourceName ) const
    {
        bool bOk = true;
        for ( int32 tierIndex = 1; tierIndex < kVehicleMiniTurboTierCount; ++tierIndex )
        {
            if ( _drift._arrMiniTurboTime[tierIndex] < _drift._arrMiniTurboTime[tierIndex - 1] )
            {
                SW_LOG_ERROR_ID( "KinematicVehicle.TierTimeOrder", "%#: mini-turbo tier times must not go down (tier %# < tier %#)", sourceName, tierIndex + 1, tierIndex );
                bOk = false;
            }
        }
        if ( _hover._bEnabled && _rail._bEnabled )
        {
            SW_LOG_ERROR_ID( "KinematicVehicle.HoverAndRail", "%#: the hover and rail modules cannot both be on", sourceName );
            bOk = false;
        }
        return bOk;
    }

    /** @brief `KinematicVehicleSettings` 의 데이터 파일 종류입니다(`AssetRef<KinematicVehicleSettings>` 가 접미사로 가른다). */
    SW_MODULE_SHARED_ASSET_CACHE( KinematicVehicleTuning, KinematicVehicleSettings, SW_GF_API, KinematicVehicleSettings::kAssetType.withLoader( AssetTypeLoader::of<KinematicVehicleSettings>( MemoryTag::Game ) ) );
} // namespace sw

namespace sw
{
    KinematicVehicleMotor::KinematicVehicleMotor()
        : _settings{}
        , _eventBuffer{}
        , _timer{ constant::time::kDefaultFixedStepSeconds, 0.25f }
        , _pGround{ nullptr }
        , _pRail{ nullptr }
        , _position{}
        , _velocity{}
        , _yaw{ 0.0f }
        , _driftCharge{ 0.0f }
        , _boost{}
        , _nitroGauge{ 0.0f }
        , _railDistance{ 0.0f }
        , _nitroCount{ 0 }
        , _driftDirection{ 0 }
        , _bAirborne{ SW_FALSE }
    {
    }

    void KinematicVehicleMotor::setRail( const SplinePath* pRail )
    {
        _pRail        = pRail;
        _railDistance = pRail != nullptr && pRail->isValid() ? pRail->findClosest( _position )._distance : 0.0f;
    }

    void KinematicVehicleMotor::reset( const float3& position, float32 yaw )
    {
        _position    = position;
        _velocity    = float3{};
        _yaw         = yaw;
        _driftCharge = 0.0f;
        _boost.clear();
        _nitroGauge     = 0.0f;
        _railDistance   = _pRail != nullptr && _pRail->isValid() ? _pRail->findClosest( position )._distance : 0.0f;
        _nitroCount     = 0;
        _driftDirection = 0;
        _bAirborne      = SW_FALSE;
        _timer.reset();
        _eventBuffer.clear();
    }

    void KinematicVehicleMotor::addImpulse( const float3& impulse )
    {
        _velocity._x += impulse._x;
        _velocity._y += impulse._y;
        _velocity._z += impulse._z;
        if ( isDrifting() )
            endDrift( false );
    }

    void KinematicVehicleMotor::startBoost( float32 duration, int32 source )
    {
        if ( duration <= 0.0f || _settings._boost._bEnabled == false )
            return;
        if ( _boost.isActive() == false )
        {
            const float3 forward = computeForward();
            _velocity._x += forward._x * _settings._boost._impulse;
            _velocity._z += forward._z * _settings._boost._impulse;
        }
        _boost.extendTo( duration );
        pushEvent( KinematicVehicleEvent::Kind::BoostStarted, source );
    }

    void KinematicVehicleMotor::update( const KinematicVehicleInput& input, float32 deltaTime )
    {
        if ( deltaTime <= 0.0f )
            return;
        KinematicVehicleInput clamped = input;
        clamped._throttle             = MathUtil::clamp( input._throttle, -1.0f, 1.0f );
        clamped._steer                = MathUtil::clamp( input._steer, -1.0f, 1.0f );
        if ( _settings._rail._bEnabled )
        {
            updateRail( clamped, deltaTime );
            return;
        }
        const float32 steer = clamped._steer;

        // 1) 지금 방향으로 속도를 앞 · 옆으로 나눈다.
        float32 forwardSpeed = getForwardSpeed();
        float32 lateralSpeed = getLateralSpeed();

        // 2) 니트로 — 눌렀고 하나라도 있으면 쓴다.
        if ( input._bBoostPressed != SW_FALSE && _nitroCount > 0 && _settings._boost._bEnabled )
        {
            --_nitroCount;
            startBoost( _settings._boost._nitroBoostTime, 0 );
            forwardSpeed = getForwardSpeed();
        }

        // 3) 앞 속도 — 바퀴가 땅에 있을 때만 페달이 듣는다(공중은 관성).
        const VehicleSurface surface = sampleSurface();
        if ( isAirborne() == false )
            updateForwardSpeed( clamped, deltaTime, forwardSpeed );

        // 4) 바깥 힘 — 경사 중력 · 구름 저항(땅에 닿을 때) · 공기 저항. 호버는 닿지 않으므로 구름 저항이 없고 경사는 덜 당긴다.
        const bool      bHover  = _settings._hover._bEnabled;
        InertiaSettings inertia = _settings._inertia;
        if ( bHover )
            inertia._slopeScale *= _settings._hover._slopeScale;
        const float32 slopeSin = isAirborne() ? 0.0f : computeSlopeSin();
        const float32 rolling  = isAirborne() || bHover ? 0.0f : surface._rollingResistance;
        forwardSpeed += inertia.computeAlongAcceleration( forwardSpeed, slopeSin, rolling, deltaTime ) * deltaTime;

        // 5) 드리프트 — 시작 · 충전 · 끝.
        if ( _settings._drift._bEnabled )
            updateDrift( clamped, forwardSpeed, deltaTime );

        // 6) 조향 — 드리프트 중이면 드리프트 방향으로 늘 돈다(조향은 조이기 · 풀기). 후진은 반대로 돈다.
        float32 steerAmount = steer;
        if ( isDrifting() )
        {
            const float32 direction = static_cast<float32>( _driftDirection );
            steerAmount             = direction * ( _settings._drift._baseSteer + _settings._drift._steerBonus * steer * direction );
        }
        float32 yawRate = computeSteerRate( MathUtil::abs( forwardSpeed ) ) * steerAmount;
        if ( forwardSpeed < 0.0f )
            yawRate = -yawRate;
        if ( isAirborne() )
            yawRate *= _settings._airControl._bEnabled ? _settings._airControl._airSteerScale : 0.0f;
        const float32 oldYaw = _yaw;
        _yaw += yawRate * deltaTime;
        if ( _yaw > MathUtil::kPi )
            _yaw -= 2.0f * MathUtil::kPi;
        else if ( _yaw < -MathUtil::kPi )
            _yaw += 2.0f * MathUtil::kPi;

        // 7) 옛 방향의 앞 · 옆 속도를 새 방향으로 다시 나눈다 — 돈 만큼 옆 성분이 생기고, 접지가 그것을 줄인다(미끄러짐).
        const float32 oldSin   = MathUtil::sin( oldYaw );
        const float32 oldCos   = MathUtil::cos( oldYaw );
        const float32 worldX   = oldSin * forwardSpeed + oldCos * lateralSpeed;
        const float32 worldZ   = oldCos * forwardSpeed - oldSin * lateralSpeed;
        const float32 newSin   = MathUtil::sin( _yaw );
        const float32 newCos   = MathUtil::cos( _yaw );
        float32       newAhead = worldX * newSin + worldZ * newCos;
        float32       newSide  = worldX * newCos - worldZ * newSin;
        if ( isAirborne() == false && _settings._grip._bEnabled )
        {
            // 접지는 옆 성분을 앞으로 돌린다(빠르기는 그대로 — 돌거나 미끄러져도 속도를 잃지 않는다). 표면 그립 배율이 빙판에서 줄인다.
            const float32 grip        = ( isDrifting() ? _settings._drift._driftGrip : _settings._grip._lateralGrip ) * MathUtil::max( 0.0f, surface._gripScale );
            const float32 speedSquare = newAhead * newAhead + newSide * newSide;
            newSide /= 1.0f + grip * deltaTime;
            const float32 aheadSquare = MathUtil::max( 0.0f, speedSquare - newSide * newSide );
            newAhead                  = newAhead < 0.0f ? -MathUtil::sqrt( aheadSquare ) : MathUtil::sqrt( aheadSquare );
        }
        _velocity._x = newSin * newAhead + newCos * newSide;
        _velocity._z = newCos * newAhead - newSin * newSide;

        // 8) 부스트 시간.
        if ( _boost.update( deltaTime ) )
            pushEvent( KinematicVehicleEvent::Kind::BoostEnded, 0 );

        // 9) 위치 · 위아래.
        _position._x += _velocity._x * deltaTime;
        _position._z += _velocity._z * deltaTime;
        if ( bHover )
            updateHover( deltaTime );
        else
            updateVertical( clamped, deltaTime );
    }

    int32 KinematicVehicleMotor::advance( const KinematicVehicleInput& input, float32 frameTime )
    {
        const int32           stepCount = _timer.consume( frameTime );
        KinematicVehicleInput stepInput = input;
        for ( int32 stepIndex = 0; stepIndex < stepCount; ++stepIndex )
        {
            update( stepInput, _timer.getStep() );
            stepInput._bBoostPressed = SW_FALSE;
            stepInput._bJumpPressed  = SW_FALSE;
        }
        return stepCount;
    }

    void KinematicVehicleMotor::pollEvents( vector<KinematicVehicleEvent>& outListEvent )
    {
        _eventBuffer.moveAllTo( outListEvent );
    }

    float32 KinematicVehicleMotor::computeSteerRate( float32 speed ) const
    {
        const float32 absSpeed   = MathUtil::abs( speed );
        const float32 speedRatio = _settings._maxSpeed > 0.0f ? MathUtil::saturate( absSpeed / _settings._maxSpeed ) : 0.0f;
        float32       rate       = MathUtil::lerp( _settings._steerRate, _settings._steerRateAtMaxSpeed, speedRatio );
        if ( _settings._steerMinSpeed > 0.0f && absSpeed < _settings._steerMinSpeed )
            rate *= absSpeed / _settings._steerMinSpeed;
        return rate;
    }

    float32 KinematicVehicleMotor::computeSpeedCap() const
    {
        const bool bBoosting = isBoosting();
        float32    cap       = _settings._maxSpeed;
        if ( bBoosting == false || _settings._boost._bIgnoresOffroad == false )
        {
            const float32 surfaceScale = MathUtil::saturate( sampleSurface()._speedScale );
            cap *= 1.0f - ( 1.0f - surfaceScale ) * MathUtil::saturate( _settings._offroadSensitivity );
        }
        if ( bBoosting )
            cap += _settings._boost._speedBonus;
        return cap;
    }

    float3 KinematicVehicleMotor::computeForward() const
    {
        return float3{ MathUtil::sin( _yaw ), 0.0f, MathUtil::cos( _yaw ) };
    }

    float32 KinematicVehicleMotor::computeSlopeSin() const
    {
        const float3  forward = computeForward();
        const float32 front   = sampleHeight( _position._x + forward._x * kSlopeProbeDistance, _position._z + forward._z * kSlopeProbeDistance );
        const float32 back    = sampleHeight( _position._x - forward._x * kSlopeProbeDistance, _position._z - forward._z * kSlopeProbeDistance );
        const float32 rise    = front - back;
        const float32 run     = 2.0f * kSlopeProbeDistance;
        return rise / MathUtil::sqrt( run * run + rise * rise );
    }

    float32 KinematicVehicleMotor::getForwardSpeed() const
    {
        return _velocity._x * MathUtil::sin( _yaw ) + _velocity._z * MathUtil::cos( _yaw );
    }

    float32 KinematicVehicleMotor::getLateralSpeed() const
    {
        return _velocity._x * MathUtil::cos( _yaw ) - _velocity._z * MathUtil::sin( _yaw );
    }

    int32 KinematicVehicleMotor::getDriftTier() const
    {
        int32 tier = 0;
        for ( int32 tierIndex = 0; tierIndex < kVehicleMiniTurboTierCount; ++tierIndex )
        {
            if ( _driftCharge >= _settings._drift._arrMiniTurboTime[tierIndex] )
                tier = tierIndex + 1;
        }
        return tier;
    }

    float32 KinematicVehicleMotor::sampleHeight( float32 x, float32 z ) const
    {
        return _pGround != nullptr ? _pGround->sampleHeight( x, z ) : 0.0f;
    }

    VehicleSurface KinematicVehicleMotor::sampleSurface() const
    {
        return _pGround != nullptr ? _pGround->sampleSurface( _position._x, _position._z ) : VehicleSurface{};
    }

    void KinematicVehicleMotor::updateForwardSpeed( const KinematicVehicleInput& input, float32 deltaTime, float32& inoutForwardSpeed ) const
    {
        const float32 cap          = computeSpeedCap();
        const float32 throttle     = input._throttle;
        const float32 acceleration = _settings._inertia.limitDriveAcceleration( _settings.computeDriveAcceleration(), inoutForwardSpeed ); // 출력 한계 — 빠를수록 · 오르막에서 처진다
        const float32 braking      = _settings.computeBrakeDeceleration();
        float32       speed        = inoutForwardSpeed;
        if ( isBoosting() && throttle >= 0.0f )
        {
            // 부스트는 페달과 상관없이 늘어난 상한까지 민다.
            if ( speed < cap )
                speed = MathUtil::min( cap, speed + _settings._boost._acceleration * deltaTime );
        }
        else if ( throttle > 0.0f )
        {
            if ( speed < -KinematicVehicleMotorInternal::kStopEpsilon )
                speed = MathUtil::min( 0.0f, speed + braking * throttle * deltaTime );
            else if ( speed < cap * throttle )
                speed = MathUtil::min( cap * throttle, speed + acceleration * throttle * deltaTime );
            else if ( speed <= cap )
                speed = MathUtil::moveToward( speed, cap * throttle, _settings._coastDrag * deltaTime );
        }
        else if ( throttle < 0.0f )
        {
            if ( speed > KinematicVehicleMotorInternal::kStopEpsilon )
                speed = MathUtil::max( 0.0f, speed + braking * throttle * deltaTime );
            else
            {
                const float32 reverseCap = -_settings._reverseMaxSpeed * MathUtil::min( 1.0f, cap / MathUtil::max( _settings._maxSpeed, 1.0e-3f ) );
                speed                    = MathUtil::moveToward( speed, reverseCap * -throttle, acceleration * -throttle * deltaTime );
            }
        }
        else
        {
            speed = MathUtil::moveToward( speed, 0.0f, _settings._coastDrag * deltaTime );
        }
        // 상한을 넘었으면(부스트 끝 · 오프로드에 들어섬) 천천히 줄인다.
        if ( speed > cap )
            speed = MathUtil::max( cap, speed - _settings._overSpeedDeceleration * deltaTime );
        inoutForwardSpeed = speed;
    }

    void KinematicVehicleMotor::updateDrift( const KinematicVehicleInput& input, float32 forwardSpeed, float32 deltaTime )
    {
        const VehicleDriftModule& drift = _settings._drift;
        if ( isDrifting() == false )
        {
            const int32 steerSign   = KinematicVehicleMotorInternal::signOf( input._steer );
            const bool  bStartDrift = input._bDriftHeld != SW_FALSE && isAirborne() == false && steerSign != 0 && forwardSpeed >= drift._minSpeed;
            if ( bStartDrift )
            {
                _driftDirection = steerSign;
                _driftCharge    = 0.0f;
                pushEvent( KinematicVehicleEvent::Kind::DriftStarted, steerSign );
            }
            return;
        }
        if ( input._bDriftHeld == SW_FALSE )
        {
            endDrift( true );
            return;
        }
        if ( forwardSpeed < drift._minSpeed * 0.5f )
        {
            endDrift( false ); // 너무 느려졌다(벽에 박음) — 보상 없음
            return;
        }
        const int32 previousTier = getDriftTier();
        _driftCharge += deltaTime;
        const int32 tier = getDriftTier();
        if ( tier > previousTier )
            pushEvent( KinematicVehicleEvent::Kind::DriftTierReached, tier );

        _nitroGauge += drift._nitroFillPerSecond * deltaTime;
        while ( _nitroGauge >= 1.0f )
        {
            if ( _nitroCount >= drift._maxNitroCount )
            {
                _nitroGauge = 1.0f; // 가득 — 쓸 때까지 더 차지 않는다
                break;
            }
            _nitroGauge -= 1.0f;
            ++_nitroCount;
            pushEvent( KinematicVehicleEvent::Kind::NitroCharged, _nitroCount );
        }
    }

    void KinematicVehicleMotor::endDrift( bool bReward )
    {
        const int32 tier = bReward && _settings._boost._bEnabled ? getDriftTier() : 0;
        _driftDirection  = 0;
        _driftCharge     = 0.0f;
        pushEvent( KinematicVehicleEvent::Kind::DriftEnded, tier );
        if ( tier > 0 )
        {
            pushEvent( KinematicVehicleEvent::Kind::MiniTurbo, tier );
            startBoost( _settings._drift._arrMiniTurboBoost[tier - 1], tier );
        }
    }

    void KinematicVehicleMotor::updateVertical( const KinematicVehicleInput& input, float32 deltaTime )
    {
        const VehicleAirControlModule& air          = _settings._airControl;
        const float32                  groundHeight = sampleHeight( _position._x, _position._z );
        if ( isAirborne() == false )
        {
            if ( input._bJumpPressed != SW_FALSE && air._bEnabled )
            {
                _velocity._y = air._jumpSpeed;
                _bAirborne   = SW_TRUE;
                pushEvent( KinematicVehicleEvent::Kind::Jumped, 0 );
            }
            else if ( _position._y - groundHeight > _settings._groundSnapDistance )
            {
                // 턱 너머로 땅이 꺼졌다 — 앞으로 날아간다.
                _velocity._y = 0.0f;
                _bAirborne   = SW_TRUE;
                pushEvent( KinematicVehicleEvent::Kind::LeftGround, 0 );
            }
            else
            {
                _position._y = groundHeight;
                _velocity._y = 0.0f;
                return;
            }
        }
        _velocity._y -= ( air._bEnabled ? air._fallGravity : _settings._inertia._gravity ) * deltaTime;
        _position._y += _velocity._y * deltaTime;
        if ( _position._y <= groundHeight && _velocity._y <= 0.0f )
        {
            _position._y = groundHeight;
            _velocity._y = 0.0f;
            _bAirborne   = SW_FALSE;
            pushEvent( KinematicVehicleEvent::Kind::Landed, 0 );
        }
    }

    void KinematicVehicleMotor::updateHover( float32 deltaTime )
    {
        // 용수철 · 감쇠로 땅 위 높이를 따른다(반 암시적 오일러 — 속도를 먼저 고친다). 땅에 닿지 않으니 공중 상태는 두지 않는다.
        const VehicleHoverModule& hover  = _settings._hover;
        const float32             target = sampleHeight( _position._x, _position._z ) + hover._height;
        const float32             accel  = hover._stiffness * ( target - _position._y ) - hover._damping * _velocity._y;
        _velocity._y += accel * deltaTime;
        _position._y += _velocity._y * deltaTime;
        _bAirborne = SW_FALSE;
    }

    void KinematicVehicleMotor::updateRail( const KinematicVehicleInput& input, float32 deltaTime )
    {
        // 레일이 없으면 움직이지 않는다 — 설정 오류는 레일을 거는 쪽(컴포넌트의 시작)이 알린다.
        if ( _pRail == nullptr || _pRail->isValid() == false )
            return;
        const SplineSample here  = _pRail->sampleAtDistance( _railDistance );
        float32            speed = _velocity._x * here._tangent._x + _velocity._y * here._tangent._y + _velocity._z * here._tangent._z;
        updateForwardSpeed( input, deltaTime, speed );
        speed += _settings._inertia.computeAlongAcceleration( speed, here._tangent._y, _settings._rail._rollingResistance, deltaTime ) * deltaTime;
        if ( _boost.update( deltaTime ) )
            pushEvent( KinematicVehicleEvent::Kind::BoostEnded, 0 );

        float32 distance = _railDistance + speed * deltaTime;
        if ( _pRail->isClosed() )
            distance = _pRail->wrapDistance( distance );
        else if ( distance <= 0.0f || distance >= _pRail->getLength() )
        {
            distance = MathUtil::clamp( distance, 0.0f, _pRail->getLength() );
            if ( distance != _railDistance ) // 끝에 닿은 걸음에만 — 끝에 서 있는 동안은 다시 알리지 않는다
                pushEvent( KinematicVehicleEvent::Kind::RailEnded, distance > 0.0f ? 1 : -1 );
            speed = 0.0f;
        }
        _railDistance              = distance;
        const SplineSample next    = _pRail->sampleAtDistance( distance );
        _position                  = next._position;
        _velocity                  = next._tangent * speed;
        const float32 horizontalSq = next._tangent._x * next._tangent._x + next._tangent._z * next._tangent._z;
        if ( horizontalSq > 1.0e-6f )
            _yaw = MathUtil::atan2( next._tangent._x, next._tangent._z );
        _bAirborne = SW_FALSE;
    }

    void KinematicVehicleMotor::pushEvent( KinematicVehicleEvent::Kind kind, int32 value )
    {
        KinematicVehicleEvent event;
        event._kind  = kind;
        event._value = value;
        _eventBuffer.push( event );
    }

    void KinematicVehicleMotor::serializeState( StateIO& io )
    {
        io.value( _timer );
        io.value( _position );
        io.value( _velocity );
        io.value( _yaw );
        io.value( _driftCharge );
        io.value( _boost );
        io.value( _nitroGauge );
        io.value( _railDistance );
        io.value( _nitroCount );
        io.value( _driftDirection );
        io.flag( _bAirborne );
        io.check( 0 <= _nitroCount && -1 <= _driftDirection && _driftDirection <= 1 && 0.0f <= _railDistance );
        if ( io.isReading() )
        {
            _eventBuffer.clear();
        }
    }

} // namespace sw
