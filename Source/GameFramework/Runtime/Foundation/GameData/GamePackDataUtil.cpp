#include "pch.h"

#include "GameFramework/Runtime/Foundation/GameData/GamePackDataUtil.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"

#include "Engine/Foundation/AssetTypeRegistry.h"

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
} // namespace sw
