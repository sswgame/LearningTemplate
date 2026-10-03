/**
 * @file Memory.h
 * @brief OS 수준 정렬 할당과 memcpy · memset · memcmp 래퍼입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) Memory — allocateAligned / freeAligned 와 바이트 유틸리티(모두 static)
    // ------------------------------------------------------------------------------
    /** @brief 플랫폼 정렬 할당과 바이트 복사 · 채우기 · 비교입니다. */
    struct SW_API Memory
    {
        /**
         * @brief 정렬된 메모리 블록을 할당합니다.
         * @param size 할당할 바이트 수
         * @param alignment 정렬 기준(2의 거듭제곱)
         * @return 정렬된 메모리 포인터. 실패하면 nullptr
         */
        static void* allocateAligned( size_t size, size_t alignment );

        /**
         * @brief allocateAligned 로 할당한 메모리 블록을 해제합니다.
         * @param pPtr 해제할 메모리 포인터
         */
        static void freeAligned( void* pPtr );

        /** @brief pSrc 에서 pDest 로 size 바이트를 복사합니다. */
        static void* copy( void* pDest, const void* pSrc, size_t size );
        /** @brief 겹칠 수 있는 영역 pSrc 에서 pDest 로 size 바이트를 옮깁니다(memmove). */
        static void* move( void* pDest, const void* pSrc, size_t size );
        /** @brief pDest 의 size 바이트를 value 로 채웁니다. */
        static void* set( void* pDest, uint8 value, size_t size );
        /** @brief 두 버퍼의 size 바이트를 비교합니다. 같으면 0 입니다. */
        static int32 compare( const void* pLhs, const void* pRhs, size_t size );

        // 일반 할당 함수
        static void* allocate( size_t size );
        static void  free( void* pPtr );

        /** @brief 할당 블록 하나가 사용자 크기 앞에 더 잡는 헤더 바이트입니다(크기 · 태그 · 콜스택 해시). 배포본은 헤더가 없어 0 입니다. */
        static size_t getAllocationHeaderSize();
    };

    struct MemoryAllocTag
    {
    };

    /**
     * @brief `T` 를 담는 블록을 정렬 할당(`allocateAligned` · `freeAligned`)으로 잡아야 하는지입니다.
     * @details **`sw_new` · `sw_delete` · `make_unique` · `Allocator` 가 모두 이 한 기준을 씁니다.** 기준은 컴파일러가 정렬 `operator new` 를
     *          고르는 경계(`__STDCPP_DEFAULT_NEW_ALIGNMENT__`, x64 에서 16)입니다. `Memory::allocate` 는 16 바이트 정렬을 보장합니다(헤더 48 바이트).
     *
     *          예전에는 해제 쪽만 `alignof( std::max_align_t )` 를 봤습니다. MSVC 에서 그 값은 **8** 이라, 16 바이트 정렬 타입이 `sw_new`(일반
     *          할당)로 잡히고 `sw_delete`(정렬 해제)로 풀려 힙이 깨졌습니다. 64 바이트 정렬 타입(`ParallelGroup` 등)은 `sw_new` 에 정렬 오버로드가
     *          없어 일반 할당(16 바이트 정렬)으로 잡혀 정렬 자체가 틀렸고, 풀 때는 정렬 해제였습니다. `Allocator` 는 정렬을 아예 보지 않아
     *          `vector` · `make_shared` 에 담긴 과정렬 타입도 16 바이트 정렬이었습니다.
     */
    template <typename T>
    inline constexpr bool kUsesAlignedAllocation = alignof( T ) > __STDCPP_DEFAULT_NEW_ALIGNMENT__;

    template <typename T>
    struct Allocator
    {
        using value_type = T;

        Allocator() = default;

        template <typename U>
        constexpr Allocator( const Allocator<U>& ) noexcept {}

        /**
         * @brief 원소 `n` 개 분량의 메모리를 잡습니다.
         * @details `n * sizeof( T )` 가 **오버플로하면 요청보다 훨씬 작은 블록이 잡히고**, 호출하는 쪽은 원소 `n` 개를 쓸 수 있다고
         *          믿고 그 밖에 씁니다. 표준 할당자가 같은 상황에서 예외를 던지는 이유입니다. `vector::max_size()` 가 이미 이 한계를
         *          알려 주고 있었지만 아무도 강제하지 않고 있었습니다.
         */
        [[nodiscard]] T* allocate( size_t n )
        {
            if ( n > ( ~size_t( 0 ) ) / sizeof( T ) )
                throw std::bad_alloc();

            T* p{ nullptr };
            if constexpr ( kUsesAlignedAllocation<T> )
                p = static_cast<T*>( Memory::allocateAligned( n * sizeof( T ), alignof( T ) ) );
            else
                p = static_cast<T*>( Memory::allocate( n * sizeof( T ) ) );
            if ( p != nullptr )
                return p;
            throw std::bad_alloc();
        }

        void deallocate( T* p, size_t ) noexcept
        {
            if constexpr ( kUsesAlignedAllocation<T> )
                Memory::freeAligned( static_cast<void*>( p ) );
            else
                Memory::free( static_cast<void*>( p ) );
        }
    };

    template <typename T, typename U>
    bool operator==( const Allocator<T>&, const Allocator<U>& ) { return true; }

    template <typename T, typename U>
    bool operator!=( const Allocator<T>&, const Allocator<U>& ) { return false; }
} // namespace sw

