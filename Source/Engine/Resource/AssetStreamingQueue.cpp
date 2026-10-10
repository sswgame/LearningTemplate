#include "pch.h"

#include "Engine/Resource/AssetStreamingQueue.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Resource/ResourceUtil.h"

namespace sw
{
    AssetStreamingQueue::AssetStreamingQueue()
        : _mutex{}
        , _mapAssetResult{}
        , _uniqueActiveRequest{}
        , _uniqueActiveDataRequest{}
        , _mapInFlightCallback{}
        , _mapInFlightDataCallback{}
        , _mapRequestGeneration{}
        , _mapInFlightIO{}
        , _completedMutex{}
        , _listCompleted{}
        , _bInitialized{ false }
    {
    }

    AssetStreamingQueue::~AssetStreamingQueue()
    {
        shutdown();
    }

    void AssetStreamingQueue::initialize()
    {
        std::scoped_lock<mutex> lock{ _mutex };
        _bInitialized = true;
    }

    void AssetStreamingQueue::shutdown()
    {
        // 순서: 찾기 태스크가 읽기를 다 걸게 둔다 → 걸린 바이트 읽기를 멈추고 완료(태스크 워커가 이 큐를 부른다)까지 기다린다. 핸들은 잠금 밖에서
        // 다룬다 — 취소의 완료가 이 스레드에서 바로 올 수 있고, 그 완료가 `_mutex` 를 잡는다.
        if ( engine::areEngineServicesBound() )
            engine::getTaskManager().waitAll();
        vector<AsyncReadHandle> listIO;
        {
            std::scoped_lock<mutex> lock{ _mutex };
            for ( auto& [path, handle] : _mapInFlightIO )
            {
                listIO.push_back( handle );
            }
        }
        for ( const AsyncReadHandle& handle : listIO )
        {
            (void)handle.cancel();
        }
        for ( const AsyncReadHandle& handle : listIO )
        {
            handle.wait();
        }

        if ( engine::areEngineServicesBound() )
            engine::getTaskManager().waitAll();

        {
            std::scoped_lock<mutex> lock{ _mutex };
            _uniqueActiveRequest.clear();
            _uniqueActiveDataRequest.clear();
            _mapInFlightCallback.clear();
            _mapInFlightDataCallback.clear();
            _mapRequestGeneration.clear();
            _mapInFlightIO.clear();
            _bInitialized = false;
        }
        std::scoped_lock<mutex> completedLock{ _completedMutex };
        _listCompleted.clear();
    }

    bool AssetStreamingQueue::requestAsset( string_view assetPath, StreamingPriority priority, const OnStreamingCompleteDelegate& onComplete )
    {
        if ( assetPath.empty() )
            return false;

        const string pathStr = string( assetPath );

        // **성공한 것만** 여기서 끝낸다. 실패는 기록돼 있어도 아래로 흘려보내 다시 요청한다
        // (아직 쿠킹하지 않은 셰이더, 늦게 마운트되는 팩 — 한 번 실패한 경로가 영영 실패가 되지 않게).
        // 콜백은 **락 밖에서** 부른다(비동기 완료 `update` 와 같다). 락을 쥔 채 부르면 콜백이 이 큐에 다시 물을 때(`isLoaded` ·
        // `requestAsset`) 같은 뮤텍스에 재진입해 스레드가 스스로 멈춘다.
        bool bAlreadyLoaded{ false };
        {
            std::scoped_lock<mutex> lock{ _mutex };
            const auto              itResult = _mapAssetResult.find( pathStr );
            bAlreadyLoaded                   = ( itResult != _mapAssetResult.end() && itResult->second );
        }
        if ( bAlreadyLoaded )
        {
            if ( onComplete.isBound() )
                onComplete( pathStr, true );
            return true;
        }

        std::scoped_lock<mutex> lock{ _mutex };

        if ( _uniqueActiveRequest.find( pathStr ) != _uniqueActiveRequest.end() )
        {
            if ( onComplete.isBound() )
                _mapInFlightCallback[pathStr].push_back( onComplete );
            return true;
        }

        _uniqueActiveRequest.insert( pathStr );
        const uint64 generation = ++_mapRequestGeneration[pathStr];
        if ( onComplete.isBound() )
            _mapInFlightCallback[pathStr].push_back( onComplete );

        startRequestLocked( pathStr, generation, false, priority );
        return true;
    }

