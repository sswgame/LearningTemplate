#include "pch.h"

#include "Engine/Resource/ResourceIO.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/AsyncFileIO.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Resource/AssetManager.h"
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
         * @brief 리소스 하나를 찾아 읽습니다. **순서는 텍스트 · 바이너리가 같습니다.**
         * @param readFromDisk 디스크 절대 경로에서 읽는 방법.
         * @param readFromPack 마운트된 팩에서 읽는 방법(찾았으면 팩 경로를 `outPackPath` 에 담습니다).
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
            if ( FileUtil::isAbsolutePath( relativePath ) )
            {
                if ( ResourceIO::getPackManager().isAllowLooseFiles() == false && isInsideResourceRoot( relativePath ) )
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

            ResourcePakManager& packManager = ResourceIO::getPackManager();
            const bool          bLooseFirst = packManager.isAllowLooseFiles();

            // 낱개 경로는 **여기서 한 번만** 푼다.
            string loosePath;
            if ( bLooseFirst )
            {
                loosePath = ResourcePaths::getResourcePath( relativePath );
                if ( loosePath.empty() == false && FileUtil::isRegularFile( loosePath ) )
                {
                    if ( pOutAbsPath != nullptr )
                        *pOutAbsPath = loosePath;
                    return readFromDisk( loosePath );
                }
            }

            // 2. 마운트된 팩에서 O(1) 해시로 찾아 압축을 풀어 읽는다
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
        return readResourceCommon(
            relativePath, pOutAbsPath,
            [&outText]( string_view absPath )
        { return FileUtil::readTextFile( absPath, outText ); },
            [&outText]( string_view key, string& outPackPath )
        { return getPackManager().readTextFile( key, outText, &outPackPath ); } );
    }

    bool ResourceIO::readBinaryResource( string_view relativePath, vector<uint8>& outBytes )
    {
        return readResourceCommon(
            relativePath, nullptr,
            [&outBytes]( string_view absPath )
        { return FileUtil::readFile( absPath, outBytes ); },
            [&outBytes]( string_view key, string& )
        { return getPackManager().readFile( key, outBytes ); } );
    }

    AsyncReadHandle ResourceIO::readBinaryResourceAsync( string_view relativePath, AsyncIOPriority priority, const ResourceReadCompleteDelegate& onComplete )
    {
        AsyncFileIO&    io = engine::getSubsystem<AsyncFileIO>();
        AsyncReadHandle handle;
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
            [&io, &handle, priority, &onComplete]( string_view key, string& )
        {
            handle = getPackManager().readFileAsync( io, key, priority, onComplete );
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

        const string        normalizedKey = FileUtil::normalizePath( relativePath );
        ResourcePakManager& packManager   = getPackManager();

        if ( packManager.hasFile( normalizedKey ) )
            return true;

        if ( packManager.isAllowLooseFiles() )
        {
            const string absPath = ResourcePaths::getResourcePath( relativePath );
            if ( absPath.empty() == false && FileUtil::exists( absPath ) )
                return true;

            return FileUtil::exists( relativePath );
        }

        return false;
    }

    ResourcePakManager& ResourceIO::getPackManager()
    {
        return engine::getSubsystem<AssetManager>().getPackManager();
    }
} // namespace sw
