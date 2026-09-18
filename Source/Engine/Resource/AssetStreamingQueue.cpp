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
        , _mapInFlightCallback{}
        , _mapRequestGeneration{}
        , _queueCompleted{}
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
        if ( engine::areEngineServicesBound() )
            engine::getTaskManager().waitAll();

        std::scoped_lock<mutex> lock{ _mutex };
        _uniqueActiveRequest.clear();
        _mapInFlightCallback.clear();
        _mapInFlightDataCallback.clear();
        _mapRequestGeneration.clear();

        CompletedItem discardItem{};
        while ( _queueCompleted.dequeue( discardItem ) )
        {
        }

        _bInitialized = false;
    }

    bool AssetStreamingQueue::requestAsset( string_view assetPath, StreamingPriority priority, OnStreamingCompleteDelegate onComplete )
    {
        // StreamingPriority 는 아직 순서에 반영되지 않는다 (헤더의 enum 주석 참고).
        (void)priority;
        if ( assetPath.empty() )
            return false;

        const string pathStr = string( assetPath );

        std::scoped_lock<mutex> lock{ _mutex };

        // **성공한 것만** 여기서 끝낸다. 실패는 기록돼 있어도 아래로 흘려보내 다시 요청한다 —
        // 예전에는 키가 있기만 하면 `true` 를 돌려줬고, 그래서 한 번 실패한 경로는 영영 실패였다.
        // (아직 굽지 않은 셰이더, 늦게 마운트되는 팩 — 한 번 빗나가면 다시는 보지 않았다.)
        const auto itResult = _mapAssetResult.find( pathStr );
        if ( itResult != _mapAssetResult.end() && itResult->second )
        {
            if ( onComplete.isBound() )
                onComplete( pathStr, true );
            return true;
        }

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

        if ( engine::areEngineServicesBound() )
        {
            TaskManager& taskManager = engine::getTaskManager();
            TaskHandle   handle      = taskManager.emplaceTask(
                "AssetStreamingTask",
                SW_DELEGATE_METHOD( TaskArgsDelegate, &AssetStreamingQueue::processAssetTask, this ),
                MakeTaskArgs( pathStr, generation, false ),
                TaskThreadAffinity::Any );
            handle.submit();
        }
        else
        {
            const bool bExists       = ResourceUtil::hasResource( pathStr );
            _mapAssetResult[pathStr] = bExists;
            _uniqueActiveRequest.erase( pathStr );

            auto itCallbacks = _mapInFlightCallback.find( pathStr );
            if ( itCallbacks != _mapInFlightCallback.end() )
            {
                for ( const auto& cb : itCallbacks->second )
                {
                    CompletedItem item{};
                    item._path     = pathStr;
                    item._callback = cb;
                    item._bSuccess = bExists;
                    _queueCompleted.enqueue( std::move( item ) );
                }
                _mapInFlightCallback.erase( itCallbacks );
            }
        }

        return true;
    }

    bool AssetStreamingQueue::requestAssetData( string_view assetPath, StreamingPriority priority, OnStreamingDataCompleteDelegate onComplete )
    {
        // StreamingPriority 는 아직 순서에 반영되지 않는다 (헤더의 enum 주석 참고).
        (void)priority;
        if ( assetPath.empty() )
            return false;

        const string pathStr = string( assetPath );

        std::scoped_lock<mutex> lock{ _mutex };
        if ( _uniqueActiveRequest.find( pathStr ) != _uniqueActiveRequest.end() )
        {
            if ( onComplete.isBound() )
                _mapInFlightDataCallback[pathStr].push_back( onComplete );
            return true;
        }

        _uniqueActiveRequest.insert( pathStr );
        const uint64 generation = ++_mapRequestGeneration[pathStr];
        if ( onComplete.isBound() )
            _mapInFlightDataCallback[pathStr].push_back( onComplete );

        if ( engine::areEngineServicesBound() )
        {
            TaskManager& taskManager = engine::getTaskManager();
            TaskHandle   handle      = taskManager.emplaceTask(
                "AssetStreamingTask",
                SW_DELEGATE_METHOD( TaskArgsDelegate, &AssetStreamingQueue::processAssetTask, this ),
                MakeTaskArgs( pathStr, generation, true ),
                TaskThreadAffinity::Any );
            handle.submit();
        }
        else
        {
            vector<uint8> bytes;
            const bool    bSuccess   = ResourceUtil::readBinaryResource( pathStr, bytes );
            _mapAssetResult[pathStr] = bSuccess;
            _uniqueActiveRequest.erase( pathStr );

            auto itDataCallbacks = _mapInFlightDataCallback.find( pathStr );
            if ( itDataCallbacks != _mapInFlightDataCallback.end() )
            {
                for ( const auto& cb : itDataCallbacks->second )
                {
                    CompletedItem item{};
                    item._path         = pathStr;
                    item._dataCallback = cb;
                    item._bSuccess     = bSuccess;
                    item._bytes        = bytes;
                    _queueCompleted.enqueue( std::move( item ) );
                }
                _mapInFlightDataCallback.erase( itDataCallbacks );
            }
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

        _mapAssetResult[pathStr] = bSuccess;
        _uniqueActiveRequest.erase( pathStr );

        auto itCallbacks = _mapInFlightCallback.find( pathStr );
        if ( itCallbacks != _mapInFlightCallback.end() )
        {
            for ( const auto& cb : itCallbacks->second )
            {
                CompletedItem item{};
                item._path     = pathStr;
                item._callback = cb;
                item._bSuccess = bSuccess;
                _queueCompleted.enqueue( std::move( item ) );
            }
            _mapInFlightCallback.erase( itCallbacks );
        }

        auto itDataCallbacks = _mapInFlightDataCallback.find( pathStr );
        if ( itDataCallbacks != _mapInFlightDataCallback.end() )
        {
            for ( const auto& cb : itDataCallbacks->second )
            {
                CompletedItem item{};
                item._path         = pathStr;
                item._dataCallback = cb;
                item._bSuccess     = bSuccess;
                item._bytes        = bytes;
                _queueCompleted.enqueue( std::move( item ) );
            }
            _mapInFlightDataCallback.erase( itDataCallbacks );
        }
    }

    void AssetStreamingQueue::cancelRequest( string_view assetPath )
    {
        const string            pathStr = string( assetPath );
        std::scoped_lock<mutex> lock{ _mutex };
        _uniqueActiveRequest.erase( pathStr );

        const auto itCallbacks = _mapInFlightCallback.find( pathStr );
        if ( itCallbacks != _mapInFlightCallback.end() )
        {
            for ( const auto& cb : itCallbacks->second )
            {
                CompletedItem item{};
                item._path     = pathStr;
                item._callback = cb;
                item._bSuccess = false;
                _queueCompleted.enqueue( std::move( item ) );
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
                _queueCompleted.enqueue( std::move( item ) );
            }
            _mapInFlightDataCallback.erase( itDataCallbacks );
        }

        ++_mapRequestGeneration[pathStr];
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
        CompletedItem item{};
        size_t        processedCount = 0;
        while ( processedCount < maxCompletionsPerFrame && _queueCompleted.dequeue( item ) )
        {
            if ( item._callback.isBound() )
                item._callback( item._path, item._bSuccess );
            if ( item._dataCallback.isBound() )
                item._dataCallback( item._path, item._bSuccess, item._bytes );
            ++processedCount;
        }
    }
} // namespace sw
