#include "pch.h"

#include "Engine/Resource/AssetDatabase.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Common/EngineDefines.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Utility/Format/KeyValueFile.h"

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
                    // Write-side: invent absolute path from domain-qualified global ID only.
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
        if ( FileUtil::makePathRelative( rootNorm, absNorm, rel ) == false )
        {
            rel.clear();
            return rel;
        }
        rel = FileUtil::normalizePath( rel );
        if ( rel.empty() || rel == "." )
            rel.clear();
        return rel;
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

    Uuid AssetDatabase::ensureMeta( string_view relativePath, bool bImported )
    {
        Uuid   result{};
        string path = FileUtil::normalizePath( relativePath );
        if ( path.empty() || FileUtil::hasExtension( path, ".meta" ) )
            return result;

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
                // 실행마다 다른 GUID 가 되고, 그 파일은 팩 옆(개발 PC 에서는 소스 트리 Resource/)에 떨어진다 —
                // 실제로 Shipping 실기동 한 번에 defaultmaterial.material.meta 의 GUID 가 바뀌어 작업 트리가
                // 더러워졌다. 배포본에서 에셋 식별자는 쿠킹 때 정해진 것만 쓴다; 없으면 없는 것이다
                // (호출부는 모두 null GUID 를 허용한다 — 씬·프리팹 로더는 경로로 물러난다).
                return result;
#else
                result = Uuid::generate();
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

    void AssetDatabase::registerMapping( string_view relativePath, const Uuid& guid )
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
        if ( path.empty() || FileUtil::hasExtension( path, ".meta" ) )
            return false;

        Uuid guid{};
        if ( loadMetaFile( path, guid ) == false )
            return false;

        std::unique_lock<std::shared_mutex> lock{ _mutex };
        _mapPathToGuid[path] = guid;
        _mapGuidToPath[guid] = path;
        return true;
    }

    bool AssetDatabase::tryGetGuid( string_view relativePath, Uuid& outGuid ) const
    {
        std::shared_lock<std::shared_mutex> lock{ _mutex };
        const auto                          it = _mapPathToGuid.find( string( relativePath ) );
        if ( it == _mapPathToGuid.end() )
            return false;
        outGuid = it->second;
        return true;
    }

    bool AssetDatabase::tryGetPath( const Uuid& guid, string& outPath ) const
    {
        std::shared_lock<std::shared_mutex> lock{ _mutex };
        const auto                          it = _mapGuidToPath.find( guid );
        if ( it == _mapGuidToPath.end() )
            return false;
        outPath = it->second;
        return true;
    }

    const Uuid* AssetDatabase::getGuid( string_view relativePath ) const
    {
        std::shared_lock<std::shared_mutex> lock{ _mutex };
        const auto                          it = _mapPathToGuid.find( string( relativePath ) );
        if ( it == _mapPathToGuid.end() )
            return nullptr;
        return &it->second;
    }

    const string* AssetDatabase::getPath( const Uuid& guid ) const
    {
        std::shared_lock<std::shared_mutex> lock{ _mutex };
        const auto                          it = _mapGuidToPath.find( guid );
        if ( it == _mapGuidToPath.end() )
            return nullptr;
        return &it->second;
    }

    size_t AssetDatabase::getAssetCount() const
    {
        std::shared_lock<std::shared_mutex> lock{ _mutex };
        return _mapPathToGuid.size();
    }

    uint32 AssetDatabase::refreshFolder( string_view absoluteFolder, bool bCreateMissing )
    {
        uint32 count{ 0 };
        if ( FileUtil::directoryExists( absoluteFolder ) == false )
            return 0;

        vector<string> listFile;
        FileUtil::collectFiles( absoluteFolder, {}, listFile, false, false );
        for ( const string& filePath : listFile )
        {
            string rel;
            BLOCK( "Filter and Normalize Path" )
            {
                const string name = FileUtil::getFileNamePart( filePath );
                if ( FileUtil::hasExtension( name, ".meta" ) )
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

    void AssetDatabase::clear()
    {
        std::unique_lock<std::shared_mutex> lock{ _mutex };
        _mapPathToGuid.clear();
        _mapGuidToPath.clear();
    }

    bool AssetDatabase::writeMetaFile( string_view relativePath, const Uuid& guid, bool bImported ) const
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

        FileUtil::createParentDirectory( absMeta );
        return FileUtil::writeTextFile( absMeta, sb.view() );
    }

    bool AssetDatabase::loadMetaFile( string_view relativePath, Uuid& outGuid, bool* pOutImported ) const
    {
        const string metaRel = metaPathFor( relativePath );
        if ( ResourceUtil::hasResource( metaRel ) == false )
            return false;

        KeyValueMap mapData;
        if ( KeyValueFile::loadResource( metaRel, mapData ) == false )
            return false;

        const utf8* pGuidStr = KeyValueFile::get( mapData, "guid", nullptr );
        if ( pGuidStr == nullptr || Uuid::tryParse( pGuidStr, outGuid ) == false || outGuid.isNull() )
            return false;
        if ( pOutImported != nullptr )
            *pOutImported = KeyValueFile::getInt( mapData, "imported", 0 ) != 0;
        return true;
    }
} // namespace sw
