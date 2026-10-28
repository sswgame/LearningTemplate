#include "pch.h"

#include "GameFramework/Base/Foundation/GameData/XMLCatalog.h"

#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

namespace sw
{
    bool XMLCatalogLoader::loadFile( void* pCatalog, ReadRootFunction pReadRoot, const AssetID& assetID, const utf8* pRootName )
    {
        XMLDocument doc;
        XMLNode     root;
        string      sourceName;
        return XMLReadUtil::loadRootOrWarn( doc, assetID, pRootName, root, sourceName ) && pReadRoot( pCatalog, root, sourceName );
    }

    bool XMLCatalogLoader::loadText( void* pCatalog, ReadRootFunction pReadRoot, string_view xmlText, string_view sourceName, const utf8* pRootName )
    {
        XMLDocument doc;
        XMLNode     root;
        return XMLReadUtil::parseRootOrWarn( doc, xmlText, sourceName, pRootName, root ) && pReadRoot( pCatalog, root, sourceName );
    }
} // namespace sw
