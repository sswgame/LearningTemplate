#include "pch.h"

#include "GameFramework/Kits/Casual/KartRacing/KartItems.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Utility/Xml/XmlDocument.h"

#include "GameFramework/Base/Foundation/Data/GameDataXml.h"
#include "GameFramework/Base/Foundation/Utility/Random/GameRandom.h"
#include "GameFramework/Base/Gameplay/Inventory/ItemStackList.h"

namespace sw
{
    SW_LOG_CALLER( "KartItems" );

    namespace
    {
        struct KartItemsInternal
        {
            /** @brief 열거자 이름표 한 줄입니다. */
            struct KindName
            {
                KartItemKind _kind;
                const utf8*  _pName;
            };

            static constexpr KindName kArrKindName[] = {
                {     KartItemKind::Banana,      "Banana"},
                { KartItemKind::GreenShell,  "GreenShell"},
                {   KartItemKind::RedShell,    "RedShell"},
                {    KartItemKind::Booster,     "Booster"},
                {     KartItemKind::Shield,      "Shield"},
                {KartItemKind::LeaderShell, "LeaderShell"},
            };
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool parseKartItemKind( string_view text, KartItemKind& outKind )
    {
        for ( const KartItemsInternal::KindName& entry : KartItemsInternal::kArrKindName )
        {
            if ( StringUtil::equals( text, string_view( entry._pName ), true ) )
            {
                outKind = entry._kind;
                return true;
            }
        }
        return false;
    }

    const utf8* toString( KartItemKind kind )
    {
        for ( const KartItemsInternal::KindName& entry : KartItemsInternal::kArrKindName )
        {
            if ( entry._kind == kind )
                return entry._pName;
        }
        return "Unknown";
    }

    KartItemCatalog::KartItemCatalog()
        : _catalog{}
        , _lootCatalog{}
        , _listRankTable{}
        , _referencePlaceCount{ 0 }
    {
    }

    void KartItemCatalog::addRankTable( int32 fromPlace, int32 toPlace, const vector<LootEntry>& listEntry )
    {
        KartRankTable rank;
        rank._fromPlace = MathUtil::max( 1, MathUtil::min( fromPlace, toPlace ) );
        rank._toPlace   = MathUtil::max( rank._fromPlace, MathUtil::max( fromPlace, toPlace ) );
        StringBuilder<constant::kMaxBuffer64> tableName;
        tableName.append( "kartRank" ).append( rank._fromPlace ).append( '_' ).append( rank._toPlace );
        rank._tableId = hashed_string( tableName.c_str() );

        LootTableDef table;
        table._id        = rank._tableId;
        table._listEntry = listEntry;
        _lootCatalog.addTable( table );
        for ( KartRankTable& existing : _listRankTable )
        {
            if ( existing._tableId == rank._tableId )
                return;
        }
        _listRankTable.push_back( rank );
    }

    const KartRankTable* KartItemCatalog::findRankTable( int32 place, int32 racerCount ) const
    {
        if ( _listRankTable.empty() )
            return nullptr;
        int32 tablePlace = place;
        if ( _referencePlaceCount > 1 && racerCount > 1 && racerCount != _referencePlaceCount )
        {
            // 인원을 표의 기준 인원으로 늘린다(꼴찌는 늘 꼴찌 표).
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
        if ( _lootCatalog.roll( pRank->_tableId, random, items ) == false || items.isEmpty() )
            return nullptr;
        vector<hashed_string> listItem;
        items.getItemIds( listItem );
        return findItem( listItem.front() );
    }

    float32 KartItemCatalog::computeItemChance( int32 place, int32 racerCount, const hashed_string& itemId ) const
    {
        const KartRankTable* pRank = findRankTable( place, racerCount );
        return pRank != nullptr ? _lootCatalog.computeDropChance( pRank->_tableId, itemId ) : 0.0f;
    }

    uint32 KartItemCatalog::loadRoot( const XmlNode& root, string_view sourceName )
    {
        _referencePlaceCount = root.getAttributeInt( "places", _referencePlaceCount );
        uint32 loadedCount   = 0;
        for ( XmlNode itemNode = root.findChild( "Item" ); itemNode; itemNode = itemNode.findNextSibling( "Item" ) )
        {
            const utf8* pId = GameDataXml::findRequiredId( itemNode, sourceName );
            if ( pId == nullptr )
                continue;
            KartItemDef def;
            def._id = hashed_string( pId );
            if ( parseKartItemKind( itemNode.getAttributeText( "kind" ), def._kind ) == false )
            {
                SW_LOG_WARNING( "%#: item '%#' has an unknown kind '%#' - skipped", sourceName, pId, itemNode.getAttributeText( "kind" ) );
                continue;
            }
            def._speed          = itemNode.getAttributeFloat( "speed", def._speed );
            def._lifetime       = itemNode.getAttributeFloat( "lifetime", def._lifetime );
            def._radius         = itemNode.getAttributeFloat( "radius", def._radius );
            def._turnRate       = itemNode.getAttributeFloat( "turnRate", def._turnRate );
            def._spinTime       = itemNode.getAttributeFloat( "spin", def._spinTime );
            def._hitSpeedScale  = itemNode.getAttributeFloat( "hitSpeedScale", def._hitSpeedScale );
            def._boostTime      = itemNode.getAttributeFloat( "boost", def._boostTime );
            def._shieldTime     = itemNode.getAttributeFloat( "shield", def._shieldTime );
            def._blastRadius    = itemNode.getAttributeFloat( "blastRadius", def._blastRadius );
            def._bIgnoresShield = itemNode.getAttributeBool( "ignoresShield", false ) ? SW_TRUE : SW_FALSE;
            addItem( def );
            ++loadedCount;
        }
        for ( XmlNode rankNode = root.findChild( "RankTable" ); rankNode; rankNode = rankNode.findNextSibling( "RankTable" ) )
        {
            vector<LootEntry> listEntry;
            for ( XmlNode entryNode = rankNode.findChild( "Entry" ); entryNode; entryNode = entryNode.findNextSibling( "Entry" ) )
            {
                const utf8* pItem = entryNode.findAttribute( "item" );
                if ( pItem == nullptr || findItem( hashed_string( pItem ) ) == nullptr )
                {
                    SW_LOG_WARNING( "%#: rank table entry names an unknown item '%#' - skipped", sourceName, pItem != nullptr ? pItem : "" );
                    continue;
                }
                LootEntry entry;
                entry._itemId = hashed_string( pItem );
                entry._weight = entryNode.getAttributeFloat( "weight", 1.0f );
                listEntry.push_back( entry );
            }
            if ( listEntry.empty() )
            {
                SW_LOG_WARNING( "%#: <RankTable> without entries - skipped", sourceName );
                continue;
            }
            const int32 fromPlace = rankNode.getAttributeInt( "from", 1 );
            addRankTable( fromPlace, rankNode.getAttributeInt( "to", fromPlace ), listEntry );
        }
        if ( loadedCount == 0 )
            SW_LOG_WARNING( "%#: no <Item> entries", sourceName );
        return loadedCount;
    }
} // namespace sw
