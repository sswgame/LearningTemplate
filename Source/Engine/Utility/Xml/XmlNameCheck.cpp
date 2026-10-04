#include "pch.h"

#include "Engine/Utility/Xml/XmlNameCheck.h"

#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"

#include "Engine/Utility/Xml/XmlDocument.h"

namespace sw
{
    SW_LOG_CALLER( "XmlNameCheck" );

    namespace
    {
        struct XmlNameCheckInternal
        {
            /** @brief 모르는 이름 한 줄을 남깁니다(@p pWhat 은 `attribute 'x'` · `element <x>` 꼴로 이미 지은 글). Error 가 아니면 Warning 이다. */
            static void logUnknown( LogLevel level, string_view sourceName, const utf8* pOwnerName, const string& what )
            {
                if ( level == LogLevel::Error )
                    SW_LOG_ERROR( "%#: <%#> has unknown %#", sourceName, pOwnerName, what );
                else
                    SW_LOG_WARNING( "%#: <%#> has unknown %#", sourceName, pOwnerName, what );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool XmlNameCheck::isKnownName( const utf8* pName, const utf8* const* ppKnown, uint32 knownCount )
    {
        for ( uint32 index = 0; index < knownCount; ++index )
        {
            if ( StringUtil::equals( pName, ppKnown[index], true ) )
                return true;
        }
        return false;
    }

    uint32 XmlNameCheck::collectUnknownAttributes( const XmlNode& node, const utf8* const* ppKnown, uint32 knownCount, vector<const utf8*>& outListName,
                                                   AcceptNameFunction pfnAlsoKnown )
    {
        uint32 unknownCount{ 0 };
        for ( XmlAttribute attribute = node.getFirstAttribute(); attribute; attribute = attribute.getNext() )
        {
            const utf8* pName  = attribute.getName();
            const bool  bKnown = isKnownName( pName, ppKnown, knownCount ) || ( pfnAlsoKnown != nullptr && pfnAlsoKnown( pName ) );
            if ( bKnown )
                continue;
            outListName.push_back( pName );
            ++unknownCount;
        }
        return unknownCount;
    }

    uint32 XmlNameCheck::collectUnknownChildren( const XmlNode& node, const utf8* const* ppKnown, uint32 knownCount, vector<const utf8*>& outListName )
    {
        uint32 unknownCount{ 0 };
        for ( XmlNode child = node.findChild(); child; child = child.findNextSibling() )
        {
            if ( isKnownName( child.getName(), ppKnown, knownCount ) )
                continue;
            outListName.push_back( child.getName() );
            ++unknownCount;
        }
        return unknownCount;
    }

    bool XmlNameCheck::reportUnknownAttributes( const XmlNode& node, const utf8* const* ppKnown, uint32 knownCount, string_view sourceName, LogLevel level,
                                                AcceptNameFunction pfnAlsoKnown )
    {
        vector<const utf8*> listName;
        if ( collectUnknownAttributes( node, ppKnown, knownCount, listName, pfnAlsoKnown ) == 0 )
            return true;
        for ( const utf8* pName : listName )
            XmlNameCheckInternal::logUnknown( level, sourceName, node.getName(), string( "attribute '" ) + pName + "'" );
        return false;
    }

    bool XmlNameCheck::reportUnknownChildren( const XmlNode& node, const utf8* const* ppKnown, uint32 knownCount, string_view sourceName, LogLevel level )
    {
        vector<const utf8*> listName;
        if ( collectUnknownChildren( node, ppKnown, knownCount, listName ) == 0 )
            return true;
        for ( const utf8* pName : listName )
            XmlNameCheckInternal::logUnknown( level, sourceName, node.getName(), string( "element <" ) + pName + ">" );
        return false;
    }
} // namespace sw
