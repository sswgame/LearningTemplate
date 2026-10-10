#pragma once
#include "Core/Container/deque.h"
#include "Core/Delegate/Delegate.h"
#include "Core/File/AsyncFileIO.h"
#include "Core/Task/TaskFuture.h"

#include "Engine/EngineMinimal.h"

namespace sw
{
    class TaskArgs;
    /**
     * @enum StreamingPriority
     * @brief 에셋 비동기 스트리밍 우선순위입니다. 요청 태스크의 `TaskPriority` 로 옮겨 실립니다(`AssetStreamingQueue::toTaskPriority`).
     * @details Low · Normal 은 백그라운드 I/O 줄(`TaskPriority::Low` — 워커가 다른 일이 없을 때 집는다), High · Immediate 는 일반 줄
     *          (`TaskPriority::Normal`)이다. **`TaskPriority::High` 로는 싣지 않는다** — 그 줄은 렌더 기록 · 물리 몫이고 병렬 그룹의 청크 사이에서도
     *          비우므로, 거기 놓인 파일 읽기는 프레임 잡을 세운다. 이미 진행 중인 같은 경로의 요청에 붙은 요청은 그 태스크의 우선순위를 따른다.
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
     * @brief 에셋을 백그라운드에서 미리 읽는 스트리밍 큐입니다.
     * @details 씬 로드나 런타임 이동 중에 메인 스레드를 멈추지 않고 텍스처 · 오디오 · 머티리얼 파일을 비동기로 미리 읽습니다.
     *          바이트를 읽는 요청(`requestAssetData`)은 워커가 찾고(낱개 파일 존재 확인은 느리다) 비동기 IO(`ResourceUtil::readBinaryResourceAsync` → `AsyncFileIO`)로 읽어 워커를 막지 않고,
     *          팩 항목의 압축 해제 · CRC 는 완료를 받은 태스크 워커가 합니다 — 여러 요청의 해제가 나란히 돈다. 있는지만 보는 요청(`requestAsset`)은
     *          태스크 하나입니다. 완료 콜백은 `update` 를 부른 스레드에서 돕니다.
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
         *          성공했는지에 대한 기억**뿐입니다(전부 지웁니다).
         */
        void clearCompletionRecord();

        /** @brief 아직 워커에서 돌고 있는 요청인지 반환합니다. */
        bool isStreaming( string_view assetPath ) const;

        /**
         * @brief **성공적으로** 끝난 요청인지 반환합니다. 실패한 적이 있는 경로는 false 입니다.
         * @details 결과 기록에 경로가 있다는 것과 그 에셋을 읽었다는 것은 다른 말입니다. 실패도
         *          기록되기 때문입니다 — 키만 보면 없는 파일을 한 번 요청한 뒤로 영원히 "로드됨" 이라고 답합니다.
         */
        bool isLoaded( string_view assetPath ) const;

        size_t getPendingCount() const;

        /** @brief 끝난 요청의 수입니다. **실패한 것도 셉니다**(끝나기는 했습니다). */
        size_t getCompletedCount() const;

        /** @brief 스트리밍 우선순위가 싣는 태스크 우선순위입니다(`StreamingPriority` 설명의 표). */
        static TaskPriority toTaskPriority( StreamingPriority priority );
        /** @brief 스트리밍 우선순위가 싣는 IO 우선순위입니다(Immediate 는 Critical). */
        static AsyncIOPriority toIOPriority( StreamingPriority priority );

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
         * @details `requestAsset` · `requestAssetData` 가 함께 씁니다 — 폴백을 따로 들면 완료를 절반씩만 하게 됩니다(존재 콜백만 ·
         *          데이터 콜백만 비우기).
         */
        void startRequestLocked( const string& pathStr, uint64 generation, bool bFetchData, StreamingPriority priority );
        /**
         * @brief 결과를 적고 진행 표를 지운 뒤 두 콜백 목록을 완료 큐로 옮깁니다. 태스크 완료 · IO 완료 · 동기 폴백이 같은 길을 씁니다. `_mutex` 를 쥔 채 부릅니다.
         * @param bytes 데이터 콜백에 넘길 바이트. 마지막 콜백이 옮겨 가고 나머지는 사본을 받습니다.
         */
        void completeRequestLocked( const string& pathStr, bool bSuccess, vector<uint8>&& bytes );
        /** @brief 리소스를 찾아 바이트 읽기를 비동기 IO 에 겁니다(찾기 태스크 · 워커). **`_mutex` 를 쥐지 않고** 부릅니다(IO 가 내려간 뒤에는 완료가 이 스레드에서 바로 온다). */
        void issueDataRead( const string& pathStr, uint64 generation, StreamingPriority priority );
        /** @brief 비동기 IO 가 끝났습니다(태스크 워커). 세대가 맞으면 요청을 끝냅니다. */
        void onDataRead( const string& pathStr, uint64 generation, bool bSuccess, vector<uint8>& bytes );
        /** @brief 완료 항목을 `update` 가 꺼내 갈 줄에 넣습니다. `_mutex` 를 쥔 채 불러도 됩니다(다른 잠금이다). */
        void pushCompleted( CompletedItem&& item );

    private:
        mutable mutex _mutex;
        /**
         * @brief 끝난 요청의 결과입니다(경로 → 성공 여부).
         * @warning **키가 있다는 것은 "끝났다" 이지 "읽었다" 가 아닙니다.** 실패도 여기 들어옵니다.
         *          그래서 이 표를 읽는 쪽은 **값까지** 보아야 합니다.
         */
        unordered_map<string, bool> _mapAssetResult;
        unordered_set<string>       _uniqueActiveRequest;
        /**
         * @brief 진행 중인 요청 가운데 **바이트까지 읽는** 것들입니다.
         * @details `requestAsset`(있는지만 봅니다)과 `requestAssetData`(바이트를 읽습니다)가 같은
         *          `_uniqueActiveRequest` 를 함께 씁니다. 존재 확인이 진행 중일 때 들어온 데이터 요청이
         *          **그 태스크에 편승**하면, 그 태스크는 파일을 읽지 않으므로 데이터 콜백이 `bSuccess = true` 와
         *          **빈 버퍼**를 받습니다. 그래서 바이트까지 읽는 요청을 따로 셉니다.
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
        unordered_map<string, uint64> _mapRequestGeneration;
        /** @brief 진행 중인 바이트 읽기의 IO 핸들입니다(경로당 하나). 취소 · 종료가 OS 에 걸린 읽기를 멈추고 기다리는 데 씁니다. */
        unordered_map<string, AsyncReadHandle> _mapInFlightIO;
        /**
         * @brief `update` 가 부를 완료입니다. **크기 상한이 없다** — 고정 용량 큐는 같은 경로에 콜백이 몰리면(편승 1024 개 초과) 넘친 완료를
         *        말없이 버린다.
         */
        mutable mutex        _completedMutex;
        deque<CompletedItem> _listCompleted;
        bool                 _bInitialized;
    };
} // namespace sw
