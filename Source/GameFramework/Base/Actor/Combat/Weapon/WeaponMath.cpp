#include "pch.h"

#include "GameFramework/Base/Actor/Combat/Weapon/WeaponMath.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    namespace
    {
        struct WeaponMathInternal
        {
        };
    } // namespace
} // namespace sw

namespace sw
{
    float3 WeaponMath::applySpread( const float3& direction, float32 coneHalfAngle, GameRandom& random )
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
        const float32 phi      = random.nextRange( 0.0f, 2.0f * MathUtil::kPi );
        return direction * cosTheta + axisA * ( sinTheta * MathUtil::cos( phi ) ) + axisB * ( sinTheta * MathUtil::sin( phi ) );
    }
} // namespace sw
