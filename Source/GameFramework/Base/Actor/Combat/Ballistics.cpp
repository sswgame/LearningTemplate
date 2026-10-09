#include "pch.h"

#include "GameFramework/Base/Actor/Combat/Ballistics.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Physics/PhysicsSystem.h"

namespace sw
{
    Projectile Ballistics::launch( const float3& origin, const float3& direction, float32 speed, float32 gravityScale, float32 maxAge )
    {
        Projectile projectile;
        projectile._position         = origin;
        projectile._previousPosition = origin;
        projectile._velocity         = direction * speed;
        projectile._gravityScale     = gravityScale;
        projectile._maxAge           = maxAge;
        return projectile;
    }

    void Ballistics::step( Projectile& projectile, float32 deltaTime )
    {
        if ( deltaTime <= 0.0f )
            return;
        projectile._previousPosition = projectile._position;
        if ( projectile._drag > 0.0f )
            projectile._velocity = projectile._velocity * MathUtil::max( 0.0f, 1.0f - projectile._drag * deltaTime );
        projectile._velocity._y -= PhysicsSystem::getConfiguredGravityMagnitude() * projectile._gravityScale * deltaTime;
        projectile._position = projectile._position + projectile._velocity * deltaTime;
        projectile._age += deltaTime;
    }

    bool Ballistics::computeLaunchDirection( const float3& from, const float3& to, float32 speed, float32 gravityScale, bool bHighArc, float3& outDirection )
    {
        const float3  delta      = to - from;
        const float32 horizontal = MathUtil::sqrt( delta._x * delta._x + delta._z * delta._z );
        const float32 gravity    = PhysicsSystem::getConfiguredGravityMagnitude() * gravityScale;
        if ( speed <= 0.0f )
            return false;
        if ( gravity <= 1.0e-6f || horizontal < 1.0e-6f )
        {
            const float32 length = delta.getLength();
            if ( length < 1.0e-6f || ( gravity > 1.0e-6f && delta._y > 0.0f && speed * speed < 2.0f * gravity * delta._y ) )
                return false;
            outDirection = delta * ( 1.0f / length );
            return true;
        }
        // tan θ = (v² ± √(v⁴ − g(g x² + 2 y v²))) / (g x)
        const float32 speedSquared = speed * speed;
        const float32 discriminant = speedSquared * speedSquared - gravity * ( gravity * horizontal * horizontal + 2.0f * delta._y * speedSquared );
        if ( discriminant < 0.0f )
            return false;
        const float32 root     = MathUtil::sqrt( discriminant );
        const float32 tanTheta = ( speedSquared + ( bHighArc ? root : -root ) ) / ( gravity * horizontal );
        const float32 cosTheta = 1.0f / MathUtil::sqrt( 1.0f + tanTheta * tanTheta );
        const float32 sinTheta = tanTheta * cosTheta;
        outDirection           = float3{ delta._x / horizontal * cosTheta, sinTheta, delta._z / horizontal * cosTheta };
        return true;
    }

    bool Ballistics::computeInterceptPoint( const float3& shooter, const float3& target, const float3& targetVelocity, float32 speed, float3& outPoint )
    {
        const float3  offset = target - shooter;
        const float32 a      = targetVelocity.getLengthSquared() - speed * speed;
        const float32 b      = 2.0f * ( offset._x * targetVelocity._x + offset._y * targetVelocity._y + offset._z * targetVelocity._z );
        const float32 c      = offset.getLengthSquared();
        float32       time   = -1.0f;
        if ( MathUtil::abs( a ) < 1.0e-6f )
        {
            if ( MathUtil::abs( b ) > 1.0e-6f )
                time = -c / b;
        }
        else
        {
            const float32 discriminant = b * b - 4.0f * a * c;
            if ( discriminant >= 0.0f )
            {
                const float32 root  = MathUtil::sqrt( discriminant );
                const float32 timeA = ( -b - root ) / ( 2.0f * a );
                const float32 timeB = ( -b + root ) / ( 2.0f * a );
                const float32 low   = MathUtil::min( timeA, timeB );
                const float32 high  = MathUtil::max( timeA, timeB );
                time                = low > 0.0f ? low : high;
            }
        }
        if ( time <= 0.0f )
            return false;
        outPoint = target + targetVelocity * time;
        return true;
    }

    float32 Ballistics::computeDrop( float32 distance, float32 speed, float32 gravityScale )
    {
        if ( speed <= 0.0f )
            return 0.0f;
        const float32 time = distance / speed;
        return 0.5f * PhysicsSystem::getConfiguredGravityMagnitude() * gravityScale * time * time;
    }
} // namespace sw
