/**
 * @file VulkanRHIRenderPassCache.h
 * @brief Vulkan 만 갖는 렌더패스 · 프레임버퍼 캐시입니다(PSO 호환 렌더패스, 합성(MRT · 컬러+깊이) 프레임버퍼, desc 로 만든 렌더패스).
 * @details 다른 API 는 렌더타깃을 그때그때 바인딩하지만 Vulkan 은 렌더패스와 프레임버퍼를 **미리 만들어** 두고 호환되는 것을
 *          골라 써야 합니다. 그 "만들어 둔 것" 의 소유자가 여기입니다. 예전에는 키 · 레코드 타입 여섯과 맵 셋 · 뮤텍스가 `VulkanRHIDevice`
 *          안에 있었고(헤더 멤버 92 개 중 절반), 파괴 순서는 shutdown 두 자리에 나뉘어 있었습니다.
 *
 *          스왑체인 렌더패스(CLEAR/LOAD 변종)와 공용 오프스크린 렌더패스는 캐시가 아니라 디바이스 상태라 여기 없습니다.
 *          그것들은 스왑체인 · 텍스처 레코드의 수명을 따릅니다.
 */
#pragma once
#include "Core/Concurrency/mutex.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHIHandle.h"

namespace sw
{
    /**
     * @class VulkanRHIRenderPassCache
     * @brief 키로 찾고 없으면 만듭니다. 파괴는 `destroyAll` 한 곳이고, 소유가 아닌 스왑체인 렌더패스만 건너뜁니다.
     */
    class VulkanRHIRenderPassCache
    {
    public:
        /**
         * @brief VkRenderPass 하나의 서술입니다. 렌더패스를 만드는 모든 자리가 이것을 채워 `createFromSpec` 에 넘깁니다.
         * @details 예전에는 스왑체인(CLEAR/LOAD) · 공용 오프스크린 · 포맷별 오프스크린 · PSO 호환용 · 합성 프레임버퍼 ·
         *          desc 기반 생성이 각자 `VkAttachmentDescription` 과 서브패스 의존성을 손으로 적었습니다(같은 40줄이 6벌).
         *          의존성 마스크가 자리마다 조금씩 달랐고 한 곳을 고치면 나머지를 빠뜨렸습니다. 단일 서브패스 · 샘플 1 ·
         *          스텐실 DONT_CARE 는 여기서 고정이고, 자리마다 다른 것(포맷 · loadOp · storeOp · 레이아웃)만 필드입니다.
         */
        struct RenderPassSpec
        {
            uint32 _colorCount{ 0 };
            uint32 _arrColorFormat[kMaxColorAttachments]{};        ///< VkFormat
            uint32 _arrColorLoadOp[kMaxColorAttachments]{};        ///< VkAttachmentLoadOp
            uint32 _arrColorStoreOp[kMaxColorAttachments]{};       ///< VkAttachmentStoreOp
            uint32 _arrColorInitialLayout[kMaxColorAttachments]{}; ///< VkImageLayout
            uint32 _arrColorFinalLayout[kMaxColorAttachments]{};   ///< VkImageLayout
            uint32 _depthFormat{ 0 };                              ///< VkFormat; 0 = 깊이 없음
            uint32 _depthLoadOp{ 0 };                              ///< VkAttachmentLoadOp
            uint32 _depthInitialLayout{ 0 };                       ///< VkImageLayout
            uint32 _depthFinalLayout{ 0 };                         ///< VkImageLayout

            /** @brief 컬러 첨부 하나를 덧붙입니다(storeOp 은 STORE. 다르게 쓰려면 `_arrColorStoreOp` 을 뒤에 고칩니다). */
            void addColor( uint32 vkFormat, uint32 loadOp, uint32 initialLayout, uint32 finalLayout );
            /** @brief 깊이 첨부를 둡니다. */
            void setDepth( uint32 vkFormat, uint32 loadOp, uint32 initialLayout, uint32 finalLayout );
        };

        /// @brief 합성 프레임버퍼 캐시 키입니다(컬러 · 깊이 핸들 + loadOp).
        struct CompositeKey
        {
            RHITextureHandle _arrColor[kMaxColorAttachments]{};
            uint32           _colorCount{ 0 };
            RHITextureHandle _depth{ 0 };
            uint8            _arrColorLoadOp[kMaxColorAttachments]{};
            uint8            _depthLoadOp{ 0 };
            /** @brief 키가 같으면 true 를 반환합니다. */
            bool operator==( const CompositeKey& other ) const;
        };

        /// @brief CompositeKey 의 해시입니다.
        struct CompositeKeyHash
        {
            /** @brief 키의 해시를 반환합니다. */
            size_t operator()( const CompositeKey& key ) const;
        };

        /// @brief 캐시된 합성 프레임버퍼입니다. 렌더패스와 프레임버퍼를 함께 소유합니다.
        struct CompositeRecord
        {
            VkRenderPass  _renderPass{ nullptr };
            VkFramebuffer _framebuffer{ nullptr };
            uint32        _width{ 0 };
            uint32        _height{ 0 };
        };

