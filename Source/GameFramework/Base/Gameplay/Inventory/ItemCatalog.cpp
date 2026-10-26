#include "pch.h"

#include "GameFramework/Base/Gameplay/Inventory/ItemCatalog.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLNameCheck.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

namespace sw
{
    SW_LOG_CALLER( "ItemCatalog" );

    namespace
    {
        struct ItemCatalogInternal
        {
            static constexpr const utf8* kArrRequiresAttribute[] = { "set", "pieces", "equippedTag", "characterTag", "bodyShape" };

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
                    SW_LOG_WARNING( "%#: item '%#' <Requires> needs exactly one of set / equippedTag / characterTag / bodyShape", sourceName, pItemID );
                    return false;
                }
                vector<const utf8*> listUnknown;
                if ( XMLNameCheck::collectUnknownAttributes( node, kArrRequiresAttribute, listUnknown ) > 0 )
                {
                    for ( const utf8* pName : listUnknown )
                    {
                        SW_LOG_WARNING( "%#: item '%#' <Requires> has unknown attribute '%#'", sourceName, pItemID, pName );
                    }
                    return false;
                }
                if ( pSet != nullptr )
                {
                    // `pieces` 를 적지 않으면(초기값 0) 세트를 다 갖춘 조건이다.
                    outCondition._setID     = hashed_string( pSet );
                    const bool bPiecesValid = XMLReadUtil::readOptionalNumber( node, "pieces", sourceName, 0, MathUtil::kMaxInt32, outCondition._pieceCount );
                    outCondition._kind      = outCondition._pieceCount > 0 ? EquipConditionKind::SetPieces : EquipConditionKind::SetComplete;
                    return bPiecesValid && outCondition._setID.empty() == false;
                }
                if ( pEquippedTag != nullptr || pCharacterTag != nullptr )
                {
                    outCondition._kind = pEquippedTag != nullptr ? EquipConditionKind::EquippedTag : EquipConditionKind::CharacterTag;
                    outCondition._tag  = TagID::request( pEquippedTag != nullptr ? pEquippedTag : pCharacterTag );
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
            const utf8* pID = XMLReadUtil::findRequiredID( node, sourceName );
            if ( pID == nullptr )
                continue;
            ItemDef def;
            def._id                = hashed_string( pID );
            const utf8* pName      = node.findAttribute( "name" );
            def._name              = pName != nullptr ? pName : pID;
            const utf8* pCategory  = node.findAttribute( "category" );
            const utf8* pSlot      = node.findAttribute( "slot" );
            const utf8* pUseEffect = node.findAttribute( "useEffect" );
            const utf8* pVisual    = node.findAttribute( "visual" );
            const utf8* pPolicy    = node.findAttribute( "breakPolicy" );
            def._category          = pCategory != nullptr ? hashed_string( pCategory ) : hashed_string{};
            def._equipSlot         = pSlot != nullptr ? hashed_string( pSlot ) : hashed_string{};
            def._useEffect         = pUseEffect != nullptr ? hashed_string( pUseEffect ) : hashed_string{};
            def._visualID          = pVisual != nullptr ? hashed_string( pVisual ) : hashed_string{};
            // 적지 않은 칸은 ItemDef 의 초기값이다. 깨진 값 · 범위 밖 값은 그 아이템을 넣지 않는 오류다(자르지 않는다).
            bool bValid = true;
            if ( pPolicy != nullptr && parseEquipBreakPolicy( string_view( pPolicy ), def._breakPolicy ) == false )
            {
                SW_LOG_ERROR( "%#: item '%#' has unknown breakPolicy '%#'", sourceName, pID, pPolicy );
                bValid = false;
            }
            bValid = XMLReadUtil::readOptionalNumber( node, "weight", sourceName, 0.0f, MathUtil::kMaxFloat, def._weight ) && bValid;
            bValid = XMLReadUtil::readOptionalNumber( node, "durability", sourceName, 0.0f, MathUtil::kMaxFloat, def._maxDurability ) && bValid;
            bValid = XMLReadUtil::readOptionalNumber( node, "maxStack", sourceName, 1, MathUtil::kMaxInt32, def._maxStack ) && bValid;
            bValid = XMLReadUtil::readOptionalNumber( node, "value", sourceName, 0, MathUtil::kMaxInt32, def._value ) && bValid;
            bValid = XMLReadUtil::readOptionalNumber( node, "rarity", sourceName, MathUtil::kMinInt32, MathUtil::kMaxInt32, def._rarity ) && bValid;
            StringUtil::forEachToken( node.getAttributeText( "tags" ), ",; ", [&]( string_view token )
            {
                def._listTag.push_back( hashed_string( token ) );
            } );
            if ( def._maxDurability > 0.0f && def._maxStack > 1 )
            {
                SW_LOG_WARNING( "%#: '%#' has durability - stacks are limited to 1", sourceName, pID );
                def._maxStack = 1;
            }
            const XMLNode statNode = node.findChild( "Stats" );
            if ( statNode )
                (void)def._stats.loadFromAttributes( statNode ); // 읽은 속성 수만 반환한다. 속성이 없으면 빈 스탯이다
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
                SW_LOG_ERROR( "%#: item '%#' has invalid fields - skipped", sourceName, pID );
                continue;
            }
            add( def );
            ++loadedCount;
        }
        if ( loadedCount == 0 )
            SW_LOG_WARNING( "%#: no <Item> entries", sourceName );
        return loadedCount;
    }
} // namespace sw
