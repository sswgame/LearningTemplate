#include "pch.h"

#include "GameFramework/Base/Foundation/Data/XMLCatalog.h"

#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Base/Foundation/Data/GameDataXML.h"

namespace sw
{
    bool XMLCatalogLoader::loadFile( void* pCatalog, ReadRootFunction pReadRoot, string_view path, const utf8* pRootName )
    {
        XMLDocument doc;
        XMLNode     root;
        string      sourceName;
        return GameDataXML::loadRoot( doc, path, pRootName, root, sourceName ) && pReadRoot( pCatalog, root, sourceName );
    }

    bool XMLCatalogLoader::loadText( void* pCatalog, ReadRootFunction pReadRoot, string_view xmlText, string_view sourceName, const utf8* pRootName )
    {
        XMLDocument doc;
        XMLNode     root;
        return GameDataXML::parseRoot( doc, xmlText, sourceName, pRootName, root ) && pReadRoot( pCatalog, root, sourceName );
    }
} // namespace sw