// sw_new 용 전역 placement new/delete 오버로드. 정렬이 기본(16)보다 큰 타입은 컴파일러가 align_val_t 판을 고른다(kUsesAlignedAllocation).
inline void* operator new( size_t size, sw::MemoryAllocTag ) { return sw::Memory::allocate( size ); }
inline void* operator new[]( size_t size, sw::MemoryAllocTag ) { return sw::Memory::allocate( size ); }
inline void  operator delete( void* pPtr, sw::MemoryAllocTag ) noexcept { sw::Memory::free( pPtr ); }
inline void  operator delete[]( void* pPtr, sw::MemoryAllocTag ) noexcept { sw::Memory::free( pPtr ); }
inline void* operator new( size_t size, std::align_val_t alignment, sw::MemoryAllocTag ) { return sw::Memory::allocateAligned( size, static_cast<size_t>( alignment ) ); }
inline void* operator new[]( size_t size, std::align_val_t alignment, sw::MemoryAllocTag ) { return sw::Memory::allocateAligned( size, static_cast<size_t>( alignment ) ); }
inline void  operator delete( void* pPtr, std::align_val_t, sw::MemoryAllocTag ) noexcept { sw::Memory::freeAligned( pPtr ); }
inline void  operator delete[]( void* pPtr, std::align_val_t, sw::MemoryAllocTag ) noexcept { sw::Memory::freeAligned( pPtr ); }

template <typename T>
void sw_delete_func( T* pPtr )
{
    if ( pPtr != nullptr )
    {
        pPtr->~T();
        if constexpr ( sw::kUsesAlignedAllocation<T> )
            sw::Memory::freeAligned( pPtr );
        else
            sw::Memory::free( const_cast<void*>( static_cast<const void*>( pPtr ) ) );
    }
}

/**
 * @brief sw_new 로 만든 배열을 해제합니다. **원소의 소멸자는 부르지 않습니다.**
 * @warning 그래서 소멸자가 하는 일이 없는 타입(trivially destructible)에만 쓸 수 있습니다. 그렇지 않은 타입을
 *          `sw_new T[n]` 으로 만들고 이것으로 해제하면 원소가 새고, 컴파일러가 배열 앞에 넣는 원소 개수 쿠키 때문에 해제
 *          주소도 어긋납니다. 소멸이 필요한 배열은 `vector<T>` 를 쓰거나 `sw_new_array<T>( n )` / `sw_delete_array( p, n )` 을
 *          짝으로 쓰십시오(`PagedArray` 가 후자입니다. 임의의 T 를 담기 때문입니다).
 */
template <typename T>
void sw_delete_array_func( T* pPtr )
{
    static_assert( std::is_trivially_destructible_v<T>,
                   "sw_delete_array 는 원소 소멸자를 부르지 않습니다. vector<T> 또는 sw_new_array / sw_delete_array( p, n ) 짝을 쓰세요." );
    if ( pPtr != nullptr )
    {
        if constexpr ( sw::kUsesAlignedAllocation<T> )
            sw::Memory::freeAligned( pPtr );
        else
            sw::Memory::free( const_cast<void*>( static_cast<const void*>( pPtr ) ) );
    }
}

#define sw_new new ( sw::MemoryAllocTag{} )
// `static_cast<void*>` 를 끼운다. T 가 포인터일 때(`vector<char*>` 등) `char**` → `void*` 가 암시적 다단 포인터
// 변환이 되어, 의도한 것인지 읽는 사람이 알 수 없기 때문이다.
#define sw_placement_new( pPtr ) new ( static_cast<void*>( pPtr ) )
#define sw_delete                sw_delete_func
#define sw_delete_array          sw_delete_array_func
#define sw_new_array             sw_new_array_func

/**
 * @brief 원소 @p count 개를 값 초기화(`T{}`)한 배열을 sw 할당자로 잡습니다. `sw_delete_array( pArray, count )` 로 풉니다.
 * @details `sw_new T[n]` 과 달리 배열 앞에 개수 쿠키를 두지 않으므로 소멸자가 있는 타입도 담을 수 있습니다. 대신 풀 때 같은 개수를 다시
 *          넘겨야 원소 소멸자가 불립니다. 원소 생성자는 예외를 던지지 않아야 합니다(던지면 이미 만든 원소와 블록이 샙니다).
 * @return 첫 원소 주소. 크기가 넘치거나 할당에 실패하면 `std::bad_alloc` 을 던집니다(`Allocator::allocate` 와 같습니다).
 */
