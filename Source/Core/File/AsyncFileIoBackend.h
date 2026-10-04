/**
 * @file AsyncFileIoBackend.h
 * @brief `AsyncFileIo` 의 안쪽 — 요청 상태 · 우선순위 큐 · 백엔드 계약입니다. 백엔드(스레드 풀 · IOCP · io_uring)와 매니저만 include 합니다.
 * @details 큐가 정책(우선순위 · 동시 진행 상한 · 취소 · 파일 열기와 버퍼 준비 · 완료 전달)을 모두 들고, 백엔드는 "꺼내서 OS 에 걸고, 끝나면 알린다"
 *          만 합니다. 그래서 백엔드를 하나 더 붙여도 정책은 그대로입니다.
 */
#pragma once
#include "Core/Common/StdHeaders.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/array.h"
#include "Core/Container/deque.h"
#include "Core/File/AsyncFileIo.h"
#include "Core/File/PlatformFileUtil.h"
#include "Core/Memory/MemoryTag.h"

#include <condition_variable>

namespace sw
{
    /**
     * @struct AsyncOpenFile
     * @brief 열린 OS 파일 하나의 공유 상태입니다(`AsyncFileHandle` 사본과 진행 중인 요청이 함께 든다). 마지막 소유자가 닫습니다.
     */
    struct AsyncOpenFile
    {
        /** @brief 이미 연 핸들을 넘겨받습니다. */
        AsyncOpenFile( NativeFileHandle handle, uint64 size, string path );
        /** @brief 핸들을 닫습니다. */
        ~AsyncOpenFile();

        /** @brief 복사를 금지합니다(핸들을 두 번 닫는다). */
        AsyncOpenFile( const AsyncOpenFile& ) = delete;
        /** @brief 복사 대입을 금지합니다. */
        AsyncOpenFile& operator=( const AsyncOpenFile& ) = delete;

        string           _path;   ///< 연 경로(로그용)
        uint64           _size;   ///< 열 때 잰 크기
        NativeFileHandle _handle; ///< OS 핸들(Windows HANDLE · POSIX fd)
        /**
         * @brief (Windows) 이 핸들을 묶은 완료 포트입니다. 핸들 하나는 포트 하나에만 묶이므로, 다른 매니저의 포트에 묶였으면 그 매니저는
         *        이 파일을 동기로 읽습니다. 묶다가 실패하면 `kUnbindablePort` 입니다.
         */
        atomic<void*> _pBoundPort;
    };
} // namespace sw

namespace sw
{
    /** @brief 요청 하나가 어디쯤 있는지입니다. */
    enum class AsyncReadState : uint8
    {
        Queued,     ///< 큐에서 기다린다(취소하면 OS 에 넘기지 않는다)
        InFlight,   ///< 백엔드가 꺼냈다(열기 · 읽기 중)
        Delivering, ///< 결과가 정해졌고 완료 콜백을 부르는 중이다
        Done,       ///< 완료 콜백까지 끝났다
    };

    /**
     * @struct AsyncReadRequest
     * @brief 읽기 요청 하나의 상태입니다. 큐 · 백엔드 · `AsyncReadHandle` 이 함께 듭니다.
     */
    struct AsyncReadRequest
    {
        /** @brief 빈 요청입니다. 매니저가 채웁니다. */
        AsyncReadRequest();

