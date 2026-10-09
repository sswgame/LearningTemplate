#include "pch.h"

#include "Core/File/AsyncFileIo.h"

#include "Core/Concurrency/ThreadName.h"
#include "Core/File/AsyncFileIoBackend.h"
#include "Core/File/PlatformFileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/MemoryProfiler.h"
#include "Core/Process/CrashHandler.h"
#include "Core/Task/TaskManager.h"

#include <thread>

namespace sw
{
    namespace
    {
        SW_LOG_CALLER( "AsyncFileIo" );

        struct AsyncFileIoInternal
        {
            /** @brief 완료 콜백을 싣는 태스크의 우선순위입니다 — 파일 일은 렌더 · 물리 몫인 High 줄에 싣지 않는다(`AssetStreamingQueue::toTaskPriority` 와 같은 표). */
            static TaskPriority toTaskPriority( AsyncIoPriority priority )
            {
                switch ( priority )
                {
                    case AsyncIoPriority::Low:
                    case AsyncIoPriority::Normal:
                        return TaskPriority::Low;
                    case AsyncIoPriority::High:
                    case AsyncIoPriority::Critical:
                        return TaskPriority::Normal;
                }
                return TaskPriority::Low;
            }

            /** @brief 우선순위의 큐 칸 번호입니다. */
            static uint32 toQueueIndex( AsyncIoPriority priority )
            {
                const uint32 index = static_cast<uint32>( priority );
                return index < kAsyncIoPriorityCount ? index : static_cast<uint32>( AsyncIoPriority::Normal );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    AsyncOpenFile::AsyncOpenFile( NativeFileHandle handle, uint64 size, string path )
        : _path{ std::move( path ) }
        , _size{ size }
        , _handle{ handle }
        , _pBoundPort{ nullptr }
    {
    }

    AsyncOpenFile::~AsyncOpenFile()
    {
        PlatformFileUtil::closeNativeFile( _handle );
    }

    AsyncReadRequest::AsyncReadRequest()
        : _path{}
        , _pFile{}
        , _onComplete{}
        , _result{}
        , _pQueue{ nullptr }
        , _offset{ 0 }
        , _size{ 0 }
        , _bytesDone{ 0 }
        , _state{ static_cast<uint8>( AsyncReadState::Queued ) }
        , _doneStatus{ static_cast<uint8>( AsyncIoStatus::Pending ) }
        , _bCancelRequested{ false }
        , _priority{ AsyncIoPriority::Normal }
        , _memoryTag{ MemoryTag::Unknown }
    {
    }
} // namespace sw

namespace sw
{
    AsyncFileHandle::AsyncFileHandle()
        : _pOpenFile{}
    {
    }

    AsyncFileHandle AsyncFileHandle::open( string_view filePath )
    {
        AsyncFileHandle handle;
        if ( filePath.empty() )
            return handle;

        const string           pathText( filePath );
        const NativeFileHandle nativeHandle = PlatformFileUtil::openNativeFileForRead( pathText.c_str() );
        if ( nativeHandle == kInvalidNativeFileHandle )
            return handle;

        const int64 size = PlatformFileUtil::getNativeFileSize( nativeHandle );
        if ( size < 0 )
        {
            PlatformFileUtil::closeNativeFile( nativeHandle );
            return handle;
        }
        handle._pOpenFile = sw::make_shared<AsyncOpenFile>( nativeHandle, static_cast<uint64>( size ), pathText );
        return handle;
    }

    bool AsyncFileHandle::isValid() const
    {
        return _pOpenFile != nullptr;
    }

    uint64 AsyncFileHandle::getSize() const
    {
        return _pOpenFile != nullptr ? _pOpenFile->_size : 0;
    }

    const string& AsyncFileHandle::getPath() const
    {
        static const string s_emptyPath{};
        return _pOpenFile != nullptr ? _pOpenFile->_path : s_emptyPath;
    }

