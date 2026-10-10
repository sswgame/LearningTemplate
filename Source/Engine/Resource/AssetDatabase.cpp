#include "pch.h"

#include "Engine/Resource/AssetDatabase.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"
#include "Core/UUID/UUID.h"

#include "Engine/Common/EngineDefines.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Utility/KeyValueFile.h"

namespace sw
{
    namespace
    {
        struct AssetDatabaseInternal
        {
            static string absoluteForRelative( string_view relativePath )
            {
                string result = ResourceUtil::getResourcePath( relativePath );
                if ( result.empty() )
                {
                    // 쓰기 쪽: 도메인을 포함한 전역 ID 만으로 절대 경로를 만든다.
                    result = ResourceUtil::makeAbsolutePath( relativePath );
                }
                return result;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "AssetDatabase" );

    string AssetDatabase::toRelativePath( string_view absolutePath )
    {
        const string  absNorm      = FileUtil::normalizeSeparators( absolutePath );
        const string& resourceRoot = ResourceUtil::getRootFolderPath();
        if ( resourceRoot.empty() )
            return {};

        string rootNorm = FileUtil::trimTrailingSlashes( FileUtil::normalizeSeparators( resourceRoot ) );

        string rel;
        if ( FileUtil::makeRelativePath( rootNorm, absNorm, rel ) == false )
        {
            rel.clear();
            return rel;
        }
        rel = FileUtil::normalizePath( rel );
        if ( rel.empty() || rel == "." )
            rel.clear();
        return rel;
    }

    bool AssetDatabase::deleteAssetFile( string_view absolutePath )
    {
        if ( absolutePath.empty() )
            return false;
        const string abs{ absolutePath };
        if ( FileUtil::removeFile( abs ) == false )
        {
            SW_LOG_ERROR( "Could not delete '%#' - it and its .meta are kept", abs );
            return false;
        }
        const string metaPath = metaPathFor( abs );
        if ( FileUtil::exists( metaPath ) && FileUtil::removeFile( metaPath ) == false )
            SW_LOG_WARNING( "Deleted '%#' but its .meta could not be removed", abs );
        return true;
    }

    string AssetDatabase::metaPathFor( string_view relativePath )
    {
        string p( relativePath );
        string result;
        if ( FileUtil::hasExtension( p, path::kMetaExtension ) )
            result = std::move( p );
        else
            result = p + path::kMetaExtension;
        return result;
    }

    UUID AssetDatabase::ensureMeta( string_view relativePath, bool bImported )
    {
        UUID   result{};
        string path = FileUtil::normalizePath( relativePath );
        if ( path.empty() || FileUtil::hasExtension( path, path::kMetaExtension ) )
            return result;

        // **리소스 루트 밖의 절대 경로에는 식별자를 주지 않는다**(null GUID). 그런 경로(테스트의 임시 프리팹, 사용자가 연 바깥 파일)에
        // `.meta` 를 쓰면 프로젝트 밖 폴더에 사이드카가 흘러나가고, 대소문자를 가리는 파일 시스템에서는 원래 파일 옆도 아닌 자리가 된다.
        // 루트 밖 에셋은 씬이 GUID 로 다시 찾을 수도 없다(유니티도
        // `Assets/` 밖에는 .meta 를 만들지 않는다). 판정은 정규화 **전** 경로로 한다 — 루트와 대소문자를 견줘야 한다.
        if ( FileUtil::isAbsolutePath( relativePath ) )
        {
            string rootRelative = toRelativePath( relativePath );
            if ( rootRelative.empty() || StringUtil::startsWith( rootRelative, ".." ) )
                return result;
            // 루트 안이면 키는 그 전역 id 다. 절대 경로를 키로 쓰면 같은 에셋이 id 키와 절대 경로 키를 따로 갖고 GUID → 경로가
            // 기계마다 다른 절대 경로를 돌려준다(유니티 `AssetDatabase` 의 키도 프로젝트 상대 경로다).
            path = std::move( rootRelative );
        }

        BLOCK( "Check Existing Meta" )
        {
            std::shared_lock<std::shared_mutex> lock{ _mutex };
            const auto                          it = _mapPathToGuid.find( path );
            if ( it != _mapPathToGuid.end() )
            {
                result = it->second;
                return result;
            }
        }

        BLOCK( "Load or Generate Meta" )
        {
            bool importedFlag = bImported;
            if ( loadMetaFile( path, result, &importedFlag ) == false )
            {
#if defined( SW_SHIPPING )
                // 배포 빌드는 .meta 를 팩에 넣지 않는다(PackConfig 의 `*.meta` 제외). 여기서 GUID 를 지어내 쓰면
                // 실행마다 다른 GUID 가 되고, 그 파일은 팩 옆(개발 PC 에서는 소스 트리 Resource/)에 떨어져 작업 트리를 더럽힌다.
                // 배포본에서 에셋 식별자는 쿠킹 때 정해진 것만 쓴다. 없으면 없는 것이다
                // (부르는 쪽은 모두 null GUID 를 허용한다. 씬 · 프리팹 로더는 경로로 물러난다).
                return result;
#else
                // 에셋 파일이 없는 경로에는 식별자를 지어내지 않는다. 지어 쓰면 없는 머티리얼을 acquire 만 해도 Resource/ 에 고아 `.meta` 가 생긴다.
                if ( ResourceUtil::getResourcePath( path ).empty() )
                    return result;
                result = UUID::generate();
                if ( writeMetaFile( path, result, bImported ) == false )
                {
                    result = {};
                    return result;
                }
#endif
            }
        }

        BLOCK( "Register Meta" )
        {
            std::unique_lock<std::shared_mutex> lock{ _mutex };
            _mapPathToGuid[path]   = result;
            _mapGuidToPath[result] = path;
        }

        return result;
    }

