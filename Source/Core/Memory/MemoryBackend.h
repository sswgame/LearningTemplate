/**
 * @file MemoryBackend.h
 * @brief 엔진 할당의 맨 아래 — 힙에서 블록을 잡고 돌려주는 한 벌입니다(언리얼 `FMalloc` 자리).
 */
#pragma once
#include "Core/Foundation/StdHeaders.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /**
     * @brief 힙 백엔드 계약입니다. 엔진 코드는 이것을 부르지 않고 `Memory` 를 부릅니다 — 부르는 곳은 `Memory.cpp` 와 계약 테스트 · 할당 벤치뿐입니다.
     * @details 구현은 CMake 선택 `SW_MEMORY_BACKEND` 가 고른 `.cpp` 하나만 링크됩니다(`Source/Core/CMakeLists.txt`). 가상 호출이 아니라 링크 시점 선택이라
     *          할당마다 간접 호출이 없습니다. 언리얼은 `FMalloc*` 가상 호출로 고르고, 여기서는 실행 중에 바꿀 일이 없어 링크로 고정합니다.
     *          이 헤더에는 어느 백엔드인지가 나오지 않습니다 — 백엔드를 바꿔도 이 헤더와 그 아래 코드는 다시 컴파일할 것이 없습니다.
     *          `SW_API` 를 붙이지 않습니다: 개발 빌드에서 Engine.dll 밖(게임 · 에디터 모듈)은 이 함수를 볼 수 없습니다.
     *
     *          **해제 길은 하나입니다** — `allocate` · `reallocate` · `allocateAligned` 블록을 모두 `free` 로 풉니다. 짝을 고르는 해제가 둘이면 섞어 풀 때
     *          백엔드에 따라 힙이 깨졌다(CRT 의 Windows 정렬 블록). 그래서 CRT 백엔드는 Windows 에서 모든 블록을 정렬 힙(`_aligned_*`)에서 잡는다.
     *          모든 함수는 어느 스레드에서 불러도 되고, 다른 스레드가 잡은 블록을 풀어도 됩니다.
     */
    struct MemoryBackend
    {
        /**
         * @brief @p size 바이트 블록을 잡습니다. 실패하면 nullptr 입니다.
         * @details 정렬은 `malloc` 과 같습니다 — 크기가 `__STDCPP_DEFAULT_NEW_ALIGNMENT__` 이상이면 그 경계에 놓입니다.
         */
        [[nodiscard]] static void* allocate( size_t size ) noexcept;

        /**
         * @brief @p alignment 경계에 놓인 @p size 바이트 블록을 잡습니다. 실패하면 nullptr 입니다. 풀기는 `free` 입니다.
         * @param alignment 2 의 거듭제곱이고 `sizeof( void* )` 이상이어야 합니다(`Memory::allocateAligned` 가 올려 맞춘다).
         */
        [[nodiscard]] static void* allocateAligned( size_t size, size_t alignment ) noexcept;

        /**
         * @brief `allocate` 블록의 크기를 바꿉니다. 앞쪽 `min( 옛 크기, newSize )` 바이트는 그대로 옮겨집니다.
         * @details @p pBlock 이 nullptr 이면 `allocate( newSize )` 입니다. @p newSize 가 0 이면 블록을 풀고 nullptr 을 돌려줍니다
         *          (C `realloc( p, 0 )` 은 구현마다 달라 계약으로 못박는다). 실패하면 nullptr 이고 @p pBlock 은 그대로 살아 있습니다.
         *          `allocateAligned` 블록에는 쓰지 않습니다.
         */
        [[nodiscard]] static void* reallocate( void* pBlock, size_t newSize ) noexcept;

        /** @brief `allocate` · `reallocate` · `allocateAligned` 블록을 풉니다. nullptr 이면 아무것도 하지 않습니다. */
        static void free( void* pBlock ) noexcept;

        /**
         * @brief `allocate` · `reallocate` 블록에 실제로 쓸 수 있는 바이트 수입니다(요청한 크기 이상 — 크기 등급으로 올림된 값).
         * @details 언리얼 `FMalloc::GetAllocationSize` 자리입니다. `allocateAligned` 블록에는 쓰지 않습니다(CRT Windows 는 기본 정렬로 잡은 블록만 잰다).
         */
        [[nodiscard]] static size_t getUsableSize( const void* pBlock ) noexcept;
    };
} // namespace sw
