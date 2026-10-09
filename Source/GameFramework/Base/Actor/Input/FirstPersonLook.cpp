#include "pch.h"

#include "GameFramework/Base/Actor/Input/FirstPersonLook.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    namespace
    {
        struct FirstPersonLookInternal
        {
        };
    } // namespace
} // namespace sw

namespace sw
{
    FirstPersonLook::FirstPersonLook()
        : _yaw{ 0.0f }
        , _pitch{ 0.0f }
        , _maxPitch{ 85.0f * MathUtil::kPi / 180.0f }
    {
    }

    void FirstPersonLook::addMouseDelta( float32 deltaX, float32 deltaY, float32 sensitivity )
    {
        setAngles( _yaw + deltaX * sensitivity, _pitch - deltaY * sensitivity );
    }

    void FirstPersonLook::addRecoil( float32 pitchKick, float32 yawKick )
    {
        setAngles( _yaw + yawKick, _pitch + pitchKick );
    }

    void FirstPersonLook::setAngles( float32 yaw, float32 pitch )
    {
        // 요는 [-π, π) 로 감는다 — 오래 돌아도 실수 정밀도가 줄지 않게.
        const float32 fullTurn = 2.0f * MathUtil::kPi;
        float32       wrapped  = MathUtil::fmod( yaw + MathUtil::kPi, fullTurn );
        if ( wrapped < 0.0f )
            wrapped += fullTurn;
        _yaw   = wrapped - MathUtil::kPi;
        _pitch = MathUtil::clamp( pitch, -_maxPitch, _maxPitch );
    }

    void FirstPersonLook::setMaxPitch( float32 maxPitch )
    {
        _maxPitch = MathUtil::clamp( maxPitch, 0.0f, MathUtil::kPi * 0.5f );
        _pitch    = MathUtil::clamp( _pitch, -_maxPitch, _maxPitch );
    }

    float3 FirstPersonLook::getFlatRight() const
    {
        return float3{ MathUtil::cos( _yaw ), 0.0f, -MathUtil::sin( _yaw ) };
    }

    float3 FirstPersonLook::computeMoveDirection( float32 forwardAxis, float32 rightAxis ) const
    {
        const float3  move   = getFlatForward() * forwardAxis + getFlatRight() * rightAxis;
        const float32 length = move.getLength();
        return length > 1.0f ? move * ( 1.0f / length ) : move;
    }
} // namespace sw