    void AssetDatabase::registerMapping( string_view relativePath, const UUID& guid )
    {
        string path = FileUtil::normalizePath( relativePath );
        if ( path.empty() || guid.isNull() )
            return;

        std::unique_lock<std::shared_mutex> lock{ _mutex };
        _mapPathToGuid[path] = guid;
        _mapGuidToPath[guid] = path;
    }

    bool AssetDatabase::registerExisting( string_view relativePath )
    {
        string path = FileUtil::normalizePath( relativePath );
        if ( path.empty() || FileUtil::hasExtension( path, path::kMetaExtension ) )
            return false;

        UUID guid{};
        if ( loadMetaFile( path, guid ) == false )
            return false;

        std::unique_lock<std::shared_mutex> lock{ _mutex };
        _mapPathToGuid[path] = guid;
        _mapGuidToPath[guid] = path;
        return true;
    }

    bool AssetDatabase::tryGetGuid( string_view relativePath, UUID& outGuid ) const
    {
        // **넣을 때 정규화했으면 찾을 때도 정규화해야 한다.** `ensureMeta` · `registerMapping` ·
        // `registerExisting` 은 모두 `normalizePath` 를 거친 키를 넣는다(소문자 · `/` 구분자). 받은 문자열을 그대로 찾으면
        // 씬 XML 의 `prefab` 경로처럼 대문자가 섞인 값으로 물을 때 등록돼 있어도 못 찾는다.
        const string path = FileUtil::normalizePath( relativePath );

        std::shared_lock<std::shared_mutex> lock{ _mutex };
        const auto                          it = _mapPathToGuid.find( path );
        if ( it == _mapPathToGuid.end() )
            return false;
        outGuid = it->second;
        return true;
    }

    bool AssetDatabase::tryGetPath( const UUID& guid, string& outPath ) const
    {
        std::shared_lock<std::shared_mutex> lock{ _mutex };
        const auto                          it = _mapGuidToPath.find( guid );
        if ( it == _mapGuidToPath.end() )
            return false;
        outPath = it->second;
        return true;
    }

    size_t AssetDatabase::getAssetCount() const
    {
        std::shared_lock<std::shared_mutex> lock{ _mutex };
        return _mapPathToGuid.size();
    }

    uint32 AssetDatabase::refreshFolder( string_view absoluteFolder, bool bCreateMissing )
    {
        uint32 count{ 0 };
        if ( FileUtil::isDirectory( absoluteFolder ) == false )
            return 0;

        vector<string> listFile;
        FileUtil::collectFiles( absoluteFolder, {}, listFile, false );
        for ( const string& filePath : listFile )
        {
            string rel;
            BLOCK( "Filter and Normalize Path" )
            {
                const string name = FileUtil::getFileNamePart( filePath );
                if ( FileUtil::hasExtension( name, path::kMetaExtension ) )
                    continue;

                const string abs = FileUtil::normalizeSeparators( filePath );
                rel              = toRelativePath( abs );
            }

            if ( rel.empty() )
                continue;

            BLOCK( "Process Asset" )
            {
                if ( bCreateMissing )
                {
                    if ( ensureMeta( rel, false ).isNull() == false )
                        ++count;
                }
                else if ( registerExisting( rel ) )
                    ++count;
            }
        }
        return count;
    }

