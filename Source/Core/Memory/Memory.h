/**
 * @file Memory.h
 * @brief OS 수준 정렬 할당과 memcpy/memset/memcmp 래퍼.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) Memory — alignedAlloc / alignedFree 와 바이트 유틸 (전부 static)
    // ------------------------------------------------------------------------------
    /** @brief 플랫폼 정렬 할당과 바이트 복사·채움·비교입니다. */
    struct SW_API Memory
    {
        /**
         * @brief 정렬된 메모리 블록을 할당합니다.
         * @param size 할당할 바이트 크기
         * @param alignment 메모리 정렬 기준 (2의 거듭제곱)
         * @return 정렬된 메모리 포인터. 실패 시 nullptr.
         */
        static void* alignedAlloc( size_t size, size_t alignment );

        /**
         * @brief alignedAlloc으로 할당된 메모리 블록을 해제합니다.
         * @param pPtr 해제할 메모리 포인터
         */
        static void alignedFree( void* pPtr );

        /** @brief pSrc 에서 pDest 로 size 바이트를 복사합니다. */
        static void* copy( void* pDest, const void* pSrc, size_t size );
        /** @brief 중첩될 수 있는 메모리 영역(pSrc)에서 pDest 로 size 바이트를 이동합니다. */
        static void* move( void* pDest, const void* pSrc, size_t size );
        /** @brief pDest 의 size 바이트를 value 로 채웁니다. */
        static void* set( void* pDest, uint8 value, size_t size );
        /** @brief 두 버퍼의 size 바이트를 비교합니다. 같으면 0 입니다. */
        static int32 compare( const void* pLhs, const void* pRhs, size_t size );

        // Custom Allocator Functions
        static void* allocMemory( size_t size );
        static void  freeMemory( void* pPtr );
    };

    struct MemoryAllocTag
    {
    };

    template <typename T>
    struct Allocator
    {
        using value_type = T;

        Allocator() = default;

        template <typename U>
        constexpr Allocator( const Allocator<U>& ) noexcept {}

        [[nodiscard]] T* allocate( size_t n )
        {
            T* p = static_cast<T*>( Memory::allocMemory( n * sizeof( T ) ) );
            if ( p != nullptr )
                return p;
            throw std::bad_alloc();
        }

        void deallocate( T* p, size_t ) noexcept { Memory::freeMemory( static_cast<void*>( p ) ); }
    };

    template <typename T, typename U>
    bool operator==( const Allocator<T>&, const Allocator<U>& ) { return true; }

    template <typename T, typename U>
    bool operator!=( const Allocator<T>&, const Allocator<U>& ) { return false; }
} // namespace sw

// Global placement new/delete overloads for sw_new
inline void* operator new( size_t size, sw::MemoryAllocTag ) { return sw::Memory::allocMemory( size ); }
inline void* operator new[]( size_t size, sw::MemoryAllocTag ) { return sw::Memory::allocMemory( size ); }
inline void  operator delete( void* pPtr, sw::MemoryAllocTag ) noexcept { sw::Memory::freeMemory( pPtr ); }
inline void  operator delete[]( void* pPtr, sw::MemoryAllocTag ) noexcept { sw::Memory::freeMemory( pPtr ); }

template <typename T>
void sw_delete_func( T* pPtr )
{
    if ( pPtr != nullptr )
    {
        pPtr->~T();
        if constexpr ( alignof( T ) > alignof( std::max_align_t ) )
            sw::Memory::alignedFree( pPtr );
        else
            sw::Memory::freeMemory( const_cast<void*>( static_cast<const void*>( pPtr ) ) );
    }
}

/**
 * @brief sw_new 로 만든 배열을 놓습니다. **원소 소멸자를 부르지 않습니다.**
 * @warning 그래서 자명하게 소멸하는 타입(trivially destructible)에만 쓸 수 있습니다. 그렇지 않은 타입을
 *          `sw_new T[n]` 으로 잡고 이걸로 놓으면 원소가 새고, 컴파일러가 배열 앞에 넣는 원소 개수 쿠키
 *          때문에 해제 주소도 어긋납니다. 소멸이 필요한 배열은 `vector<T>` 를 쓰거나 `new[]`/`delete[]` 를
 *          짝으로 쓰세요(`PagedArray` 가 후자입니다 — 임의의 T 를 담기 때문입니다).
 */
template <typename T>
void sw_delete_array_func( T* pPtr )
{
    static_assert( std::is_trivially_destructible_v<T>,
                   "sw_delete_array 는 원소 소멸자를 부르지 않습니다. vector<T> 또는 new[]/delete[] 짝을 쓰세요." );
    if ( pPtr != nullptr )
    {
        if constexpr ( alignof( T ) > alignof( std::max_align_t ) )
            sw::Memory::alignedFree( pPtr );
        else
            sw::Memory::freeMemory( const_cast<void*>( static_cast<const void*>( pPtr ) ) );
    }
}

#define sw_malloc( size ) sw::Memory::allocMemory( size )
#define sw_free( pPtr )   sw::Memory::freeMemory( pPtr )
#define sw_new            new ( sw::MemoryAllocTag{} )
// `static_cast<void*>` 를 끼운다. T 가 포인터일 때(`vector<char*>` 등) `char**` → `void*` 가
// 암시적 다단 포인터 변환이 되어, 의도한 것인지 읽는 사람이 알 수 없다.
#define sw_placement_new( pPtr ) new ( static_cast<void*>( pPtr ) )
#define sw_delete                sw_delete_func
#define sw_delete_array          sw_delete_array_func

namespace sw
{
    template <typename T>
    struct default_delete
    {
        constexpr default_delete() noexcept = default;

        template <typename U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
        default_delete( const default_delete<U>& ) noexcept {}

        void operator()( T* pPtr ) const
        {
            // 불완전 타입 삭제를 막는 표준 관용구다. sizeof 는 0 이 될 수 없으니 비교가 무의미해
            // 보이지만, **T 가 불완전하면 sizeof 자체가 컴파일되지 않는다** — 그게 목적이다.
            // NOLINTNEXTLINE(bugprone-sizeof-expression)
            static_assert( sizeof( T ) > 0, "can't delete an incomplete type" );
            sw_delete_func( pPtr );
        }
    };

    template <typename T, typename Deleter = default_delete<T>>
    using unique_ptr = std::unique_ptr<T, Deleter>;

    template <typename T, typename... Args>
    unique_ptr<T> make_unique( Args&&... args )
    {
        if constexpr ( alignof( T ) > alignof( std::max_align_t ) )
        {
            void* pMem = Memory::alignedAlloc( sizeof( T ), alignof( T ) );
            if ( pMem == nullptr )
                throw std::bad_alloc();
            return unique_ptr<T>( new ( pMem ) T( std::forward<Args>( args )... ) );
        }
        else
        {
            return unique_ptr<T>( sw_new T( std::forward<Args>( args )... ) );
        }
    }

    template <typename T>
    using shared_ptr = std::shared_ptr<T>;

    template <typename T>
    using weak_ptr = std::weak_ptr<T>;

    template <typename T, typename... Args>
    shared_ptr<T> make_shared( Args&&... args ) { return std::allocate_shared<T>( sw::Allocator<T>{}, std::forward<Args>( args )... ); }
} // namespace sw