    bool AssetStreamingQueue::requestAssetData( string_view assetPath, StreamingPriority priority, const OnStreamingDataCompleteDelegate& onComplete )
    {
        if ( assetPath.empty() )
            return false;

        const string pathStr = string( assetPath );
        uint64       generation{ 0 };
        bool         bIssueRead{ false };
        {
            std::scoped_lock<mutex> lock{ _mutex };

            // **바이트를 읽는 요청에만 편승한다.** 존재 확인 태스크(`requestAsset`)는 파일을 읽지
            // 않으므로, 거기 붙으면 `bSuccess = true` 에 빈 버퍼를 받는다. 그때는 세대를 올려
            // 그 태스크의 완료를 버리고 **데이터 읽기를 새로 건다.** `processAssetTask` 는 세대가
            // 어긋나면 콜백 표에 손대기 전에 돌아가므로, 먼저 등록된 존재 확인 콜백도 그대로 살아
            // 새 읽기의 완료에 함께 실린다.
            const bool bAlreadyFetchingData = _uniqueActiveDataRequest.find( pathStr ) != _uniqueActiveDataRequest.end();
            if ( bAlreadyFetchingData )
            {
                if ( onComplete.isBound() )
                    _mapInFlightDataCallback[pathStr].push_back( onComplete );
                return true;
            }

            _uniqueActiveRequest.insert( pathStr );
            _uniqueActiveDataRequest.insert( pathStr );
            generation = ++_mapRequestGeneration[pathStr];
            if ( onComplete.isBound() )
                _mapInFlightDataCallback[pathStr].push_back( onComplete );

            if ( engine::areEngineServicesBound() )
                bIssueRead = true;
            else
                startRequestLocked( pathStr, generation, true, priority );
        }

        // 찾기(낱개 파일의 존재 확인 — 검색 루트마다 파일 시스템 조회 한 번)는 부른 스레드가 아니라 워커에서 한다. 게임 스레드가 요청 천 개를 내면
        // 그 조회만 수백 ms 다. 찾은 뒤의 바이트 읽기는 비동기 IO 이므로 워커를 막지 않는다.
        if ( bIssueRead )
        {
            TaskHandle handle = engine::getTaskManager().emplaceTask(
                "AssetStreamingResolve",
                SW_DELEGATE_LAMBDA( TaskDelegate, [this, pathStr, generation, priority]()
            {
                issueDataRead( pathStr, generation, priority );
            } ) );
            handle.setPriority( toTaskPriority( priority ) );
            handle.submit();
        }
        return true;
    }

    TaskFuture<bool> AssetStreamingQueue::requestAssetFuture( string_view assetPath, StreamingPriority priority )
    {
        auto       pPromise   = sw::make_shared<TaskPromise<bool>>();
        const bool bRequested = requestAsset(
            assetPath,
            priority,
            SW_DELEGATE_LAMBDA(
                OnStreamingCompleteDelegate,
                [pPromise]( string_view /*path*/, bool bSuccess )
        {
            pPromise->setValue( bSuccess );
        } ) );

        if ( bRequested == false )
            pPromise->setValue( false );
        return pPromise->getFuture();
    }

    TaskPriority AssetStreamingQueue::toTaskPriority( StreamingPriority priority )
    {
        switch ( priority )
        {
            case StreamingPriority::Low:
            case StreamingPriority::Normal:
                return TaskPriority::Low;
            case StreamingPriority::High:
            case StreamingPriority::Immediate:
                return TaskPriority::Normal;
        }
        return TaskPriority::Low;
    }

    AsyncIOPriority AssetStreamingQueue::toIOPriority( StreamingPriority priority )
    {
        switch ( priority )
        {
            case StreamingPriority::Low:
                return AsyncIOPriority::Low;
            case StreamingPriority::Normal:
                return AsyncIOPriority::Normal;
            case StreamingPriority::High:
                return AsyncIOPriority::High;
            case StreamingPriority::Immediate:
                return AsyncIOPriority::Critical;
        }
        return AsyncIOPriority::Normal;
    }

    void AssetStreamingQueue::startRequestLocked( const string& pathStr, uint64 generation, bool bFetchData, StreamingPriority priority )
    {
        if ( engine::areEngineServicesBound() )
        {
            TaskHandle handle = engine::getTaskManager().emplaceTask(
                "AssetStreamingTask",
                SW_DELEGATE_METHOD( TaskArgsDelegate, &AssetStreamingQueue::processAssetTask, this ),
                MakeTaskArgs( pathStr, generation, bFetchData ),
                TaskThreadAffinity::Any );
            handle.setPriority( toTaskPriority( priority ) );
            handle.submit();
            return;
        }

        // 엔진 서비스가 없으면(테스트 · 툴) 그 자리에서 끝낸다. 태스크가 끝났을 때와 **같은 완료 절차**다.
        vector<uint8> bytes;
        const bool    bSuccess = bFetchData ? ResourceUtil::readBinaryResource( pathStr, bytes ) : ResourceUtil::hasResource( pathStr );
        completeRequestLocked( pathStr, bSuccess, std::move( bytes ) );
    }

