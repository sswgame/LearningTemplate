#include "pch.h"

#include "GameFramework/Base/Gameplay/Inventory/LootTable.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Base/Foundation/GameData/GameDataXML.h"
#include "GameFramework/Base/Foundation/Utility/Random/GameRandom.h"
#include "GameFramework/Base/Gameplay/Inventory/ItemStackList.h"

namespace sw
{
    SW_LOG_CALLER( "LootTable" );

    namespace
    {
        /**
         * @brief `<Entry>` · `<Always>` 하나를 스키마(`LootEntryRowDef`)로 읽어 @p outEntry 를 채웁니다.
         * @return 모르는 속성 · 범위 밖 값 · `max` < `min` 이면 false 입니다(오류는 이름과 함께 남긴다).
         */
        [[nodiscard]] bool readLootEntry( const XMLNode& node, string_view sourceName, LootEntry& outEntry )
        {
            LootEntryRowDef row;
            if ( GameDataXML::readFields( node, row, sourceName ) == false )
                return false;
            const bool bHasMax = node.findAttribute( "max" ) != nullptr;
            if ( bHasMax && row._max < row._min )
            {
                SW_LOG_ERROR( "%#: <%# max=\"%#\"> is less than min=\"%#\"", sourceName, node.getName(), row._max, row._min );
                return false;
            }
            outEntry._itemID   = row._item.empty() ? hashed_string{} : hashed_string( row._item );
            outEntry._tableID  = row._table.empty() ? hashed_string{} : hashed_string( row._table );
            outEntry._weight   = row._weight;
            outEntry._chance   = row._chance;
            outEntry._minCount = row._min;
            outEntry._maxCount = bHasMax ? row._max : row._min;
            return true;
        }
    } // namespace
} // namespace sw

namespace sw
{
    uint32 LootCatalog::loadRoot( const XMLNode& root, string_view sourceName )
    {
        uint32 loadedCount = 0;
        for ( XMLNode node = root.findChild( "Table" ); node; node = node.findNextSibling( "Table" ) )
        {
            const utf8* pID = XMLReadUtil::findRequiredID( node, sourceName );
            if ( pID == nullptr )
                continue;
            // 기본값 · 범위는 스키마(LootTableRowDef · LootEntryRowDef)가 정한다. 오류가 있는 테이블 · 항목은 넣지 않는다(오류는 readFields 가 남긴다).
            LootTableRowDef row;
            if ( GameDataXML::readFields( node, row, sourceName, { "id" } ) == false )
                continue;
            const bool bHasRollsMax = node.findAttribute( "rollsMax" ) != nullptr;
            if ( bHasRollsMax && row._rollsMax < row._rolls )
            {
                SW_LOG_ERROR( "%#: table '%#' rollsMax=\"%#\" is less than rolls=\"%#\"", sourceName, pID, row._rollsMax, row._rolls );
                continue;
            }
            LootTableDef table;
            table._id         = hashed_string( pID );
            table._minRolls   = row._rolls;
            table._maxRolls   = bHasRollsMax ? row._rollsMax : row._rolls;
            table._noneWeight = row._none;
            for ( XMLNode child = node.findChild( "Entry" ); child; child = child.findNextSibling( "Entry" ) )
            {
                LootEntry entry;
                if ( readLootEntry( child, sourceName, entry ) )
                    table._listEntry.push_back( entry );
            }
            for ( XMLNode child = node.findChild( "Always" ); child; child = child.findNextSibling( "Always" ) )
            {
                LootEntry entry;
                if ( readLootEntry( child, sourceName, entry ) )
                    table._listAlways.push_back( entry );
            }
            add( table );
            ++loadedCount;
        }
        for ( const LootTableDef& table : getAll() )
        {
            for ( const LootEntry& entry : table._listEntry )
            {
                if ( entry._tableID.empty() == false && find( entry._tableID ) == nullptr )
                    SW_LOG_WARNING( "%#: table '%#' refers to unknown table '%#'", sourceName, table._id.c_str(), entry._tableID.c_str() );
            }
        }
        return loadedCount;
    }

