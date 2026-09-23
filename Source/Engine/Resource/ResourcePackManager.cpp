#include "pch.h"

#include "Engine/Resource/ResourcePackManager.h"

#include "Core/Common/StdHeaders.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringUtil.h"

#include "Engine/Resource/ResourceUtil.h"

namespace sw
{
    namespace
    {
        SW_LOG_CALLER( "ResourcePackManager" );

        struct ResourcePackManagerInternal
        {
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

                const vector<string>& listPriorityEffective = listPriority.empty() ? ResourceUtil::getDefaultSearchPriority() : listPriority;
                const size_t          priorityCount         = listPriorityEffective.size();

                // 1. listPriorityEffective 순서대로 맞는 토큰을 찾는다(앞쪽 인덱스일수록 우선순위가 높다)
                for ( size_t index = 0; index < priorityCount; ++index )
                {
                    const string_view token{ listPriorityEffective[index] };
                    if ( matchesTokenCaseInsensitive( stem, token ) )
                        return static_cast<int32>( ( priorityCount - index ) * 1000 );
                }

                // 2. 패치 접두사(patch_)가 붙으면 대상 모듈 우선순위에 500 을 더한다
                if ( stem.size() >= 6 && StringUtil::startsWith( stem, "patch_", true ) )
                {
                    const string_view subStem = stem.substr( 6 );
                    for ( size_t index = 0; index < priorityCount; ++index )
                    {
                        const string_view token{ listPriorityEffective[index] };
                        if ( matchesTokenCaseInsensitive( subStem, token ) )
                            return static_cast<int32>( ( priorityCount - index ) * 1000 ) + 500;
                    }
                    // 대상 모듈이 없는 단독 핫픽스 팩(가장 높은 우선순위)
                    return static_cast<int32>( ( priorityCount + 1 ) * 1000 );
                }

                // 3. 맞는 우선순위 토큰이 없는 일반 팩의 기본 우선순위
                return 0;
            }

            /**
             * @brief "engine/textures/splash.dds" 나 "game/empty/maps/title.xml" 을 도메인과 나머지 경로로 나눕니다.
             */
            static bool trySplitDomainPrefix( string_view relativePath, string_view& outDomain, string_view& outSubPath )
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

