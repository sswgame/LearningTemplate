#include "pch.h"

#include "GameFramework/Base/Actor/Camera/CameraMode.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/Base/Actor/Camera/CameraCollisionProbe.h"
#include "GameFramework/Base/Foundation/Utility/Math/RayMath.h"

namespace sw
{
    namespace
    {
        struct CameraModeInternal
        {
            /** @brief 화면 비율의 하한입니다. 0 이나 음수(크기가 0 인 창)로 나누지 않게 합니다. */
            static constexpr float32 kMinAspectRatio = 0.01f;

            /**
             * @brief 모드가 정하는 시점(위치, 회전, 투영)입니다. 렌즈의 나머지 값(초점, 조리개, 시프트, 더치)과 후처리는 `evaluateCameraMode` 가 적용한다.
             * @details 모드마다 일찍 끝나는 분기가 있어서, 모든 모드에 같이 적용하는 값은 이 함수 밖에서 한 번에 적용한다.
             *          단계는 줌, look-ahead 초점, 모드별 배치, 암, 프레이밍, 경계 가두기 순이다.
             */
            static CameraPose evaluateView( const CameraPresetDef& def, const CameraTarget& target, float32 deltaTime, CameraModeState& inoutState,
                                            const ICameraCollisionProbe* pProbe );

            /** @brief 모드별 배치 단계가 읽고 고치는 시점 값입니다. 초점은 look-ahead 를 더한 대상 위치입니다. */
            struct CameraRig
            {
                float3  _focus{};
                float3  _pivot{};
                float3  _aimPoint{};
                float32 _yaw{ 0.0f };
                float32 _pitch{ 0.0f };
                bool    _bArm{ false };     ///< 피벗에서 물러나는 모드(암이 있다)
                bool    _bCompose{ false }; ///< 프레이밍이 조준을 정하는 모드
            };

            /**
             * @brief 줌 입력과 그룹 맞추기를 적용하고 원근 거리를 돌려줍니다. 직교면 @p inoutPose 의 화면 높이를 바꿉니다.
             * @details 그룹 맞추기는 그룹이 화면에 들어올 만큼만 늘리고 줄이지 않는다.
             */
            static float32 applyZoom( const CameraPresetDef& def, const CameraTarget& target, const CameraModeState& state, CameraPose& inoutPose );
            /** @brief look-ahead 를 더한 초점입니다. 대상 속도는 지난 프레임 초점과의 차이로 구한다. */
            static float3 computeLookAheadFocus( const CameraFramingDef& framing, const CameraTarget& target, float32 elapsed, CameraModeState& inoutState );
            /**
             * @brief 모드마다 위치와 각을 정합니다. 시점이 다 정해져 남은 단계가 필요 없으면 true 입니다.
             * @details 모드마다 짧은 분기인 switch 디스패처라 더 나누지 않는다. 모드 하나의 배치 규칙을 한곳에서 읽는다.
             */
            static bool placeByMode( const CameraPresetDef& def, const CameraTarget& target, float32 inputYaw, CameraModeState& inoutState, CameraRig& inoutRig,
                                     CameraPose& inoutPose );
            /** @brief 대상이 프레이밍의 화면 위치에 오도록 감쇠한 조준 회전입니다. */
            static quaternion composeFraming( const CameraFramingDef& framing, const CameraTarget& target, const CameraPose& pose, const float3& aimPoint, float32 elapsed,
                                              CameraModeState& inoutState );

            static float3 rotateByYaw( const float3& value, float32 yaw ) { return float3::transform( value, quaternion::makeFromYawPitchRoll( yaw, 0.0f, 0.0f ) ); }

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
                float32    allowed         = distance;
                bool       bHit            = false;
                const bool bCollisionProbe = collision._bEnabled && pProbe != nullptr && distance > 0.0f;
                if ( bCollisionProbe )
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
            // XY 평면(2D)은 화면 위가 +Y, 오른쪽이 +X 다.
            const float32 yaw     = def._view._yaw + inoutState._yawOffset + inoutState._rotateYawShown;
            const bool    bPlanar = def._view._mode == CameraPresetMode::FollowXY;
            const float3  forward = bPlanar ? float3{ 0.0f, 1.0f, 0.0f } : float3{ MathUtil::sin( yaw ), 0.0f, MathUtil::cos( yaw ) };
            const float3  right   = bPlanar ? float3{ 1.0f, 0.0f, 0.0f } : float3{ forward._z, 0.0f, -forward._x };
            const float32 zoom    = inoutState._zoom > 0.0f ? inoutState._zoom : def._lens._orthoHeight;
            const float32 scale   = ( bOrtho && def._lens._orthoHeight > 0.0f ) ? zoom / def._lens._orthoHeight : 1.0f;
            inoutState._panOffset = inoutState._panOffset + ( forward * input._pan._x + right * input._pan._y ) * ( inputDef._panSpeed * scale * MathUtil::max( 0.0f, deltaTime ) );
        }

