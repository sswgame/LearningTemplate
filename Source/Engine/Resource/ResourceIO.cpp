#include "pch.h"

#include "Engine/Resource/ResourceIO.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/pair.h"
#include "Core/File/AsyncFileIO.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Resource/AssetManager.h"
#include "Engine/Resource/Cook/CookJob.h"
#include "Engine/Resource/Pack/ResourcePakManager.h"
#include "Engine/Resource/ResourcePaths.h"

namespace sw
{
    namespace
    {
        /** @brief 절대 경로가 리소스 루트 안이면 true 입니다(팩 전용 모드가 막아야 하는 낱개 에셋). */
        bool isInsideResourceRoot( string_view absolutePath )
        {
            const string& root = ResourcePaths::getRootFolderPath();
            if ( root.empty() )
                return false;
            const string normalized = FileUtil::normalizeSeparators( absolutePath );
            return normalized.size() > root.size() && StringUtil::startsWith( normalized, root, true ) &&
                   ( normalized[root.size()] == '/' || root.back() == '/' );
        }

        /**
         * @brief 마운트된 팩을 든 매니저입니다. 엔진 서비스가 바인딩되지 않았으면(엔진 없이 도는 도구 · 쿠커 · 테스트) nullptr 이고, 그때는 마운트된 팩이 없다.
         * @details 낱개 파일 정책은 이 함수와 `isLooseFileAllowed` 한 곳에서만 본다 — 부르는 쪽이 `ResourceIO` 가 실패한 뒤 디스크를 다시 읽지 않게.
         */
        const ResourcePakManager* findPackManager()
        {
            return engine::areEngineServicesBound() ? &ResourceIO::getPackManager() : nullptr;
        }

        /** @brief 낱개 파일을 읽어도 되는지입니다. 팩 매니저가 정하고, 엔진 밖이면 팩 매니저를 만들 때의 빌드 기본값과 같다(배포 구성은 금지). */
        bool isLooseFileAllowed( const ResourcePakManager* pPackManager )
        {
            return pPackManager != nullptr ? pPackManager->isAllowLooseFiles() : ResourcePakManager::kAllowLooseFilesByDefault;
        }