    bool AsyncFileHandle::readAt( uint64 offset, void* pDst, size_t size ) const
    {
        if ( _pOpenFile == nullptr )
            return false;
        if ( size == 0 )
            return true;
        if ( pDst == nullptr )
            return false;
        size_t     readBytes{ 0 };
        const bool bRead = PlatformFileUtil::readNativeFileAt( _pOpenFile->_handle, offset, pDst, size, readBytes );
        return bRead && readBytes == size;
    }

    void AsyncFileHandle::close()
    {
        _pOpenFile.reset();
    }
} // namespace sw

namespace sw
{
    AsyncReadHandle::AsyncReadHandle()
        : _pRequest{}
    {
    }

    AsyncReadHandle::AsyncReadHandle( shared_ptr<AsyncReadRequest> pRequest )
        : _pRequest{ std::move( pRequest ) }
    {
    }

    bool AsyncReadHandle::isDone() const
    {
        return _pRequest != nullptr && _pRequest->_state.load( std::memory_order_acquire ) == static_cast<uint8>( AsyncReadState::Done );
    }

    AsyncIoStatus AsyncReadHandle::getStatus() const
    {
        if ( _pRequest == nullptr )
            return AsyncIoStatus::Pending;
        return static_cast<AsyncIoStatus>( _pRequest->_doneStatus.load( std::memory_order_acquire ) );
    }

    bool AsyncReadHandle::cancel() const
    {
        if ( _pRequest == nullptr || _pRequest->_pQueue == nullptr )
            return false;
        return _pRequest->_pQueue->cancel( _pRequest );
    }

    void AsyncReadHandle::wait() const
    {
        (void)waitFor( 0 );
    }

    bool AsyncReadHandle::waitFor( uint32 timeoutMs ) const
    {
        if ( _pRequest == nullptr )
            return true;
        if ( isDone() )
            return true;
        if ( _pRequest->_pQueue == nullptr )
            return false;
        return _pRequest->_pQueue->waitRequest( *_pRequest, timeoutMs );
    }
} // namespace sw

namespace sw
{
    AsyncFileIoQueue::AsyncFileIoQueue( const AsyncFileIoSettings& settings )
        : _mutex{}
        , _cv{}
        , _arrQueue{}
        , _wakeDelegate{}
        , _pTaskManager{ settings._pTaskManager }
        , _maxInFlightCount{ settings._maxInFlightCount > 0 ? settings._maxInFlightCount : 1 }
        , _inFlightCount{ 0 }
        , _outstandingCount{ 0 }
        , _bStopping{ false }
    {
    }

    bool AsyncFileIoQueue::push( const shared_ptr<AsyncReadRequest>& pRequest )
    {
        {
            std::scoped_lock<mutex> lock{ _mutex };
            if ( _bStopping )
                return false;
            pRequest->_pQueue = this;
            pRequest->_state.store( static_cast<uint8>( AsyncReadState::Queued ), std::memory_order_release );
            _arrQueue[AsyncFileIoInternal::toQueueIndex( pRequest->_priority )].push_back( pRequest );
            ++_outstandingCount;
        }
        _cv.notify_all();
        if ( _wakeDelegate.isBound() )
            _wakeDelegate();
        return true;
    }

    void AsyncFileIoQueue::completeDetached( const shared_ptr<AsyncReadRequest>& pRequest, AsyncIoStatus status )
    {
        AsyncReadRequest& request = *pRequest;
        request._pQueue           = nullptr;
        request._result._status   = status;
        request._state.store( static_cast<uint8>( AsyncReadState::Delivering ), std::memory_order_release );
        {
            const ScopedMemoryTag memoryTag{ request._memoryTag };
            if ( request._onComplete.isBound() )
                request._onComplete( request._result );
            request._onComplete = {};
        }
        request._doneStatus.store( static_cast<uint8>( status ), std::memory_order_release );
        request._state.store( static_cast<uint8>( AsyncReadState::Done ), std::memory_order_release );
    }