template <typename T>
[[nodiscard]] T* sw_new_array_func( size_t count )
{
    if ( count > ( ~size_t( 0 ) ) / sizeof( T ) )
        throw std::bad_alloc();

    void* pMemory{ nullptr };
    if constexpr ( sw::kUsesAlignedAllocation<T> )
        pMemory = sw::Memory::allocateAligned( count * sizeof( T ), alignof( T ) );
    else
        pMemory = sw::Memory::allocate( count * sizeof( T ) );
    if ( pMemory == nullptr )
        throw std::bad_alloc();

    T* pArray = static_cast<T*>( pMemory );
    for ( size_t index = 0; index < count; ++index )
    {
        sw_placement_new( pArray + index ) T{};
    }
    return pArray;
}

/**
 * @brief `sw_new_array` 로 만든 배열의 원소 @p count 개를 소멸시키고 블록을 풉니다. @p count 는 만들 때 넘긴 값과 같아야 합니다.
 */
template <typename T>
void sw_delete_array_func( T* pArray, size_t count )
{
    if ( pArray == nullptr )
        return;
    std::destroy_n( pArray, count );
    if constexpr ( sw::kUsesAlignedAllocation<T> )
        sw::Memory::freeAligned( pArray );
    else
        sw::Memory::free( const_cast<void*>( static_cast<const void*>( pArray ) ) );
}

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
            // 불완전 타입의 삭제를 막는 표준 관용구다. sizeof 는 0 이 될 수 없으니 비교가 무의미해 보이지만, **T 가 불완전하면
            // sizeof 자체가 컴파일되지 않는다.** 그것이 목적이다.
            // NOLINTNEXTLINE(bugprone-sizeof-expression)
            static_assert( sizeof( T ) > 0, "can't delete an incomplete type" );
            sw_delete_func( pPtr );
        }
    };

    /**
     * @brief `unique_ptr<T[]>` 의 해제자입니다. 원소 소멸자를 부르지 않으므로(개수를 모른다) 소멸자가 하는 일이 없는 원소만 담습니다.
     * @details 소멸이 필요한 원소의 배열은 `vector<T>` 로 담습니다. 그런 `T` 로 이 해제자를 만들면 `sw_delete_array` 의 static_assert 가 막습니다.
     */
    template <typename T>
    struct default_delete<T[]>
    {
        constexpr default_delete() noexcept = default;

        void operator()( T* pArray ) const { sw_delete_array_func( pArray ); }
    };

    template <typename T, typename Deleter = default_delete<T>>
    using unique_ptr = std::unique_ptr<T, Deleter>;

    template <typename T, typename... Args>
    std::enable_if_t<std::is_array_v<T> == false, unique_ptr<T>> make_unique( Args&&... args )
    {
        if constexpr ( kUsesAlignedAllocation<T> )
        {
            void* pMem = Memory::allocateAligned( sizeof( T ), alignof( T ) );
            if ( pMem == nullptr )
                throw std::bad_alloc();
            return unique_ptr<T>( sw_placement_new( pMem ) T( std::forward<Args>( args )... ) );
        }
        else
        {
            return unique_ptr<T>( sw_new T( std::forward<Args>( args )... ) );
        }
    }

    /**
     * @brief 원소 @p count 개를 값 초기화한 배열을 sw 할당자로 잡아 `unique_ptr<T[]>` 로 돌려줍니다(`std::make_unique<T[]>` 와 같은 모양).
     * @details 원소는 소멸자가 하는 일이 없는 타입이어야 합니다(`default_delete<T[]>`).
     */
    template <typename T>
    std::enable_if_t<std::is_array_v<T> && std::extent_v<T> == 0, unique_ptr<T>> make_unique( size_t count )
    {
        return unique_ptr<T>( sw_new_array_func<std::remove_extent_t<T>>( count ) );
    }

    template <typename T>
    using shared_ptr = std::shared_ptr<T>;

    template <typename T>
    using weak_ptr = std::weak_ptr<T>;

#if defined( SW_ENABLE_STL_CONTAINER )
    // 표준 할당자 구성이다. 표준 함수를 그대로 들여야 std 컨테이너 인자의 ADL 이 같은 함수를 찾는다(따로 정의하면 호출이 모호해진다).
    using std::make_shared;
#else
    template <typename T, typename... Args>
    shared_ptr<T> make_shared( Args&&... args ) { return std::allocate_shared<T>( sw::Allocator<T>{}, std::forward<Args>( args )... ); }
#endif
} // namespace sw
