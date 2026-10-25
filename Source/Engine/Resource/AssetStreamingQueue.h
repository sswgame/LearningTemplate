#pragma once
#include "Core/Container/deque.h"
#include "Core/Delegate/Delegate.h"
#include "Core/File/AsyncFileIO.h"
#include "Core/Subsystem/ISubsystem.h"
#include "Core/Task/TaskFuture.h"
#include "Core/Task/TaskHandleTracker.h"

#include "Engine/EngineMinimal.h"

namespace sw
{
    class TaskArgs;
    /**
     * @enum StreamingPriority
     * @brief 에셋 비동기 스트리밍 우선순위입니다. 요청 태스크의 `TaskPriority` 로 변환됩니다(`AssetStreamingQueue::toTaskPriority`).
     * @details Low 와 Normal 은 백그라운드 I/O 큐(`TaskPriority::Low`. 워커가 다른 일이 없을 때 가져간다)를 쓰고, High 와 Immediate 는 일반 큐
     *          (`TaskPriority::Normal`)를 쓴다. **`TaskPriority::High` 는 쓰지 않는다.** 그 큐는 렌더 기록과 물리 전용이고 병렬 그룹의 청크 사이에서도
     *          비우므로, 거기 놓인 파일 읽기는 프레임 잡을 멈춘다. 이미 진행 중인 같은 경로의 요청에 붙은 요청은 그 태스크의 우선순위를 따른다.
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
     * @details 씬 로드나 런타임 이동 중에 메인 스레드를 멈추지 않고 텍스처, 오디오, 머티리얼 파일을 비동기로 미리 읽는다.
     *          바이트를 읽는 요청(`requestAssetData`)은 워커가 찾고(낱개 파일 존재 확인은 느리다) 비동기 IO(`ResourceIO::readBinaryResourceAsync` → `AsyncFileIO`)로 읽어 워커를 막지 않고,
     *          팩 항목의 압축 해제와 CRC 는 완료를 받은 태스크 워커가 한다. 여러 요청의 해제가 나란히 돈다. 있는지만 보는 요청(`requestAsset`)은
     *          태스크 하나다. 완료 콜백은 `update` 를 부른 스레드에서 실행된다.
     */
    class SW_API AssetStreamingQueue : public ISubsystem
    {
    public:
        AssetStreamingQueue();
        ~AssetStreamingQueue() override;

        void initialize();
        void shutdown();

        /** @brief 서브시스템 이름입니다(다른 선언의 의존 칸이 이 상수를 가리킨다). */
        static constexpr const utf8* kSubsystemName = "AssetStreaming";
        /** @brief 기동 순서의 자리 — 종료가 걸린 읽기와 완료(태스크 워커)를 기다리므로 비동기 IO · 태스크가 이것보다 늦게 내려간다. */
        SubsystemInitResult     initializeSubsystem() override;
        void                    shutdownSubsystem() override { shutdown(); }
        const utf8*             getSubsystemName() const override { return kSubsystemName; }
        span<const utf8* const> getSubsystemDependencies() const override;
        MemoryTag               getSubsystemMemoryTag() const override { return MemoryTag::Asset; }
        void                    update( size_t maxCompletionsPerFrame = 32 );

        bool requestAsset( string_view assetPath, StreamingPriority priority = StreamingPriority::Normal, const OnStreamingCompleteDelegate& onComplete = {} );

        /** @brief 에셋 바이트를 백그라운드에서 읽어 완료 콜백으로 넘깁니다. */
        bool requestAssetData( string_view assetPath, StreamingPriority priority = StreamingPriority::Normal, const OnStreamingDataCompleteDelegate& onComplete = {} );

        /** @brief 에셋 프리페치를 비동기로 요청하고 완료 여부를 TaskFuture<bool> 로 반환합니다. */
        TaskFuture<bool> requestAssetFuture( string_view assetPath, StreamingPriority priority = StreamingPriority::Normal );
        void             cancelRequest( string_view assetPath );

        /**
         * @brief 끝난 요청의 결과 기록을 통째로 버립니다. 다음 요청은 디스크를 다시 봅니다.
         * @details 이 큐는 에셋 바이트를 가지고 있지 않다(데이터 요청의 바이트는 콜백으로 나가고 버려진다).
         *          그래서 버릴 "쓰지 않는 캐시" 같은 것은 없고, 여기서 사라지는 것은 **무엇이 끝났고
         *          성공했는지에 대한 기억**뿐이다(전부 지운다).
         */
        void clearCompletionRecord();

        /** @brief 아직 워커에서 돌고 있는 요청인지 반환합니다. */
        bool isStreaming( string_view assetPath ) const;

        /**
         * @brief **성공적으로** 끝난 요청인지 반환합니다. 실패한 적이 있는 경로는 false 입니다.
         * @details 결과 기록에 경로가 있다는 것과 그 에셋을 읽었다는 것은 다른 말이다. 실패도
         *          기록되기 때문이다. 키만 보면 없는 파일을 한 번 요청한 뒤로 영원히 "로드됨"이라고 답한다.
         */
        bool isLoaded( string_view assetPath ) const;

        size_t getPendingCount() const;

        /** @brief 끝난 요청의 수입니다. **실패한 것도 셉니다**(끝나기는 했습니다). */
        size_t getCompletedCount() const;

