#include "pch.h"

#include "Engine/Content/Pack/PackChunkLoader.h"

#include "Core/Concurrency/ScopedLock.h"
#include "Core/File/ContentFileSource.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Log/ScopedDeferredLog.h"

#include "Engine/Content/Pack/PackChunkManifest.h"
#include "Engine/Content/Pack/PackManager.h"

#if defined( SW_PLATFORM_WEB )
    #include "Engine/Content/Pack/Web/WebPackChunkSource.h"
#endif

namespace sw
{
    unique_ptr<IPackChunkSource> IPackChunkSource::createPlatformSource()
    {
#if defined( SW_PLATFORM_WEB )
        return makeWebPackChunkSource();
#elif defined( SW_PLATFORM_WINDOWS ) || defined( SW_PLATFORM_LINUX ) || defined( SW_PLATFORM_ANDROID )
        return nullptr; // 데스크톱 · Android 는 패키지가 모든 청크를 팩 폴더에 싣는다
#else
    #error "Unsupported platform"
#endif
    }

    PackChunkLoader::PackChunkLoader( PackManager& packs, unique_ptr<IPackChunkSource> pSource )
        : _packs{ packs }
        , _pSource{ std::move( pSource ) }
        , _pShared{ make_shared<SharedState>() }
    {
    }

    PackChunkState PackChunkLoader::prepareScene( string_view sceneID, vector<string>& outListToMount )
    {
        vector<string> listPack;
        if ( _packs.getChunkManifest().appendScenePackFiles( sceneID, listPack ) == false )
            return PackChunkState::Failed;
        if ( listPack.empty() )
            return PackChunkState::Ready;

        const string&  packsDirectory = _packs.getPacksDirectory();
        vector<string> listToFetch;
        bool           bAllMounted = true;
        bool           bFailed     = false;
        {
            const ScopedDeferredLog deferredLog;
            ScopedLock<mutex>       lock{ _pShared->_mutex };
            for ( const string& packName : listPack )
            {
                FileState& state = _pShared->_mapState[packName];
                if ( state == FileState::Unknown )
                {
                    if ( ContentFileSource::getCurrent().isFile( _packs.makePackKey( packName ) ) )
                        state = FileState::Local;
                    else if ( _pSource != nullptr )
                    {
                        state = FileState::Fetching;
                        listToFetch.push_back( packName );
                    }
                    else
                    {
                        SW_LOG_ERROR_ID( "PackChunkLoader.PackMissing", "Scene '%#' needs pack '%#' but it is not in %# - the package left out a chunk", sceneID, packName, packsDirectory );
                        state = FileState::Failed;
                    }
                }
                if ( state == FileState::Local )
                {
                    state = FileState::MountQueued;
                    outListToMount.push_back( packName );
                }
                bFailed     = bFailed || state == FileState::Failed;
                bAllMounted = bAllMounted && state == FileState::Mounted;
            }
        }
        // 받기는 잠금 밖에서 건다 — 받기가 콜백을 바로 부를 수 있다.
        for ( const string& packName : listToFetch )
        {
            const shared_ptr<SharedState> pShared = _pShared;
            _pSource->fetch( packName, FileUtil::toDiskPath( _packs.makePackKey( packName ) ), SW_DELEGATE_LAMBDA( PackChunkFetchDoneDelegate, [pShared, packName]( bool bFetched )
            {
                ScopedLock<mutex> lock{ pShared->_mutex };
                pShared->_mapState[packName] = bFetched ? FileState::Local : FileState::Failed;
            } ) );
        }
        if ( bFailed )
            return PackChunkState::Failed;
        return bAllMounted ? PackChunkState::Ready : PackChunkState::Pending;
    }

    void PackChunkLoader::mountPacks( const vector<string>& listPackFileName )
    {
        for ( const string& packName : listPackFileName )
        {
            const string packPath = _packs.makePackKey( packName );
            const bool   bMounted = _packs.mountPack( packPath );
            if ( bMounted == false )
                SW_LOG_ERROR_ID( "PackChunkLoader.MountFailed", "Chunk pack '%#' could not be mounted - the scenes that need it cannot open", packPath );
            ScopedLock<mutex> lock{ _pShared->_mutex };
            _pShared->_mapState[packName] = bMounted ? FileState::Mounted : FileState::Failed;
        }
    }
} // namespace sw
