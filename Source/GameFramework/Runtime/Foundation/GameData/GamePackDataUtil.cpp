#include "pch.h"

#include "GameFramework/Runtime/Foundation/GameData/GamePackDataUtil.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"

#include "Engine/Content/AssetTypeRegistry.h"

namespace sw
{
    bool GamePackDataUtil::tryMakeKindPath( string_view packRoot, string_view kindLabel, string& outPath, string& outError )
    {
        outPath.clear();
        outError.clear();
        const AssetTypeDef* pKind = AssetTypeRegistry::findType( kindLabel );
        if ( pKind == nullptr )
        {
            outError = "data kind '" + string( kindLabel ) + "' is not registered (SW_ASSET_TYPE)";
            return false;
        }
        const string pack = FileUtil::trimTrailingSlashes( packRoot );
        if ( pack.empty() )
        {
            outError = "data kind '" + string( kindLabel ) + "' needs a pack root, but the game has none";
            return false;
        }
        const AssetPathRule& rule = pKind->_pathRule;
        if ( rule._prefix.empty() == false && StringUtil::startsWith( pack, rule._prefix, true ) == false )
        {
            outError = "pack '" + pack + "' is outside the '" + string( rule._prefix ) + "' prefix of data kind '" + string( kindLabel ) + "'";
            return false;
        }
        const bool bFileKind = rule._suffix.empty() == false && rule._suffix.front() == '/';
        if ( bFileKind )
        {
            outPath = pack + string( rule._suffix );
            return true;
        }
        const bool bFolderKind = rule._contains.empty() == false && rule._contains.front() == '/';
        if ( bFolderKind )
        {
            outPath = pack + FileUtil::trimTrailingSlashes( rule._contains );
            return true;
        }
        outError = "data kind '" + string( kindLabel ) + "' has no in-pack path rule (a '/...' suffix or a '/.../' folder)";
        return false;
    }

    bool GamePackDataUtil::tryMakeSiblingKindPath( string_view assetID, string_view fromKind, string_view toKind, string& outPath, string& outError )
    {
        outPath.clear();
        outError.clear();
        const AssetTypeDef* pFrom = AssetTypeRegistry::findType( fromKind );
        const AssetTypeDef* pTo   = AssetTypeRegistry::findType( toKind );
        if ( pFrom == nullptr || pTo == nullptr )
        {
            outError = "data kind '" + string( pFrom == nullptr ? fromKind : toKind ) + "' is not registered (SW_ASSET_TYPE)";
            return false;
        }
        const string_view fromSuffix = pFrom->_pathRule._suffix;
        const string_view toSuffix   = pTo->_pathRule._suffix;
        const bool        bFileKinds = fromSuffix.empty() == false && fromSuffix.front() == '/' && toSuffix.empty() == false && toSuffix.front() == '/';
        if ( bFileKinds == false )
        {
            outError = "data kinds '" + string( fromKind ) + "' and '" + string( toKind ) + "' must both be in-pack files (a '/...' suffix)";
            return false;
        }
        if ( StringUtil::equals( pFrom->_pathRule._prefix, pTo->_pathRule._prefix, true ) == false )
        {
            outError = "data kinds '" + string( fromKind ) + "' and '" + string( toKind ) + "' have different prefixes - they do not share packs";
            return false;
        }
        if ( StringUtil::endsWith( assetID, fromSuffix, true ) == false || assetID.size() == fromSuffix.size() )
        {
            outError = "'" + string( assetID ) + "' does not end with the '" + string( fromSuffix ) + "' suffix of data kind '" + string( fromKind ) + "'";
            return false;
        }
        outPath = string( assetID.substr( 0, assetID.size() - fromSuffix.size() ) ) + string( toSuffix );
        return true;
    }
} // namespace sw
