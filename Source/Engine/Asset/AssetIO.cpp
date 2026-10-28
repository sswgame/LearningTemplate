#include "pch.h"

#include "Engine/Asset/AssetIO.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/pair.h"
#include "Core/File/AsyncFileIO.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Asset/AssetPaths.h"
#include "Engine/Asset/Cook/CookJob.h"
#include "Engine/Asset/Pack/PackManager.h"
#include "Engine/Common/EngineServices.h"

namespace sw
{
    SW_LOG_CALLER( "AssetIO" );

    namespace
    {
        /**
         * @brief 마운트된 팩을 든 매니저입니다. 엔진 서비스가 바인딩되지 않았으면(엔진 없이 도는 도구 · 쿠커 · 테스트) nullptr 이고, 그때는 마운트된 팩이 없다.
         * @details 낱개 파일 정책은 이 함수와 `isLooseFileAllowed` 한 곳에서만 본다 — 부르는 쪽이 `AssetIO` 가 실패한 뒤 디스크를 다시 읽지 않게.
         */
        const PackManager* findPackManager()
        {
            return engine::areEngineServicesBound() ? &engine::getSubsystem<AssetManager>().getPackManager() : nullptr;
        }

        /** @brief 낱개 파일을 읽어도 되는지입니다. 팩 매니저가 정하고, 엔진 밖이면 팩 매니저를 만들 때의 빌드 기본값과 같다(배포 구성은 금지). */
        bool isLooseFileAllowed( const PackManager* pPackManager )
        {
            return pPackManager != nullptr ? pPackManager->isAllowLooseFiles() : PackManager::kAllowLooseFilesByDefault;
        }

        /**
         * @brief 리소스 id 가 아닌 OS 경로를 리소스 입구에 넣었으면 오류를 남기고 true 입니다.
         * @details 리소스 입구는 **리소스 id 만** 받는다. 디스크 파일은 디스크 입구(`AssetIO::readTextFile` · `readBinaryFile`, 로더의 `*FromDiskFile`)로
         *          읽고, 루트 밖 폴더를 리소스로 읽어야 하면 `AssetPaths::mountDiskDomain` 으로 붙인다. 예전에는 OS 절대 경로를 디스크에서 바로 읽고
         *          배포 구성만 루트 안 경로를 거절해, 잘못 넣은 호출이 Dev 테스트를 지나 Shipping 에서야 드러났다(2026-10-19 CI).
         */
        bool rejectsOSPath( string_view relativePath )
        {
            if ( FileUtil::isAbsolutePath( relativePath ) == false )
                return false;
            SW_LOG_ERROR( "'%#' is an OS path, not a resource id - read disk files through AssetIO::readTextFile / readBinaryFile or a loader's "
                          "*FromDiskFile entry, or mount the folder with AssetPaths::mountDiskDomain",
                          relativePath );
            return true;
        }

        /**
         * @brief 리소스 하나를 찾아 읽습니다. **순서는 텍스트 · 바이너리가 같습니다.**
         * @param readFromDisk 디스크 절대 경로에서 읽는 방법.
         * @param readFromPack 마운트된 팩에서 읽는 방법(팩 매니저를 받고, 찾았으면 팩 경로를 `outPackPath` 에 담습니다).
         * @details 인자는 리소스 id 하나다 — OS 경로는 오류로 거절한다(`rejectsOSPath`). 순서는 둘입니다. (1) 낱개 파일 우선이 켜져 있거나 붙인 디스크
         *          도메인(`AssetPaths::mountDiskDomain`)이면 디스크, (2) 마운트된 팩. 붙인 도메인은 팩에 없으므로 (2) 를 보지 않는다.
         *          풀지 못한 id 를 작업 폴더 기준 경로로 다시 열어 보는 마지막 단계는 없앴다 — 리소스 id 가 아닌 것을 디스크에서 찾는 폴백이었다.
         *
         *          `readTextAsset` 와 `readBinaryAsset` 가 이 순서 하나를 씁니다 — 두 벌이면 한쪽만 고쳐 텍스트와
         *          바이너리가 다른 파일을 읽게 됩니다.
         */
        template <typename DiskReadFn, typename PackReadFn>
        [[nodiscard]] bool readResourceCommon( string_view relativePath, string* pOutAbsPath,
                                               const DiskReadFn& readFromDisk, const PackReadFn& readFromPack )
        {
            if ( relativePath.empty() || rejectsOSPath( relativePath ) )
                return false;
            // 이 호스트가 읽지 않는 에셋 종류(전용 서버의 텍스처 · 셰이더 바이너리 · 오디오)는 없는 것으로 친다 — 서버 팩에는 들어 있지도 않다.
            if ( AssetPaths::isExcludedForHost( relativePath ) )
                return false;
            if ( AssetPaths::hasParentDirectoryComponent( relativePath ) )
            {
                SW_LOG_WARNING( "Refusing resource id '%#' — '..' would leave the resource root", relativePath );
                return false;
            }

            // 1. 낱개 경로는 **여기서 한 번만** 푼다.
            const PackManager* pPackManager = findPackManager();
            const bool         bDiskDomain  = AssetPaths::isMountedDiskDomainID( relativePath );
            if ( isLooseFileAllowed( pPackManager ) || bDiskDomain )
            {
                const string loosePath = AssetPaths::findLooseFile( relativePath ).getDiskPath();
                if ( loosePath.empty() == false && FileUtil::isRegularFile( loosePath ) )
                {
                    if ( pOutAbsPath != nullptr )
                        *pOutAbsPath = loosePath;
                    return readFromDisk( loosePath );
                }
            }
            if ( bDiskDomain )
                return false;

            // 2. 마운트된 팩에서 O(1) 해시로 찾아 압축을 풀어 읽는다(엔진 밖이면 마운트된 팩이 없다)
            const string normalizedKey = FileUtil::normalizePath( relativePath );
            string       mountedPackPath;
            if ( pPackManager != nullptr && readFromPack( *pPackManager, normalizedKey, mountedPackPath ) )
            {
                if ( pOutAbsPath != nullptr )
                    *pOutAbsPath = "[" + FileUtil::getFileNamePart( mountedPackPath ) + "]:" + normalizedKey;
                return true;
            }
            return false;
        }
    } // namespace
} // namespace sw

