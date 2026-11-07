#include "pch.h"

#include "GameFramework/Runtime/Character/Movement/InertiaSettings.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    float32 InertiaSettings::computeAlongAcceleration( float32 speed, float32 slopeSin, float32 rollingResistance, float32 deltaTime ) const
    {
        const float32 clampedSin = MathUtil::clamp( slopeSin, -1.0f, 1.0f );
        const float32 cosSlope   = MathUtil::sqrt( MathUtil::max( 0.0f, 1.0f - clampedSin * clampedSin ) );
        // 경사 중력(오르막이면 뒤로) − 공기 저항(속도², 질량으로 나눔).
        const float32 acceleration = -_gravity * _slopeScale * clampedSin - _airDrag * speed * MathUtil::abs( speed ) / _mass;
        // 구름 저항은 움직임을 거스르고 한 걸음 안에서 속도의 부호를 뒤집지 않는다 — 멈췄으면 경사가 이길 때까지 붙잡는다(정지 마찰).
        const float32 rollingDeceleration = MathUtil::max( 0.0f, rollingResistance ) * _rollingResistanceScale * _gravity * cosSlope;
        const float32 speedAfter          = speed + acceleration * deltaTime;
        if ( deltaTime > 0.0f && MathUtil::abs( speedAfter ) <= rollingDeceleration * deltaTime )
            return -speed / deltaTime;
        return acceleration - ( speedAfter > 0.0f ? 1.0f : ( speedAfter < 0.0f ? -1.0f : 0.0f ) ) * rollingDeceleration;
    }
} // namespace sw