        string                    _path;       ///< 열 경로(`_pFile` 이 있으면 쓰지 않는다)
        shared_ptr<AsyncOpenFile> _pFile;      ///< 이미 연 파일. 없으면 백엔드가 `prepare` 에서 연다
        AsyncReadCompleteDelegate _onComplete; ///< 완료 콜백
        AsyncReadResult           _result;     ///< 결과(바이트 · 상태)
        AsyncFileIoQueue*         _pQueue;     ///< 기다리기에 쓰는 큐(요청이 끝나기 전에는 큐가 살아 있다 — `shutdown` 이 모두 기다린다)
        uint64                    _offset;     ///< 구간 시작
        uint64                    _size;       ///< 구간 크기(`kWholeFile` 은 `prepare` 가 실제 크기로 바꾼다)
        uint64                    _bytesDone;  ///< 백엔드가 지금까지 읽은 바이트(짧은 읽기를 이어 읽는다)
        atomic<uint8>             _state;      ///< `AsyncReadState`
        atomic<uint8>             _doneStatus; ///< 완료 콜백까지 끝난 뒤의 `AsyncIoStatus`(그 전에는 Pending)
        atomic<bool>              _bCancelRequested;
        AsyncIoPriority           _priority;
        MemoryTag                 _memoryTag; ///< 요청한 스레드의 태그 — 버퍼 할당과 완료 콜백이 이 태그로 센다
    };
} // namespace sw

namespace sw
{
    /**
     * @class IAsyncFileIoBackend
     * @brief OS 에 읽기를 거는 쪽의 계약입니다. 큐(`AsyncFileIoQueue`)에서 꺼내(`tryPop` · `waitPop`) 준비하고(`prepare`) 읽은 뒤 알립니다(`finish`).
     */
    class IAsyncFileIoBackend
    {
    public:
        virtual ~IAsyncFileIoBackend() = default;

        /** @brief IO 스레드를 띄웁니다. 이 플랫폼 · 커널에서 쓸 수 없으면 false 입니다(그때 매니저가 스레드 풀로 내려간다). */
        [[nodiscard]] virtual bool start() = 0;
        /** @brief 큐가 멈춘 뒤(`beginStop`) 불립니다. OS 에 걸린 요청이 모두 끝나면 스레드를 내립니다. */
        virtual void stop() = 0;
        /** @brief 큐에 새 요청이 들어왔거나 멈추라는 신호입니다. 아무 스레드에서나 불립니다. */
        virtual void wake() = 0;
        /** @brief 백엔드 종류입니다. */
        virtual AsyncIoBackendKind getKind() const = 0;
    };
} // namespace sw

namespace sw
{
    /**
     * @class AsyncFileIoQueue
     * @brief 우선순위 큐 · 동시 진행 상한 · 취소 · 준비 · 완료 전달입니다. 모든 백엔드가 같은 정책을 씁니다.
     */
    class AsyncFileIoQueue
    {
    public:
        /** @brief 설정의 상한 · 태스크 매니저를 받습니다. */
        explicit AsyncFileIoQueue( const AsyncFileIoSettings& settings );

        /** @brief 새 요청이 들어올 때 백엔드를 깨우는 콜백을 겁니다. */
        void setWakeDelegate( const Delegate<void()>& wakeDelegate ) { _wakeDelegate = wakeDelegate; }

        /** @brief 요청을 넣습니다. 멈추는 중이면 false 이고 요청은 그대로입니다(부르는 쪽이 `ShutDown` 으로 끝낸다). */
        [[nodiscard]] bool push( const shared_ptr<AsyncReadRequest>& pRequest );
        /** @brief 큐에 넣지 못한(시작 전 · 내리는 중 · 경로 없음) 요청을 @p status 로 바로 끝냅니다. 완료 콜백은 부른 스레드에서 돕니다. */
        static void completeDetached( const shared_ptr<AsyncReadRequest>& pRequest, AsyncIoStatus status );
        /** @brief 취소합니다(`AsyncReadHandle::cancel` 의 설명). */
        bool cancel( const shared_ptr<AsyncReadRequest>& pRequest );

        /** @brief 가장 급한 요청을 꺼냅니다. 진행 중인 수가 상한이거나 큐가 비었으면 nullptr 입니다(IOCP · io_uring). */
        shared_ptr<AsyncReadRequest> tryPop();
        /** @brief 요청이 올 때까지 기다려 꺼냅니다. 멈추는 중이고 큐가 비면 nullptr 입니다(스레드 풀). */
        shared_ptr<AsyncReadRequest> waitPop();

