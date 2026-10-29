#include "pch.h"

#include "GameFramework/Kits/Genre/Action/BattleRoyale/Rule/BattleRoyaleLoot.h"

#include "Core/Container/VectorUtil.h"

#include "GameFramework/Base/Foundation/Utility/Random/GameRandom.h"
#include "GameFramework/Base/Gameplay/Inventory/ItemStackList.h"
#include "GameFramework/Base/Gameplay/Inventory/LootTable.h"
#include "GameFramework/Kits/Genre/Action/BattleRoyale/Catalog/BattleRoyaleCatalog.h"

#include <algorithm>

namespace sw
{
    int32 BattleRoyaleLootPlacement::placeMapLoot( const BattleRoyaleCatalog& catalog, const LootCatalog& lootCatalog, const vector<BattleRoyaleLootSpot>& listSpot, uint32 seed,
                                                   vector<BattleRoyaleGroundItem>& outListItem )
    {
        outListItem.clear();
        for ( size_t spotIndex = 0; spotIndex < listSpot.size(); ++spotIndex )
        {
            const BattleRoyaleLootSpot&    spot = listSpot[spotIndex];
            const BattleRoyaleLootSpotDef* pDef = catalog.findLootSpot( spot._kind );
            if ( pDef == nullptr )
                continue;
            GameRandom random{ GameHash::hashCoord( static_cast<int32>( spotIndex ), 0x4252, seed ) };
            if ( random.nextChance( pDef->_chance ) == false )
                continue;
            const int32 rollCount = random.nextInt( pDef->_minRolls, pDef->_maxRolls );
            (void)rollTableAt( lootCatalog, pDef->_tableID, spot._position, static_cast<int32>( spotIndex ), rollCount, random, outListItem );
        }
        return static_cast<int32>( outListItem.size() );
    }

    int32 BattleRoyaleLootPlacement::rollTableAt( const LootCatalog& lootCatalog, const hashed_string& tableID, const float2& position, int32 spotIndex, int32 rollCount,
                                                  GameRandom& random, vector<BattleRoyaleGroundItem>& outListItem )
    {
        ItemStackList items;
        for ( int32 rollIndex = 0; rollIndex < rollCount; ++rollIndex )
        {
            (void)lootCatalog.roll( tableID, random, items );
        }
        vector<hashed_string> listItemID;
        items.getItemIDs( listItemID );
        VectorUtil::sort( listItemID, HashedStringLexicalLess{} );
        for ( const hashed_string& itemID : listItemID )
        {
            BattleRoyaleGroundItem item;
            item._position  = position;
            item._itemID    = itemID;
            item._count     = items.getItemCount( itemID );
            item._spotIndex = spotIndex;
            outListItem.push_back( item );
        }
        return static_cast<int32>( listItemID.size() );
    }
} // namespace sw
