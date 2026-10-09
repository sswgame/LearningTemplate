#include "pch.h"

#include "Core/Common/Defines.h"
#include "Core/Common/PlatformOsHeaders.h"
#include "Core/File/AsyncFileIoBackend.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Core/Concurrency/ThreadName.h"
    #include "Core/Log/Logger.h"
    #include "Core/Memory/Memory.h"
    #include "Core/Process/CrashHandler.h"

    #include <thread>

namespace sw
{
    namespace
    {
        SW_LOG_CALLER( "AsyncFileIo" );

        struct WindowsAsyncFileIoBackendInternal
        {
            /** @brief 큐에 새 요청이 들어왔다는 완료 포트 패킷의 키입니다(OVERLAPPED 없음). */
            static constexpr ULONG_PTR kWakeKey = 1;
            /** @brief 파일 핸들을 포트에 묶을 때의 키입니다(읽기 완료 패킷). */
            static constexpr ULONG_PTR kReadKey = 2;
            /** @brief 핸들을 포트에 묶지 못했다는 표식입니다(오버랩드로 열지 않은 핸들). 그 파일은 동기로 읽는다. */
            static inline void* const kUnbindablePort = reinterpret_cast<void*>( static_cast<uintptr_t>( 1 ) );
        };

        /** @brief OS 에 걸린 읽기 하나 — OVERLAPPED 가 맨 앞이라 완료 패킷의 포인터에서 바로 찾는다. */
        struct IocpReadOperation
        {
            OVERLAPPED                   _overlapped;
            shared_ptr<AsyncReadRequest> _pRequest;
        };
    } // namespace
} // namespace sw

namespace sw
{
    /**
     * @class IocpAsyncFileIoBackend
     * @brief Windows 오버랩드 IO + 완료 포트 백엔드입니다. IO 스레드 하나가 큐에서 꺼내 `ReadFile` 을 걸고(상한까지) 완료 포트에서 끝난 것을 받습니다.
     * @details 파일은 `FILE_FLAG_OVERLAPPED` 로 열려 있고(`PlatformFileUtil::openNativeFileForRead`) 처음 쓸 때 이 포트에 묶습니다. 같은 핸들의
     *          동기 위치 읽기(`readNativeFileAt`)는 이벤트 핸들의 낮은 비트를 세워 포트로 완료가 오지 않게 합니다.
     */
    class IocpAsyncFileIoBackend final : public IAsyncFileIoBackend
    {
    public:
        explicit IocpAsyncFileIoBackend( AsyncFileIoQueue& queue )
            : _thread{}
            , _pQueue{ &queue }
            , _hPort{ nullptr }
        {
        }

        ~IocpAsyncFileIoBackend() override
        {
            stop();
            if ( _hPort != nullptr )
                CloseHandle( _hPort );
        }

        bool start() override
        {
            _hPort = CreateIoCompletionPort( INVALID_HANDLE_VALUE, nullptr, 0, 1 );
            if ( _hPort == nullptr )
            {
                SW_LOG_ERROR( "CreateIoCompletionPort failed (error %#)", static_cast<uint32>( GetLastError() ) );
                return false;
            }
            _thread = std::thread( &IocpAsyncFileIoBackend::run, this, Memory::getCurrentMemoryTag() );
            return true;
        }

        void stop() override
        {
            if ( _thread.joinable() )
            {
                wake();
                _thread.join();
            }
        }

        void wake() override
        {
            if ( _hPort != nullptr )
                PostQueuedCompletionStatus( _hPort, 0, WindowsAsyncFileIoBackendInternal::kWakeKey, nullptr );
        }

        AsyncIoBackendKind getKind() const override { return AsyncIoBackendKind::Iocp; }

