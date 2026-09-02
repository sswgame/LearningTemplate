#pragma once
#include "Core/Concurrency/ConcurrentQueue.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Task/TaskFuture.h"

#include "Engine/EngineMinimal.h"

namespace sw
{
    class TaskArgs;
    /**
     * @enum StreamingPriority
     * @brief 에셋 비동기 스트리밍 우선순위
     */
    enum class StreamingPriority : uint8
    {
        Low       = 0,
        Normal    = 1,
        High      = 2,
        Immediate = 3,
    };

    using OnStreamingCompleteDelegate     = Delegate<void( string_view, bool )>;
    using OnStreamingDataCompleteDelegate = Delegate<void( string_view, bool, const vector<uint8>& )>;

    /**
     * @struct StreamingRequest
     * @brief 비동기 스트리밍 작업 단위
     */
    struct SW_API StreamingRequest
    {
        string                      _assetPath{};
        StreamingPriority           _priority{ StreamingPriority::Normal };
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

        /** @brief 에셋 바이너리 데이터를 백그라운드에서 직접 프리페치하고 완료 콜백으로 전달받습니다. */
        bool requestAssetData( string_view assetPath, StreamingPriority priority = StreamingPriority::Normal, OnStreamingDataCompleteDelegate onComplete = {} );

        /** @brief 에셋 프리페치를 비동기 요청하고 완료 상태를 TaskFuture<bool>로 반환합니다. */
        TaskFuture<bool> requestAssetFuture( string_view assetPath, StreamingPriority priority = StreamingPriority::Normal );
        void             cancelRequest( string_view assetPath );

        void sweepUnusedCache();

        bool isStreaming( string_view assetPath ) const;
        bool isLoaded( string_view assetPath ) const;

        size_t getPendingCount() const;
        size_t getCompletedCount() const;

    private:
        struct CompletedItem
        {
            string                          _path;
            bool                            _bSuccess;
            vector<uint8>                   _bytes;
            OnStreamingCompleteDelegate     _callback;
            OnStreamingDataCompleteDelegate _dataCallback;
        };

        /** @brief TaskArgs: path string, generation, bFetchData. 워커에서 파일 존재 확인 및 바이너리 데이터를 읽습니다. */
        void processAssetTask( const TaskArgs& args );

    private:
        mutable mutex                                                  _mutex;
        vector<StreamingRequest>                                       _listPendingRequest;
        unordered_map<string, bool>                                    _mapLoadedAsset;
        unordered_set<string>                                          _uniqueActiveRequest;
        unordered_map<string, vector<OnStreamingCompleteDelegate>>     _mapInFlightCallback;
        unordered_map<string, vector<OnStreamingDataCompleteDelegate>> _mapInFlightDataCallback;
        /**
         * @brief 경로별 요청 세대. 취소 후 즉시 재요청하면 세대가 올라갑니다.
         * @details 취소해도 이미 큐에 들어간 워커 태스크는 계속 실행됩니다. 세대가 없으면
         *          그 오래된 태스크가 완료되면서 새 요청의 콜백을 대신 소비해 버립니다.
         */
        unordered_map<string, uint64>        _mapRequestGeneration;
        ConcurrentQueue<CompletedItem, 1024> _queueCompleted;
        bool                                 _bInitialized;
    };
} // namespace sw
