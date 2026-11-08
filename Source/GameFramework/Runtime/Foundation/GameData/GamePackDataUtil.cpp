#include "pch.h"

#include "GameFramework/Runtime/Foundation/GameData/GamePackDataUtil.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Content/AssetTypeRegistry.h"

namespace sw
{
    expected<void> GamePackDataUtil::tryMakeKindPath( string_view packRoot, string_view kindLabel, string& outPath )
    {
        outPath.clear();
        const AssetTypeDef* pKind = AssetTypeRegistry::findType( kindLabel );
        if ( pKind == nullptr )
            return unexpected( Error{ ErrorKind::NotFound, "the data kind is not registered (SW_ASSET_TYPE)", kindLabel } );
        const string pack = FileUtil::trimTrailingSlashes( packRoot );
        if ( pack.empty() )
            return unexpected( Error{ ErrorKind::InvalidArgument, "the data kind needs a pack root, but the game has none", kindLabel } );
        const AssetPathRule& rule = pKind->_pathRule;
        if ( rule._prefix.empty() == false && StringUtil::startsWith( pack, rule._prefix, true ) == false )
        {
            Error error{ ErrorKind::InvalidArgument, "the pack is outside the prefix of the data kind (pack, prefix, kind)", pack };
            error.appendDetail( ", " ).appendDetail( rule._prefix ).appendDetail( ", " ).appendDetail( kindLabel );
            return unexpected( error );
        }
        const bool bFileKind = rule._suffix.empty() == false && rule._suffix.front() == '/';
        if ( bFileKind )
        {
            outPath = pack + string( rule._suffix );
            return {};
        }
        const bool bFolderKind = rule._contains.empty() == false && rule._contains.front() == '/';
        if ( bFolderKind )
        {
            outPath = pack + FileUtil::trimTrailingSlashes( rule._contains );
            return {};
        }
        return unexpected( Error{ ErrorKind::Unsupported, "the data kind has no in-pack path rule (a '/...' suffix or a '/.../' folder)", kindLabel } );
    }

    expected<void> GamePackDataUtil::tryMakeSiblingKindPath( string_view assetID, string_view fromKind, string_view toKind, string& outPath )
    {
        outPath.clear();
        const AssetTypeDef* pFrom = AssetTypeRegistry::findType( fromKind );
        const AssetTypeDef* pTo   = AssetTypeRegistry::findType( toKind );
        if ( pFrom == nullptr || pTo == nullptr )
            return unexpected( Error{ ErrorKind::NotFound, "the data kind is not registered (SW_ASSET_TYPE)", pFrom == nullptr ? fromKind : toKind } );
        const string_view fromSuffix = pFrom->_pathRule._suffix;
        const string_view toSuffix   = pTo->_pathRule._suffix;
        const bool        bFileKinds = fromSuffix.empty() == false && fromSuffix.front() == '/' && toSuffix.empty() == false && toSuffix.front() == '/';
        if ( bFileKinds == false )
        {
            Error error{ ErrorKind::Unsupported, "both data kinds must be in-pack files (a '/...' suffix)", fromKind };
            error.appendDetail( ", " ).appendDetail( toKind );
            return unexpected( error );
        }
        if ( StringUtil::equals( pFrom->_pathRule._prefix, pTo->_pathRule._prefix, true ) == false )
        {
            Error error{ ErrorKind::InvalidArgument, "the data kinds have different prefixes - they do not share packs", fromKind };
            error.appendDetail( ", " ).appendDetail( toKind );
            return unexpected( error );
        }
        if ( StringUtil::endsWith( assetID, fromSuffix, true ) == false || assetID.size() == fromSuffix.size() )
        {
            Error error{ ErrorKind::InvalidArgument, "the asset id does not end with the suffix of its data kind (id, suffix, kind)", assetID };
            error.appendDetail( ", " ).appendDetail( fromSuffix ).appendDetail( ", " ).appendDetail( fromKind );
            return unexpected( error );
        }
        outPath = string( assetID.substr( 0, assetID.size() - fromSuffix.size() ) ) + string( toSuffix );
        return {};
    }
} // namespace sw
