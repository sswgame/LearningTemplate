#include "pch.h"

#include "GameFramework/Kits/Feature/Content/ModSupport/ModPackBuilder.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"

#include "Engine/Asset/AssetDatabase.h"
#include "Engine/Asset/AssetPackIndex.h"
#include "Engine/Config/PackStorageSettings.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Content/AssetRegistry.h"
#include "Engine/Content/Pack/PackConfig.h"
#include "Engine/Content/Pack/PackWriter.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialTagIndex.h"

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

    bool ModPackBuilder::addMaterialTags( const string& relativePath, const vector<uint8>& bytes, MaterialTagIndex& inoutIndex )
    {
        if ( StringUtil::endsWith( relativePath, Material::kExtension ) == false )
            return true;
        const string_view          text( reinterpret_cast<const utf8*>( bytes.data() ), bytes.size() );
        const shared_ptr<Material> material = Material::create();
        if ( material->loadFromXML( text ) == false )
        {
            SW_LOG_ERROR_ID( "ModPackBuilder.MaterialUnreadable", "Mod material '%#' could not be read for the material index", relativePath );
            return false;
        }
        // 열쇠는 팩 색인과 같은 참조 글이다 — 머리 GUID 가 있으면 GUID 참조(덮어쓴 기본 머티리얼은 기본 줄을 이긴다), 없으면 전체 에셋 id.
        const AssetGUIDHeader header = AssetGUIDHeader::read( text );
        inoutIndex._listEntry.push_back( MaterialTagIndex::makeEntry( AssetRegistry::makeReferenceText( relativePath, header._guid ), *material ) );
        return true;
    }

    bool ModPackBuilder::buildFromFolder( const AbsolutePath& contentFolder, const AbsolutePath& packPath, const ModSupportSettings& settings, const PackStorageSettings& storage )
    {
        vector<AbsolutePath> listFilePath;
        if ( FileUtil::collectFiles( contentFolder, {}, listFilePath, true ) == false )
        {
            SW_LOG_ERROR_ID( "ModPackBuilder.CannotList", "Mod source folder '%#' could not be listed", contentFolder.str() );
            return false;
        }
        if ( listFilePath.size() > settings._maxEntryCount )
        {
            SW_LOG_ERROR_ID( "ModPackBuilder.TooManyFiles", "Mod source folder '%#' has %# files, more than the %# a mod pack may hold", contentFolder.str(), listFilePath.size(),
                             settings._maxEntryCount );
            return false;
        }
        AssetPackIndex   identityIndex; // 모드 에셋의 GUID 색인 — 런타임이 도메인 색인 뒤 층으로 합친다(`AssetManager`, 줄은 전체 에셋 id)
        MaterialTagIndex materialIndex; // 모드 머티리얼의 태그 색인 — 같은 층 규칙(`MaterialTagIndex::loadMounted`)
        // 스트림 쓰기는 경로 해시 순만 받는다 — 경로를 먼저 다 보고(정규형 · 크기) 해시로 세운 뒤 하나씩 읽어 담는다(팩 전체를 들지 않는다).
        vector<std::pair<uint64, AbsolutePath>> listOrdered; // (경로 해시, 원본 파일 경로)
        listOrdered.reserve( listFilePath.size() );
        for ( const AbsolutePath& filePath : listFilePath )
        {
            string relativePath;
            if ( FileUtil::makeRelativePath( contentFolder.str(), filePath.str(), relativePath ) == false )
            {
                SW_LOG_ERROR_ID( "ModPackBuilder.OutsideFolder", "Mod source file '%#' is not under '%#'", filePath.str(), contentFolder.str() );
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
        VectorUtil::sort( listOrdered, []( const std::pair<uint64, AbsolutePath>& left, const std::pair<uint64, AbsolutePath>& right )
        { return left.first < right.first; } );

        // 정체성 색인(모드 에셋의 GUID — 런타임이 도메인 색인 뒤 층으로 합친다, `AssetManager`)은 모든 파일을 본 뒤에야 정해진다. 스트림 쓰기는 경로 해시 순만 받으므로
        // 색인을 먼저 만들어 그 경로 해시 자리에 끼운다(파일을 한 번 더 읽는다 — 모드 팩은 항목 수 · 크기 상한 안이다).
        for ( const std::pair<uint64, AbsolutePath>& ordered : listOrdered )
        {
            string        relativePath;
            vector<uint8> bytes;
            if ( FileUtil::makeRelativePath( contentFolder.str(), ordered.second.str(), relativePath ) == false || FileUtil::readFile( ordered.second, bytes ) == false )
            {
                SW_LOG_ERROR_ID( "ModPackBuilder.CannotRead", "Mod source file '%#' could not be read", ordered.second.str() );
                return false;
            }
            const string normalizedPath = FileUtil::normalizeSeparators( relativePath );
            addIdentity( normalizedPath, bytes, identityIndex );
            if ( addMaterialTags( normalizedPath, bytes, materialIndex ) == false )
                return false;
        }
        // 만든 색인(팩 루트의 글 파일) — 빈 원본 경로 항목이 경로 해시로 이것을 찾는다.
        vector<std::pair<string_view, vector<uint8>>> listGenerated;
        if ( identityIndex._listEntry.empty() == false )
        {
            const expected<string> indexText = identityIndex.makeText();
            if ( indexText.has_value() == false )
            {
                SW_LOG_ERROR_ID( "ModPackBuilder.IndexInvalid", "Mod '%#' asset index - %# '%#'", contentFolder.str(), indexText.error().message(), indexText.error().detail() );
                return false;
            }
            listGenerated.emplace_back( AssetPackIndex::kFileName, vector<uint8>( indexText->begin(), indexText->end() ) );
        }
        if ( materialIndex._listEntry.empty() == false )
        {
            const expected<string> indexText = materialIndex.makeText();
            if ( indexText.has_value() == false )
            {
                SW_LOG_ERROR_ID( "ModPackBuilder.MaterialIndexInvalid", "Mod '%#' material index - %# '%#'", contentFolder.str(), indexText.error().message(), indexText.error().detail() );
                return false;
            }
            listGenerated.emplace_back( MaterialTagIndex::kFileName, vector<uint8>( indexText->begin(), indexText->end() ) );
        }
        for ( const std::pair<string_view, vector<uint8>>& generated : listGenerated )
        {
            listOrdered.emplace_back( PackStreamWriter::makePathHash( generated.first ), AbsolutePath{} ); // 빈 원본 경로 = 만든 색인 항목
        }
        VectorUtil::sort( listOrdered, []( const std::pair<uint64, AbsolutePath>& left, const std::pair<uint64, AbsolutePath>& right )
        { return left.first < right.first; } );

        PackWriterDesc desc; // 도메인 없음 — 항목이 전체 에셋 경로인 패치 팩이다(`mods/<id>/…` 와 덮어쓰는 기본 리소스가 한 팩에 든다)
        desc._blockSize        = settings._packBlockSize;
        desc._writeWindowBytes = settings._maxEntryBytes; // 창은 항목 하나의 한계와 같다 — 모드 팩의 메모리 상한이 이미 받아들인 값이다
        PackStreamWriter writer;
        expected<void>   step = writer.open( packPath, desc );
        for ( size_t index = 0; index < listOrdered.size() && step.has_value(); ++index )
        {
            const AbsolutePath& filePath = listOrdered[index].second;
            if ( filePath.empty() )
            {
                const auto generatedIt = std::find_if( listGenerated.begin(), listGenerated.end(), [&listOrdered, index]( const std::pair<string_view, vector<uint8>>& generated )
                { return PackStreamWriter::makePathHash( generated.first ) == listOrdered[index].first; } );
                SW_ASSERT( generatedIt != listGenerated.end() ); // 빈 원본 경로는 위에서 만든 색인에만 쓴다
                const expected<PackEntryStorage> entryStorage = PackEntryStorageRule::select( generatedIt->first, storage._listStoredExtension );
                if ( entryStorage.has_value() == false )
                    return false;
                step = writer.append( generatedIt->first, *entryStorage, std::move( generatedIt->second ) );
                continue;
            }
            string        relativePath;
            vector<uint8> bytes;
            if ( FileUtil::makeRelativePath( contentFolder.str(), filePath.str(), relativePath ) == false || FileUtil::readFile( filePath, bytes ) == false )
            {
                SW_LOG_ERROR_ID( "ModPackBuilder.CannotRead", "Mod source file '%#' could not be read", filePath.str() );
                return false;
            }
            const string                     entryPath    = FileUtil::normalizeSeparators( relativePath );
            const expected<PackEntryStorage> entryStorage = PackEntryStorageRule::select( entryPath, storage._listStoredExtension ); // 쿠커와 같은 규칙
            if ( entryStorage.has_value() == false )
                return false; // 규칙이 이유를 남겼다
            step = writer.append( entryPath, *entryStorage, std::move( bytes ) );
        }
        const expected<PackWriteResult> closed = step.has_value() ? writer.close() : expected<PackWriteResult>( unexpected( step.error() ) );
        if ( closed.has_value() == false )
        {
            SW_LOG_ERROR_ID( "ModPackBuilder.CannotWrite", "Mod pack '%#' could not be written: %# '%#'", packPath.str(), closed.error().message(), closed.error().detail() );
            return false;
        }
        return true;
    }
} // namespace sw
