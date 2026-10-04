#include "pch.h"

#include "GameFramework/Combat/DamageMath.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/Combat/Weapon.h"

namespace sw
{
    float32 DamageMath::computeFalloffScale( const WeaponDef& weapon, float32 distance )
    {
        if ( weapon._falloffEnd <= 0.0f || distance <= weapon._falloffStart )
            return 1.0f;
        if ( distance >= weapon._falloffEnd || weapon._falloffEnd <= weapon._falloffStart )
            return weapon._falloffMinScale;
        const float32 ratio = ( distance - weapon._falloffStart ) / ( weapon._falloffEnd - weapon._falloffStart );
        return 1.0f + ( weapon._falloffMinScale - 1.0f ) * ratio;
    }

    float32 DamageMath::computeWeaponDamage( const WeaponDef& weapon, float32 distance, bool bHeadshot )
    {
        const float32 damage = weapon._damage * computeFalloffScale( weapon, distance );
        return bHeadshot ? damage * weapon._headshotMultiplier : damage;
    }

    float32 DamageMath::applyArmor( float32 damage, float32 flatArmor, float32 percentReduction, float32 minimumDamage )
    {
        if ( damage <= 0.0f )
            return 0.0f;
        const float32 reduced = ( damage - flatArmor ) * ( 1.0f - MathUtil::saturate( percentReduction ) );
        return MathUtil::max( MathUtil::min( minimumDamage, damage ), reduced );
    }
} // namespace sw
