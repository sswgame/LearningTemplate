#include "pch.h"

#include "Core/Memory/MemoryBackend.h"

#include <mimalloc.h>

// 힙 백엔드 — mimalloc(MIT, `SW_MEMORY_BACKEND=mimalloc`). 이 파일과 짝 백엔드 파일 가운데 하나만 링크된다(Source/Core/CMakeLists.txt).
// mimalloc 헤더를 include 하는 곳은 이 파일 하나다. 전역 `malloc` · `operator new` 를 덮어쓰지 않는다(포트의 `override` 기능을 켜지 않고, Windows 의
// mimalloc-redirect.dll 이 따라오지 않게 정적으로 링크한다, ThirdParty/mimalloc/vcpkg-port). 그래서
// 서드파티 라이브러리는 자기 CRT 힙을 그대로 쓰고, 엔진 블록과 서로 풀 일이 없다(언리얼도 FMalloc 은 엔진 입구만 바꾼다).
// 힙은 Core 를 담는 모듈(개발: Engine, 정적 모듈 구성: App) 하나에만 있다 — 다른 모듈은 `SW_API` 인 `Memory` 로만 잡고 풀어 어느 모듈에서 풀어도 같은 힙이고,
// 게임 모듈을 내려도(핫 리로드) 힙과 그 모듈이 잡은 블록은 남는다.
// 스레드 캐시는 mimalloc 이 스레드 종료 때 스스로 거둔다(Windows TLS 콜백 · POSIX pthread 키) — 그 모듈의 `thread_local` 소멸자보다 뒤다. 더 늦게 푸는
// 블록(실행 파일의 `thread_local`)은 다른 스레드의 해제로 처리된다. 프로세스 종료 정리는 그 모듈의 정적 소멸자보다 뒤이고(Windows DLL 은 CRT 초기화 맨 앞의
// atexit, 실행 파일은 TLS 콜백) 살아 있는 블록의 페이지는 돌려주지 않는다 — 종료 시 누수 보고는 블록 헤더를 세는 `MemoryProfiler` 몫이라 백엔드와 무관하다.

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

    bool MemoryBackend::isInCRTHeap() noexcept
    {
        return false;
    }
} // namespace sw
