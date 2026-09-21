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
             * @brief `initialize()` 본문을 프로세스당 한 번만 돌립니다.
             * @details 늦게 온 스레드는 첫 호출이 **끝날 때까지 막힌다** — 초기화 도중의 빈 경로를
             *          보는 경우가 생기지 않는다. 이 안에서 `_s_pathCacheMutex` 를 다시 잡는데,
             *          반대 순서로 잡는 곳은 없다.
             */
            inline static std::once_flag _s_initOnce{};

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
                if ( resourceRoot.empty() || lowerRel.empty() || FileUtil::isAbsolutePath( lowerRel ) )
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
                if ( firstSlash != string::npos && firstSlash > 0 )
                {
                    const string_view domain = lowerRel.substr( 0, firstSlash );
                    if ( domain.find( ':' ) == string_view::npos )
                    {
                        const string candidateDomainDir = FileUtil::joinPath( resourceRoot, domain );
                        if ( FileUtil::directoryExists( candidateDomainDir ) )
                        {
                            outRoot         = candidateDomainDir;
                            outKeyUnderRoot = string( lowerRel.substr( firstSlash + 1 ) );
                            return true;
                        }
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

        /**
         * @brief 리소스 하나를 찾아 읽습니다 — **순서는 텍스트·바이너리가 같습니다.**
         * @param readFromDisk 디스크 절대 경로에서 읽는 방법.
         * @param readFromPack 마운트된 팩에서 읽는 방법(찾았으면 팩 경로를 `outPackPath` 에 담는다).
         * @details 순서는 넷이다 — (0) OS 절대 경로면 디스크에서 바로, (1) 낱개 파일 우선이 켜져
         *          있으면 디스크, (2) 마운트된 팩, (3) 낱개 경로를 못 푼 경우의 마지막 폴백.
         *
         *          예전에는 이 순서가 `readTextResource` 와 `readBinaryResource` 에 **두 벌**
         *          적혀 있었다. 한쪽만 고치면 텍스트와 바이너리가 다른 파일을 읽게 된다.
         *
         *          그리고 둘 다 못 찾을 때 `getResourcePath` 와 `fileExists` 를 **두 번씩** 했다.
         *          1단계가 이미 "그 경로에 없다" 를 확인했는데 3단계가 같은 경로를 다시 물었다 —
         *          경로를 한 번만 풀고, 3단계는 **1단계가 경로를 못 푼 경우에만** 의미가 있다.
         */
        template <typename DiskReadFn, typename PackReadFn>
        bool readResourceCommon( string_view relativePath, string* pOutAbsPath,
                                 const DiskReadFn& readFromDisk, const PackReadFn& readFromPack )
        {
            if ( relativePath.empty() )
                return false;

            // 0. OS 절대 경로(임시 파일, 외부 세이브 등)인 경우 디스크에서 직접 읽기
            if ( FileUtil::isAbsolutePath( relativePath ) )
            {
                if ( FileUtil::fileExists( relativePath ) == false )
                    return false;
                if ( pOutAbsPath != nullptr )
                    *pOutAbsPath = string( relativePath );
                return readFromDisk( relativePath );
            }

            ResourcePackManager& packManager = ResourceUtil::getPackManager();
            const bool           bLooseFirst = packManager.isAllowLooseFiles();

            // 낱개 경로는 **여기서 한 번만** 푼다.
            string loosePath;
            if ( bLooseFirst )
            {
                loosePath = ResourceUtil::getResourcePath( relativePath );
                if ( loosePath.empty() == false && FileUtil::fileExists( loosePath ) )
                {
                    if ( pOutAbsPath != nullptr )
                        *pOutAbsPath = loosePath;
                    return readFromDisk( loosePath );
                }
            }

            // 2. VFS 마운트된 팩들에서 O(1) 해시 룩업 및 압축 해제 읽기
            const string normalizedKey = FileUtil::normalizePath( relativePath );
            string       mountedPackPath;
            if ( readFromPack( normalizedKey, mountedPackPath ) )
            {
                if ( pOutAbsPath != nullptr )
                    *pOutAbsPath = "[" + FileUtil::getFileNamePart( mountedPackPath ) + "]:" + normalizedKey;
                return true;
            }

            // 3. 낱개 경로를 **못 푼 경우에만** 상대 경로 그대로 마지막으로 본다.
            //    풀렸는데 없었다면 위에서 이미 확인했으므로 다시 묻지 않는다.
            if ( bLooseFirst && loosePath.empty() && FileUtil::fileExists( relativePath ) )
            {
                if ( pOutAbsPath != nullptr )
                    *pOutAbsPath = string( relativePath );
                return readFromDisk( relativePath );
            }

            return false;
        }
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "ResourceUtil" );

    bool ResourceUtil::initialize()
    {
        // `_s_bInitialize` 는 "시작했다" 가 아니라 **"성공했다"** 를 뜻한다.
        //
        // 예전에는 본문 맨 앞에서 이 플래그를 켰고, 그래서 두 가지가 깨져 있었다:
        //  1) 루트를 못 찾아 false 로 나가도 "초기화됨" 으로 남는다. `App::initialize` 는 반환값을
        //     보지 않으므로, 실패를 검사하는 유일한 호출부(`ResourceManager::initialize`)가 그 다음에
        //     true 를 받아 **빈 경로로** 팩 마운트와 레지스트리 로드를 진행한다.
        //  2) 다른 스레드가 초기화 도중에 true 를 보고 아직 비어 있는 경로를 읽는다.
        //
        // `call_once` 는 콜러블이 **예외 없이 반환하면 완료로 표시**한다. 그래서 성패는 콜러블 안에서
        // 플래그에 남기고, 돌려주는 값은 그 플래그를 읽는다 — 본문을 그냥 감싸기만 하면 실패가 사라진다.
        std::call_once( ResourceUtilInternal::_s_initOnce, []()
        {
            {
                std::scoped_lock<mutex> lock( ResourceUtilInternal::_s_pathCacheMutex );
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
                return; // 플래그를 켜지 않는다 — initialize() 가 false 를 돌려준다.

            SW_LOG_INFO( "RootFolder : %#", rootPath );

            _s_projectFolderPath = FileUtil::normalizeSeparators( FileUtil::trimTrailingSlashes( rootPath ) );

            const string resourceRoot = FileUtil::joinPath( rootPath, path::kResourceFolder );

            // Canonical top-level Resource/ root (all domains are dynamically resolved under this).
            _s_resourceRootFolderPath =
                FileUtil::directoryExists( resourceRoot ) ? FileUtil::normalizeSeparators( resourceRoot ) : "";

            if ( _s_listSearchPriority.empty() )
                _s_listSearchPriority = getDefaultSearchPriority();

            setSearchPriority( _s_listSearchPriority );

            // 여기까지 와야 상태가 전부 채워진 것이다.
            _s_bInitialize.store( true, std::memory_order_release );
        } );

        return _s_bInitialize.load( std::memory_order_acquire );
    }

    string ResourceUtil::getResourcePath( string_view filePath, string_view folderName )
    {
        if ( filePath.empty() )
            return {};

        if ( FileUtil::isAbsolutePath( filePath ) )
        {
            if ( FileUtil::fileExists( filePath ) )
                return FileUtil::normalizeSeparators( filePath );
            return {};
        }

        uint64 cacheKeyHash = StringUtil::computeHash64( filePath );
        if ( folderName.empty() == false )
            cacheKeyHash = StringUtil::computeHash64( folderName, true, cacheKeyHash );

        {
            std::scoped_lock<mutex> lock( ResourceUtilInternal::_s_pathCacheMutex );
            auto                    it = ResourceUtilInternal::_s_mapResolvedPath.find( cacheKeyHash );
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
            std::scoped_lock<mutex> lock( ResourceUtilInternal::_s_pathCacheMutex );
            ResourceUtilInternal::_s_mapResolvedPath.insert_or_assign( cacheKeyHash, found );
        }

        return found;
    }

    string ResourceUtil::getWritePath( string_view path )
    {
        if ( FileUtil::isAbsolutePath( path ) )
            return FileUtil::normalizeSeparators( path );

        // NRVO 가 걸리도록 이름 있는 값 하나로 모아 한 번만 돌려준다.
        string result = getResourcePath( path );
        if ( result.empty() )
            result = makeAbsolutePath( path );
        if ( result.empty() )
            result = string{ path };
        return result;
    }

    string ResourceUtil::makeAbsolutePath( string_view relativePath )
    {
        if ( relativePath.empty() )
            return {};

        if ( FileUtil::isAbsolutePath( relativePath ) )
            return FileUtil::normalizeSeparators( relativePath );

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
        return readResourceCommon(
            relativePath, pOutAbsPath,
            [&outText]( string_view absPath )
        { return FileUtil::readTextFile( absPath, outText ); },
            [&outText]( string_view key, string& outPackPath )
        { return getPackManager().readTextFile( key, outText, &outPackPath ); } );
    }

    bool ResourceUtil::readBinaryResource( string_view relativePath, vector<uint8>& outBytes )
    {
        return readResourceCommon(
            relativePath, nullptr,
            [&outBytes]( string_view absPath )
        { return FileUtil::readFile( absPath, outBytes ); },
            [&outBytes]( string_view key, string& )
        { return getPackManager().readFile( key, outBytes ); } );
    }

    bool ResourceUtil::hasResource( string_view relativePath )
    {
        if ( relativePath.empty() )
            return false;

        // 0. OS 절대 경로인 경우 디스크에서 직접 확인
        // 판정은 `FileUtil::isAbsolutePath` 가 정본이다 — 여기 손으로 적은 복사본이 있었는데
        // **드라이브 문자가 글자인지 보지 않아** 정본과 답이 갈렸다. 같은 복사본이 에디터 설정
        // 경로에도 셋 있었다(지금은 `EditorUtil::resolveProjectRelativePath` 하나로 모았다).
        if ( FileUtil::isAbsolutePath( relativePath ) )
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
                    FileUtil::collectFolders( gameDir, listPackFolder, false );
                    for ( const string& packFolder : listPackFolder )
                    {
                        const string normPack = FileUtil::normalizeSeparators( packFolder );
                        if ( std::find( _s_listResourceFolder.begin(), _s_listResourceFolder.end(), normPack ) == _s_listResourceFolder.end() )
                            _s_listResourceFolder.push_back( normPack );
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
        std::scoped_lock<mutex> lock( ResourceUtilInternal::_s_pathCacheMutex );
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

    string ResourceUtil::makeUniqueSavePath( string_view absoluteFolder, string_view fileName )
    {
        // 반환은 `savePath` 하나로만 한다 — 갈래마다 다른 변수를 돌려주면 NRVO 가 막힌다(-Wnrvo).
        // 그래서 `const` 도 붙이지 않는다(반환할 때 자동 이동이 막힌다).
        string savePath = makeSavePath( absoluteFolder, fileName );
        if ( savePath.empty() || FileUtil::fileExists( savePath ) == false )
            return savePath;

        const string stem      = FileUtil::removeExtension( savePath );
        const string extension = FileUtil::getExtension( savePath );

        // 오브젝트 이름과 같은 규약으로 센다 — 2 부터, 넉넉한 곳에서 멈춘다.
        StringBuilder<constant::kMaxBuffer512> sb;
        for ( uint32 nameSuffix = 2; nameSuffix < 10000; ++nameSuffix )
        {
            sb.clear();
            sb.append( stem ).append( '_' ).append( nameSuffix ).append( extension );
            if ( FileUtil::fileExists( sb.view() ) == false )
            {
                SW_LOG_WARNING( "Name already taken '%#' — using '%#'", savePath, sb.view() );
                savePath.assign( sb.view() );
                return savePath;
            }
        }

        SW_LOG_WARNING( "Could not find a free name near '%#' — overwriting.", savePath );
        return savePath;
    }

    atomic<bool> ResourceUtil::_s_bInitialize{ false };

    string ResourceUtil::_s_projectFolderPath;

    string ResourceUtil::_s_resourceRootFolderPath;

    vector<string> ResourceUtil::_s_listSearchPriority;

    vector<string> ResourceUtil::_s_listResourceFolder;

} // namespace sw
