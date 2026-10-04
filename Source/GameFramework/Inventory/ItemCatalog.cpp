#include "pch.h"

#include "GameFramework/Inventory/ItemCatalog.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Utility/Xml/XmlDocument.h"

#include "GameFramework/Data/GameDataXml.h"

namespace sw
{
    SW_LOG_CALLER( "ItemCatalog" );

    bool ItemDef::hasTag( const hashed_string& tag ) const
    {
        for ( const hashed_string& ownTag : _listTag )
        {
            if ( ownTag == tag )
                return true;
        }
        return false;
    }

    bool ItemCatalog::loadFromResource( string_view path )
    {
        return GameDataXml::loadFile( *this, &ItemCatalog::loadRoot, path, "ItemCatalog" );
    }

    bool ItemCatalog::loadFromXmlText( string_view xmlText, string_view sourceName )
    {
        return GameDataXml::loadText( *this, &ItemCatalog::loadRoot, xmlText, sourceName, "ItemCatalog" );
    }

    int32 ItemCatalog::getMaxStack( const hashed_string& id ) const
    {
        const ItemDef* pDef = _catalog.find( id );
        return pDef != nullptr ? pDef->_maxStack : 1;
    }

    uint32 ItemCatalog::loadRoot( const XmlNode& root, string_view sourceName )
    {
        uint32 loadedCount = 0;
        for ( XmlNode node = root.findChild( "Item" ); node; node = node.findNextSibling( "Item" ) )
        {
            const utf8* pId = GameDataXml::findRequiredId( node, sourceName );
            if ( pId == nullptr )
                continue;
            ItemDef def;
            def._id                = hashed_string( pId );
            const utf8* pName      = node.findAttribute( "name" );
            def._name              = pName != nullptr ? pName : pId;
            const utf8* pCategory  = node.findAttribute( "category" );
            const utf8* pSlot      = node.findAttribute( "slot" );
            const utf8* pUseEffect = node.findAttribute( "useEffect" );
            def._category          = pCategory != nullptr ? hashed_string( pCategory ) : hashed_string{};
            def._equipSlot         = pSlot != nullptr ? hashed_string( pSlot ) : hashed_string{};
            def._useEffect         = pUseEffect != nullptr ? hashed_string( pUseEffect ) : hashed_string{};
            def._weight            = MathUtil::max( 0.0f, node.getAttributeFloat( "weight", def._weight ) );
            def._maxDurability     = MathUtil::max( 0.0f, node.getAttributeFloat( "durability", def._maxDurability ) );
            def._maxStack          = MathUtil::max( 1, node.getAttributeInt( "maxStack", def._maxStack ) );
            def._value             = MathUtil::max( 0, node.getAttributeInt( "value", def._value ) );
            def._rarity            = node.getAttributeInt( "rarity", def._rarity );
            GameDataXml::forEachToken( node.getAttributeText( "tags" ), ",; ", [&]( string_view token )
            {
                def._listTag.push_back( hashed_string( token ) );
            } );
            if ( def._maxDurability > 0.0f && def._maxStack > 1 )
            {
                SW_LOG_WARNING( "%#: '%#' has durability - stacks are limited to 1", sourceName, pId );
                def._maxStack = 1;
            }
            const XmlNode statNode = node.findChild( "Stats" );
            if ( statNode )
                (void)def._stats.loadFromAttributes( statNode );
            addItem( def );
            ++loadedCount;
        }
        if ( loadedCount == 0 )
            SW_LOG_WARNING( "%#: no <Item> entries", sourceName );
        return loadedCount;
    }
} // namespace sw
