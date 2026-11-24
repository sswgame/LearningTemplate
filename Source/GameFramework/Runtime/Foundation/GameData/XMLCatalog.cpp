#include "pch.h"

#include "GameFramework/Runtime/Foundation/GameData/XMLCatalog.h"

#include "Core/Log/Logger.h"

#include "Engine/Serialization/Inherit/DataCatalogReferences.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

#include "GameFramework/Runtime/Foundation/GameData/XMLCatalogRoot.h"
#include "GameFramework/Runtime/Foundation/GameData/XMLCatalogTable.h"

namespace sw
{
    void XMLCatalogReadResult::reportNoEntries( string_view sourceName, const utf8* pRootName )
    {
        SW_LOG_WARNING_ID( "XMLCatalog.NoEntries", "%#: <%#> has no entries", sourceName, pRootName );
    }

    bool XMLCatalogLoader::loadFromAsset( void* pCatalog, ReadRootFunction pReadRoot, const AssetID& assetID, const utf8* pRootName )
    {
        XMLDocument doc;
        XMLNode     root;
        string      sourceName;
        if ( XMLReadUtil::loadRootOrWarn( doc, assetID, pRootName, root, sourceName ) == false )
            return false;
        if ( doc.isCooked() )
            return pReadRoot( pCatalog, XMLCatalogRoot{ root }, sourceName ); // 참조 규칙은 쿠커가 같은 표로 보았다
        return readAndValidate( pCatalog, pReadRoot, root, sourceName );
    }

    bool XMLCatalogLoader::loadCatalog( void* pCatalog, ReadRootFunction pReadRoot, const AssetID& assetID, const utf8* pRootName )
    {
        XMLDocument doc;
        XMLNode     root;
        string      sourceName;
        if ( XMLReadUtil::loadRootOrWarn( doc, assetID, pRootName, root, sourceName ) == false )
            return false;
        if ( doc.isCooked() )
            return pReadRoot( pCatalog, XMLCatalogRoot{ root }, sourceName );
        DataCatalogReferenceRules rules;
        if ( rules.loadFromAsset() == false )
            return false;
        if ( rules.isCookedRoot( pRootName ) == false )
            SW_LOG_ERROR_ID( "XMLCatalog.RootNotCooked", "Catalog root <%#> (%#) has no <Cooked> row in '%#' - packs would ship it as text", pRootName, sourceName, DataCatalogReferenceRules::kTableAssetID );
        const bool bRead       = pReadRoot( pCatalog, XMLCatalogRoot{ root }, sourceName );
        const bool bReferenced = rules.validateDocument( root, sourceName );
        return bRead && bReferenced;
    }

    bool XMLCatalogLoader::readAndValidate( void* pCatalog, ReadRootFunction pReadRoot, const XMLNode& root, string_view sourceName )
    {
        // 둘 다 돈다 — 형식 오류와 참조 오류를 한 번에 모두 알린다. 참조 규칙은 쿠커와 같은 데이터 표다(`DataCatalogReferenceRules`).
        const bool bRead       = pReadRoot( pCatalog, XMLCatalogRoot{ root }, sourceName );
        const bool bReferenced = DataCatalogReferenceRules::validateWithTable( root, sourceName );
        return bRead && bReferenced;
    }

    bool XMLCatalogLoader::loadFromXMLText( void* pCatalog, ReadRootFunction pReadRoot, string_view xmlText, string_view sourceName, const utf8* pRootName )
    {
        XMLDocument doc;
        XMLNode     root;
        if ( XMLReadUtil::parseRootOrWarn( doc, xmlText, sourceName, pRootName, root ) == false )
            return false;
        return readAndValidate( pCatalog, pReadRoot, root, sourceName );
    }
} // namespace sw
