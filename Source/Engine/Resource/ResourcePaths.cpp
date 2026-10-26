#include "pch.h"

#include "Engine/Resource/ResourcePaths.h"

#include "Core/Common/HashUtil.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Diagnostics/CacheStats.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/LifetimeScope.h"
#include "Core/Memory/Memory.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Common/AssetTypeRegistry.h"
#include "Engine/Common/EngineServices.h"
#include "Engine/Config/EngineConfig.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Serialization/JSON/JSONDocument.h"

#include "sw/config/CookContract.gen.h"
#include "sw/config/ShippingHostDefaults.h"

namespace sw
{
    namespace
    {
        /**
         * @brief 푼 경로 캐시의 잠금과 배치 세대입니다. 세대는 검색 목록 · 루트 · 마운트가 바뀔 때마다 오른다(`clearPathCache`, 잠금 아래).
         * @details 세대가 표(`_s_mapResolvedPath`)의 수명과 따로라(표는 엔진 수명 범위가 닫히면 사라진다) 표가 없을 때도 읽을 수 있다.
         *          `ResourcePathsInternal` 밖에 둔다 — 안에 두면 기본 멤버 초기화가 감싸는 클래스 정의가 끝나기 전에 필요해 `inline static` 멤버를 만들 수 없다.
         */
        struct PathCacheGuard
        {
            mutex  _mutex{};
            uint64 _layoutGeneration{ 0 };
        };

        struct ResourcePathsInternal
        {
            inline static PathCacheGuard _s_pathCache{};

            /**
             * @brief 푼 경로 캐시의 한 칸입니다. `_path` 가 비면 "디스크에 없다" 를 기억한 칸(음성 결과)입니다.
             * @details 없는 파일은 나중에 생길 수 있으므로 음성 칸은 기억한 때의 `FileUtil::getCreationEpoch` 가 지금과 같은 동안만 믿는다. 있는 파일은 나중에 지워질 수 있으므로
             *          양성 칸은 기억한 때의 `FileUtil::getRemovalEpoch` 가 지금과 같은 동안만 믿는다(`_epoch` 는 칸의 종류에 맞는 쪽 값이다). 이 프로세스가 파일을 만들면(지우면)
             *          에포크가 올라 모든 음성(양성) 칸이 한꺼번에 낡는다. 검색 목록이 바뀔 때(`clearPathCache`)는 모든 칸을 버린다.
             */
            struct ResolvedEntry
            {
                string _path{};
                uint64 _epoch{ 0 };
            };

            /**
             * @brief 해석을 시작한 때의 세대입니다. **저장은 이 값을 들고서만 할 수 있습니다**(`storeResolved`) — 해석하는 동안 검색 목록이 바뀌었으면(`clearPathCache`) 옛 목록으로 푼
             *        결과를 새 캐시에 넣지 않는다. 찾기 전에 한 번 잡고(`captureStamp`) 찾은 뒤 저장에 넘긴다.
             */
            struct PathCacheStamp
            {
                uint64 _layoutGeneration{ 0 };
                uint64 _creationEpoch{ 0 };
                uint64 _removalEpoch{ 0 };
            };

            /** @brief 칸이 믿을 만한지 가르는 에포크입니다. 음성 칸(@p path 가 빔)은 만든 횟수, 양성 칸은 지운 횟수입니다. */
            static uint64 selectEpoch( const string& path, uint64 creationEpoch, uint64 removalEpoch ) { return path.empty() ? creationEpoch : removalEpoch; }

            /** @brief 푼 경로 캐시입니다. 엔진 수명 범위가 소유해 엔진 종료 때 저장소째 사라집니다(처음 넣을 때 만들어진다). 접근은 `_s_pathCache._mutex` 아래에서만 한다. */
            inline static ScopeOwned<unordered_map<string, ResolvedEntry>> _s_mapResolvedPath{};

            /** @brief 푼 경로 캐시의 계측 칸입니다(`ResourcePath`: 적중 = 캐시가 답함, 실패 = 디스크를 훑음). */
            static const CacheStatsHandle& getPathStats()
            {
                static const CacheStatsHandle s_stats = CacheStats::getOrCreate( "ResourcePath" );
                return s_stats;
            }
            /** @brief 음성 결과의 계측 칸입니다(`ResourcePathMissing`: 적중 = 기억한 "없음" 으로 답함, 실패 = 디스크를 훑었더니 없었음). 실패가 많고 적중이 적으면 음성 캐시가 헛돈다. */
            static const CacheStatsHandle& getMissingStats()
            {
                static const CacheStatsHandle s_stats = CacheStats::getOrCreate( "ResourcePathMissing" );
                return s_stats;
            }