    uint32 AssetDatabase::scanMetaFiles( string_view absoluteRoot )
    {
        if ( FileUtil::isDirectory( absoluteRoot ) == false )
            return 0;

        // 절대 경로는 대소문자를 **보존**해서 받아야 한다(refreshFolder 와 같은 규칙). normalizePath 는
        // 경로 전체를 소문자로 내리는데, 그러면 실제 디렉터리 이름이 섞여 있는 구간(예: .../LearningTemplate/Resource)
        // 까지 소문자가 되어 대소문자를 가리는 파일시스템에서 toRelativePath 의 루트 비교가 어긋난다.
        // 소문자화는 상대 경로가 된 뒤 toRelativePath 안에서 한 번만 한다.
        vector<string> listMeta;
        FileUtil::collectFiles( absoluteRoot, path::kMetaExtension, listMeta, true );

        uint32 count{ 0 };
        for ( const string& metaAbs : listMeta )
        {
            const string metaRel = toRelativePath( metaAbs );
            if ( metaRel.size() <= string_view( path::kMetaExtension ).size() )
                continue;
            const string assetRel = metaRel.substr( 0, metaRel.size() - string_view( path::kMetaExtension ).size() );
            if ( registerExisting( assetRel ) )
                ++count;
        }
        return count;
    }

    uint32 AssetDatabase::loadRegistry( string_view registryRelativePath )
    {
        if ( ResourceUtil::hasResource( registryRelativePath ) == false )
            return 0;
        string text;
        if ( ResourceUtil::readTextResource( registryRelativePath, text ) == false )
            return 0;
        return loadRegistryText( text );
    }

    string AssetDatabase::makeRegistryText( string_view resourceRoot, string_view domain, uint32& outFailedCount )
    {
        const string   domainRoot = FileUtil::joinPath( FileUtil::trimTrailingSlashes( FileUtil::normalizeSeparators( resourceRoot ) ), domain );
        vector<string> listMeta;
        FileUtil::collectFiles( domainRoot, path::kMetaExtension, listMeta, true );
        std::sort( listMeta.begin(), listMeta.end() );

        const string_view                       metaExtension( path::kMetaExtension );
        const string                            domainPrefix = FileUtil::normalizePath( domain );
        StringBuilder<constant::kMaxBuffer8192> sb;
        sb.append( "# <guid> <sourcePath> - AssetDatabase::makeRegistryText (path = where the .meta sits)\n" );
        uint32 lineCount{ 0 };
        for ( const string& metaAbs : listMeta )
        {
            const string normalizedMeta = FileUtil::normalizeSeparators( metaAbs );
            if ( normalizedMeta.size() <= domainRoot.size() + 1 + metaExtension.size() )
                continue;
            const string assetUnderDomain = normalizedMeta.substr( domainRoot.size() + 1, normalizedMeta.size() - domainRoot.size() - 1 - metaExtension.size() );

            KeyValueMap mapData;
            UUID        guid{};
            const utf8* pGuidText = nullptr;
            if ( KeyValueFile::loadFile( metaAbs, mapData ) )
                pGuidText = KeyValueFile::get( mapData, "guid", nullptr );
            if ( pGuidText == nullptr || UUID::tryParse( pGuidText, guid ) == false || guid.isNull() )
            {
                SW_LOG_ERROR( "Asset registry: '%#' has no readable guid - the asset is left out of the shipped registry", metaAbs );
                ++outFailedCount;
                continue;
            }
            sb.append( guid.toString().c_str() ).append( ' ' ).append( domainPrefix ).append( '/' ).append( FileUtil::normalizePath( assetUnderDomain ) ).append( '\n' );
            ++lineCount;
        }
        return lineCount > 0 ? string( sb.view() ) : string{};
    }

