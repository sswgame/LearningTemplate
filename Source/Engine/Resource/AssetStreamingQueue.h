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
     * @warning **지금은 순서에 반영되지 않는다.** 요청은 받는 즉시 TaskManager 로 넘어가고
     *          TaskManager 에는 우선순위 큐가 없다(affinity 만 있다). 값은 호출부 의도를
     *          남겨 두기 위해 받아 두며, 실제 정렬은 TaskManager 가 우선순위를 갖게 된 뒤에
     *          붙일 자리다. 예전엔 요청마다 이 값을 구조체에 저장했지만 읽는 곳이 없었다.
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

        /**
         * @brief 끝난 요청의 결과 기록을 통째로 버립니다 — 다음 요청은 디스크를 다시 본다.
         * @details 이 큐는 에셋 바이트를 들고 있지 않다(데이터 요청의 바이트는 콜백으로 나가고 버려진다).
         *          그래서 버릴 "쓰지 않는 캐시" 같은 것은 없고, 여기서 사라지는 것은 **무엇이 끝났고
         *          성공했는지에 대한 기억**뿐이다. 예전 이름은 `sweepUnusedCache` 였는데, 쓰이지 않는
         *          것을 골라내는 일은 하지 않으면서(전부 지운다) 이름은 고른다고 말하고 있었다.
         */
        void clearCompletionRecord();

        /** @brief 아직 워커에서 돌고 있는 요청인지 */
        bool isStreaming( string_view assetPath ) const;

        /**
         * @brief **성공적으로** 끝난 요청인지 — 실패한 적이 있는 경로는 false 다.
         * @details 결과 기록에 경로가 있다는 것과 그 에셋을 읽었다는 것은 다른 말이다. 실패도
         *          기록되기 때문이다. 예전에는 이 함수가 키의 존재만 보아서, 없는 파일을 한 번
         *          요청하고 나면 그 뒤로 영원히 "로드됨" 이라고 답했다.
         */
        bool isLoaded( string_view assetPath ) const;

        size_t getPendingCount() const;

        /** @brief 끝난 요청의 수 — **실패한 것도 센다**(끝나기는 했다). */
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
        mutable mutex _mutex;
        /**
         * @brief 끝난 요청의 결과 — 경로에서 성공 여부로.
         * @warning **키가 있다는 것은 "끝났다" 이지 "읽었다" 가 아니다.** 실패도 여기 들어온다.
         *          그래서 이 표를 읽는 쪽은 **값까지** 보아야 한다. 예전 이름은 `_mapLoadedAsset`
         *          이었고, 읽는 곳 셋 중 둘이 이름을 믿고 키만 보다가 틀렸다.
         */
        unordered_map<string, bool>                                    _mapAssetResult;
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