    bool AsyncFileIoQueue::cancel( const shared_ptr<AsyncReadRequest>& pRequest )
    {
        bool bRemovedFromQueue{ false };
        {
            std::scoped_lock<mutex> lock{ _mutex };
            const auto              state = static_cast<AsyncReadState>( pRequest->_state.load( std::memory_order_acquire ) );
            if ( state == AsyncReadState::InFlight )
            {
                pRequest->_bCancelRequested.store( true, std::memory_order_release );
                return true;
            }
            if ( state != AsyncReadState::Queued )
                return false;

            deque<shared_ptr<AsyncReadRequest>>& queue = _arrQueue[AsyncFileIoInternal::toQueueIndex( pRequest->_priority )];
            for ( auto it = queue.begin(); it != queue.end(); ++it )
            {
                if ( it->get() == pRequest.get() )
                {
                    queue.erase( it );
                    bRemovedFromQueue = true;
                    break;
                }
            }
            if ( bRemovedFromQueue == false )
                return false;
            pRequest->_state.store( static_cast<uint8>( AsyncReadState::Delivering ), std::memory_order_release );
        }

        // OS 에 넘기기 전에 뺐다 — 결과는 취소다. 콜백은 다른 완료와 같은 길(태스크 워커)로 간다.
        pRequest->_result._status = AsyncIoStatus::Canceled;
        deliver( pRequest, false );
        return true;
    }

    shared_ptr<AsyncReadRequest> AsyncFileIoQueue::tryPop()
    {
        std::scoped_lock<mutex> lock{ _mutex };
        if ( _inFlightCount >= _maxInFlightCount )
            return nullptr;
        for ( uint32 index = kAsyncIoPriorityCount; index > 0; --index )
        {
            deque<shared_ptr<AsyncReadRequest>>& queue = _arrQueue[index - 1];
            if ( queue.empty() )
                continue;
            shared_ptr<AsyncReadRequest> pRequest = std::move( queue.front() );
            queue.pop_front();
            pRequest->_state.store( static_cast<uint8>( AsyncReadState::InFlight ), std::memory_order_release );
            ++_inFlightCount;
            return pRequest;
        }
        return nullptr;
    }

    shared_ptr<AsyncReadRequest> AsyncFileIoQueue::waitPop()
    {
        std::unique_lock<mutex> lock{ _mutex };
        while ( true )
        {
            for ( uint32 index = kAsyncIoPriorityCount; index > 0; --index )
            {
                deque<shared_ptr<AsyncReadRequest>>& queue = _arrQueue[index - 1];
                if ( queue.empty() )
                    continue;
                shared_ptr<AsyncReadRequest> pRequest = std::move( queue.front() );
                queue.pop_front();
                pRequest->_state.store( static_cast<uint8>( AsyncReadState::InFlight ), std::memory_order_release );
                ++_inFlightCount;
                return pRequest;
            }
            if ( _bStopping )
                return nullptr;
            _cv.wait( lock );
        }
    }

    AsyncIoStatus AsyncFileIoQueue::prepare( AsyncReadRequest& request ) const
    {
        if ( request._pFile == nullptr )
        {
            const AsyncFileHandle file = AsyncFileHandle::open( request._path );
            if ( file.isValid() == false )
                return AsyncIoStatus::FileNotFound;
            request._pFile = file.getOpenFile();
        }

        // 구간은 정확해야 한다. 끝을 넘는 구간을 짧게 읽어 주면 부르는 쪽이 크기를 다시 재야 하고, 팩처럼 크기를 믿는 쪽이 틀린다.
        // 뺄셈으로 잰다 — `offset + size` 는 넘칠 수 있다.
        const uint64 fileSize = request._pFile->_size;
        if ( request._offset > fileSize )
            return AsyncIoStatus::OutOfRange;
        if ( request._size == AsyncFileIo::kWholeFile )
            request._size = fileSize - request._offset;
        else if ( request._size > fileSize - request._offset )
            return AsyncIoStatus::OutOfRange;

        // 버퍼는 요청한 쪽의 용도로 센다 — IO 스레드의 태그가 아니다.
        const ScopedMemoryTag memoryTag{ request._memoryTag };
        try
        {
            request._result._bytes.resize( static_cast<size_t>( request._size ) );
        }
        catch ( const std::bad_alloc& )
        {
            SW_LOG_ERROR( "Out of memory reserving %# bytes to read '%#'", request._size, request._pFile->_path );
            return AsyncIoStatus::ReadFailed;
        }
        request._result._offset = request._offset;
        request._bytesDone      = 0;
        return AsyncIoStatus::Pending;
    }

