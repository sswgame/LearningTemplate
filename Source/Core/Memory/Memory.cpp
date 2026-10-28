#include "pch.h"

#include "Core/Memory/Memory.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Foundation/PlatformOSHeaders.h"
#include "Core/Math/MathUtil.h"

#include <cstdio>

namespace sw
{
#if !defined( SW_SHIPPING )
    /**
     * @struct AllocHeader
     * @brief 사용자 데이터 바로 앞에 놓이는 할당 메타데이터 헤더입니다.
     */
    struct alignas( 16 ) AllocHeader
    {
        size_t    _size;        ///< 요청한 사용자 데이터 크기(바이트)
        MemoryTag _tag;         ///< 할당 시점 스레드의 용도 태그(`MemoryTag`). 해제는 이 값으로 뺀다
        uint8     _bObserved;   ///< 할당 관찰자(`registerAllocationObserver`)에 알린 블록이면 SW_TRUE — 해제도 그때만 알린다
        uint8     _arrPad[2];   ///< 16바이트 경계 정렬용 패딩
        uint32    _windowStamp; ///< 기록기가 붙인 스탬프(`AllocationRecord::_windowStamp`). 할당기는 뜻을 모르고 해제 때 돌려준다
        uint64    _hash;        ///< 할당 시점 콜 스택의 해시
        uint64    _magic;       ///< 유효성 검증용 매직 넘버
        void*     _pRawPtr;     ///< OS 가 준 원래 할당 시작 주소(정렬 패딩 이전)
        uint64    _trackerID;   ///< 이 블록을 센 기록기의 번호(`IAllocationTracker::getTrackerID`). 0 이면 아무도 세지 않았고 해제도 알리지 않는다
    };

    static_assert( sizeof( AllocHeader ) == 48, "AllocHeader must stay 48 bytes (a multiple of 16)" );

    /** @brief 엔진이 할당한 블록인지 식별하는 64비트 매직 상수입니다. */
    static constexpr uint64 kAllocMagic = 0x5C09B10CDA7A0000;
#endif

    namespace
    {
        /** @brief 걸린 할당 기록기(`MemoryProfiler`)입니다. 할당마다 acquire 읽기 하나입니다. */
        atomic<IAllocationTracker*> s_pAllocationTracker{ nullptr };
        /** @brief 마지막으로 나눠 준 기록기 번호입니다(`IAllocationTracker` 생성자). 0 은 "세지 않음" 이라 1 부터 나눈다. */
        atomic<uint64> s_nextTrackerID{ 0 };
        /** @brief 이 스레드의 현재 할당 태그입니다(`Memory::setCurrentMemoryTag`). */
        thread_local MemoryTag t_currentMemoryTag{ MemoryTag::Unknown };

#if !defined( SW_SHIPPING )
        /** @brief 걸린 할당 관찰자입니다. 할당마다 relaxed 읽기 하나입니다. */
        atomic<const MemoryAllocationObserver*> s_pAllocationObserver{ nullptr };
        /** @brief 이 스레드에서 실패로 돌릴 남은 할당 수입니다(`injectAllocationFailures`). */
        thread_local uint32 t_injectedFailureCount{ 0 };

        struct MemoryInternal
        {
            /** @brief 테스트가 주입한 실패가 남았으면 하나를 쓰고 true 입니다(`injectAllocationFailures`). */
            static bool consumeInjectedFailure()
            {
                if ( t_injectedFailureCount == 0 )
                    return false;
                --t_injectedFailureCount;
                return true;
            }

