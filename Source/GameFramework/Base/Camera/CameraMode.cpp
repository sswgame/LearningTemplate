#include "pch.h"

#include "GameFramework/Base/Camera/CameraMode.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/Base/Camera/CameraCollisionProbe.h"

namespace sw
{
    namespace
    {
        struct CameraModeInternal
        {
            /** @brief 프레이밍이 화면 가로를 셀 때의 비율입니다. 모드는 화면을 모르므로 흔한 16:9 로 셉니다(세로 위치 · 존은 정확하다). */
            static constexpr float32 kComposeAspect = 16.0f / 9.0f;

            static float3 rotateByYaw( const float3& value, float32 yaw ) { return float3::transform( value, quaternion::createFromYawPitchRoll( yaw, 0.0f, 0.0f ) ); }

            static float3 computeForward( float32 yaw, float32 pitch )
            {
                const float32 horizontal = MathUtil::cos( pitch );
                return float3{ MathUtil::sin( yaw ) * horizontal, -MathUtil::sin( pitch ), MathUtil::cos( yaw ) * horizontal };
            }

            static bool isOrthographic( const CameraPresetDef& def ) { return def._view._mode == CameraPresetMode::OrthoTopDown || def._lens._bOrthographic; }

            static float32 computeAtan( float32 value ) { return MathUtil::atan2( value, 1.0f ); }

            static float32 clampPitch( const CameraConfinerDef& confiner, float32 pitch ) { return MathUtil::clamp( pitch, confiner._pitchMin, confiner._pitchMax ); }

            static float32 clampZoom( const CameraConfinerDef& confiner, float32 zoom )
            {
                float32 clamped = zoom;
                if ( confiner._zoomMin > 0.0f )
                    clamped = MathUtil::max( clamped, confiner._zoomMin );
                if ( confiner._zoomMax > 0.0f )
                    clamped = MathUtil::min( clamped, confiner._zoomMax );
                return clamped;
            }

            static float3 clampToBounds( const CameraConfinerDef& confiner, const float3& value )
            {
                return float3{ MathUtil::clamp( value._x, confiner._boundsMin._x, confiner._boundsMax._x ),
                               MathUtil::clamp( value._y, confiner._boundsMin._y, confiner._boundsMax._y ),
                               MathUtil::clamp( value._z, confiner._boundsMin._z, confiner._boundsMax._z ) };
            }

            /** @brief 축 하나의 조준을 데드존 · 소프트존으로 원하는 각 쪽으로 옮깁니다. */
            static float32 composeAxis( float32 aim, float32 desired, float32 deadHalf, float32 softHalf, float32 alpha )
            {
                float32 error = MathUtil::wrapAngle( desired - aim );
                if ( MathUtil::abs( error ) > deadHalf )
                {
                    const float32 excess = error > 0.0f ? error - deadHalf : error + deadHalf;
                    aim += excess * alpha;
                }
                // 소프트존 밖으로는 한 프레임도 나가지 않는다 — 감쇠가 늦어도 대상이 화면 밖으로 빠지지 않게.
                error = MathUtil::wrapAngle( desired - aim );
                if ( error > softHalf )
                    aim = desired - softHalf;
                else if ( error < -softHalf )
                    aim = desired + softHalf;
                return aim;
            }