    void AsyncFileIoQueue::finish( const shared_ptr<AsyncReadRequest>& pRequest, AsyncIoStatus status )
    {
        AsyncReadRequest& request = *pRequest;
        // 취소 표시는 상태를 바꾸는 잠금 **안에서** 읽는다. 밖에서 읽으면 그 사이에 `cancel` 이 "진행 중" 을 보고 true 를 돌려준 요청이
        // 성공으로 끝난다.
        bool bCanceled{ false };
        {
            std::scoped_lock<mutex> lock{ _mutex };
            if ( _inFlightCount > 0 )
                --_inFlightCount;
            bCanceled = request._bCancelRequested.load( std::memory_order_acquire );
            request._state.store( static_cast<uint8>( AsyncReadState::Delivering ), std::memory_order_release );
        }
        if ( bCanceled )
            status = AsyncIoStatus::Canceled;
        if ( status != AsyncIoStatus::Succeeded )
            vector<uint8>{}.swap( request._result._bytes );
        request._result._status = status;
        // 다 쓴 파일은 여기서 놓는다 — 마지막 소유자면 콜백보다 먼저 닫혀, 콜백이 같은 파일을 다시 쓰거나 지워도 막지 않는다.
        request._pFile.reset();
        deliver( pRequest, false );
    }

    void AsyncFileIoQueue::deliver( const shared_ptr<AsyncReadRequest>& pRequest, bool bInline )
    {
        if ( bInline == false && _pTaskManager != nullptr )
        {
            // 캡처가 포인터 둘(24 바이트 안)이라 델리게이트가 힙을 쓰지 않는다.
            TaskHandle handle = _pTaskManager->emplaceTask(
                "AsyncIoComplete",
                SW_DELEGATE_LAMBDA( TaskDelegate, [this, pRequest]()
            {
                runDelivery( pRequest );
            } ) );
            handle.setPriority( AsyncFileIoInternal::toTaskPriority( pRequest->_priority ) );
            handle.submit();
            return;
        }
        runDelivery( pRequest );
    }

    void AsyncFileIoQueue::runDelivery( const shared_ptr<AsyncReadRequest>& pRequest )
    {
        AsyncReadRequest& request = *pRequest;
        {
            const ScopedMemoryTag memoryTag{ request._memoryTag };
            if ( request._onComplete.isBound() )
                request._onComplete( request._result );
            // 콜백이 든 것(캡처한 리더 · 약속)을 요청보다 먼저 놓는다 — 핸들을 든 쪽이 오래 쥐어도 그것까지 붙들지 않는다.
            request._onComplete = {};
        }
        request._doneStatus.store( static_cast<uint8>( request._result._status ), std::memory_order_release );
        {
            std::scoped_lock<mutex> lock{ _mutex };
            request._state.store( static_cast<uint8>( AsyncReadState::Done ), std::memory_order_release );
            if ( _outstandingCount > 0 )
                --_outstandingCount;
        }
        _cv.notify_all();
    }

    void AsyncFileIoQueue::beginStop()
    {
        vector<shared_ptr<AsyncReadRequest>> listCanceled;
        {
            std::scoped_lock<mutex> lock{ _mutex };
            _bStopping = true;
            for ( deque<shared_ptr<AsyncReadRequest>>& queue : _arrQueue )
            {
                for ( shared_ptr<AsyncReadRequest>& pRequest : queue )
                {
                    pRequest->_state.store( static_cast<uint8>( AsyncReadState::Delivering ), std::memory_order_release );
                    listCanceled.push_back( std::move( pRequest ) );
                }
                queue.clear();
            }
        }
        _cv.notify_all();
        for ( const shared_ptr<AsyncReadRequest>& pRequest : listCanceled )
        {
            pRequest->_result._status = AsyncIoStatus::Canceled;
            deliver( pRequest, false );
        }
        if ( _wakeDelegate.isBound() )
            _wakeDelegate();
    }

