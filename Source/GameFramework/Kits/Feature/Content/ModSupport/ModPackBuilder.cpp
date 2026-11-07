#include "pch.h"

#include "GameFramework/Kits/Feature/Content/ModSupport/ModPackBuilder.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

#include "Engine/Content/AssetPaths.h"
#include "Engine/Content/Pack/PackWriter.h"

namespace sw
{
    bool ModPackBuilder::buildFromFolder( string_view contentFolder, string_view packPath, const ModSupportSettings& settings )
    {
        vector<string> listFilePath;
        if ( FileUtil::collectFiles( contentFolder, {}, listFilePath, true ) == false )
        {
            SW_LOG_ERROR_ID( "ModPackBuilder.CannotList", "Mod source folder '%#' could not be listed", contentFolder );
            return false;
        }
        if ( listFilePath.size() > settings._maxEntryCount )
        {
            SW_LOG_ERROR_ID( "ModPackBuilder.TooManyFiles", "Mod source folder '%#' has %# files, more than the %# a mod pack may hold", contentFolder, listFilePath.size(),
                             settings._maxEntryCount );
            return false;
        }
        PackWriterDesc desc; // 도메인 없음 — 항목이 전체 리소스 경로인 패치 팩이다(`mods/<id>/…` 와 덮어쓰는 기본 리소스가 한 팩에 든다)
        PackWriter     writer( desc );
        for ( const string& filePath : listFilePath )
        {
            string relativePath;
            if ( FileUtil::makeRelativePath( contentFolder, filePath, relativePath ) == false )
            {
                SW_LOG_ERROR_ID( "ModPackBuilder.OutsideFolder", "Mod source file '%#' is not under '%#'", filePath, contentFolder );
                return false;
            }
            relativePath = FileUtil::normalizeSeparators( relativePath );
            if ( AssetPaths::normalizeAssetID( relativePath ) != relativePath )
            {
                SW_LOG_ERROR_ID( "ModPackBuilder.BadPath", "Mod source file '%#' is not a resource id in normal form (lowercase, '/', no '..')", relativePath );
                return false;
            }
            uint64 size = 0;
            if ( FileUtil::tryGetFileSize( filePath, size ) == false || size > settings._maxEntryBytes )
            {
                SW_LOG_ERROR_ID( "ModPackBuilder.FileTooLarge", "Mod source file '%#' is unreadable or larger than %# bytes", relativePath, settings._maxEntryBytes );
                return false;
            }
            vector<uint8> bytes;
            if ( FileUtil::readFile( filePath, bytes ) == false )
            {
                SW_LOG_ERROR_ID( "ModPackBuilder.CannotRead", "Mod source file '%#' could not be read", filePath );
                return false;
            }
            writer.addBytes( relativePath, std::move( bytes ) );
        }
        if ( writer.writePackFile( packPath ) == false )
        {
            SW_LOG_ERROR_ID( "ModPackBuilder.CannotWrite", "Mod pack '%#' could not be written", packPath );
            return false;
        }
        return true;
    }
} // namespace sw
