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
			// 이미 로드됨
			if ( onComplete.isBound() )
				onComplete( pathStr, true );
			return true;
		}

		if ( _uniqueActiveRequest.find( pathStr ) != _uniqueActiveRequest.end() )
		{
			// 이미 처리 중인 경우 대기 콜백 목록에 추가하여 완료 시 함께 통지
			if ( onComplete.isBound() )
				_mapInFlightCallback[pathStr].push_back( onComplete );
			return true;
		}

		_uniqueActiveRequest.insert( pathStr );
		if ( onComplete.isBound() )
			_mapInFlightCallback[pathStr].push_back( onComplete );

		StreamingRequest req{};
		req._assetPath	= pathStr;
		req._priority	= priority;
		req._onComplete = onComplete;
		_listPendingRequest.push_back( req );

		if ( engine::areEngineServicesBound() )
		{
			TaskManager& taskManager = engine::getTaskManager();
			TaskHandle	 handle		 = taskManager.emplaceTask(
				   "AssetStreamingTask",
				   SW_DELEGATE_METHOD( TaskArgsDelegate, &AssetStreamingQueue::processAssetTask, this ),
				   MakeTaskArgs( pathStr ),
				   TaskThreadAffinity::Any );
			handle.submit();
		}
		else
		{
			// 동기 즉시 폴백
			const bool bExists		 = ResourceUtil::hasResource( pathStr );
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
					item._path	   = pathStr;
					item._callback = cb;
					item._bSuccess = bExists;
					_queueCompleted.enqueue( std::move( item ) );
				}
				_mapInFlightCallback.erase( itCallbacks );
			}
		}

		return true;
	}

	TaskFuture<bool> AssetStreamingQueue::requestAssetFuture( string_view assetPath, StreamingPriority priority )
	{
		auto	   pPromise	  = sw::make_shared<TaskPromise<bool>>();
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
		const string pathStr = args.get<string>( 0 );
		const bool	 bExists = ResourceUtil::hasResource( pathStr );

		std::scoped_lock<mutex> innerLock{ _mutex };
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
				item._path	   = pathStr;
				item._callback = cb;
				item._bSuccess = bExists;
				_queueCompleted.enqueue( std::move( item ) );
			}
			_mapInFlightCallback.erase( itCallbacks );
		}
	}

	void AssetStreamingQueue::cancelRequest( string_view assetPath )
	{
		const string			pathStr = string( assetPath );
		std::scoped_lock<mutex> lock{ _mutex };
		_uniqueActiveRequest.erase( pathStr );
		_mapInFlightCallback.erase( pathStr );

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
		const string			pathStr = string( assetPath );
		std::scoped_lock<mutex> lock{ _mutex };
		return _uniqueActiveRequest.find( pathStr ) != _uniqueActiveRequest.end();
	}

	bool AssetStreamingQueue::isLoaded( string_view assetPath ) const
	{
		const string			pathStr = string( assetPath );
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
		size_t		  processedCount = 0;
		while ( processedCount < maxCompletionsPerFrame && _queueCompleted.dequeue( item ) )
		{
			if ( item._callback.isBound() )
				item._callback( item._path, item._bSuccess );
			++processedCount;
		}
	}
} // namespace sw
