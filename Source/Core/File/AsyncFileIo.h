/**
 * @file AsyncFileIo.h
 * @brief 비동기 파일 읽기 — 요청(파일 전체 · 구간) · 우선순위 · 취소 · 완료 콜백 / `TaskFuture` 입니다.
 * @details 상용 엔진의 같은 자리는 언리얼 `IAsyncReadFileHandle` · `IAsyncReadRequest`(+ IoStore 의 우선순위 큐)와 유니티 `AsyncReadManager` 입니다.
 *          모양은 그 둘을 따릅니다.
 *          - **요청은 큐에 들어가고, 백엔드가 우선순위 순서로 꺼내 OS 에 넘깁니다.** 동시에 OS 에 걸린 요청 수에는 상한이 있어
 *            (`_maxInFlightCount`) 큐 뒤쪽의 급한 요청이 앞의 대량 요청을 기다리지 않습니다(같은 우선순위 안은 들어온 순서).
 *          - 백엔드는 셋입니다: Windows 오버랩드 IO + 완료 포트(IOCP), 리눅스 io_uring(시스템 호출 직접), 어디서나 도는 스레드 풀
 *            (위치 지정 읽기 `pread` · `ReadFile` + OVERLAPPED). `Auto` 는 플랫폼 것을 고르고, io_uring 을 쓸 수 없는 커널(WSL1 · 옛 커널 ·
 *            컨테이너에서 꺼 둔 곳)이면 스레드 풀로 내려갑니다.
 *          - 파일 열기 · 크기 확인 · 버퍼 할당은 **백엔드 스레드**가 합니다 — 요청하는 스레드(게임 · 렌더)는 큐에 넣기만 합니다.
 *          - 완료는 요청마다 한 번 `AsyncReadCompleteDelegate` 로 옵니다. `TaskManager` 를 넘겼으면 그 워커에서(우선순위를 태스크
 *            우선순위로 옮겨 싣는다 — Low · Normal 은 `TaskPriority::Low`, High · Critical 은 `Normal`. 파일 일은 렌더 · 물리 몫인 High 줄에 싣지 않는다),
 *            아니면 IO 스레드에서 부릅니다. 결과 버퍼는 콜백이 옮겨 가져도 됩니다.
 *          - 취소는 큐에 있으면 OS 에 넘기지 않고 `Canceled` 로 끝내고, 이미 OS 에 걸렸으면 읽기가 끝난 뒤 결과를 버리고 `Canceled` 로 끝냅니다
 *            (언리얼 · 유니티도 진행 중인 읽기의 취소는 "최선" 입니다).
 *          - 구간은 정확해야 합니다. 파일 끝을 넘는 구간은 짧게 읽지 않고 `OutOfRange` 로 실패합니다.
 *          - 버퍼 할당은 요청한 스레드의 메모리 태그로 셉니다(텍스처 로드가 건 읽기는 Texture 줄).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Memory/Memory.h"
#include "Core/Task/TaskFuture.h"

namespace sw
{
    struct AsyncOpenFile;
    struct AsyncReadRequest;

    class AsyncFileIoQueue;
    class IAsyncFileIoBackend;
    class TaskManager;

    /** @brief 읽기 요청의 우선순위입니다. 큐는 높은 것부터 꺼내고, 같은 우선순위 안에서는 들어온 순서를 지킵니다. */
    enum class AsyncIoPriority : uint8
    {
        Low      = 0, ///< 미리 읽기 · 백그라운드 스트리밍
        Normal   = 1, ///< 보통 에셋 로드
        High     = 2, ///< 화면에 곧 보일 것(가까운 밉 · 들리는 소리)
        Critical = 3, ///< 프레임이 기다리는 것
    };

    /** @brief 우선순위 줄 수입니다. */
    inline constexpr uint32 kAsyncIoPriorityCount = 4;

    /** @brief 읽기 요청 하나의 결과입니다. `Pending` 이 아니면 끝난 것입니다. */
    enum class AsyncIoStatus : uint8
    {
        Pending,      ///< 아직 끝나지 않았다
        Succeeded,    ///< 요청한 바이트를 모두 읽었다
        Canceled,     ///< 취소됐다(바이트 없음)
        FileNotFound, ///< 파일을 열지 못했다(없음 · 권한)
        OutOfRange,   ///< 구간이 파일 끝을 넘는다
        ReadFailed,   ///< OS 읽기가 실패했다
        ShutDown,     ///< 매니저가 시작되지 않았거나 내려가는 중이다
    };

    /** @brief 백엔드 종류입니다. */
    enum class AsyncIoBackendKind : uint8
    {
        Auto,       ///< 플랫폼 것(Windows IOCP · 리눅스 io_uring), 쓸 수 없으면 스레드 풀
        ThreadPool, ///< 스레드 몇 개가 위치 지정 읽기를 막고 기다린다 — 어디서나 돈다
        Iocp,       ///< Windows 오버랩드 IO + 완료 포트
        IoUring,    ///< 리눅스 io_uring(5.1+)
    };

    /**
     * @struct AsyncReadResult
     * @brief 읽기 요청 하나의 결과입니다. 완료 콜백이 받고, 바이트는 옮겨 가져도 됩니다.
     */
    struct AsyncReadResult
    {
        vector<uint8> _bytes;                            ///< 읽은 바이트. 성공이 아니면 비어 있다
        uint64        _offset{ 0 };                      ///< 읽은 구간의 파일 안 시작 위치
        AsyncIoStatus _status{ AsyncIoStatus::Pending }; ///< 결과

        /** @brief 요청한 바이트를 모두 읽었으면 true 입니다. */
        bool isSucceeded() const { return _status == AsyncIoStatus::Succeeded; }
    };

    /** @brief 읽기 요청이 끝났을 때 한 번 불립니다(성공 · 실패 · 취소 모두). */
    using AsyncReadCompleteDelegate = Delegate<void( AsyncReadResult& result )>;
} // namespace sw