    private:
        /** @brief 꺼내 걸고 · 끝난 것을 받는 루프입니다. 큐가 멈추고 걸린 것이 없으면 끝납니다. */
        void run( MemoryTag memoryTag )
        {
            const ScopedMemoryTag threadMemoryTag{ memoryTag };
            CrashHandler::initializeCurrentThread();
            ThreadName::setCurrentThreadName( "IO.Iocp" );
            while ( true )
            {
                submitPending();
                if ( _pQueue->isStopping() && _pQueue->getInFlightCount() == 0 )
                    return;

                DWORD       transferredBytes{ 0 };
                ULONG_PTR   completionKey{ 0 };
                OVERLAPPED* pOverlapped{ nullptr };
                const BOOL  bDequeued = GetQueuedCompletionStatus( _hPort, &transferredBytes, &completionKey, &pOverlapped, INFINITE );
                if ( pOverlapped == nullptr )
                {
                    // 깨우기 패킷이거나 포트 자체의 실패다. 실패면 이 스레드가 할 수 있는 일이 없다 — 걸린 요청은 돌아오지 않는다.
                    if ( bDequeued == FALSE )
                    {
                        SW_LOG_ERROR( "GetQueuedCompletionStatus failed (error %#)", static_cast<uint32>( GetLastError() ) );
                        return;
                    }
                    continue;
                }
                const DWORD errorCode = bDequeued != FALSE ? ERROR_SUCCESS : GetLastError();
                onReadCompleted( reinterpret_cast<IocpReadOperation*>( pOverlapped ), transferredBytes, errorCode );
            }
        }

        /** @brief 상한까지 큐에서 꺼내 겁니다. */
        void submitPending()
        {
            while ( true )
            {
                shared_ptr<AsyncReadRequest> pRequest = _pQueue->tryPop();
                if ( pRequest == nullptr )
                    return;
                startRequest( pRequest );
            }
        }

        /** @brief 요청 하나를 준비해 OS 에 겁니다. 걸 수 없으면 그 자리에서 끝냅니다. */
        void startRequest( const shared_ptr<AsyncReadRequest>& pRequest )
        {
            const AsyncIoStatus prepareStatus = _pQueue->prepare( *pRequest );
            if ( prepareStatus != AsyncIoStatus::Pending )
            {
                _pQueue->finish( pRequest, prepareStatus );
                return;
            }
            if ( pRequest->_size == 0 )
            {
                _pQueue->finish( pRequest, AsyncIoStatus::Succeeded );
                return;
            }
            if ( bindToPort( *pRequest->_pFile ) == false )
            {
                // 다른 매니저의 포트에 묶인 핸들이다 — 이 스레드에서 동기로 읽는다(결과는 같고 겹치지만 않는다).
                size_t     readBytes{ 0 };
                const bool bRead = PlatformFileUtil::readNativeFileAt( pRequest->_pFile->_handle, pRequest->_offset, pRequest->_result._bytes.data(),
                                                                       static_cast<size_t>( pRequest->_size ), readBytes );
                _pQueue->finish( pRequest, AsyncFileIoBackendUtil::classifyReadResult( bRead, readBytes, pRequest->_size ) );
                return;
            }

            IocpReadOperation* pOperation = sw_new IocpReadOperation{};
            pOperation->_pRequest         = pRequest;
            issueRead( pOperation );
        }

        /** @brief 남은 구간의 다음 조각을 겁니다. 걸자마자 실패하면(완료 패킷이 오지 않는다) 그 자리에서 끝냅니다. */
        void issueRead( IocpReadOperation* pOperation )
        {
            AsyncReadRequest& request    = *pOperation->_pRequest;
            const uint64      remaining  = request._size - request._bytesDone;
            const uint64      chunkBytes = remaining < constant::kMaxFileReadChunkBytes ? remaining : constant::kMaxFileReadChunkBytes;
            const uint64      position   = request._offset + request._bytesDone;

            Memory::set( &pOperation->_overlapped, 0, sizeof( pOperation->_overlapped ) );
            pOperation->_overlapped.Offset     = static_cast<DWORD>( position & 0xFFFFFFFFull );
            pOperation->_overlapped.OffsetHigh = static_cast<DWORD>( position >> 32 );

            const HANDLE hFile = reinterpret_cast<HANDLE>( static_cast<intptr_t>( request._pFile->_handle ) );
            if ( ReadFile( hFile, request._result._bytes.data() + request._bytesDone, static_cast<DWORD>( chunkBytes ), nullptr, &pOperation->_overlapped ) != FALSE )
                return; // 바로 끝났어도 완료 패킷은 온다(FILE_SKIP_COMPLETION_PORT_ON_SUCCESS 를 켜지 않았다)

            const DWORD errorCode = GetLastError();
            if ( errorCode == ERROR_IO_PENDING )
                return;
            onReadCompleted( pOperation, 0, errorCode );
        }

