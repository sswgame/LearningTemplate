#include "pch.h"

#include "Engine/FileSystem/Pack/PackManager.h"

#include "Core/Container/CompareUtil.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"
#include "Core/Network/Security/INetSecurityProvider.h"

#include "Engine/FileSystem/AssetIO.h"
#include "Engine/FileSystem/AssetPaths.h"
#include "Engine/FileSystem/Pack/PackPatch.h"
#include "Engine/Foundation/EngineServices.h"

namespace sw
{
    namespace
    {
        SW_LOG_CALLER( "PackManager" );

        struct PackManagerInternal
        {
            static constexpr int32 kPriorityStep       = 1000; ///< 우선순위 토큰 한 칸
            static constexpr int32 kPatchPriorityBonus = 500;  ///< patch_ 팩은 대상 모듈보다 반 칸 위

            /**
             * @brief 팩 파일 이름이 우선순위 토큰과 맞는지 대소문자를 무시하고 판별합니다(할당 없음).
             * @param stem 팩 파일 이름(확장자 제외. 예: "game", "game_patch", "dlc_expansion")
             * @param token 검색 우선순위 토큰(예: "game", "dlc", "engine")
             */
            static bool matchesTokenCaseInsensitive( string_view stem, string_view token )
            {
                if ( stem.empty() || token.empty() )
                    return false;

                if ( StringUtil::equals( stem, token, true ) )
                    return true;

                // token + '_' 또는 token + '.' 접두사와 맞춰 본다
                if ( stem.size() > token.size() && StringUtil::startsWith( stem, token, true ) )
                {
                    const utf8 delimiter = stem[token.size()];
                    if ( delimiter == '_' || delimiter == '.' )
                        return true;
                }

                return false;
            }

            /**
             * @brief EngineConfig 의 리소스 우선순위 목록(listPriority)으로 팩의 기본 마운트 우선순위를 계산합니다(할당 없음).
             */
            static int32 computePackDefaultPriority( string_view packFileName, const vector<string>& listPriority )
            {
                string_view fileName;
                FileUtil::getFileNamePart( packFileName, fileName );
                string_view stem;
                FileUtil::removeExtension( fileName, stem );
                if ( stem.empty() )
                    return 0;

                const vector<string>& listPriorityEffective = listPriority.empty() ? AssetPaths::getDefaultSearchPriority() : listPriority;
                const size_t          priorityCount         = listPriorityEffective.size();

                // 1. listPriorityEffective 순서대로 맞는 토큰을 찾는다(앞쪽 인덱스일수록 우선순위가 높다)
                for ( size_t index = 0; index < priorityCount; ++index )
                {
                    const string_view token{ listPriorityEffective[index] };
                    if ( matchesTokenCaseInsensitive( stem, token ) )
                        return static_cast<int32>( priorityCount - index ) * kPriorityStep;
                }

                // 2. 패치 접두사(patch_)가 붙으면 대상 모듈 우선순위에 500 을 더한다
                if ( stem.size() >= 6 && StringUtil::startsWith( stem, "patch_", true ) )
                {
                    const string_view subStem = stem.substr( 6 );
                    for ( size_t index = 0; index < priorityCount; ++index )
                    {
                        const string_view token{ listPriorityEffective[index] };
                        if ( matchesTokenCaseInsensitive( subStem, token ) )
                            return static_cast<int32>( priorityCount - index ) * kPriorityStep + kPatchPriorityBonus;
                    }
                    // 대상 모듈이 없는 단독 핫픽스 팩(가장 높은 우선순위)
                    return static_cast<int32>( priorityCount + 1 ) * kPriorityStep;
                }

                // 3. 맞는 우선순위 토큰이 없는 일반 팩의 기본 우선순위
                return 0;
            }