        /**
         * @brief 꺼낸 요청을 읽을 수 있게 합니다 — 파일을 열고(없으면), 구간을 파일 크기에 맞춰 보고, 버퍼를 요청자의 태그로 잡습니다.
         * @return 읽을 준비가 됐으면 `Pending`, 아니면 실패 결과(그대로 `finish` 에 넘긴다)
         */
        AsyncIoStatus prepare( AsyncReadRequest& request ) const;
        /**
         * @brief 백엔드가 읽기를 끝냈습니다. 진행 자리를 놓고 완료를 전달합니다(태스크 매니저가 있으면 그 워커에서).
         * @details 취소가 요청됐으면 결과를 버리고 `Canceled` 로 바꿉니다. 성공이 아니면 바이트를 비웁니다.
         */
        void finish( const shared_ptr<AsyncReadRequest>& pRequest, AsyncIoStatus status );

        /** @brief 멈춥니다 — 새 요청을 받지 않고 큐에 남은 것을 `Canceled` 로 끝낸 뒤 백엔드를 깨웁니다. */
        void beginStop();
        /** @brief 멈추는 중이면 true 입니다. */
        bool isStopping() const;
        /** @brief 백엔드가 꺼내 아직 `finish` 하지 않은 요청 수입니다. */
        uint32 getInFlightCount() const;
        /** @brief 완료 콜백까지 끝나지 않은 요청 수입니다. */
        uint32 getOutstandingCount() const;
        /** @brief 모든 요청이 끝날 때까지 기다립니다. */
        void waitIdle() const;
        /** @brief @p request 가 끝날 때까지 기다립니다. @p timeoutMs 가 0 이면 끝없이. 끝났으면 true 입니다. */
        bool waitRequest( const AsyncReadRequest& request, uint32 timeoutMs ) const;

    private:
        /** @brief 결과를 정한 요청의 완료를 전달합니다. @p bInline 이면 이 스레드에서, 아니면 태스크 매니저가 있을 때 그 워커에서. */
        void deliver( const shared_ptr<AsyncReadRequest>& pRequest, bool bInline );
        /** @brief 완료 콜백을 부르고 요청을 끝난 것으로 표시합니다. */
        void runDelivery( const shared_ptr<AsyncReadRequest>& pRequest );

        mutable mutex                                                     _mutex;
        mutable std::condition_variable_any                               _cv;
        array<deque<shared_ptr<AsyncReadRequest>>, kAsyncIoPriorityCount> _arrQueue;
        Delegate<void()>                                                  _wakeDelegate;
        TaskManager*                                                      _pTaskManager;
        uint32                                                            _maxInFlightCount;
        uint32                                                            _inFlightCount;
        uint32                                                            _outstandingCount;
        bool                                                              _bStopping;
    };
} // namespace sw

namespace sw
{
    /** @brief 백엔드를 만드는 곳입니다. 플랫폼 것(`createPlatform`)은 그 플랫폼 폴더의 `.cpp` 하나가 정의합니다. */
    struct AsyncFileIoBackendUtil
    {
        /** @brief 스레드 풀 백엔드입니다(어디서나). */
        static unique_ptr<IAsyncFileIoBackend> createThreadPool( AsyncFileIoQueue& queue, uint32 threadCount );
        /**
         * @brief 플랫폼 백엔드입니다(Windows IOCP · 리눅스 io_uring). 시작하지 않은 채 돌려줍니다.
         * @param kind `Auto` 면 그 플랫폼 것, 그 플랫폼에 없는 종류면 nullptr
         */
        static unique_ptr<IAsyncFileIoBackend> createPlatform( AsyncFileIoQueue& queue, AsyncIoBackendKind kind, uint32 maxInFlightCount );
        /** @brief 읽은 바이트 수와 OS 결과로 요청 결과를 정합니다(짧게 끝났으면 파일 끝 — `OutOfRange`). */
        static AsyncIoStatus classifyReadResult( bool bReadSucceeded, uint64 bytesDone, uint64 bytesRequested );
    };
} // namespace sw