            /** @brief 해석을 시작하는 지금의 세대를 잡습니다. 찾기 전에 한 번 잡는다. */
            static PathCacheStamp captureStamp()
            {
                std::scoped_lock<mutex> lock( _s_pathCache._mutex );
                return PathCacheStamp{ _s_pathCache._layoutGeneration, FileUtil::getCreationEpoch(), FileUtil::getRemovalEpoch() };
            }

            /** @brief 캐시에서 찾습니다. 찾았으면 true 이고 @p outPath 가 비면 "없음" 입니다. 기억한 뒤 파일이 만들어졌거나(음성 칸) 지워진(양성 칸) 낡은 칸은 없는 것으로 칩니다. */
            [[nodiscard]] static bool lookupResolved( const string& cacheKey, string& outPath )
            {
                const uint64                                      creationEpoch = FileUtil::getCreationEpoch();
                const uint64                                      removalEpoch  = FileUtil::getRemovalEpoch();
                std::scoped_lock<mutex>                           lock( _s_pathCache._mutex );
                const unordered_map<string, ResolvedEntry>* const pMapResolved = _s_mapResolvedPath.find();
                if ( pMapResolved == nullptr )
                    return false;
                const auto it = pMapResolved->find( cacheKey );
                if ( it == pMapResolved->end() )
                    return false;
                if ( it->second._epoch != selectEpoch( it->second._path, creationEpoch, removalEpoch ) )
                    return false; // 기억한 뒤 파일이 만들어지거나 지워졌다 — 다시 훑는다
                outPath = it->second._path;
                return true;
            }

            /** @brief 푼 결과(@p path 가 비면 "없음")를 저장합니다. @p stamp 의 검색 목록 세대가 이미 낡았으면 저장하지 않습니다. */
            static void storeResolved( string&& cacheKey, const string& path, const PathCacheStamp& stamp )
            {
                // 캐시는 에셋 색인의 몫이다 — 부르는 쪽(스플래시 · 모듈 · 워커)의 태그가 아니라 Asset 으로 센다(재해시가 어디서 일어나든).
                SW_MEMORY_SCOPE( Asset );
                std::scoped_lock<mutex> lock( _s_pathCache._mutex );
                if ( stamp._layoutGeneration != _s_pathCache._layoutGeneration )
                    return;
                unordered_map<string, ResolvedEntry>* const pMapResolved = _s_mapResolvedPath.getOrCreate();
                // nullptr 은 엔진 수명 범위가 닫힌 뒤다(Debug 는 `getOrCreate` 가 단언한다) — 캐시할 곳이 없을 뿐 찾은 경로는 그대로 돌려준다.
                if ( pMapResolved == nullptr )
                    return;
                const auto inserted           = pMapResolved->try_emplace( std::move( cacheKey ) );
                inserted.first->second._path  = path;
                inserted.first->second._epoch = selectEpoch( path, stamp._creationEpoch, stamp._removalEpoch );
                if ( inserted.second )
                    getPathStats().addItemCount( 1 );
            }

            /**
             * @brief 이 플랫폼의 파일 시스템이 대소문자를 구분하는지입니다. Linux 만 구분합니다 — Windows 는 소문자로 이어도 같은 폴더라 새 경로를 디스크 표기에 맞출 일이 없습니다.
             * @details `findLooseFile` 의 `SW_PLATFORM_LINUX` 재시도와 같은 기준입니다. 쓰기 쪽(`resolveWritePath`)은 이 값으로 분기해 Windows 에서도 같은 코드가 컴파일됩니다.
             */
#if defined( SW_PLATFORM_LINUX )
            static constexpr bool kCaseSensitiveFileSystem = true;
#else
            static constexpr bool kCaseSensitiveFileSystem = false;
#endif

