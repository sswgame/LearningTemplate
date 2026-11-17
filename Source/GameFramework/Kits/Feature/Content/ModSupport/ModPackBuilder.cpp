#include "pch.h"

#include "GameFramework/Kits/Feature/Content/ModSupport/ModPackBuilder.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"

#include "Engine/Asset/AssetDatabase.h"
#include "Engine/Asset/AssetPackIndex.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Content/Pack/PackWriter.h"

namespace sw
{
    void ModPackBuilder::addIdentity( const string& relativePath, const vector<uint8>& bytes, AssetPackIndex& inoutIndex )
    {
        const string_view text( reinterpret_cast<const utf8*>( bytes.data() ), bytes.size() );
        if ( StringUtil::endsWith( relativePath, AssetMeta::kExtension ) )
        {
            // 원본 곁의 서술 — 정체성은 원본의 것이다.
            AssetMeta                 meta;
            const optional<AssetGUID> guid = meta.parseXML( text, relativePath );
            if ( guid.has_value() )
                inoutIndex._listEntry.push_back( AssetPackIndexEntry{ *guid, relativePath.substr( 0, relativePath.size() - AssetMeta::kExtension.size() ) } );
            return;
        }
        const AssetGUIDHeader header = AssetGUIDHeader::read( text ); // 우리 형식 글의 머리 — 없으면 정체성이 없는 파일(바이너리 · 머리 없는 글)이다
        if ( header._state == AssetGUIDHeaderState::Found )
            inoutIndex._listEntry.push_back( AssetPackIndexEntry{ *header._guid, relativePath } );
    }

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
        // 스트림 쓰기는 경로 해시 순만 받는다 — 첫 번째 훑기에서 경로(정규형 · 크기)를 보고 파일을 하나씩 읽어 정체성 색인을 모은 뒤 해시로 세우고,
        // 두 번째 훑기에서 하나씩 다시 읽어 담는다. 색인 파일은 모든 파일을 본 뒤에야 바이트가 정해져 해시 순 자리에 끼워 넣는다(팩 전체를 들지 않는다).
        AssetPackIndex                    identityIndex; // 모드 에셋의 GUID 색인 — 런타임이 도메인 색인 뒤 층으로 합친다(`AssetManager`, 줄은 전체 리소스 id)
        vector<std::pair<uint64, string>> listOrdered;   // (경로 해시, 원본 파일 경로)
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
            vector<uint8> bytes;
            if ( FileUtil::readFile( filePath, bytes ) == false )
            {
                SW_LOG_ERROR_ID( "ModPackBuilder.CannotRead", "Mod source file '%#' could not be read", filePath );
                return false;
            }
            addIdentity( relativePath, bytes, identityIndex );
            listOrdered.emplace_back( PackStreamWriter::makePathHash( relativePath ), filePath );
        }
        vector<uint8> listIndexByte;
        if ( identityIndex._listEntry.empty() == false )
        {
            const expected<string> indexText = identityIndex.makeText();
            if ( indexText.has_value() == false )
            {
                SW_LOG_ERROR_ID( "ModPackBuilder.IndexInvalid", "Mod '%#' asset index - %# '%#'", contentFolder, indexText.error().message(), indexText.error().detail() );
                return false;
            }
            listIndexByte.assign( indexText->begin(), indexText->end() );
        }
        const uint64 indexPathHash = PackStreamWriter::makePathHash( AssetPackIndex::kFileName );
        bool         bIndexPending = listIndexByte.empty() == false;
        VectorUtil::sort( listOrdered, []( const std::pair<uint64, string>& left, const std::pair<uint64, string>& right )
        { return left.first < right.first; } );

        PackWriterDesc desc; // 도메인 없음 — 항목이 전체 리소스 경로인 패치 팩이다(`mods/<id>/…` 와 덮어쓰는 기본 리소스가 한 팩에 든다)
        desc._blockSize        = settings._packBlockSize;
        desc._writeWindowBytes = settings._maxEntryBytes; // 창은 항목 하나의 한계와 같다 — 모드 팩의 메모리 상한이 이미 받아들인 값이다
        PackStreamWriter writer;
        expected<void>   step = writer.open( packPath, desc );
        for ( size_t index = 0; index < listOrdered.size() && step.has_value(); ++index )
        {
            if ( bIndexPending && indexPathHash <= listOrdered[index].first ) // 같은 해시면 쓰기가 중복으로 거절한다
            {
                step          = writer.append( AssetPackIndex::kFileName, PackEntryStorage::Blocks, std::move( listIndexByte ) );
                bIndexPending = false;
                if ( step.has_value() == false )
                    break;
            }
            const string& filePath = listOrdered[index].second;
            string        relativePath;
            vector<uint8> bytes;
            if ( FileUtil::makeRelativePath( contentFolder, filePath, relativePath ) == false || FileUtil::readFile( filePath, bytes ) == false )
            {
                SW_LOG_ERROR_ID( "ModPackBuilder.CannotRead", "Mod source file '%#' could not be read", filePath );
                return false;
            }
            step = writer.append( FileUtil::normalizeSeparators( relativePath ), PackEntryStorage::Blocks, std::move( bytes ) );
        }
        if ( step.has_value() && bIndexPending )
            step = writer.append( AssetPackIndex::kFileName, PackEntryStorage::Blocks, std::move( listIndexByte ) );
        const expected<PackWriteResult> closed = step.has_value() ? writer.close() : expected<PackWriteResult>( unexpected( step.error() ) );
        if ( closed.has_value() == false )
        {
            SW_LOG_ERROR_ID( "ModPackBuilder.CannotWrite", "Mod pack '%#' could not be written: %# '%#'", packPath, closed.error().message(), closed.error().detail() );
            return false;
        }
        return true;
    }
} // namespace sw
