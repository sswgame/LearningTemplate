#include "pch.h"

#include "GameFramework/Base/Character/Camera/PhotoModeCamera.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    PhotoModeCamera::PhotoModeCamera()
        : _settings{}
        , _rotation{}
        , _position{}
        , _yaw{ 0.0f }
        , _pitch{ 0.0f }
        , _roll{ 0.0f }
        , _fieldOfViewY{ 0.0f }
        , _orthoHeight{ 0.0f }
        , _exposureOffset{ 0.0f }
        , _bOrthographic{ false }
        , _bActive{ false }
    {
    }

    void PhotoModeCamera::begin( const CameraPose& from )
    {
        _rotation      = from._rotation;
        _position      = from._position;
        _fieldOfViewY  = from._fieldOfViewY;
        _orthoHeight   = from._orthoHeight;
        _bOrthographic = from._bOrthographic == SW_TRUE;
        // 요 · 피치 · 롤은 `makeFromYawPitchRoll` 의 역(`getEulerAngles`)이다 — 롤도 읽어야 둘러보기를 시작해도 기울기가 남는다.
        const float3 euler = from._rotation.getEulerAngles();
        _pitch             = euler._x;
        _yaw               = euler._y;
        _roll              = euler._z;
        _exposureOffset    = 0.0f;
        _bActive           = true;
    }

    void PhotoModeCamera::end()
    {
        _bActive = false;
    }

    void PhotoModeCamera::applyInput( const PhotoModeInput& input, float32 deltaTime )
    {
        if ( _bActive == false )
            return;
        const float32 elapsed = MathUtil::max( deltaTime, 0.0f );
        if ( _bOrthographic )
        {
            // 2D: 화면의 오른쪽 · 위로 옮긴다. 화면 높이에 비례해 빨라져서 확대해도 화면 기준 속도가 같다.
            const float3  right    = float3::transform( float3{ 1.0f, 0.0f, 0.0f }, _rotation );
            const float3  up       = float3::transform( float3{ 0.0f, 1.0f, 0.0f }, _rotation );
            const float32 distance = _settings._orthoPanScreensPerSecond * elapsed * _orthoHeight;
            _position              = _position + ( right * input._move._x + up * input._move._y ) * distance;
            if ( input._zoomNotches != 0.0f )
                _orthoHeight = MathUtil::clamp( _orthoHeight * MathUtil::pow( _settings._zoomStep, input._zoomNotches ), _settings._minOrthoHeight, _settings._maxOrthoHeight );
        }
        else
        {
            const bool bLooked = input._look._x != 0.0f || input._look._y != 0.0f;
            if ( bLooked )
            {
                _yaw   = MathUtil::wrapAngle( _yaw + input._look._x * _settings._lookSensitivity );
                _pitch = MathUtil::clamp( _pitch + input._look._y * _settings._lookSensitivity, -_settings._pitchLimit, _settings._pitchLimit );
            }
            if ( input._roll != 0.0f )
                _roll = MathUtil::wrapAngle( _roll + input._roll * _settings._rollSpeed * elapsed );
            // 입력이 없으면 켤 때의 방향을 그대로 둔다(피치 한계 밖이던 시점도 건드리기 전까지는 그대로).
            if ( bLooked || input._roll != 0.0f )
                _rotation = quaternion::makeFromYawPitchRoll( _yaw, _pitch, _roll );
            // 수평 이동은 요 기준(앞 +Z, 오른쪽 +X 에서 돈다), 오르내림은 월드 위다.
            const float3  forward  = float3{ MathUtil::sin( _yaw ), 0.0f, MathUtil::cos( _yaw ) };
            const float3  right    = float3{ MathUtil::cos( _yaw ), 0.0f, -MathUtil::sin( _yaw ) };
            const float3  up       = float3{ 0.0f, 1.0f, 0.0f };
            const float32 distance = _settings._moveSpeed * elapsed;
            _position              = _position + ( forward * input._move._y + right * input._move._x + up * input._rise ) * distance;
            if ( input._zoomNotches != 0.0f )
                _fieldOfViewY = MathUtil::clamp( _fieldOfViewY * MathUtil::pow( _settings._zoomStep, input._zoomNotches ), _settings._minFieldOfViewY, _settings._maxFieldOfViewY );
        }
        if ( input._exposureSteps != 0.0f )
            _exposureOffset = MathUtil::clamp( _exposureOffset + input._exposureSteps * _settings._exposureStep, -_settings._maxExposureOffset, _settings._maxExposureOffset );
    }

    CameraPose PhotoModeCamera::makePose( const CameraPose& base ) const
    {
        if ( _bActive == false )
            return base;
        CameraPose pose    = base;
        pose._position     = _position;
        pose._rotation     = _rotation;
        pose._fieldOfViewY = _fieldOfViewY;
        pose._orthoHeight  = _orthoHeight;
        pose._postProcess._exposureCompensation += _exposureOffset;
        return pose;
    }
} // namespace sw
