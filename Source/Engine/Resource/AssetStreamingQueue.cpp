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
        , _listPendingRequest{}
        , _mapLoadedAsset{}
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
        {
            engine::getTaskManager().waitAll();
        }

        std::scoped_lock<mutex> lock{ _mutex };
        _listPendingRequest.clear();
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
        if ( assetPath.empty() )
            return false;

        const string pathStr = string( assetPath );

        std::scoped_lock<mutex> lock{ _mutex };
        if ( _mapLoadedAsset.find( pathStr ) != _mapLoadedAsset.end() )
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

        StreamingRequest req{};
        req._assetPath  = pathStr;
        req._priority   = priority;
        req._onComplete = onComplete;
        _listPendingRequest.push_back( req );

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
            _mapLoadedAsset[pathStr] = bExists;
            _uniqueActiveRequest.erase( pathStr );

            for ( auto it = _listPendingRequest.begin(); it != _listPendingRequest.end(); ++it )
            {
                if ( it->_assetPath == pathStr )
                {
                    _listPendingRequest.erase( it );
                    break;
                }
            }

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

        StreamingRequest req{};
        req._assetPath = pathStr;
        req._priority  = priority;
        _listPendingRequest.push_back( req );

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
            _mapLoadedAsset[pathStr] = bSuccess;
            _uniqueActiveRequest.erase( pathStr );

            for ( auto it = _listPendingRequest.begin(); it != _listPendingRequest.end(); ++it )
            {
                if ( it->_assetPath == pathStr )
                {
                    _listPendingRequest.erase( it );
                    break;
                }
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
        {
            pPromise->setValue( false );
        }
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
        {
            bSuccess = ResourceUtil::readBinaryResource( pathStr, bytes );
        }
        else
        {
            bSuccess = ResourceUtil::hasResource( pathStr );
        }

        std::scoped_lock<mutex> innerLock{ _mutex };

        const auto itGeneration = _mapRequestGeneration.find( pathStr );
        if ( itGeneration == _mapRequestGeneration.end() || itGeneration->second != generation )
            return;

        _mapLoadedAsset[pathStr] = bSuccess;
        _uniqueActiveRequest.erase( pathStr );

        for ( auto it = _listPendingRequest.begin(); it != _listPendingRequest.end(); ++it )
        {
            if ( it->_assetPath == pathStr )
            {
                _listPendingRequest.erase( it );
                break;
            }
        }

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

        for ( auto it = _listPendingRequest.begin(); it != _listPendingRequest.end(); ++it )
        {
            if ( it->_assetPath == pathStr )
            {
                _listPendingRequest.erase( it );
                break;
            }
        }
    }

    void AssetStreamingQueue::sweepUnusedCache()
    {
        std::scoped_lock<mutex> lock{ _mutex };
        _mapLoadedAsset.clear();
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
        return _mapLoadedAsset.find( pathStr ) != _mapLoadedAsset.end();
    }

    size_t AssetStreamingQueue::getPendingCount() const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        return _listPendingRequest.size();
    }

    size_t AssetStreamingQueue::getCompletedCount() const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        return _mapLoadedAsset.size();
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
