#pragma once
#include "Core/Common/EnumUtil.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
    /**
     * @class IRHIResource
     * @brief RHI 버퍼 · 텍스처 · PSO · 렌더 패스의 생성 · 파괴와 bindless 등록을 맡는 인터페이스입니다.
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

        /**
         * @brief 상수 버퍼 데이터를 갱신합니다.
         * @param size 보낼 바이트 수. **`createConstantBuffer` 에 준 크기를 넘으면 안 됩니다.**
         * @warning 그 전제는 **백엔드가 검사해 주지 않습니다.** DX12 · Vulkan · DX11 은 받은 크기를 그대로
         *          복사하므로, 넘기면 프레임 슬롯 밖(또는 버퍼 밖)까지 씁니다. GL 만 `glBufferSubData`
         *          가 `GL_INVALID_VALUE` 로 막아 줍니다. 즉 **한 백엔드에서만 조용히 안전합니다.**
         *          버퍼가 작아졌다면 갱신하지 말고 **다시 만들어야 합니다**
         *          (`MaterialInstance::updateRhi` 가 셰이더 재컴파일로 레이아웃이 커지는 경우를
         *          그렇게 처리합니다).
         */
        virtual void updateConstantBuffer( RHIBufferHandle buffer, const void* pData, uint32 size ) = 0;

        /** @brief Structured / Storage 버퍼를 만듭니다. */
        virtual RHIBufferHandle createStructuredBuffer( uint32 elementSize, uint32 elementCount ) = 0;

        /**
         * @brief Structured / Storage 버퍼의 **여러 조각**을 한 번에 갱신합니다.
         * @param pBaseSource 조각들의 `_srcOffset` 이 가리키는 원본 블롭.
         *
         * @details 오프셋이 필요한 이유는 하나입니다. **바뀐 것만 올리기 위해서입니다.** 인스턴스 버퍼는
         *          하나만 움직여도 전체를 다시 올리고 있었고, 8000 개 중 10 개만 움직여도 800 개를
         *          움직일 때와 같은 100 us 를 썼습니다(`docs/06_Backlog.md` 2026-09-20).
         *
         * @warning 크기와 마찬가지로 **범위를 백엔드가 검사해 주지 않습니다.** `byteOffset + size` 가
         *          버퍼를 넘으면 버퍼 밖까지 씁니다. 부르는 쪽이 지켜야 합니다.
         */
        virtual void updateStructuredBufferRegions( RHIBufferHandle buffer, const void* pBaseSource,
                                                    const RHIBufferCopyRegion* pRegions, uint32 regionCount ) = 0;

        /** @brief 한 조각만 갱신합니다 (`updateStructuredBufferRegions` 의 영역 1 개). */
        void updateStructuredBufferRange( RHIBufferHandle buffer, const void* pData, uint32 size, uint32 byteOffset )
        {
            const RHIBufferCopyRegion region{ 0, byteOffset, size };
            updateStructuredBufferRegions( buffer, pData, &region, 1 );
        }

        /** @brief Structured / Storage 버퍼를 앞에서부터 갱신합니다 (`updateStructuredBufferRange` 의 오프셋 0). */
        void updateStructuredBuffer( RHIBufferHandle buffer, const void* pData, uint32 size )
        {
            updateStructuredBufferRange( buffer, pData, size, 0 );
        }

        /** @brief 내용을 실어 정점 버퍼를 만듭니다. 레이아웃은 PSO 의 입력 레이아웃(`constant::arrVertexAttribute`)이 정합니다. */
        virtual RHIBufferHandle createVertexBuffer( const void* pData, uint32 sizeBytes ) = 0;

        /**
         * @brief 범용 버퍼(구조 · UAV · 인다이렉트 인자 · 인덱스)를 만듭니다.
         * @details 내용이 있는 `Vertex` 용도는 createVertexBuffer 로, `Index` 용도는 createIndexBuffer 로 넘깁니다
         *          (`_elementSize` 가 2 면 uint16 인덱스).
         */
        virtual RHIBufferHandle createBuffer( const RHIBufferDesc& desc )
        {
            if ( EnumUtil::hasFlag( desc._usage, RHIBufferUsage::Vertex ) && desc._pInitialData != nullptr && desc._sizeBytes > 0 )
                return createVertexBuffer( desc._pInitialData, desc._sizeBytes );
            if ( EnumUtil::hasFlag( desc._usage, RHIBufferUsage::Index ) && desc._pInitialData != nullptr && desc._sizeBytes > 0 )
                return createIndexBuffer( desc._pInitialData, desc._sizeBytes, ( desc._elementSize == 2 ) ? 2u : 4u );
            if ( EnumUtil::hasFlag( desc._usage, RHIBufferUsage::Constant ) )
                return createConstantBuffer( desc._sizeBytes > 0 ? desc._sizeBytes : 256u );
            const uint32    elemSize  = desc._elementSize > 0 ? desc._elementSize : 4u;
            const uint32    elemCount = desc._elementCount > 0
                                          ? desc._elementCount
                                          : ( desc._sizeBytes > 0 ? ( desc._sizeBytes / elemSize ) : 1u );
            RHIBufferHandle buffer    = createStructuredBuffer( elemSize, elemCount );
            if ( buffer != 0 && desc._pInitialData != nullptr && desc._sizeBytes > 0 )
                updateStructuredBuffer( buffer, desc._pInitialData, desc._sizeBytes );
            return buffer;
        }

        /**
         * @brief 내용을 실어 인덱스 버퍼(uint16 · uint32)를 만듭니다. 인덱스 크기는 걸 때(`setIndexBuffer`) 다시 알려 줍니다.
         * @details 백엔드마다 **인덱스 버퍼 용도**로 만들어야 합니다(D3D11_BIND_INDEX_BUFFER · VK_BUFFER_USAGE_INDEX_BUFFER_BIT 등).
         *          예전의 기본 구현은 구조버퍼를 만들었고 DX11 · DX12 · Vulkan 이 그것을 그대로 썼습니다. 구조버퍼는 인덱스 버퍼
         *          용도가 아닙니다(D3D11 은 BUFFER_STRUCTURED 에 BIND_INDEX_BUFFER 를 붙일 수 없고, Vulkan 은 검증 레이어가 용도
         *          위반으로 잡습니다). 드라이버가 받아 줘서 그려진 백엔드도 있었지만 규칙 밖이었습니다. 엔진이 아직 인덱스 메시를
         *          쓰지 않아 드러나지 않았습니다(RHIDeviceTest.IndexedIndirectDrawReadsInstanceSlotStream).
         */
        virtual RHIBufferHandle createIndexBuffer( const void* pData, uint32 sizeBytes, uint32 indexStride = 4 ) = 0;

        /** @brief GPU 버퍼 리소스를 삭제합니다. */
        virtual void destroyBuffer( RHIBufferHandle buffer ) = 0;

        /** @brief 2D 텍스처(렌더 타깃 포함)를 만듭니다. */
        virtual RHITextureHandle createTexture2D( const RHITextureDesc& desc ) = 0;

        /** @brief GPU 텍스처 리소스를 삭제합니다. */
        virtual void destroyTexture( RHITextureHandle texture ) = 0;

        /**
         * @brief 텍스처가 실제로 만들어진 RHIFormat 을 반환합니다(없는 핸들이면 Unknown).
         * @details 렌더 타깃에 그리는 PSO 는 **대상의 실제 포맷**으로 만들어야 합니다. 언리얼이 PSO 초기화자의
         *          RenderTargetFormats 를 바인딩된 FRHITexture::GetFormat() 에서 뽑는 것과 같은 자리입니다.
         *          백버퍼(핸들 0)는 여기가 아니라 `IRHIDevice::getBackBufferFormat()` 이 답합니다.
         */
        virtual RHIFormat getTextureFormat( RHITextureHandle texture ) const = 0;

        /**
         * @brief 2D 텍스처에 픽셀을 올립니다. 밉 0 부터 차례로, 행은 빈틈없이(DDS 배치) 받습니다.
         * @details 로드 시점용 동기 경로입니다. DX12 는 열어 둔 복사 리스트에 기록해 두고 프레임 리스트 앞에 끼워 제출하며
         *          (flushPendingUploads), Vulkan 은 제출하고 기다립니다. DX11 · GL 은 즉시 컨텍스트에 들어가 있습니다.
         *          어느 쪽이든 뒤이은 드로우보다 먼저 실행됩니다. 매 프레임 갱신 용도가 아닙니다.
         *          createTexture2D 가 _bIsShaderResource 로 만든 컬러 포맷(비압축 · BC)만 받고, 깊이 · Unknown 은
         *          받지 않습니다(getRhiFormatBlockInfo 참고). 밉 크기 · 오프셋 규칙은 resolveTextureUploadMips 한 곳이 정합니다.
         * @return 포맷이 업로드 불가이거나 데이터가 모자라면 false.
         */
        virtual bool uploadTexture2D( RHITextureHandle texture, const RHITextureUploadDesc& desc ) = 0;

        /**
         * @brief 2D 텍스처의 밉 하나를 CPU 로 읽어 옵니다. 행은 빈틈없이(업로드와 같은 배치, BC 는 블록 행) 채웁니다.
         * @details GPU 를 기다리는 **동기** 경로입니다. 테스트 · 도구 · 스크린샷 용도이지 프레임 경로가 아닙니다.
         *          outLayout 에 밉 크기와 행 바이트가 채워집니다(_pData 는 outBytes.data() 를 가리키지 않습니다).
         * @return 포맷이 대상이 아니거나(깊이) 밉이 범위 밖이면 false.
         */
        virtual bool readbackTexture2D( RHITextureHandle texture, uint32 mip, vector<uint8>& outBytes, RHITextureMipSpan& outLayout ) = 0;

        // ------------------------------------------------------------------------------
        // Bindless — 텍스처/버퍼/UAV 등록과 해제
        // ------------------------------------------------------------------------------
        /**
         * @brief Bindless 테이블에 텍스처를 등록하고 SRV 인덱스를 발급합니다.
         * @details 텍스처 인덱스와 버퍼 인덱스는 **서로 다른 공간**입니다. DX12 만 하나의 셰이더 가시
         *          힙을 공유하고, DX11/OpenGL/Vulkan 은 텍스처 표와 버퍼 표를 따로 둡니다. 같은 정수가
         *          양쪽에서 각각 다른 리소스를 가리킬 수 있습니다. 그래서 해제도 종류별로 나뉩니다.
         */
        virtual RHIDescriptorIndex registerBindlessTexture( RHITextureHandle texture ) = 0;

        /**
         * @brief registerBindlessTexture 가 발급한 텍스처 SRV 인덱스를 해제합니다.
         * @details 버퍼 인덱스를 여기에 넘기거나 텍스처 인덱스를 unregisterBindlessResource 에 넘기면
         *          안 됩니다. 후자는 실제로 있었던 사고입니다. 트랜지언트 텍스처 SRV 0·1·2 가 버퍼
         *          프리리스트로 들어가 살아 있는 패스 상수버퍼 슬롯 0·1·2 를 비운 것으로 만들었고, 다음에
         *          등록된 인스턴스 구조버퍼가 슬롯 2 를 차지해 Vulkan set 0 에 STORAGE 세트가 걸렸습니다.
         *          destroyTexture 는 등록을 스스로 정리하므로 파괴 직전이라면 이 호출은 생략해도 됩니다.
         */
        virtual void unregisterBindlessTexture( RHIDescriptorIndex index ) = 0;

        /** @brief Bindless 테이블에 버퍼를 등록하고 인덱스를 발급합니다. */
        virtual RHIDescriptorIndex registerBindlessResource( RHIBufferHandle buffer ) = 0;

        /**
         * @brief registerBindlessResource 가 발급한 **버퍼** 인덱스를 해제합니다.
         * @details 이미 비어 있는 슬롯(다른 종류의 인덱스, 이중 해제)은 프리리스트에 다시 넣지 않습니다.
         *          한 번이라도 넣으면 같은 인덱스가 두 리소스에 발급돼 조용히 엉뚱한 버퍼가 바인딩됩니다.
         */
        virtual void unregisterBindlessResource( RHIDescriptorIndex index ) = 0;

        /** @brief bindless UAV 를 등록하고 인덱스를 발급합니다. */
        virtual RHIDescriptorIndex registerBindlessUav( RHIBufferHandle buffer ) = 0;

        /**
         * @brief 텍스처(생성 시 _bIsUnorderedAccess)를 컴퓨트 RW 텍스처로 등록하고 인덱스를 발급합니다.
         * @details DX12/Vulkan 은 RW 텍스처 배열(g_SwBindlessRWTex2D)의 원소 인덱스라 셰이더가 그 값으로 고릅니다(루트 상수 등으로 넘깁니다).
         *          DX11/GL 은 bindComputeUav( index, shaderslot::kComputeTextureUav0 + 서수 ) 로 슬롯에 걸고 셰이더는 서수를 씁니다.
         *          해제는 unregisterBindlessUav 이고, 버퍼 UAV 와 같은 인덱스 공간입니다.
         */
        virtual RHIDescriptorIndex registerBindlessTextureUav( RHITextureHandle texture ) = 0;

        /** @brief bindless UAV 등록을 해제합니다. */
        virtual void unregisterBindlessUav( RHIDescriptorIndex index ) = 0;
    };
} // namespace sw
