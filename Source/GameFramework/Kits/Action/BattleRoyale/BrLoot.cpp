#include "pch.h"

#include "GameFramework/Kits/Action/BattleRoyale/BrLoot.h"

#include "GameFramework/Base/Foundation/Utility/GameRandom.h"
#include "GameFramework/Base/Gameplay/Inventory/ItemStackList.h"
#include "GameFramework/Base/Gameplay/Inventory/LootTable.h"
#include "GameFramework/Kits/Action/BattleRoyale/BrCatalog.h"

#include <algorithm>

namespace sw
{
    int32 BrLootPlacement::placeMapLoot( const BrCatalog& catalog, const LootCatalog& lootCatalog, const vector<BrLootSpot>& listSpot, uint32 seed,
                                         vector<BrGroundItem>& outListItem )
    {
        outListItem.clear();
        for ( size_t spotIndex = 0; spotIndex < listSpot.size(); ++spotIndex )
        {
            const BrLootSpot&    spot = listSpot[spotIndex];
            const BrLootSpotDef* pDef = catalog.findLootSpot( spot._kind );
            if ( pDef == nullptr )
                continue;
            GameRandom random{ GameHash::hashCoord( static_cast<int32>( spotIndex ), 0x4252, seed ) };
            if ( random.nextChance( pDef->_chance ) == false )
                continue;
            const int32 rollCount = random.nextInt( pDef->_minRolls, pDef->_maxRolls );
            (void)rollTableAt( lootCatalog, pDef->_tableId, spot._position, static_cast<int32>( spotIndex ), rollCount, random, outListItem );
        }
        return static_cast<int32>( outListItem.size() );
    }

    int32 BrLootPlacement::rollTableAt( const LootCatalog& lootCatalog, const hashed_string& tableId, const float2& position, int32 spotIndex, int32 rollCount,
                                        GameRandom& random, vector<BrGroundItem>& outListItem )
    {
        ItemStackList items;
        for ( int32 rollIndex = 0; rollIndex < rollCount; ++rollIndex )
        {
            (void)lootCatalog.roll( tableId, random, items );
        }
        vector<hashed_string> listItemId;
        items.getItemIds( listItemId );
        std::sort( listItemId.begin(), listItemId.end(), HashedStringLexicalLess{} );
        for ( const hashed_string& itemId : listItemId )
        {
            BrGroundItem item;
            item._position  = position;
            item._itemId    = itemId;
            item._count     = items.getItemCount( itemId );
            item._spotIndex = spotIndex;
            outListItem.push_back( item );
        }
        return static_cast<int32>( listItemId.size() );
    }
} // namespace sw
