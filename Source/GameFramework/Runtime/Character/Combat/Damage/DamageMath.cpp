#include "pch.h"

#include "GameFramework/Runtime/Character/Combat/Damage/DamageMath.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    float32 DamageMath::applyArmor( float32 damage, float32 flatArmor, float32 percentReduction, float32 minimumDamage )
    {
        if ( damage <= 0.0f )
            return 0.0f;
        const float32 reduced = ( damage - flatArmor ) * ( 1.0f - MathUtil::saturate( percentReduction ) );
        return MathUtil::max( MathUtil::min( minimumDamage, damage ), reduced );
    }
} // namespace sw
