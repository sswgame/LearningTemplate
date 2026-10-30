#include "pch.h"

#include "Engine/FileSystem/AssetPaths.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/unordered_set.h"
#include "Core/Diagnostics/CacheStats.h"
#include "Core/File/FileUtil.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Config/EngineConfig.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Foundation/AssetTypeRegistry.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Serialization/JSON/JSONDocument.h"

#include "sw/config/CookContract.gen.h"
#include "sw/config/ShippingHostDefaults.h"

namespace sw
{
    namespace
    {
        /**
         * @brief 경로 표 하나(프로젝트 · 리소스 루트, 검색 우선순위 · 검색 루트, 호스트가 읽지 않는 에셋 묶음)입니다. **만든 뒤 바꾸지 않는다.**
         * @details 읽는 쪽(로더 · 디코드 워커 · 게임 스레드)은 지금 표를 원자 포인터로 한 번 잡아 잠금 없이 읽는다 — 쓰는 쪽이 칸을 제자리에서 고치던 때는 읽는 쪽이
         *          반쯤 고친 벡터를 볼 수 있었다. 쓰기는 사본을 고쳐 새 표로 갈아 끼운다(`AssetPathsInternal::publishTable`).
         */
        struct AssetPathTable
        {
            string         _projectFolderPath{};      ///< 프로젝트 루트
            string         _resourceRootFolderPath{}; ///< Content/ 최상위 루트(유일한 기준점)
            vector<string> _listSearchPriority{};     ///< 검색 우선순위 토큰 목록
            vector<string> _listResourceFolder{};     ///< findLooseFile 의 검색 루트 목록
            vector<string> _listHostExcludedGroup{};  ///< 이 호스트가 읽지 않는 에셋 묶음(`Texture` · `ShaderBinary` · `Audio`, 종류 표의 `_excludeGroup`)
        };

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

        /** @brief `mountDiskDomain` 으로 붙인 도메인 하나입니다. */
        struct MountedDiskDomain
        {
            string _domain; ///< 소문자 도메인 이름(`/` 없음)
            string _folder; ///< 정규화한 절대 디스크 폴더(끝 `/` 없음)
        };

        /** @brief `readBasePriority` 의 결과입니다. 오류 문장은 부르는 쪽이 정적 초기화 가드 밖에서 로그합니다. */
        struct BasePriority
        {
            vector<string> _listPriority; ///< 읽은 우선순위 목록. 읽지 못하면 비어 있다.
            string         _error;        ///< 읽지 못한 이유. 비어 있으면 성공
        };
    } // namespace

    /**
     * @brief 객체 하나의 경로 상태 전부(경로 표 · 붙인 디스크 도메인 · 푼 경로 캐시)입니다 — 예전에는 이 파일의 프로세스 정적이었다.
     * @details 정적이던 때는 서비스가 서기 전(리소스 루트 단계 · `setHostTarget` · `AssetManager::initialize`)과 엔진 없는 도구가 같은 한 벌을 나눠 썼고,
     *          캐시만 떼어 서비스로 보내면 한 상태가 두 주인으로 갈렸다. 이제 주인은 이 객체를 가진 쪽(`ContentRootSubsystem` · 도구) 하나다.
     */
    struct AssetPaths::State
    {
        // --- 푼 경로 캐시 ---
        mutex                                                 _cacheMutex{};
        uint64 _layoutGeneration                              SW_GUARDED_BY( _cacheMutex ){ 0 }; ///< 검색 목록 · 루트 · 마운트가 바뀔 때마다 오른다(`clearPathCache`)
        unordered_map<string, ResolvedEntry> _mapResolvedPath SW_GUARDED_BY( _cacheMutex ){};

        // --- 경로 표 ---
        /**
         * @brief 지금 경로 표와 지난 표들입니다. 지난 표는 놓지 않는다 — 읽는 쪽이 잡은 표의 참조(`getRootFolderPath` 의 `const string&` 따위)가 이 객체가 사는 동안 유효해야 한다.
         * @details 표는 기동 · 설정 로드 · 호스트 타깃 때만 새로 생겨 몇 개뿐이다.
         */
        AssetPathTable                                         _initialTable{};
        atomic<const AssetPathTable*>                          _pCurrentTable{ &_initialTable };
        mutex                                                  _publishMutex{};
        vector<unique_ptr<AssetPathTable>> _listPublishedTable SW_GUARDED_BY( _publishMutex ){};
        bool                                                   _bInitialized{ false }; ///< `initialize()` 가 **성공했는지**(소유자가 직렬 구간에서만 쓴다)

        // --- 붙인 디스크 도메인 ---
        mutex                                     _diskDomainMutex{};
        vector<MountedDiskDomain> _listDiskDomain SW_GUARDED_BY( _diskDomainMutex ){};
    };

    namespace
    {
        struct AssetPathsInternal
        {
            using State = AssetPaths::State;

