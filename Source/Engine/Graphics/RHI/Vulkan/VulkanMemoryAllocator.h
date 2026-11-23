/**
 * @file VulkanMemoryAllocator.h
 * @brief 버퍼 · 텍스처의 장치 메모리를 큰 블록에서 나눠 주는 할당기입니다(Vulkan 헤더 없이 읽힌다).
 */
#pragma once
#include "Core/Concurrency/mutex.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Graphics/RHI/Backend/RHIMemoryRangeAllocator.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHIHandle.h"

namespace sw
{
    class VulkanRHIDevice;

    /**
     * @struct VulkanMemoryRange
     * @brief 자원 하나가 쓰는 장치 메모리 구간입니다. 자원은 `_memory` 의 `_offset` 에 묶습니다.
     */
    struct VulkanMemoryRange
    {
        VkDeviceMemory _memory{ nullptr };
        uint64         _offset{ 0 };
        uint64         _sizeBytes{ 0 };
        uint8*         _pMapped{ nullptr };                             ///< 호스트가 보는 메모리면 구간의 시작(블록이 사는 동안 매핑해 둔다), 아니면 nullptr
        uint32         _blockIndex{ constant::invalid_index::kUint32 }; ///< 나눠 준 블록(무효면 전용 할당)

        bool isValid() const { return _memory != nullptr; }
    };

    /**
     * @class VulkanMemoryAllocator
     * @brief 메모리 종류마다 블록을 두고 구간으로 나눠 준다. 블록의 절반보다 큰 요청은 전용 할당이다.
     * @details 자원마다 `vkAllocateMemory` 하나면 자원 수가 장치 한도(`maxMemoryAllocationCount`, 명세 최소 4096)에 닿는 순간 만들지 못한다(언리얼 `FMemoryManager` · 유니티 · VMA 도 블록을 나눈다).
     *          버퍼(선형)와 이미지(최적 배치)는 다른 블록이다 — 같은 블록에 섞으면 `bufferImageGranularity` 만큼 벌려야 한다.
     *          호스트가 보는 블록은 만들 때 한 번 매핑해 둔다 — 같은 `VkDeviceMemory` 를 둘이 매핑할 수 없다. 여러 스레드가 부른다(잠금 하나).
     */
    class VulkanMemoryAllocator final
    {
    public:
        static constexpr uint64 kDeviceBlockBytes = 64ull * 1024ull * 1024ull; ///< 기기 로컬 블록(언리얼 · VMA 의 대형 힙 블록 급)
        static constexpr uint64 kHostBlockBytes   = 16ull * 1024ull * 1024ull; ///< 호스트가 보는 블록

        /** @brief 요청 하나입니다. */
        struct Request
        {
            uint64 _sizeBytes{ 0 };
            uint64 _alignment{ 1 };
            uint32 _memoryTypeIndex{ 0 };
            uint8  _bHostVisible{ SW_FALSE }; ///< 매핑해 둔다
            uint8  _bLinear{ SW_TRUE };       ///< 버퍼면 켠다(이미지면 끈다)
        };

        explicit VulkanMemoryAllocator( VulkanRHIDevice& device );

        VulkanMemoryAllocator( const VulkanMemoryAllocator& )            = delete;
        VulkanMemoryAllocator& operator=( const VulkanMemoryAllocator& ) = delete;

        /** @brief 구간을 받습니다. 블록도 전용 할당도 못 만들면 false 입니다. */
        [[nodiscard]] bool allocate( const Request& request, VulkanMemoryRange& outRange );
        /** @brief 구간을 놓습니다(그 자원을 쓴 GPU 일이 끝난 뒤). 빈 구간은 아무것도 하지 않는다. */
        void release( const VulkanMemoryRange& range );
        /** @brief 블록을 모두 놓습니다(장치 종료 — 구간을 쓰는 자원이 모두 사라진 뒤). */
        void releaseAll();

    private:
        /** @brief 블록 하나입니다. `_memory` 가 비면 놓은 칸이다(다시 쓴다). */
        struct Block
        {
            VkDeviceMemory          _memory{ nullptr };
            uint8*                  _pMapped{ nullptr };
            uint64                  _sizeBytes{ 0 };
            uint32                  _memoryTypeIndex{ 0 };
            uint8                   _bLinear{ SW_TRUE };
            RHIMemoryRangeAllocator _ranges{};
        };

        /** @brief 장치 메모리 하나를 할당하고, @p bHostVisible 이면 매핑합니다. 실패하면 남기는 것이 없다. */
        [[nodiscard]] bool allocateMemory( uint64 sizeBytes, uint32 memoryTypeIndex, bool bHostVisible, VkDeviceMemory& outMemory, uint8*& outMapped );
        /** @brief 블록에서 구간을 떼어 봅니다. */
        [[nodiscard]] bool allocateFromBlock( uint32 blockIndex, const Request& request, VulkanMemoryRange& outRange ) SW_REQUIRES_LOCK( _mutex );

    private:
        VulkanRHIDevice*         _pDevice;
        mutex                    _mutex;
        vector<Block> _listBlock SW_GUARDED_BY( _mutex );
    };
} // namespace sw
