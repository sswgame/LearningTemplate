#include "pch.h"

#include "GameFramework/Base/Foundation/Data/GamePackDataUtil.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"

#include "Engine/Asset/AssetDataKind.h"

namespace sw
{
    bool GamePackDataUtil::tryMakeKindPath( string_view packRoot, string_view kindLabel, string& outPath, string& outError )
    {
        outPath.clear();
        outError.clear();
        const AssetDataKind* pKind = AssetDataKindUtil::findByLabel( kindLabel );
        if ( pKind == nullptr )
        {
            outError = "data kind '" + string( kindLabel ) + "' is not registered (SW_RESOURCE_DATA_KIND)";
            return false;
        }
        const string pack = FileUtil::trimTrailingSlashes( packRoot );
        if ( pack.empty() )
        {
            outError = "data kind '" + string( kindLabel ) + "' needs a pack root, but the game has none";
            return false;
        }
        if ( pKind->_pPrefix != nullptr && StringUtil::startsWith( pack, string_view( pKind->_pPrefix ), true ) == false )
        {
            outError = "pack '" + pack + "' is outside the '" + pKind->_pPrefix + "' prefix of data kind '" + string( kindLabel ) + "'";
            return false;
        }
        const bool bFileKind = pKind->_pSuffix != nullptr && pKind->_pSuffix[0] == '/';
        if ( bFileKind )
        {
            outPath = pack + pKind->_pSuffix;
            return true;
        }
        const bool bFolderKind = pKind->_pContains != nullptr && pKind->_pContains[0] == '/';
        if ( bFolderKind )
        {
            outPath = pack + FileUtil::trimTrailingSlashes( pKind->_pContains );
            return true;
        }
        outError = "data kind '" + string( kindLabel ) + "' has no in-pack path rule (a '/...' suffix or a '/.../' folder)";
        return false;
    }
} // namespace sw