            /**
             * @brief 사용자 블록 앞의 헤더를 채우고 프로파일러에 할당을 알립니다. `allocate` · `allocateAligned` 가 함께 씁니다.
             * @return 사용자 블록 주소(`pUserPtr`) 그대로입니다.
             */
            static void* writeAllocHeader( void* pUserPtr, void* pRawPtr, size_t size, MemoryTag tag )
            {
                AllocHeader* pHeader  = reinterpret_cast<AllocHeader*>( static_cast<utf8*>( pUserPtr ) - sizeof( AllocHeader ) );
                pHeader->_size        = size;
                pHeader->_tag         = tag;
                pHeader->_magic       = kAllocMagic;
                pHeader->_hash        = 0;
                pHeader->_windowStamp = 0;
                pHeader->_pRawPtr     = pRawPtr;
                pHeader->_trackerID   = 0;
                pHeader->_bObserved   = SW_FALSE;

                IAllocationTracker* pTracker = s_pAllocationTracker.load( std::memory_order_acquire );
                if ( pTracker != nullptr )
                {
                    const AllocationRecord record = pTracker->recordAllocation( pUserPtr, size, pHeader->_tag );
                    if ( record._bCounted )
                    {
                        pHeader->_hash        = record._callStackHash;
                        pHeader->_windowStamp = record._windowStamp;
                        pHeader->_trackerID   = pTracker->getTrackerID();
                    }
                }

                const MemoryAllocationObserver* pObserver = s_pAllocationObserver.load( std::memory_order_acquire );
                if ( pObserver != nullptr )
                {
                    pHeader->_bObserved = SW_TRUE;
                    pObserver->_pOnAllocate( pUserPtr, size, tag );
                }
                return pUserPtr;
            }

            /**
             * @brief 엔진이 할당하지 않은 블록(또는 이미 해제한 블록)을 풀려 했다고 알립니다. 해제는 하지 않습니다.
             * @details 말없이 건너뛰면 안 됩니다 — **배포본에는 헤더가 없어** 같은 호출이 진짜 이중 해제 · 남의 힙 해제가 되므로,
             *          개발 빌드에서 조용한 버그가 배포본에서만 힙을 깹니다. 로그보다 아래 층이라 낮은 층 보고(Error 는 stderr 에 동기로 먼저)로 냅니다.
             */
            static void reportForeignFree( const void* pUserPtr )
            {
                SW_LOW_LEVEL_ERROR( "[Memory] free of a block this allocator does not own (double free or foreign pointer): %p", pUserPtr );
            }

            /**
             * @brief 헤더를 확인하고 프로파일러에 해제를 알린 뒤 매직을 지웁니다(이중 해제 방지). `free` · `freeAligned` 가 함께 씁니다.
             * @return OS 에 돌려줄 원래 할당 주소입니다. 헤더가 손상됐거나 엔진이 할당한 블록이 아니면 nullptr 이고, 그때는 해제하지 않습니다.
             */
            static void* releaseAllocHeader( void* pUserPtr )
            {
                AllocHeader* pHeader = reinterpret_cast<AllocHeader*>( static_cast<utf8*>( pUserPtr ) - sizeof( AllocHeader ) );
                if ( pHeader->_magic != kAllocMagic )
                    return nullptr;

                // 센 블록을 센 기록기에만 알린다. 기록기가 떨어졌거나 다른 기록기로 바뀌었으면 알릴 곳이 없다.
                if ( pHeader->_trackerID != 0 )
                {
                    IAllocationTracker* pTracker = s_pAllocationTracker.load( std::memory_order_acquire );
                    if ( pTracker != nullptr && pTracker->getTrackerID() == pHeader->_trackerID )
                        pTracker->recordFree( pUserPtr, pHeader->_size, pHeader->_tag, AllocationRecord{ pHeader->_hash, true, pHeader->_windowStamp } );
                }

                // 관찰 중에 할당된 블록만 알린다. 관찰자가 그 사이 떨어졌으면 알릴 곳이 없다.
                if ( pHeader->_bObserved == SW_TRUE )
                {
                    const MemoryAllocationObserver* pObserver = s_pAllocationObserver.load( std::memory_order_acquire );
                    if ( pObserver != nullptr )
                        pObserver->_pOnFree( pUserPtr, pHeader->_tag );
                }

                pHeader->_magic = 0;
                return pHeader->_pRawPtr;
            }
        };
#endif
    } // namespace

#if !defined( SW_SHIPPING )
    void Memory::injectAllocationFailures( uint32 count )
    {
        t_injectedFailureCount = count;
    }
#endif

