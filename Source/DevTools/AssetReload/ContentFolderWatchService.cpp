#include "pch.h"

#include "DevTools/AssetReload/ContentFolderWatchService.h"

#include "Core/Log/Logger.h"

#include "DevTools/FileWatch/FileWatchService.h"

#include "Engine/Content/AssetPaths.h"
#include "Engine/Foundation/EngineSubsystems.h"

namespace sw
{
    /** @struct ContentFolderWatchService::Watch @brief 감시 하나 — 디스크 폴더마다의 구독, 알릴 델리게이트, 이번 프레임에 바뀌었는지입니다. */
    struct ContentFolderWatchService::Watch
    {
        uint64               _id{ 0 };
        vector<Subscription> _listMatch{};
        ChangedDelegate      _onChanged{};
        bool                 _bChanged{ SW_FALSE };

        void onFileChanged( const FileChangeEvent& /*changeEvent*/ ) { _bChanged = SW_TRUE; } // 확장자는 감시 창구가 거른다 — 바뀌었다는 사실만 쓴다
    };

    ContentFolderWatchService::ContentFolderWatchService( FileWatchService& fileWatch )
        : _fileWatch{ fileWatch }
        , _listWatch{}
        , _watchIDAllocator{}
    {
    }

    ContentFolderWatchService::~ContentFolderWatchService() = default;

    Subscription ContentFolderWatchService::watchAssetFolder( string_view folderID, const vector<string>& listExtension, const ChangedDelegate& onChanged )
    {
        vector<DiskPath> listDiskFolder;
        engine::getSubsystem<AssetPaths>().collectLooseFolders( folderID, listDiskFolder );
        if ( listDiskFolder.empty() )
        {
            SW_LOG_INFO_ID( "ContentFolderWatch.NotDiskLooseFiles", "'%#' is not on disk as loose files (packed data) - it is not watched", folderID );
            return {};
        }
        unique_ptr<Watch> watch = make_unique<Watch>();
        for ( const DiskPath& diskFolder : listDiskFolder )
        {
            Subscription match = _fileWatch.watch( diskFolder, FileWatchDepth::Tree, listExtension, SW_DELEGATE_METHOD( FileChangeDelegate, &Watch::onFileChanged, watch.get() ) );
            if ( match.isActive() == false )
                return {}; // 감시 창구가 이유를 남겼다
            watch->_listMatch.push_back( std::move( match ) );
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
            if ( watch._bChanged == false )
                continue;
            watch._bChanged = SW_FALSE;
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
            _listWatch.erase( _listWatch.begin() + static_cast<ptrdiff_t>( index ) );
            return;
        }
    }
} // namespace sw