        /** @brief 스트리밍 우선순위에 대응하는 태스크 우선순위입니다(`StreamingPriority` 설명의 테이블). */
        static TaskPriority toTaskPriority( StreamingPriority priority );
        /** @brief 스트리밍 우선순위에 대응하는 IO 우선순위입니다(Immediate 는 Critical). */
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
         * @brief 요청을 태스크로 내보내거나, 엔진 서비스가 없으면(테스트, 툴) 그 자리에서 끝냅니다. `_mutex` 를 쥔 채 부릅니다.
         * @details `requestAsset` 과 `requestAssetData` 가 함께 쓴다. 폴백을 따로 두면 완료를 절반씩만 하게 된다(존재 콜백만,
         *          데이터 콜백만 비우기).
         */
        void startRequestLocked( const string& pathStr, uint64 generation, bool bFetchData, StreamingPriority priority );
        /**
         * @brief 결과를 기록하고 진행 테이블을 지운 뒤 두 콜백 목록을 완료 큐로 옮깁니다. 태스크 완료, IO 완료, 동기 폴백이 같은 경로를 씁니다. `_mutex` 를 쥔 채 부릅니다.
         * @param bytes 데이터 콜백에 넘길 바이트. 마지막 콜백이 가져가고 나머지는 사본을 받습니다.
         */
        void completeRequestLocked( const string& pathStr, bool bSuccess, vector<uint8>&& bytes );
        /** @brief 리소스를 찾아 바이트 읽기를 비동기 IO 에 요청합니다(찾기 태스크, 워커). **`_mutex` 를 쥐지 않고** 부릅니다(IO 에 제출한 뒤에는 완료가 이 스레드에서 바로 올 수 있습니다). */
        void issueDataRead( const string& pathStr, uint64 generation, StreamingPriority priority );
        /** @brief 비동기 IO 가 끝났을 때 부릅니다(태스크 워커). 세대가 맞으면 요청을 끝냅니다. */
        void onDataRead( const string& pathStr, uint64 generation, bool bSuccess, vector<uint8>& bytes );
        /** @brief 완료 항목을 `update` 가 꺼내 갈 큐에 넣습니다. `_mutex` 를 쥔 채 불러도 됩니다(다른 잠금입니다). */
        void pushCompleted( CompletedItem&& item );
        /** @brief 진행 테이블에서 뺀 읽기 핸들을 `shutdown` 이 기다리도록 보관합니다. 끝난 핸들은 이때 버립니다. `_mutex` 를 쥔 채 부릅니다. */
        void retainDetachedIOLocked( const AsyncReadHandle& handle );

    private:
        mutable mutex _mutex;
        /**
         * @brief 끝난 요청의 결과입니다(경로 → 성공 여부).
         * @warning **키가 있다는 것은 "끝났다"는 뜻이지 "읽었다"는 뜻이 아닙니다.** 실패도 여기 들어옵니다.
         *          그래서 이 테이블을 읽는 쪽은 **값까지** 보아야 합니다.
         */
        unordered_map<string, bool> _mapAssetResult;
        unordered_set<string>       _uniqueActiveRequest;
        /**
         * @brief 진행 중인 요청 가운데 **바이트까지 읽는** 것들입니다.
         * @details `requestAsset`(있는지만 봅니다)과 `requestAssetData`(바이트를 읽습니다)가 같은
         *          `_uniqueActiveRequest` 를 함께 쓴다. 존재 확인이 진행 중일 때 들어온 데이터 요청이
         *          **그 태스크에 편승**하면, 그 태스크는 파일을 읽지 않으므로 데이터 콜백이 `bSuccess = true` 와
         *          **빈 버퍼**를 받는다. 그래서 바이트까지 읽는 요청을 따로 센다.
         *
         *          진행 중인 것이 어느 쪽인지 알아야 "편승해도 되는가"를 가릴 수 있다.
         */
        unordered_set<string>                                          _uniqueActiveDataRequest;
        unordered_map<string, vector<OnStreamingCompleteDelegate>>     _mapInFlightCallback;
        unordered_map<string, vector<OnStreamingDataCompleteDelegate>> _mapInFlightDataCallback;
        /**
         * @brief 경로별 요청 세대입니다. 취소 후 즉시 재요청하면 세대가 올라갑니다.
         * @details 취소해도 이미 큐에 들어간 워커 태스크는 계속 실행된다. 세대가 없으면
         *          그 오래된 태스크가 완료되면서 새 요청의 콜백을 대신 소비해 버린다.
         */
        unordered_map<string, uint64> _mapRequestGeneration;
        /** @brief 진행 중인 바이트 읽기의 IO 핸들입니다(경로당 하나). 취소와 종료가 OS 에 걸린 읽기를 멈추고 기다리는 데 씁니다. */
        unordered_map<string, AsyncReadHandle> _mapInFlightIO;
        /**
         * @brief 진행 테이블에서 빠졌지만 아직 끝나지 않았을 수 있는 읽기입니다(취소한 것, 늦게 제출해 테이블에 기록하지 못한 것).
         * @details 그 완료 콜백도 이 큐를 부르므로 `shutdown` 이 이것까지 기다린다. 빠뜨리면 큐가 사라진 뒤 완료가 도착한다.
         */
        vector<AsyncReadHandle> _listDetachedIO;
        /** @brief 이 큐가 낸 태스크(찾기 · 존재 확인)만 든다. `shutdown` 은 엔진 전체(`TaskManager::waitAll`)가 아니라 이것만 기다린다 — 영구 태스크가 종료를 막지 않게. */
        TaskHandleTracker _taskTracker;
        /**
         * @brief `update` 가 부를 완료 목록입니다. **크기 상한이 없다.** 고정 용량 큐는 같은 경로에 콜백이 몰리면(편승 1024 개 초과) 넘친 완료를
         *        말없이 버린다.
         */
        mutable mutex        _completedMutex;
        deque<CompletedItem> _listCompleted;
        bool                 _bInitialized;
    };
} // namespace sw
