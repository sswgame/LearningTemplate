#include "pch.h"

#include "GameFramework/Kits/Feature/Content/ModSupport/ModPackBuilder.h"

#include "Core/Algorithm/VectorUtil.h"
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
        // 스트림 쓰기는 경로 해시 순만 받는다 — 경로를 먼저 다 보고(정규형 · 크기) 해시로 세운 뒤 하나씩 읽어 담는다(팩 전체를 들지 않는다).
        vector<std::pair<uint64, string>> listOrdered; // (경로 해시, 원본 파일 경로)
        listOrdered.reserve( listFilePath.size() );
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
            listOrdered.emplace_back( PackStreamWriter::makePathHash( relativePath ), filePath );
        }
        VectorUtil::sort( listOrdered, []( const std::pair<uint64, string>& left, const std::pair<uint64, string>& right )
        { return left.first < right.first; } );

        PackWriterDesc desc; // 도메인 없음 — 항목이 전체 리소스 경로인 패치 팩이다(`mods/<id>/…` 와 덮어쓰는 기본 리소스가 한 팩에 든다)
        desc._blockSize        = settings._packBlockSize;
        desc._writeWindowBytes = settings._maxEntryBytes; // 창은 항목 하나의 한계와 같다 — 모드 팩의 메모리 상한이 이미 받아들인 값이다
        PackStreamWriter writer;
        expected<void>   step = writer.open( packPath, desc );
        for ( size_t index = 0; index < listOrdered.size() && step.has_value(); ++index )
        {
            const string& filePath = listOrdered[index].second;
            string        relativePath;
            vector<uint8> bytes;
            if ( FileUtil::makeRelativePath( contentFolder, filePath, relativePath ) == false || FileUtil::readFile( filePath, bytes ) == false )
            {
                SW_LOG_ERROR_ID( "ModPackBuilder.CannotRead", "Mod source file '%#' could not be read", filePath );
                return false;
            }
            step = writer.append( FileUtil::normalizeSeparators( relativePath ), std::move( bytes ) );
        }
        const expected<PackWriteResult> closed = step.has_value() ? writer.close() : expected<PackWriteResult>( unexpected( step.error() ) );
        if ( closed.has_value() == false )
        {
            SW_LOG_ERROR_ID( "ModPackBuilder.CannotWrite", "Mod pack '%#' could not be written: %# '%#'", packPath, closed.error().message(), closed.error().detail() );
            return false;
        }
        return true;
    }
} // namespace sw