            /** @brief `mountDiskDomain` 으로 붙인 도메인 하나입니다. */
            struct MountedDiskDomain
            {
                string _domain; ///< 소문자 도메인 이름(`/` 없음)
                string _folder; ///< 정규화한 절대 디스크 폴더(끝 `/` 없음)
            };
            inline static mutex                     _s_diskDomainMutex{};
            inline static vector<MountedDiskDomain> _s_listDiskDomain{};

            /** @brief 소문자 id 의 첫 성분이 붙인 도메인이면 그 폴더와 나머지 키를 줍니다. */
            static bool findMountedDiskDomain( string_view lowerRel, string& outRoot, string& outKeyUnderRoot )
            {
                const size_t firstSlash = lowerRel.find( '/' );
                if ( firstSlash == string_view::npos || firstSlash == 0 )
                    return false;
                const string_view       domain = lowerRel.substr( 0, firstSlash );
                std::scoped_lock<mutex> lock( _s_diskDomainMutex );
                for ( const MountedDiskDomain& mounted : _s_listDiskDomain )
                {
                    if ( mounted._domain == domain )
                    {
                        outRoot         = mounted._folder;
                        outKeyUnderRoot = string( lowerRel.substr( firstSlash + 1 ) );
                        return true;
                    }
                }
                return false;
            }

            /** @brief 절대 경로 @p absolutePath 가 붙인 디스크 도메인 폴더 아래면 그 도메인의 id(`<도메인>/<폴더 아래 경로>`)를 적고 true 입니다. */
            static bool findDiskDomainID( const string& absolutePath, string& outID )
            {
                const string            path = FileUtil::normalizePath( absolutePath );
                std::scoped_lock<mutex> lock( _s_diskDomainMutex );
                for ( const MountedDiskDomain& mounted : _s_listDiskDomain )
                {
                    const string folder = FileUtil::normalizePath( mounted._folder );
                    if ( path.size() <= folder.size() || FileUtil::startsWithPathComponent( path, folder ) == false )
                        continue;
                    outID = mounted._domain + "/" + FileUtil::suffixAfterPathComponent( path, folder );
                    return true;
                }
                return false;
            }

            /**
             * @brief `initialize()` 본문을 프로세스당 한 번만 돌립니다.
             * @details 늦게 온 스레드는 첫 호출이 **끝날 때까지 막힙니다.** 그래서 초기화 도중의 빈 경로를
             *          보는 일이 없습니다. 이 안에서 `_s_pathCache._mutex` 를 다시 잡는데,
             *          반대 순서로 잡는 곳은 없습니다.
             */
            inline static std::once_flag _s_initOnce{};

            /**
             * @brief 락 없는 전역(검색 목록 · 루트 · 호스트 제외 그룹)을 쓰는 API 가 병렬 단계 중에 불리지 않았는지 단언합니다.
             * @details 읽는 쪽(`findLooseFile` · `makeSaveFolderPath`)은 락 없이 이 칸들을 읽는다 — 워커나 병렬 본문 중에 쓰면 읽는 쪽이 깨진 벡터를 본다. 쓰기는
             *          메인 스레드의 직렬 구간(기동 · 설정 로드 · 에디터 모드 전환)에서만 한다. `initialize()` 의 첫 호출은 `call_once` 가 직렬로 만들어 대상이 아니다.
             * @param pAPIName 로그에 남길 API 이름
             */
            static void assertSerialPhase( const utf8* pAPIName )
            {
                SW_LOG_ASSERT( TaskManager::isInParallelBody() == false && TaskManager::isOnPoolWorkerThread() == false,
                               "ResourcePaths::%# writes lock-free globals and must run in a serial phase (not on a worker or inside a parallel body)", pAPIName );
            }

            /** @brief `readBasePriority` 의 결과입니다. 오류 문장은 부르는 쪽이 정적 초기화 가드 밖에서 로그합니다. */
            struct BasePriority
            {
                vector<string> _listPriority; ///< 읽은 우선순위 목록. 읽지 못하면 비어 있다.
                string         _error;        ///< 읽지 못한 이유. 비어 있으면 성공
            };

