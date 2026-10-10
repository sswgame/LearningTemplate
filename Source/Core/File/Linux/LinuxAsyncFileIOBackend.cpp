#include "pch.h"

#include "Core/Common/Defines.h"
#include "Core/Common/PlatformOsHeaders.h"
#include "Core/File/AsyncFileIOBackend.h"

#if defined( SW_PLATFORM_LINUX )
    #include "Core/Concurrency/ThreadName.h"
    #include "Core/Log/Logger.h"
    #include "Core/Memory/Memory.h"
    #include "Core/Process/ThreadCrashStack.h"

    #include <cerrno>
    #include <sys/syscall.h>
    #include <sys/uio.h>
    #include <thread>

    // io_uring 은 liburing 없이 시스템 호출로 직접 쓴다(vcpkg 의존을 늘리지 않는다). 커널 헤더가 없거나 시스템 호출 번호가 없는 툴체인은
    // 스레드 풀만 쓴다.
    #if __has_include( <linux/io_uring.h> )
        #include <linux/io_uring.h>
        #if defined( __NR_io_uring_setup ) && defined( __NR_io_uring_enter )
            #define SW_HAS_IO_URING 1
        #endif
    #endif

    #if defined( SW_HAS_IO_URING )
namespace sw
{
    namespace
    {
        SW_LOG_CALLER( "AsyncFileIO" );

        struct LinuxAsyncFileIOBackendInternal
        {
            /** @brief 깨우기용 eventfd 읽기의 user_data 입니다(읽기 연산의 포인터와 겹치지 않는 값). */
            static constexpr uint64 kWakeUserData = 1;

            static int32 setupRing( uint32 entryCount, io_uring_params& params )
            {
                return static_cast<int32>( syscall( __NR_io_uring_setup, entryCount, &params ) );
            }

            static int32 enterRing( int32 ringFd, uint32 submitCount, uint32 minCompleteCount, uint32 flags )
            {
                return static_cast<int32>( syscall( __NR_io_uring_enter, ringFd, submitCount, minCompleteCount, flags, nullptr, 0 ) );
            }

            /** @brief @p value 이상인 가장 작은 2 의 거듭제곱입니다. */
            static uint32 roundUpToPowerOfTwo( uint32 value )
            {
                uint32 result = 1;
                while ( result < value )
                {
                    result <<= 1;
                }
                return result;
            }
        };

        /** @brief 커널에 걸린 읽기 하나입니다. iovec 은 SQE 가 커널로 넘어갈 때까지 살아 있어야 하므로 연산에 둔다. */
        struct IOUringReadOperation
        {
            iovec                        _iov;
            shared_ptr<AsyncReadRequest> _pRequest;
        };
    } // namespace
} // namespace sw

namespace sw
{
    /**
     * @class IOUringAsyncFileIOBackend
     * @brief 리눅스 io_uring 백엔드입니다(시스템 호출 직접 — liburing 없음). IO 스레드 하나가 SQE 를 채워 제출하고 CQE 를 거둡니다.
     * @details 다른 스레드의 새 요청은 eventfd 에 쓰고, 링에 걸어 둔 eventfd 읽기가 끝나 IO 스레드가 깹니다(링은 IO 스레드만 만진다 — 잠금 없음).
     *          읽기는 `IORING_OP_READV`(5.1+)로 걸어 오래된 커널에서도 돈다. 짧게 끝난 읽기는 남은 구간을 다시 건다.
     *          `io_uring_setup` 이 실패하면(ENOSYS — 옛 커널 · WSL1, EPERM — 컨테이너 seccomp · `kernel.io_uring_disabled`) `start` 가 false 를
     *          돌려 매니저가 스레드 풀로 내려간다.
     */
    class IOUringAsyncFileIOBackend final : public IAsyncFileIOBackend
    {
    public:
        IOUringAsyncFileIOBackend( AsyncFileIOQueue& queue, uint32 maxInFlightCount )
            : _thread{}
            , _pQueue{ &queue }
            , _pSqRing{ nullptr }
            , _pCqRing{ nullptr }
            , _pSqeArray{ nullptr }
            , _pSqHead{ nullptr }
            , _pSqTail{ nullptr }
            , _pSqIndexArray{ nullptr }
            , _pCqHead{ nullptr }
            , _pCqTail{ nullptr }
            , _pCqeArray{ nullptr }
            , _sqRingBytes{ 0 }
            , _cqRingBytes{ 0 }
            , _sqeArrayBytes{ 0 }
            , _wakeIov{}
            , _wakeValue{ 0 }
            , _sqMask{ 0 }
            , _cqMask{ 0 }
            , _sqEntryCount{ 0 }
            , _pendingSubmitCount{ 0 }
            , _maxInFlightCount{ maxInFlightCount > 0 ? maxInFlightCount : 1 }
            , _ringFd{ -1 }
            , _eventFd{ -1 }
        {
        }