    bool LootCatalog::roll( const hashed_string& tableID, GameRandom& random, ItemStackList& outItems, float32 luck ) const
    {
        const LootTableDef* pTable = find( tableID );
        if ( pTable == nullptr )
            return false;
        rollTable( *pTable, random, outItems, MathUtil::max( 0.01f, luck ), 0 );
        return true;
    }

    void LootCatalog::giveEntry( const LootEntry& entry, GameRandom& random, ItemStackList& outItems, float32 luck, int32 depth ) const
    {
        if ( entry._tableID.empty() == false )
        {
            const LootTableDef* pTable = find( entry._tableID );
            if ( pTable != nullptr && depth + 1 < kMaxDepth )
                rollTable( *pTable, random, outItems, luck, depth + 1 );
            return;
        }
        const int32 count = random.nextInt( entry._minCount, entry._maxCount );
        if ( count > 0 && entry._itemID.empty() == false )
            outItems.addItem( entry._itemID, count );
    }

    void LootCatalog::rollTable( const LootTableDef& table, GameRandom& random, ItemStackList& outItems, float32 luck, int32 depth ) const
    {
        for ( const LootEntry& entry : table._listAlways )
        {
            if ( random.nextChance( MathUtil::saturate( entry._chance * luck ) ) )
                giveEntry( entry, random, outItems, luck, depth );
        }
        float32 totalWeight = table._noneWeight / luck;
        for ( const LootEntry& entry : table._listEntry )
        {
            totalWeight += entry._weight;
        }
        if ( totalWeight <= 0.0f )
            return;
        const int32 rollCount = random.nextInt( table._minRolls, table._maxRolls );
        for ( int32 rollIndex = 0; rollIndex < rollCount; ++rollIndex )
        {
            float32 pick = random.nextFloat() * totalWeight;
            for ( const LootEntry& entry : table._listEntry )
            {
                if ( pick < entry._weight )
                {
                    giveEntry( entry, random, outItems, luck, depth );
                    break;
                }
                pick -= entry._weight;
            }
            // 남은 pick 은 "없음" 에 해당한다.
        }
    }

    float32 LootCatalog::computeEntryChance( const LootEntry& entry, const hashed_string& itemID, int32 depth ) const
    {
        if ( entry._tableID.empty() == false )
        {
            const LootTableDef* pTable = find( entry._tableID );
            return pTable != nullptr && depth + 1 < kMaxDepth ? computeTableChance( *pTable, itemID, depth + 1 ) : 0.0f;
        }
        return entry._itemID == itemID && entry._maxCount > 0 ? 1.0f : 0.0f;
    }

    float32 LootCatalog::computeTableChance( const LootTableDef& table, const hashed_string& itemID, int32 depth ) const
    {
        float32 missChance = 1.0f;
        for ( const LootEntry& entry : table._listAlways )
        {
            missChance *= 1.0f - entry._chance * computeEntryChance( entry, itemID, depth );
        }
        float32 totalWeight = table._noneWeight;
        for ( const LootEntry& entry : table._listEntry )
        {
            totalWeight += entry._weight;
        }
        if ( totalWeight > 0.0f )
        {
            float32 pickChance = 0.0f;
            for ( const LootEntry& entry : table._listEntry )
            {
                pickChance += entry._weight / totalWeight * computeEntryChance( entry, itemID, depth );
            }
            const float32 rolls = 0.5f * static_cast<float32>( table._minRolls + table._maxRolls );
            missChance *= MathUtil::pow( 1.0f - MathUtil::saturate( pickChance ), rolls );
        }
        return 1.0f - missChance;
    }

    float32 LootCatalog::computeDropChance( const hashed_string& tableID, const hashed_string& itemID ) const
    {
        const LootTableDef* pTable = find( tableID );
        return pTable != nullptr ? computeTableChance( *pTable, itemID, 0 ) : 0.0f;
    }
} // namespace sw
