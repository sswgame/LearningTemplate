#include "pch.h"

#include "GameFramework/Kits/Genre/Casual/KartRacing/Rule/KartItems.h"

#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Base/Foundation/GameData/GameDataXML.h"
#include "GameFramework/Base/Foundation/Utility/Random/GameRandom.h"
#include "GameFramework/Base/Gameplay/Inventory/ItemStackList.h"

namespace sw
{
    SW_LOG_CALLER( "KartItems" );

    namespace
    {
        struct KartItemsInternal
        {
            /** @brief 열거자 이름 한 줄을 반환합니다. */
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
            if ( StringUtil::equals( text, string_view( entry._pName ) ) )
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
        StringBuilder<constant::kMaxBuffer64> tableName;
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

    uint32 KartItemCatalog::loadRoot( const XMLNode& root, string_view sourceName )
    {
        // 칸의 기본값은 Def 의 초기값, 범위는 `PROPERTY( Min · Max )` 다. 범위 밖 · 모르는 속성 · 깨진 값은 그 원소를 넣지 않고 오류다(readFields 가 남긴다).
        KartItemsRootDef rootDef;
        if ( GameDataXML::readFields( root, rootDef, sourceName ) )
            _referencePlaceCount = rootDef._referencePlaceCount;
        uint32 loadedCount = 0;
        // 순위 표는 아이템을 가리키므로 아이템을 모두 읽은 뒤에 읽는다(적힌 순서와 상관없게).
        for ( XMLNode node = root.findChild(); node; node = node.findNextSibling() )
        {
            const string_view name = node.getName() != nullptr ? string_view( node.getName() ) : string_view{};
            if ( name == "RankTable" )
                continue;
            if ( name != "Item" )
            {
                SW_LOG_ERROR( "%#: unknown element <%#> - skipped", sourceName, name );
                continue;
            }
            KartItemDef def;
            if ( GameDataXML::readFields( node, def, sourceName ) == false )
                continue; // 오류는 readFields 가 남겼고, 그 아이템만 빠진다
            add( def );
            ++loadedCount;
        }
        for ( XMLNode node = root.findChild( "RankTable" ); node; node = node.findNextSibling( "RankTable" ) )
        {
            if ( loadRankTable( node, sourceName ) == false )
                SW_LOG_ERROR( "%#: a <RankTable> was not loaded (see above)", sourceName );
        }
        if ( loadedCount == 0 )
            SW_LOG_ERROR( "%#: no <Item> entries", sourceName );
        return loadedCount;
    }

    bool KartItemCatalog::loadRankTable( const XMLNode& node, string_view sourceName )
    {
        KartRankRangeDef  range;
        bool              bValid = GameDataXML::readFields( node, range, sourceName );
        vector<LootEntry> listEntry;
        for ( XMLNode entryNode = node.findChild( "Entry" ); entryNode; entryNode = entryNode.findNextSibling( "Entry" ) )
        {
            KartRankEntryDef entry;
            if ( GameDataXML::readFields( entryNode, entry, sourceName ) == false )
            {
                bValid = false;
                continue;
            }
            if ( find( entry._itemID ) == nullptr )
            {
                SW_LOG_ERROR( "%#: rank table entry names an unknown item '%#'", sourceName, entry._itemID.c_str() );
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
            SW_LOG_ERROR( "%#: <RankTable from=\"%#\"> needs <Entry> children", sourceName, range._fromPlace );
            bValid = false;
        }
        if ( bValid )
            addRankTable( range._fromPlace, range._toPlace, listEntry );
        return bValid;
    }
} // namespace sw