        ~IOUringAsyncFileIOBackend() override
        {
            stop();
            releaseRing();
        }

        bool start() override
        {
            // 걸린 읽기(상한) + 깨우기 읽기 하나 + 여유. 상한을 큐가 지키므로 SQ 가 넘치지 않는다.
            const uint32    entryCount = LinuxAsyncFileIOBackendInternal::roundUpToPowerOfTwo( _maxInFlightCount + 2 );
            io_uring_params params{};
            _ringFd = LinuxAsyncFileIOBackendInternal::setupRing( entryCount, params );
            if ( _ringFd < 0 )
            {
                SW_LOG_INFO( "io_uring is not available (errno %#) — using the thread-pool backend", errno );
                _ringFd = -1;
                return false;
            }

            _sqRingBytes   = params.sq_off.array + params.sq_entries * sizeof( uint32 );
            _cqRingBytes   = params.cq_off.cqes + params.cq_entries * sizeof( io_uring_cqe );
            _sqeArrayBytes = params.sq_entries * sizeof( io_uring_sqe );
        #if defined( IORING_FEAT_SINGLE_MMAP )
            const bool bSingleMap = ( params.features & IORING_FEAT_SINGLE_MMAP ) != 0;
        #else
            const bool bSingleMap = false;
        #endif
            if ( bSingleMap )
            {
                _sqRingBytes = _sqRingBytes > _cqRingBytes ? _sqRingBytes : _cqRingBytes;
                _cqRingBytes = _sqRingBytes;
            }

            _pSqRing = mmap( nullptr, _sqRingBytes, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, _ringFd, IORING_OFF_SQ_RING );
            if ( _pSqRing == MAP_FAILED )
            {
                _pSqRing = nullptr;
                SW_LOG_WARNING( "io_uring ring mmap failed (errno %#) — using the thread-pool backend", errno );
                releaseRing();
                return false;
            }
            if ( bSingleMap )
            {
                _pCqRing = _pSqRing;
            }
            else
            {
                _pCqRing = mmap( nullptr, _cqRingBytes, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, _ringFd, IORING_OFF_CQ_RING );
                if ( _pCqRing == MAP_FAILED )
                {
                    _pCqRing = nullptr;
                    SW_LOG_WARNING( "io_uring completion ring mmap failed (errno %#) — using the thread-pool backend", errno );
                    releaseRing();
                    return false;
                }
            }
            void* pSqeMemory = mmap( nullptr, _sqeArrayBytes, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, _ringFd, IORING_OFF_SQES );
            if ( pSqeMemory == MAP_FAILED )
            {
                SW_LOG_WARNING( "io_uring SQE mmap failed (errno %#) — using the thread-pool backend", errno );
                releaseRing();
                return false;
            }
            _pSqeArray = static_cast<io_uring_sqe*>( pSqeMemory );

            uint8* pSq     = static_cast<uint8*>( _pSqRing );
            uint8* pCq     = static_cast<uint8*>( _pCqRing );
            _pSqHead       = reinterpret_cast<uint32*>( pSq + params.sq_off.head );
            _pSqTail       = reinterpret_cast<uint32*>( pSq + params.sq_off.tail );
            _sqMask        = *reinterpret_cast<uint32*>( pSq + params.sq_off.ring_mask );
            _pSqIndexArray = reinterpret_cast<uint32*>( pSq + params.sq_off.array );
            _sqEntryCount  = params.sq_entries;
            _pCqHead       = reinterpret_cast<uint32*>( pCq + params.cq_off.head );
            _pCqTail       = reinterpret_cast<uint32*>( pCq + params.cq_off.tail );
            _cqMask        = *reinterpret_cast<uint32*>( pCq + params.cq_off.ring_mask );
            _pCqeArray     = reinterpret_cast<io_uring_cqe*>( pCq + params.cq_off.cqes );

            _eventFd = eventfd( 0, EFD_CLOEXEC );
            if ( _eventFd < 0 )
            {
                SW_LOG_WARNING( "eventfd failed (errno %#) — using the thread-pool backend", errno );
                releaseRing();
                return false;
            }

            _thread = std::thread( &IOUringAsyncFileIOBackend::run, this, Memory::getCurrentMemoryTag() );
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
            if ( _eventFd < 0 )
                return;
            const uint64 one = 1;
            // eventfd 쓰기는 8 바이트 하나라 짧게 끝나지 않는다. 실패(EAGAIN — 카운터가 넘침)는 이미 깨울 값이 쌓였다는 뜻이다.
            (void)::write( _eventFd, &one, sizeof( one ) );
        }