        /**
         * @brief 리소스 하나를 찾아 읽습니다. **순서는 텍스트 · 바이너리가 같습니다.**
         * @param readFromDisk 디스크 절대 경로에서 읽는 방법.
         * @param readFromPack 마운트된 팩에서 읽는 방법(팩 매니저를 받고, 찾았으면 팩 경로를 `outPackPath` 에 담습니다).
         * @details 순서는 넷입니다. (0) OS 절대 경로면 디스크에서 바로, (1) 낱개 파일 우선이 켜져
         *          있으면 디스크, (2) 마운트된 팩, (3) 낱개 경로를 풀지 못한 경우의 마지막 폴백입니다.
         *
         *          `readTextResource` 와 `readBinaryResource` 가 이 순서 하나를 씁니다 — 두 벌이면 한쪽만 고쳐 텍스트와
         *          바이너리가 다른 파일을 읽게 됩니다.
         *
         *          경로를 한 번만 풀고, 3단계는 **1단계가 경로를 풀지 못한 경우에만** 의미가 있습니다(같은 경로를 두 번 묻지 않는다).
         */
        template <typename DiskReadFn, typename PackReadFn>
        [[nodiscard]] bool readResourceCommon( string_view relativePath, string* pOutAbsPath,
                                               const DiskReadFn& readFromDisk, const PackReadFn& readFromPack )
        {
            if ( relativePath.empty() )
                return false;
            // 이 호스트가 읽지 않는 에셋 종류(전용 서버의 텍스처 · 셰이더 바이너리 · 오디오)는 없는 것으로 친다 — 서버 팩에는 들어 있지도 않다.
            if ( ResourcePaths::isExcludedForHost( relativePath ) )
                return false;

            // 0. OS 절대 경로(임시 파일, 외부 세이브 등)면 디스크에서 바로 읽는다. 단, 팩 전용 모드(낱개 파일 금지 — 배포 구성)에서 리소스 루트
            //    **안**을 가리키면 거절한다 — 절대 경로로 적기만 하면 팩 전용 모드를 지나 낱개 에셋을 읽게 된다.
            const ResourcePakManager* pPackManager = findPackManager();
            const bool                bLooseFirst  = isLooseFileAllowed( pPackManager );
            if ( FileUtil::isAbsolutePath( relativePath ) )
            {
                if ( bLooseFirst == false && isInsideResourceRoot( relativePath ) )
                {
                    SW_LOG_WARNING( "Refusing a loose read of '%#' — this build reads resources from packs only", relativePath );
                    return false;
                }
                if ( FileUtil::isRegularFile( relativePath ) == false )
                    return false;
                if ( pOutAbsPath != nullptr )
                    *pOutAbsPath = string( relativePath );
                return readFromDisk( relativePath );
            }

            if ( ResourcePaths::hasParentDirectoryComponent( relativePath ) )
            {
                SW_LOG_WARNING( "Refusing resource id '%#' — '..' would leave the resource root", relativePath );
                return false;
            }

            // 낱개 경로는 **여기서 한 번만** 푼다.
            string loosePath;
            if ( bLooseFirst )
            {
                loosePath = ResourcePaths::findLooseFile( relativePath ).getDiskPath();
                if ( loosePath.empty() == false && FileUtil::isRegularFile( loosePath ) )
                {
                    if ( pOutAbsPath != nullptr )
                        *pOutAbsPath = loosePath;
                    return readFromDisk( loosePath );
                }
            }

            // 2. 마운트된 팩에서 O(1) 해시로 찾아 압축을 풀어 읽는다(엔진 밖이면 마운트된 팩이 없다)
            const string normalizedKey = FileUtil::normalizePath( relativePath );
            string       mountedPackPath;
            if ( pPackManager != nullptr && readFromPack( *pPackManager, normalizedKey, mountedPackPath ) )
            {
                if ( pOutAbsPath != nullptr )
                    *pOutAbsPath = "[" + FileUtil::getFileNamePart( mountedPackPath ) + "]:" + normalizedKey;
                return true;
            }

            // 3. 낱개 경로를 **못 푼 경우에만** 상대 경로 그대로 마지막으로 본다.
            //    풀렸는데 없었다면 위에서 이미 확인했으므로 다시 묻지 않는다.
            const bool bLooseFallback = bLooseFirst && loosePath.empty() && FileUtil::isRegularFile( relativePath );
            if ( bLooseFallback )
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
    SW_LOG_CALLER( "ResourceIO" );

    bool ResourceIO::readTextResource( string_view relativePath, string& outText,
                                       string* pOutAbsPath )
    {
        const bool bRead = readResourceCommon(
            relativePath, pOutAbsPath,
            [&outText]( string_view absPath )
        { return FileUtil::readTextFile( absPath, outText ); },
            [&outText]( const ResourcePakManager& packManager, string_view key, string& outPackPath )
        { return packManager.readTextFile( key, outText, &outPackPath ); } );
        // 쿠킹 작업 안의 읽기는 그 작업의 입력이다. 쿠킹 세션 안인데 작업 밖이면 기록이 빠지므로 읽기가 실패한다(`CookJob`).
        // 찾지 못한 것도 입력이다(해시 0) — 그 파일이 생기면 다시 굽는다.
        if ( bRead == false )
        {
            CookJob::admitMissingRead( relativePath );
            return false;
        }
        return CookJob::admitRead( relativePath, reinterpret_cast<const uint8*>( outText.data() ), outText.size() );
    }

    bool ResourceIO::readTextFile( string_view filePath, string& outText )
    {
        if ( FileUtil::readTextFile( filePath, outText ) == false )
        {
            CookJob::admitMissingRead( filePath );
            return false;
        }
        return CookJob::admitRead( filePath, reinterpret_cast<const uint8*>( outText.data() ), outText.size() );
    }

    bool ResourceIO::readBinaryResource( string_view relativePath, vector<uint8>& outBytes )
    {
        const bool bRead = readResourceCommon(
            relativePath, nullptr,
            [&outBytes]( string_view absPath )
        { return FileUtil::readFile( absPath, outBytes ); },
            [&outBytes]( const ResourcePakManager& packManager, string_view key, string& )
        { return packManager.readFile( key, outBytes ); } );
        if ( bRead == false )
        {
            CookJob::admitMissingRead( relativePath );
            return false;
        }
        return CookJob::admitRead( relativePath, outBytes.data(), outBytes.size() );
    }

    AsyncReadHandle ResourceIO::readBinaryResourceAsync( string_view relativePath, AsyncIOPriority priority, const ResourceReadCompleteDelegate& onComplete )
    {
        AsyncFileIO&    io = engine::getSubsystem<AsyncFileIO>();
        AsyncReadHandle handle;
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
            [&io, &handle, priority, &onComplete]( const ResourcePakManager& packManager, string_view key, string& )
        {
            handle = packManager.readFileAsync( io, key, priority, onComplete );
            return handle.isValid();
        } );
        return handle;
    }

