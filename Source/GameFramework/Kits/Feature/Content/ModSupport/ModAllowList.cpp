#include "pch.h"

#include "GameFramework/Kits/Feature/Content/ModSupport/ModAllowList.h"

#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"

#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Runtime/Foundation/GameData/XMLCatalogTable.h"

namespace sw
{
    namespace
    {
        struct ModAllowListInternal
        {
            static constexpr size_t kHashTextLength = 16;
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool ModAllowList::parseXMLText( string_view text, string_view sourceName, ModAllowList& outList )
    {
        XMLDocument doc;
        if ( doc.parse( text, sourceName ) == false )
        {
            SW_LOG_ERROR_ID( "ModAllowList.NotXML", "Mod allow list '%#' could not be parsed: %#", sourceName, doc.getLastError() );
            return false;
        }
        const XMLNode root = doc.getRoot( kRootElement );
        if ( root.isValid() == false )
        {
            SW_LOG_ERROR_ID( "ModAllowList.WrongRoot", "Mod allow list '%#' has no <%#> root", sourceName, kRootElement );
            return false;
        }
        ModAllowList list;
        using Element                          = XMLCatalogElement<ModAllowList>;
        static constexpr Element kArrElement[] = {
            Element::of<&ModAllowList::readMod>( "Mod" ).uncounted(), // 빈 목록은 모드를 하나도 허락하지 않는 맞는 목록이다
        };
        if ( XMLCatalogReadResult::readInDocumentOrder( list, XMLCatalogRoot{ root }, sourceName, kArrElement ).isClean() == false )
            return false;
        outList = std::move( list );
        return true;
    }

    bool ModAllowList::readMod( const XMLNode& node, string_view sourceName )
    {
        const string_view id       = node.getAttributeText( "id" );
        const string_view hashText = node.getAttributeText( "contentHash" );
        ModDigestEntry    entry;
        const bool        bRead = ModManifest::isValidID( id ) && ModVersion::parse( node.getAttributeText( "version" ), entry._version ) &&
                           hashText.size() == ModAllowListInternal::kHashTextLength && StringUtil::parseUint64( hashText, entry._contentHash, 16 );
        if ( bRead == false )
        {
            SW_LOG_ERROR_ID( "ModAllowList.BadEntry", "Mod allow list '%#': <Mod> needs id (a-z, 0-9, _), version (major.minor.patch) and contentHash (16 hex digits)", sourceName );
            return false;
        }
        entry._id = string{ id };
        for ( const ModDigestEntry& listed : _listEntry )
        {
            if ( listed._id == entry._id )
            {
                SW_LOG_ERROR_ID( "ModAllowList.DuplicateID", "Mod allow list '%#': mod '%#' is listed twice", sourceName, entry._id );
                return false;
            }
        }
        _listEntry.push_back( std::move( entry ) );
        return true;
    }

    bool ModAllowList::checkDigest( const ModSetDigest& digest ) const
    {
        for ( const ModDigestEntry& active : digest._listEntry )
        {
            const ModDigestEntry* pAllowed = nullptr;
            for ( const ModDigestEntry& entry : _listEntry )
            {
                if ( entry._id == active._id )
                    pAllowed = &entry;
            }
            if ( pAllowed == nullptr )
            {
                SW_LOG_ERROR_ID( "ModAllowList.NotAllowed", "Mod '%#' is not on the server's allow list", active._id );
                return false;
            }
            if ( pAllowed->_version != active._version || pAllowed->_contentHash != active._contentHash )
            {
                SW_LOG_ERROR_ID( "ModAllowList.Mismatch", "Mod '%#' %# is allowed only as %# with its listed content hash - this copy differs", active._id, active._version.toString(),
                                 pAllowed->_version.toString() );
                return false;
            }
        }
        return true;
    }
} // namespace sw