        AsyncIOBackendKind getKind() const override { return AsyncIOBackendKind::IOUring; }

    private:
        /** @brief 채우고 · 제출하고 · 거두는 루프입니다. 큐가 멈추고 걸린 것이 없으면 끝납니다. */
        void run( MemoryTag memoryTag )
        {
            const ScopedMemoryTag threadMemoryTag{ memoryTag };
            ThreadCrashStack::initializeCurrentThread();
            ThreadName::setCurrentThreadName( "IO.Uring" );
            armWakeRead();
            while ( true )
            {
                submitPending();
                if ( _pQueue->isStopping() && _pQueue->getInFlightCount() == 0 )
                    return;

                const uint32 submitCount = _pendingSubmitCount;
                const int32  result      = LinuxAsyncFileIOBackendInternal::enterRing( _ringFd, submitCount, 1, IORING_ENTER_GETEVENTS );
                if ( result < 0 )
                {
                    if ( errno == EINTR || errno == EAGAIN || errno == EBUSY )
                        continue;
                    SW_LOG_ERROR( "io_uring_enter failed (errno %#)", errno );
                    return;
                }
                // 커널이 받은 SQE 만큼 줄인다(EINTR 로 일부만 받았으면 남은 것은 다음 enter 가 낸다).
                _pendingSubmitCount -= static_cast<uint32>( result ) < submitCount ? static_cast<uint32>( result ) : submitCount;
                reapCompletions();
            }
        }

        /** @brief 상한까지 큐에서 꺼내 SQE 를 채웁니다. */
        void submitPending()
        {
            while ( true )
            {
                shared_ptr<AsyncReadRequest> pRequest = _pQueue->tryPop();
                if ( pRequest == nullptr )
                    return;

                const AsyncIOStatus prepareStatus = _pQueue->prepare( *pRequest );
                if ( prepareStatus != AsyncIOStatus::Pending )
                {
                    _pQueue->finish( pRequest, prepareStatus );
                    continue;
                }
                if ( pRequest->_size == 0 )
                {
                    _pQueue->finish( pRequest, AsyncIOStatus::Succeeded );
                    continue;
                }
                IOUringReadOperation* pOperation = sw_new IOUringReadOperation{};
                pOperation->_pRequest            = std::move( pRequest );
                pushReadSqe( pOperation );
            }
        }