            /**
             * @brief 경로를 가진 팩을 찾아 `visit( mounted, pathHash, pathInPack )` 을 부릅니다. 방문자가 true 를 반환하면 멈춥니다.
             * @details 두 단계입니다. 전체 경로의 해시로 모든 팩을 보고, 그다음 경로가 도메인으로 시작하면(`engine/…`) 그 도메인 팩에서
             *          나머지 경로로 봅니다. `hasFile` · `readFile` · `readTextFile` 이 이 스무 줄을 각자 들고 있었습니다. 조회 규칙이
             *          바뀌면 셋을 같이 고쳐야 했습니다. 잠금은 부르는 쪽이 쥡니다.
             * @return 방문자가 true 를 반환한 적이 있으면 true 입니다.
             */
            template <typename VisitFn>
            static bool visitPacksWithFile( const vector<MountedPack>& listMountedPack, string_view relativePath, VisitFn&& visit )
            {
                const uint64 pathHash = StringUtil::computeHash64( relativePath );
                for ( const MountedPack& mounted : listMountedPack )
                {
                    if ( mounted._pReader != nullptr && mounted._pReader->hasFile( pathHash ) && visit( mounted, pathHash, relativePath ) )
                        return true;
                }

                string_view queryDomain;
                string_view subPath;
                if ( trySplitDomainPrefix( relativePath, queryDomain, subPath ) == false )
                    return false;

                const uint64 subHash = StringUtil::computeHash64( subPath );
                for ( const MountedPack& mounted : listMountedPack )
                {
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
    ResourcePackManager::ResourcePackManager()
        : _vfsMutex{}
        , _listMountedPack{}
        , _dlcValidator{}
#if defined( SW_SHIPPING )
        , _bAllowLooseFiles{ false }
#else
        , _bAllowLooseFiles{ true }
#endif
    {
    }

    ResourcePackManager::~ResourcePackManager()
    {
        unmountAll();
    }

    ResourcePackManager::ResourcePackManager( ResourcePackManager&& other ) noexcept
    {
        std::scoped_lock<mutex> lock( other._vfsMutex );
        _listMountedPack  = std::move( other._listMountedPack );
        _dlcValidator     = std::move( other._dlcValidator );
        _bAllowLooseFiles = other._bAllowLooseFiles;
    }

    ResourcePackManager& ResourcePackManager::operator=( ResourcePackManager&& other ) noexcept
    {
        if ( this != &other )
        {
            std::scoped_lock<mutex, mutex> lock( _vfsMutex, other._vfsMutex );
            _listMountedPack  = std::move( other._listMountedPack );
            _dlcValidator     = std::move( other._dlcValidator );
            _bAllowLooseFiles = other._bAllowLooseFiles;
        }
        return *this;
    }

    bool ResourcePackManager::mountPack( string_view packFilePath, int32 priority )
    {
        if ( packFilePath.empty() )
            return false;

        const string normalizedPath = FileUtil::normalizeSeparators( packFilePath );

        int32 effectivePriority = priority;
        if ( effectivePriority <= 0 )
            effectivePriority = ResourcePackManagerInternal::computePackDefaultPriority( packFilePath, ResourceUtil::getSearchPriority() );

        auto pReader = make_unique<ResourcePackReader>();
        if ( pReader->open( normalizedPath ) == false )
        {
            SW_LOG_ERROR( "Failed to open and mount pack: %#", packFilePath );
            return false;
        }

        // DLC 소유권을 확인한다
        const uint32 dlcAppId = pReader->getDlcAppId();
        if ( dlcAppId > 0 )
        {
            if ( _dlcValidator.isBound() && _dlcValidator( dlcAppId ) == false )
            {
                SW_LOG_WARNING( "Access denied for DLC pack %# (DLC AppID %# not owned)", packFilePath, dlcAppId );
                return false;
            }
        }

        {
            std::scoped_lock<mutex> lock( _vfsMutex );

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
            mounted._domainName = string( stem );
            mounted._priority   = effectivePriority;
            mounted._pReader    = std::move( pReader );

            _listMountedPack.push_back( std::move( mounted ) );

            // 우선순위 내림차순으로 정렬한다(높은 priority 가 앞)
            std::stable_sort( _listMountedPack.begin(), _listMountedPack.end(), []( const MountedPack& left, const MountedPack& right )
            {
                return left._priority > right._priority;
            } );
        }

        SW_LOG_INFO( "Mounted resource pack: %# (Priority: %#)", normalizedPath, effectivePriority );
        return true;
    }

    bool ResourcePackManager::unmountPack( string_view packFilePath )
    {
        if ( packFilePath.empty() )
            return false;

        std::scoped_lock<mutex> lock( _vfsMutex );
        for ( auto it = _listMountedPack.begin(); it != _listMountedPack.end(); ++it )
        {
            if ( it->_pReader != nullptr && FileUtil::pathsEqualNormalized( it->_pReader->getPackPath(), packFilePath ) )
            {
                it->_pReader->close();
                _listMountedPack.erase( it );
                SW_LOG_INFO( "Unmounted resource pack: %#", packFilePath );
                return true;
            }
        }
        return false;
    }

    void ResourcePackManager::unmountAll()
    {
        std::scoped_lock<mutex> lock( _vfsMutex );
        for ( auto& mounted : _listMountedPack )
        {
            if ( mounted._pReader != nullptr )
                mounted._pReader->close();
        }
        _listMountedPack.clear();
    }

    bool ResourcePackManager::hasFile( string_view relativePath ) const
    {
        if ( relativePath.empty() )
            return false;

        std::scoped_lock<mutex> lock( _vfsMutex );
        return ResourcePackManagerInternal::visitPacksWithFile( _listMountedPack, relativePath,
                                                                []( const MountedPack&, uint64, string_view )
        { return true; } );
    }

    bool ResourcePackManager::readFile( string_view relativePath, vector<uint8>& outBytes ) const
    {
        if ( relativePath.empty() )
            return false;

        std::scoped_lock<mutex> lock( _vfsMutex );
        return ResourcePackManagerInternal::visitPacksWithFile( _listMountedPack, relativePath,
                                                                [&outBytes]( const MountedPack& mounted, uint64 pathHash, string_view )
        { return mounted._pReader->readFile( pathHash, outBytes ); } );
    }

    bool ResourcePackManager::readTextFile( string_view relativePath, string& outText, string* pOutMountedPackPath ) const
    {
        if ( relativePath.empty() )
            return false;

        std::scoped_lock<mutex> lock( _vfsMutex );
        return ResourcePackManagerInternal::visitPacksWithFile( _listMountedPack, relativePath,
                                                                [&outText, pOutMountedPackPath]( const MountedPack& mounted, uint64, string_view pathInPack )
        {
            if ( mounted._pReader->readTextFile( pathInPack, outText ) == false )
                return false;
            if ( pOutMountedPackPath != nullptr )
                *pOutMountedPackPath = mounted._pReader->getPackPath();
            return true;
        } );
    }

    void ResourcePackManager::setDlcEntitlementValidator( DlcEntitlementDelegate validator )
    {
        std::scoped_lock<mutex> lock( _vfsMutex );
        _dlcValidator = std::move( validator );
    }

    void ResourcePackManager::setAllowLooseFiles( bool bAllow )
    {
        _bAllowLooseFiles = bAllow;
    }

    bool ResourcePackManager::isAllowLooseFiles() const
    {
        return _bAllowLooseFiles;
    }

    size_t ResourcePackManager::getMountedPackCount() const
    {
        std::scoped_lock<mutex> lock( _vfsMutex );
        return _listMountedPack.size();
    }

    bool ResourcePackManager::scanAndMountPacks( string_view packsDirectory, const vector<string>& listPriority )
    {
        if ( packsDirectory.empty() || FileUtil::directoryExists( packsDirectory ) == false )
            return false;

        vector<string> listCandidateFile;
        FileUtil::collectFiles( packsDirectory, "", listCandidateFile, false );

        if ( listCandidateFile.empty() )
            return false;

        bool bAnyMounted = false;
        for ( const string& packPath : listCandidateFile )
        {
            if ( FileUtil::hasAnyExtension( packPath, { ".pack", ".swpk" } ) == false )
                continue;

            const int32 priority = ResourcePackManagerInternal::computePackDefaultPriority( packPath, listPriority );
            if ( mountPack( packPath, priority ) )
                bAnyMounted = true;
        }

        return bAnyMounted;
    }

} // namespace sw