    void AssetStreamingQueue::issueDataRead( const string& pathStr, uint64 generation, StreamingPriority priority )
    {
        // 찾기 태스크가 도는 사이 취소 · 다시 요청됐으면 걸지 않는다(취소가 이미 콜백을 실패로 돌려보냈다).
        {
            std::scoped_lock<mutex> lock{ _mutex };
            const auto              itGeneration = _mapRequestGeneration.find( pathStr );
            if ( itGeneration == _mapRequestGeneration.end() || itGeneration->second != generation )
                return;
        }
        const AsyncReadHandle handle = ResourceUtil::readBinaryResourceAsync(
            pathStr, toIOPriority( priority ),
            SW_DELEGATE_LAMBDA( ResourceReadCompleteDelegate, [this, pathStr, generation]( bool bSuccess, vector<uint8>& bytes )
        {
            onDataRead( pathStr, generation, bSuccess, bytes );
        } ) );

        std::scoped_lock<mutex> lock{ _mutex };
        const auto              itGeneration = _mapRequestGeneration.find( pathStr );
        const bool              bCurrent     = itGeneration != _mapRequestGeneration.end() && itGeneration->second == generation;
        if ( handle.isValid() == false )
        {
            // 어디에도 없다(낱개 파일 · 팩 모두). 읽기를 걸지 않았으니 여기서 실패로 끝낸다.
            if ( bCurrent && _uniqueActiveDataRequest.find( pathStr ) != _uniqueActiveDataRequest.end() )
                completeRequestLocked( pathStr, false, vector<uint8>{} );
            return;
        }
        // 이미 끝났으면(빠른 읽기 · 취소) 핸들을 적지 않는다 — 완료가 진행 표를 먼저 지웠다.
        if ( bCurrent && _uniqueActiveDataRequest.find( pathStr ) != _uniqueActiveDataRequest.end() )
            _mapInFlightIO[pathStr] = handle;
    }

