#include "pch.h"

#include "Core/Memory/Memory.h"

#include "Core/Common/PlatformOsHeaders.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/MemoryProfiler.h"

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
        size_t    _size;      ///< 요청한 사용자 데이터 크기(바이트)
        MemoryTag _tag;       ///< 할당 시점 스레드의 용도 태그(`MemoryTag`). 해제는 이 값으로 뺀다
        uint8     _bObserved; ///< 할당 관찰자(`setAllocationObserver`)에 알린 블록이면 SW_TRUE — 해제도 그때만 알린다
        uint8     _arrPad[6]; ///< 16바이트 경계 정렬용 패딩
        uint64    _hash;      ///< 할당 시점 콜 스택의 해시
        uint64    _magic;     ///< 유효성 검증용 매직 넘버
        void*     _pRawPtr;   ///< OS 가 준 원래 할당 시작 주소(정렬 패딩 이전)
        void*     _pReserved; ///< 헤더를 48바이트(16의 배수)로 맞추는 패딩
    };

    static_assert( sizeof( AllocHeader ) == 48, "AllocHeader must stay 48 bytes (a multiple of 16)" );

    /** @brief 엔진이 할당한 블록인지 식별하는 64비트 매직 상수입니다. */
    static constexpr uint64 kAllocMagic = 0x5C09B10CDA7A0000;

    namespace
    {
        /** @brief 걸린 할당 관찰자입니다. 할당마다 relaxed 읽기 하나입니다. */
        atomic<const MemoryAllocationObserver*> s_pAllocationObserver{ nullptr };

        struct MemoryInternal
        {
            /**
             * @brief 사용자 블록 앞의 헤더를 채우고 프로파일러에 할당을 알립니다. `allocate` · `allocateAligned` 가 함께 씁니다.
             * @return 사용자 블록 주소(`pUserPtr`) 그대로입니다.
             */
            static void* writeAllocHeader( void* pUserPtr, void* pRawPtr, size_t size, MemoryTag tag )
            {
                AllocHeader* pHeader = reinterpret_cast<AllocHeader*>( static_cast<utf8*>( pUserPtr ) - sizeof( AllocHeader ) );
                pHeader->_size       = size;
                pHeader->_tag        = tag;
                pHeader->_magic      = kAllocMagic;
                pHeader->_hash       = 0;
                pHeader->_pRawPtr    = pRawPtr;
                pHeader->_bObserved  = SW_FALSE;

                MemoryProfiler* pProfiler = MemoryProfiler::getActive();
                if ( pProfiler != nullptr )
                    pHeader->_hash = pProfiler->recordAllocation( pUserPtr, size, pHeader->_tag );

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
             *          개발 빌드에서 조용한 버그가 배포본에서만 힙을 깹니다. 로거는 이 할당기를 쓰므로 stderr 로 직접 씁니다.
             */
            static void reportForeignFree( const void* pUserPtr )
            {
                std::fprintf( stderr, "[Memory] free of a block this allocator does not own (double free or foreign pointer): %p\n", pUserPtr );
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

                MemoryProfiler* pProfiler = MemoryProfiler::getActive();
                if ( pProfiler != nullptr )
                    pProfiler->recordFree( pUserPtr, pHeader->_size, pHeader->_tag, pHeader->_hash );

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
    } // namespace
#endif

    /**
     * @brief 지정한 바이트 경계로 정렬된 메모리 블록을 할당합니다.
     */
    void* Memory::allocateAligned( size_t size, size_t alignment )
    {
#if defined( SW_SHIPPING )
        return allocateAligned( size, alignment, MemoryTag::Unknown );
#else
        return allocateAligned( size, alignment, MemoryProfiler::getCurrentMemoryTag() );
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
        return allocate( size, MemoryProfiler::getCurrentMemoryTag() );
#endif
    }

    void* Memory::allocate( size_t size, [[maybe_unused]] MemoryTag tag )
    {
#if defined( SW_SHIPPING )
        return ::malloc( size );
#else  // SW_SHIPPING
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

    void Memory::setAllocationObserver( [[maybe_unused]] const MemoryAllocationObserver* pObserver )
    {
#if !defined( SW_SHIPPING )
        s_pAllocationObserver.store( pObserver, std::memory_order_release );
#endif
    }

    void* Memory::copy( void* pDest, const void* pSrc, size_t size )
    {
        if ( pDest == nullptr || pSrc == nullptr || size == 0 )
            return pDest;
        return std::memcpy( pDest, pSrc, size );
    }

    void* Memory::move( void* pDest, const void* pSrc, size_t size )
    {
        if ( pDest == nullptr || pSrc == nullptr || size == 0 )
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
