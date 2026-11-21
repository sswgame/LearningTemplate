#include "pch.h"

#include "Core/Memory/MemoryBackend.h"

#include <mimalloc.h>

// 힙 백엔드 — mimalloc(MIT, `SW_MEMORY_BACKEND=mimalloc`). 이 파일과 짝 백엔드 파일 가운데 하나만 링크된다(Source/Core/CMakeLists.txt).
// mimalloc 헤더를 include 하는 곳은 이 파일 하나다. 전역 `malloc` · `operator new` 를 덮어쓰지 않는다(vcpkg 포트의 `override` 기능을 켜지 않는다) —
// 서드파티 라이브러리는 자기 CRT 힙을 그대로 쓰고, 엔진 블록과 서로 풀 일이 없다(언리얼도 FMalloc 은 엔진 입구만 바꾼다).
// 스레드 캐시는 mimalloc 이 스레드 종료 때 스스로 거둔다 — 따로 정리를 부를 자리가 없다.

namespace sw
{
    void* MemoryBackend::allocate( size_t size ) noexcept
    {
        return mi_malloc( size );
    }

    void* MemoryBackend::allocateAligned( size_t size, size_t alignment ) noexcept
    {
        return mi_malloc_aligned( size, alignment );
    }

    void* MemoryBackend::reallocate( void* pBlock, size_t newSize ) noexcept
    {
        if ( newSize == 0 )
        {
            mi_free( pBlock );
            return nullptr;
        }
        return mi_realloc( pBlock, newSize );
    }

    void MemoryBackend::free( void* pBlock ) noexcept
    {
        mi_free( pBlock );
    }

    size_t MemoryBackend::getUsableSize( const void* pBlock ) noexcept
    {
        return mi_usable_size( pBlock );
    }
} // namespace sw