    void AssetStreamingQueue::onDataRead( const string& pathStr, uint64 generation, bool bSuccess, vector<uint8>& bytes )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        const auto              itGeneration = _mapRequestGeneration.find( pathStr );
        if ( itGeneration == _mapRequestGeneration.end() || itGeneration->second != generation )
            return;
        completeRequestLocked( pathStr, bSuccess, std::move( bytes ) );
    }

    void AssetStreamingQueue::completeRequestLocked( const string& pathStr, bool bSuccess, vector<uint8>&& bytes )
    {
        _mapAssetResult[pathStr] = bSuccess;
        _uniqueActiveRequest.erase( pathStr );
        _uniqueActiveDataRequest.erase( pathStr );
        _mapInFlightIO.erase( pathStr );

        auto itCallbacks = _mapInFlightCallback.find( pathStr );
        if ( itCallbacks != _mapInFlightCallback.end() )
        {
            for ( const auto& cb : itCallbacks->second )
            {
                CompletedItem item{};
                item._path     = pathStr;
                item._callback = cb;
                item._bSuccess = bSuccess;
                pushCompleted( std::move( item ) );
            }
            _mapInFlightCallback.erase( itCallbacks );
        }

        auto itDataCallbacks = _mapInFlightDataCallback.find( pathStr );
        if ( itDataCallbacks != _mapInFlightDataCallback.end() )
        {
            const vector<OnStreamingDataCompleteDelegate>& listCallback = itDataCallbacks->second;
            for ( size_t index = 0; index < listCallback.size(); ++index )
            {
                CompletedItem item{};
                item._path         = pathStr;
                item._dataCallback = listCallback[index];
                item._bSuccess     = bSuccess;
                // 마지막 콜백이 바이트를 옮겨 간다 — 64 KB 항목을 콜백마다 복사하지 않는다.
                if ( index + 1 == listCallback.size() )
                    item._bytes = std::move( bytes );
                else
                    item._bytes = bytes;
                pushCompleted( std::move( item ) );
            }
            _mapInFlightDataCallback.erase( itDataCallbacks );
        }
    }

    void AssetStreamingQueue::pushCompleted( CompletedItem&& item )
    {
        std::scoped_lock<mutex> lock{ _completedMutex };
        _listCompleted.push_back( std::move( item ) );
    }

    void AssetStreamingQueue::processAssetTask( const TaskArgs& args )
    {
        const string pathStr    = args.get<string>( 0 );
        const uint64 generation = args.get<uint64>( 1 );
        const bool   bFetchData = args.getCount() > 2 ? args.get<bool>( 2 ) : false;

        vector<uint8> bytes;
        bool          bSuccess = false;
        if ( bFetchData )
            bSuccess = ResourceUtil::readBinaryResource( pathStr, bytes );
        else
            bSuccess = ResourceUtil::hasResource( pathStr );

        std::scoped_lock<mutex> innerLock{ _mutex };

        const auto itGeneration = _mapRequestGeneration.find( pathStr );
        if ( itGeneration == _mapRequestGeneration.end() || itGeneration->second != generation )
            return;

        completeRequestLocked( pathStr, bSuccess, std::move( bytes ) );
    }

    void AssetStreamingQueue::cancelRequest( string_view assetPath )
    {
        const string    pathStr = string( assetPath );
        AsyncReadHandle ioHandle;
        {
            std::scoped_lock<mutex> lock{ _mutex };
            _uniqueActiveRequest.erase( pathStr );
            _uniqueActiveDataRequest.erase( pathStr );
            const auto itIO = _mapInFlightIO.find( pathStr );
            if ( itIO != _mapInFlightIO.end() )
            {
                ioHandle = itIO->second;
                _mapInFlightIO.erase( itIO );
            }

            const auto itCallbacks = _mapInFlightCallback.find( pathStr );
            if ( itCallbacks != _mapInFlightCallback.end() )
            {
                for ( const auto& cb : itCallbacks->second )
                {
                    CompletedItem item{};
                    item._path     = pathStr;
                    item._callback = cb;
                    item._bSuccess = false;
                    pushCompleted( std::move( item ) );
                }
                _mapInFlightCallback.erase( itCallbacks );
            }

            const auto itDataCallbacks = _mapInFlightDataCallback.find( pathStr );
            if ( itDataCallbacks != _mapInFlightDataCallback.end() )
            {
                for ( const auto& cb : itDataCallbacks->second )
                {
                    CompletedItem item{};
                    item._path         = pathStr;
                    item._dataCallback = cb;
                    item._bSuccess     = false;
                    pushCompleted( std::move( item ) );
                }
                _mapInFlightDataCallback.erase( itDataCallbacks );
            }

            ++_mapRequestGeneration[pathStr];
        }

        // 아직 OS 에 넘기지 않은 읽기는 큐에서 빠지고, 걸린 읽기는 결과를 버린다(세대가 올라 완료는 무시된다). 잠금 밖이다 — 취소의 완료가 이 스레드에서
        // 바로 올 수 있다.
        if ( ioHandle.isValid() )
            (void)ioHandle.cancel();
    }

    void AssetStreamingQueue::clearCompletionRecord()
    {
        std::scoped_lock<mutex> lock{ _mutex };
        _mapAssetResult.clear();
    }

    bool AssetStreamingQueue::isStreaming( string_view assetPath ) const
    {
        const string            pathStr = string( assetPath );
        std::scoped_lock<mutex> lock{ _mutex };
        return _uniqueActiveRequest.find( pathStr ) != _uniqueActiveRequest.end();
    }

    bool AssetStreamingQueue::isLoaded( string_view assetPath ) const
    {
        const string            pathStr = string( assetPath );
        std::scoped_lock<mutex> lock{ _mutex };
        const auto              itResult = _mapAssetResult.find( pathStr );
        return itResult != _mapAssetResult.end() && itResult->second;
    }

    size_t AssetStreamingQueue::getPendingCount() const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        return _uniqueActiveRequest.size();
    }

    size_t AssetStreamingQueue::getCompletedCount() const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        return _mapAssetResult.size();
    }

    void AssetStreamingQueue::update( size_t maxCompletionsPerFrame )
    {
        // 한 번에 꺼내 잠금 밖에서 부른다 — 콜백이 다시 요청을 넣어도(같은 잠금) 막히지 않는다.
        vector<CompletedItem> listReady;
        {
            std::scoped_lock<mutex> lock{ _completedMutex };
            const size_t            readyCount = _listCompleted.size() < maxCompletionsPerFrame ? _listCompleted.size() : maxCompletionsPerFrame;
            listReady.reserve( readyCount );
            for ( size_t index = 0; index < readyCount; ++index )
            {
                listReady.push_back( std::move( _listCompleted.front() ) );
                _listCompleted.pop_front();
            }
        }
        for ( CompletedItem& item : listReady )
        {
            if ( item._callback.isBound() )
                item._callback( item._path, item._bSuccess );
            if ( item._dataCallback.isBound() )
                item._dataCallback( item._path, item._bSuccess, item._bytes );
        }
    }
} // namespace sw
