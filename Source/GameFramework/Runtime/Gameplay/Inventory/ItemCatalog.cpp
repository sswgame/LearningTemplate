#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Inventory/ItemCatalog.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Asset/AssetDataKind.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLFields.h"
#include "Engine/Serialization/XML/XMLNameCheck.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

namespace sw
{
    namespace
    {
        struct ItemCatalogInternal
        {
            /** @brief `<Requires>` 하나를 읽습니다. 조건 속성이 없거나 둘 이상이면 경고를 남기고 false 를 반환합니다. */
            [[nodiscard]] static bool readRequires( const XMLNode& node, EquipCondition& outCondition, string_view sourceName, const utf8* pItemID )
            {
                const utf8* pSet          = node.findAttribute( "set" );
                const utf8* pEquippedTag  = node.findAttribute( "equippedTag" );
                const utf8* pCharacterTag = node.findAttribute( "characterTag" );
                const utf8* pBodyShape    = node.findAttribute( "bodyShape" );
                const int32 kindCount     = ( pSet != nullptr ? 1 : 0 ) + ( pEquippedTag != nullptr ? 1 : 0 ) + ( pCharacterTag != nullptr ? 1 : 0 ) + ( pBodyShape != nullptr ? 1 : 0 );
                if ( kindCount != 1 )
                {
                    SW_LOG_WARNING_ID( "ItemCatalog.ItemRequiresNeedsExactly", "%#: item '%#' <Requires> needs exactly one of set / equippedTag / characterTag / bodyShape", sourceName, pItemID );
                    return false;
                }
                EquipRequiresFields fields;
                if ( XMLFields::readFields( node, fields, sourceName, { "bodyShape" } ) == false ) // 모르는 속성 · 깨진 pieces 는 오류다
                    return false;
                if ( pSet != nullptr )
                {
                    outCondition._setID      = fields._set;
                    outCondition._pieceCount = fields._pieces;
                    outCondition._kind       = outCondition._pieceCount > 0 ? EquipConditionKind::SetPieces : EquipConditionKind::SetComplete;
                    return outCondition._setID.empty() == false;
                }
                if ( pEquippedTag != nullptr || pCharacterTag != nullptr )
                {
                    outCondition._kind = pEquippedTag != nullptr ? EquipConditionKind::EquippedTag : EquipConditionKind::CharacterTag;
                    outCondition._tag  = TagID::request( pEquippedTag != nullptr ? fields._equippedTag : fields._characterTag );
                    return outCondition._tag.isValid();
                }
                outCondition._kind = EquipConditionKind::BodyShape;
                StringUtil::forEachToken( string_view( pBodyShape ), ",; ", [&]( string_view token )
                {
                    outCondition._listBodyShape.push_back( hashed_string( token ) );
                } );
                return outCondition._listBodyShape.empty() == false;
            }
        };
    } // namespace

    bool ItemDef::hasTag( const hashed_string& tag ) const
    {
        return VectorUtil::contains( _listTag, tag );
    }

    bool ItemDef::hasTagUnder( TagID parentTag ) const
    {
        return VectorUtil::containsIf( _listTag, [&]( const hashed_string& ownTag )
        { return TagID::request( ownTag.view() ).isSubtagOf( parentTag ); } );
    }

    int32 ItemCatalog::getMaxStack( const hashed_string& id ) const
    {
        const ItemDef* pDef = find( id );
        return pDef != nullptr ? pDef->_maxStack : 1;
    }

    uint32 ItemCatalog::loadRoot( const XMLNode& root, string_view sourceName )
    {
        uint32 loadedCount = 0;
        for ( XMLNode node = root.findChild( "Item" ); node; node = node.findNextSibling( "Item" ) )
        {
            // 적지 않은 칸은 ItemDef 의 초기값이다. 깨진 값 · 범위 밖 값 · 모르는 깨짐 정책은 그 아이템을 넣지 않는 오류다(자르지 않는다).
            ItemDef def;
            bool    bValid = XMLFields::readFields( node, def, sourceName, { "tags" } );
            if ( def._id.empty() )
                continue;
            const utf8* const pID = def._id.c_str();
            if ( def._name.empty() )
                def._name = pID;
            StringUtil::forEachToken( node.getAttributeText( "tags" ), ",; ", [&]( string_view token )
            {
                def._listTag.push_back( hashed_string( token ) );
            } );
            if ( def._maxDurability > 0.0f && def._maxStack > 1 )
            {
                SW_LOG_WARNING_ID( "ItemCatalog.DurabilityStacksLimited", "%#: '%#' has durability - stacks are limited to 1", sourceName, pID );
                def._maxStack = 1;
            }
            const XMLNode statNode = node.findChild( "Stats" );
            if ( statNode )
                def._stats.addAttributes( statNode ); // 읽은 속성 수만 반환한다. 속성이 없으면 빈 스탯이다
            for ( XMLNode requiresNode = node.findChild( "Requires" ); requiresNode; requiresNode = requiresNode.findNextSibling( "Requires" ) )
            {
                EquipCondition condition;
                if ( ItemCatalogInternal::readRequires( requiresNode, condition, sourceName, pID ) )
                    def._listEquipCondition.push_back( condition );
                else
                    bValid = false;
            }
            if ( bValid == false )
            {
                SW_LOG_ERROR_ID( "ItemCatalog.ItemInvalidFieldsSkipped", "%#: item '%#' has invalid fields - skipped", sourceName, pID );
                continue;
            }
            add( def );
            ++loadedCount;
        }
        if ( loadedCount == 0 )
            SW_LOG_WARNING_ID( "ItemCatalog.NoItemEntries", "%#: no <Item> entries", sourceName );
        return loadedCount;
    }
} // namespace sw

namespace sw
{
    /** @brief `ItemCatalog` 의 데이터 파일 종류입니다(스키마 테스트와 카탈로그 편집기가 이 줄로 어떤 파일을 어떻게 읽는지 압니다). */
    SW_RESOURCE_DATA_KIND( Items, ItemCatalog::kDataKind, nullptr, nullptr, "/data/items.xml", &AssetDataKind::loadCatalog<ItemCatalog> );
} // namespace sw