        if ( inputDef._rotateStep != 0.0f && input._rotateSteps != 0 )
            inoutState._rotateYaw += static_cast<float32>( input._rotateSteps ) * inputDef._rotateStep;
    }

    float32 CameraModeInternal::applyZoom( const CameraPresetDef& def, const CameraTarget& target, const CameraModeState& state, CameraPose& inoutPose )
    {
        const CameraConfinerDef& confiner = def._confiner;
        const CameraFramingDef&  framing  = def._framing;
        const bool               bOrtho   = inoutPose._bOrthographic == SW_TRUE;
        float32                  distance = def._view._distance;
        if ( state._zoom > 0.0f )
        {
            if ( bOrtho )
                inoutPose._orthoHeight = state._zoom;
            else
                distance = state._zoom;
        }
        if ( framing._groupPadding > 0.0f && target._groupRadius > 0.0f )
        {
            const float32 radius = target._groupRadius * framing._groupPadding;
            if ( bOrtho )
                inoutPose._orthoHeight = MathUtil::max( inoutPose._orthoHeight, radius * 2.0f );
            else
                distance = MathUtil::max( distance, radius / MathUtil::max( 0.01f, MathUtil::sin( inoutPose._fieldOfViewY * 0.5f ) ) );
        }
        if ( bOrtho )
            inoutPose._orthoHeight = clampZoom( confiner, inoutPose._orthoHeight );
        else
            distance = clampZoom( confiner, distance );
        return distance;
    }

    float3 CameraModeInternal::computeLookAheadFocus( const CameraFramingDef& framing, const CameraTarget& target, float32 elapsed, CameraModeState& inoutState )
    {
        if ( framing._lookAheadTime <= 0.0f )
            return target._focus;
        if ( inoutState._bHasPreviousFocus == SW_TRUE && elapsed > 0.0f )
        {
            const float3 velocity = ( target._focus - inoutState._previousFocus ) * ( 1.0f / elapsed );
            inoutState._lookAhead = float3::lerp( inoutState._lookAhead, velocity * framing._lookAheadTime, computeDampingAlpha( framing._lookAheadSmoothing, elapsed ) );
        }
        inoutState._previousFocus     = target._focus;
        inoutState._bHasPreviousFocus = SW_TRUE;
        return target._focus + inoutState._lookAhead;
    }

