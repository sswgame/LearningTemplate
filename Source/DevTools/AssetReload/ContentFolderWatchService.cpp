#include "pch.h"

#include "DevTools/AssetReload/ContentFolderWatchService.h"

#include "DevTools/AssetReload/FileWatchDispatcher.h"
#include "DevTools/FileWatch/IFileWatcher.h"

namespace sw
{
    /** @struct ContentFolderWatchService::Watch @brief 감시 하나 — 분배기, 그 안의 경로 구독, 알릴 델리게이트, 이번 프레임에 바뀌었는지입니다. */
    struct ContentFolderWatchService::Watch
    {
        uint64                          _id{ 0 };
        unique_ptr<FileWatchDispatcher> _pDispatcher{};
        Subscription                    _match{};
        ChangedDelegate                 _onChanged{};
        bool                            _bChanged{ false };

        void onFileChanged( const FileChangeEvent& /*changeEvent*/ ) { _bChanged = true; } // 확장자는 분배기가 거른다 — 바뀌었다는 사실만 쓴다
    };

    ContentFolderWatchService::ContentFolderWatchService()
        : _listWatch{}
        , _nextID{ 1 }
    {
    }

    ContentFolderWatchService::~ContentFolderWatchService() = default;

    Subscription ContentFolderWatchService::watchDiskFolder( string_view absoluteFolder, const vector<string>& listExtension, const ChangedDelegate& onChanged )
    {
        unique_ptr<Watch> watch = make_unique<Watch>();
        watch->_pDispatcher     = make_unique<FileWatchDispatcher>();
        if ( watch->_pDispatcher->initialize( absoluteFolder ) == false )
            return {}; // 감시자가 이유를 남겼다
        watch->_id        = _nextID++;
        watch->_onChanged = onChanged;
        watch->_match     = watch->_pDispatcher->registerWatch( absoluteFolder, listExtension, FileWatchMatchDelegate::create<&Watch::onFileChanged>( watch.get() ) );
        const uint64 id   = watch->_id;
        _listWatch.push_back( std::move( watch ) );
        return _subscriptions.issue( id );
    }

    void ContentFolderWatchService::update()
    {
        for ( size_t index = 0; index < _listWatch.size(); ++index )
        {
            Watch& watch = *_listWatch[index];
            watch._pDispatcher->update();
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
            _listWatch[index]->_match.reset();
            _listWatch[index]->_pDispatcher->shutdown();
            _listWatch.erase( _listWatch.begin() + static_cast<ptrdiff_t>( index ) );
            return;
        }
    }
} // namespace sw