    /**
     * @brief 지정한 바이트 경계로 정렬된 메모리 블록을 할당합니다.
     */
    void* Memory::allocateAligned( size_t size, size_t alignment )
    {
#if defined( SW_SHIPPING )
        return allocateAligned( size, alignment, MemoryTag::Unknown );
#else
        return allocateAligned( size, alignment, getCurrentMemoryTag() );
#endif
    }

    void* Memory::allocateAligned( size_t size, size_t alignment, [[maybe_unused]] MemoryTag tag )
    {
        const size_t align = MathUtil::max( alignment, sizeof( void* ) );

#if defined( SW_SHIPPING )
    #if defined( SW_PLATFORM_WINDOWS )
        return _aligned_malloc( size, align );
    #else
        void* pRawPtr{ nullptr };
        if ( posix_memalign( &pRawPtr, align, size ) != 0 )
            return nullptr;
        return pRawPtr;
    #endif
#else // SW_SHIPPING
        if ( MemoryInternal::consumeInjectedFailure() )
            return nullptr;

        // 헤더와 정렬 여유를 더하다 오버플로하면 **요청보다 작은 블록**이 잡히고, 그 뒤의 헤더 쓰기가 곧바로 범위를 넘는다.
        // 오버플로할 크기는 어차피 할당될 수 없으므로 여기서 거절한다.
        if ( size > SIZE_MAX - sizeof( AllocHeader ) - align )
            return nullptr;

        size_t totalSize = size + sizeof( AllocHeader ) + align;

    #if defined( SW_PLATFORM_WINDOWS )
        void* pRawPtr = _aligned_malloc( totalSize, align );
    #else
        void* pRawPtr{ nullptr };
        if ( posix_memalign( &pRawPtr, align, totalSize ) != 0 )
            pRawPtr = nullptr;
    #endif

        if ( pRawPtr == nullptr )
            return nullptr;

        const uintptr_t rawAddr  = reinterpret_cast<uintptr_t>( pRawPtr );
        const uintptr_t userAddr = MathUtil::align( rawAddr + sizeof( AllocHeader ), static_cast<uintptr_t>( align ) );
        return MemoryInternal::writeAllocHeader( reinterpret_cast<void*>( userAddr ), pRawPtr, size, tag );
#endif // SW_SHIPPING
    }

    /**
     * @brief 정렬 할당한 메모리 블록을 해제합니다.
     */
    void Memory::freeAligned( void* pPtr )
    {
        if ( pPtr == nullptr )
            return;

#if defined( SW_SHIPPING )
    #if defined( SW_PLATFORM_WINDOWS )
        _aligned_free( pPtr );
    #else
        ::free( pPtr );
    #endif
#else // SW_SHIPPING
        void* pRawPtr = MemoryInternal::releaseAllocHeader( pPtr );
        if ( pRawPtr == nullptr )
        {
            MemoryInternal::reportForeignFree( pPtr );
            return;
        }
    #if defined( SW_PLATFORM_WINDOWS )
        _aligned_free( pRawPtr );
    #else
        ::free( pRawPtr );
    #endif
#endif // SW_SHIPPING
    }

    /**
     * @brief 일반 동적 메모리를 할당합니다.
     */
    void* Memory::allocate( size_t size )
    {
#if defined( SW_SHIPPING )
        return ::malloc( size );
#else
        return allocate( size, getCurrentMemoryTag() );
#endif
    }