            /** @brief 스프링 암 — 피벗에서 원하는 자리까지 쓸어 막히면 당기고(바로), 풀리면 회복 시간으로 돌아간다. 쓴 길이를 돌려준다. */
            static float32 resolveArm( const CameraCollisionDef& collision, const float3& pivot, const float3& backward, float32 distance, float32 deltaTime,
                                       const ICameraCollisionProbe* pProbe, CameraModeState& inoutState )
            {
                float32 allowed = distance;
                bool    bHit    = false;
                if ( collision._bEnabled && pProbe != nullptr && distance > 0.0f )
                {
                    float32 hitDistance = distance;
                    bHit                = pProbe->sweepSphere( pivot, pivot + backward * distance, collision._radius, hitDistance );
                    if ( bHit )
                        allowed = MathUtil::clamp( hitDistance, MathUtil::min( collision._minDistance, distance ), distance );
                }
                inoutState._bArmBlocked = bHit ? SW_TRUE : SW_FALSE;
                if ( inoutState._armLength < 0.0f || allowed < inoutState._armLength )
                    inoutState._armLength = allowed;
                else
                    inoutState._armLength += ( allowed - inoutState._armLength ) * computeDampingAlpha( collision._recoverTime, deltaTime );
                return inoutState._armLength;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    float2 computeLookAngles( const float3& from, const float3& to )
    {
        float3 direction = to - from;
        if ( direction.getLengthSquared() <= MathUtil::kEpsilon )
            return float2{ 0.0f, 0.0f };
        direction.normalize();
        return float2{ MathUtil::atan2( direction._x, direction._z ), -MathUtil::asin( MathUtil::clamp( direction._y, -1.0f, 1.0f ) ) };
    }

    CameraTarget makeGroupCameraTarget( const float3* pPoint, uint32 pointCount )
    {
        CameraTarget target;
        if ( pPoint == nullptr || pointCount == 0 )
            return target;
        float3 boundsMin = pPoint[0];
        float3 boundsMax = pPoint[0];
        for ( uint32 index = 1; index < pointCount; ++index )
        {
            boundsMin = float3::min( boundsMin, pPoint[index] );
            boundsMax = float3::max( boundsMax, pPoint[index] );
        }
        target._focus = ( boundsMin + boundsMax ) * 0.5f;
        for ( uint32 index = 0; index < pointCount; ++index )
        {
            target._groupRadius = MathUtil::max( target._groupRadius, ( pPoint[index] - target._focus ).getLength() );
        }
        return target;
    }

    void applyCameraInput( const CameraPresetDef& def, const CameraModeInput& input, float32 deltaTime, CameraModeState& inoutState )
    {
        const CameraInputDef&    inputDef = def._input;
        const CameraConfinerDef& confiner = def._confiner;
        const bool               bOrtho   = CameraModeInternal::isOrthographic( def );

        const bool bLook = inputDef._lookSensitivity > 0.0f && ( inputDef._bLookWhileHeld == false || input._bLookHeld == SW_TRUE );
        if ( bLook )
        {
            inoutState._yawOffset = MathUtil::wrapAngle( inoutState._yawOffset + input._lookDelta._x * inputDef._lookSensitivity );
            // 1인칭 · 3인칭은 대상의 피치에 더하므로 여기서는 오프셋만 자르고, 합은 평가가 다시 자른다.
            const float32 pitch     = CameraModeInternal::clampPitch( confiner, def._view._pitch + inoutState._pitchOffset + input._lookDelta._y * inputDef._lookSensitivity );
            inoutState._pitchOffset = pitch - def._view._pitch;
        }

        if ( inputDef._zoomStep > 0.0f && input._zoomNotches != 0.0f )
        {
            const float32 current = inoutState._zoom > 0.0f ? inoutState._zoom : ( bOrtho ? def._lens._orthoHeight : def._view._distance );
            inoutState._zoom      = CameraModeInternal::clampZoom( confiner, current * MathUtil::pow( inputDef._zoomStep, input._zoomNotches ) );
        }

        if ( inputDef._panSpeed > 0.0f && ( input._pan._x != 0.0f || input._pan._y != 0.0f ) )
        {
            // 지금 보이는 요 기준이다 — 화면 위쪽이 앞이다. 직교는 확대할수록 느리게(화면에서 보이는 빠르기가 같다).
            const float32 yaw = def._view._yaw + inoutState._yawOffset + inoutState._rotateYawShown;
            const float3  forward{ MathUtil::sin( yaw ), 0.0f, MathUtil::cos( yaw ) };
            const float3  right{ forward._z, 0.0f, -forward._x };
            const float32 zoom    = inoutState._zoom > 0.0f ? inoutState._zoom : def._lens._orthoHeight;
            const float32 scale   = ( bOrtho && def._lens._orthoHeight > 0.0f ) ? zoom / def._lens._orthoHeight : 1.0f;
            inoutState._panOffset = inoutState._panOffset + ( forward * input._pan._x + right * input._pan._y ) * ( inputDef._panSpeed * scale * MathUtil::max( 0.0f, deltaTime ) );
        }

        if ( inputDef._rotateStep != 0.0f && input._rotateSteps != 0 )
            inoutState._rotateYaw += static_cast<float32>( input._rotateSteps ) * inputDef._rotateStep;
    }

    CameraPose evaluateCameraMode( const CameraPresetDef& def, const CameraTarget& target, float32 deltaTime, CameraModeState& inoutState,
                                   const ICameraCollisionProbe* pProbe )
    {
        const CameraViewDef&     view     = def._view;
        const CameraConfinerDef& confiner = def._confiner;
        const CameraFramingDef&  framing  = def._framing;
        const float32            elapsed  = MathUtil::max( 0.0f, deltaTime );
        inoutState._time += elapsed;
        inoutState._rotateYawShown += ( inoutState._rotateYaw - inoutState._rotateYawShown ) * computeDampingAlpha( def._input._rotateTime, elapsed );

        CameraPose pose;
        pose._fieldOfViewY  = def._lens._fieldOfViewY;
        pose._orthoHeight   = def._lens._orthoHeight;
        pose._nearPlane     = def._lens._nearPlane;
        pose._farPlane      = def._lens._farPlane;
        pose._bOrthographic = CameraModeInternal::isOrthographic( def ) ? SW_TRUE : SW_FALSE;

        // 줌 — 원근은 거리, 직교는 화면 높이. 그룹 맞추기는 묶음이 화면에 들어올 만큼만 늘린다(줄이지 않는다).
        float32 distance = view._distance;
        if ( inoutState._zoom > 0.0f )
        {
            if ( pose._bOrthographic == SW_TRUE )
                pose._orthoHeight = inoutState._zoom;
            else
                distance = inoutState._zoom;
        }
        if ( framing._groupPadding > 0.0f && target._groupRadius > 0.0f )
        {
            const float32 radius = target._groupRadius * framing._groupPadding;
            if ( pose._bOrthographic == SW_TRUE )
                pose._orthoHeight = MathUtil::max( pose._orthoHeight, radius * 2.0f );
            else
                distance = MathUtil::max( distance, radius / MathUtil::max( 0.01f, MathUtil::sin( pose._fieldOfViewY * 0.5f ) ) );
        }
        if ( pose._bOrthographic == SW_TRUE )
            pose._orthoHeight = CameraModeInternal::clampZoom( confiner, pose._orthoHeight );
        else
            distance = CameraModeInternal::clampZoom( confiner, distance );

        // look-ahead — 대상이 가는 쪽을 미리 본다. 속도는 지난 프레임 초점과의 차이다.
        float3 focus = target._focus;
        if ( framing._lookAheadTime > 0.0f )
        {
            if ( inoutState._bHasPreviousFocus == SW_TRUE && elapsed > 0.0f )
            {
                const float3 velocity = ( target._focus - inoutState._previousFocus ) * ( 1.0f / elapsed );
                inoutState._lookAhead = float3::lerp( inoutState._lookAhead, velocity * framing._lookAheadTime,
                                                      computeDampingAlpha( framing._lookAheadSmoothing, elapsed ) );
            }
            inoutState._previousFocus     = target._focus;
            inoutState._bHasPreviousFocus = SW_TRUE;
            focus                         = focus + inoutState._lookAhead;
        }

        const float32 sweep    = def._sweep._yawAmplitude > 0.0f
                                   ? def._sweep._yawAmplitude * MathUtil::sin( MathUtil::kTwoPi * ( inoutState._time / def._sweep._period + def._sweep._phase ) )
                                   : 0.0f;
        const float32 inputYaw = inoutState._yawOffset + inoutState._rotateYawShown + sweep;
        float32       yaw      = view._yaw + inputYaw;
        float32       pitch    = view._pitch + inoutState._pitchOffset;
        float3        pivot    = focus + view._offset + inoutState._panOffset;
        bool          bArm     = false; ///< 피벗에서 물러나는 모드(암이 있다)
        bool          bCompose = false; ///< 프레이밍이 조준을 정하는 모드
        float3        aimPoint = pivot;

        switch ( view._mode )
        {
            case CameraPresetMode::Fixed:
            {
                pose._position = view._offset + inoutState._panOffset;
                if ( confiner._bBounds )
                    pose._position = CameraModeInternal::clampToBounds( confiner, pose._position );
                if ( view._aim == CameraAimMode::Angles )
                {
                    pose._rotation = quaternion::createFromYawPitchRoll( yaw, CameraModeInternal::clampPitch( confiner, pitch ), 0.0f );
                    return pose;
                }
                // 점 · 대상을 보는 고정 카메라(CCTV) — 보는 각에 훑기 · 입력을 더한다.
                aimPoint           = view._aim == CameraAimMode::Point ? view._lookAt : focus;
                const float2 toAim = computeLookAngles( pose._position, aimPoint );
                yaw                = toAim._x + inputYaw;
                pitch              = toAim._y + inoutState._pitchOffset;
                bCompose           = view._aim == CameraAimMode::Target;
                break;
            }
            case CameraPresetMode::FirstPerson:
            {
                yaw            = target._yaw + yaw;
                pitch          = CameraModeInternal::clampPitch( confiner, target._pitch + pitch );
                pose._rotation = quaternion::createFromYawPitchRoll( yaw, pitch, 0.0f );
                pose._position = target._focus + CameraModeInternal::rotateByYaw( view._offset, target._yaw );
                return pose;
            }
            case CameraPresetMode::ThirdPerson:
            {
                // 어깨 너머 — 대상의 시점을 따른다. 피벗(어깨)은 대상 요로 돌린 오프셋이다.
                yaw   = target._yaw + yaw;
                pitch = target._pitch + pitch;
                pivot = target._focus + CameraModeInternal::rotateByYaw( view._offset, target._yaw ) + inoutState._lookAhead;
                bArm  = true;
                break;
            }
            case CameraPresetMode::Follow:
            {
                yaw      = target._yaw + yaw;
                pivot    = focus + CameraModeInternal::rotateByYaw( view._offset, target._yaw );
                aimPoint = pivot;
                bArm     = true;
                bCompose = true;
                break;
            }
            case CameraPresetMode::OrthoTopDown:
            {
                // 직교 시점의 상자는 초점(X · Z)을 가둔다 — 카메라 자리는 수백 m 위에 있어 그것을 가두면 화면이 상자 밖을 본다.
                if ( confiner._bBounds )
                {
                    const float3 clamped = CameraModeInternal::clampToBounds( confiner, pivot );
                    pivot                = float3{ clamped._x, pivot._y, clamped._z };
                }
                break;
            }
            case CameraPresetMode::Orbit:
            {
                aimPoint = pivot;
                bArm     = true;
                bCompose = true;
                break;
            }
        }

        pitch                = CameraModeInternal::clampPitch( confiner, pitch );
        const float3 forward = CameraModeInternal::computeForward( yaw, pitch );
        if ( view._mode != CameraPresetMode::Fixed )
        {
            const float32 arm = bArm ? CameraModeInternal::resolveArm( def._collision, pivot, forward * -1.0f, distance, elapsed, pProbe, inoutState ) : distance;
            pose._position    = pivot - forward * arm;
        }

        if ( bCompose && framing._bCompose && pose._bOrthographic == SW_FALSE )
        {
            // 대상이 화면 위치에 오도록 조준을 비튼다. 데드존 안의 움직임은 카메라를 돌리지 않고, 소프트존 밖으로는 나가지 않는다.
            const float32 tanHalfY     = MathUtil::tan( pose._fieldOfViewY * 0.5f );
            const float32 tanHalfX     = tanHalfY * CameraModeInternal::kComposeAspect;
            const float2  toAim        = computeLookAngles( pose._position, aimPoint );
            const float32 screenYaw    = CameraModeInternal::computeAtan( ( framing._screenPosition._x - 0.5f ) * 2.0f * tanHalfX );
            const float32 screenPitch  = CameraModeInternal::computeAtan( ( framing._screenPosition._y - 0.5f ) * 2.0f * tanHalfY );
            const float32 desiredYaw   = toAim._x - screenYaw;
            const float32 desiredPitch = toAim._y - screenPitch;
            if ( inoutState._bAimValid == SW_FALSE )
            {
                inoutState._aimYaw    = desiredYaw;
                inoutState._aimPitch  = desiredPitch;
                inoutState._bAimValid = SW_TRUE;
            }
            else
            {
                const float32 alpha  = computeDampingAlpha( framing._damping, elapsed );
                inoutState._aimYaw   = CameraModeInternal::composeAxis( inoutState._aimYaw, desiredYaw, CameraModeInternal::computeAtan( framing._deadZone._x * tanHalfX ),
                                                                        CameraModeInternal::computeAtan( framing._softZone._x * tanHalfX ), alpha );
                inoutState._aimPitch = CameraModeInternal::composeAxis( inoutState._aimPitch, desiredPitch, CameraModeInternal::computeAtan( framing._deadZone._y * tanHalfY ),
                                                                        CameraModeInternal::computeAtan( framing._softZone._y * tanHalfY ), alpha );
            }
            pose._rotation = quaternion::createFromYawPitchRoll( inoutState._aimYaw, inoutState._aimPitch, 0.0f );
        }
        else
        {
            pose._rotation = quaternion::createFromYawPitchRoll( yaw, pitch, 0.0f );
        }

        if ( confiner._bBounds && view._mode != CameraPresetMode::OrthoTopDown && view._mode != CameraPresetMode::Fixed )
            pose._position = CameraModeInternal::clampToBounds( confiner, pose._position );
        return pose;
    }

    CameraPose evaluatePreset( const CameraPresetDef& def, const CameraTarget& target )
    {
        CameraModeState state;
        return evaluateCameraMode( def, target, 0.0f, state, nullptr );
    }
} // namespace sw