namespace sw
{
    bool AssetIO::readTextAsset( const AssetID& id, string& outText, string* pOutAbsPath )
    {
        const string_view relativePath = id.view();
        const bool        bRead        = readResourceCommon(
            relativePath, pOutAbsPath,
            [&outText]( string_view absPath )
        { return FileUtil::readTextFile( absPath, outText ); },
            [&outText]( const PackManager& packManager, string_view key, string& outPackPath )
        { return packManager.readTextFile( key, outText, &outPackPath ); } );
        // 쿠킹 작업 안의 읽기는 그 작업의 입력이다. 쿠킹 세션 안인데 작업 밖이면 기록이 빠지므로 읽기가 실패한다(`CookJob`).
        // 찾지 못한 것도 입력이다(해시 0) — 그 파일이 생기면 다시 베이크한다.
        if ( bRead == false )
        {
            CookJob::admitMissingRead( relativePath );
            return false;
        }
        return CookJob::admitRead( relativePath, reinterpret_cast<const uint8*>( outText.data() ), outText.size() );
    }

    bool AssetIO::readTextFile( string_view filePath, string& outText )
    {
        if ( FileUtil::readTextFile( filePath, outText ) == false )
        {
            CookJob::admitMissingRead( filePath );
            return false;
        }
        return CookJob::admitRead( filePath, reinterpret_cast<const uint8*>( outText.data() ), outText.size() );
    }

    bool AssetIO::readBinaryFile( string_view filePath, vector<uint8>& outBytes )
    {
        if ( FileUtil::readFile( filePath, outBytes ) == false )
        {
            CookJob::admitMissingRead( filePath );
            return false;
        }
        return CookJob::admitRead( filePath, outBytes.data(), outBytes.size() );
    }

    bool AssetIO::readBinaryAsset( const AssetID& id, vector<uint8>& outBytes )
    {
        const string_view relativePath = id.view();
        const bool        bRead        = readResourceCommon(
            relativePath, nullptr,
            [&outBytes]( string_view absPath )
        { return FileUtil::readFile( absPath, outBytes ); },
            [&outBytes]( const PackManager& packManager, string_view key, string& )
        { return packManager.readFile( key, outBytes ); } );
        if ( bRead == false )
        {
            CookJob::admitMissingRead( relativePath );
            return false;
        }
        return CookJob::admitRead( relativePath, outBytes.data(), outBytes.size() );
    }

    AsyncReadHandle AssetIO::readBinaryResourceAsync( const AssetID& id, AsyncIOPriority priority, const ResourceReadCompleteDelegate& onComplete )
    {
        const string_view relativePath = id.view();
        AsyncFileIO&      io           = engine::getSubsystem<AsyncFileIO>();
        AsyncReadHandle   handle;
        // 완료가 다른 스레드에서 오므로 쿠킹 작업의 입력으로 기록할 수 없다 — 쿠킹 중이면 걸지 않는다.
        if ( CookJob::admitAsyncRead( relativePath ) == false )
            return handle;
        // 찾는 순서는 동기 읽기와 같은 함수 하나다 — 두 벌이면 한쪽만 고쳐 동기와 비동기가 다른 파일을 읽는다.
        (void)readResourceCommon(
            relativePath, nullptr,
            [&io, &handle, priority, &onComplete]( string_view absPath )
        {
            handle = io.readFile( absPath, priority, SW_DELEGATE_LAMBDA( AsyncReadCompleteDelegate, [onComplete]( AsyncReadResult& result )
            {
                if ( onComplete.isBound() )
                    onComplete( result.isSucceeded(), result._bytes );
            } ) );
            return handle.isValid();
        },
            [&io, &handle, priority, &onComplete]( const PackManager& packManager, string_view key, string& )
        {
            handle = packManager.readFileAsync( io, key, priority, onComplete );
            return handle.isValid();
        } );
        return handle;
    }