namespace sw
{
    /**
     * @class AsyncFileHandle
     * @brief 열린 읽기 파일 하나입니다. 같은 파일의 구간을 여러 번 읽을 때(팩) 열기를 한 번으로 줄입니다(언리얼 `IAsyncReadFileHandle`).
     * @details 값 타입이고 사본이 OS 핸들을 함께 듭니다 — 마지막 사본이 사라질 때 닫힙니다. 그래서 진행 중인 읽기는 리더가 파일을 닫아도
     *          끝까지 읽습니다. `readAt` 은 위치를 지정하는 동기 읽기라 여러 스레드가 잠금 없이 동시에 불러도 됩니다(공유 파일 위치가 없다).
     */
    class SW_API AsyncFileHandle
    {
    public:
        /** @brief 열리지 않은 핸들입니다. */
        AsyncFileHandle();

        /**
         * @brief 읽기용으로 엽니다. 다른 프로세스의 읽기 · 쓰기 · 삭제를 막지 않습니다.
         * @return 열지 못했으면 `isValid() == false` 인 핸들입니다.
         */
        static AsyncFileHandle open( string_view filePath );

        /** @brief 열린 파일이면 true 입니다. */
        bool isValid() const;
        /** @brief 열 때 잰 파일 크기(바이트)입니다. 열리지 않았으면 0 입니다. */
        uint64 getSize() const;
        /** @brief 연 경로입니다. */
        const string& getPath() const;

        /**
         * @brief @p offset 에서 @p size 바이트를 @p pDst 로 읽습니다(동기, 위치 지정).
         * @return 정확히 @p size 바이트를 읽었으면 true. 파일 끝을 넘거나 OS 가 실패하면 false 입니다.
         */
        [[nodiscard]] bool readAt( uint64 offset, void* pDst, size_t size ) const;

        /** @brief 이 사본을 놓습니다. 다른 사본(진행 중인 읽기)이 있으면 파일은 그것이 놓을 때 닫힙니다. */
        void close();

        /** @brief 백엔드가 쓰는 공유 상태입니다. */
        const shared_ptr<AsyncOpenFile>& getOpenFile() const { return _pOpenFile; }

    private:
        shared_ptr<AsyncOpenFile> _pOpenFile;
    };
} // namespace sw

namespace sw
{
    /**
     * @class AsyncReadHandle
     * @brief 걸어 둔 읽기 요청 하나를 가리킵니다(언리얼 `IAsyncReadRequest` · 유니티 `ReadHandle`). 버려도 요청은 계속 돕니다.
     */
    class SW_API AsyncReadHandle
    {
    public:
        /** @brief 아무 요청도 가리키지 않습니다. */
        AsyncReadHandle();
        /** @brief 요청 하나를 가리킵니다. */
        explicit AsyncReadHandle( shared_ptr<AsyncReadRequest> pRequest );

        /** @brief 요청을 가리키면 true 입니다. */
        bool isValid() const { return _pRequest != nullptr; }
        /** @brief 완료 콜백까지 끝났으면 true 입니다. */
        bool isDone() const;
        /** @brief 지금 결과입니다. 끝나지 않았으면 `Pending` 입니다. */
        AsyncIoStatus getStatus() const;

        /**
         * @brief 취소합니다. 큐에 있으면 OS 에 넘기지 않고, 진행 중이면 읽은 뒤 결과를 버립니다. 어느 쪽이든 결과는 `Canceled` 입니다.
         * @return 이 호출로 결과가 `Canceled` 가 되면 true, 이미 끝났거나 끝나는 중이면 false 입니다.
         */
        bool cancel() const;

        /** @brief 완료 콜백까지 끝날 때까지 기다립니다. */
        void wait() const;
        /** @brief @p timeoutMs 까지 기다립니다. 끝났으면 true 입니다. */
        bool waitFor( uint32 timeoutMs ) const;

    private:
        shared_ptr<AsyncReadRequest> _pRequest;
    };
} // namespace sw