    bool AsyncFileIoQueue::isStopping() const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        return _bStopping;
    }

    uint32 AsyncFileIoQueue::getInFlightCount() const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        return _inFlightCount;
    }

    uint32 AsyncFileIoQueue::getOutstandingCount() const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        return _outstandingCount;
    }

    void AsyncFileIoQueue::waitIdle() const
    {
        std::unique_lock<mutex> lock{ _mutex };
        _cv.wait( lock, [this]()
        { return _outstandingCount == 0; } );
    }

    bool AsyncFileIoQueue::waitRequest( const AsyncReadRequest& request, uint32 timeoutMs ) const
    {
        std::unique_lock<mutex> lock{ _mutex };
        const auto              isDone = [&request]()
        {
            return request._state.load( std::memory_order_acquire ) == static_cast<uint8>( AsyncReadState::Done );
        };
        if ( timeoutMs == 0 )
        {
            _cv.wait( lock, isDone );
            return true;
        }
        return _cv.wait_for( lock, std::chrono::milliseconds( timeoutMs ), isDone );
    }
} // namespace sw

namespace sw
{
    /**
     * @class ThreadPoolAsyncFileIoBackend
     * @brief 스레드 몇 개가 큐에서 꺼내 위치 지정 읽기(`readNativeFileAt`)를 막고 기다리는 백엔드입니다. 어느 플랫폼에서나 돌고, io_uring 이
     *        없는 리눅스의 폴백입니다. 동시에 진행하는 요청 수 = 스레드 수입니다.
     */
    class ThreadPoolAsyncFileIoBackend final : public IAsyncFileIoBackend
    {
    public:
        ThreadPoolAsyncFileIoBackend( AsyncFileIoQueue& queue, uint32 threadCount )
            : _listThread{}
            , _pQueue{ &queue }
            , _threadCount{ threadCount > 0 ? threadCount : 1 }
        {
        }

        ~ThreadPoolAsyncFileIoBackend() override { stop(); }

        bool start() override
        {
            const MemoryTag memoryTag = MemoryProfiler::getCurrentMemoryTag();
            _listThread.reserve( _threadCount );
            for ( uint32 index = 0; index < _threadCount; ++index )
            {
                _listThread.emplace_back( &ThreadPoolAsyncFileIoBackend::runWorker, this, memoryTag );
            }
            return true;
        }

        void stop() override
        {
            for ( std::thread& thread : _listThread )
            {
                if ( thread.joinable() )
                    thread.join();
            }
            _listThread.clear();
        }

        void wake() override {} // 워커는 큐의 조건 변수에서 깬다

        AsyncIoBackendKind getKind() const override { return AsyncIoBackendKind::ThreadPool; }

    private:
        /** @brief 큐가 멈출 때까지 꺼내 읽습니다. */
        void runWorker( MemoryTag memoryTag )
        {
            const ScopedMemoryTag threadMemoryTag{ memoryTag };
            CrashHandler::initializeCurrentThread();
            ThreadName::setCurrentThreadName( "IO" );
            while ( true )
            {
                const shared_ptr<AsyncReadRequest> pRequest = _pQueue->waitPop();
                if ( pRequest == nullptr )
                    return;
                AsyncIoStatus status = _pQueue->prepare( *pRequest );
                if ( status == AsyncIoStatus::Pending )
                {
                    size_t     readBytes{ 0 };
                    const bool bRead = pRequest->_size == 0 ||
                                       PlatformFileUtil::readNativeFileAt( pRequest->_pFile->_handle, pRequest->_offset, pRequest->_result._bytes.data(),
                                                                           static_cast<size_t>( pRequest->_size ), readBytes );
                    status = AsyncFileIoBackendUtil::classifyReadResult( bRead, pRequest->_size == 0 ? 0 : readBytes, pRequest->_size );
                }
                _pQueue->finish( pRequest, status );
            }
        }

