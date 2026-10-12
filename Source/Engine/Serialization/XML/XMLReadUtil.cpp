#include "pch.h"

#include "Engine/Serialization/XML/XMLReadUtil.h"

#include "Core/Container/StringUtil.h"

#include "Engine/Serialization/XML/XMLDocument.h"

namespace sw
{
    XMLRootStatus XMLReadUtil::loadRoot( XMLDocument& outDocument, string_view path, const utf8* pRootName, XMLNode& outRoot, string* pOutAbsPath )
    {
        if ( outDocument.loadPath( path, pOutAbsPath ) == false )
            return XMLRootStatus::NotLoaded;
        outRoot = outDocument.getRoot( pRootName );
        return outRoot.isValid() ? XMLRootStatus::Found : XMLRootStatus::RootMissing;
    }

    XMLRootStatus XMLReadUtil::parseRoot( XMLDocument& outDocument, string_view xmlText, string_view sourceName, const utf8* pRootName,
                                          XMLNode& outRoot )
    {
        if ( outDocument.parse( xmlText, sourceName ) == false )
            return XMLRootStatus::NotLoaded;
        outRoot = outDocument.getRoot( pRootName );
        return outRoot.isValid() ? XMLRootStatus::Found : XMLRootStatus::RootMissing;
    }

    hashed_string XMLReadUtil::readName( const XMLNode& node, const utf8* pAttribute )
    {
        const string_view text = StringUtil::trim( node.getAttributeText( pAttribute ) );
        return text.empty() ? hashed_string{} : hashed_string( text );
    }
} // namespace sw
