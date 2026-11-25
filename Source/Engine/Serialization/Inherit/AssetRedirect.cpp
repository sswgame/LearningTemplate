#include "pch.h"

#include "Engine/Serialization/Inherit/AssetRedirect.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"

#include "Engine/Serialization/XML/XMLDocument.h"

namespace sw
{
    namespace
    {
        struct AssetRedirectInternal
        {
            static constexpr const utf8* kIdentityRedirectElement = "IdentityRedirect";
        };
    } // namespace

    expected<vector<AssetIdentityRedirect>> AssetRedirect::parseIdentityRows( string_view text, string_view sourceName )
    {
        using Internal = AssetRedirectInternal;
        const Error malformed{ ErrorKind::InvalidArgument, "the asset redirect table is malformed", sourceName };
        XMLDocument doc;
        if ( doc.parse( text, sourceName ) == false )
        {
            SW_LOG_ERROR_ID( "AssetRedirect.NotXML", "Asset redirect table '%#' could not be parsed: %#", sourceName, doc.getLastError() );
            return unexpected( malformed );
        }
        const XMLNode root = doc.getRoot( kRootElement );
        if ( root.isValid() == false )
        {
            SW_LOG_ERROR_ID( "AssetRedirect.WrongRoot", "Asset redirect table '%#' has no <%#> root", sourceName, kRootElement );
            return unexpected( malformed );
        }
        vector<AssetIdentityRedirect> listRow;
        for ( XMLNode node = root.findChild(); node.isValid(); node = node.findNextSibling() )
        {
            if ( StringUtil::isNullOrEmpty( node.getName() ) )
                continue;
            if ( string_view{ node.getName() } != Internal::kIdentityRedirectElement )
            {
                SW_LOG_ERROR_ID( "AssetRedirect.UnknownElement", "Asset redirect table '%#': <%#> is not <%#>", sourceName, node.getName(), Internal::kIdentityRedirectElement );
                return unexpected( malformed );
            }
            const optional<AssetGUID> from   = AssetGUID::tryParse( node.getAttributeText( "from" ) );
            const optional<AssetGUID> to     = AssetGUID::tryParse( node.getAttributeText( "to" ) );
            const bool                bBad   = from.has_value() == false || to.has_value() == false || *from == *to;
            const bool                bTwice = bBad == false && VectorUtil::containsIf( listRow, [&from]( const AssetIdentityRedirect& row )
                           { return row._from == *from; } );
            if ( bBad || bTwice )
            {
                SW_LOG_ERROR_ID( "AssetRedirect.IdentityRowInvalid", "Asset redirect table '%#': <%# from='%#' to='%#'> needs two different GUIDs and one row per 'from'", sourceName, Internal::kIdentityRedirectElement,
                                 node.getAttributeText( "from" ), node.getAttributeText( "to" ) );
                return unexpected( malformed );
            }
            listRow.push_back( AssetIdentityRedirect{ *from, *to } );
        }
        return listRow;
    }

    string AssetRedirect::makeTableID( string_view assetID )
    {
        const size_t firstSlash = assetID.find( '/' );
        string_view  domain     = assetID.substr( 0, firstSlash );
        if ( firstSlash != string_view::npos && ( domain == "game" || domain == "dlc" ) )
            domain = assetID.substr( 0, assetID.find( '/', firstSlash + 1 ) ); // 팩 도메인은 두 마디다(`game/<팩>`) — 검색 순서의 "game" 이 활성 팩 뿌리다
        return FileUtil::joinPath( domain, kFileName );
    }

    bool AssetRedirect::isTable( string_view assetID )
    {
        return FileUtil::getFileNamePart( assetID ) == kFileName;
    }
} // namespace sw