        vector<std::thread> _listThread;
        AsyncFileIoQueue*   _pQueue;
        uint32              _threadCount;
    };
} // namespace sw

namespace sw
{
    unique_ptr<IAsyncFileIoBackend> AsyncFileIoBackendUtil::createThreadPool( AsyncFileIoQueue& queue, uint32 threadCount )
    {
        return make_unique<ThreadPoolAsyncFileIoBackend>( queue, threadCount );
    }

    AsyncIoStatus AsyncFileIoBackendUtil::classifyReadResult( bool bReadSucceeded, uint64 bytesDone, uint64 bytesRequested )
    {
        if ( bReadSucceeded == false )
            return AsyncIoStatus::ReadFailed;
        // 열 때 잰 크기 안의 구간인데 짧게 끝났다 — 그 사이 파일이 줄었다.
        return bytesDone == bytesRequested ? AsyncIoStatus::Succeeded : AsyncIoStatus::OutOfRange;
    }
} // namespace sw

namespace sw
{
    AsyncFileIo::AsyncFileIo()
        : _queue{}
        , _backend{}
    {
    }

    AsyncFileIo::~AsyncFileIo()
    {
        shutdown();
    }

    bool AsyncFileIo::initialize( const AsyncFileIoSettings& settings )
    {
        if ( _backend != nullptr )
            return true;

        _queue = make_unique<AsyncFileIoQueue>( settings );

        unique_ptr<IAsyncFileIoBackend> backend;
        if ( settings._backendKind != AsyncIoBackendKind::ThreadPool )
        {
            backend = AsyncFileIoBackendUtil::createPlatform( *_queue, settings._backendKind, settings._maxInFlightCount );
            if ( backend != nullptr && backend->start() == false )
                backend.reset();
            if ( backend == nullptr && settings._backendKind != AsyncIoBackendKind::Auto )
            {
                SW_LOG_ERROR( "Async file IO backend %# is not available on this platform", getBackendName( settings._backendKind ) );
                _queue.reset();
                return false;
            }
        }
        if ( backend == nullptr )
        {
            backend = AsyncFileIoBackendUtil::createThreadPool( *_queue, settings._threadPoolThreadCount );
            if ( backend->start() == false )
            {
                _queue.reset();
                return false;
            }
        }

        IAsyncFileIoBackend* pBackend = backend.get();
        _queue->setWakeDelegate( SW_DELEGATE_LAMBDA( Delegate<void()>, [pBackend]()
        {
            pBackend->wake();
        } ) );
        _backend = std::move( backend );
        SW_LOG_INFO( "Async file IO started (backend %#)", getBackendName( _backend->getKind() ) );
        return true;
    }

    void AsyncFileIo::shutdown()
    {
        if ( _backend == nullptr )
            return;
        // 순서: 새 요청을 막고 큐를 비운다 → OS 에 걸린 것이 끝나면 IO 스레드가 내려간다 → 태스크로 실은 완료 콜백까지 기다린다.
        _queue->beginStop();
        _backend->stop();
        _queue->waitIdle();
        _backend.reset();
        _queue.reset();
    }

    bool AsyncFileIo::isInitialized() const
    {
        return _backend != nullptr;
    }

    AsyncIoBackendKind AsyncFileIo::getBackendKind() const
    {
        return _backend != nullptr ? _backend->getKind() : AsyncIoBackendKind::Auto;
    }

    AsyncReadHandle AsyncFileIo::readFile( string_view filePath, AsyncIoPriority priority, const AsyncReadCompleteDelegate& onComplete )
    {
        return readRange( filePath, 0, kWholeFile, priority, onComplete );
    }

    AsyncReadHandle AsyncFileIo::readRange( string_view filePath, uint64 offset, uint64 size, AsyncIoPriority priority, const AsyncReadCompleteDelegate& onComplete )
    {
        shared_ptr<AsyncReadRequest> pRequest = sw::make_shared<AsyncReadRequest>();
        pRequest->_path                       = string( filePath );
        pRequest->_offset                     = offset;
        pRequest->_size                       = size;
        pRequest->_priority                   = priority;
        pRequest->_memoryTag                  = MemoryProfiler::getCurrentMemoryTag();
        pRequest->_onComplete                 = onComplete;
        if ( filePath.empty() )
        {
            AsyncFileIoQueue::completeDetached( pRequest, AsyncIoStatus::FileNotFound );
            return AsyncReadHandle( pRequest );
        }
        return submit( pRequest );
    }

