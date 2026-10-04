#include "pch.h"

#include "GameFramework/Data/XmlCatalog.h"

#include "Engine/Utility/Xml/XmlDocument.h"

#include "GameFramework/Data/GameDataXml.h"

namespace sw
{
    bool XmlCatalogLoader::loadFile( void* pCatalog, ReadRootFunction pReadRoot, string_view path, const utf8* pRootName )
    {
        XmlDocument doc;
        XmlNode     root;
        string      sourceName;
        return GameDataXml::loadRoot( doc, path, pRootName, root, sourceName ) && pReadRoot( pCatalog, root, sourceName );
    }

    bool XmlCatalogLoader::loadText( void* pCatalog, ReadRootFunction pReadRoot, string_view xmlText, string_view sourceName, const utf8* pRootName )
    {
        XmlDocument doc;
        XmlNode     root;
        return GameDataXml::parseRoot( doc, xmlText, sourceName, pRootName, root ) && pReadRoot( pCatalog, root, sourceName );
    }
} // namespace sw
