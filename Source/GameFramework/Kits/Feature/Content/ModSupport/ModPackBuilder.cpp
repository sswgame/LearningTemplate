#include "pch.h"

#include "GameFramework/Kits/Feature/Content/ModSupport/ModPackBuilder.h"

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
        AssetPackIndex identityIndex; // 모드 에셋의 GUID 색인 — 런타임이 도메인 색인 뒤 층으로 합친다(`AssetManager`, 줄은 전체 리소스 id)
        PackWriterDesc desc;          // 도메인 없음 — 항목이 전체 리소스 경로인 패치 팩이다(`mods/<id>/…` 와 덮어쓰는 기본 리소스가 한 팩에 든다)
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
            addIdentity( relativePath, bytes, identityIndex );
            writer.addBytes( relativePath, std::move( bytes ) );
        }
        if ( identityIndex._listEntry.empty() == false )
        {
            const expected<string> indexText = identityIndex.makeText();
            if ( indexText.has_value() == false )
            {
                SW_LOG_ERROR_ID( "ModPackBuilder.IndexInvalid", "Mod '%#' asset index - %# '%#'", contentFolder, indexText.error().message(), indexText.error().detail() );
                return false;
            }
            writer.addBytes( AssetPackIndex::kFileName, vector<uint8>( indexText->begin(), indexText->end() ) );
        }
        if ( writer.writePackFile( packPath ) == false )
        {
            SW_LOG_ERROR_ID( "ModPackBuilder.CannotWrite", "Mod pack '%#' could not be written", packPath );
            return false;
        }
        return true;
    }
} // namespace sw