    bool ResourceIO::hasResource( string_view relativePath )
    {
        if ( relativePath.empty() || ResourcePaths::isExcludedForHost( relativePath ) )
            return false;

        // 0. OS 절대 경로면 디스크에서 바로 확인한다
        // 판정은 `FileUtil::isAbsolutePath` 가 기준이다 — 손으로 적은 복사본을 두면 기준과 답이 갈린다.
        if ( FileUtil::isAbsolutePath( relativePath ) )
            return FileUtil::exists( relativePath );

        const string              normalizedKey = FileUtil::normalizePath( relativePath );
        const ResourcePakManager* pPackManager  = findPackManager();
        if ( pPackManager != nullptr && pPackManager->hasFile( normalizedKey ) )
            return true;

        if ( isLooseFileAllowed( pPackManager ) )
        {
            const string absPath = ResourcePaths::findLooseFile( relativePath ).getDiskPath();
            if ( absPath.empty() == false && FileUtil::exists( absPath ) )
                return true;

            return FileUtil::exists( relativePath );
        }

        return false;
    }

    ResourceFolderListing ResourceIO::collectResources( string_view relativeFolder, string_view extension, vector<string>& outListPath )
    {
        outListPath.clear();
        const bool bNotResourceID = relativeFolder.empty() || FileUtil::isAbsolutePath( relativeFolder ) || ResourcePaths::hasParentDirectoryComponent( relativeFolder );
        if ( bNotResourceID )
        {
            SW_LOG_ERROR( "Cannot list '%#' - a resource folder is a relative resource id without '..'", relativeFolder );
            return ResourceFolderListing::Failed;
        }
        const string folder = FileUtil::trimTrailingSlashes( FileUtil::normalizeSeparators( relativeFolder ) );

        // 찾는 순서는 읽기와 같다 — 낱개 파일을 허용하면 디스크가 먼저(같은 이름이면 그 표기가 남는다), 그다음 우선순위 순서의 팩.
        vector<string>            listCandidate;
        bool                      bFolderFound = false;
        const ResourcePakManager* pPackManager = findPackManager();
        if ( isLooseFileAllowed( pPackManager ) )
        {
            const string diskFolder = ResourcePaths::findLooseFile( folder ).getDiskPath();
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
            const bool bSkip = ( extension.empty() == false && StringUtil::endsWith( path, extension, true ) == false ) || ResourcePaths::isExcludedForHost( path );
            if ( bSkip == false )
                listKeyed.emplace_back( StringUtil::toLower( path.c_str() ), std::move( path ) );
        }
        std::stable_sort( listKeyed.begin(), listKeyed.end(), []( const pair<string, string>& lhs, const pair<string, string>& rhs )
        { return lhs.first < rhs.first; } );
        for ( size_t index = 0; index < listKeyed.size(); ++index )
        {
            if ( index == 0 || listKeyed[index].first != listKeyed[index - 1].first )
                outListPath.push_back( std::move( listKeyed[index].second ) );
        }
        return ResourceFolderListing::Listed;
    }

    ResourcePakManager& ResourceIO::getPackManager()
    {
        return engine::getSubsystem<AssetManager>().getPackManager();
    }
} // namespace sw
