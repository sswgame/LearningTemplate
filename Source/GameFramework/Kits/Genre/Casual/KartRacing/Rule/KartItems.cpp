#include "pch.h"

#include "GameFramework/Kits/Genre/Casual/KartRacing/Rule/KartItems.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Module/ModuleSharedAssetCache.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Runtime/Foundation/GameData/GameDataXML.h"
#include "GameFramework/Runtime/Foundation/GameData/XMLCatalogTable.h"
#include "GameFramework/Runtime/Foundation/Random/GameRandom.h"
#include "GameFramework/Runtime/Gameplay/Inventory/ItemStackList.h"

namespace sw
{
    namespace
    {
        struct KartItemsInternal
        {
        };
    } // namespace
} // namespace sw

namespace sw
{
    KartItemCatalog::KartItemCatalog()
        : _lootCatalog{}
        , _listRankTable{}
        , _referencePlaceCount{ 0 }
    {
    }

    void KartItemCatalog::addRankTable( int32 fromPlace, int32 toPlace, const vector<LootEntry>& listEntry )
    {
        KartRankTable rank;
        rank._fromPlace = MathUtil::max( 1, MathUtil::min( fromPlace, toPlace ) );
        rank._toPlace   = MathUtil::max( rank._fromPlace, MathUtil::max( fromPlace, toPlace ) );
        StringBuilder<constant::buffer::kMax64> tableName;
        tableName.append( "kartRank" ).append( rank._fromPlace ).append( '_' ).append( rank._toPlace );
        rank._tableID = hashed_string( tableName.c_str() );

        LootTableDef table;
        table._id        = rank._tableID;
        table._listEntry = listEntry;
        _lootCatalog.add( table );
        for ( KartRankTable& existing : _listRankTable )
        {
            if ( existing._tableID == rank._tableID )
                return;
        }
        _listRankTable.push_back( rank );
    }

    const KartRankTable* KartItemCatalog::findRankTable( int32 place, int32 racerCount ) const
    {
        if ( _listRankTable.empty() )
            return nullptr;
        int32      tablePlace               = place;
        const bool bShouldScaleByRacerCount = _referencePlaceCount > 1 && racerCount > 1 && racerCount != _referencePlaceCount;
        if ( bShouldScaleByRacerCount )
        {
            // 인원을 테이블의 기준 인원으로 늘린다(꼴찌는 항상 꼴찌 테이블을 쓴다).
            const float32 ratio = static_cast<float32>( place - 1 ) / static_cast<float32>( racerCount - 1 );
            tablePlace          = 1 + static_cast<int32>( MathUtil::round( ratio * static_cast<float32>( _referencePlaceCount - 1 ) ) );
        }
        const KartRankTable* pNearest    = nullptr;
        int32                nearestDiff = MathUtil::kMaxInt32;
        for ( const KartRankTable& rank : _listRankTable )
        {
            if ( rank._fromPlace <= tablePlace && tablePlace <= rank._toPlace )
                return &rank;
            const int32 diff = tablePlace < rank._fromPlace ? rank._fromPlace - tablePlace : tablePlace - rank._toPlace;
            if ( diff < nearestDiff )
            {
                nearestDiff = diff;
                pNearest    = &rank;
            }
        }
        return pNearest;
    }

    const KartItemDef* KartItemCatalog::rollItem( int32 place, int32 racerCount, GameRandom& random ) const
    {
        const KartRankTable* pRank = findRankTable( place, racerCount );
        if ( pRank == nullptr )
            return nullptr;
        ItemStackList items;
        if ( _lootCatalog.roll( pRank->_tableID, random, items ) == false || items.isEmpty() )
            return nullptr;
        vector<hashed_string> listItem;
        items.getItemIDs( listItem );
        return find( listItem.front() );
    }

