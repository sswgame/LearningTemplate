/**
 * @file VulkanMemoryAllocator.h
 * @brief Vulkan Memory Allocator(VMA)로 버퍼 · 이미지와 그 장치 메모리를 만드는 자리입니다(Vulkan · VMA 헤더 없이 읽힌다).
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Graphics/RHI/Vulkan/VulkanRHIHandle.h"

namespace sw
{
    struct RHIMemoryBudget;

    /** @brief Vulkan 버퍼의 메모리 자리입니다. 기본값이 없다 — 만드는 쪽이 CPU 가 무엇을 하는지로 고른다. */
    enum class VulkanBufferMemory : uint8
    {
        HostWrite,    ///< CPU 가 매핑해 순서대로 쓴다(상수 링 · 업로드 스테이징). 쓴 뒤 `VulkanMemoryAllocator::flush`
        HostRead,     ///< GPU 가 쓰고 CPU 가 매핑해 읽는다(되읽기). 읽기 전 `VulkanMemoryAllocator::invalidate`
        HostCoherent, ///< `HostRead` 이되 일관 메모리여야 한다 — 장치를 잃은 뒤에도 매핑만으로 읽는 브레드크럼
        DeviceLocal   ///< GPU 가 그리며 읽는다(정점 · 인덱스 · 구조 버퍼). 내용은 스테이징 복사로 들어온다
    };
} // namespace sw

namespace sw
{
    /**
     * @struct VulkanAllocation
     * @brief VMA 할당 하나입니다. 버퍼 · 이미지를 만든 것과 함께 놓는다(`VulkanMemoryAllocator::destroyBuffer` · `destroyImage`).
     */
    struct VulkanAllocation
    {
        VmaAllocation _allocation{ nullptr };
        uint8*        _pMapped{ nullptr }; ///< 호스트가 보는 자리면 할당의 시작(할당이 사는 동안 매핑해 둔다), 아니면 nullptr
        uint64        _sizeBytes{ 0 };     ///< 할당 크기(드라이버 요구 크기 이상 — 메모리 추적기가 세는 값)

        bool isValid() const { return _allocation != nullptr; }
    };
} // namespace sw

namespace sw
{
    /**
     * @class VulkanMemoryAllocator
     * @brief 장치의 VMA 할당기입니다. 블록 나누기 · 전용 할당 · 버퍼와 이미지의 간격(`bufferImageGranularity`) · 예산 · 매핑은 VMA 가 정한다.
     * @details Godot 처럼 VMA 를 그대로 쓴다 — 언리얼 `FVulkanMemoryManager` 는 자체 구현이지만 하는 일(블록 · 전용 · 예산 · 앨리어싱)이 같고, 자체 구현의 까닭(콘솔 · 잔류 제어를 한 코드로)이
     *          이 엔진에는 없다. `vk_mem_alloc.h` 는 이 클래스의 cpp 하나만 본다. VMA 가 스스로 동기화하므로 여러 스레드가 부른다.
     * @note https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/usage_patterns.html
     */
    class VulkanMemoryAllocator final
    {
    public:
        VulkanMemoryAllocator()                                          = default;
        VulkanMemoryAllocator( const VulkanMemoryAllocator& )            = delete;
        VulkanMemoryAllocator& operator=( const VulkanMemoryAllocator& ) = delete;

        /** @brief 논리 장치 위에 할당기를 만듭니다. @p bMemoryBudget 은 장치에 `VK_EXT_memory_budget` 을 켰는가다. */
        [[nodiscard]] bool initialize( VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device, bool bMemoryBudget );
        /** @brief 할당기를 부숩니다(장치 종료 — 자원 · 해제 큐가 모두 놓은 뒤). 남은 할당은 이름과 함께 오류로 남긴다. */
        void shutdown();
        /** @brief 프레임 시작을 알립니다 — VMA 가 예산을 다시 읽는다. */
        void onFrameBegin( uint64 frameSerial );

