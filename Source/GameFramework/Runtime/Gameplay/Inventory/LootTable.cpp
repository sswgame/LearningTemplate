#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Inventory/LootTable.h"

#include "Core/Math/MathUtil.h"

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
        /**
         * @brief `<Entry>` · `<Always>` 하나를 스키마(`LootEntryRowDef`)로 읽어 @p outEntry 를 채웁니다.
         * @return 모르는 속성 · 범위 밖 값 · `max` < `min` 이면 false 입니다(오류는 이름과 함께 남긴다).
         */
        [[nodiscard]] bool readLootEntry( const XMLNode& node, string_view sourceName, LootEntry& outEntry )
        {
            LootEntryRowDef row;
            if ( XMLFields::readProperties( node, row, sourceName ) == false )
                return false;
            const bool bHasMax = node.findAttribute( "max" ) != nullptr;
            if ( bHasMax && row._max < row._min )
            {
                SW_LOG_ERROR_ID( "LootTable.MaxLessMin", "%#: <%# max=\"%#\"> is less than min=\"%#\"", sourceName, node.getName(), row._max, row._min );
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
        using Element                          = XMLCatalogElement<LootCatalog>;
        static constexpr Element kArrElement[] = {
            Element::of<&LootCatalog::loadTable>( "Table" ),
        };
        const XMLCatalogReadResult result = XMLCatalogReadResult::readInDocumentOrder( *this, root, sourceName, kArrElement );
        for ( const LootTableDef& table : getAll() )
        {
            for ( const LootEntry& entry : table._listEntry )
            {
                if ( entry._tableID.empty() == false && find( entry._tableID ) == nullptr )
                    SW_LOG_WARNING_ID( "LootTable.TableRefersUnknownTable", "%#: table '%#' refers to unknown table '%#'", sourceName, table._id.c_str(), entry._tableID.c_str() );
            }
        }
        return result._loadedCount;
    }

    bool LootCatalog::loadTable( const XMLNode& node, string_view sourceName )
    {

        const utf8* pID = XMLReadUtil::findRequiredID( node, sourceName );
        if ( pID == nullptr )
            return false;
        // 기본값 · 범위는 스키마(LootTableRowDef · LootEntryRowDef)가 정한다. 오류가 있는 테이블 · 항목은 넣지 않는다(오류는 readProperties 가 남긴다).
        LootTableRowDef row;
        if ( XMLFields::readProperties( node, row, sourceName, { "id" } ) == false )
            return false;
        const bool bHasRollsMax = node.findAttribute( "rollsMax" ) != nullptr;
        if ( bHasRollsMax && row._rollsMax < row._rolls )
        {
            SW_LOG_ERROR_ID( "LootTable.TableRollsMaxLessRolls", "%#: table '%#' rollsMax=\"%#\" is less than rolls=\"%#\"", sourceName, pID, row._rollsMax, row._rolls );
            return false;
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
        return true;
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

namespace sw
{
    /** 쓰는 쪽은 공유 캐시의 한 벌을 받는다(`SharedAssetCache<LootCatalog>::acquire`). 경로 규칙 없이 접미사로 가르는 종류라 카탈로그 편집기에는 나오지 않는다. */
    SW_MODULE_SHARED_ASSET_CACHE( LootCatalog, LootCatalog, SW_GF_API, LootCatalog::kAssetType.withLoader( AssetTypeLoader::of<LootCatalog>( MemoryTag::Game ) ) );
} // namespace sw