            /** @brief 지금 경로 표입니다. 한 번 잡아 그 안에서 읽는다(칸마다 다시 잡으면 둘 사이에 표가 바뀔 수 있다). */
            static const AssetPathTable& getTable( const State& state ) { return *state._pCurrentTable.load( std::memory_order_acquire ); }

            /**
             * @brief 지금 표의 사본을 @p edit 로 고쳐 새 표로 갈아 끼웁니다. 쓰는 API(직렬 구간)만 부른다. @p edit 안에서 다시 갈아 끼우지 않는다.
             * @return 새 표입니다.
             */
            template <typename TEdit>
            static const AssetPathTable& publishTable( State& state, TEdit&& edit )
            {
                ScopedLock<mutex>          lock( state._publishMutex );
                unique_ptr<AssetPathTable> next = make_unique<AssetPathTable>( getTable( state ) );
                edit( *next );
                // 같은 내용의 표가 이미 있으면 그것으로 되돌린다 — 같은 설정을 다시 넣을 때 표가 쌓이지 않게. 지난 표를 놓지 않는 대신 늘어나는 것은
                // 서로 다른 내용뿐이다. 놓는 훅은 두지 않는다 — 읽는 쪽이 쥔 참조를 끊는 길이 된다.
                const AssetPathTable* pNext = isSameTable( state._initialTable, *next ) ? &state._initialTable : nullptr;
                for ( size_t index = 0; pNext == nullptr && index < state._listPublishedTable.size(); ++index )
                {
                    if ( isSameTable( *state._listPublishedTable[index], *next ) )
                        pNext = state._listPublishedTable[index].get();
                }
                if ( pNext == nullptr )
                {
                    pNext = next.get();
                    state._listPublishedTable.push_back( std::move( next ) );
                }
                state._pCurrentTable.store( pNext, std::memory_order_release );
                return *pNext;
            }

            /** @brief 두 표의 내용이 같으면 true 입니다(`publishTable` 이 같은 표를 다시 쓴다). */
            static bool isSameTable( const AssetPathTable& left, const AssetPathTable& right )
            {
                return left._projectFolderPath == right._projectFolderPath && left._resourceRootFolderPath == right._resourceRootFolderPath &&
                       left._listSearchPriority == right._listSearchPriority && left._listResourceFolder == right._listResourceFolder &&
                       left._listHostExcludedGroup == right._listHostExcludedGroup;
            }

            /** @brief @p root 아래 도메인(과 하위 폴더)의 디스크 폴더입니다. 없으면 빈 글입니다(`findLooseDomainFolder` 의 본체 — 새 표를 짓는 동안 그 표의 루트로 푼다). */
            static string findDomainFolderUnder( const string& root, string_view domainName, string_view subFolder )
            {
                if ( root.empty() || domainName.empty() )
                    return {};
                const string domainDir = FileUtil::joinPath( root, domainName );
                if ( FileUtil::isDirectory( domainDir ) == false )
                    return {};
                if ( subFolder.empty() )
                    return FileUtil::normalizeSeparators( domainDir );
                const string targetDir = FileUtil::joinPath( domainDir, subFolder );
                return FileUtil::isDirectory( targetDir ) ? FileUtil::normalizeSeparators( targetDir ) : string{};
            }

            /** @brief @p root 와 우선순위 @p listPriority 로 검색 루트 목록을 짓습니다(디스크를 본다 — 잠금 밖에서 부른다). */
            static vector<string> buildResourceFolders( const string& root, const vector<string>& listPriority )
            {
                vector<string> listFolder;
                if ( root.empty() )
                    return listFolder;
                listFolder.reserve( 16 );
                // 순서는 우선순위 그대로 두고 같은 폴더는 한 번만 넣는다.
                unordered_set<string> uniqueFolder;
                const auto            appendFolder = [&listFolder, &uniqueFolder]( const string& folder )
                {
                    if ( uniqueFolder.insert( folder ).second )
                        listFolder.push_back( folder );
                };
                for ( const string& tokenRaw : listPriority )
                {
                    const string token = FileUtil::normalizePath( tokenRaw );
                    if ( token == "game" )
                    {
                        // 게임 프리셋을 읽기 전(기동의 리소스 루트 단계 - 서비스 표가 서기 전이거나 아직 읽지 않았다)에는 활성 팩이 아직 없다 - 그때는 game/ 아래 팩만 순서에 넣는다.
                        const GameConfig* pGameConfig = engine::findSubsystem<GameConfig>();
                        const string      activePack  = pGameConfig != nullptr && pGameConfig->_packRoot.isSet() ? findDomainFolderUnder( root, pGameConfig->_packRoot.get(), {} ) : string();
                        if ( activePack.empty() == false && FileUtil::isDirectory( activePack ) )
                            appendFolder( FileUtil::normalizeSeparators( activePack ) );
                        const string gameDir = findDomainFolderUnder( root, path::kGamePack, {} );
                        if ( gameDir.empty() == false && FileUtil::isDirectory( gameDir ) )
                        {
                            vector<string> listPackFolder;
                            if ( FileUtil::collectFolders( gameDir, listPackFolder, false ) == false )
                                SW_LOG_ERROR( "Pack folders under '%#' could not all be listed - the packs left out are not searched", gameDir );
                            for ( const string& packFolder : listPackFolder )
                            {
                                appendFolder( FileUtil::normalizeSeparators( packFolder ) );
                            }
                            appendFolder( gameDir );
                        }
                    }
                    else
                    {
                        // 모든 도메인 · 커스텀 팩 · DLC · 모드를 이름으로 푼다("engine", "common", "editor", "dlc/expansion1", "mods/pack1")
                        const string domainDir = findDomainFolderUnder( root, token, {} );
                        if ( domainDir.empty() == false )
                            appendFolder( domainDir );
                    }
                }
                return listFolder;
            }