    float32 KartItemCatalog::computeItemChance( int32 place, int32 racerCount, const hashed_string& itemID ) const
    {
        const KartRankTable* pRank = findRankTable( place, racerCount );
        return pRank != nullptr ? _lootCatalog.computeDropChance( pRank->_tableID, itemID ) : 0.0f;
    }

    uint32 KartItemCatalog::loadRoot( const XMLCatalogRoot& root, string_view sourceName )
    {
        // 칸의 기본값은 Def 의 초기값, 범위는 `PROPERTY( Min · Max )` 다. 범위 밖 · 모르는 속성 · 깨진 값은 그 원소를 넣지 않고 오류다(readProperties 가 남긴다).
        KartItemsRootDef rootDef;
        if ( root.readProperties( rootDef, sourceName ) )
            _referencePlaceCount = rootDef._referencePlaceCount;
        // 순위 표는 아이템을 가리키므로 아이템을 모두 읽은 뒤에 읽는다(적힌 순서와 상관없게).
        using Element                          = XMLCatalogElement<KartItemCatalog>;
        static constexpr Element kArrElement[] = {
            Element::of<&KartItemCatalog::loadItem>( "Item" ),
            Element::of<&KartItemCatalog::loadRankTable>( "RankTable" ).uncounted(),
        };
        return XMLCatalogReadResult::readInTableOrder( *this, root, sourceName, kArrElement )._loadedCount;
    }

    bool KartItemCatalog::loadItem( const XMLNode& node, string_view sourceName )
    {
        KartItemDef def;
        if ( XMLFields::readProperties( node, def, sourceName ) == false )
            return false; // 오류는 readProperties 가 남겼고, 그 아이템만 빠진다
        if ( def.isProjectile() && def._projectileID.empty() )
        {
            SW_LOG_ERROR_ID( "KartItems.ItemNeedsProjectile", "%#: <Item id=\"%#\"> flies or is dropped and needs projectile=\"...\" (a ProjectileCatalog id) - skipped", sourceName, def._id.c_str() );
            return false;
        }
        add( def );
        return true;
    }

    bool KartItemCatalog::loadRankTable( const XMLNode& node, string_view sourceName )
    {
        KartRankRangeDef  range;
        bool              bValid = XMLFields::readProperties( node, range, sourceName );
        vector<LootEntry> listEntry;
        for ( XMLNode entryNode = node.findChild( "Entry" ); entryNode; entryNode = entryNode.findNextSibling( "Entry" ) )
        {
            KartRankEntryDef entry;
            if ( XMLFields::readProperties( entryNode, entry, sourceName ) == false )
            {
                bValid = false;
                continue;
            }
            if ( find( entry._itemID ) == nullptr )
            {
                SW_LOG_ERROR_ID( "KartItems.RankTableEntryNames", "%#: rank table entry names an unknown item '%#'", sourceName, entry._itemID.c_str() );
                bValid = false;
                continue;
            }
            LootEntry loot;
            loot._itemID = entry._itemID;
            loot._weight = entry._weight;
            listEntry.push_back( loot );
        }
        if ( listEntry.empty() )
        {
            SW_LOG_ERROR_ID( "KartItems.RankTableNeedsEntryChildren", "%#: <RankTable from=\"%#\"> needs <Entry> children", sourceName, range._fromPlace );
            bValid = false;
        }
        if ( bValid )
            addRankTable( range._fromPlace, range._toPlace, listEntry );
        return bValid;
    }
} // namespace sw

namespace sw
{
    /** 쓰는 쪽은 공유 캐시의 한 벌을 받는다(`SharedAssetCache<KartItemCatalog>::acquire`). 경로 규칙 없이 접미사로 가르는 종류라 카탈로그 편집기에는 나오지 않는다. */
    SW_MODULE_SHARED_ASSET_CACHE( KartItemCatalog, KartItemCatalog, SW_KIT_API, KartItemCatalog::kAssetType.withLoader( AssetTypeLoader::of<KartItemCatalog>( MemoryTag::Game ) ) );
} // namespace sw