    bool AssetIO::hasResource( const AssetID& id )
    {
        const string_view relativePath = id.view();
        // 찾는 규칙은 읽기(`readResourceCommon`)와 같다 — OS 경로는 오류, 붙인 디스크 도메인은 디스크만, 나머지는 낱개 파일(허용될 때) · 팩.
        if ( relativePath.empty() || rejectsOSPath( relativePath ) )
            return false;
        const bool bUnreadableHere = AssetPaths::isExcludedForHost( relativePath ) || AssetPaths::hasParentDirectoryComponent( relativePath );
        if ( bUnreadableHere )
            return false;

        const PackManager* pPackManager = findPackManager();
        const bool         bDiskDomain  = AssetPaths::isMountedDiskDomainID( relativePath );
        if ( isLooseFileAllowed( pPackManager ) || bDiskDomain )
        {
            const string absPath = AssetPaths::findLooseFile( relativePath ).getDiskPath();
            if ( absPath.empty() == false && FileUtil::exists( absPath ) )
                return true;
        }
        if ( bDiskDomain )
            return false;

        const string normalizedKey = FileUtil::normalizePath( relativePath );
        return pPackManager != nullptr && pPackManager->hasFile( normalizedKey );
    }

    ResourceFolderListing AssetIO::collectResources( const AssetID& folderID, string_view extension, vector<AssetID>& outListID )
    {
        const string_view relativeFolder = folderID.view();
        outListID.clear();
        const bool bNotAssetID = relativeFolder.empty() || FileUtil::isAbsolutePath( relativeFolder ) || AssetPaths::hasParentDirectoryComponent( relativeFolder );
        if ( bNotAssetID )
        {
            SW_LOG_ERROR( "Cannot list '%#' - a resource folder is a relative resource id without '..'", relativeFolder );
            return ResourceFolderListing::Failed;
        }
        const string folder = FileUtil::trimTrailingSlashes( FileUtil::normalizeSeparators( relativeFolder ) );

        // 찾는 순서는 읽기와 같다 — 낱개 파일을 허용하면 디스크가 먼저(같은 이름이면 그 표기가 남는다), 그다음 우선순위 순서의 팩.
        vector<string>     listCandidate;
        bool               bFolderFound = false;
        const PackManager* pPackManager = findPackManager();
        if ( isLooseFileAllowed( pPackManager ) || AssetPaths::isMountedDiskDomainID( folder ) )
        {
            const string diskFolder = AssetPaths::findLooseFile( folder ).getDiskPath();
            if ( diskFolder.empty() == false && FileUtil::isDirectory( diskFolder ) )
            {
                vector<string> listDiskFile;
                if ( FileUtil::collectFiles( diskFolder, {}, listDiskFile, false ) == false )
                {
                    SW_LOG_ERROR( "Cannot list the resource folder '%#' (%#)", folder, diskFolder );
                    return ResourceFolderListing::Failed;
                }
                bFolderFound = true;
                for ( const string& filePath : listDiskFile )
                {
                    listCandidate.push_back( FileUtil::joinPath( folder, FileUtil::getFileNamePart( filePath ) ) );
                }
            }
        }
        const size_t diskCount = listCandidate.size();
        if ( pPackManager != nullptr )
            pPackManager->collectFolderEntries( folder, listCandidate );
        bFolderFound = bFolderFound || listCandidate.size() > diskCount;
        if ( bFolderFound == false )
            return ResourceFolderListing::FolderMissing;

        // (소문자 키, 표기) — 키로 안정 정렬해 같은 키의 첫 표기(디스크 · 높은 우선순위 팩)를 남긴다.
        vector<pair<string, string>> listKeyed;
        listKeyed.reserve( listCandidate.size() );
        for ( string& path : listCandidate )
        {
            const bool bSkip = ( extension.empty() == false && StringUtil::endsWith( path, extension, true ) == false ) || AssetPaths::isExcludedForHost( path );
            if ( bSkip == false )
                listKeyed.emplace_back( StringUtil::toLower( path.c_str() ), std::move( path ) );
        }
        std::stable_sort( listKeyed.begin(), listKeyed.end(), []( const pair<string, string>& lhs, const pair<string, string>& rhs )
        { return lhs.first < rhs.first; } );
        for ( size_t index = 0; index < listKeyed.size(); ++index )
        {
            if ( index == 0 || listKeyed[index].first != listKeyed[index - 1].first )
                outListID.emplace_back( listKeyed[index].second );
        }
        return ResourceFolderListing::Listed;
    }
} // namespace sw