        /** @brief 남은 구간의 다음 조각을 SQE 로 채웁니다(제출은 다음 enter). */
        void pushReadSqe( IOUringReadOperation* pOperation )
        {
            AsyncReadRequest& request    = *pOperation->_pRequest;
            const uint64      remaining  = request._size - request._bytesDone;
            const uint64      chunkBytes = remaining < constant::kMaxFileReadChunkBytes ? remaining : constant::kMaxFileReadChunkBytes;
            pOperation->_iov.iov_base    = request._result._bytes.data() + request._bytesDone;
            pOperation->_iov.iov_len     = static_cast<size_t>( chunkBytes );

            io_uring_sqe* pSqe = acquireSqe();
            pSqe->opcode       = IORING_OP_READV;
            pSqe->fd           = static_cast<int32>( request._pFile->_handle );
            pSqe->off          = request._offset + request._bytesDone;
            pSqe->addr         = reinterpret_cast<uint64>( &pOperation->_iov );
            pSqe->len          = 1;
            pSqe->user_data    = reinterpret_cast<uint64>( pOperation );
            commitSqe();
        }

        /** @brief eventfd 8 바이트 읽기를 걸어 둡니다. 다른 스레드가 `wake` 하면 이것이 끝나 enter 가 돌아온다. */
        void armWakeRead()
        {
            io_uring_sqe* pSqe = acquireSqe();
            pSqe->opcode       = IORING_OP_READV;
            pSqe->fd           = _eventFd;
            pSqe->off          = 0;
            _wakeIov.iov_base  = &_wakeValue;
            _wakeIov.iov_len   = sizeof( _wakeValue );
            pSqe->addr         = reinterpret_cast<uint64>( &_wakeIov );
            pSqe->len          = 1;
            pSqe->user_data    = LinuxAsyncFileIOBackendInternal::kWakeUserData;
            commitSqe();
        }

        /**
         * @brief 다음 빈 SQE 를 0 으로 비워 돌려줍니다(링은 이 스레드만 쓴다).
         * @details 큐가 걸린 수를 상한으로 묶어 SQ 는 차지 않는다. 그래도 찼으면(짧은 읽기의 이어 읽기가 몰린 경우) 쌓인 것을 먼저 제출해 자리를 낸다.
         */
        io_uring_sqe* acquireSqe()
        {
            while ( *_pSqTail - __atomic_load_n( _pSqHead, __ATOMIC_ACQUIRE ) >= _sqEntryCount )
            {
                const int32 result = LinuxAsyncFileIOBackendInternal::enterRing( _ringFd, _pendingSubmitCount, 0, 0 );
                if ( result > 0 )
                    _pendingSubmitCount -= static_cast<uint32>( result ) < _pendingSubmitCount ? static_cast<uint32>( result ) : _pendingSubmitCount;
                else if ( result < 0 && errno != EINTR && errno != EAGAIN && errno != EBUSY )
                    break;
            }
            const uint32  tail = *_pSqTail;
            io_uring_sqe* pSqe = &_pSqeArray[tail & _sqMask];
            Memory::set( pSqe, 0, sizeof( io_uring_sqe ) );
            return pSqe;
        }

        /** @brief 채운 SQE 를 꼬리에 올립니다 — 커널이 보도록 꼬리는 release 로 씁니다. */
        void commitSqe()
        {
            const uint32 tail              = *_pSqTail;
            _pSqIndexArray[tail & _sqMask] = tail & _sqMask;
            __atomic_store_n( _pSqTail, tail + 1, __ATOMIC_RELEASE );
            ++_pendingSubmitCount;
        }

        /** @brief 끝난 CQE 를 모두 거둡니다. */
        void reapCompletions()
        {
            uint32       head = *_pCqHead;
            const uint32 tail = __atomic_load_n( _pCqTail, __ATOMIC_ACQUIRE );
            while ( head != tail )
            {
                const io_uring_cqe& cqe      = _pCqeArray[head & _cqMask];
                const uint64        userData = cqe.user_data;
                const int32         result   = cqe.res;
                ++head;
                __atomic_store_n( _pCqHead, head, __ATOMIC_RELEASE );

                if ( userData == LinuxAsyncFileIOBackendInternal::kWakeUserData )
                {
                    armWakeRead();
                    continue;
                }
                onReadCompleted( reinterpret_cast<IOUringReadOperation*>( userData ), result );
            }
        }

