#include "pch.h"

#include "GameFramework/Kits/Casual/KartRacing/KartAi.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/Kits/Casual/KartRacing/KartTrack.h"

namespace sw
{
    namespace
    {
        struct KartAiInternal
        {
            static float32 wrapAngle( float32 angle )
            {
                while ( angle > MathUtil::kPi )
                {
                    angle -= 2.0f * MathUtil::kPi;
                }
                while ( angle < -MathUtil::kPi )
                {
                    angle += 2.0f * MathUtil::kPi;
                }
                return angle;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    KartAiDriver::KartAiDriver()
        : _settings{}
        , _driftSide{ 0 }
    {
    }

    ArcadeVehicleInput KartAiDriver::computeInput( const KartTrack& track, const ArcadeVehicleMotor& motor, float32 trackDistance )
    {
        ArcadeVehicleInput input;
        if ( track.isValid() == false )
            return input;
        const float32 speed     = MathUtil::max( 0.0f, motor.getForwardSpeed() );
        const float32 lookAhead = _settings._lookAheadBase + _settings._lookAheadPerSpeed * speed;

        // 1) 앞 곡선 — 도는 쪽 안으로 레이싱 라인을 붙인다(오른쪽으로 돌면 오른쪽 +).
        const float32 corner      = track.computeHeadingChange( trackDistance, _settings._cornerLookAhead );
        const float32 cornerRatio = _settings._cornerAngle > 0.0f ? MathUtil::clamp( corner / _settings._cornerAngle, -1.0f, 1.0f ) : 0.0f;
        const float32 lineOffset  = cornerRatio * _settings._racingLine * track.getWidth() * 0.5f;

        const KartTrackFrame target = track.sample( trackDistance + lookAhead );
        const float32        aimX   = target._position._x + target._right._x * lineOffset;
        const float32        aimZ   = target._position._z + target._right._z * lineOffset;
        const float3&        here   = motor.getPosition();
        const float32        aimYaw = MathUtil::atan2( aimX - here._x, aimZ - here._z );
        const float32        error  = KartAiInternal::wrapAngle( aimYaw - motor.getYaw() );
        input._steer                = MathUtil::clamp( error * _settings._steerGain, -1.0f, 1.0f );

        // 2) 페달 — 아주 급한 곡선만 늦춘다.
        const float32 absCorner = MathUtil::abs( corner );
        input._throttle         = absCorner > _settings._brakeAngle ? 0.4f : 1.0f;

        // 3) 드리프트 — 큰 곡선 앞에서 그 쪽으로 걸고, 곡선이 펴지면 놓는다(미니터보를 받는다).
        const float32 driftMinSpeed = motor.getSettings()._driftMinSpeed;
        const int32   cornerSide    = corner > 0.0f ? 1 : -1;
        if ( _driftSide == 0 )
        {
            const bool bSharp = absCorner >= _settings._driftStartAngle && speed >= driftMinSpeed;
            if ( bSharp )
                _driftSide = cornerSide;
        }
        else
        {
            const bool bStraightened = absCorner <= _settings._driftEndAngle || cornerSide != _driftSide;
            const bool bTooSlow      = motor.isDrifting() == false && speed < driftMinSpeed;
            if ( bStraightened || bTooSlow )
                _driftSide = 0;
        }
        if ( _driftSide != 0 )
        {
            input._bDriftHeld = SW_TRUE;
            // 걸 때는 그 쪽으로 꺾어야 걸린다. 걸린 뒤의 조향은 조이기 · 풀기라 오차를 그대로 쓴다.
            if ( motor.isDrifting() == false )
                input._steer = static_cast<float32>( _driftSide );
        }
        return input;
    }

    bool KartAiDriver::shouldUseItem( const KartAiContext& context ) const
    {
        if ( context._bHasItem == SW_FALSE || context._itemHeldTime < _settings._itemUseDelay )
            return false;
        if ( context._itemHeldTime >= _settings._itemMaxHold )
            return true;
        switch ( context._itemKind )
        {
            case KartItemKind::Banana:
                return 0.0f <= context._gapBehind && context._gapBehind <= _settings._bananaRange;
            case KartItemKind::GreenShell:
            case KartItemKind::RedShell:
                return 0.0f <= context._gapAhead && context._gapAhead <= _settings._shellRange;
            case KartItemKind::Shield:
                return context._bShielded == SW_FALSE;
            case KartItemKind::Booster:
            case KartItemKind::LeaderShell:
                return true;
        }
        return false;
    }
} // namespace sw