            /**
             * @brief "engine/textures/splash.dds" 나 "game/empty/maps/title.xml" 을 도메인과 나머지 경로로 나눕니다.
             */
            [[nodiscard]] static bool trySplitDomainPrefix( string_view relativePath, string_view& outDomain, string_view& outSubPath )
            {
                outDomain  = {};
                outSubPath = {};

                const size_t firstSlash = relativePath.find( '/' );
                if ( firstSlash == string_view::npos )
                    return false;

                const string_view firstPart = relativePath.substr( 0, firstSlash );
                // game/<pack>/... 인 경우
                if ( StringUtil::equals( firstPart, "game", true ) )
                {
                    const size_t secondSlash = relativePath.find( '/', firstSlash + 1 );
                    if ( secondSlash != string_view::npos )
                    {
                        outDomain  = relativePath.substr( 0, secondSlash ); // 예: "game/empty"
                        outSubPath = relativePath.substr( secondSlash + 1 );
                        return outSubPath.empty() == false;
                    }
                }

                outDomain  = firstPart; // 예: "engine", "common", "editor", "dlc"
                outSubPath = relativePath.substr( firstSlash + 1 );
                return outSubPath.empty() == false;
            }

            /**
             * @brief 마운트된 팩 도메인("engine", "common", "game_empty")과 질의 도메인("engine", "game/empty")을 대소문자를 무시하고 맞춰 봅니다.
             */
            static bool matchPackDomain( string_view packDomain, string_view queryDomain )
            {
                if ( StringUtil::equals( packDomain, queryDomain, true ) )
                    return true;

                // "game_empty" 와 "game/empty" 를 같은 것으로 본다
                if ( queryDomain.find( '/' ) != string_view::npos )
                {
                    string converted{ queryDomain };
                    StringUtil::replaceChar( converted, '/', '_' );
                    if ( StringUtil::equals( packDomain, converted, true ) )
                        return true;
                }

                return false;
            }

            /** @brief 후보 파일 가운데 패치 목록(`*.packpatch.xml`)을 모두 읽습니다. 하나라도 읽지 못하면 이유를 남기고 false 입니다. */
            [[nodiscard]] static bool collectPatchManifests( const vector<string>& listCandidateFile, vector<PackPatchManifest>& outListManifest )
            {
                outListManifest.clear();
                for ( const string& candidate : listCandidateFile )
                {
                    if ( StringUtil::endsWith( candidate, PackPatchUtil::kManifestExtension, true ) == false )
                        continue;
                    string text;
                    if ( FileUtil::readTextFile( candidate, text ) == false )
                    {
                        SW_LOG_ERROR( "Pack patch manifest could not be read: %#", candidate );
                        return false;
                    }
                    PackPatchManifest manifest;
                    if ( PackPatchUtil::parseManifest( text, candidate, manifest ) == false )
                        return false;
                    outListManifest.push_back( std::move( manifest ) );
                }
                return true;
            }

            /** @brief @p packPath 가 패치 목록이 가리키는 패치 팩이면 true 입니다(파일 이름 비교, 대소문자 무시). */
            static bool isPatchPackFile( const string& packPath, const vector<PackPatchManifest>& listManifest )
            {
                string_view fileName;
                FileUtil::getFileNamePart( packPath, fileName );
                for ( const PackPatchManifest& manifest : listManifest )
                {
                    if ( StringUtil::equals( fileName, manifest._packFileName, true ) )
                        return true;
                }
                return false;
            }

            /**
             * @brief 전체 경로 해시 @p pathHash 를 지운 패치 팩 중 가장 우선순위가 높은 것의 인덱스입니다. 없으면 목록 크기입니다.
             * @details 그 인덱스보다 앞(우선순위가 높은) 팩만 그 경로를 줄 수 있다 — 패치보다 아래 팩의 같은 경로는 없는 것이다.
             */
            static size_t findRemovingPackIndex( const vector<MountedPack>& listMountedPack, uint64 pathHash )
            {
                for ( size_t packIndex = 0; packIndex < listMountedPack.size(); ++packIndex )
                {
                    const vector<uint64>& listRemoved = listMountedPack[packIndex]._listRemovedPathHash;
                    if ( std::binary_search( listRemoved.begin(), listRemoved.end(), pathHash ) )
                        return packIndex;
                }
                return listMountedPack.size();
            }

