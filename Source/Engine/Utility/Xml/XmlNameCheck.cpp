#include "pch.h"

#include "Engine/Utility/Xml/XmlNameCheck.h"

#include "Core/String/StringUtil.h"

#include "Engine/Utility/Xml/XmlDocument.h"

namespace sw
{
    namespace
    {
        struct XmlNameCheckInternal
        {
            static bool isKnown( const utf8* pName, const utf8* const* ppKnown, uint32 knownCount )
            {
                for ( uint32 index = 0; index < knownCount; ++index )
                {
                    if ( StringUtil::equals( pName, ppKnown[index], true ) )
                        return true;
                }
                return false;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    const utf8* XmlNameCheck::findUnknownAttribute( const XmlNode& node, const utf8* const* ppKnown, uint32 knownCount )
    {
        for ( XmlAttribute attribute = node.getFirstAttribute(); attribute; attribute = attribute.getNext() )
        {
            if ( XmlNameCheckInternal::isKnown( attribute.getName(), ppKnown, knownCount ) == false )
                return attribute.getName();
        }
        return nullptr;
    }

    const utf8* XmlNameCheck::findUnknownChild( const XmlNode& node, const utf8* const* ppKnown, uint32 knownCount )
    {
        for ( XmlNode child = node.findChild(); child; child = child.findNextSibling() )
        {
            if ( XmlNameCheckInternal::isKnown( child.getName(), ppKnown, knownCount ) == false )
                return child.getName();
        }
        return nullptr;
    }
} // namespace sw
