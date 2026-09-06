#pragma once
#include "Core/Common/EnumUtil.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
    /**
     * @class IRHIResource
     * @brief RHI 버퍼, 텍스처, PSO, 렌더 패스 등의 생성/파괴를 담당하는 인터페이스
     */
    class SW_API IRHIResource
    {
    public:
        IRHIResource()                                 = default;
        virtual ~IRHIResource()                        = default;
        IRHIResource( const IRHIResource& )            = delete;
        IRHIResource& operator=( const IRHIResource& ) = delete;

        // ------------------------------------------------------------------------------
        // 리소스 — PSO, 렌더 패스, 버퍼, 텍스처
        // ------------------------------------------------------------------------------
        /** @brief 그래픽스 파이프라인 상태(PSO)를 만듭니다. */
        virtual RHIPipelineStateHandle createPipelineState( const RHIPipelineStateDesc& desc ) = 0;

        /** @brief 컴퓨트 파이프라인 상태(PSO)를 만듭니다. */
        virtual RHIPipelineStateHandle createComputePipelineState( string_view shaderPath, string_view entryPoint = "CSMain" ) = 0;

        /** @brief 파이프라인 상태 객체를 해제합니다. */
        virtual void destroyPipelineState( RHIPipelineStateHandle pso ) = 0;

        /** @brief 렌더 패스 객체를 만듭니다. */
        virtual RHIRenderPassHandle createRenderPass( const RHIRenderPassDesc& desc ) = 0;

        /** @brief 렌더 패스 객체를 해제합니다. */
        virtual void destroyRenderPass( RHIRenderPassHandle pass ) = 0;

        /** @brief 상수 버퍼를 만듭니다. */
        virtual RHIBufferHandle createConstantBuffer( uint32 size ) = 0;

        /** @brief 상수 버퍼 데이터를 갱신합니다. */
        virtual void updateConstantBuffer( RHIBufferHandle buffer, const void* pData, uint32 size ) = 0;

        /** @brief Structured / Storage 버퍼를 만듭니다. */
        virtual RHIBufferHandle createStructuredBuffer( uint32 elementSize, uint32 elementCount ) = 0;

        /** @brief Structured / Storage 버퍼 데이터를 갱신합니다. */
        virtual void updateStructuredBuffer( RHIBufferHandle buffer, const void* pData, uint32 size ) = 0;

        /** @brief 불변/정적 버텍스 버퍼 (POSITION+COLOR 레이아웃). */
        virtual RHIBufferHandle createVertexBuffer( const void* pData, uint32 sizeBytes ) = 0;

        /** @brief 범용 버퍼 (structured / UAV / 인디렉트 인자 / 인덱스). */
        virtual RHIBufferHandle createBuffer( const RHIBufferDesc& desc )
        {
            if ( EnumUtil::hasFlag( desc._usage, RHIBufferUsage::Vertex ) && desc._pInitialData != nullptr && desc._sizeBytes > 0 )
                return createVertexBuffer( desc._pInitialData, desc._sizeBytes );
            if ( EnumUtil::hasFlag( desc._usage, RHIBufferUsage::Constant ) )
                return createConstantBuffer( desc._sizeBytes > 0 ? desc._sizeBytes : 256u );
            const uint32    elemSize  = desc._elementSize > 0 ? desc._elementSize : 4u;
            const uint32    elemCount = desc._elementCount > 0
                                          ? desc._elementCount
                                          : ( desc._sizeBytes > 0 ? ( desc._sizeBytes / elemSize ) : 1u );
            RHIBufferHandle h         = createStructuredBuffer( elemSize, elemCount );
            if ( h != 0 && desc._pInitialData != nullptr && desc._sizeBytes > 0 )
                updateStructuredBuffer( h, desc._pInitialData, desc._sizeBytes );
            return h;
        }

        /** @brief 인덱스 버퍼 (uint16/uint32). */
        virtual RHIBufferHandle createIndexBuffer( const void* pData, uint32 sizeBytes, uint32 indexStride = 4 )
        {
            (void)indexStride;
            RHIBufferDesc desc{};
            desc._sizeBytes    = sizeBytes;
            desc._usage        = RHIBufferUsage::Index | RHIBufferUsage::ShaderResource;
            desc._pInitialData = pData;
            return createBuffer( desc );
        }

        /** @brief GPU 버퍼 리소스를 삭제합니다. */
        virtual void destroyBuffer( RHIBufferHandle buffer ) = 0;

        /** @brief 2D 텍스처 (RenderTarget 포함)를 만듭니다. */
        virtual RHITextureHandle createTexture2D( const RHITextureDesc& desc ) = 0;

        /** @brief GPU 텍스처 리소스를 삭제합니다. */
        virtual void destroyTexture( RHITextureHandle texture ) = 0;

        /**
         * @brief 2D 텍스처에 픽셀을 올립니다 — 밉 0 부터 차례로, 행은 빈틈없이(DDS 배치).
         * @details 로드 시점용 동기 경로다. 반환했을 때 DX12/Vulkan 은 복사가 GPU 큐에서 뒤이은 드로우보다
         *          앞서도록 제출돼 있고(Vulkan 은 대기까지 함), DX11/GL 은 즉시 컨텍스트에 들어가 있다.
         *          매 프레임 갱신 용도가 아니다. createTexture2D 가 _bIsShaderResource 로 만든 비압축 컬러
         *          포맷만 받는다 — 압축(BC) 포맷은 RHIFormat 에 아직 없다(getRHIFormatBytesPerPixel 참고).
         *          밉 크기·오프셋 규칙은 resolveTextureUploadMips 한 곳이 정한다.
         * @return 포맷이 업로드 불가이거나 데이터가 모자라면 false.
         */
        virtual bool uploadTexture2D( RHITextureHandle texture, const RHITextureUploadDesc& desc ) = 0;

        // ------------------------------------------------------------------------------
        // Bindless — 텍스처/버퍼/UAV 등록과 해제
        // ------------------------------------------------------------------------------
        /**
         * @brief Bindless 테이블에 텍스처를 등록하고 SRV 인덱스를 발급합니다.
         * @details 텍스처 인덱스와 버퍼 인덱스는 **서로 다른 공간**입니다. DX12 만 하나의 셰이더 가시
         *          힙을 공유하고, DX11/OpenGL/Vulkan 은 텍스처 표와 버퍼 표를 따로 둡니다 — 같은 정수가
         *          양쪽에서 각각 다른 리소스를 가리킬 수 있습니다. 그래서 해제도 종류별로 나뉩니다.
         */
        virtual RHIDescriptorIndex registerBindlessTexture( RHITextureHandle texture ) = 0;

        /**
         * @brief registerBindlessTexture 가 발급한 텍스처 SRV 인덱스를 해제합니다.
         * @details 버퍼 인덱스를 여기에 넘기거나 텍스처 인덱스를 unregisterBindlessResource 에 넘기면
         *          안 됩니다. 후자는 실제로 있었던 사고입니다 — 트랜지언트 텍스처 SRV 0·1·2 가 버퍼
         *          프리리스트로 들어가 살아 있는 패스 상수버퍼 슬롯 0·1·2 를 비운 것으로 만들었고, 다음에
         *          등록된 인스턴스 구조버퍼가 슬롯 2 를 차지해 Vulkan set 0 에 STORAGE 세트가 걸렸습니다.
         *          destroyTexture 는 등록을 스스로 정리하므로 파괴 직전이라면 이 호출은 생략해도 됩니다.
         */
        virtual void unregisterBindlessTexture( RHIDescriptorIndex index ) = 0;

        /** @brief Bindless 테이블에 버퍼를 등록하고 인덱스를 발급합니다. */
        virtual RHIDescriptorIndex registerBindlessResource( RHIBufferHandle buffer ) = 0;

        /**
         * @brief registerBindlessResource 가 발급한 **버퍼** 인덱스를 해제합니다.
         * @details 이미 비어 있는 슬롯(다른 종류의 인덱스, 이중 해제)은 프리리스트에 다시 넣지 않습니다 —
         *          한 번이라도 넣으면 같은 인덱스가 두 리소스에 발급돼 조용히 엉뚱한 버퍼가 바인딩됩니다.
         */
        virtual void unregisterBindlessResource( RHIDescriptorIndex index ) = 0;

        /** @brief Bindless UAV를 등록하고 인덱스를 발급합니다. */
        virtual RHIDescriptorIndex registerBindlessUAV( RHIBufferHandle buffer ) = 0;

        /** @brief Bindless UAV 등록을 해제합니다. */
        virtual void unregisterBindlessUAV( RHIDescriptorIndex index ) = 0;
    };
} // namespace sw
