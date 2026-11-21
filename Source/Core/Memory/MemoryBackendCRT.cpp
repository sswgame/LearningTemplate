#include "pch.h"

#include "Core/Foundation/PlatformOSHeaders.h"
#include "Core/Memory/MemoryBackend.h"

#if defined( SW_PLATFORM_WINDOWS )
#elif defined( SW_PLATFORM_POSIX )
    #include <malloc.h>
#else
    #error "Unsupported platform"
#endif

// 힙 백엔드 — C 런타임 힙(`SW_MEMORY_BACKEND=crt`). 이 파일과 짝 백엔드 파일 가운데 하나만 링크된다(Source/Core/CMakeLists.txt).
// 웹(Emscripten)은 이 백엔드로 고정이다 — 브라우저 wasm 힙 위에서 다른 할당기가 얻을 것이 없다(Godot 웹도 시스템 malloc).
// Windows CRT 는 정렬 블록을 `_aligned_free` 로만 풀 수 있다 — 해제 길을 하나로 두려고(`MemoryBackend::free`) 보통 블록도 기본 정렬로 정렬 힙에서 잡는다.
// 재할당 · 쓸 수 있는 크기도 같은 정렬을 넘기는 `_aligned_realloc` · `_aligned_msize` 다. POSIX 는 `posix_memalign` 블록을 `free` 로 풀 수 있어 그대로다.

namespace sw
{
#if defined( SW_PLATFORM_WINDOWS )
    namespace
    {
        /** @brief Windows 에서 보통 블록을 잡는 정렬입니다(`malloc` 과 같은 보장 — `Memory` 헤더와 `kUsesAlignedAllocation` 이 기댄다). */
        constexpr size_t kDefaultBlockAlignment = __STDCPP_DEFAULT_NEW_ALIGNMENT__;
    } // namespace
#endif

    void* MemoryBackend::allocate( size_t size ) noexcept
    {
#if defined( SW_PLATFORM_WINDOWS )
        return _aligned_malloc( size, kDefaultBlockAlignment );
#elif defined( SW_PLATFORM_POSIX )
        return ::malloc( size );
#else
    #error "Unsupported platform"
#endif
    }

    void* MemoryBackend::allocateAligned( size_t size, size_t alignment ) noexcept
    {
#if defined( SW_PLATFORM_WINDOWS )
        return _aligned_malloc( size, alignment );
#elif defined( SW_PLATFORM_POSIX )
        void* pBlock{ nullptr };
        if ( posix_memalign( &pBlock, alignment, size ) != 0 )
            return nullptr;
        return pBlock;
#else
    #error "Unsupported platform"
#endif
    }

    void* MemoryBackend::reallocate( void* pBlock, size_t newSize ) noexcept
    {
        if ( newSize == 0 )
        {
            free( pBlock );
            return nullptr;
        }
#if defined( SW_PLATFORM_WINDOWS )
        return _aligned_realloc( pBlock, newSize, kDefaultBlockAlignment );
#elif defined( SW_PLATFORM_POSIX )
        return ::realloc( pBlock, newSize );
#else
    #error "Unsupported platform"
#endif
    }

    void MemoryBackend::free( void* pBlock ) noexcept
    {
#if defined( SW_PLATFORM_WINDOWS )
        _aligned_free( pBlock );
#elif defined( SW_PLATFORM_POSIX )
        ::free( pBlock );
#else
    #error "Unsupported platform"
#endif
    }

    size_t MemoryBackend::getUsableSize( const void* pBlock ) noexcept
    {
        if ( pBlock == nullptr )
            return 0;
#if defined( SW_PLATFORM_WINDOWS )
        return _aligned_msize( const_cast<void*>( pBlock ), kDefaultBlockAlignment, 0 );
#elif defined( SW_PLATFORM_POSIX )
        return malloc_usable_size( const_cast<void*>( pBlock ) );
#else
    #error "Unsupported platform"
#endif
    }
} // namespace sw
