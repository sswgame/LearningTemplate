#include "pch.h"

#include "Engine/Resource/ResourceUtil.h"

#include "Core/Concurrency/mutex.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Config/EngineConfig.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Resource/ResourceManager.h"
#include "Engine/Resource/ResourcePackManager.h"

namespace sw
{
    namespace
    {
        struct ResourceUtilInternal
        {
            inline static mutex                         _s_pathCacheMutex{};
            inline static unordered_map<uint64, string> _s_mapResolvedPath{};

            /**
             * @brief 전역 ID(`engine/`·`common/`·`editor/`·`game/<pack>/`)를 도메인 루트로 매핑합니다.
             * @param lowerRel normalizePath 된 상대 키
             * @param outRoot 도메인 절대 루트
             * @param outKeyUnderRoot 그 루트 아래 상대 키 (팩 이름만이면 empty)
             * @return 알려진 전역 ID이고 해당 루트가 존재하면 true
             */
            static bool mapGlobalIdToRoot( string_view lowerRel, string& outRoot, string& outKeyUnderRoot )
            {
                outRoot.clear();
                outKeyUnderRoot.clear();

                const string& resourceRoot = ResourceUtil::getRootFolderPath();
                if ( resourceRoot.empty() || lowerRel.empty() )
                    return false;

                // 1. game/<pack>/... 형식 분해
                if ( FileUtil::startsWithPathComponent( lowerRel, path::kGamePack ) )
                {
                    const string rest  = FileUtil::suffixAfterPathComponent( lowerRel, path::kGamePack );
                    const size_t slash = rest.find( '/' );
                    if ( slash == string::npos )
                    {
                        outRoot         = FileUtil::joinPath( FileUtil::joinPath( resourceRoot, path::kGamePack ), rest );
                        outKeyUnderRoot = {};
                        return FileUtil::directoryExists( outRoot );
                    }
                    outRoot         = FileUtil::joinPath( FileUtil::joinPath( resourceRoot, path::kGamePack ), rest.substr( 0, slash ) );
                    outKeyUnderRoot = rest.substr( slash + 1 );
                    return FileUtil::directoryExists( outRoot );
                }

                // 2. 임의의 도메인 (<domain>/<key>) 동적 해석 (engine, common, editor, dlc, mods 등)
                const size_t firstSlash = lowerRel.find( '/' );
                if ( firstSlash != string::npos )
                {
                    const string_view domain             = lowerRel.substr( 0, firstSlash );
                    const string      candidateDomainDir = FileUtil::joinPath( resourceRoot, domain );
                    if ( FileUtil::directoryExists( candidateDomainDir ) )
                    {
                        outRoot         = candidateDomainDir;
                        outKeyUnderRoot = string( lowerRel.substr( firstSlash + 1 ) );
                        return true;
                    }
                }

                return false;
            }

            /**
             * @brief 단일 검색/도메인 루트 아래에서 파일이 있으면 절대 경로를 반환합니다.
             * @param root 절대 루트
             * @param relFile 루트(또는 folder) 아래 상대 파일
             * @param relFolder 선택적 하위 폴더 (비우면 root 직하)
             * @return 존재하면 normalizeSeparators 된 절대 경로, 아니면 empty
             */
            static string tryUnderRoot( string_view root, string_view relFile, string_view relFolder )
            {
                if ( root.empty() || relFile.empty() )
                    return {};
                const string absolutePath = relFolder.empty()
                                              ? FileUtil::joinPath( root, relFile )
                                              : FileUtil::joinPath( FileUtil::joinPath( root, relFolder ), relFile );
                if ( FileUtil::fileExists( absolutePath ) )
                    return FileUtil::normalizeSeparators( absolutePath );
                return {};
            }

            /**
             * @brief 검색 루트 목록을 순회하며 tryUnderRoot 합니다.
             * @param listRoot getResourcePath 검색 루트들
             * @param relFile 상대 파일 키
             * @param relFolder 선택적 하위 폴더
             * @return 첫 번째로 찾은 절대 경로. 없으면 empty.
             */
            static string tryResolveAmong( const vector<string>& listRoot, string_view relFile, string_view relFolder )
            {
                for ( const string& root : listRoot )
                {
                    const string hit = tryUnderRoot( root, relFile, relFolder );
                    if ( hit.empty() == false )
                        return hit;
                }
                return {};
            }

