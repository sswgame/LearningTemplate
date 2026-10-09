#include "pch.h"

#include "GameFramework/Kits/Rpg/OpenWorldWestern/WesternHunting.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/Base/Gameplay/Inventory/ItemStackList.h"
#include "GameFramework/Base/Gameplay/Inventory/LootTable.h"
#include "GameFramework/Kits/Rpg/OpenWorldWestern/WesternCatalog.h"

namespace sw
{
    int32 WesternHunting::computePeltStars( const WesternCatalog& catalog, const WesternKill& kill )
    {
        const WesternAnimalDef* pAnimal = catalog.findAnimal( kill._animalId );
        if ( pAnimal == nullptr )
            return 0;
        int32                       stars   = pAnimal->_quality;
        const WesternHuntWeaponDef* pWeapon = catalog.findHuntWeapon( kill._weaponId );
        if ( pWeapon != nullptr )
        {
            if ( pWeapon->_bRuinsPelt != SW_FALSE )
                return 0;
            const uint8 sizeBit = static_cast<uint8>( 1u << static_cast<uint32>( pAnimal->_size ) );
            if ( ( pWeapon->_sizeMask & sizeBit ) == 0 )
                --stars;
        }
        const WesternHitZoneDef* pZone = catalog.findHitZone( kill._zoneId );
        if ( pZone != nullptr )
            stars -= pZone->_penalty;
        stars -= MathUtil::max( 0, kill._hitCount - 1 ) * catalog.getExtraHitPenalty();
        return MathUtil::clamp( stars, 0, pAnimal->_quality );
    }

    WesternCarcass WesternHunting::makeCarcass( const WesternCatalog& catalog, const WesternKill& kill )
    {
        WesternCarcass carcass;
        carcass._animalId = kill._animalId;
        carcass._stars    = computePeltStars( catalog, kill );
        return carcass;
    }

    void WesternHunting::ageCarcass( WesternCarcass& inoutCarcass, float32 gameHours ) { inoutCarcass._ageHours += MathUtil::max( 0.0f, gameHours ); }

    int32 WesternHunting::computeCarcassStars( const WesternCatalog& catalog, const WesternCarcass& carcass )
    {
        const WesternAnimalDef* pAnimal = catalog.findAnimal( carcass._animalId );
        if ( pAnimal == nullptr || carcass._ageHours >= pAnimal->_decayHours )
            return 0;
        const int32 spoiled = carcass._ageHours >= pAnimal->_decayHours * 0.5f ? 1 : 0;
        return MathUtil::max( 0, carcass._stars - spoiled );
    }

    bool WesternHunting::isRotten( const WesternCatalog& catalog, const WesternCarcass& carcass )
    {
        const WesternAnimalDef* pAnimal = catalog.findAnimal( carcass._animalId );
        return pAnimal == nullptr || carcass._ageHours >= pAnimal->_decayHours;
    }

    bool WesternHunting::skin( const WesternCatalog& catalog, WesternCarcass& inoutCarcass, const LootCatalog* pLoot, GameRandom& random, WesternPelt& outPelt,
                               ItemStackList& outItems )
    {
        if ( inoutCarcass._bSkinned != SW_FALSE || isRotten( catalog, inoutCarcass ) )
            return false;
        outPelt._animalId               = inoutCarcass._animalId;
        outPelt._stars                  = computeCarcassStars( catalog, inoutCarcass );
        inoutCarcass._bSkinned          = SW_TRUE;
        const WesternAnimalDef* pAnimal = catalog.findAnimal( inoutCarcass._animalId );
        if ( pLoot != nullptr && pAnimal != nullptr && pAnimal->_lootTable.empty() == false )
            (void)pLoot->roll( pAnimal->_lootTable, random, outItems );
        return true;
    }

    int32 WesternHunting::computePeltPrice( const WesternCatalog& catalog, const WesternPelt& pelt )
    {
        const WesternAnimalDef* pAnimal = catalog.findAnimal( pelt._animalId );
        if ( pAnimal == nullptr )
            return 0;
        return static_cast<int32>( MathUtil::round( pAnimal->_peltPrice * catalog.getGradeScale( pelt._stars ) * 100.0f ) );
    }

    int32 WesternHunting::computeCarcassPrice( const WesternCatalog& catalog, const WesternCarcass& carcass )
    {
        const WesternAnimalDef* pAnimal = catalog.findAnimal( carcass._animalId );
        if ( pAnimal == nullptr || isRotten( catalog, carcass ) )
            return 0;
        // 사체 값은 고기 몫도 있어 등급 0 이어도 1 성 값을 준다. 벗긴 사체는 절반.
        const int32   stars = MathUtil::max( 1, computeCarcassStars( catalog, carcass ) );
        const float32 scale = carcass._bSkinned != SW_FALSE ? 0.5f : 1.0f;
        return static_cast<int32>( MathUtil::round( pAnimal->_carcassPrice * catalog.getGradeScale( stars ) * scale * 100.0f ) );
    }
} // namespace sw
