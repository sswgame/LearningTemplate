#include "pch.h"

#include "Engine/Asset/AssetDatabase.h"

#include "Core/Concurrency/mutex.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"
#include "Core/UUID/UUID.h"

#include "Engine/Asset/AssetMeta.h"
#include "Engine/Asset/Cook/CookStore.h"
#include "Engine/FileSystem/AssetIO.h"
#include "Engine/FileSystem/AssetPaths.h"
#include "Engine/Foundation/EngineDefines.h"
#include "Engine/Foundation/EngineServices.h"

namespace sw
{
    SW_LOG_CALLER( "AssetDatabase" );

    string AssetDatabase::toRootAssetID( string_view absolutePath )
    {
        // 리소스 id 로 바꾸는 계산은 `AssetPaths::toAssetID` 하나다. 붙인 디스크 도메인(시나리오의 `saved`)은 리소스 루트 밖이라 `.meta` · GUID 를 주지 않는다.
        const AssetPaths& paths   = engine::getSubsystem<AssetPaths>();
        const string      assetID = paths.toAssetID( absolutePath );
        if ( assetID.empty() || paths.isMountedDiskDomainID( assetID ) )
            return {};
        return assetID;
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
        // `Assets/` 밖에는 .meta 를 만들지 않는다). 판정은 정규화 **전** 경로로 한다 — 루트와 대소문자를 비교해야 한다.
        if ( FileUtil::isAbsolutePath( relativePath ) )
        {
            string rootRelative = toRootAssetID( relativePath );
            if ( rootRelative.empty() || StringUtil::startsWith( rootRelative, ".." ) )
                return result;
            // 루트 안이면 키는 그 전역 id 다. 절대 경로를 키로 쓰면 같은 에셋이 id 키와 절대 경로 키를 따로 갖고 GUID → 경로가
            // 기계마다 다른 절대 경로를 돌려준다(유니티 `AssetDatabase` 의 키도 프로젝트 상대 경로다).
            path = std::move( rootRelative );
        }

        BLOCK( "Check Existing Meta" )
        {
            ScopedSharedLock<shared_mutex> lock{ _mutex };
            const auto                     it = _mapPathToGUID.find( path );
            if ( it != _mapPathToGUID.end() )
            {
                result = it->second;
                return result;
            }
        }

        BLOCK( "Load or Generate Meta" )
        {
            bool                      importedFlag = bImported;
            const AssetMetaReadResult metaResult   = loadMetaFile( path, result, &importedFlag );
            if ( metaResult == AssetMetaReadResult::Invalid )
            {
                // 깨진 .meta 를 새 GUID 로 덮어쓰지 않는다 — 그 에셋을 가리키던 씬 · 프리팹의 참조가 모두 끊긴다. 고칠 때까지 식별자가 없다(오류는 남겼다).
                result = {};
                return result;
            }
            if ( metaResult == AssetMetaReadResult::Missing )
            {
                // 쿠킹본 정책(배포 빌드 · `-cooked-content`)은 .meta 가 팩에 없다(PackConfig 의 `*.meta` 제외). 여기서 GUID 를 지어내 쓰면
                // 실행마다 다른 GUID 가 되고, 그 파일은 팩 옆(개발 PC 에서는 소스 트리 Content/)에 떨어져 작업 트리를 더럽힌다.
                // 쿠킹본에서 에셋 식별자는 쿠킹 때 정해진 것만 쓴다. 없으면 없는 것이다
                // (부르는 쪽은 모두 null GUID 를 허용한다. 씬 · 프리팹 로더는 경로로 물러난다).
                // 소스 정책도 에셋 파일이 없는 경로에는 식별자를 지어내지 않는다. 지어 쓰면 없는 머티리얼을 acquire 만 해도 Content/ 에 고아 `.meta` 가 생긴다.
                if ( AssetIO::getContentSource() == ContentSource::Cooked || engine::getSubsystem<AssetPaths>().findLooseFile( path ).getDiskPath().empty() )
                    return result;
                result = UUID::generate();
                if ( writeMetaFile( path, result, bImported ) == false )
                {
                    result = {};
                    return result;
                }
            }
        }

        BLOCK( "Register Meta" )
        {
            UniqueLock<shared_mutex> lock{ _mutex };
            _mapPathToGUID[path]   = result;
            _mapGUIDToPath[result] = path;
        }

        return result;
    }