    uint32 AssetDatabase::writeRegistryFiles( string_view resourceRoot, string_view cookedDir, uint32& outFailedCount )
    {
        // 도메인 = 루트의 폴더 하나, `game` 은 그 아래 폴더 하나씩(팩 하나가 도메인 하나다 — `CookAssets.py` `cookAllPacks` 와 같은 나눔).
        vector<string> listDomain;
        vector<string> listTopFolder;
        (void)FileUtil::collectFolders( resourceRoot, listTopFolder, false ); // 루트가 없으면 도메인이 없다 — 아래가 아무것도 쓰지 않는다
        std::sort( listTopFolder.begin(), listTopFolder.end() );
        for ( const string& topFolder : listTopFolder )
        {
            const string name = FileUtil::getFileNamePart( FileUtil::trimTrailingSlashes( topFolder ) );
            if ( StringUtil::equals( name, path::kGamePack, true ) == false )
            {
                listDomain.push_back( name );
                continue;
            }
            vector<string> listGameFolder;
            (void)FileUtil::collectFolders( topFolder, listGameFolder, false ); // 방금 찾은 폴더다
            std::sort( listGameFolder.begin(), listGameFolder.end() );
            for ( const string& gameFolder : listGameFolder )
            {
                listDomain.push_back( string( path::kGamePack ) + "/" + FileUtil::getFileNamePart( FileUtil::trimTrailingSlashes( gameFolder ) ) );
            }
        }

        uint32 writtenCount{ 0 };
        for ( const string& domain : listDomain )
        {
            const string text = makeRegistryText( resourceRoot, domain, outFailedCount );
            if ( text.empty() )
                continue;
            const string outputPath = FileUtil::joinPath( FileUtil::joinPath( cookedDir, domain ), "assetregistry.txt" );
            FileUtil::ensureParentDirectoryExists( outputPath );
            if ( FileUtil::writeTextFile( outputPath, text ) == false )
            {
                SW_LOG_ERROR( "Asset registry: could not write '%#'", outputPath );
                ++outFailedCount;
                continue;
            }
            ++writtenCount;
        }
        return writtenCount;
    }

    uint32 AssetDatabase::loadRegistryText( string_view text )
    {
        uint32 count{ 0 };
        size_t lineStart{ 0 };
        while ( lineStart < text.size() )
        {
            size_t lineEnd = text.find( '\n', lineStart );
            if ( lineEnd == string_view::npos )
                lineEnd = text.size();
            string_view line = text.substr( lineStart, lineEnd - lineStart );
            lineStart        = lineEnd + 1;

            line = StringUtil::trimEnd( line );
            if ( line.empty() || line.front() == '#' )
                continue;

            const size_t space = line.find( ' ' );
            if ( space == string_view::npos || space + 1 >= line.size() )
                continue;

            UUID guid{};
            if ( UUID::tryParse( string( line.substr( 0, space ) ).c_str(), guid ) == false || guid.isNull() )
                continue;

            registerMapping( line.substr( space + 1 ), guid );
            ++count;
        }
        return count;
    }

    void AssetDatabase::clear()
    {
        std::unique_lock<std::shared_mutex> lock{ _mutex };
        _mapPathToGuid.clear();
        _mapGuidToPath.clear();
    }

    bool AssetDatabase::writeMetaFile( string_view relativePath, const UUID& guid, bool bImported ) const
    {
        const string metaRel = metaPathFor( relativePath );
        const string absMeta = AssetDatabaseInternal::absoluteForRelative( metaRel );
        if ( absMeta.empty() )
        {
            SW_LOG_WARNING( "Cannot resolve meta path for %#", relativePath );
            return false;
        }

        StringBuilder<constant::kMaxBuffer1024> sb;
        sb.appendFormat( "guid=%#\nsourcePath=%#\nimported=%#\n",
                         guid.toString(),
                         relativePath,
                         bImported ? 1 : 0 );

        FileUtil::ensureParentDirectoryExists( absMeta );
        return FileUtil::writeTextFile( absMeta, sb.view() );
    }

    bool AssetDatabase::loadMetaFile( string_view relativePath, UUID& outGuid, bool* pOutImported ) const
    {
        const string metaRel = metaPathFor( relativePath );
        if ( ResourceUtil::hasResource( metaRel ) == false )
            return false;

        KeyValueMap mapData;
        if ( KeyValueFile::loadResource( metaRel, mapData ) == false )
            return false;

        const utf8* pGuidStr = KeyValueFile::get( mapData, "guid", nullptr );
        if ( pGuidStr == nullptr || UUID::tryParse( pGuidStr, outGuid ) == false || outGuid.isNull() )
            return false;
        if ( pOutImported != nullptr )
            *pOutImported = KeyValueFile::getInt( mapData, "imported", 0 ) != 0;
        return true;
    }
} // namespace sw