    AsyncReadHandle AsyncFileIo::readRange( const AsyncFileHandle& file, uint64 offset, uint64 size, AsyncIoPriority priority,
                                            const AsyncReadCompleteDelegate& onComplete )
    {
        shared_ptr<AsyncReadRequest> pRequest = sw::make_shared<AsyncReadRequest>();
        pRequest->_pFile                      = file.getOpenFile();
        pRequest->_offset                     = offset;
        pRequest->_size                       = size;
        pRequest->_priority                   = priority;
        pRequest->_memoryTag                  = MemoryProfiler::getCurrentMemoryTag();
        pRequest->_onComplete                 = onComplete;
        if ( pRequest->_pFile == nullptr )
        {
            AsyncFileIoQueue::completeDetached( pRequest, AsyncIoStatus::FileNotFound );
            return AsyncReadHandle( pRequest );
        }
        return submit( pRequest );
    }

    TaskFuture<AsyncReadResult> AsyncFileIo::readFileFuture( string_view filePath, AsyncIoPriority priority )
    {
        shared_ptr<TaskPromise<AsyncReadResult>> pPromise = sw::make_shared<TaskPromise<AsyncReadResult>>();
        TaskFuture<AsyncReadResult>              future   = pPromise->getFuture();
        // 핸들은 쓰지 않는다 — 실패도 완료 콜백(상태)으로 퓨처에 온다
        (void)readFile( filePath, priority, SW_DELEGATE_LAMBDA( AsyncReadCompleteDelegate, [pPromise]( AsyncReadResult& result )
        {
            pPromise->setValue( std::move( result ) );
        } ) );
        return future;
    }

    uint32 AsyncFileIo::getOutstandingCount() const
    {
        return _queue != nullptr ? _queue->getOutstandingCount() : 0;
    }

    void AsyncFileIo::waitIdle() const
    {
        if ( _queue != nullptr )
            _queue->waitIdle();
    }

    AsyncReadHandle AsyncFileIo::submit( const shared_ptr<AsyncReadRequest>& pRequest )
    {
        if ( _queue == nullptr )
        {
            // 시작 전(또는 내린 뒤)이다. 조용히 버리지 않고 결과로 알린다.
            AsyncFileIoQueue::completeDetached( pRequest, AsyncIoStatus::ShutDown );
            return AsyncReadHandle( pRequest );
        }
        if ( _queue->push( pRequest ) == false )
            AsyncFileIoQueue::completeDetached( pRequest, AsyncIoStatus::ShutDown );
        return AsyncReadHandle( pRequest );
    }

    const utf8* AsyncFileIo::getBackendName( AsyncIoBackendKind kind )
    {
        switch ( kind )
        {
            case AsyncIoBackendKind::Auto:
                return "Auto";
            case AsyncIoBackendKind::ThreadPool:
                return "ThreadPool";
            case AsyncIoBackendKind::Iocp:
                return "Iocp";
            case AsyncIoBackendKind::IoUring:
                return "IoUring";
        }
        return "Unknown";
    }

    const utf8* AsyncFileIo::getStatusName( AsyncIoStatus status )
    {
        switch ( status )
        {
            case AsyncIoStatus::Pending:
                return "Pending";
            case AsyncIoStatus::Succeeded:
                return "Succeeded";
            case AsyncIoStatus::Canceled:
                return "Canceled";
            case AsyncIoStatus::FileNotFound:
                return "FileNotFound";
            case AsyncIoStatus::OutOfRange:
                return "OutOfRange";
            case AsyncIoStatus::ReadFailed:
                return "ReadFailed";
            case AsyncIoStatus::ShutDown:
                return "ShutDown";
        }
        return "Unknown";
    }
} // namespace sw
