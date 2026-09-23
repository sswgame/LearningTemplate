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
     * @brief 에셋 비동기 스트리밍 우선순위입니다.
     * @warning **지금은 순서에 반영되지 않습니다.** 요청은 받는 즉시 기본 우선순위의 태스크로 TaskManager 에
     *          넘어갑니다. TaskManager 에는 이제 우선순위(`TaskPriority` · High 전용 큐)가 있으므로, 이 값을
     *          거기로 옮겨 싣는 일이 남아 있습니다. 값은 부르는 쪽의 의도를 남겨 두려고 받아 둡니다.
     *          예전에는 요청마다 이 값을 구조체에 저장했지만 읽는 곳이 없었습니다.
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
     * @brief 에셋을 워커 스레드에서 미리 읽는 스트리밍 큐입니다.
     * @details 씬 로드나 런타임 이동 중에 메인 스레드를 멈추지 않고 텍스처 · 오디오 · 머티리얼 파일을 비동기로 미리 읽습니다.
     */
    class SW_API AssetStreamingQueue
    {
    public:
        AssetStreamingQueue();
        ~AssetStreamingQueue();

        void initialize();
        void shutdown();
        void update( size_t maxCompletionsPerFrame = 32 );

        bool requestAsset( string_view assetPath, StreamingPriority priority = StreamingPriority::Normal, const OnStreamingCompleteDelegate& onComplete = {} );

        /** @brief 에셋 바이트를 백그라운드에서 읽어 완료 콜백으로 넘깁니다. */
        bool requestAssetData( string_view assetPath, StreamingPriority priority = StreamingPriority::Normal, const OnStreamingDataCompleteDelegate& onComplete = {} );

        /** @brief 에셋 프리페치를 비동기로 요청하고 완료 여부를 TaskFuture<bool> 로 반환합니다. */
        TaskFuture<bool> requestAssetFuture( string_view assetPath, StreamingPriority priority = StreamingPriority::Normal );
        void             cancelRequest( string_view assetPath );

        /**
         * @brief 끝난 요청의 결과 기록을 통째로 버립니다. 다음 요청은 디스크를 다시 봅니다.
         * @details 이 큐는 에셋 바이트를 들고 있지 않습니다(데이터 요청의 바이트는 콜백으로 나가고 버려집니다).
         *          그래서 버릴 "쓰지 않는 캐시" 같은 것은 없고, 여기서 사라지는 것은 **무엇이 끝났고
         *          성공했는지에 대한 기억**뿐입니다. 예전 이름은 `sweepUnusedCache` 였는데, 쓰이지 않는
         *          것을 골라내는 일은 하지 않으면서(전부 지웁니다) 이름은 고른다고 말하고 있었습니다.
         */
        void clearCompletionRecord();

        /** @brief 아직 워커에서 돌고 있는 요청인지 반환합니다. */
        bool isStreaming( string_view assetPath ) const;

        /**
         * @brief **성공적으로** 끝난 요청인지 반환합니다. 실패한 적이 있는 경로는 false 입니다.
         * @details 결과 기록에 경로가 있다는 것과 그 에셋을 읽었다는 것은 다른 말입니다. 실패도
         *          기록되기 때문입니다. 예전에는 이 함수가 키가 있는지만 봐서, 없는 파일을 한 번
         *          요청하고 나면 그 뒤로 영원히 "로드됨" 이라고 답했습니다.
         */
        bool isLoaded( string_view assetPath ) const;

        size_t getPendingCount() const;

        /** @brief 끝난 요청의 수입니다. **실패한 것도 셉니다**(끝나기는 했습니다). */
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

        /** @brief 워커에서 파일이 있는지 확인하고 필요하면 바이트를 읽습니다. TaskArgs 는 경로 문자열, 세대, bFetchData 입니다. */
        void processAssetTask( const TaskArgs& args );
        /**
         * @brief 요청을 태스크로 내거나, 엔진 서비스가 없으면(테스트 · 툴) 그 자리에서 끝냅니다. `_mutex` 를 쥔 채 부릅니다.
         * @details `requestAsset` · `requestAssetData` 가 태스크 내기와 동기 폴백을 각자 들고 있었고, 두 폴백이 완료를 절반씩만
         *          했습니다(하나는 존재 콜백만, 하나는 데이터 콜백만 비웠습니다).
         */
        void startRequestLocked( const string& pathStr, uint64 generation, bool bFetchData );
        /** @brief 결과를 적고 진행 표를 지운 뒤 두 콜백 목록을 완료 큐로 옮깁니다. 태스크 완료와 동기 폴백이 같은 길을 씁니다. `_mutex` 를 쥔 채 부릅니다. */
        void completeRequestLocked( const string& pathStr, bool bSuccess, const vector<uint8>& bytes );

    private:
        mutable mutex _mutex;
        /**
         * @brief 끝난 요청의 결과입니다(경로 → 성공 여부).
         * @warning **키가 있다는 것은 "끝났다" 이지 "읽었다" 가 아닙니다.** 실패도 여기 들어옵니다.
         *          그래서 이 표를 읽는 쪽은 **값까지** 보아야 합니다. 예전 이름은 `_mapLoadedAsset`
         *          이었고, 읽는 곳 셋 중 둘이 이름을 믿고 키만 보다가 틀렸습니다.
         */
        unordered_map<string, bool> _mapAssetResult;
        unordered_set<string>       _uniqueActiveRequest;
        /**
         * @brief 진행 중인 요청 가운데 **바이트까지 읽는** 것들입니다.
         * @details `requestAsset`(있는지만 봅니다)과 `requestAssetData`(바이트를 읽습니다)가 같은
         *          `_uniqueActiveRequest` 를 함께 씁니다. 그래서 존재 확인이 진행 중일 때 데이터
         *          요청이 들어오면 **그 태스크에 편승**했는데, 그 태스크는 파일을 읽지 않습니다.
         *          데이터 콜백이 `bSuccess = true` 와 **빈 버퍼**를 받았습니다. 성공이라고 말하면서
         *          아무것도 주지 않는, 가장 나쁜 모양의 틀린 답입니다.
         *
         *          진행 중인 것이 어느 쪽인지 알아야 "편승해도 되는가" 를 가릴 수 있습니다.
         */
        unordered_set<string>                                          _uniqueActiveDataRequest;
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
