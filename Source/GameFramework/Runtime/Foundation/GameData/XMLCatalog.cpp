#include "pch.h"

#include "GameFramework/Runtime/Foundation/GameData/XMLCatalog.h"

#include "Core/Log/Logger.h"

#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

#include "GameFramework/Runtime/Foundation/GameData/XMLCatalogTable.h"

namespace sw
{
    void XMLCatalogReader::reportUnknownReference( string_view sourceName, const utf8* pOwnerLabel, const utf8* pOwnerID, const utf8* pTargetLabel, const utf8* pTargetID )
    {
        SW_LOG_ERROR_ID( "XMLCatalog.UnknownReference", "%#: %# '%#' names an unknown %# '%#'", sourceName, pOwnerLabel, pOwnerID, pTargetLabel, pTargetID );
    }

    void XMLCatalogReader::reportNoEntries( string_view sourceName, const utf8* pRootName )
    {
        SW_LOG_WARNING_ID( "XMLCatalog.NoEntries", "%#: <%#> has no entries", sourceName, pRootName );
    }

    bool XMLCatalogLoader::loadFromFile( void* pCatalog, ReadRootFunction pReadRoot, const AssetID& assetID, const utf8* pRootName )
    {
        XMLDocument doc;
        XMLNode     root;
        string      sourceName;
        return XMLReadUtil::loadRootOrWarn( doc, assetID, pRootName, root, sourceName ) && pReadRoot( pCatalog, root, sourceName );
    }

    bool XMLCatalogLoader::loadFromXMLText( void* pCatalog, ReadRootFunction pReadRoot, string_view xmlText, string_view sourceName, const utf8* pRootName )
    {
        XMLDocument doc;
        XMLNode     root;
        return XMLReadUtil::parseRootOrWarn( doc, xmlText, sourceName, pRootName, root ) && pReadRoot( pCatalog, root, sourceName );
    }
} // namespace sw
