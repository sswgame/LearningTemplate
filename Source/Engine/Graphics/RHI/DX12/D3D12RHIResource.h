#pragma once
#include "Core/Common/Types.h"

#include "Engine/Graphics/RHI/IRHIResource.h"

#include <shared_mutex>

#if defined( SW_PLATFORM_WINDOWS )
namespace sw
{
    class D3D12RHIDevice;

    class D3D12RHIResource : public IRHIResource
    {
    public:
        explicit D3D12RHIResource( D3D12RHIDevice* pDevice )
            : _pDevice{ pDevice } {}
        RHIPipelineStateHandle createPipelineState( const RHIPipelineStateDesc& desc ) override;
        RHIPipelineStateHandle createComputePipelineState( string_view shaderPath, string_view entryPoint = "CSMain" ) override;
        void                   destroyPipelineState( RHIPipelineStateHandle pso ) override;
        RHIRenderPassHandle    createRenderPass( const RHIRenderPassDesc& desc ) override;
        void                   destroyRenderPass( RHIRenderPassHandle pass ) override;
        RHIBufferHandle        createConstantBuffer( uint32 size ) override;
        void                   updateConstantBuffer( RHIBufferHandle buffer, const void* pData, uint32 size ) override;
        RHIBufferHandle        createStructuredBuffer( uint32 elementSize, uint32 elementCount ) override;
        void                   updateStructuredBuffer( RHIBufferHandle buffer, const void* pData, uint32 size ) override;
        RHIBufferHandle        createVertexBuffer( const void* pData, uint32 sizeBytes ) override;
        void                   destroyBuffer( RHIBufferHandle buffer ) override;
        RHITextureHandle       createTexture2D( const RHITextureDesc& desc ) override;
        void                   destroyTexture( RHITextureHandle texture ) override;
        bool                   uploadTexture2D( RHITextureHandle texture, const RHITextureUploadDesc& desc ) override;
        bool                   readbackTexture2D( RHITextureHandle texture, uint32 mip, vector<uint8>& outBytes, RHITextureMipSpan& outLayout ) override;
        RHIFormat              getTextureFormat( RHITextureHandle texture ) const override;
        RHIDescriptorIndex     registerBindlessTexture( RHITextureHandle texture ) override;
        void                   unregisterBindlessTexture( RHIDescriptorIndex index ) override;
        RHIDescriptorIndex     registerBindlessResource( RHIBufferHandle buffer ) override;
        void                   unregisterBindlessResource( RHIDescriptorIndex index ) override;
        RHIDescriptorIndex     registerBindlessUav( RHIBufferHandle buffer ) override;
        RHIDescriptorIndex     registerBindlessTextureUav( RHITextureHandle texture ) override;
        void                   unregisterBindlessUav( RHIDescriptorIndex index ) override;

    private:
        /**
         * @brief bindless 힙에서 디스크립터 인덱스를 하나 집습니다.
         * @param lock 호출자가 **이미 쥔** `_bindlessMutex` 잠금.
         * @return 힙이 가득 찼으면 `kInvalidDescriptorIndex`.
         * @details 잠그지 않고 이미 쥔 것을 받는 이유: 인덱스를 집는 것과 **그 자리에 뷰를 만드는 것**이
         *          한 임계 구역이어야 한다. 여기서 잠갔다 풀면 그 틈에 다른 스레드가 같은 인덱스를 받는다.
         *          인자로 받으면 그 전제가 컴파일러에게도 호출부에게도 보인다.
         * @note 네 등록 함수(`registerBindlessTexture` · `registerBindlessResource` · `registerBindlessUav` ·
         *       `registerBindlessTextureUav`)가 **같은 열두 줄을 각자** 갖고 있었다. 용량 검사와 프리리스트
         *       정책이 네 벌이면, 힙을 키우거나 회수 규칙을 바꿀 때 한 곳만 고치고 넘어가기 쉽다 —
         *       그리고 그 결과는 **디스크립터가 어긋나 화면이 조용히 깨지는 것**이다.
         */
        RHIDescriptorIndex acquireBindlessIndex( const std::unique_lock<std::shared_mutex>& lock );

        /**
         * @brief 어느 bindless 등록부인가 — **힙 인덱스 공간은 하나**지만 기록은 둘로 나뉩니다.
         * @details 둘이 같은 프리리스트를 쓰므로(`_listFreeBindless`) 인덱스는 섞이지 않는다.
         *          나뉘는 것은 "그 슬롯이 무엇을 들고 있었나" 뿐이다.
         */
        enum class BindlessRegistry : uint8
        {
            ShaderResource, ///< SRV/CBV 등록부.
            UnorderedAccess ///< UAV 등록부.
        };

        /**
         * @brief 등록부의 한 슬롯을 비우고 인덱스를 펜스 뒤 회수에 맡깁니다.
         * @details **이미 빈 슬롯을 다시 돌려주면 같은 인덱스가 두 리소스에 발급된다** — 그 검사가
         *          SRV/CBV 쪽과 UAV 쪽에 한 벌씩 복사돼 있었다. 둘은 등록부와 로그 문구만 달랐다.
         */
        void releaseBindlessRecord( BindlessRegistry registry, RHIDescriptorIndex index );

        /** @brief 텍스처/버퍼 공용 힙 슬롯을 비우고 프리리스트에 돌려줍니다(빈 슬롯은 무시). */
        void releaseBindlessSlot( RHIDescriptorIndex index );
        /** @brief 인덱스를 GPU 펜스 뒤에 프리리스트로 돌려보냅니다 — 실행 중인 리스트가 새 리소스를 읽지 않도록. */
        void deferFreeBindlessIndex( RHIDescriptorIndex index );

        /**
         * @brief 현재 프레임 링 슬롯의 스테이징 힙에서 sizeBytes 를 bump 할당하고 복사 리스트를 열어 둡니다.
         * @details 얼로케이터는 펜스 구간이 바뀔 때만 Reset 한다 — 같은 구간의 앞선 복사가 GPU 에서 도는 중일 수
         *          있다. 성공하면 `_pDevice->_arrStructuredUploadSlot[outSlotIndex]._copyCommandList` 에 기록하고
         *          submitUploadSlot 으로 닫는다. updateStructuredBuffer / uploadTexture2D 공용.
         */
        bool acquireUploadStaging( uint64 sizeBytes, uint64 alignment, uint32& outSlotIndex, uint64& outOffset, void*& pOutMapped );
        /** @brief 현재 프레임 링 슬롯의 복사 리스트만 엽니다(스테이징 없이 — readback 처럼 소스가 다른 곳일 때). */
        bool openUploadSlot( uint32& outSlotIndex );
        /** @brief acquireUploadStaging / openUploadSlot 으로 연 복사 리스트를 닫고 그래픽스 큐에 제출합니다. */
        void submitUploadSlot( uint32 slotIndex );
        /** @brief 지금까지 큐에 넣은 작업이 끝날 때까지 CPU 를 세웁니다(readback 전용 — 프레임 경로에서 부르지 말 것). */
        bool waitForQueueDrain();

    public:
    private:
        D3D12RHIDevice* _pDevice;
    };
} // namespace sw
#endif