            /** @brief 칸이 믿을 만한지 가르는 에포크입니다. 음성 칸(@p path 가 빔)은 만든 횟수, 양성 칸은 지운 횟수입니다. */
            static uint64 selectEpoch( const string& path, uint64 creationEpoch, uint64 removalEpoch ) { return path.empty() ? creationEpoch : removalEpoch; }

            /** @brief 푼 경로 캐시의 계측 칸입니다(`AssetPath`: 적중 = 캐시가 답함, 실패 = 디스크를 훑음). 계측 레지스트리의 손잡이라 객체 상태가 아니다. */
            static const CacheStatsHandle& getPathStats()
            {
                static const CacheStatsHandle s_stats = CacheStats::getOrCreate( "AssetPath" );
                return s_stats;
            }
            /** @brief 음성 결과의 계측 칸입니다(`AssetPathMissing`: 적중 = 기억한 "없음" 으로 답함, 실패 = 디스크를 훑었더니 없었음). 실패가 많고 적중이 적으면 음성 캐시가 헛돈다. */
            static const CacheStatsHandle& getMissingStats()
            {
                static const CacheStatsHandle s_stats = CacheStats::getOrCreate( "AssetPathMissing" );
                return s_stats;
            }

            /** @brief 해석을 시작하는 지금의 세대를 잡습니다. 찾기 전에 한 번 잡는다. */
            static PathCacheStamp captureStamp( State& state )
            {
                ScopedLock<mutex> lock( state._cacheMutex );
                return PathCacheStamp{ state._layoutGeneration, FileUtil::getCreationEpoch(), FileUtil::getRemovalEpoch() };
            }

            /** @brief 캐시에서 찾습니다. 찾았으면 true 이고 @p outPath 가 비면 "없음" 입니다. 기억한 뒤 파일이 만들어졌거나(음성 칸) 지워진(양성 칸) 낡은 칸은 없는 것으로 칩니다. */
            [[nodiscard]] static bool findResolved( State& state, const string& cacheKey, string& outPath )
            {
                const uint64      creationEpoch = FileUtil::getCreationEpoch();
                const uint64      removalEpoch  = FileUtil::getRemovalEpoch();
                ScopedLock<mutex> lock( state._cacheMutex );
                const auto        it = state._mapResolvedPath.find( cacheKey );
                if ( it == state._mapResolvedPath.end() )
                    return false;
                if ( it->second._epoch != selectEpoch( it->second._path, creationEpoch, removalEpoch ) )
                    return false; // 기억한 뒤 파일이 만들어지거나 지워졌다 — 다시 훑는다
                outPath = it->second._path;
                return true;
            }

