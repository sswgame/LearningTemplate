#include "pch.h"

#include "Engine/Resource/ResourcePaths.h"

#include "Core/Concurrency/mutex.h"
#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"

#include "Engine/Config/EngineConfig.h"
#include "Engine/Config/GameConfig.h"

#include "sw/config/CookContract.gen.h"

namespace sw
{
    namespace
    {
        struct ResourcePathsInternal
        {
            inline static mutex                         _s_pathCacheMutex{};
            inline static unordered_map<uint64, string> _s_mapResolvedPath{};

            /**
             * @brief `initialize()` 본문을 프로세스당 한 번만 돌립니다.
             * @details 늦게 온 스레드는 첫 호출이 **끝날 때까지 막힙니다.** 그래서 초기화 도중의 빈 경로를
             *          보는 일이 없습니다. 이 안에서 `_s_pathCacheMutex` 를 다시 잡는데,
             *          반대 순서로 잡는 곳은 없습니다.
             */
            inline static std::once_flag _s_initOnce{};

            /**
             * @brief 전역 ID(`engine/` · `common/` · `editor/` · `game/<pack>/`)를 도메인 루트로 매핑합니다.
             * @param lowerRel normalizePath 한 상대 키
             * @param outRoot 도메인 절대 루트
             * @param outKeyUnderRoot 그 루트 아래 상대 키(팩 이름만 있으면 빈 문자열)
             * @return 알려진 전역 ID 이고 그 루트가 있으면 true 입니다.
             */
            static bool mapGlobalIDToRoot( string_view lowerRel, string& outRoot, string& outKeyUnderRoot )
            {
                outRoot.clear();
                outKeyUnderRoot.clear();

                const string& resourceRoot      = ResourcePaths::getRootFolderPath();
                const bool    bUnresolvablePath = resourceRoot.empty() || lowerRel.empty() || FileUtil::isAbsolutePath( lowerRel );
                if ( bUnresolvablePath )
                    return false;

                // 1. game/<pack>/... 형식을 쪼갠다
                if ( FileUtil::startsWithPathComponent( lowerRel, path::kGamePack ) )
                {
                    const string rest  = FileUtil::suffixAfterPathComponent( lowerRel, path::kGamePack );
                    const size_t slash = rest.find( '/' );
                    if ( slash == string::npos )
                    {
                        outRoot         = FileUtil::joinPath( FileUtil::joinPath( resourceRoot, path::kGamePack ), rest );
                        outKeyUnderRoot = {};
                        return FileUtil::isDirectory( outRoot );
                    }
                    outRoot         = FileUtil::joinPath( FileUtil::joinPath( resourceRoot, path::kGamePack ), rest.substr( 0, slash ) );
                    outKeyUnderRoot = rest.substr( slash + 1 );
                    return FileUtil::isDirectory( outRoot );
                }

                // 2. 그 밖의 도메인(<domain>/<key>)을 이름으로 푼다(engine, common, editor, dlc, mods 등)
                const size_t firstSlash = lowerRel.find( '/' );
                if ( firstSlash != string::npos && firstSlash > 0 )
                {
                    const string_view domain = lowerRel.substr( 0, firstSlash );
                    if ( domain.find( ':' ) == string_view::npos )
                    {
                        const string candidateDomainDir = FileUtil::joinPath( resourceRoot, domain );
                        if ( FileUtil::isDirectory( candidateDomainDir ) )
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
             * @brief 검색 · 도메인 루트 하나 아래에 파일이 있으면 절대 경로를 반환합니다.
             * @param root 절대 루트
             * @param relFile 루트(또는 folder) 아래 상대 파일
             * @param relFolder 선택적 하위 폴더(비우면 root 바로 아래)
             * @return 있으면 normalizeSeparators 한 절대 경로, 없으면 빈 문자열
             */
            static string tryUnderRoot( string_view root, string_view relFile, string_view relFolder )
            {
                if ( root.empty() || relFile.empty() )
                    return {};
                const string absolutePath = relFolder.empty()
                                              ? FileUtil::joinPath( root, relFile )
                                              : FileUtil::joinPath( FileUtil::joinPath( root, relFolder ), relFile );
                if ( FileUtil::exists( absolutePath ) )
                    return FileUtil::normalizeSeparators( absolutePath );
                return {};
            }

            /**
             * @brief 검색 루트 목록을 돌며 tryUnderRoot 를 부릅니다.
             * @param listRoot getResourcePath 검색 루트들
             * @param relFile 상대 파일 키
             * @param relFolder 선택적 하위 폴더
             * @return 처음 찾은 절대 경로입니다. 없으면 빈 문자열입니다.
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
             * @brief 저장 대상 폴더가 지정한 루트 아래인지 보고, 더 긴(구체적인) 일치를 남깁니다.
             */
            static void updateLongestSaveRoot( string_view folderNorm, string_view candidateRoot, string& outPhysicalRoot, string& outRootNorm )
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
    SW_LOG_CALLER( "ResourcePaths" );

    /**
     * @brief 상대 id 에 `..` 성분이 있으면 true 입니다 — 리소스 루트 밖으로 나가는 id 입니다.
     * @details 거르지 않으면 `engine/../../x` 같은 id 가 도메인 루트에 붙어 루트 밖 파일을 읽는다(언리얼은 경로를 가상
     *          경로 · 팩 마운트 지점 안으로 가둔다). 저장소의 데이터 · 코드에는 `..` 를 쓰는 id 가 없다.
     */
    bool ResourcePaths::hasParentDirectoryComponent( string_view path )
    {
        size_t start{ 0 };
        while ( start <= path.size() )
        {
            size_t end = path.find_first_of( "/\\", start );
            if ( end == string_view::npos )
                end = path.size();
            if ( path.substr( start, end - start ) == ".." )
                return true;
            start = end + 1;
        }
        return false;
    }

    bool ResourcePaths::initialize()
    {
        // `_s_bInitialize` 는 "시작했다" 가 아니라 **"성공했다"** 를 뜻한다.
        //
        // 본문 맨 앞에서 이 플래그를 켜면 두 가지가 깨진다.
        //  1) 루트를 못 찾아 false 로 나가도 "초기화됨" 으로 남는다. `App::initialize` 는 반환값을
        //     보지 않으므로, 실패를 검사하는 유일한 호출부(`AssetManager::initialize`)가 그 다음에
        //     true 를 받아 **빈 경로로** 팩 마운트와 레지스트리 로드를 진행한다.
        //  2) 다른 스레드가 초기화 도중에 true 를 보고 아직 비어 있는 경로를 읽는다.
        //
        // `call_once` 는 콜러블이 **예외 없이 반환하면 완료로 표시**한다. 그래서 성패는 콜러블 안에서
        // 플래그에 남기고, 반환하는 값은 그 플래그를 읽는다. 본문을 그냥 감싸기만 하면 실패가 사라진다.
        std::call_once( ResourcePathsInternal::_s_initOnce, []()
        {
            {
                std::scoped_lock<mutex> lock( ResourcePathsInternal::_s_pathCacheMutex );
                ResourcePathsInternal::_s_mapResolvedPath.clear();
            }

            string currentPath = FileUtil::getCurrentPath();
            string rootPath;
            while ( currentPath.empty() == false )
            {
                const string folderPath = FileUtil::joinPath( currentPath, path::kResourceFolder );
                if ( FileUtil::isDirectory( folderPath ) )
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
                return; // 플래그를 켜지 않는다. initialize() 가 false 를 반환한다.

            SW_LOG_INFO( "RootFolder : %#", rootPath );

            _s_projectFolderPath = FileUtil::normalizeSeparators( FileUtil::trimTrailingSlashes( rootPath ) );

            const string resourceRoot = FileUtil::joinPath( rootPath, path::kResourceFolder );

            // Resource/ 최상위 루트. 모든 도메인은 이 아래에서 이름으로 푼다.
            _s_resourceRootFolderPath =
                FileUtil::isDirectory( resourceRoot ) ? FileUtil::normalizeSeparators( resourceRoot ) : "";

            if ( _s_listSearchPriority.empty() )
                _s_listSearchPriority = getDefaultSearchPriority();

            setSearchPriority( _s_listSearchPriority );

            // 여기까지 와야 상태가 모두 채워진 것이다.
            _s_bInitialize.store( true, std::memory_order_release );
        } );

        return _s_bInitialize.load( std::memory_order_acquire );
    }

    string ResourcePaths::getResourcePath( string_view filePath, string_view folderName )
    {
        if ( filePath.empty() )
            return {};

        if ( FileUtil::isAbsolutePath( filePath ) )
        {
            if ( FileUtil::exists( filePath ) )
                return FileUtil::normalizeSeparators( filePath );
            return {};
        }
        // 루트 밖으로 나가는 id 는 풀지 않는다(`readResourceCommon` 과 같은 규칙).
        if ( hasParentDirectoryComponent( filePath ) )
            return {};

        uint64 cacheKeyHash = StringUtil::computeHash64( filePath );
        if ( folderName.empty() == false )
            cacheKeyHash = StringUtil::computeHash64( folderName, true, cacheKeyHash );

        {
            std::scoped_lock<mutex> lock( ResourcePathsInternal::_s_pathCacheMutex );
            auto                    it = ResourcePathsInternal::_s_mapResolvedPath.find( cacheKeyHash );
            if ( it != ResourcePathsInternal::_s_mapResolvedPath.end() )
                return it->second;
        }

        const string lowerFile   = FileUtil::normalizePath( filePath );
        const string lowerFolder = folderName.empty() ? string{} : FileUtil::normalizePath( folderName );

        string found;
        string domainRoot;
        string keyUnderRoot;
        if ( ResourcePathsInternal::mapGlobalIDToRoot( lowerFile, domainRoot, keyUnderRoot ) )
        {
            found = ResourcePathsInternal::tryUnderRoot( domainRoot, keyUnderRoot, lowerFolder );
#if defined( SW_PLATFORM_LINUX )
            // 대소문자를 가리는 파일시스템: 소문자 키로 못 찾았으면 원래 표기로 다시 찾는다.
            if ( found.empty() && lowerFile != filePath )
            {
                const string raw    = string{ filePath };
                string       rawKey = keyUnderRoot;
                const size_t slash  = FileUtil::normalizeSeparators( raw ).find( '/' );
                if ( slash != string::npos )
                    rawKey = FileUtil::normalizeSeparators( raw ).substr( slash + 1 );
                found = ResourcePathsInternal::tryUnderRoot( domainRoot, rawKey, folderName );
            }
#endif
        }
        else
        {
            found = ResourcePathsInternal::tryResolveAmong( _s_listResourceFolder, lowerFile, lowerFolder );
#if defined( SW_PLATFORM_LINUX )
            if ( found.empty() && ( lowerFile != filePath || ( folderName.empty() == false && lowerFolder != folderName ) ) )
                found = ResourcePathsInternal::tryResolveAmong( _s_listResourceFolder, filePath, folderName );
#endif
        }

        if ( found.empty() == false )
        {
            // 캐시는 에셋 색인의 몫이다 — 부르는 쪽(스플래시 · 모듈 · 워커)의 태그가 아니라 Asset 으로 센다(재해시가 어디서 일어나든).
            SW_MEMORY_SCOPE( Asset );
            std::scoped_lock<mutex> lock( ResourcePathsInternal::_s_pathCacheMutex );
            ResourcePathsInternal::_s_mapResolvedPath.insert_or_assign( cacheKeyHash, found );
        }

        return found;
    }

    string ResourcePaths::getWritePath( string_view path )
    {
        if ( FileUtil::isAbsolutePath( path ) )
            return FileUtil::normalizeSeparators( path );

        // 상대 경로가 `..` 로 올라가면 리소스 루트 밖에 쓴다(데이터에 적힌 경로 하나로 아무 곳에나 쓸 수 있다). 쓰지 않는다.
        if ( toResourceID( path ).empty() )
        {
            SW_LOG_ERROR( "'%#' leaves the resource tree - nothing is written there", path );
            return {};
        }

        // NRVO 가 걸리도록 이름 있는 값 하나로 모아 한 번만 반환한다.
        string result = getResourcePath( path );
        if ( result.empty() )
            result = makeAbsolutePath( path );
        if ( result.empty() )
            result = string{ path };
        return result;
    }

    string ResourcePaths::toResourceID( string_view path )
    {
        string id;
        if ( path.empty() )
            return id;
        string relative = FileUtil::normalizeSeparators( path );
        if ( FileUtil::isAbsolutePath( relative ) )
        {
            const string root = FileUtil::trimTrailingSlashes( FileUtil::normalizeSeparators( getRootFolderPath() ) );
            string       underRoot;
            if ( root.empty() || FileUtil::makeRelativePath( root, relative, underRoot ) == false )
                return id;
            relative = std::move( underRoot );
        }
        else if ( FileUtil::startsWithPathComponent( FileUtil::normalizePath( relative ), FileUtil::normalizePath( path::kResourceFolder ) ) )
        {
            relative = FileUtil::suffixAfterPathComponent( FileUtil::normalizePath( relative ), FileUtil::normalizePath( path::kResourceFolder ) );
        }

        id = FileUtil::normalizePath( relative );
        while ( StringUtil::startsWith( id, "./" ) )
        {
            id.erase( 0, 2 );
        }
        const bool bUnsafeID = id.empty() || id == "." || id == ".." || StringUtil::startsWith( id, "../" ) || id.find( "/../" ) != string::npos ||
                               StringUtil::endsWith( id, "/.." );
        if ( bUnsafeID )
            id.clear();
        return id;
    }

    string ResourcePaths::makeAbsolutePath( string_view relativePath )
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
            if ( ResourcePathsInternal::mapGlobalIDToRoot( lowerRel, domainRoot, keyUnderRoot ) )
                result = FileUtil::joinPath( domainRoot, keyUnderRoot );
        }
        return result;
    }

    string ResourcePaths::getDomainFolderPath( string_view domainName, string_view subFolder )
    {
        if ( _s_resourceRootFolderPath.empty() || domainName.empty() )
            return {};

        const string domainDir = FileUtil::joinPath( _s_resourceRootFolderPath, domainName );
        if ( FileUtil::isDirectory( domainDir ) == false )
            return {};

        if ( subFolder.empty() )
            return FileUtil::normalizeSeparators( domainDir );

        const string targetDir = FileUtil::joinPath( domainDir, subFolder );
        if ( FileUtil::isDirectory( targetDir ) )
            return FileUtil::normalizeSeparators( targetDir );

        return {};
    }

    const string& ResourcePaths::getRootFolderPath()
    {
        return _s_resourceRootFolderPath;
    }

    const string& ResourcePaths::getProjectFolderPath()
    {
        return _s_projectFolderPath;
    }

    bool ResourcePaths::setSearchPriority( const vector<string>& listPriority )
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
                if ( activePack.empty() == false && FileUtil::isDirectory( activePack ) )
                {
                    const string normPack = FileUtil::normalizeSeparators( activePack );
                    if ( std::find( _s_listResourceFolder.begin(), _s_listResourceFolder.end(), normPack ) == _s_listResourceFolder.end() )
                        _s_listResourceFolder.push_back( normPack );
                }
                const string gameDir = getDomainFolderPath( path::kGamePack );
                if ( gameDir.empty() == false && FileUtil::isDirectory( gameDir ) )
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
                // 모든 도메인 · 커스텀 팩 · DLC · 모드를 이름으로 푼다("engine", "common", "editor", "dlc/expansion1", "mods/pack1")
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

    const vector<string>& ResourcePaths::getSearchPriority()
    {
        return _s_listSearchPriority;
    }

    string ResourcePaths::exchangeRootFolderPath( string_view resourceRootFolderPath )
    {
        string previous           = _s_resourceRootFolderPath;
        _s_resourceRootFolderPath = FileUtil::normalizeSeparators( resourceRootFolderPath );
        // 검색 폴더를 새 루트에서 다시 짓는다(경로 캐시도 거기서 비운다).
        (void)setSearchPriority( _s_listSearchPriority );
        return previous;
    }

    const vector<string>& ResourcePaths::getDefaultSearchPriority()
    {
        static const vector<string> s_listDefaultPriority = EngineConfig{}._listResourcePriority;
        return s_listDefaultPriority;
    }

    void ResourcePaths::clearPathCache()
    {
        std::scoped_lock<mutex> lock( ResourcePathsInternal::_s_pathCacheMutex );
        // `clear()` 는 버킷 · 밀집 배열을 남긴다 — 종료 끝에서도 부르므로 저장소까지 놓는다.
        ResourcePathsInternal::_s_mapResolvedPath = unordered_map<uint64, string>{};
    }

    string ResourcePaths::makeSaveFolderPath( string_view absoluteFolder )
    {
        const string folderNorm = FileUtil::trimTrailingSlashes( FileUtil::normalizePath( absoluteFolder ) );

        string physicalRoot;
        string rootNorm;

        for ( const string& root : _s_listResourceFolder )
        {
            ResourcePathsInternal::updateLongestSaveRoot( folderNorm, root, physicalRoot, rootNorm );
        }
        ResourcePathsInternal::updateLongestSaveRoot( folderNorm, getDomainFolderPath( path::kGamePack ), physicalRoot, rootNorm );
        ResourcePathsInternal::updateLongestSaveRoot( folderNorm, getDomainFolderPath( path::kEnginePack ), physicalRoot, rootNorm );
        ResourcePathsInternal::updateLongestSaveRoot( folderNorm, getDomainFolderPath( path::kCommonPack ), physicalRoot, rootNorm );
        ResourcePathsInternal::updateLongestSaveRoot( folderNorm, getDomainFolderPath( path::kEditorPack ), physicalRoot, rootNorm );

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

    string ResourcePaths::makeSavePath( string_view absoluteFolder, string_view fileName )
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

    string ResourcePaths::makeUniqueSavePath( string_view absoluteFolder, string_view fileName )
    {
        // 반환은 `savePath` 하나로만 한다. 분기마다 다른 변수를 반환하면 NRVO 가 막힌다(-Wnrvo).
        // 그래서 `const` 도 붙이지 않는다(반환할 때 자동 이동이 막힌다).
        string savePath = makeSavePath( absoluteFolder, fileName );
        if ( savePath.empty() || FileUtil::exists( savePath ) == false )
            return savePath;

        const string stem      = FileUtil::removeExtension( savePath );
        const string extension = FileUtil::getExtension( savePath );

        // 오브젝트 이름과 같은 규약으로 센다. 2 부터 세고, 넉넉한 곳에서 멈춘다.
        StringBuilder<constant::kMaxBuffer512> sb;
        for ( uint32 nameSuffix = 2; nameSuffix < 10000; ++nameSuffix )
        {
            sb.clear();
            sb.append( stem ).append( '_' ).append( nameSuffix ).append( extension );
            if ( FileUtil::exists( sb.view() ) == false )
            {
                SW_LOG_WARNING( "Name already taken '%#' — using '%#'", savePath, sb.view() );
                savePath.assign( sb.view() );
                return savePath;
            }
        }

        SW_LOG_WARNING( "Could not find a free name near '%#' — overwriting.", savePath );
        return savePath;
    }

    atomic<bool> ResourcePaths::_s_bInitialize{ false };

    string ResourcePaths::_s_projectFolderPath;

    string ResourcePaths::_s_resourceRootFolderPath;

    vector<string> ResourcePaths::_s_listSearchPriority;

    vector<string> ResourcePaths::_s_listResourceFolder;

    vector<string> ResourcePaths::_s_listHostExcludedExtension;

    vector<string> ResourcePaths::_s_listHostExcludedFolder;

    void ResourcePaths::setHostTarget( string_view buildTargetName )
    {
        _s_listHostExcludedExtension.clear();
        _s_listHostExcludedFolder.clear();
#define SW_ADD_HOST_EXCLUSION( Target, Kind, Extension, Folder )    \
    if ( buildTargetName == #Target )                               \
    {                                                               \
        if ( string_view{ Extension }.empty() == false )            \
            _s_listHostExcludedExtension.emplace_back( Extension ); \
        else                                                        \
            _s_listHostExcludedFolder.emplace_back( Folder );       \
    }
        SW_TARGET_ASSET_EXCLUSION_TABLE( SW_ADD_HOST_EXCLUSION )
#undef SW_ADD_HOST_EXCLUSION
        if ( _s_listHostExcludedExtension.empty() == false || _s_listHostExcludedFolder.empty() == false )
            SW_LOG_INFO( "Host target %# does not read %# asset extensions and %# asset folders", string( buildTargetName ).c_str(),
                         _s_listHostExcludedExtension.size(), _s_listHostExcludedFolder.size() );
    }

    bool ResourcePaths::isExcludedForHost( string_view resourcePath )
    {
        if ( _s_listHostExcludedExtension.empty() && _s_listHostExcludedFolder.empty() )
            return false;
        const string normalized = FileUtil::normalizePath( resourcePath );
        for ( const string& extension : _s_listHostExcludedExtension )
        {
            if ( StringUtil::endsWith( normalized, extension ) )
                return true;
        }
        for ( const string& folder : _s_listHostExcludedFolder )
        {
            // 폴더 조각은 경로 처음이거나 '/' 뒤에서 시작해 '/' 로 끝나야 한다(`shaders/bin` 이 `myshaders/binary` 에 맞지 않게).
            const size_t position            = normalized.find( folder );
            const bool   bFolderSegmentMatch = position != string::npos && ( position == 0 || normalized[position - 1] == '/' ) && position + folder.size() < normalized.size() &&
                                             normalized[position + folder.size()] == '/';
            if ( bFolderSegmentMatch )
                return true;
        }
        return false;
    }

} // namespace sw