    void* Memory::allocate( size_t size, [[maybe_unused]] MemoryTag tag )
    {
#if defined( SW_SHIPPING )
        return ::malloc( size );
#else  // SW_SHIPPING
        if ( MemoryInternal::consumeInjectedFailure() )
            return nullptr;
        // allocateAligned 와 같은 이유로 오버플로할 크기를 먼저 거절한다.
        if ( size > SIZE_MAX - sizeof( AllocHeader ) )
            return nullptr;

        size_t totalSize = size + sizeof( AllocHeader );
        void*  pRawPtr   = ::malloc( totalSize );
        if ( pRawPtr == nullptr )
            return nullptr;

        return MemoryInternal::writeAllocHeader( static_cast<utf8*>( pRawPtr ) + sizeof( AllocHeader ), pRawPtr, size, tag );
#endif // SW_SHIPPING
    }

    /**
     * @brief 동적 메모리를 해제합니다.
     */
    void Memory::free( void* pPtr )
    {
        if ( pPtr == nullptr )
            return;

#if defined( SW_SHIPPING )
        ::free( pPtr );
#else  // SW_SHIPPING
        void* pRawPtr = MemoryInternal::releaseAllocHeader( pPtr );
        if ( pRawPtr != nullptr )
            ::free( pRawPtr );
        else
            MemoryInternal::reportForeignFree( pPtr );
#endif // SW_SHIPPING
    }

    size_t Memory::getAllocationHeaderSize()
    {
#if defined( SW_SHIPPING )
        return 0;
#else
        return sizeof( AllocHeader );
#endif
    }

    void Memory::registerAllocationObserver( [[maybe_unused]] const MemoryAllocationObserver* pObserver )
    {
#if !defined( SW_SHIPPING )
        s_pAllocationObserver.store( pObserver, std::memory_order_release );
#endif
    }

    void Memory::unregisterAllocationObserver()
    {
#if !defined( SW_SHIPPING )
        s_pAllocationObserver.store( nullptr, std::memory_order_release );
#endif
    }

    void* Memory::copy( void* pDest, const void* pSrc, size_t size )
    {
        const bool bNothingToCopy = pDest == nullptr || pSrc == nullptr || size == 0;
        if ( bNothingToCopy )
            return pDest;
        return std::memcpy( pDest, pSrc, size );
    }

    void* Memory::move( void* pDest, const void* pSrc, size_t size )
    {
        const bool bNothingToMove = pDest == nullptr || pSrc == nullptr || size == 0;
        if ( bNothingToMove )
            return pDest;
        return std::memmove( pDest, pSrc, size );
    }

    void* Memory::set( void* pDest, uint8 value, size_t size )
    {
        if ( pDest == nullptr || size == 0 )
            return pDest;
        return std::memset( pDest, value, size );
    }

    int32 Memory::compare( const void* pLhs, const void* pRhs, size_t size )
    {
        if ( pLhs == pRhs || size == 0 )
            return 0;
        if ( pLhs == nullptr )
            return -1;
        if ( pRhs == nullptr )
            return 1;
        return std::memcmp( pLhs, pRhs, size );
    }
} // namespace sw

namespace sw
{
    IAllocationTracker::IAllocationTracker()
        : _trackerID{ s_nextTrackerID.fetch_add( 1, std::memory_order_relaxed ) + 1 }
    {
    }

    void Memory::registerAllocationTracker( IAllocationTracker* pTracker )
    {
        IAllocationTracker* pExpected{ nullptr };
        s_pAllocationTracker.compare_exchange_strong( pExpected, pTracker, std::memory_order_acq_rel, std::memory_order_relaxed );
    }

    void Memory::unregisterAllocationTracker( IAllocationTracker* pTracker )
    {
        IAllocationTracker* pExpected = pTracker;
        s_pAllocationTracker.compare_exchange_strong( pExpected, nullptr, std::memory_order_acq_rel, std::memory_order_relaxed );
    }

    void Memory::setCurrentMemoryTag( MemoryTag tag )
    {
        t_currentMemoryTag = tag;
    }

    MemoryTag Memory::getCurrentMemoryTag()
    {
        return t_currentMemoryTag;
    }
} // namespace sw