            /** @brief 푼 결과(@p path 가 비면 "없음")를 저장합니다. @p stamp 의 검색 목록 세대가 이미 낡았으면 저장하지 않습니다. */
            static void storeResolved( State& state, string&& cacheKey, const string& path, const PathCacheStamp& stamp )
            {
                // 캐시는 에셋 색인의 몫이다 — 부르는 쪽(스플래시 · 모듈 · 워커)의 태그가 아니라 Asset 으로 센다(재해시가 어디서 일어나든).
                SW_MEMORY_SCOPE( Asset );
                ScopedLock<mutex> lock( state._cacheMutex );
                if ( stamp._layoutGeneration != state._layoutGeneration )
                    return;
                const auto inserted           = state._mapResolvedPath.try_emplace( std::move( cacheKey ) );
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

            /** @brief 소문자 id 의 첫 성분이 붙인 도메인이면 그 폴더와 나머지 키를 줍니다. */
            [[nodiscard]] static bool findMountedDiskDomain( State& state, string_view lowerRel, string& outRoot, string& outKeyUnderRoot )
            {
                const size_t firstSlash = lowerRel.find( '/' );
                if ( firstSlash == string_view::npos || firstSlash == 0 )
                    return false;
                const string_view domain = lowerRel.substr( 0, firstSlash );
                ScopedLock<mutex> lock{ state._diskDomainMutex };
                for ( const MountedDiskDomain& mounted : state._listDiskDomain )
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
            [[nodiscard]] static bool findDiskDomainID( State& state, const string& absolutePath, string& outID )
            {
                const string      path = FileUtil::normalizePath( absolutePath );
                ScopedLock<mutex> lock{ state._diskDomainMutex };
                for ( const MountedDiskDomain& mounted : state._listDiskDomain )
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
             * @brief 락 없이 읽는 표(검색 목록 · 루트 · 호스트 제외 그룹)를 갈아 끼우는 API 가 병렬 단계 중에 불리지 않았는지 단언합니다.
             * @details 쓰기는 메인 스레드의 직렬 구간(기동 · 설정 로드 · 에디터 모드 전환)에서만 한다. `initialize()` 의 첫 호출은 소유자의 기동(직렬)이라 대상이 아니다.
             * @param pAPIName 로그에 남길 API 이름
             */
            static void assertSerialPhase( const utf8* pAPIName )
            {
                SW_LOG_ASSERT( TaskManager::isInParallelBody() == false && TaskManager::isOnPoolWorkerThread() == false,
                               "AssetPaths::%# swaps the lock-free path table and must run in a serial phase (not on a worker or inside a parallel body)", pAPIName );
            }

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
            static bool mapGlobalIDToRoot( State& state, string_view lowerRel, string& outRoot, string& outKeyUnderRoot )
            {
                outRoot.clear();
                outKeyUnderRoot.clear();

                // 0. 붙인 디스크 도메인(`mountDiskDomain`)은 리소스 루트와 무관하게 그 폴더로 푼다.
                const bool bAssetID = lowerRel.empty() == false && FileUtil::isAbsolutePath( lowerRel ) == false;
                if ( bAssetID && findMountedDiskDomain( state, lowerRel, outRoot, outKeyUnderRoot ) )
                    return true;

                const string& resourceRoot      = getTable( state )._resourceRootFolderPath; // 표는 놓지 않으므로 참조가 유효하다
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
    SW_LOG_CALLER( "AssetPaths" );

    AssetPaths::AssetPaths()
        : _pState{ make_unique<State>() }
    {
    }

    AssetPaths::~AssetPaths() = default; // `State` 가 여기서 완전한 형식이다

    /**
     * @brief 상대 id 에 `..` 성분이 있으면 true 입니다 — 리소스 루트 밖으로 나가는 id 입니다.
     * @details 거르지 않으면 `engine/../../x` 같은 id 가 도메인 루트에 붙어 루트 밖 파일을 읽는다(언리얼은 경로를 가상
     *          경로 · 팩 마운트 지점 안으로 가둔다). 저장소의 데이터 · 코드에는 `..` 를 쓰는 id 가 없다.
     */
    bool AssetPaths::hasParentDirectoryComponent( string_view path )
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

    bool AssetPaths::initialize()
    {
        // `_bInitialized` 는 "시작했다" 가 아니라 **"성공했다"** 를 뜻한다 — 루트를 못 찾아 false 로 나가면 다음 호출이 다시 찾는다.
        // 예전에는 프로세스 정적이라 `call_once` 로 막았다. 이제 부르는 쪽은 이 객체의 소유자 하나(기동의 직렬 구간)라 겹치지 않는다.
        if ( _pState->_bInitialized )
            return true;
        clearPathCache();

        string currentPath = FileUtil::getCurrentPath();
        string rootPath;
        while ( currentPath.empty() == false )
        {
            const string folderPath = FileUtil::joinPath( currentPath, path::kContentFolder );
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

        SW_LOG_ASSERT( rootPath.empty() == false, "RootFolder not found" );
        if ( rootPath.empty() )
            return false; // 플래그를 켜지 않는다.
        SW_LOG_INFO( "RootFolder : %#", rootPath );

        const string resourceRoot = FileUtil::joinPath( rootPath, path::kContentFolder );
        // Content/ 최상위 루트. 모든 도메인은 이 아래에서 이름으로 푼다.
        const string resourceRootFolderPath = FileUtil::isDirectory( resourceRoot ) ? FileUtil::normalizeSeparators( resourceRoot ) : string{};

        vector<string> listSearchPriority = AssetPathsInternal::getTable( *_pState )._listSearchPriority;
        if ( listSearchPriority.empty() )
            listSearchPriority = getDefaultSearchPriority();
        if ( listSearchPriority.empty() )
        {
            // 읽지 못한 이유를 여기서 남긴다(읽는 함수는 정적 초기화 가드 안이라 로그하지 않는다). 가드 안에서 만든 값이므로 다시 읽어도 같은 문장이다.
            SW_LOG_ERROR( "%#", AssetPathsInternal::readBasePriority()._error );
            return false; // 검색 목록 없이는 어떤 리소스도 찾지 못한다 — 플래그를 켜지 않아 false 를 반환한다.
        }

        // 루트 · 우선순위 · 검색 루트를 한 표로 한 번에 갈아 끼운다(읽는 쪽이 새 루트와 옛 검색 루트를 섞어 보지 않는다).
        vector<string> listAssetFolder = AssetPathsInternal::buildResourceFolders( resourceRootFolderPath, listSearchPriority );
        (void)AssetPathsInternal::publishTable( *_pState, [&]( AssetPathTable& table )
        {
            table._projectFolderPath      = FileUtil::normalizeSeparators( FileUtil::trimTrailingSlashes( rootPath ) );
            table._resourceRootFolderPath = resourceRootFolderPath;
            table._listSearchPriority     = std::move( listSearchPriority );
            table._listResourceFolder     = std::move( listAssetFolder );
        } );
        clearPathCache();

        // 여기까지 와야 상태가 모두 채워진 것이다.
        _pState->_bInitialized = true;
        return true;
    }

    bool AssetPaths::isInitialized() const
    {
        return _pState->_bInitialized;
    }

    LooseFilePath AssetPaths::findLooseFile( string_view filePath, string_view folderName ) const
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

        State& state = *_pState;
        // 원문 키다 — 해시만 비교하면 충돌한 다른 파일의 경로를 조용히 돌려준다. `\n` 은 파일 id 에 들어갈 수 없어 둘을 가르는 구분자다.
        string cacheKey( filePath );
        if ( folderName.empty() == false )
        {
            cacheKey += '\n';
            cacheKey += folderName;
        }

        // 세대는 찾기 전에 잡는다 — 찾는 동안 검색 목록이 바뀌었거나 파일이 만들어졌으면 이 결과는 낡은 채로 저장되지 못하거나(목록) 다음 조회에서 버려진다(음성).
        const PathCacheStamp stamp = AssetPathsInternal::captureStamp( state );
        {
            string cached;
            if ( AssetPathsInternal::findResolved( state, cacheKey, cached ) )
            {
                AssetPathsInternal::getPathStats().recordHit();
                if ( cached.empty() )
                    AssetPathsInternal::getMissingStats().recordHit();
                return LooseFilePath( std::move( cached ) );
            }
        }
        AssetPathsInternal::getPathStats().recordMiss();

        const string lowerFile   = FileUtil::normalizePath( filePath );
        const string lowerFolder = folderName.empty() ? string{} : FileUtil::normalizePath( folderName );

        string found;
        string domainRoot;
        string keyUnderRoot;
        if ( AssetPathsInternal::mapGlobalIDToRoot( state, lowerFile, domainRoot, keyUnderRoot ) )
        {
            found = AssetPathsInternal::tryUnderRoot( domainRoot, keyUnderRoot, lowerFolder );
#if defined( SW_PLATFORM_LINUX )
            // 대소문자를 가리는 파일시스템: 소문자 키로 못 찾았으면 원래 표기로 다시 찾는다.
            if ( found.empty() && lowerFile != filePath )
            {
                const string raw    = string{ filePath };
                string       rawKey = keyUnderRoot;
                const size_t slash  = FileUtil::normalizeSeparators( raw ).find( '/' );
                if ( slash != string::npos )
                    rawKey = FileUtil::normalizeSeparators( raw ).substr( slash + 1 );
                found = AssetPathsInternal::tryUnderRoot( domainRoot, rawKey, folderName );
            }
            // 붙인 디스크 도메인의 폴더는 대소문자가 섞일 수 있고, 기록해 둔 id(쿠킹 입력 등)는 소문자뿐이라 원래 표기가 없다 — 폴더를 훑어 소문자가 같은 항목으로 푼다.
            // 리소스 트리의 도메인은 소문자 규칙이라 훑지 않는다(없는 파일을 묻는 호출이 훑기를 부르지 않게).
            string mountedRoot;
            string mountedKey;
            if ( found.empty() && AssetPathsInternal::findMountedDiskDomain( state, lowerFile, mountedRoot, mountedKey ) )
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
            const vector<string>& listAssetFolder = AssetPathsInternal::getTable( state )._listResourceFolder; // 표 하나를 잡아 그 안에서 읽는다
            found                                 = AssetPathsInternal::tryResolveAmong( listAssetFolder, lowerFile, lowerFolder );
#if defined( SW_PLATFORM_LINUX )
            if ( found.empty() && ( lowerFile != filePath || ( folderName.empty() == false && lowerFolder != folderName ) ) )
                found = AssetPathsInternal::tryResolveAmong( listAssetFolder, filePath, folderName );
#endif
        }

        if ( found.empty() )
            AssetPathsInternal::getMissingStats().recordMiss();
        // 없음도 저장한다(음성 결과) — 같은 없는 파일을 묻는 호출(선택적 파일 · 팩에만 있는 에셋)이 매번 모든 검색 루트를 훑지 않게.
        AssetPathsInternal::storeResolved( state, std::move( cacheKey ), found, stamp );

        return LooseFilePath( std::move( found ) );
    }

    std::optional<DomainAssetID> AssetPaths::toWriteID( string_view pathOrID ) const
    {
        // 절대 경로 · `Content/…` 는 리소스 id 로 바꾼다(붙인 디스크 도메인 아래면 그 도메인 id). 트리 밖이거나 `..` 로 올라가면 비어 있다.
        const string id = toAssetIDFromPathOrID( pathOrID );
        if ( id.empty() )
        {
            SW_LOG_ERROR_ID( "AssetPaths.WriteOutsideTree", "'%#' is outside the resource tree and mounted disk domains - nothing is written there", pathOrID );
            return std::nullopt;
        }
        string domainRoot;
        string keyUnderRoot;
        if ( AssetPathsInternal::mapGlobalIDToRoot( *_pState, id, domainRoot, keyUnderRoot ) )
            return DomainAssetID( id );

        // 상대 키 — 읽기가 검색 순서로 고른 그 파일에 쓴다. 아직 없으면 어느 도메인에 만들지 정할 수 없다.
        const LooseFilePath found   = findLooseFile( id );
        const string        foundID = found.isEmpty() ? string{} : toAssetID( found.getDiskPath() );
        if ( foundID.empty() )
        {
            SW_LOG_ERROR_ID( "AssetPaths.WriteRelativeKeyMissing", "'%#' is a relative key with no file yet - give the domain (e.g. game/<pack>/%#) to create it", pathOrID, id );
            return std::nullopt;
        }
        return DomainAssetID( foundID );
    }

    string AssetPaths::getWritePath( string_view pathOrID ) const
    {
        // 절대 경로는 부르는 쪽이 고른 디스크 자리다(테스트 · 도구의 작업 폴더) — 도메인을 고를 일이 없다.
        if ( FileUtil::isAbsolutePath( pathOrID ) )
            return FileUtil::normalizeSeparators( pathOrID );
        const std::optional<DomainAssetID> writeID = toWriteID( pathOrID );
        return writeID.has_value() ? resolveWritePath( *writeID ) : string{};
    }

    string AssetPaths::toAssetID( string_view diskPath ) const
    {
        string absolutePath;
        if ( diskPath.empty() || FileUtil::makeAbsolutePath( diskPath, absolutePath ) == false )
            return {};
        absolutePath = FileUtil::normalizeSeparators( absolutePath );
        string domainID;
        // 붙인 디스크 도메인(시나리오의 `Saved/` 등) 아래면 그 도메인의 id 다 — 리소스 입구는 OS 경로를 받지 않는다.
        if ( AssetPathsInternal::findDiskDomainID( *_pState, absolutePath, domainID ) )
            return normalizeAssetID( domainID );
        const string root = FileUtil::trimTrailingSlashes( FileUtil::normalizeSeparators( getRootFolderPath() ) );
        string       underRoot;
        if ( root.empty() || FileUtil::makeRelativePath( root, absolutePath, underRoot ) == false )
            return {};
        return normalizeAssetID( underRoot );
    }

    string AssetPaths::toAssetIDFromPathOrID( string_view pathOrID ) const
    {
        return FileUtil::isAbsolutePath( pathOrID ) ? toAssetID( pathOrID ) : normalizeAssetID( pathOrID );
    }

    string AssetPaths::normalizeAssetID( string_view text )
    {
        string id = FileUtil::normalizePath( FileUtil::normalizeSeparators( text ) );
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

    string AssetPaths::resolveWritePath( const DomainAssetID& assetID ) const
    {
        // 이미 있는 파일이면 그 디스크 경로가 답이다(덮어쓸 자리).
        string result = findLooseFile( assetID.str() ).getDiskPath();
        if ( result.empty() == false )
            return result;

        string domainRoot;
        string keyUnderRoot;
        if ( AssetPathsInternal::mapGlobalIDToRoot( *_pState, assetID.str(), domainRoot, keyUnderRoot ) == false )
            return result;

        // 붙인 디스크 도메인의 폴더는 대소문자가 섞일 수 있다. 대소문자를 구분하는 파일 시스템에서 소문자 키를 그대로 이으면 이미 있는 폴더 옆에 대소문자만 다른 새 폴더가 생기므로,
        // 있는 성분은 디스크 표기를 쓰고 없는 성분부터는 id 의 표기를 쓴다. 리소스 트리의 도메인은 소문자 규칙이라 훑지 않고 새 성분도 소문자다(Windows 도 그대로 소문자로 잇는다).
        string mountedRoot;
        string mountedKey;
        if ( AssetPathsInternal::kCaseSensitiveFileSystem && AssetPathsInternal::findMountedDiskDomain( *_pState, assetID.str(), mountedRoot, mountedKey ) )
            return FileUtil::makePathIgnoringCase( mountedRoot, assetID.str().substr( assetID.str().find( '/' ) + 1 ) );
        return FileUtil::joinPath( domainRoot, keyUnderRoot );
    }

    LooseFilePath AssetPaths::findLooseDomainFolder( string_view domainName, string_view subFolder ) const
    {
        string folder = AssetPathsInternal::findDomainFolderUnder( AssetPathsInternal::getTable( *_pState )._resourceRootFolderPath, domainName, subFolder );
        if ( folder.empty() )
            return {};
        return LooseFilePath( std::move( folder ) );
    }

    bool AssetPaths::mountDiskDomain( string_view domainName, string_view diskFolder )
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
        const string root = FileUtil::trimTrailingSlashes( FileUtil::normalizeSeparators( AssetPathsInternal::getTable( *_pState )._resourceRootFolderPath ) );
        if ( root.empty() == false && FileUtil::startsWithPathComponent( FileUtil::normalizePath( folder ), FileUtil::normalizePath( root ) ) )
        {
            SW_LOG_ERROR( "Cannot mount disk domain '%#' - '%#' is inside the content root (packs-only builds would read loose assets)", domainName, diskFolder );
            return false;
        }
        // 락 안에서는 목록만 만지고, 오류는 이미 붙은 폴더를 적어 두었다가 락을 놓은 뒤에 남긴다(락을 쥔 채 로그를 부르지 않는다 — 로그 싱크가 무엇을 하든
        // 이 락과 얽히지 않게 한다).
        State& state = *_pState;
        string conflictFolder;
        bool   bConflict{ false };
        {
            ScopedLock<mutex> lock{ state._diskDomainMutex };
            for ( const MountedDiskDomain& mounted : state._listDiskDomain )
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
                state._listDiskDomain.push_back( MountedDiskDomain{ domain, folder } );
        }
        if ( bConflict )
        {
            SW_LOG_ERROR( "Cannot mount disk domain '%#' at '%#' - it is already mounted at '%#'", domainName, diskFolder, conflictFolder );
            return false;
        }
        clearPathCache();
        return true;
    }

    bool AssetPaths::isMountedDiskDomainID( string_view assetID ) const
    {
        string root;
        string key;
        return assetID.empty() == false && FileUtil::isAbsolutePath( assetID ) == false &&
               AssetPathsInternal::findMountedDiskDomain( *_pState, FileUtil::normalizePath( assetID ), root, key );
    }

    const string& AssetPaths::getRootFolderPath() const
    {
        return AssetPathsInternal::getTable( *_pState )._resourceRootFolderPath; // 표는 놓지 않으므로 참조가 유효하다
    }

    const string& AssetPaths::getProjectFolderPath() const
    {
        return AssetPathsInternal::getTable( *_pState )._projectFolderPath;
    }

    bool AssetPaths::setSearchPriority( const vector<string>& listPriority )
    {
        if ( listPriority.empty() )
            return false;

        // 첫 `initialize()` 는 소유자의 기동(직렬)에서 표를 짓는다 — 성공 표시가 서기 전의 호출은 단언 대상이 아니다.
        if ( _pState->_bInitialized )
            AssetPathsInternal::assertSerialPhase( "setSearchPriority" );

        // 검색 루트는 디스크를 보고 짓는다 — 잠금 밖에서 지은 뒤 우선순위와 함께 한 표로 갈아 끼운다.
        vector<string> listAssetFolder = AssetPathsInternal::buildResourceFolders( AssetPathsInternal::getTable( *_pState )._resourceRootFolderPath, listPriority );
        (void)AssetPathsInternal::publishTable( *_pState, [&]( AssetPathTable& table )
        {
            table._listSearchPriority = listPriority;
            if ( table._resourceRootFolderPath.empty() == false )
                table._listResourceFolder = std::move( listAssetFolder );
        } );
        clearPathCache();
        return true;
    }

    const vector<string>& AssetPaths::getSearchPriority() const
    {
        return AssetPathsInternal::getTable( *_pState )._listSearchPriority;
    }

    const vector<string>& AssetPaths::getDefaultSearchPriority()
    {
        // 값의 출처는 `Config/Engine/EngineConfig.json` 하나다. 기동의 이 시점에는 설정을 아직 읽지 않았으므로 구성 때 베이크한 같은 파일의 사본에서
        // 이 칸만 읽는다(리플렉션이 서기 전이라 JSON 문서로). 실행 중 값은 설정을 읽은 뒤 `AssetManager::mountContent` 가 다시 넣는다.
        // 구성 때 정해진 상수라 객체 상태가 아니다(어느 객체가 읽어도 같다). 오류 로그는 함수 정적 초기화 가드 안에서 남기지 않는다 — 로거가 가드를 쥔 채
        // 되돌아 들어오면 교착이다. 오류 문장은 `initialize()` 가 가드 밖에서 남긴다.
        static const BasePriority s_basePriority = AssetPathsInternal::readBasePriority();
        return s_basePriority._listPriority;
    }

    void AssetPaths::clearPathCache()
    {
        State&            state = *_pState;
        ScopedLock<mutex> lock( state._cacheMutex );
        // `clear()` 는 버킷 · 밀집 배열을 남긴다 — 검색 목록이 바뀐 뒤의 캐시가 자란 만큼 남지 않게 저장소까지 놓는다. 끝의 반납은 이 객체의 소멸이 한다.
        // 세대를 올려, 이 순간 이전의 검색 목록으로 풀던 호출이 결과를 새 캐시에 넣지 못하게 한다(`storeResolved`).
        ++state._layoutGeneration;
        AssetPathsInternal::getPathStats().addItemCount( -static_cast<int64>( state._mapResolvedPath.size() ) );
        AssetPathsInternal::getPathStats().recordClear();
        state._mapResolvedPath = unordered_map<string, ResolvedEntry>{};
    }

    string AssetPaths::makeSaveFolderPath( string_view absoluteFolder ) const
    {
        const string folderNorm = FileUtil::trimTrailingSlashes( FileUtil::normalizePath( absoluteFolder ) );

        string physicalRoot;
        string rootNorm;

        for ( const string& root : AssetPathsInternal::getTable( *_pState )._listResourceFolder )
        {
            AssetPathsInternal::updateLongestSaveRoot( folderNorm, root, physicalRoot, rootNorm );
        }
        AssetPathsInternal::updateLongestSaveRoot( folderNorm, findLooseDomainFolder( path::kGamePack ).getDiskPath(), physicalRoot, rootNorm );
        AssetPathsInternal::updateLongestSaveRoot( folderNorm, findLooseDomainFolder( path::kEnginePack ).getDiskPath(), physicalRoot, rootNorm );
        AssetPathsInternal::updateLongestSaveRoot( folderNorm, findLooseDomainFolder( path::kCommonPack ).getDiskPath(), physicalRoot, rootNorm );
        AssetPathsInternal::updateLongestSaveRoot( folderNorm, findLooseDomainFolder( path::kEditorPack ).getDiskPath(), physicalRoot, rootNorm );

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

    string AssetPaths::makeSavePath( string_view absoluteFolder, string_view fileName ) const
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

    string AssetPaths::makeUniqueSavePath( string_view absoluteFolder, string_view fileName ) const
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

    void AssetPaths::setHostTarget( string_view buildTargetName )
    {
        AssetPathsInternal::assertSerialPhase( "setHostTarget" );
        vector<string> listHostExcludedGroup;
#define SW_ADD_HOST_EXCLUDED_GROUP( Target, Group ) \
    if ( buildTargetName == #Target )               \
        listHostExcludedGroup.emplace_back( #Group );
        SW_TARGET_EXCLUDED_GROUP_TABLE( SW_ADD_HOST_EXCLUDED_GROUP )
#undef SW_ADD_HOST_EXCLUDED_GROUP
        const size_t excludedCount = listHostExcludedGroup.size();
        (void)AssetPathsInternal::publishTable( *_pState, [&]( AssetPathTable& table )
        { table._listHostExcludedGroup = std::move( listHostExcludedGroup ); } );
        if ( excludedCount > 0 )
            SW_LOG_INFO( "Host target %# does not read %# asset groups", string( buildTargetName ).c_str(), excludedCount );
    }

    bool AssetPaths::isExcludedForHost( string_view assetPath ) const
    {
        const vector<string>& listHostExcludedGroup = AssetPathsInternal::getTable( *_pState )._listHostExcludedGroup; // 표 하나를 잡아 그 안에서 읽는다
        if ( listHostExcludedGroup.empty() )
            return false;
        const string_view group = AssetTypeRegistry::findExcludeGroup( assetPath );
        if ( group.empty() )
            return false;
        for ( const string& excluded : listHostExcludedGroup )
        {
            if ( StringUtil::equals( excluded, group, true ) )
                return true;
        }
        return false;
    }

    bool AssetPaths::isExcludedForTarget( string_view buildTargetName, string_view assetPath )
    {
        if ( buildTargetName.empty() )
            return false;
        const string_view group = AssetTypeRegistry::findExcludeGroup( assetPath );
        if ( group.empty() )
            return false;
#define SW_MATCH_TARGET_EXCLUDED_GROUP( Target, Group )                            \
    if ( buildTargetName == #Target && StringUtil::equals( group, #Group, true ) ) \
        return true;
        SW_TARGET_EXCLUDED_GROUP_TABLE( SW_MATCH_TARGET_EXCLUDED_GROUP )
#undef SW_MATCH_TARGET_EXCLUDED_GROUP
        return false;
    }

    bool AssetPaths::collectContentFiles( string_view contentRoot, string_view extension, vector<string>& outListFile )
    {
        if ( FileUtil::collectFiles( contentRoot, extension, outListFile, true ) == false )
            return false;
        const string scratchPrefix = FileUtil::trimTrailingSlashes( FileUtil::normalizeSeparators( contentRoot ) ) + "/" + kScratchDomain + "/";
        const auto   isScratch     = [&scratchPrefix]( const string& path )
        { return StringUtil::startsWith( FileUtil::normalizeSeparators( path ), scratchPrefix, true ); };
        outListFile.erase( std::remove_if( outListFile.begin(), outListFile.end(), isScratch ), outListFile.end() );
        return true;
    }
} // namespace sw
