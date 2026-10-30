#include "pch.h"

#include "DevTools/AssetReload/ContentFolderWatchService.h"

#include "Core/Log/Logger.h"

#include "DevTools/AssetReload/FileWatchDispatcher.h"
#include "DevTools/FileWatch/IFileWatcher.h"

#include "Engine/FileSystem/AssetPaths.h"
#include "Engine/Foundation/EngineServices.h"

namespace sw
{
    SW_LOG_CALLER( "ContentFolderWatch" );

    /** @struct ContentFolderWatchService::Watch @brief 감시 하나 — 디스크 폴더마다의 분배기 · 경로 구독, 알릴 델리게이트, 이번 프레임에 바뀌었는지입니다. */
    struct ContentFolderWatchService::Watch
    {
        uint64                                  _id{ 0 };
        vector<unique_ptr<FileWatchDispatcher>> _listDispatcher{};
        vector<Subscription>                    _listMatch{};
        ChangedDelegate                         _onChanged{};
        bool                                    _bChanged{ false };

        void onFileChanged( const FileChangeEvent& /*changeEvent*/ ) { _bChanged = true; } // 확장자는 분배기가 거른다 — 바뀌었다는 사실만 쓴다
    };

    ContentFolderWatchService::ContentFolderWatchService()
        : _listWatch{}
        , _watchIDAllocator{}
    {
    }

    ContentFolderWatchService::~ContentFolderWatchService() = default;

    Subscription ContentFolderWatchService::watchAssetFolder( string_view folderID, const vector<string>& listExtension, const ChangedDelegate& onChanged )
    {
        vector<string> listDiskFolder;
        engine::getSubsystem<AssetPaths>().collectLooseFolders( folderID, listDiskFolder );
        if ( listDiskFolder.empty() )
        {
            SW_LOG_INFO( "'%#' is not on disk as loose files (packed data) - it is not watched", folderID );
            return {};
        }
        unique_ptr<Watch> watch = make_unique<Watch>();
        for ( const string& diskFolder : listDiskFolder )
        {
            unique_ptr<FileWatchDispatcher> pDispatcher = make_unique<FileWatchDispatcher>();
            if ( pDispatcher->initialize( diskFolder ) == false )
                return {}; // 감시자가 이유를 남겼다
            watch->_listMatch.push_back( pDispatcher->registerWatch( diskFolder, listExtension, FileWatchMatchDelegate::create<&Watch::onFileChanged>( watch.get() ) ) );
            watch->_listDispatcher.push_back( std::move( pDispatcher ) );
        }
        watch->_id        = _watchIDAllocator.allocate();
        watch->_onChanged = onChanged;
        const uint64 id   = watch->_id;
        _listWatch.push_back( std::move( watch ) );
        return _subscriptions.issue( id );
    }

    void ContentFolderWatchService::update()
    {
        for ( size_t index = 0; index < _listWatch.size(); ++index )
        {
            Watch& watch = *_listWatch[index];
            for ( const unique_ptr<FileWatchDispatcher>& pDispatcher : watch._listDispatcher )
            {
                pDispatcher->update();
            }
            if ( watch._bChanged == false )
                continue;
            watch._bChanged = false;
            if ( watch._onChanged.isBound() )
                watch._onChanged(); // 알림 안에서 구독을 놓아도 된다 — 지우기는 releaseWatch 가 한다(인덱스로 돈다)
        }
    }

    void ContentFolderWatchService::releaseWatch( uint64 id )
    {
        for ( size_t index = 0; index < _listWatch.size(); ++index )
        {
            if ( _listWatch[index]->_id != id )
                continue;
            Watch& watch = *_listWatch[index];
            watch._listMatch.clear();
            for ( const unique_ptr<FileWatchDispatcher>& pDispatcher : watch._listDispatcher )
            {
                pDispatcher->shutdown();
            }
            _listWatch.erase( _listWatch.begin() + static_cast<ptrdiff_t>( index ) );
            return;
        }
    }
} // namespace sw