    bool CameraModeInternal::placeByMode( const CameraPresetDef& def, const CameraTarget& target, float32 inputYaw, CameraModeState& inoutState, CameraRig& inoutRig,
                                          CameraPose& inoutPose )
    {
        const CameraViewDef&     view     = def._view;
        const CameraConfinerDef& confiner = def._confiner;
        switch ( view._mode )
        {
            case CameraPresetMode::Fixed:
            {
                inoutPose._position = view._offset + inoutState._panOffset;
                if ( confiner._bBounds )
                    inoutPose._position = clampToBounds( confiner, inoutPose._position );
                if ( view._aim == CameraAimMode::Angles )
                {
                    inoutPose._rotation = quaternion::makeFromYawPitchRoll( inoutRig._yaw, clampPitch( confiner, inoutRig._pitch ), 0.0f );
                    return true;
                }
                // 점이나 대상을 보는 고정 카메라(CCTV)다. 보는 각에 훑기와 입력을 더한다.
                inoutRig._aimPoint = view._aim == CameraAimMode::Point ? view._lookAt : inoutRig._focus;
                const float2 toAim = computeLookAngles( inoutPose._position, inoutRig._aimPoint );
                inoutRig._yaw      = toAim._x + inputYaw;
                inoutRig._pitch    = toAim._y + inoutState._pitchOffset;
                inoutRig._bCompose = view._aim == CameraAimMode::Target;
                return false;
            }
            case CameraPresetMode::FirstPerson:
            {
                inoutRig._yaw       = target._yaw + inoutRig._yaw;
                inoutRig._pitch     = clampPitch( confiner, target._pitch + inoutRig._pitch );
                inoutPose._rotation = quaternion::makeFromYawPitchRoll( inoutRig._yaw, inoutRig._pitch, 0.0f );
                inoutPose._position = target._focus + rotateByYaw( view._offset, target._yaw );
                return true;
            }
            case CameraPresetMode::ThirdPerson:
            {
                // 어깨 너머 시점이다. 대상의 시점을 따르고, 피벗(어깨)은 대상 요로 돌린 오프셋이다.
                inoutRig._yaw   = target._yaw + inoutRig._yaw;
                inoutRig._pitch = target._pitch + inoutRig._pitch;
                inoutRig._pivot = target._focus + rotateByYaw( view._offset, target._yaw ) + inoutState._lookAhead;
                inoutRig._bArm  = true;
                return false;
            }
            case CameraPresetMode::Follow:
            {
                inoutRig._yaw      = target._yaw + inoutRig._yaw;
                inoutRig._pivot    = inoutRig._focus + rotateByYaw( view._offset, target._yaw );
                inoutRig._aimPoint = inoutRig._pivot;
                inoutRig._bArm     = true;
                inoutRig._bCompose = true;
                return false;
            }
            case CameraPresetMode::OrthoTopDown:
            {
                // 직교 시점의 상자는 초점(X, Z)을 가둔다. 카메라 위치는 수백 m 위에 있어 그것을 가두면 화면이 상자 밖을 본다.
                // 팬도 상자 끝에서 멈춘다. 넘친 만큼 남겨 두면 되돌아올 때 그만큼 키를 눌러도 화면이 움직이지 않는다.
                if ( confiner._bBounds )
                {
                    const float3 clamped = clampToBounds( confiner, inoutRig._pivot );
                    inoutState._panOffset._x += clamped._x - inoutRig._pivot._x;
                    inoutState._panOffset._z += clamped._z - inoutRig._pivot._z;
                    inoutRig._pivot = float3{ clamped._x, inoutRig._pivot._y, clamped._z };
                }
                return false;
            }
            case CameraPresetMode::Orbit:
            {
                inoutRig._aimPoint = inoutRig._pivot;
                inoutRig._bArm     = true;
                inoutRig._bCompose = true;
                return false;
            }
            case CameraPresetMode::FollowXY:
            {
                // 2D 씬은 Y 가 위인 땅(XZ)이 아니라 XY 평면이다. 카메라는 초점 위가 아니라 카메라 평면(오프셋 Z)에 서서 평면을 본다.
                inoutPose._position = float3{ inoutRig._pivot._x, inoutRig._pivot._y, view._offset._z };
                if ( confiner._bBounds )
                {
                    const float3 clamped = clampToBounds( confiner, inoutPose._position );
                    inoutPose._position  = float3{ clamped._x, clamped._y, inoutPose._position._z };
                }
                inoutPose._rotation = quaternion::makeFromYawPitchRoll( inoutRig._yaw, clampPitch( confiner, inoutRig._pitch ), 0.0f );
                return true;
            }
            case CameraPresetMode::Dolly:
            {
                // 레일 위에서 대상에 가장 가까운 점에 서서 대상을 본다. 곡선은 처음 한 번만 만든다(호 길이 테이블).
                if ( inoutState._bDollyPathBuilt == SW_FALSE )
                {
                    // 점이 모자라면 곡선이 비고 isValid 가 false 다. 그때는 오프셋 위치에 선다.
                    (void)inoutState._dollyPath.initialize( def._dolly._listPoint, def._dolly._type, def._dolly._bClosed );
                    inoutState._bDollyPathBuilt = SW_TRUE;
                }
                if ( inoutState._dollyPath.isValid() )
                {
                    const SplineSample nearest = inoutState._dollyPath.findClosest( inoutRig._focus );
                    inoutPose._position        = inoutState._dollyPath.sampleAtDistance( nearest._distance + def._dolly._pathOffset )._position;
                }
                else
                {
                    inoutPose._position = view._offset;
                }
                inoutRig._aimPoint = inoutRig._focus;
                const float2 toAim = computeLookAngles( inoutPose._position, inoutRig._aimPoint );
                inoutRig._yaw      = toAim._x + inputYaw;
                inoutRig._pitch    = toAim._y + inoutState._pitchOffset;
                inoutRig._bCompose = true;
                return false;
            }
        }
        return false;
    }