            /**
             * @brief 구성 때 베이크한 `EngineConfig.json` 사본의 `_listResourcePriority` 입니다. 읽지 못하면 오류 문장을 담고 빈 목록입니다.
             * @details 빈 목록은 "읽지 못함" 의 표식이다 — `initialize()` 가 보고 실패로 멈춘다(빈 검색 목록으로 계속하지 않는다).
             */
            static BasePriority readBasePriority()
            {
                BasePriority result;
                JSONDocument document;
                if ( document.parse( shipping_host::kEngineConfigJSON, "EngineConfig.json (configure-time copy)" ) == false )
                {
                    result._error = string( "EngineConfig.json (configure-time copy) could not be parsed: " ) + document.getLastError();
                    return result;
                }
                const JSONValue priority = document.getRoot().get( "_listResourcePriority" );
                for ( size_t index = 0; index < priority.size(); ++index )
                {
                    result._listPriority.push_back( priority.at( index ).asString() );
                }
                if ( result._listPriority.empty() )
                    result._error = "EngineConfig.json has no _listResourcePriority - resource paths cannot be initialized";
                return result;
            }

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

                // 0. 붙인 디스크 도메인(`mountDiskDomain`)은 리소스 루트와 무관하게 그 폴더로 푼다.
                const bool bResourceID = lowerRel.empty() == false && FileUtil::isAbsolutePath( lowerRel ) == false;
                if ( bResourceID && findMountedDiskDomain( lowerRel, outRoot, outKeyUnderRoot ) )
                    return true;

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
                if ( root.empty() )
                    return {};
                // 빈 키는 도메인 루트 자신이다(`game/shooter3d` 처럼 팩 폴더를 가리키는 id — `mapGlobalIDToRoot` 가 빈 키로 돌려준다).
                const string base         = relFolder.empty() ? string( root ) : FileUtil::joinPath( root, relFolder );
                const string absolutePath = relFile.empty() ? base : FileUtil::joinPath( base, relFile );
                if ( FileUtil::exists( absolutePath ) )
                    return FileUtil::normalizeSeparators( absolutePath );
                return {};
            }

