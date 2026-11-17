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

namespace sw
{
    void* MemoryBackend::allocate( size_t size ) noexcept
    {
        return ::malloc( size );
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
            ::free( pBlock );
            return nullptr;
        }
        return ::realloc( pBlock, newSize );
    }

    void MemoryBackend::free( void* pBlock ) noexcept
    {
        ::free( pBlock );
    }

    void MemoryBackend::freeAligned( void* pBlock ) noexcept
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
        return _msize( const_cast<void*>( pBlock ) );
#elif defined( SW_PLATFORM_POSIX )
        return malloc_usable_size( const_cast<void*>( pBlock ) );
#else
    #error "Unsupported platform"
#endif
    }
} // namespace sw