            /**
             * @brief 경로를 가진 팩을 찾아 `visit( mounted, pathHash, pathInPack )` 을 부릅니다. 방문자가 true 를 반환하면 멈춥니다.
             * @details 두 단계입니다. 전체 경로의 해시로 모든 팩을 보고, 그다음 경로가 도메인으로 시작하면(`engine/…`) 그 도메인 팩에서
             *          나머지 경로로 봅니다. `hasFile` · `readFile` · `readTextFile` 이 함께 씁니다. 잠금은 부르는 쪽이 쥡니다.
             * @return 방문자가 true 를 반환한 적이 있으면 true 입니다.
             */
            template <typename VisitFn>
            static bool visitPacksWithFile( const vector<MountedPack>& listMountedPack, string_view relativePath, VisitFn&& visit )
            {
                const uint64 pathHash  = HashUtil::computeStringHash64( relativePath );
                const size_t packLimit = findRemovingPackIndex( listMountedPack, pathHash );
                for ( size_t packIndex = 0; packIndex < packLimit; ++packIndex )
                {
                    const MountedPack& mounted      = listMountedPack[packIndex];
                    const bool         bPackHasFile = mounted._pReader != nullptr && mounted._pReader->hasFile( pathHash ) && visit( mounted, pathHash, relativePath );
                    if ( bPackHasFile )
                        return true;
                }

                string_view queryDomain;
                string_view subPath;
                if ( trySplitDomainPrefix( relativePath, queryDomain, subPath ) == false )
                    return false;

                const uint64 subHash = HashUtil::computeStringHash64( subPath );
                for ( size_t packIndex = 0; packIndex < packLimit; ++packIndex )
                {
                    const MountedPack& mounted = listMountedPack[packIndex];
                    if ( mounted._pReader == nullptr || matchPackDomain( mounted._domainName, queryDomain ) == false )
                        continue;
                    if ( mounted._pReader->hasFile( subHash ) && visit( mounted, subHash, subPath ) )
                        return true;
                }
                return false;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    PackManager::PackManager()
        : _vfsMutex{}
        , _listMountedPack{}
        , _dlcValidator{}
        , _contentSource{ kBuildContentSource }
    {
    }

    PackManager::~PackManager()
    {
        unmountAll();
    }

    PackManager::PackManager( PackManager&& other ) noexcept
    {
        ScopedLock<mutex> lock{ other._vfsMutex };
        _listMountedPack = std::move( other._listMountedPack );
        _dlcValidator    = std::move( other._dlcValidator );
        _contentSource.store( other._contentSource.load( std::memory_order_relaxed ), std::memory_order_relaxed );
    }

    PackManager& PackManager::operator=( PackManager&& other ) noexcept
    {
        if ( this != &other )
        {
            ScopedLockPair<mutex> lock{ _vfsMutex, other._vfsMutex };
            _listMountedPack = std::move( other._listMountedPack );
            _dlcValidator    = std::move( other._dlcValidator );
            _contentSource.store( other._contentSource.load( std::memory_order_relaxed ), std::memory_order_relaxed );
        }
        return *this;
    }

    bool PackManager::mountPack( string_view packFilePath, int32 priority )
    {
        return mountReader( packFilePath, priority, vector<uint64>{} );
    }

    bool PackManager::mountPatchPack( string_view packFilePath, int32 priority, const vector<string>& listRemovedPath )
    {
        vector<uint64> listRemovedPathHash;
        listRemovedPathHash.reserve( listRemovedPath.size() );
        for ( const string& removedPath : listRemovedPath )
        {
            listRemovedPathHash.push_back( HashUtil::computeStringHash64( removedPath ) );
        }
        VectorUtil::sort( listRemovedPathHash );
        return mountReader( packFilePath, priority, std::move( listRemovedPathHash ) );
    }

    bool PackManager::mountReader( string_view packFilePath, int32 priority, vector<uint64> listRemovedPathHash )
    {
        if ( packFilePath.empty() )
            return false;

        const string normalizedPath = FileUtil::normalizeSeparators( packFilePath );

        int32 effectivePriority = priority;
        if ( effectivePriority <= 0 )
            effectivePriority = PackManagerInternal::computePackDefaultPriority( packFilePath, engine::getSubsystem<AssetPaths>().getSearchPriority() );

        shared_ptr<PackReader> pReader = sw::make_shared<PackReader>();
        if ( pReader->open( normalizedPath ) == false )
        {
            SW_LOG_ERROR( "Failed to open and mount pack: %#", packFilePath );
            return false;
        }

        // DLC 소유권을 확인한다
        const uint32 dlcAppID = pReader->getDLCAppID();
        if ( dlcAppID > 0 )
        {
            if ( _dlcValidator.isBound() && _dlcValidator( dlcAppID ) == false )
            {
                SW_LOG_WARNING( "Access denied for DLC pack %# (DLC AppID %# not owned)", packFilePath, dlcAppID );
                return false;
            }
        }

        {
            ScopedLock<mutex> lock{ _vfsMutex };

            // 이미 마운트되어 있으면 이전 팩을 뺀다
            for ( auto it = _listMountedPack.begin(); it != _listMountedPack.end(); ++it )
            {
                if ( it->_pReader != nullptr && FileUtil::pathsEqualNormalized( it->_pReader->getPackPath(), normalizedPath ) )
                {
                    _listMountedPack.erase( it );
                    break;
                }
            }

            string_view fileNamePart;
            FileUtil::getFileNamePart( normalizedPath, fileNamePart );
            string_view stem;
            FileUtil::removeExtension( fileNamePart, stem );

            MountedPack mounted{};
            mounted._domainName          = string( stem );
            mounted._priority            = effectivePriority;
            mounted._pReader             = std::move( pReader );
            mounted._listRemovedPathHash = std::move( listRemovedPathHash );

            _listMountedPack.push_back( std::move( mounted ) );

            // 우선순위 내림차순으로 정렬한다(높은 priority 가 앞)
            VectorUtil::stableSort( _listMountedPack, Descending<&MountedPack::_priority>{} );
        }

        SW_LOG_INFO( "Mounted resource pack: %# (Priority: %#)", normalizedPath, effectivePriority );
        return true;
    }

    bool PackManager::unmountPack( string_view packFilePath )
    {
        if ( packFilePath.empty() )
            return false;

        bool bUnmounted{ false };
        {
            ScopedLock<mutex> lock{ _vfsMutex };
            for ( auto it = _listMountedPack.begin(); it != _listMountedPack.end(); ++it )
            {
                if ( it->_pReader != nullptr && FileUtil::pathsEqualNormalized( it->_pReader->getPackPath(), packFilePath ) )
                {
                    it->_pReader->close();
                    _listMountedPack.erase( it );
                    bUnmounted = true;
                    break;
                }
            }
        }
        if ( bUnmounted )
            SW_LOG_INFO( "Unmounted resource pack: %#", packFilePath ); // 잠금은 잎이다 — 로그는 놓은 뒤에 쓴다
        return bUnmounted;
    }

    void PackManager::unmountAll()
    {
        ScopedLock<mutex> lock{ _vfsMutex };
        for ( auto& mounted : _listMountedPack )
        {
            if ( mounted._pReader != nullptr )
                mounted._pReader->close();
        }
        _listMountedPack.clear();
    }

    bool PackManager::hasFile( string_view relativePath ) const
    {
        if ( relativePath.empty() )
            return false;

        ScopedLock<mutex> lock{ _vfsMutex };
        return PackManagerInternal::visitPacksWithFile( _listMountedPack, relativePath,
                                                        []( const MountedPack&, uint64, string_view )
        { return true; } );
    }

    void PackManager::collectFolderEntries( string_view relativeFolder, vector<string>& outListPath ) const
    {
        if ( relativeFolder.empty() )
            return;

        ScopedLock<mutex> lock{ _vfsMutex };
        // 패치 팩이 지운 경로는 그 패치보다 아래 팩에서 나온 항목이면 뺀다(`visitPacksWithFile` 과 같은 규칙).
        vector<string> listPackEntry;
        for ( size_t packIndex = 0; packIndex < _listMountedPack.size(); ++packIndex )
        {
            const MountedPack& mounted = _listMountedPack[packIndex];
            if ( mounted._pReader == nullptr )
                continue;
            listPackEntry.clear();
            mounted._pReader->collectFolderEntries( relativeFolder, listPackEntry );
            for ( string& entryPath : listPackEntry )
            {
                if ( PackManagerInternal::findRemovingPackIndex( _listMountedPack, HashUtil::computeStringHash64( entryPath ) ) > packIndex )
                    outListPath.push_back( std::move( entryPath ) );
            }
        }

        // 도메인 팩은 도메인을 뗀 경로로 담겨 있다(`visitPacksWithFile` 의 둘째 단계와 같은 규칙).
        string_view queryDomain;
        string_view subFolder;
        if ( PackManagerInternal::trySplitDomainPrefix( relativeFolder, queryDomain, subFolder ) == false )
            return;
        vector<string> listPathInPack;
        for ( size_t packIndex = 0; packIndex < _listMountedPack.size(); ++packIndex )
        {
            const MountedPack& mounted = _listMountedPack[packIndex];
            if ( mounted._pReader == nullptr || PackManagerInternal::matchPackDomain( mounted._domainName, queryDomain ) == false )
                continue;
            listPathInPack.clear();
            mounted._pReader->collectFolderEntries( subFolder, listPathInPack );
            for ( const string& pathInPack : listPathInPack )
            {
                string fullPath = FileUtil::joinPath( queryDomain, pathInPack );
                if ( PackManagerInternal::findRemovingPackIndex( _listMountedPack, HashUtil::computeStringHash64( fullPath ) ) > packIndex )
                    outListPath.push_back( std::move( fullPath ) );
            }
        }
    }

    shared_ptr<PackReader> PackManager::findReaderWithFile( string_view relativePath, uint64& outPathHash, string_view& outPathInPack ) const
    {
        shared_ptr<PackReader> pFound;
        ScopedLock<mutex>      lock{ _vfsMutex };
        PackManagerInternal::visitPacksWithFile( _listMountedPack, relativePath,
                                                 [&]( const MountedPack& mounted, uint64 pathHash, string_view pathInPack )
        {
            pFound        = mounted._pReader;
            outPathHash   = pathHash;
            outPathInPack = pathInPack;
            return true;
        } );
        return pFound;
    }

    bool PackManager::readFile( string_view relativePath, vector<uint8>& outBytes ) const
    {
        if ( relativePath.empty() )
            return false;

        uint64                       pathHash{ 0 };
        string_view                  pathInPack;
        const shared_ptr<PackReader> pReader = findReaderWithFile( relativePath, pathHash, pathInPack );
        return pReader != nullptr && pReader->readFile( pathHash, outBytes );
    }

    bool PackManager::readTextFile( string_view relativePath, string& outText, string* pOutMountedPackPath ) const
    {
        if ( relativePath.empty() )
            return false;

        uint64                       pathHash{ 0 };
        string_view                  pathInPack;
        const shared_ptr<PackReader> pReader = findReaderWithFile( relativePath, pathHash, pathInPack );
        if ( pReader == nullptr || pReader->readTextFile( pathInPack, outText ) == false )
            return false;
        if ( pOutMountedPackPath != nullptr )
            *pOutMountedPackPath = pReader->getPackPath();
        return true;
    }

    AsyncReadHandle PackManager::readFileAsync( AsyncFileIO& io, string_view relativePath, AsyncIOPriority priority, const AssetReadCompleteDelegate& onComplete ) const
    {
        if ( relativePath.empty() )
            return AsyncReadHandle{};

        uint64                       pathHash{ 0 };
        string_view                  pathInPack;
        const shared_ptr<PackReader> pReader = findReaderWithFile( relativePath, pathHash, pathInPack );
        if ( pReader == nullptr )
            return AsyncReadHandle{};
        return pReader->readFileAsync( io, pathHash, priority, onComplete );
    }

    void PackManager::setDLCEntitlementValidator( DLCEntitlementDelegate validator )
    {
        ScopedLock<mutex> lock{ _vfsMutex };
        _dlcValidator = std::move( validator );
    }

    void PackManager::setContentSource( ContentSource source )
    {
        _contentSource.store( source, std::memory_order_relaxed );
    }

    ContentSource PackManager::getContentSource() const
    {
        return _contentSource.load( std::memory_order_relaxed );
    }

    size_t PackManager::getMountedPackCount() const
    {
        ScopedLock<mutex> lock{ _vfsMutex };
        return _listMountedPack.size();
    }

    bool PackManager::scanAndMountPacks( string_view packsDirectory, const vector<string>& listPriority )
    {
        if ( packsDirectory.empty() || FileUtil::isDirectory( packsDirectory ) == false )
            return false;

        vector<string> listCandidateFile;
        if ( FileUtil::collectFiles( packsDirectory, "", listCandidateFile, false ) == false )
        {
            // 일부 팩만 마운트하면 빠진 팩의 파일이 아래 팩 판으로 조용히 바뀐다 — 아무것도 마운트하지 않는다.
            SW_LOG_ERROR( "Pack folder '%#' could not be listed completely - no pack is mounted", packsDirectory );
            return false;
        }

        if ( listCandidateFile.empty() )
            return false;

        // 패치 목록이 가리키는 팩은 기본 팩이 아니다 — 지움 목록과 함께 사슬 순서로 따로 마운트한다. 목록을 못 읽으면 어느 팩이 패치인지 모르므로 아무것도 마운트하지 않는다.
        vector<PackPatchManifest> listManifest;
        if ( PackManagerInternal::collectPatchManifests( listCandidateFile, listManifest ) == false )
            return false;

        bool           bAnyMounted = false;
        vector<string> listMountedPath;
        for ( const string& packPath : listCandidateFile )
        {
            if ( FileUtil::hasAnyExtension( packPath, { ".pack", ".swpk" } ) == false || PackManagerInternal::isPatchPackFile( packPath, listManifest ) )
                continue;

            const int32 priority = PackManagerInternal::computePackDefaultPriority( packPath, listPriority );
            if ( mountPack( packPath, priority ) )
            {
                bAnyMounted = true;
                listMountedPath.push_back( packPath );
            }
        }
        if ( bAnyMounted == false || listManifest.empty() )
            return bAnyMounted;

        // 패치를 적용하지 못하면 기본 팩만 마운트된 절반만 패치된 게임이 되므로, 이 호출이 마운트한 팩을 모두 해제하고 실패한다.
        if ( mountPatchChain( packsDirectory, listManifest, listMountedPath ) == false )
        {
            for ( const string& mountedPath : listMountedPath )
            {
                (void)unmountPack( mountedPath );
            }
            return false;
        }
        return true;
    }

    bool PackManager::setPatchTrust( const PackPatchTrust& trust )
    {
        if ( trust._pProvider == nullptr )
        {
            SW_LOG_ERROR( "Pack patch trust has no crypto provider - a signature requirement that cannot be checked is not accepted" );
            return false;
        }
        _patchTrust = trust;
        return true;
    }

    bool PackManager::verifyPatchSignatures( string_view packsDirectory, const vector<PackPatchManifest>& listManifest, const vector<size_t>& listOrder ) const
    {
        if ( _patchTrust._pProvider == nullptr )
            return true; // 신뢰 설정이 없는 채널은 서명을 보지 않는다
        for ( const size_t manifestIndex : listOrder )
        {
            const PackPatchManifest& manifest = listManifest[manifestIndex];
            const string             packPath = FileUtil::joinPath( packsDirectory, manifest._packFileName );
            vector<uint8>            packBytes;
            if ( FileUtil::readFile( packPath, packBytes ) == false )
            {
                SW_LOG_ERROR( "Pack patch %# -> %# pack %# could not be read to check its signature", manifest._fromBuild, manifest._toBuild, packPath );
                return false;
            }
            if ( PackPatchUtil::verifyManifest( *_patchTrust._pProvider, _patchTrust._publicKey, packBytes, manifest, packPath ) == false )
                return false;
        }
        return true;
    }

    bool PackManager::mountPatchChain( string_view packsDirectory, const vector<PackPatchManifest>& listManifest, vector<string>& listMountedPath )
    {
        const string recordPath = FileUtil::joinPath( packsDirectory, PackPatchUtil::kBuildRecordFileName );
        string       recordText;
        if ( FileUtil::readTextFile( recordPath, recordText ) == false )
        {
            SW_LOG_ERROR( "Pack patches exist in %# but the installed build is unknown - %# is missing", packsDirectory, PackPatchUtil::kBuildRecordFileName );
            return false;
        }
        PackBuildRecord record;
        if ( PackPatchUtil::parseBuildRecord( recordText, recordPath, record ) == false )
            return false;
        if ( record._patching == PackPatching::None )
        {
            SW_LOG_ERROR( "Pack patches exist in %# but %# says patching=\"none\" - a build that takes no patches must not ship patch lists", packsDirectory,
                          PackPatchUtil::kBuildRecordFileName );
            return false;
        }
        vector<size_t> listOrder;
        string         latestBuild;
        if ( PackPatchUtil::orderPatchChainToLatest( record._build, listManifest, listOrder, latestBuild ) == false )
            return false;
        // 서명은 패치 팩을 하나라도 올리기 전에 모두 확인한다 — 중간에 틀리면 절반만 얹힌 상태를 거치지 않는다.
        if ( verifyPatchSignatures( packsDirectory, listManifest, listOrder ) == false )
            return false;

        int32 priority = 0;
        {
            ScopedLock<mutex> lock{ _vfsMutex };
            for ( const MountedPack& mounted : _listMountedPack )
            {
                priority = mounted._priority > priority ? mounted._priority : priority;
            }
        }
        priority = ( priority / PackManagerInternal::kPriorityStep + 1 ) * PackManagerInternal::kPriorityStep; // 기본 팩 어느 것보다 높다
        for ( const size_t manifestIndex : listOrder )
        {
            const PackPatchManifest& manifest = listManifest[manifestIndex];
            const string             packPath = FileUtil::joinPath( packsDirectory, manifest._packFileName );
            if ( FileUtil::exists( packPath ) == false )
            {
                SW_LOG_ERROR( "Pack patch %# -> %# names %# but the file is missing from %#", manifest._fromBuild, manifest._toBuild, manifest._packFileName, packsDirectory );
                return false;
            }
            priority += PackManagerInternal::kPriorityStep; // 뒤 패치가 앞 패치를 덮는다
            if ( mountPatchPack( packPath, priority, manifest._listRemovedPath ) == false )
                return false;
            listMountedPath.push_back( packPath );
        }
        if ( listOrder.empty() == false )
            SW_LOG_INFO( "Applied %# pack patch(es): build %# -> %#", listOrder.size(), record._build, latestBuild );
        return true;
    }

} // namespace sw
