#include "pch.h"

#include "GameFramework/Base/Gameplay/Inventory/LootTable.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Base/Foundation/Data/GameDataXML.h"
#include "GameFramework/Base/Foundation/Utility/Random/GameRandom.h"
#include "GameFramework/Base/Gameplay/Inventory/ItemStackList.h"

namespace sw
{
    SW_LOG_CALLER( "LootTable" );

    namespace
    {
        struct LootTableInternal
        {
            static LootEntry readEntry( const XMLNode& node )
            {
                LootEntry   entry;
                const utf8* pItem  = node.findAttribute( "item" );
                const utf8* pTable = node.findAttribute( "table" );
                entry._itemId      = pItem != nullptr ? hashed_string( pItem ) : hashed_string{};
                entry._tableId     = pTable != nullptr ? hashed_string( pTable ) : hashed_string{};
                entry._weight      = MathUtil::max( 0.0f, node.getAttributeFloat( "weight", entry._weight ) );
                entry._chance      = MathUtil::saturate( node.getAttributeFloat( "chance", entry._chance ) );
                entry._minCount    = MathUtil::max( 0, node.getAttributeInt( "min", node.getAttributeInt( "count", entry._minCount ) ) );
                entry._maxCount    = MathUtil::max( entry._minCount, node.getAttributeInt( "max", entry._minCount ) );
                return entry;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    uint32 LootCatalog::loadRoot( const XMLNode& root, string_view sourceName )
    {
        uint32 loadedCount = 0;
        for ( XMLNode node = root.findChild( "Table" ); node; node = node.findNextSibling( "Table" ) )
        {
            const utf8* pId = GameDataXML::findRequiredId( node, sourceName );
            if ( pId == nullptr )
                continue;
            LootTableDef table;
            table._id         = hashed_string( pId );
            table._minRolls   = MathUtil::max( 0, node.getAttributeInt( "rolls", table._minRolls ) );
            table._maxRolls   = MathUtil::max( table._minRolls, node.getAttributeInt( "rollsMax", table._minRolls ) );
            table._noneWeight = MathUtil::max( 0.0f, node.getAttributeFloat( "none", table._noneWeight ) );
            for ( XMLNode child = node.findChild( "Entry" ); child; child = child.findNextSibling( "Entry" ) )
            {
                table._listEntry.push_back( LootTableInternal::readEntry( child ) );
            }
            for ( XMLNode child = node.findChild( "Always" ); child; child = child.findNextSibling( "Always" ) )
            {
                table._listAlways.push_back( LootTableInternal::readEntry( child ) );
            }
            addTable( table );
            ++loadedCount;
        }
        for ( const LootTableDef& table : _catalog.getAll() )
        {
            for ( const LootEntry& entry : table._listEntry )
            {
                if ( entry._tableId.empty() == false && _catalog.find( entry._tableId ) == nullptr )
                    SW_LOG_WARNING( "%#: table '%#' refers to unknown table '%#'", sourceName, table._id.c_str(), entry._tableId.c_str() );
            }
        }
        return loadedCount;
    }

    bool LootCatalog::roll( const hashed_string& tableId, GameRandom& random, ItemStackList& outItems, float32 luck ) const
    {
        const LootTableDef* pTable = _catalog.find( tableId );
        if ( pTable == nullptr )
            return false;
        rollTable( *pTable, random, outItems, MathUtil::max( 0.01f, luck ), 0 );
        return true;
    }

    void LootCatalog::giveEntry( const LootEntry& entry, GameRandom& random, ItemStackList& outItems, float32 luck, int32 depth ) const
    {
        if ( entry._tableId.empty() == false )
        {
            const LootTableDef* pTable = _catalog.find( entry._tableId );
            if ( pTable != nullptr && depth + 1 < kMaxDepth )
                rollTable( *pTable, random, outItems, luck, depth + 1 );
            return;
        }
        const int32 count = random.nextInt( entry._minCount, entry._maxCount );
        if ( count > 0 && entry._itemId.empty() == false )
            outItems.addItem( entry._itemId, count );
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
            // 남은 pick 은 "없음" 몫이다.
        }
    }

    float32 LootCatalog::computeEntryChance( const LootEntry& entry, const hashed_string& itemId, int32 depth ) const
    {
        if ( entry._tableId.empty() == false )
        {
            const LootTableDef* pTable = _catalog.find( entry._tableId );
            return pTable != nullptr && depth + 1 < kMaxDepth ? computeTableChance( *pTable, itemId, depth + 1 ) : 0.0f;
        }
        return entry._itemId == itemId && entry._maxCount > 0 ? 1.0f : 0.0f;
    }

    float32 LootCatalog::computeTableChance( const LootTableDef& table, const hashed_string& itemId, int32 depth ) const
    {
        float32 missChance = 1.0f;
        for ( const LootEntry& entry : table._listAlways )
        {
            missChance *= 1.0f - entry._chance * computeEntryChance( entry, itemId, depth );
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
                pickChance += entry._weight / totalWeight * computeEntryChance( entry, itemId, depth );
            }
            const float32 rolls = 0.5f * static_cast<float32>( table._minRolls + table._maxRolls );
            missChance *= MathUtil::pow( 1.0f - MathUtil::saturate( pickChance ), rolls );
        }
        return 1.0f - missChance;
    }

    float32 LootCatalog::computeDropChance( const hashed_string& tableId, const hashed_string& itemId ) const
    {
        const LootTableDef* pTable = _catalog.find( tableId );
        return pTable != nullptr ? computeTableChance( *pTable, itemId, 0 ) : 0.0f;
    }
} // namespace sw