    void AssetDatabase::registerMapping( string_view relativePath, const UUID& guid )
    {
        string path = FileUtil::normalizePath( relativePath );
        if ( path.empty() || guid.isNull() )
            return;

        UniqueLock<shared_mutex> lock{ _mutex };
        _mapPathToGUID[path] = guid;
        _mapGUIDToPath[guid] = path;
    }

    bool AssetDatabase::registerExisting( string_view relativePath )
    {
        string path = FileUtil::normalizePath( relativePath );
        if ( path.empty() || FileUtil::hasExtension( path, path::kMetaExtension ) )
            return false;

        UUID guid{};
        if ( loadMetaFile( path, guid ) != AssetMetaReadResult::Loaded )
            return false;

        UniqueLock<shared_mutex> lock{ _mutex };
        _mapPathToGUID[path] = guid;
        _mapGUIDToPath[guid] = path;
        return true;
    }

    bool AssetDatabase::tryGetGUID( string_view relativePath, UUID& outGUID ) const
    {
        // **넣을 때 정규화했으면 찾을 때도 정규화해야 한다.** `ensureMeta` · `registerMapping` ·
        // `registerExisting` 은 모두 `normalizePath` 를 거친 키를 넣는다(소문자 · `/` 구분자). 받은 문자열을 그대로 찾으면
        // 씬 XML 의 `prefab` 경로처럼 대문자가 섞인 값으로 물을 때 등록돼 있어도 못 찾는다.
        const string path = FileUtil::normalizePath( relativePath );

        ScopedSharedLock<shared_mutex> lock{ _mutex };
        const auto                     it = _mapPathToGUID.find( path );
        if ( it == _mapPathToGUID.end() )
            return false;
        outGUID = it->second;
        return true;
    }

    bool AssetDatabase::tryGetPath( const UUID& guid, string& outPath ) const
    {
        ScopedSharedLock<shared_mutex> lock{ _mutex };
        const auto                     it = _mapGUIDToPath.find( guid );
        if ( it == _mapGUIDToPath.end() )
            return false;
        outPath = it->second;
        return true;
    }

    size_t AssetDatabase::getAssetCount() const
    {
        ScopedSharedLock<shared_mutex> lock{ _mutex };
        return _mapPathToGUID.size();
    }

