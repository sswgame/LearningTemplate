#pragma once
#include "Core/Delegate/Delegate.h"
#include "Core/Task/TaskTypes.h"

#include "Engine/EngineMinimal.h"

namespace sw
{
	/**
	 * @enum StreamingPriority
	 * @brief 에셋 비동기 스트리밍 우선순위
	 */
	enum class StreamingPriority : uint8
	{
		Low		  = 0,
		Normal	  = 1,
		High	  = 2,
		Immediate = 3,
	};

	using OnStreamingCompleteDelegate = Delegate<void( string_view, bool )>;

	/**
	 * @struct StreamingRequest
	 * @brief 비동기 스트리밍 작업 단위
	 */
	struct SW_API StreamingRequest
	{
		string						_assetPath{};
		StreamingPriority			_priority{ StreamingPriority::Normal };
		OnStreamingCompleteDelegate _onComplete{};
	};

	/**
	 * @class AssetStreamingQueue
	 * @brief 백그라운드 멀티스레드 에셋 프리페치 및 스트리밍 큐
	 * @details 씬 로드 및 런타임 이동 중 메인 스레드 스터터링 없이 텍스처/오디오/머티리얼을 비동기 프리로드합니다.
	 */
	class SW_API AssetStreamingQueue
	{
	public:
		AssetStreamingQueue();
		~AssetStreamingQueue();

		void initialize();
		void shutdown();
		void update( size_t maxCompletionsPerFrame = 32 );

		bool requestAsset( string_view assetPath, StreamingPriority priority = StreamingPriority::Normal, OnStreamingCompleteDelegate onComplete = {} );
		void cancelRequest( string_view assetPath );

		void sweepUnusedCache();

		bool isStreaming( string_view assetPath ) const;
		bool isLoaded( string_view assetPath ) const;

		size_t getPendingCount() const;
		size_t getCompletedCount() const;

	private:
		struct CompletedItem
		{
			string						_path;
			bool						_bSuccess;
			OnStreamingCompleteDelegate _callback;
		};

		/** @brief TaskArgs: path string. 워커에서 파일 존재 여부를 확인합니다. */
		void processAssetTask( const TaskArgs& args );

	private:
		mutable mutex											   _mutex;
		vector<StreamingRequest>								   _listPendingRequests;
		unordered_map<string, bool>								   _mapLoadedAssets;
		unordered_set<string>									   _uniqueActiveRequests;
		unordered_map<string, vector<OnStreamingCompleteDelegate>> _mapInFlightCallbacks;
		vector<CompletedItem>									   _listCompletedItems;
		bool													   _bInitialized;
	};
} // namespace sw