            /**
             * @brief 저장 대상 폴더가 지정한 루트 아래인지 판정하고, 더 긴(구체적인) 일치를 유지합니다.
             */
            static void considerSaveRoot( string_view folderNorm, string_view candidateRoot, string& outPhysicalRoot, string& outRootNorm )
            {
                if ( candidateRoot.empty() )
                    return;
                const string norm = FileUtil::trimTrailingSlashes( FileUtil::normalizePath( candidateRoot ) );
                if ( norm.empty() )
                    return;
                if ( FileUtil::startsWithPathComponent( folderNorm, norm ) )
                {
                    if ( norm.size() > outRootNorm.size() )
                    {
                        outRootNorm     = norm;
                        outPhysicalRoot = FileUtil::trimTrailingSlashes( FileUtil::normalizeSeparators( string{ candidateRoot } ) );
                    }
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "ResourceUtil" );

    bool ResourceUtil::initialize()
    {
        if ( _s_bInitialize.load( std::memory_order_acquire ) )
            return true;
        _s_bInitialize.store( true, std::memory_order_release );

        {
            std::lock_guard<mutex> lock( ResourceUtilInternal::_s_pathCacheMutex );
            ResourceUtilInternal::_s_mapResolvedPath.clear();
        }

        string currentPath = FileUtil::getCurrentPath();
        string rootPath;
        while ( currentPath.empty() == false )
        {
            const string folderPath = FileUtil::joinPath( currentPath, path::kResourceFolder );
            if ( FileUtil::directoryExists( folderPath ) )
            {
                rootPath = currentPath;
                break;
            }
            const string parent = FileUtil::getDirectoryPart( currentPath );
            if ( parent.empty() || parent == currentPath )
                break;
            currentPath = parent;
        }

        SW_LOG_ASSERT( rootPath.empty() == false, "RootFolder를 찾지 못했습니다" );
        if ( rootPath.empty() )
            return false;

        SW_LOG_INFO( "RootFolder : %#", rootPath );

        _s_projectFolderPath = FileUtil::normalizeSeparators( FileUtil::trimTrailingSlashes( rootPath ) );

        const string resourceRoot = FileUtil::joinPath( rootPath, path::kResourceFolder );

        // Canonical top-level Resource/ root (all domains are dynamically resolved under this).
        _s_resourceRootFolderPath =
            FileUtil::directoryExists( resourceRoot ) ? FileUtil::normalizeSeparators( resourceRoot ) : "";

        if ( _s_listSearchPriority.empty() )
            _s_listSearchPriority = getDefaultSearchPriority();

        setSearchPriority( _s_listSearchPriority );
        return true;
    }

    string ResourceUtil::getResourcePath( string_view filePath, string_view folderName )
    {
        if ( filePath.empty() )
            return {};

        uint64 cacheKeyHash = StringUtil::computeHash64( filePath );
        if ( folderName.empty() == false )
        {
            cacheKeyHash = StringUtil::computeHash64( folderName, true, cacheKeyHash );
        }

        {
            std::lock_guard<mutex> lock( ResourceUtilInternal::_s_pathCacheMutex );
            auto                   it = ResourceUtilInternal::_s_mapResolvedPath.find( cacheKeyHash );
            if ( it != ResourceUtilInternal::_s_mapResolvedPath.end() )
                return it->second;
        }

        const string lowerFile   = FileUtil::normalizePath( filePath );
        const string lowerFolder = folderName.empty() ? string{} : FileUtil::normalizePath( folderName );

        string found;
        string domainRoot;
        string keyUnderRoot;
        if ( ResourceUtilInternal::mapGlobalIdToRoot( lowerFile, domainRoot, keyUnderRoot ) )
        {
            found = ResourceUtilInternal::tryUnderRoot( domainRoot, keyUnderRoot, lowerFolder );
#if defined( SW_PLATFORM_LINUX )
            // Case-sensitive FS: retry original spelling if lowercase key missed.
            if ( found.empty() && lowerFile != filePath )
            {
                const string raw    = string{ filePath };
                string       rawKey = keyUnderRoot;
                const size_t slash  = FileUtil::normalizeSeparators( raw ).find( '/' );
                if ( slash != string::npos )
                    rawKey = FileUtil::normalizeSeparators( raw ).substr( slash + 1 );
                found = ResourceUtilInternal::tryUnderRoot( domainRoot, rawKey, folderName );
            }
#endif
        }
        else
        {
            found = ResourceUtilInternal::tryResolveAmong( _s_listResourceFolder, lowerFile, lowerFolder );
#if defined( SW_PLATFORM_LINUX )
            if ( found.empty() && ( lowerFile != filePath || ( folderName.empty() == false && lowerFolder != folderName ) ) )
                found = ResourceUtilInternal::tryResolveAmong( _s_listResourceFolder, filePath, folderName );
#endif
        }

        if ( found.empty() == false )
        {
            std::lock_guard<mutex> lock( ResourceUtilInternal::_s_pathCacheMutex );
            ResourceUtilInternal::_s_mapResolvedPath.insert_or_assign( cacheKeyHash, found );
        }

        return found;
    }

    string ResourceUtil::makeAbsolutePath( string_view relativePath )
    {
        if ( relativePath.empty() )
            return {};

        string result = getResourcePath( relativePath );
        if ( result.empty() )
        {
            const string lowerRel = FileUtil::normalizePath( relativePath );
            string       domainRoot;
            string       keyUnderRoot;
            if ( ResourceUtilInternal::mapGlobalIdToRoot( lowerRel, domainRoot, keyUnderRoot ) )
                result = FileUtil::joinPath( domainRoot, keyUnderRoot );
        }
        return result;
    }

    bool ResourceUtil::readTextResource( string_view relativePath, string& outText,
                                         string* pOutAbsPath )
    {
        if ( relativePath.empty() )
            return false;

        // 0. OS 절대 경로(임시 파일, 외부 세이브 등)인 경우 디스크에서 직접 읽기
        const bool bIsAbsolute = ( relativePath.size() >= 2 && relativePath[1] == ':' ) ||
                                 ( relativePath.size() >= 1 && ( relativePath[0] == '/' || relativePath[0] == '\\' ) );
        if ( bIsAbsolute )
        {
            if ( FileUtil::fileExists( relativePath ) )
            {
                if ( pOutAbsPath != nullptr )
                    *pOutAbsPath = string( relativePath );
                return FileUtil::readTextFile( relativePath, outText );
            }
            return false;
        }

        const string         normalizedKey = FileUtil::normalizePath( relativePath );
        ResourcePackManager& packManager   = getPackManager();

        // 1. 낱개 파일 우선 로드가 켜져 있는 경우, 디스크 파일 먼저 확인
        if ( packManager.isAllowLooseFiles() )
        {
            const string absPath = getResourcePath( relativePath );
            if ( absPath.empty() == false && FileUtil::fileExists( absPath ) )
            {
                if ( pOutAbsPath != nullptr )
                    *pOutAbsPath = absPath;
                return FileUtil::readTextFile( absPath, outText );
            }
        }

        // 2. VFS 마운트된 팩들에서 O(1) 해시 룩업 및 압축 해제 읽기
        string mountedPackPath;
        if ( packManager.readTextFile( normalizedKey, outText, &mountedPackPath ) )
        {
            if ( pOutAbsPath != nullptr )
                *pOutAbsPath = "[" + FileUtil::getFileNamePart( mountedPackPath ) + "]:" + normalizedKey;
            return true;
        }

        // 3. 낱개 파일 우선 로드가 켜져 있는 경우에만 디스크 fallback 읽기
        if ( packManager.isAllowLooseFiles() )
        {
            string absPath = getResourcePath( relativePath );
            if ( absPath.empty() )
                absPath = string( relativePath );
            if ( FileUtil::fileExists( absPath ) )
            {
                if ( pOutAbsPath != nullptr )
                    *pOutAbsPath = absPath;
                return FileUtil::readTextFile( absPath, outText );
            }
        }

        return false;
    }

    bool ResourceUtil::readBinaryResource( string_view relativePath, vector<uint8>& outBytes )
    {
        if ( relativePath.empty() )
            return false;

        // 0. OS 절대 경로(임시 파일, 외부 세이브 등)인 경우 디스크에서 직접 읽기
        const bool bIsAbsolute = ( relativePath.size() >= 2 && relativePath[1] == ':' ) ||
                                 ( relativePath.size() >= 1 && ( relativePath[0] == '/' || relativePath[0] == '\\' ) );
        if ( bIsAbsolute )
        {
            if ( FileUtil::fileExists( relativePath ) )
                return FileUtil::readFile( relativePath, outBytes );
            return false;
        }

        const string         normalizedKey = FileUtil::normalizePath( relativePath );
        ResourcePackManager& packManager   = getPackManager();

        if ( packManager.isAllowLooseFiles() )
        {
            const string absPath = getResourcePath( relativePath );
            if ( absPath.empty() == false && FileUtil::fileExists( absPath ) )
            {
                return FileUtil::readFile( absPath, outBytes );
            }
        }

        if ( packManager.readFile( normalizedKey, outBytes ) )
        {
            return true;
        }

        if ( packManager.isAllowLooseFiles() )
        {
            string absPath = getResourcePath( relativePath );
            if ( absPath.empty() )
                absPath = string( relativePath );
            if ( FileUtil::fileExists( absPath ) )
                return FileUtil::readFile( absPath, outBytes );
        }

        return false;
    }

    bool ResourceUtil::hasResource( string_view relativePath )
    {
        if ( relativePath.empty() )
            return false;

        // 0. OS 절대 경로인 경우 디스크에서 직접 확인
        const bool bIsAbsolute = ( relativePath.size() >= 2 && relativePath[1] == ':' ) ||
                                 ( relativePath.size() >= 1 && ( relativePath[0] == '/' || relativePath[0] == '\\' ) );
        if ( bIsAbsolute )
            return FileUtil::fileExists( relativePath );

        const string         normalizedKey = FileUtil::normalizePath( relativePath );
        ResourcePackManager& packManager   = getPackManager();

        if ( packManager.hasFile( normalizedKey ) )
            return true;

        if ( packManager.isAllowLooseFiles() )
        {
            const string absPath = getResourcePath( relativePath );
            if ( absPath.empty() == false && FileUtil::fileExists( absPath ) )
                return true;

            return FileUtil::fileExists( relativePath );
        }

        return false;
    }

    ResourcePackManager& ResourceUtil::getPackManager()
    {
        return engine::getResourceManager().getPackManager();
    }

    string ResourceUtil::getDomainFolderPath( string_view domainName, string_view subFolder )
    {
        if ( _s_resourceRootFolderPath.empty() || domainName.empty() )
            return {};

        const string domainDir = FileUtil::joinPath( _s_resourceRootFolderPath, domainName );
        if ( FileUtil::directoryExists( domainDir ) == false )
            return {};

        if ( subFolder.empty() )
            return FileUtil::normalizeSeparators( domainDir );

        const string targetDir = FileUtil::joinPath( domainDir, subFolder );
        if ( FileUtil::directoryExists( targetDir ) )
            return FileUtil::normalizeSeparators( targetDir );

        return {};
    }

    const string& ResourceUtil::getRootFolderPath()
    {
        return _s_resourceRootFolderPath;
    }

    const string& ResourceUtil::getProjectFolderPath()
    {
        return _s_projectFolderPath;
    }

    bool ResourceUtil::setSearchPriority( const vector<string>& listPriority )
    {
        if ( listPriority.empty() )
            return false;

        _s_listSearchPriority = listPriority;

        if ( _s_resourceRootFolderPath.empty() )
            return true;

        _s_listResourceFolder.clear();
        _s_listResourceFolder.reserve( 16 );

        const string& resourceRoot = _s_resourceRootFolderPath;

        for ( const string& tokenRaw : listPriority )
        {
            const string token = FileUtil::normalizePath( tokenRaw );
            if ( token == "game" )
            {
                const string activePack = getDomainFolderPath( GameConfig::getActive()._packRoot );
                if ( activePack.empty() == false && FileUtil::directoryExists( activePack ) )
                {
                    const string normPack = FileUtil::normalizeSeparators( activePack );
                    if ( std::find( _s_listResourceFolder.begin(), _s_listResourceFolder.end(), normPack ) == _s_listResourceFolder.end() )
                        _s_listResourceFolder.push_back( normPack );
                }
                const string gameDir = getDomainFolderPath( path::kGamePack );
                if ( gameDir.empty() == false && FileUtil::directoryExists( gameDir ) )
                {
                    vector<string> listPackFolder;
                    FileUtil::collectFolders( gameDir, listPackFolder, false, false );
                    for ( const string& packFolder : listPackFolder )
                    {
                        const string normPack = FileUtil::normalizeSeparators( packFolder );
                        if ( std::find( _s_listResourceFolder.begin(), _s_listResourceFolder.end(), normPack ) == _s_listResourceFolder.end() )
                        {
                            _s_listResourceFolder.push_back( normPack );
                        }
                    }
                    if ( std::find( _s_listResourceFolder.begin(), _s_listResourceFolder.end(), gameDir ) == _s_listResourceFolder.end() )
                        _s_listResourceFolder.push_back( gameDir );
                }
            }
            else
            {
                // 모든 도메인/커스텀 팩/DLC/모드 동적 해석 ("engine", "common", "editor", "dlc/expansion1", "mods/pack1")
                const string domainDir = getDomainFolderPath( token );
                if ( domainDir.empty() == false )
                {
                    if ( std::find( _s_listResourceFolder.begin(), _s_listResourceFolder.end(), domainDir ) == _s_listResourceFolder.end() )
                        _s_listResourceFolder.push_back( domainDir );
                }
            }
        }

        clearPathCache();
        return true;
    }

    const vector<string>& ResourceUtil::getSearchPriority()
    {
        return _s_listSearchPriority;
    }

    const vector<string>& ResourceUtil::getDefaultSearchPriority()
    {
        static const vector<string> s_listDefaultPriority = EngineConfig{}._listResourcePriority;
        return s_listDefaultPriority;
    }

    void ResourceUtil::clearPathCache()
    {
        std::lock_guard<mutex> lock( ResourceUtilInternal::_s_pathCacheMutex );
        ResourceUtilInternal::_s_mapResolvedPath.clear();
    }

    string ResourceUtil::makeSaveFolderPath( string_view absoluteFolder )
    {
        const string folderNorm = FileUtil::trimTrailingSlashes( FileUtil::normalizePath( absoluteFolder ) );

        string physicalRoot;
        string rootNorm;

        for ( const string& root : _s_listResourceFolder )
        {
            ResourceUtilInternal::considerSaveRoot( folderNorm, root, physicalRoot, rootNorm );
        }
        ResourceUtilInternal::considerSaveRoot( folderNorm, getDomainFolderPath( path::kGamePack ), physicalRoot, rootNorm );
        ResourceUtilInternal::considerSaveRoot( folderNorm, getDomainFolderPath( path::kEnginePack ), physicalRoot, rootNorm );
        ResourceUtilInternal::considerSaveRoot( folderNorm, getDomainFolderPath( path::kCommonPack ), physicalRoot, rootNorm );
        ResourceUtilInternal::considerSaveRoot( folderNorm, getDomainFolderPath( path::kEditorPack ), physicalRoot, rootNorm );

        if ( physicalRoot.empty() )
            return FileUtil::trimTrailingSlashes( FileUtil::normalizeSeparators( string{ absoluteFolder } ) );

        string result = physicalRoot;
        if ( folderNorm.size() > rootNorm.size() )
        {
            string rel = folderNorm.substr( rootNorm.size() );
            while ( rel.empty() == false && rel.front() == '/' )
            {
                rel.erase( rel.begin() );
            }
            if ( rel.empty() == false )
                result = FileUtil::joinPath( result, rel );
        }
        return result;
    }

    string ResourceUtil::makeSavePath( string_view absoluteFolder, string_view fileName )
    {
        string result = makeSaveFolderPath( absoluteFolder );
        if ( fileName.empty() == false )
        {
            const string lowerName = FileUtil::normalizePath( fileName );
            if ( result.empty() )
                result = lowerName;
            else
                result = FileUtil::joinPath( result, lowerName );
        }
        return result;
    }

    atomic<bool> ResourceUtil::_s_bInitialize{ false };

    string ResourceUtil::_s_projectFolderPath;

    string ResourceUtil::_s_resourceRootFolderPath;

    vector<string> ResourceUtil::_s_listSearchPriority;

    vector<string> ResourceUtil::_s_listResourceFolder;

} // namespace sw
