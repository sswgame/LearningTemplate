/**
 * @file ConstantBufferSlotAllocator.h
 * @brief 작은 상수 버퍼(머티리얼 인스턴스)를 큰 페이지의 고정 크기 칸으로 모으는 할당기의 CPU 쪽입니다(감사 R-4).
 * @details 백엔드를 모른다 — 칸은 (페이지 번호, 오프셋, 등급)이고 부르는 쪽이 페이지 번호마다 RHI 버퍼(`createConstantBuffer( kPageBytes )`)를 하나 갖는다.
 *          등급은 256 · 512 · 1024 · 2048 · 4096 B(256 B 는 DX12 CBV · WebGPU 동적 오프셋의 정렬이라 모든 칸이 정렬된다), 페이지 하나는 한 등급만 담는다(빈 칸 목록이 단순하다).
 *          등급보다 큰 요청은 칸이 없다 — 부르는 쪽이 전용 버퍼를 만든다. 언리얼 `FD3D12PoolAllocator` 의 크기 등급 풀 · Vulkan `FVulkanResourceHeap` 페이지와 같은 자리다.
 *          한 스레드(렌더 스레드)에서만 부른다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /** @brief 할당한 칸 하나입니다. `_pageIndex` 가 무효면 칸이 없다(등급보다 크다). */
    struct ConstantBufferSlot
    {
        static constexpr uint32 kInvalidPage = ~uint32{ 0 };

        uint32 _pageIndex{ kInvalidPage };
        uint32 _offset{ 0 };    ///< 페이지 안 바이트 오프셋(등급 크기의 배수 — 256 B 정렬)
        uint32 _sizeBytes{ 0 }; ///< 칸 크기(등급)

        bool isValid() const { return _pageIndex != kInvalidPage; }
    };

    /**
     * @class ConstantBufferSlotAllocator
     * @brief 크기 등급별 페이지와 빈 칸 목록입니다. 파일 머리말 참고.
     */
    class SW_API ConstantBufferSlotAllocator
    {
    public:
        /** @brief 페이지 하나의 바이트입니다. */
        static constexpr uint32 kPageBytes = 64u * 1024u;
        /** @brief 가장 작은 · 큰 등급입니다(2 의 거듭제곱). */
        static constexpr uint32 kMinSlotBytes = 256u;
        static constexpr uint32 kMaxSlotBytes = 4096u;

        /**
         * @brief @p sizeBytes 를 담는 가장 작은 등급의 칸을 줍니다. 빈 칸이 없으면 새 페이지를 연다(@p outbNewPage 가 켜지고 부르는 쪽이 그 번호의 RHI 버퍼를 만든다).
         * @return 등급보다 크거나 0 이면 무효 칸입니다.
         */
        [[nodiscard]] ConstantBufferSlot allocate( uint32 sizeBytes, bool& outbNewPage );
        /**
         * @brief 칸을 돌려줍니다. 그 페이지의 칸이 모두 비면 페이지를 닫고 true 입니다(부르는 쪽이 그 번호의 RHI 버퍼를 놓는다 — 번호는 다음 새 페이지가 다시 쓴다).
         */
        [[nodiscard]] bool release( const ConstantBufferSlot& slot );
        /**
         * @brief 칸 @p slot 을 @p sizeBytes 를 담는 등급으로 옮깁니다(머티리얼 레이아웃이 커지거나 줄었다). 등급이 같으면 그 칸 그대로입니다.
         * @details 새 칸을 먼저 잡고 옛 칸을 놓는다 — 옛 페이지가 닫혀도(@p outbOldPageClosed) 그 번호가 이번 새 칸에 다시 쓰이지 않아, 부르는 쪽이 옛 RHI 버퍼를 놓고
         *          새 RHI 버퍼를 만드는 순서가 엇갈리지 않는다. 새 크기가 등급보다 크면 무효 칸을 돌려준다(옛 칸은 놓았다 — 부르는 쪽이 전용 버퍼를 만든다).
         */
        [[nodiscard]] ConstantBufferSlot reallocate( const ConstantBufferSlot& slot, uint32 sizeBytes, bool& outbNewPage, bool& outbOldPageClosed );

        /** @brief 열린 페이지 수입니다(백엔드 할당 수 — 측정 · 테스트). */
        uint32 getOpenPageCount() const;
        /** @brief @p sizeBytes 를 담는 등급의 칸 크기입니다. 등급보다 크면 0 입니다. */
        static uint32 findSlotBytes( uint32 sizeBytes );

    private:
        /** @brief 페이지 하나입니다. 닫힌 페이지는 `_slotBytes` 0 이다. */
        struct Page
        {
            vector<uint32> _listFreeOffset; ///< 빈 칸 오프셋(뒤에서 꺼낸다)
            uint32         _slotBytes{ 0 };
            uint32         _usedCount{ 0 };
        };

    private:
        vector<Page> _listPage;
    };
} // namespace sw
