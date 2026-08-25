#include "pch.h"

#include "Engine/Utility/Resource/AssetStreamingQueue.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Utility/Task/TaskManager.h"

namespace sw
{
	AssetStreamingQueue::AssetStreamingQueue()
		: _mutex{}
		, _listPendingRequests{}
		, _mapLoadedAssets{}
		, _uniqueActiveRequests{}
		, _mapInFlightCallbacks{}
		, _listCompletedItems{}
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
		_listPendingRequests.clear();
		_uniqueActiveRequests.clear();
		_mapInFlightCallbacks.clear();
		_listCompletedItems.clear();
		_bInitialized = false;
	}

	bool AssetStreamingQueue::requestAsset( string_view assetPath, StreamingPriority priority, OnStreamingCompleteDelegate onComplete )
	{
		if ( assetPath.empty() )
			return false;

		const string pathStr = string( assetPath );

		std::scoped_lock<mutex> lock{ _mutex };
		if ( _mapLoadedAssets.find( pathStr ) != _mapLoadedAssets.end() )
		{
			// 이미 로드됨
			if ( onComplete.isBound() )
				onComplete( pathStr, true );
			return true;
		}

		if ( _uniqueActiveRequests.find( pathStr ) != _uniqueActiveRequests.end() )
		{
			// 이미 처리 중인 경우 대기 콜백 목록에 추가하여 완료 시 함께 통지
			if ( onComplete.isBound() )
				_mapInFlightCallbacks[pathStr].push_back( onComplete );
			return true;
		}

		_uniqueActiveRequests.insert( pathStr );
		if ( onComplete.isBound() )
			_mapInFlightCallbacks[pathStr].push_back( onComplete );

		StreamingRequest req{};
		req._assetPath	= pathStr;
		req._priority	= priority;
		req._onComplete = onComplete;
		_listPendingRequests.push_back( req );

		if ( engine::areEngineServicesBound() )
		{
			TaskManager& taskManager = engine::getTaskManager();
			TaskHandle	 handle		 = taskManager.emplaceTask( "AssetStreamingTask", SW_DELEGATE_LAMBDA( TaskDelegate, [this, pathStr]()
			{
				const bool bExists = FileUtil::fileExists( pathStr );

				std::scoped_lock<mutex> innerLock{ _mutex };
				_mapLoadedAssets[pathStr] = bExists;
				_uniqueActiveRequests.erase( pathStr );

				auto itCallbacks = _mapInFlightCallbacks.find( pathStr );
				if ( itCallbacks != _mapInFlightCallbacks.end() )
				{
					for ( const auto& cb : itCallbacks->second )
					{
						CompletedItem item{};
						item._path	   = pathStr;
						item._bSuccess = bExists;
						item._callback = cb;
						_listCompletedItems.push_back( std::move( item ) );
					}
					_mapInFlightCallbacks.erase( itCallbacks );
				}
			} ),
																TaskThreadAffinity::Any );
			handle.submit();
		}
		else
		{
			// 동기 즉시 폴백
			const bool bExists		  = FileUtil::fileExists( pathStr );
			_mapLoadedAssets[pathStr] = bExists;
			_uniqueActiveRequests.erase( pathStr );

			auto itCallbacks = _mapInFlightCallbacks.find( pathStr );
			if ( itCallbacks != _mapInFlightCallbacks.end() )
			{
				for ( const auto& cb : itCallbacks->second )
				{
					CompletedItem item{};
					item._path	   = pathStr;
					item._bSuccess = bExists;
					item._callback = cb;
					_listCompletedItems.push_back( std::move( item ) );
				}
				_mapInFlightCallbacks.erase( itCallbacks );
			}
		}

		return true;
	}

	void AssetStreamingQueue::cancelRequest( string_view assetPath )
	{
		const string			pathStr = string( assetPath );
		std::scoped_lock<mutex> lock{ _mutex };
		_uniqueActiveRequests.erase( pathStr );
		_mapInFlightCallbacks.erase( pathStr );

		for ( auto it = _listPendingRequests.begin(); it != _listPendingRequests.end(); ++it )
		{
			if ( it->_assetPath == pathStr )
			{
				_listPendingRequests.erase( it );
				break;
			}
		}
	}

	void AssetStreamingQueue::sweepUnusedCache()
	{
		std::scoped_lock<mutex> lock{ _mutex };
		_mapLoadedAssets.clear();
	}

	bool AssetStreamingQueue::isStreaming( string_view assetPath ) const
	{
		const string			pathStr = string( assetPath );
		std::scoped_lock<mutex> lock{ _mutex };
		return _uniqueActiveRequests.find( pathStr ) != _uniqueActiveRequests.end();
	}

	bool AssetStreamingQueue::isLoaded( string_view assetPath ) const
	{
		const string			pathStr = string( assetPath );
		std::scoped_lock<mutex> lock{ _mutex };
		return _mapLoadedAssets.find( pathStr ) != _mapLoadedAssets.end();
	}

	size_t AssetStreamingQueue::getPendingCount() const
	{
		std::scoped_lock<mutex> lock{ _mutex };
		return _listPendingRequests.size();
	}

	size_t AssetStreamingQueue::getCompletedCount() const
	{
		std::scoped_lock<mutex> lock{ _mutex };
		return _mapLoadedAssets.size();
	}

	void AssetStreamingQueue::update( size_t maxCompletionsPerFrame )
	{
		vector<CompletedItem> listToNotify;
		{
			std::scoped_lock<mutex> lock{ _mutex };
			if ( _listCompletedItems.empty() )
				return;

			const size_t count = MathUtil::min( maxCompletionsPerFrame, _listCompletedItems.size() );
			listToNotify.assign( _listCompletedItems.begin(), _listCompletedItems.begin() + count );
			_listCompletedItems.erase( _listCompletedItems.begin(), _listCompletedItems.begin() + count );
		}

		for ( const CompletedItem& item : listToNotify )
		{
			if ( item._callback.isBound() )
				item._callback( item._path, item._bSuccess );
		}
	}
} // namespace sw