        /** @brief 조각 하나가 끝났습니다(@p result 는 읽은 바이트 또는 -errno). */
        void onReadCompleted( IOUringReadOperation* pOperation, int32 result )
        {
            AsyncReadRequest& request = *pOperation->_pRequest;
            AsyncIOStatus     status  = AsyncIOStatus::ReadFailed;
            if ( result < 0 )
            {
                if ( result == -ECANCELED )
                    status = AsyncIOStatus::Canceled;
                else
                    SW_LOG_ERROR( "io_uring read failed on '%#' (errno %#)", request._pFile != nullptr ? request._pFile->_path.c_str() : "", -result );
            }
            else
            {
                request._bytesDone += static_cast<uint64>( result );
                const bool bMoreToRead = result > 0 && request._bytesDone < request._size;
                if ( bMoreToRead && request._bCancelRequested.load( std::memory_order_acquire ) == false )
                {
                    pushReadSqe( pOperation );
                    return;
                }
                status = AsyncFileIOBackendUtil::classifyReadResult( true, request._bytesDone, request._size );
            }

            const shared_ptr<AsyncReadRequest> pRequest = std::move( pOperation->_pRequest );
            sw_delete( pOperation );
            _pQueue->finish( pRequest, status );
        }

        /** @brief 매핑과 서술자를 놓습니다. */
        void releaseRing()
        {
            if ( _pSqeArray != nullptr )
                munmap( _pSqeArray, _sqeArrayBytes );
            if ( _pCqRing != nullptr && _pCqRing != _pSqRing )
                munmap( _pCqRing, _cqRingBytes );
            if ( _pSqRing != nullptr )
                munmap( _pSqRing, _sqRingBytes );
            _pSqeArray = nullptr;
            _pCqRing   = nullptr;
            _pSqRing   = nullptr;
            if ( _eventFd >= 0 )
                ::close( _eventFd );
            if ( _ringFd >= 0 )
                ::close( _ringFd );
            _eventFd = -1;
            _ringFd  = -1;
        }

        std::thread       _thread;
        AsyncFileIOQueue* _pQueue;
        void*             _pSqRing;
        void*             _pCqRing;
        io_uring_sqe*     _pSqeArray;
        uint32*           _pSqHead;
        uint32*           _pSqTail;
        uint32*           _pSqIndexArray;
        uint32*           _pCqHead;
        uint32*           _pCqTail;
        io_uring_cqe*     _pCqeArray;
        size_t            _sqRingBytes;
        size_t            _cqRingBytes;
        size_t            _sqeArrayBytes;
        iovec             _wakeIov;
        uint64            _wakeValue;
        uint32            _sqMask;
        uint32            _cqMask;
        uint32            _sqEntryCount;
        uint32            _pendingSubmitCount;
        uint32            _maxInFlightCount;
        int32             _ringFd;
        int32             _eventFd;
    };
} // namespace sw
    #endif // SW_HAS_IO_URING

namespace sw
{
    unique_ptr<IAsyncFileIOBackend> AsyncFileIOBackendUtil::createPlatform( AsyncFileIOQueue& queue, AsyncIOBackendKind kind, uint32 maxInFlightCount )
    {
        if ( kind != AsyncIOBackendKind::Auto && kind != AsyncIOBackendKind::IOUring )
            return nullptr;
    #if defined( SW_HAS_IO_URING )
        return make_unique<IOUringAsyncFileIOBackend>( queue, maxInFlightCount );
    #else
        (void)queue;
        (void)maxInFlightCount;
        return nullptr;
    #endif
    }
} // namespace sw
#endif // SW_PLATFORM_LINUX
