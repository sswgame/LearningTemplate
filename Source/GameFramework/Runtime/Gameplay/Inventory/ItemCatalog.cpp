#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Inventory/ItemCatalog.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"
#include "Core/String/formatString.h"

#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Module/ModuleSharedAssetCache.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLFields.h"
#include "Engine/Serialization/XML/XMLNameCheck.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

#include "GameFramework/Runtime/Foundation/GameData/XMLCatalogTable.h"

namespace sw
{
    namespace
    {
        struct ItemCatalogInternal
        {
            /** @brief `<Requires>` 하나를 읽습니다. 조건 속성이 없거나 둘 이상이면 경고를 남기고 false 를 반환합니다. */
            [[nodiscard]] static bool readRequires( const XMLNode& node, EquipmentCondition& outCondition, string_view sourceName, const utf8* pItemID )
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
                EquipmentRequiresParams params;
                if ( XMLFields::readProperties( node, params, sourceName, { "bodyShape" } ) == false ) // 모르는 속성 · 깨진 pieces 는 오류다
                    return false;
                // 속성은 공통 어휘의 줄임말이다. 칸은 읽기 때의 정적 검사(조건 순환 · 없는 세트)가 읽는다.
                string text;
                if ( pSet != nullptr )
                {
                    outCondition._setID = params._set;
                    outCondition._kind  = params._pieces > 0 ? EquipmentConditionKind::SetPieces : EquipmentConditionKind::SetComplete;
                    if ( outCondition._setID.empty() )
                        return false;
                    StringBuilder<constant::buffer::kMax256> textBuilder;
                    if ( params._pieces > 0 )
                        textBuilder.appendFormat( "setPieces(%#, %#)", outCondition._setID.c_str(), params._pieces );
                    else
                        textBuilder.appendFormat( "setComplete(%#)", outCondition._setID.c_str() );
                    text = string( textBuilder.view() );
                }
                else if ( pEquippedTag != nullptr || pCharacterTag != nullptr )
                {
                    const string_view tagText = pEquippedTag != nullptr ? string_view( params._equippedTag ) : string_view( params._characterTag );
                    outCondition._kind        = pEquippedTag != nullptr ? EquipmentConditionKind::EquippedTag : EquipmentConditionKind::CharacterTag;
                    outCondition._tag         = TagID::request( tagText );
                    if ( outCondition._tag.isValid() == false )
                        return false;
                    text = string( pEquippedTag != nullptr ? "equippedTag(" : "tag(" ) + string( tagText ) + ")";
                }
                else
                {
                    outCondition._kind = EquipmentConditionKind::BodyShape;
                    StringUtil::forEachToken( string_view( pBodyShape ), ",; ", [&]( string_view token )
                    {
                        text += text.empty() ? "" : ", ";
                        text += token;
                    } );
                    if ( text.empty() )
                        return false;
                    text = "bodyShape(" + text + ")";
                }
                return ConditionExpr::compile( text, sourceName, outCondition._expr );
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

    uint32 ItemCatalog::loadRoot( const XMLCatalogRoot& root, string_view sourceName )
    {
        using Element                          = XMLCatalogElement<ItemCatalog>;
        static constexpr Element kArrElement[] = {
            Element::of<&ItemCatalog::loadItem>( "Item" ),
        };
        const XMLCatalogReadResult result = XMLCatalogReadResult::readInDocumentOrder( *this, root, sourceName, kArrElement );
        return result._loadedCount;
    }

    bool ItemCatalog::loadItem( const XMLNode& node, string_view sourceName )
    {

        // 적지 않은 칸은 ItemDef 의 초기값이다. 깨진 값 · 범위 밖 값 · 모르는 깨짐 정책은 그 아이템을 넣지 않는 오류다(자르지 않는다).
        ItemDef def;
        bool    bValid = XMLFields::readProperties( node, def, sourceName );
        if ( def._id.empty() )
            return false;
        const utf8* const pID = def._id.c_str();
        if ( def._name.empty() )
            def._name = pID;
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
            EquipmentCondition condition;
            if ( ItemCatalogInternal::readRequires( requiresNode, condition, sourceName, pID ) )
                def._listEquipCondition.push_back( condition );
            else
                bValid = false;
        }
        if ( bValid == false )
        {
            SW_LOG_ERROR_ID( "ItemCatalog.ItemInvalidFieldsSkipped", "%#: item '%#' has invalid fields - skipped", sourceName, pID );
            return false;
        }
        add( def );
        return true;
    }
} // namespace sw

namespace sw
{
    /** @brief `ItemCatalog` 의 데이터 파일 종류입니다(스키마 테스트와 카탈로그 편집기가 이 줄로 어떤 파일을 어떻게 읽는지 압니다). */
    SW_MODULE_SHARED_ASSET_CACHE( Items, ItemCatalog, SW_GF_API, AssetTypeDef::makeDataFile( ItemCatalog::kDataKind, AssetPathRule{ {}, {}, "/data/items.xml" }, AssetTypeLoader::of<ItemCatalog>( MemoryTag::Game ) ) );
} // namespace sw