        /// @brief PSO 에 묶인 렌더패스 포맷 키입니다. 파이프라인은 이것과 "호환되는" 렌더패스 어디서든 쓰입니다.
        struct PipelineKey
        {
            uint32 _colorCount{ 1 };
            uint32 _arrColorFormat[kMaxColorAttachments]{}; ///< VkFormat
            uint32 _depthFormat{ 0 };                       ///< VkFormat; 0 = 깊이 없음
            /** @brief 키가 같으면 true 를 반환합니다. */
            bool operator==( const PipelineKey& other ) const;
        };

        /// @brief PipelineKey 의 해시입니다.
        struct PipelineKeyHash
        {
            /** @brief 키의 해시를 반환합니다. */
            size_t operator()( const PipelineKey& key ) const;
        };

        /// @brief `IRHIResource::createRenderPass( desc )` 가 만든 렌더패스와 소유권입니다(스왑체인 RP 별칭은 파괴하지 않습니다).
        struct RenderPassRecord
        {
            VkRenderPass _renderPass{ nullptr };
            uint8        _bOwned{ 0 }; ///< 1 = desc 로 만들어 소유, 0 = 스왑체인 RP 별칭
        };

        VulkanRHIRenderPassCache();
        ~VulkanRHIRenderPassCache() = default;

        VulkanRHIRenderPassCache( const VulkanRHIRenderPassCache& )            = delete;
        VulkanRHIRenderPassCache& operator=( const VulkanRHIRenderPassCache& ) = delete;

        /** @brief 서술대로 VkRenderPass 를 만듭니다. 실패하면 VK_NULL_HANDLE 입니다. 캐시하지 않고, 부르는 쪽이 소유합니다. */
        static VkRenderPass createFromSpec( VkDevice device, const RenderPassSpec& spec );

        /** @brief PSO 호환 렌더패스를 키로 찾고, 없으면 spec 으로 만들어 넣습니다. 실패하면 VK_NULL_HANDLE 입니다. */
        VkRenderPass ensurePipelineRenderPass( VkDevice device, const PipelineKey& key, const RenderPassSpec& spec );

        /**
         * @brief 합성 프레임버퍼를 키로 찾고, 없으면 spec 과 첨부 뷰로 렌더패스 · 프레임버퍼를 만들어 넣습니다.
         * @details 조회와 생성이 **한 임계구역**입니다. RenderGraph::executeParallel 이 여러 스레드에서 동시에 beginRenderPass 를
         *          부르므로, 나눠 놓으면 같은 키를 둘이 만들어 하나가 샙니다. 첨부 뷰는 부르는 쪽이 미리 풀어 넘깁니다.
         */
        bool ensureComposite( VkDevice device, const CompositeKey& key, const RenderPassSpec& spec, const VkImageView* pAttachmentView,
                              uint32 attachmentCount, uint32 width, uint32 height, CompositeRecord& outRecord );
        /**
         * @brief 이 텍스처를 쓰는 합성 프레임버퍼를 캐시에서 떼어 반환합니다. 파괴는 부르는 쪽이 GPU 펜스 뒤로 미룹니다.
         * @details 실행 중인 커맨드버퍼가 아직 참조할 수 있어 여기서 바로 부수면 안 됩니다(게임뷰 리사이즈가 대표 경로).
         */
        void detachCompositesUsing( RHITextureHandle texture, vector<CompositeRecord>& outListDetached );

        /** @brief desc 로 만든(또는 스왑체인 RP 를 별칭한) 렌더패스를 등록하고 핸들(인덱스+1)을 반환합니다. */
        RHIRenderPassHandle addRenderPassRecord( VkRenderPass renderPass, bool bOwned );
        /** @brief 핸들의 레코드를 반환합니다. 범위 밖이면 nullptr 입니다. */
        RenderPassRecord* resolveRenderPassRecord( RHIRenderPassHandle handle );
        /** @brief 핸들의 렌더패스를 파괴합니다. 소유한 것만 부수고 스왑체인 RP 는 건너뜁니다. 레코드는 비웁니다. */
        void destroyRenderPassRecord( VkDevice device, RHIRenderPassHandle handle, VkRenderPass swapchainRenderPass );

        /** @brief 모두 파괴하고 비웁니다(디바이스가 살아 있을 때). 스왑체인 RP 별칭은 소유가 아니라 건너뜁니다. */
        void destroyAll( VkDevice device, VkRenderPass swapchainRenderPass );

    private:
        /// @brief 합성 프레임버퍼 캐시를 보호합니다. 여러 스레드가 동시에 beginRenderPass 를 부릅니다.
        mutable mutex                                                  _compositeMutex;
        unordered_map<CompositeKey, CompositeRecord, CompositeKeyHash> _mapComposite;
        unordered_map<PipelineKey, VkRenderPass, PipelineKeyHash>      _mapPipelineRenderPass;
        vector<RenderPassRecord>                                       _listRenderPassRecord;
    };
} // namespace sw