        /** @brief 버퍼를 만들어 @p placement 의 메모리에 묶습니다. 호스트 자리면 매핑까지 되어 있다. 실패하면 오류를 남기고 두 출력은 비어 있다. */
        [[nodiscard]] bool createBuffer( const VkBufferCreateInfo& info, VulkanBufferMemory placement, const utf8* pName, VkBuffer& outBuffer, VulkanAllocation& outAllocation );
        /** @brief 이미지를 만들어 기기 로컬 메모리에 묶습니다. 전용 할당은 드라이버의 선호(`VkMemoryDedicatedRequirements`)를 VMA 가 따른다. */
        [[nodiscard]] bool createImage( const VkImageCreateInfo& info, const utf8* pName, VkImage& outImage, VulkanAllocation& outAllocation );
        /** @brief 버퍼와 그 할당을 놓습니다(GPU 가 다 쓴 뒤). 빈 것은 아무것도 하지 않는다. */
        void destroyBuffer( VkBuffer buffer, const VulkanAllocation& allocation );
        /** @brief 이미지와 그 할당을 놓습니다(GPU 가 다 쓴 뒤). 앨리어싱 이미지는 빈 할당을 넘긴다. */
        void destroyImage( VkImage image, const VulkanAllocation& allocation );

        /** @brief 이미지를 겹쳐 놓을 기기 로컬 메모리를 전용 블록으로 받습니다(트랜지언트 힙). */
        [[nodiscard]] bool allocateHeap( const VkMemoryRequirements& requirements, const utf8* pName, VulkanAllocation& outAllocation );
        /** @brief `allocateHeap` 이 준 메모리를 놓습니다. 빈 할당은 아무것도 하지 않는다. */
        void freeHeap( const VulkanAllocation& allocation );
        /**
         * @brief @p heap 의 @p offsetBytes 에 이미지를 놓습니다. 종류 · 정렬 · 크기가 힙에 맞지 않으면 오류를 남기고 false 입니다.
         * @param outBytes 이미지의 요구 크기입니다.
         */
        [[nodiscard]] bool createAliasingImage( const VulkanAllocation& heap, uint64 offsetBytes, const VkImageCreateInfo& info, VkImage& outImage, uint64& outBytes );

        /** @brief 할당의 진단 이름을 바꿉니다(종료 때 남은 할당의 보고 · VMA 통계가 이 이름을 보인다). */
        void setName( const VulkanAllocation& allocation, const utf8* pName );
        /** @brief CPU 가 쓴 구간을 GPU 에 보이게 합니다(일관 메모리면 VMA 가 아무것도 하지 않는다). 오프셋은 할당의 시작에서 센다. */
        void flush( const VulkanAllocation& allocation, uint64 offsetBytes, uint64 sizeBytes );
        /** @brief GPU 가 쓴 구간을 CPU 가 읽을 수 있게 합니다(일관 메모리면 VMA 가 아무것도 하지 않는다). 오프셋은 할당의 시작에서 센다. */
        void invalidate( const VulkanAllocation& allocation, uint64 offsetBytes, uint64 sizeBytes );

        /** @brief 이 프로세스의 힙별 사용량과 예산입니다(사용량은 모든 힙, 예산 · 남은 양은 기기 로컬 힙). `VK_EXT_memory_budget` 이 없으면 false 입니다(VMA 의 어림값을 드라이버 값으로 내지 않는다). */
        [[nodiscard]] bool queryBudget( RHIMemoryBudget& outBudget ) const;
        /** @brief 살아 있는 `VkDeviceMemory` 수입니다(블록 + 전용 할당 — 장치 한도 `maxMemoryAllocationCount` 의 분자이고 한도는 VMA 가 지킨다). */
        uint32 computeBlockCount() const;

    private:
        VmaAllocator _allocator{ nullptr };
        VkDevice     _device{ nullptr };
        uint8        _bMemoryBudget{ SW_FALSE };
    };
} // namespace sw