    quaternion CameraModeInternal::composeFraming( const CameraFramingDef& framing, const CameraTarget& target, const CameraPose& pose, const float3& aimPoint,
                                                   float32 elapsed, CameraModeState& inoutState )
    {
        const float32 tanHalfY     = MathUtil::tan( pose._fieldOfViewY * 0.5f );
        const float32 tanHalfX     = tanHalfY * MathUtil::max( kMinAspectRatio, target._aspectRatio );
        const float2  toAim        = computeLookAngles( pose._position, aimPoint );
        const float32 screenYaw    = computeAtan( ( framing._screenPosition._x - 0.5f ) * 2.0f * tanHalfX );
        const float32 screenPitch  = computeAtan( ( framing._screenPosition._y - 0.5f ) * 2.0f * tanHalfY );
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
            inoutState._aimYaw   = composeAxis( inoutState._aimYaw, desiredYaw, computeAtan( framing._deadZone._x * tanHalfX ), computeAtan( framing._softZone._x * tanHalfX ), alpha );
            inoutState._aimPitch = composeAxis( inoutState._aimPitch, desiredPitch, computeAtan( framing._deadZone._y * tanHalfY ), computeAtan( framing._softZone._y * tanHalfY ),
                                                alpha );
        }
        return quaternion::makeFromYawPitchRoll( inoutState._aimYaw, inoutState._aimPitch, 0.0f );
    }

    CameraPose CameraModeInternal::evaluateView( const CameraPresetDef& def, const CameraTarget& target, float32 deltaTime, CameraModeState& inoutState,
                                                 const ICameraCollisionProbe* pProbe )
    {
        const CameraViewDef&     view     = def._view;
        const CameraConfinerDef& confiner = def._confiner;
        const CameraFramingDef&  framing  = def._framing;
        const float32            elapsed  = MathUtil::max( 0.0f, deltaTime );
        inoutState._time += elapsed;
        inoutState._rotateYawShown += ( inoutState._rotateYaw - inoutState._rotateYawShown ) * computeDampingAlpha( def._input._rotateTime, elapsed );

        CameraPose pose;
        pose._fieldOfViewY  = def._lens.computeFieldOfViewY();
        pose._orthoHeight   = def._lens._orthoHeight;
        pose._nearPlane     = def._lens._nearPlane;
        pose._farPlane      = def._lens._farPlane;
        pose._bOrthographic = isOrthographic( def ) ? SW_TRUE : SW_FALSE;

        const float32 distance = applyZoom( def, target, inoutState, pose );
        const float32 sweep    = def._sweep._yawAmplitude > 0.0f
                                   ? def._sweep._yawAmplitude * MathUtil::sin( MathUtil::kTwoPi * ( inoutState._time / def._sweep._period + def._sweep._phase ) )
                                   : 0.0f;
        const float32 inputYaw = inoutState._yawOffset + inoutState._rotateYawShown + sweep;

        CameraRig rig;
        rig._focus    = computeLookAheadFocus( framing, target, elapsed, inoutState );
        rig._yaw      = view._yaw + inputYaw;
        rig._pitch    = view._pitch + inoutState._pitchOffset;
        rig._pivot    = rig._focus + view._offset + inoutState._panOffset;
        rig._aimPoint = rig._pivot;
        if ( placeByMode( def, target, inputYaw, inoutState, rig, pose ) )
            return pose;

        const float32 pitch   = clampPitch( confiner, rig._pitch );
        const float3  forward = computeForward( rig._yaw, pitch );
        const bool    bPlaced = view._mode == CameraPresetMode::Fixed || view._mode == CameraPresetMode::Dolly; ///< 위치를 이미 정한 모드(피벗에서 물러나지 않는다)
        if ( bPlaced == false )
        {
            const float32 arm = rig._bArm ? resolveArm( def._collision, rig._pivot, forward * -1.0f, distance, elapsed, pProbe, inoutState ) : distance;
            pose._position    = rig._pivot - forward * arm;
        }

        // 대상이 화면 위치에 오도록 조준을 비튼다. 데드존 안의 움직임은 카메라를 돌리지 않고, 소프트존 밖으로는 나가지 않는다.
        const bool bComposeFraming = rig._bCompose && framing._bCompose && pose._bOrthographic == SW_FALSE;
        if ( bComposeFraming )
            pose._rotation = composeFraming( framing, target, pose, rig._aimPoint, elapsed, inoutState );
        else
            pose._rotation = quaternion::makeFromYawPitchRoll( rig._yaw, pitch, 0.0f );

        const bool bClampToBounds = confiner._bBounds && view._mode != CameraPresetMode::OrthoTopDown && bPlaced == false;
        if ( bClampToBounds )
            pose._position = clampToBounds( confiner, pose._position );
        return pose;
    }

    CameraPose evaluateCameraMode( const CameraPresetDef& def, const CameraTarget& target, float32 deltaTime, CameraModeState& inoutState,
                                   const ICameraCollisionProbe* pProbe )
    {
        CameraPose           pose = CameraModeInternal::evaluateView( def, target, deltaTime, inoutState, pProbe );
        const CameraLensDef& lens = def._lens;
        pose._focusDistance       = lens._focusDistance;
        pose._aperture            = lens._aperture;
        pose._lensShift           = lens._lensShift;
        pose._postProcess         = def._postProcess;
        // 더치는 모든 모드의 시점 위에 마지막으로 — 카메라 앞축으로 화면을 기울인다.
        if ( lens._dutch != 0.0f )
            pose._rotation = pose._rotation * quaternion::makeFromYawPitchRoll( 0.0f, 0.0f, lens._dutch );
        return pose;
    }

    CameraShakeOffset computeCameraHeadBob( const CameraHeadBobDef& headBob, const float3& focus, float32 deltaTime, CameraModeState& inoutState )
    {
        CameraShakeOffset offset{};
        const bool        bEnabled = headBob._verticalAmplitude > 0.0f || headBob._lateralAmplitude > 0.0f;
        if ( bEnabled == false )
            return offset;
        if ( inoutState._bHasBobFocus == SW_TRUE )
        {
            const float3  moved    = focus - inoutState._bobPreviousFocus;
            const float32 distance = float2{ moved._x, moved._z }.getLength();
            // 한 평가에 한 주기보다 멀리 갔으면 순간 이동이다 — 위상 · 빠르기에 넣지 않는다.
            if ( distance < headBob._strideLength )
            {
                inoutState._bobPhase = MathUtil::wrapAngle( inoutState._bobPhase + distance / headBob._strideLength * MathUtil::kTwoPi );
                inoutState._bobDistance += distance;
            }
        }
        inoutState._bobPreviousFocus = focus;
        inoutState._bHasBobFocus     = SW_TRUE;
        if ( deltaTime > 0.0f )
        {
            const float32 speed        = inoutState._bobDistance / deltaTime;
            const float32 targetWeight = MathUtil::saturate( speed / MathUtil::max( MathUtil::kEpsilon, headBob._referenceSpeed ) );
            inoutState._bobDistance    = 0.0f;
            inoutState._bobWeight += ( targetWeight - inoutState._bobWeight ) * computeDampingAlpha( headBob._blendTime, deltaTime );
        }
        // 한 주기(두 걸음)에 위아래는 두 번, 좌우는 한 번 — 발이 땅을 디딜 때마다 내려간다.
        offset._position = float3{ MathUtil::sin( inoutState._bobPhase ) * headBob._lateralAmplitude, MathUtil::sin( inoutState._bobPhase * 2.0f ) * headBob._verticalAmplitude,
                                   0.0f } *
                           inoutState._bobWeight;
        return offset;
    }

    CameraPose evaluatePreset( const CameraPresetDef& def, const CameraTarget& target )
    {
        CameraModeState state;
        return evaluateCameraMode( def, target, 0.0f, state, nullptr );
    }

    GameRay computeCameraScreenRay( const CameraPose& pose, float32 aspectRatio, const float2& mouseNormalized )
    {
        const float3  right   = float3::transform( float3{ 1.0f, 0.0f, 0.0f }, pose._rotation );
        const float3  up      = float3::transform( float3{ 0.0f, 1.0f, 0.0f }, pose._rotation );
        const float3  forward = float3::transform( float3{ 0.0f, 0.0f, 1.0f }, pose._rotation );
        const float32 aspect  = MathUtil::max( CameraModeInternal::kMinAspectRatio, aspectRatio );
        const float32 screenX = mouseNormalized._x * 2.0f - 1.0f;
        const float32 screenY = 1.0f - mouseNormalized._y * 2.0f;
        GameRay       ray;
        if ( pose._bOrthographic == SW_TRUE )
        {
            // 직교 — 광선은 모두 앞 방향과 나란하고, 시작점이 화면 평면 위에서 옮겨진다.
            const float32 halfHeight = pose._orthoHeight * 0.5f;
            ray._origin              = pose._position + right * ( screenX * halfHeight * aspect ) + up * ( screenY * halfHeight );
            ray._direction           = forward;
            return ray;
        }
        const float32 tanHalfY  = MathUtil::tan( pose._fieldOfViewY * 0.5f );
        float3        direction = forward + right * ( screenX * tanHalfY * aspect ) + up * ( screenY * tanHalfY );
        direction.normalize();
        ray._origin    = pose._position;
        ray._direction = direction;
        return ray;
    }
} // namespace sw
