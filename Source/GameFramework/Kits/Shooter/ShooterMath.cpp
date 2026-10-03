#include "pch.h"

#include "GameFramework/Kits/Shooter/ShooterMath.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    namespace
    {
        struct ShooterMathInternal
        {
            static constexpr float32 kPi = 3.14159265358979f;
        };
    } // namespace
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // ShooterRandom
    // ------------------------------------------------------------------------------
    ShooterRandom::ShooterRandom( uint32 seed )
        : _state{ seed != 0 ? seed : 0x9E3779B9u }
    {
    }

    void ShooterRandom::setSeed( uint32 seed )
    {
        _state = seed != 0 ? seed : 0x9E3779B9u; // 0 은 xorshift 의 고정점이다
    }

    uint32 ShooterRandom::nextUint()
    {
        uint32 value = _state;
        value ^= value << 13;
        value ^= value >> 17;
        value ^= value << 5;
        _state = value;
        return value;
    }

    float32 ShooterRandom::nextFloat()
    {
        return static_cast<float32>( nextUint() >> 8 ) * ( 1.0f / 16777216.0f );
    }

    float32 ShooterRandom::nextRange( float32 minValue, float32 maxValue )
    {
        return minValue + ( maxValue - minValue ) * nextFloat();
    }

    // ------------------------------------------------------------------------------
    // ShooterMath
    // ------------------------------------------------------------------------------
    bool ShooterMath::intersectSphere( const ShooterRay& ray, const float3& center, float32 radius, float32 maxDistance, float32& outDistance )
    {
        const float3  toCenter     = center - ray._origin;
        const float32 radiusSquare = radius * radius;
        if ( toCenter.getLengthSquared() <= radiusSquare )
        {
            outDistance = 0.0f;
            return true;
        }
        const float32 projection = toCenter.dot( ray._direction );
        if ( projection < 0.0f )
            return false;
        const float32 closestSquare = toCenter.getLengthSquared() - projection * projection;
        if ( closestSquare > radiusSquare )
            return false;
        const float32 distance = projection - MathUtil::sqrt( radiusSquare - closestSquare );
        if ( distance > maxDistance )
            return false;
        outDistance = distance;
        return true;
    }

    bool ShooterMath::intersectAabb( const ShooterRay& ray, const float3& boxMin, const float3& boxMax, float32 maxDistance, float32& outDistance )
    {
        const float32 arrOrigin[3]    = { ray._origin._x, ray._origin._y, ray._origin._z };
        const float32 arrDirection[3] = { ray._direction._x, ray._direction._y, ray._direction._z };
        const float32 arrMin[3]       = { boxMin._x, boxMin._y, boxMin._z };
        const float32 arrMax[3]       = { boxMax._x, boxMax._y, boxMax._z };
        float32       nearDistance    = 0.0f;
        float32       farDistance     = maxDistance;
        for ( int32 axisIndex = 0; axisIndex < 3; ++axisIndex )
        {
            if ( MathUtil::abs( arrDirection[axisIndex] ) < 1.0e-8f )
            {
                // 이 축과 나란하다 — 슬랩 밖이면 만나지 않는다.
                if ( arrOrigin[axisIndex] < arrMin[axisIndex] || arrOrigin[axisIndex] > arrMax[axisIndex] )
                    return false;
                continue;
            }
            const float32 inverse  = 1.0f / arrDirection[axisIndex];
            float32       entering = ( arrMin[axisIndex] - arrOrigin[axisIndex] ) * inverse;
            float32       leaving  = ( arrMax[axisIndex] - arrOrigin[axisIndex] ) * inverse;
            if ( entering > leaving )
            {
                const float32 swapped = entering;
                entering              = leaving;
                leaving               = swapped;
            }
            nearDistance = MathUtil::max( nearDistance, entering );
            farDistance  = MathUtil::min( farDistance, leaving );
            if ( nearDistance > farDistance )
                return false;
        }
        outDistance = nearDistance;
        return true;
    }

    float3 ShooterMath::computeLookDirection( float32 yaw, float32 pitch )
    {
        const float32 cosPitch = MathUtil::cos( pitch );
        return float3{ MathUtil::sin( yaw ) * cosPitch, MathUtil::sin( pitch ), MathUtil::cos( yaw ) * cosPitch };
    }

    float3 ShooterMath::applySpread( const float3& direction, float32 coneHalfAngle, ShooterRandom& random )
    {
        if ( coneHalfAngle <= 0.0f )
            return direction;

        // 방향에 직교하는 두 축.
        const float3 helper = MathUtil::abs( direction._y ) < 0.99f ? float3{ 0.0f, 1.0f, 0.0f } : float3{ 1.0f, 0.0f, 0.0f };
        float3       axisA  = helper.cross( direction );
        axisA               = axisA * ( 1.0f / MathUtil::max( 1.0e-6f, axisA.getLength() ) );
        const float3 axisB  = direction.cross( axisA );

        const float32 cosMax   = MathUtil::cos( coneHalfAngle );
        const float32 cosTheta = random.nextRange( cosMax, 1.0f );
        const float32 sinTheta = MathUtil::sqrt( MathUtil::max( 0.0f, 1.0f - cosTheta * cosTheta ) );
        const float32 phi      = random.nextRange( 0.0f, 2.0f * ShooterMathInternal::kPi );
        return direction * cosTheta + axisA * ( sinTheta * MathUtil::cos( phi ) ) + axisB * ( sinTheta * MathUtil::sin( phi ) );
    }

    // ------------------------------------------------------------------------------
    // FirstPersonLook
    // ------------------------------------------------------------------------------
    FirstPersonLook::FirstPersonLook()
        : _yaw{ 0.0f }
        , _pitch{ 0.0f }
        , _maxPitch{ 85.0f * ShooterMathInternal::kPi / 180.0f }
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
        const float32 fullTurn = 2.0f * ShooterMathInternal::kPi;
        float32       wrapped  = MathUtil::fmod( yaw + ShooterMathInternal::kPi, fullTurn );
        if ( wrapped < 0.0f )
            wrapped += fullTurn;
        _yaw   = wrapped - ShooterMathInternal::kPi;
        _pitch = MathUtil::clamp( pitch, -_maxPitch, _maxPitch );
    }

    float3 FirstPersonLook::getFlatRight() const
    {
        return float3{ MathUtil::cos( _yaw ), 0.0f, -MathUtil::sin( _yaw ) };
    }
} // namespace sw