        /** @brief 조각 하나가 끝났습니다. 남았으면 이어서 걸고, 다 됐거나 실패면 요청을 끝냅니다. */
        void onReadCompleted( IocpReadOperation* pOperation, DWORD transferredBytes, DWORD errorCode )
        {
            AsyncReadRequest& request = *pOperation->_pRequest;
            request._bytesDone += transferredBytes;

            const bool bMoreToRead = errorCode == ERROR_SUCCESS && transferredBytes > 0 && request._bytesDone < request._size;
            if ( bMoreToRead && request._bCancelRequested.load( std::memory_order_acquire ) == false )
            {
                issueRead( pOperation );
                return;
            }

            AsyncIoStatus status = AsyncIoStatus::ReadFailed;
            if ( errorCode == ERROR_SUCCESS || errorCode == ERROR_HANDLE_EOF )
                status = AsyncFileIoBackendUtil::classifyReadResult( true, request._bytesDone, request._size );
            else if ( errorCode == ERROR_OPERATION_ABORTED )
                status = AsyncIoStatus::Canceled;
            else
                SW_LOG_ERROR( "ReadFile failed on '%#' (error %#)", request._pFile != nullptr ? request._pFile->_path.c_str() : "", static_cast<uint32>( errorCode ) );

            const shared_ptr<AsyncReadRequest> pRequest = std::move( pOperation->_pRequest );
            sw_delete( pOperation );
            _pQueue->finish( pRequest, status );
        }

        /**
         * @brief 파일 핸들을 이 포트에 묶습니다(핸들마다 처음 한 번).
         * @return 이 포트로 완료를 받을 수 있으면 true. 다른 포트에 묶였거나 묶을 수 없는 핸들이면 false 입니다.
         */
        bool bindToPort( AsyncOpenFile& file ) const
        {
            void* pExpected{ nullptr };
            if ( file._pBoundPort.compare_exchange_strong( pExpected, _hPort, std::memory_order_acq_rel ) )
            {
                const HANDLE hFile = reinterpret_cast<HANDLE>( static_cast<intptr_t>( file._handle ) );
                if ( CreateIoCompletionPort( hFile, _hPort, WindowsAsyncFileIoBackendInternal::kReadKey, 0 ) == nullptr )
                {
                    SW_LOG_WARNING( "Cannot bind '%#' to the IO completion port (error %#) — reading it synchronously",
                                    file._path, static_cast<uint32>( GetLastError() ) );
                    file._pBoundPort.store( WindowsAsyncFileIoBackendInternal::kUnbindablePort, std::memory_order_release );
                    return false;
                }
                return true;
            }
            return pExpected == _hPort;
        }

        std::thread       _thread;
        AsyncFileIoQueue* _pQueue;
        HANDLE            _hPort;
    };
} // namespace sw

namespace sw
{
    unique_ptr<IAsyncFileIoBackend> AsyncFileIoBackendUtil::createPlatform( AsyncFileIoQueue& queue, AsyncIoBackendKind kind, uint32 maxInFlightCount )
    {
        (void)maxInFlightCount; // 상한은 큐가 지킨다(`tryPop`)
        if ( kind != AsyncIoBackendKind::Auto && kind != AsyncIoBackendKind::Iocp )
            return nullptr;
        return make_unique<IocpAsyncFileIoBackend>( queue );
    }
} // namespace sw
#endif