            /**
             * @brief 검색 루트 목록을 돌며 tryUnderRoot 를 부릅니다.
             * @param listRoot findLooseFile 검색 루트들
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
        //
        // **로그는 `call_once` 밖에서 남긴다.** `call_once` 는 끝날 때까지 늦은 스레드를 막는 잠금이다. 안에서 로그를 남기면 로그 경로(리스너 · 파일 출력)가
        // `ResourcePaths` 를 되불렀을 때 같은 스레드가 자기 `call_once` 를 다시 기다려 교착한다. 그래서 안에서는 남길 말을 지역 변수에만 적는다.
        bool   bRootMissing = false;
        string rootToLog;
        std::call_once( ResourcePathsInternal::_s_initOnce, [&bRootMissing, &rootToLog]()
        {
            clearPathCache();

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

            if ( rootPath.empty() )
            {
                bRootMissing = true;
                return; // 플래그를 켜지 않는다. initialize() 가 false 를 반환한다.
            }

            rootToLog = rootPath;

            _s_projectFolderPath = FileUtil::normalizeSeparators( FileUtil::trimTrailingSlashes( rootPath ) );

            const string resourceRoot = FileUtil::joinPath( rootPath, path::kResourceFolder );

            // Resource/ 최상위 루트. 모든 도메인은 이 아래에서 이름으로 푼다.
            _s_resourceRootFolderPath =
                FileUtil::isDirectory( resourceRoot ) ? FileUtil::normalizeSeparators( resourceRoot ) : "";

            if ( _s_listSearchPriority.empty() )
                _s_listSearchPriority = getDefaultSearchPriority();
            if ( _s_listSearchPriority.empty() )
            {
                // 읽지 못한 이유를 여기서 남긴다(읽는 함수는 정적 초기화 가드 안이라 로그하지 않는다). 가드 안에서 만든 값이므로 다시 읽어도 같은 문장이다.
                SW_LOG_ERROR( "%#", ResourcePathsInternal::readBasePriority()._error );
                return; // 검색 목록 없이는 어떤 리소스도 찾지 못한다 — 플래그를 켜지 않아 initialize() 가 false 를 반환한다.
            }

            setSearchPriority( _s_listSearchPriority );

            // 여기까지 와야 상태가 모두 채워진 것이다.
            _s_bInitialize.store( true, std::memory_order_release );
        } );

        SW_LOG_ASSERT( bRootMissing == false, "RootFolder not found" );
        if ( rootToLog.empty() == false )
            SW_LOG_INFO( "RootFolder : %#", rootToLog );

        return _s_bInitialize.load( std::memory_order_acquire );
    }

    LooseFilePath ResourcePaths::findLooseFile( string_view filePath, string_view folderName )
    {
        if ( filePath.empty() )
            return {};

        if ( FileUtil::isAbsolutePath( filePath ) )
        {
            if ( FileUtil::exists( filePath ) )
                return LooseFilePath( FileUtil::normalizeSeparators( filePath ) );
            return {};
        }
        // 루트 밖으로 나가는 id 는 풀지 않는다(`readResourceCommon` 과 같은 규칙).
        if ( hasParentDirectoryComponent( filePath ) )
            return {};

        // 원문 키다 — 해시만 비교하면 충돌한 다른 파일의 경로를 조용히 돌려준다. `\n` 은 파일 id 에 들어갈 수 없어 둘을 가르는 구분자다.
        string cacheKey( filePath );
        if ( folderName.empty() == false )
        {
            cacheKey += '\n';
            cacheKey += folderName;
        }

        // 세대는 찾기 전에 잡는다 — 찾는 동안 검색 목록이 바뀌었거나 파일이 만들어졌으면 이 결과는 낡은 채로 저장되지 못하거나(목록) 다음 조회에서 버려진다(음성).
        const ResourcePathsInternal::PathCacheStamp stamp = ResourcePathsInternal::captureStamp();
        {
            string cached;
            if ( ResourcePathsInternal::lookupResolved( cacheKey, cached ) )
            {
                ResourcePathsInternal::getPathStats().recordHit();
                if ( cached.empty() )
                    ResourcePathsInternal::getMissingStats().recordHit();
                return LooseFilePath( std::move( cached ) );
            }
        }
        ResourcePathsInternal::getPathStats().recordMiss();

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
            // 붙인 디스크 도메인의 폴더는 대소문자가 섞일 수 있고, 기록해 둔 id(쿠킹 입력 등)는 소문자뿐이라 원래 표기가 없다 — 폴더를 훑어 소문자가 같은 항목으로 푼다.
            // 리소스 트리의 도메인은 소문자 규칙이라 훑지 않는다(없는 파일을 묻는 호출이 훑기를 부르지 않게).
            string mountedRoot;
            string mountedKey;
            if ( found.empty() && ResourcePathsInternal::findMountedDiskDomain( lowerFile, mountedRoot, mountedKey ) )
            {
                const string lowerKey = lowerFolder.empty() ? mountedKey : FileUtil::joinPath( lowerFolder, mountedKey );
                const string hit      = FileUtil::findPathIgnoringCase( mountedRoot, lowerKey );
                if ( hit.empty() == false && FileUtil::exists( hit ) )
                    found = FileUtil::normalizeSeparators( hit );
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

        if ( found.empty() )
            ResourcePathsInternal::getMissingStats().recordMiss();
        // 없음도 저장한다(음성 결과) — 같은 없는 파일을 묻는 호출(선택적 파일 · 팩에만 있는 에셋)이 매번 모든 검색 루트를 훑지 않게.
        ResourcePathsInternal::storeResolved( std::move( cacheKey ), found, stamp );

        return LooseFilePath( std::move( found ) );
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
        string result = resolveWritePath( path );
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
        string domainID;
        if ( FileUtil::isAbsolutePath( relative ) && ResourcePathsInternal::findDiskDomainID( relative, domainID ) )
        {
            // 붙인 디스크 도메인(시나리오의 `Saved/` 등) 아래면 그 도메인의 id 다 — 리소스 입구는 OS 경로를 받지 않는다.
            relative = std::move( domainID );
        }
        else if ( FileUtil::isAbsolutePath( relative ) )
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

    string ResourcePaths::resolveWritePath( string_view resourceID )
    {
        if ( resourceID.empty() )
            return {};

        if ( FileUtil::isAbsolutePath( resourceID ) )
            return FileUtil::normalizeSeparators( resourceID );

        // 루트 밖으로 나가는 id 는 풀지 않는다(`findLooseFile` 과 같은 규칙).
        if ( hasParentDirectoryComponent( resourceID ) )
            return {};

        // 이미 있는 파일이면 그 디스크 경로가 답이다(덮어쓸 자리).
        string result = findLooseFile( resourceID ).getDiskPath();
        if ( result.empty() == false )
            return result;

        const string lowerID = FileUtil::normalizePath( resourceID );
        string       domainRoot;
        string       keyUnderRoot;
        if ( ResourcePathsInternal::mapGlobalIDToRoot( lowerID, domainRoot, keyUnderRoot ) == false )
            return result;

        // 붙인 디스크 도메인의 폴더는 대소문자가 섞일 수 있다. 대소문자를 구분하는 파일 시스템에서 소문자 키를 그대로 이으면 이미 있는 폴더 옆에 대소문자만 다른 새 폴더가 생기므로,
        // 있는 성분은 디스크 표기를 쓰고 없는 성분부터는 id 의 표기를 쓴다. 리소스 트리의 도메인은 소문자 규칙이라 훑지 않고 새 성분도 소문자다(Windows 도 그대로 소문자로 잇는다).
        string mountedRoot;
        string mountedKey;
        if ( ResourcePathsInternal::kCaseSensitiveFileSystem && ResourcePathsInternal::findMountedDiskDomain( lowerID, mountedRoot, mountedKey ) )
        {
            const string normalizedID = FileUtil::normalizeSeparators( resourceID );
            return FileUtil::makePathIgnoringCase( mountedRoot, normalizedID.substr( normalizedID.find( '/' ) + 1 ) );
        }
        return FileUtil::joinPath( domainRoot, keyUnderRoot );
    }

    LooseFilePath ResourcePaths::findLooseDomainFolder( string_view domainName, string_view subFolder )
    {
        if ( _s_resourceRootFolderPath.empty() || domainName.empty() )
            return {};

        const string domainDir = FileUtil::joinPath( _s_resourceRootFolderPath, domainName );
        if ( FileUtil::isDirectory( domainDir ) == false )
            return {};

        if ( subFolder.empty() )
            return LooseFilePath( FileUtil::normalizeSeparators( domainDir ) );

        const string targetDir = FileUtil::joinPath( domainDir, subFolder );
        if ( FileUtil::isDirectory( targetDir ) )
            return LooseFilePath( FileUtil::normalizeSeparators( targetDir ) );

        return {};
    }

    bool ResourcePaths::mountDiskDomain( string_view domainName, string_view diskFolder )
    {
        const string domain       = FileUtil::normalizePath( domainName );
        const bool   bInvalidName = domain.empty() || domain.find_first_of( "/\\:" ) != string::npos || domain == "." || domain == "..";
        if ( bInvalidName )
        {
            SW_LOG_ERROR( "Cannot mount disk domain '%#' - the name is one path component without ':'", domainName );
            return false;
        }
        const string folder = FileUtil::trimTrailingSlashes( FileUtil::normalizeSeparators( diskFolder ) );
        if ( FileUtil::isAbsolutePath( folder ) == false || FileUtil::isDirectory( folder ) == false )
        {
            SW_LOG_ERROR( "Cannot mount disk domain '%#' - '%#' is not an existing absolute folder", domainName, diskFolder );
            return false;
        }
        const string root = FileUtil::trimTrailingSlashes( FileUtil::normalizeSeparators( _s_resourceRootFolderPath ) );
        if ( root.empty() == false && FileUtil::startsWithPathComponent( FileUtil::normalizePath( folder ), FileUtil::normalizePath( root ) ) )
        {
            SW_LOG_ERROR( "Cannot mount disk domain '%#' - '%#' is inside the resource root (packs-only builds would read loose assets)", domainName, diskFolder );
            return false;
        }
        // 락 안에서는 목록만 만지고, 오류는 이미 붙은 폴더를 적어 두었다가 락을 놓은 뒤에 남긴다(락을 쥔 채 로그를 부르지 않는다 — 로그 싱크가 무엇을 하든
        // 이 락과 얽히지 않게 한다).
        string conflictFolder;
        bool   bConflict{ false };
        {
            std::scoped_lock<mutex> lock( ResourcePathsInternal::_s_diskDomainMutex );
            for ( const ResourcePathsInternal::MountedDiskDomain& mounted : ResourcePathsInternal::_s_listDiskDomain )
            {
                if ( mounted._domain != domain )
                    continue;
                if ( FileUtil::normalizePath( mounted._folder ) == FileUtil::normalizePath( folder ) )
                    return true;
                conflictFolder = mounted._folder;
                bConflict      = true;
                break;
            }
            if ( bConflict == false )
                ResourcePathsInternal::_s_listDiskDomain.push_back( ResourcePathsInternal::MountedDiskDomain{ domain, folder } );
        }
        if ( bConflict )
        {
            SW_LOG_ERROR( "Cannot mount disk domain '%#' at '%#' - it is already mounted at '%#'", domainName, diskFolder, conflictFolder );
            return false;
        }
        clearPathCache();
        return true;
    }

    bool ResourcePaths::isMountedDiskDomainID( string_view resourceID )
    {
        string root;
        string key;
        return resourceID.empty() == false && FileUtil::isAbsolutePath( resourceID ) == false &&
               ResourcePathsInternal::findMountedDiskDomain( FileUtil::normalizePath( resourceID ), root, key );
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

        // 첫 `initialize()` 는 `call_once` 안(직렬)에서 이 함수를 부른다 — 성공 표시가 서기 전이라 그 호출은 단언 대상이 아니다.
        if ( _s_bInitialize.load( std::memory_order_acquire ) )
            ResourcePathsInternal::assertSerialPhase( "setSearchPriority" );

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
                // 게임 프리셋을 읽기 전(기동의 리소스 루트 단계 - 서비스 표가 서기 전이거나 아직 읽지 않았다)에는 활성 팩이 아직 없다 - 그때는 game/ 아래 팩만 순서에 넣는다.
                const GameConfig* pGameConfig = engine::findSubsystem<GameConfig>();
                const string      activePack  = pGameConfig != nullptr && pGameConfig->_packRoot.isSet() ? findLooseDomainFolder( pGameConfig->_packRoot.get() ).getDiskPath() : string();
                if ( activePack.empty() == false && FileUtil::isDirectory( activePack ) )
                {
                    const string normPack = FileUtil::normalizeSeparators( activePack );
                    if ( VectorUtil::contains( _s_listResourceFolder, normPack ) == false )
                        _s_listResourceFolder.push_back( normPack );
                }
                const string gameDir = findLooseDomainFolder( path::kGamePack ).getDiskPath();
                if ( gameDir.empty() == false && FileUtil::isDirectory( gameDir ) )
                {
                    vector<string> listPackFolder;
                    FileUtil::collectFolders( gameDir, listPackFolder, false );
                    for ( const string& packFolder : listPackFolder )
                    {
                        const string normPack = FileUtil::normalizeSeparators( packFolder );
                        if ( VectorUtil::contains( _s_listResourceFolder, normPack ) == false )
                            _s_listResourceFolder.push_back( normPack );
                    }
                    if ( VectorUtil::contains( _s_listResourceFolder, gameDir ) == false )
                        _s_listResourceFolder.push_back( gameDir );
                }
            }
            else
            {
                // 모든 도메인 · 커스텀 팩 · DLC · 모드를 이름으로 푼다("engine", "common", "editor", "dlc/expansion1", "mods/pack1")
                const string domainDir = findLooseDomainFolder( token ).getDiskPath();
                if ( domainDir.empty() == false )
                {
                    if ( VectorUtil::contains( _s_listResourceFolder, domainDir ) == false )
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
        ResourcePathsInternal::assertSerialPhase( "exchangeRootFolderPath" );
        string previous           = _s_resourceRootFolderPath;
        _s_resourceRootFolderPath = FileUtil::normalizeSeparators( resourceRootFolderPath );
        // 검색 폴더를 새 루트에서 다시 짓는다(경로 캐시도 거기서 비운다).
        (void)setSearchPriority( _s_listSearchPriority );
        return previous;
    }

    const vector<string>& ResourcePaths::getDefaultSearchPriority()
    {
        // 값의 출처는 `Config/Engine/EngineConfig.json` 하나다. 기동의 이 시점에는 설정을 아직 읽지 않았으므로 구성 때 베이크한 같은 파일의 사본에서
        // 이 칸만 읽는다(리플렉션이 서기 전이라 JSON 문서로). 실행 중 값은 설정을 읽은 뒤 `AssetManager::mountContent` 가 다시 넣는다.
        // 오류 로그는 함수 정적 초기화 가드 안에서 남기지 않는다 — 로거가 가드를 쥔 채 되돌아 들어오면 교착이다. 오류 문장은 `initialize()` 가 가드 밖에서 남긴다.
        static const ResourcePathsInternal::BasePriority s_basePriority = ResourcePathsInternal::readBasePriority();
        return s_basePriority._listPriority;
    }

    void ResourcePaths::clearPathCache()
    {
        std::scoped_lock<mutex> lock( ResourcePathsInternal::_s_pathCache._mutex );
        // `clear()` 는 버킷 · 밀집 배열을 남긴다 — 검색 목록이 바뀐 뒤의 캐시가 자란 만큼 남지 않게 저장소까지 놓는다. 종료 때의 반납은 엔진 수명 범위가 한다.
        // 세대를 올려, 이 순간 이전의 검색 목록으로 풀던 호출이 결과를 새 캐시에 넣지 못하게 한다(`storeResolved`).
        ++ResourcePathsInternal::_s_pathCache._layoutGeneration;
        unordered_map<string, ResourcePathsInternal::ResolvedEntry>* const pMapResolved = ResourcePathsInternal::_s_mapResolvedPath.find();
        if ( pMapResolved != nullptr )
        {
            ResourcePathsInternal::getPathStats().addItemCount( -static_cast<int64>( pMapResolved->size() ) );
            ResourcePathsInternal::getPathStats().recordClear();
            *pMapResolved = unordered_map<string, ResourcePathsInternal::ResolvedEntry>{};
        }
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
        ResourcePathsInternal::updateLongestSaveRoot( folderNorm, findLooseDomainFolder( path::kGamePack ).getDiskPath(), physicalRoot, rootNorm );
        ResourcePathsInternal::updateLongestSaveRoot( folderNorm, findLooseDomainFolder( path::kEnginePack ).getDiskPath(), physicalRoot, rootNorm );
        ResourcePathsInternal::updateLongestSaveRoot( folderNorm, findLooseDomainFolder( path::kCommonPack ).getDiskPath(), physicalRoot, rootNorm );
        ResourcePathsInternal::updateLongestSaveRoot( folderNorm, findLooseDomainFolder( path::kEditorPack ).getDiskPath(), physicalRoot, rootNorm );

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

    vector<string> ResourcePaths::_s_listHostExcludedGroup;

    void ResourcePaths::setHostTarget( string_view buildTargetName )
    {
        ResourcePathsInternal::assertSerialPhase( "setHostTarget" );
        _s_listHostExcludedGroup.clear();
#define SW_ADD_HOST_EXCLUDED_GROUP( Target, Group ) \
    if ( buildTargetName == #Target )               \
        _s_listHostExcludedGroup.emplace_back( #Group );
        SW_TARGET_EXCLUDED_GROUP_TABLE( SW_ADD_HOST_EXCLUDED_GROUP )
#undef SW_ADD_HOST_EXCLUDED_GROUP
        if ( _s_listHostExcludedGroup.empty() == false )
            SW_LOG_INFO( "Host target %# does not read %# asset groups", string( buildTargetName ).c_str(), _s_listHostExcludedGroup.size() );
    }

    bool ResourcePaths::isExcludedForHost( string_view resourcePath )
    {
        if ( _s_listHostExcludedGroup.empty() )
            return false;
        const string_view group = AssetTypeRegistry::findExcludeGroup( resourcePath );
        if ( group.empty() )
            return false;
        for ( const string& excluded : _s_listHostExcludedGroup )
        {
            if ( StringUtil::equals( excluded, group, true ) )
                return true;
        }
        return false;
    }

    bool ResourcePaths::isExcludedForTarget( string_view buildTargetName, string_view resourcePath )
    {
        if ( buildTargetName.empty() )
            return false;
        const string_view group = AssetTypeRegistry::findExcludeGroup( resourcePath );
        if ( group.empty() )
            return false;
#define SW_MATCH_TARGET_EXCLUDED_GROUP( Target, Group )                            \
    if ( buildTargetName == #Target && StringUtil::equals( group, #Group, true ) ) \
        return true;
        SW_TARGET_EXCLUDED_GROUP_TABLE( SW_MATCH_TARGET_EXCLUDED_GROUP )
#undef SW_MATCH_TARGET_EXCLUDED_GROUP
        return false;
    }

} // namespace sw