namespace sw
{
    /** @brief `AsyncFileIo::initialize` 의 설정입니다. */
    struct AsyncFileIoSettings
    {
        AsyncIoBackendKind _backendKind{ AsyncIoBackendKind::Auto }; ///< 쓸 백엔드
        uint32             _maxInFlightCount{ 32 };                  ///< OS 에 동시에 걸어 둘 요청 수(IOCP · io_uring). 큐의 우선순위는 이 너머에서 일한다
        uint32             _threadPoolThreadCount{ 2 };              ///< 스레드 풀 백엔드의 스레드 수(= 동시에 진행하는 요청 수)
        TaskManager*       _pTaskManager{ nullptr };                 ///< 있으면 완료 콜백을 그 워커에서 부른다. 없으면 IO 스레드에서
    };
} // namespace sw

namespace sw
{
    /**
     * @class AsyncFileIo
     * @brief 비동기 파일 읽기 매니저입니다. 엔진은 하나를 서비스로 들고(`engine::getAsyncFileIo()`), 시험은 따로 만들어 씁니다.
     */
    class SW_API AsyncFileIo
    {
    public:
        /** @brief `readRange` 의 크기로 주면 @p offset 부터 파일 끝까지 읽습니다. */
        static constexpr uint64 kWholeFile = ~uint64( 0 );

        /** @brief 시작하지 않은 매니저입니다. 시작 전 요청은 `ShutDown` 으로 끝납니다. */
        AsyncFileIo();
        /** @brief `shutdown` 을 부릅니다. */
        ~AsyncFileIo();

        /** @brief 복사를 금지합니다. */
        AsyncFileIo( const AsyncFileIo& ) = delete;
        /** @brief 복사 대입을 금지합니다. */
        AsyncFileIo& operator=( const AsyncFileIo& ) = delete;

        /**
         * @brief 백엔드를 골라 IO 스레드를 띄웁니다.
         * @return 시작했으면 true. 명시한 백엔드(`Iocp` · `IoUring`)를 이 플랫폼 · 커널에서 쓸 수 없으면 폴백하지 않고 false 입니다.
         */
        [[nodiscard]] bool initialize( const AsyncFileIoSettings& settings );
        /**
         * @brief 큐에 남은 요청을 `Canceled` 로 끝내고, OS 에 걸린 요청과 완료 콜백이 모두 끝나기를 기다린 뒤 IO 스레드를 내립니다.
         * @note 완료 콜백을 태스크로 싣는 설정이면 그 `TaskManager` 가 아직 돌고 있어야 합니다(엔진은 Task 단계가 FileIo 보다 늦게 내려간다).
         */
        void shutdown();

        /** @brief 시작했으면 true 입니다. */
        bool isInitialized() const;
        /** @brief 실제로 고른 백엔드입니다(`Auto` 가 무엇이 됐는지). 시작 전이면 `Auto` 입니다. */
        AsyncIoBackendKind getBackendKind() const;

        /** @brief 파일 전체를 읽습니다. */
        AsyncReadHandle readFile( string_view filePath, AsyncIoPriority priority, const AsyncReadCompleteDelegate& onComplete = {} );
        /** @brief 파일의 [@p offset, @p offset + @p size) 를 읽습니다. @p size 가 `kWholeFile` 이면 끝까지입니다. */
        AsyncReadHandle readRange( string_view filePath, uint64 offset, uint64 size, AsyncIoPriority priority, const AsyncReadCompleteDelegate& onComplete = {} );
        /** @brief 이미 연 파일의 구간을 읽습니다(팩처럼 한 파일을 여러 번 읽을 때 — 열기가 없다). */
        AsyncReadHandle readRange( const AsyncFileHandle& file, uint64 offset, uint64 size, AsyncIoPriority priority,
                                   const AsyncReadCompleteDelegate& onComplete = {} );
        /** @brief 파일 전체를 읽어 결과를 `TaskFuture` 로 돌려줍니다(`then` 으로 이어 붙인다). */
        TaskFuture<AsyncReadResult> readFileFuture( string_view filePath, AsyncIoPriority priority );

        /** @brief 아직 완료 콜백까지 끝나지 않은 요청 수입니다(큐 · 진행 · 전달 중 모두). */
        uint32 getOutstandingCount() const;
        /** @brief 걸린 요청이 모두 끝날 때까지 기다립니다. 완료 콜백을 싣는 태스크 워커 안에서 부르지 마십시오. */
        void waitIdle() const;

        /** @brief 백엔드 이름입니다(로그용). */
        static const utf8* getBackendName( AsyncIoBackendKind kind );
        /** @brief 결과 이름입니다(로그 · 시험 메시지용). */
        static const utf8* getStatusName( AsyncIoStatus status );

    private:
        /** @brief 요청을 큐에 넣고 핸들을 돌려줍니다. 시작 전 · 내리는 중이면 `ShutDown` 으로 바로 끝냅니다. */
        AsyncReadHandle submit( const shared_ptr<AsyncReadRequest>& pRequest );

        unique_ptr<AsyncFileIoQueue>    _queue;
        unique_ptr<IAsyncFileIoBackend> _backend;
    };
} // namespace sw