    uint32 AssetDatabase::refreshFolder( string_view absoluteFolder, bool bCreateMissing )
    {
        uint32 count{ 0 };
        if ( FileUtil::isDirectory( absoluteFolder ) == false )
            return 0;

        vector<string> listFile;
        // 더하기만 하는 새로고침이라 모은 데까지 등록한다 — 도중에 멈춘 것은 collectFiles 가 경로와 함께 알리고, 다음 새로고침이 나머지를 채운다.
        (void)FileUtil::collectFiles( absoluteFolder, {}, listFile, false );
        for ( const string& filePath : listFile )
        {
            string rel;
            BLOCK( "Filter and Normalize Path" )
            {
                const string name = FileUtil::getFileNamePart( filePath );
                if ( FileUtil::hasExtension( name, path::kMetaExtension ) )
                    continue;

                const string abs = FileUtil::normalizeSeparators( filePath );
                rel              = toRootAssetID( abs );
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
        // 경로 전체를 소문자로 내리는데, 그러면 실제 디렉터리 이름이 섞여 있는 구간(예: .../LearningTemplate/Content)
        // 까지 소문자가 되어 대소문자를 가리는 파일시스템에서 toRootAssetID 의 루트 비교가 어긋난다.
        // 소문자화는 상대 경로가 된 뒤 toRootAssetID 안에서 한 번만 한다.
        vector<string> listMeta;
        // 더하기만 하는 등록이라 모은 데까지 등록한다(`refreshFolder` 와 같다) — 도중에 멈춘 것은 collectFiles 가 경로와 함께 알린다.
        (void)FileUtil::collectFiles( absoluteRoot, path::kMetaExtension, listMeta, true );

        uint32 count{ 0 };
        for ( const string& metaAbs : listMeta )
        {
            const string metaRel = toRootAssetID( metaAbs );
            if ( metaRel.size() <= string_view( path::kMetaExtension ).size() )
                continue;
            const string assetRel = metaRel.substr( 0, metaRel.size() - string_view( path::kMetaExtension ).size() );
            if ( registerExisting( assetRel ) )
                ++count;
        }
        return count;
    }

    uint32 AssetDatabase::loadRegistry( const AssetID& assetID )
    {
        if ( AssetIO::hasAsset( assetID ) == false )
            return 0;
        string text;
        if ( AssetIO::readTextAsset( assetID, text ) == false )
            return 0;
        return loadRegistryText( text );
    }

    string AssetDatabase::makeRegistryText( string_view resourceRoot, string_view domain, uint32& outFailedCount )
    {
        const string domainRoot = FileUtil::joinPath( FileUtil::trimTrailingSlashes( FileUtil::normalizeSeparators( resourceRoot ) ), domain );
        // 목록과 각 .meta 는 쿠킹 작업 안이면 그 작업의 입력이다 — .meta 가 생기거나 사라져도 레지스트리를 다시 만든다.
        vector<string> listMeta;
        if ( CookJob::collectFiles( domainRoot, path::kMetaExtension, listMeta ) == false )
        {
            SW_LOG_ERROR( "Asset registry: the .meta files under '%#' could not all be listed - no registry is written for it", domainRoot );
            ++outFailedCount;
            return {};
        }

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

            AssetMeta  meta;
            UUID       guid{};
            string     metaText;
            const bool bMetaRead       = AssetIO::readTextFile( metaAbs, metaText ) && meta.parseXML( metaText, metaAbs.c_str() );
            const bool bUnreadableGUID = bMetaRead == false || UUID::tryParse( meta._guid.c_str(), guid ) == false || guid.isNull();
            if ( bUnreadableGUID )
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

    CookJobDesc AssetDatabase::makeCookJobDesc()
    {
        CookJobDesc desc;
        desc._cookerName    = "AssetRegistry";
        desc._cookerVersion = kCookVersion;
        desc._target        = "Any";
        desc._settingsHash  = 0; // 레지스트리에는 설정이 없다
        return desc;
    }

    uint32 AssetDatabase::writeRegistryFiles( string_view resourceRoot, string_view cookedDir, const CookStore& store, uint32& outFailedCount )
    {
        // 도메인 = 루트의 폴더 하나, `game` 은 그 아래 폴더 하나씩(팩 하나가 도메인 하나다 — `PackCooker::cookAllPacks` 와 같은 나눔).
        vector<string> listDomain;
        vector<string> listTopFolder;
        (void)FileUtil::collectFolders( resourceRoot, listTopFolder, false ); // 루트가 없으면 도메인이 없다 — 아래가 아무것도 쓰지 않는다
        VectorUtil::sort( listTopFolder );
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
            VectorUtil::sort( listGameFolder );
            for ( const string& gameFolder : listGameFolder )
            {
                listDomain.push_back( string( path::kGamePack ) + "/" + FileUtil::getFileNamePart( FileUtil::trimTrailingSlashes( gameFolder ) ) );
            }
        }

        const CookJobDesc desc = makeCookJobDesc();
        CookTally         tally;
        uint32            partialWrittenCount = 0; // 읽지 못한 .meta 를 뺀 채 쓴 레지스트리 수
        for ( const string& domain : listDomain )
        {
            const string      outputID         = FileUtil::joinPath( domain, "assetregistry.txt" );
            const string      outputPath       = FileUtil::joinPath( cookedDir, outputID );
            uint32            unreadableCount  = 0; // 읽지 못한 .meta 는 하나씩 센다
            bool              bPartialWritten  = false;
            const CookOutcome outcome          = store.cookFile( desc, outputID, outputPath, [&]()
                     {
                const string text = makeRegistryText( resourceRoot, domain, unreadableCount );
                if ( text.empty() )
                    return unreadableCount == 0 ? CookStep::Skipped : CookStep::Failed;
                if ( FileUtil::ensureParentDirectoryExists( outputPath ) == false || FileUtil::writeTextFile( outputPath, text ) == false )
                {
                    SW_LOG_ERROR( "Asset registry: could not write '%#'", outputPath );
                    return CookStep::Failed;
                }
                // 읽지 못한 .meta 가 있으면 레지스트리는 쓰되 기록은 남기지 않는다 — 다음 쿠킹도 그 실패를 다시 센다.
                bPartialWritten = unreadableCount > 0;
                return bPartialWritten ? CookStep::Failed : CookStep::Written;
            } );
            const bool        bFailedOtherwise = outcome == CookOutcome::Failed && unreadableCount == 0;
            outFailedCount += unreadableCount + ( bFailedOtherwise ? 1u : 0u );
            partialWrittenCount += bPartialWritten ? 1u : 0u;
            tally.add( outcome );
        }
        SW_LOG_INFO( "Asset registry cook: %# reused from the DDC, %# cooked", tally._reusedCount, tally._cookedCount );
        return tally.getWrittenCount() + partialWrittenCount;
    }

    uint32 AssetDatabase::loadRegistryText( string_view text )
    {
        uint32 count{ 0 };
        StringUtil::forEachLine( text, [&]( string_view line )
        {
            line = StringUtil::trimEnd( line );
            if ( line.empty() || line.front() == '#' )
                return;

            const size_t space = line.find( ' ' );
            if ( space == string_view::npos || space + 1 >= line.size() )
                return;

            UUID guid{};
            if ( UUID::tryParse( string( line.substr( 0, space ) ).c_str(), guid ) == false || guid.isNull() )
                return;

            registerMapping( line.substr( space + 1 ), guid );
            ++count;
        } );
        return count;
    }

    void AssetDatabase::clear()
    {
        UniqueLock<shared_mutex> lock{ _mutex };
        _mapPathToGUID.clear();
        _mapGUIDToPath.clear();
    }

    bool AssetDatabase::writeMetaFile( string_view relativePath, const UUID& guid, bool bImported ) const
    {
        // .meta 는 아직 없을 수 있다 — 에셋 파일(상대 키면 읽기가 고른 그 파일) 곁에 쓴다. 에셋의 쓸 자리가 곧 그 도메인이다.
        const std::optional<AbsolutePath> assetPath = engine::getSubsystem<AssetPaths>().resolveWritePath( relativePath );
        if ( assetPath.has_value() == false )
            return false; // 이유는 resolveWritePath 가 남겼다
        const string absMeta = metaPathFor( assetPath->str() );

        AssetMeta meta;
        meta._guid       = string( guid.toString().c_str() );
        meta._sourcePath = string( relativePath );
        meta._bImported  = bImported;
        return FileUtil::ensureParentDirectoryExists( absMeta ) && FileUtil::writeTextFile( absMeta, meta.toXML() );
    }

    AssetMetaReadResult AssetDatabase::loadMetaFile( string_view relativePath, UUID& outGUID, bool* pOutImported ) const
    {
        const string metaRel = metaPathFor( relativePath );
        if ( AssetIO::hasAsset( AssetID{ metaRel } ) == false )
            return AssetMetaReadResult::Missing;

        string    metaText;
        AssetMeta meta;
        if ( AssetIO::readTextAsset( AssetID{ metaRel }, metaText ) == false || meta.parseXML( metaText, metaRel.c_str() ) == false )
            return AssetMetaReadResult::Invalid;

        const bool bInvalidGUID = UUID::tryParse( meta._guid.c_str(), outGUID ) == false || outGUID.isNull();
        if ( bInvalidGUID )
        {
            SW_LOG_ERROR( "'%#' has an unreadable guid '%#'", metaRel, meta._guid );
            return AssetMetaReadResult::Invalid;
        }
        if ( pOutImported != nullptr )
            *pOutImported = meta._bImported;
        return AssetMetaReadResult::Loaded;
    }
} // namespace sw
