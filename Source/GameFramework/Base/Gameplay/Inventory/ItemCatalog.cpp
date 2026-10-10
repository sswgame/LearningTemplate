#include "pch.h"

#include "GameFramework/Base/Gameplay/Inventory/ItemCatalog.h"

#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLNameCheck.h"

#include "GameFramework/Base/Foundation/Data/GameDataXML.h"

namespace sw
{
    SW_LOG_CALLER( "ItemCatalog" );

    namespace
    {
        struct ItemCatalogInternal
        {
            static constexpr const utf8* kArrRequiresAttribute[] = { "set", "pieces", "equippedTag", "characterTag", "bodyShape" };

            /** @brief `<Requires>` 하나를 읽습니다. 조건 속성이 없거나 둘 이상이면 경고하고 false 입니다. */
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
                    outCondition._setID      = hashed_string( pSet );
                    outCondition._pieceCount = node.getAttributeInt( "pieces", 0 );
                    outCondition._kind       = outCondition._pieceCount > 0 ? EquipConditionKind::SetPieces : EquipConditionKind::SetComplete;
                    return outCondition._setID.empty() == false;
                }
                if ( pEquippedTag != nullptr || pCharacterTag != nullptr )
                {
                    outCondition._kind = pEquippedTag != nullptr ? EquipConditionKind::EquippedTag : EquipConditionKind::CharacterTag;
                    outCondition._tag  = TagID::request( pEquippedTag != nullptr ? pEquippedTag : pCharacterTag );
                    return outCondition._tag.isValid();
                }
                outCondition._kind = EquipConditionKind::BodyShape;
                GameDataXML::forEachToken( string_view( pBodyShape ), ",; ", [&]( string_view token )
                {
                    outCondition._listBodyShape.push_back( hashed_string( token ) );
                } );
                return outCondition._listBodyShape.empty() == false;
            }
        };
    } // namespace

    bool ItemDef::hasTag( const hashed_string& tag ) const
    {
        for ( const hashed_string& ownTag : _listTag )
        {
            if ( ownTag == tag )
                return true;
        }
        return false;
    }

    bool ItemDef::hasTagUnder( TagID parentTag ) const
    {
        for ( const hashed_string& ownTag : _listTag )
        {
            if ( TagID::request( ownTag.view() ).isSubtagOf( parentTag ) )
                return true;
        }
        return false;
    }

    int32 ItemCatalog::getMaxStack( const hashed_string& id ) const
    {
        const ItemDef* pDef = _catalog.find( id );
        return pDef != nullptr ? pDef->_maxStack : 1;
    }

    uint32 ItemCatalog::loadRoot( const XMLNode& root, string_view sourceName )
    {
        uint32 loadedCount = 0;
        for ( XMLNode node = root.findChild( "Item" ); node; node = node.findNextSibling( "Item" ) )
        {
            const utf8* pID = GameDataXML::findRequiredID( node, sourceName );
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
            if ( pPolicy != nullptr && parseEquipBreakPolicy( string_view( pPolicy ), def._breakPolicy ) == false )
                SW_LOG_WARNING( "%#: item '%#' has unknown breakPolicy '%#'", sourceName, pID, pPolicy );
            def._weight        = MathUtil::max( 0.0f, node.getAttributeFloat( "weight", def._weight ) );
            def._maxDurability = MathUtil::max( 0.0f, node.getAttributeFloat( "durability", def._maxDurability ) );
            def._maxStack      = MathUtil::max( 1, node.getAttributeInt( "maxStack", def._maxStack ) );
            def._value         = MathUtil::max( 0, node.getAttributeInt( "value", def._value ) );
            def._rarity        = node.getAttributeInt( "rarity", def._rarity );
            GameDataXML::forEachToken( node.getAttributeText( "tags" ), ",; ", [&]( string_view token )
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
                (void)def._stats.loadFromAttributes( statNode ); // 읽은 속성 수만 돌려준다 — 없으면 빈 스탯이다
            for ( XMLNode requiresNode = node.findChild( "Requires" ); requiresNode; requiresNode = requiresNode.findNextSibling( "Requires" ) )
            {
                EquipCondition condition;
                if ( ItemCatalogInternal::readRequires( requiresNode, condition, sourceName, pID ) )
                    def._listEquipCondition.push_back( condition );
            }
            addItem( def );
            ++loadedCount;
        }
        if ( loadedCount == 0 )
            SW_LOG_WARNING( "%#: no <Item> entries", sourceName );
        return loadedCount;
    }
} // namespace sw
