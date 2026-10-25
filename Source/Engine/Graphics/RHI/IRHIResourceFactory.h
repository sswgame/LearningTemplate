#pragma once
#include "Core/Common/EnumUtil.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
    /**
     * @class IRHIResourceFactory
     * @brief RHI 버퍼, 텍스처, PSO, 렌더 패스의 생성과 파괴, 그리고 bindless 등록을 담당하는 인터페이스입니다.
     */
    class SW_API IRHIResourceFactory
    {
    public:
        IRHIResourceFactory()                                        = default;
        virtual ~IRHIResourceFactory()                               = default;
        IRHIResourceFactory( const IRHIResourceFactory& )            = delete;
        IRHIResourceFactory& operator=( const IRHIResourceFactory& ) = delete;

        // ------------------------------------------------------------------------------
        // 리소스: PSO, 렌더 패스, 버퍼, 텍스처
        // ------------------------------------------------------------------------------
        /** @brief 그래픽스 파이프라인 상태(PSO)를 만듭니다. */
        virtual RHIPipelineStateHandle createPipelineState( const RHIPipelineStateDesc& desc ) = 0;

        /** @brief 컴퓨트 파이프라인 상태(PSO)를 만듭니다. */
        virtual RHIPipelineStateHandle createComputePipelineState( string_view shaderPath, string_view entryPoint = {} ) = 0;

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
         * @param size 보낼 바이트 수입니다. **`createConstantBuffer` 에 준 크기를 넘으면 안 됩니다.**
         * @warning 이 전제는 **백엔드가 검사해 주지 않는다.** DX12, Vulkan, DX11 은 받은 크기를 그대로
         *          복사하므로, 넘기면 프레임 슬롯 밖(또는 버퍼 밖)까지 쓴다. GL 만 `glBufferSubData`
         *          가 `GL_INVALID_VALUE` 로 막아 준다. 즉 **한 백엔드에서만 조용히 안전하다.**
         *          버퍼가 작아졌다면 갱신하지 말고 **다시 만들어야 한다**
         *          (`MaterialInstance::updateRHI` 는 셰이더를 다시 컴파일해 레이아웃이 커지는 경우를
         *          그렇게 처리한다).
         */
        virtual void updateConstantBuffer( RHIBufferHandle buffer, const void* pData, uint32 size ) = 0;

        /** @brief 구조화(Structured) 버퍼나 스토리지(Storage) 버퍼를 만듭니다. */
        virtual RHIBufferHandle createStructuredBuffer( uint32 elementSize, uint32 elementCount ) = 0;

        /**
         * @brief 구조화 버퍼나 스토리지 버퍼의 **여러 조각**을 한 번에 갱신합니다.
         * @param pBaseSource 조각들의 `_srcOffset` 이 가리키는 원본 블롭입니다.
         *
         * @details 오프셋이 필요한 이유는 하나다. **바뀐 것만 올리기 위해서다.** 인스턴스 8000개 중 10개만
         *          움직였는데 버퍼 전체를 올리면 800개가 움직일 때와 같은 비용이 든다.
         *
         * @warning 크기와 마찬가지로 **범위를 백엔드가 검사해 주지 않는다.** `byteOffset + size` 가
         *          버퍼를 넘으면 버퍼 밖까지 쓴다. 호출하는 쪽이 지켜야 한다.
         */
        virtual void updateStructuredBufferRegions( RHIBufferHandle buffer, const void* pBaseSource,
                                                    const RHIBufferCopyRegion* pRegions, uint32 regionCount ) = 0;

        /** @brief 한 조각만 갱신합니다(`updateStructuredBufferRegions` 의 영역 1개). */
        void updateStructuredBufferRange( RHIBufferHandle buffer, const void* pData, uint32 size, uint32 byteOffset )
        {
            const RHIBufferCopyRegion region{ 0, byteOffset, size };
            updateStructuredBufferRegions( buffer, pData, &region, 1 );
        }

        /** @brief 구조화 버퍼나 스토리지 버퍼를 앞에서부터 갱신합니다(`updateStructuredBufferRange` 의 오프셋 0). */
        void updateStructuredBuffer( RHIBufferHandle buffer, const void* pData, uint32 size )
        {
            updateStructuredBufferRange( buffer, pData, size, 0 );
        }

        /** @brief 내용을 담아 정점 버퍼를 만듭니다. 레이아웃은 PSO 의 입력 레이아웃(`constant::arrVertexAttribute`)이 정합니다. */
        virtual RHIBufferHandle createVertexBuffer( const void* pData, uint32 sizeBytes ) = 0;

        /**
         * @brief `createVertexBuffer` 로 만든 정점 버퍼의 **여러 조각**을 갱신합니다. 정점 풀(`GPUMeshVertexPool`)의 부분 갱신용입니다.
         * @param pBaseSource 조각들의 `_srcOffset` 이 가리키는 원본 블롭입니다.
         * @details 정점 버퍼는 백엔드마다 다른 메모리에 산다. DX12 와 Vulkan 은 CPU 가 볼 수 있는 메모리(업로드 힙, HOST_VISIBLE)라 매핑해서
         *          직접 쓰고, DX11 과 GL 은 즉시 컨텍스트의 갱신(UpdateSubresource, glBufferSubData)으로 쓴다. 업로드 힙은 복사의 목적지가 될 수
         *          없어 구조화 버퍼의 경로(`updateStructuredBufferRegions`)를 그대로 쓸 수 없다.
         * @warning **아직 끝나지 않은 프레임이 읽는 조각을 쓰면 안 된다.** 직접 쓰는 백엔드는 GPU 와 순서를 맞추지 않는다. 그래서 부르는 쪽이
         *          돌려받은 구간을 `constant::kMaxFrameCountInFlight` 프레임이 지난 뒤에야 다시 쓴다(`SpanAllocator::freeDeferred`).
         *          범위는 백엔드가 검사해 넘는 조각을 버리고 알린다.
         */
        virtual void updateVertexBufferRegions( RHIBufferHandle buffer, const void* pBaseSource, const RHIBufferCopyRegion* pRegions, uint32 regionCount ) = 0;

        /**
         * @brief 범용 버퍼(구조화, UAV, 인다이렉트 인자, 인덱스)를 만듭니다.
         * @details 내용이 있는 `Vertex` 용도는 createVertexBuffer 로, `Index` 용도는 createIndexBuffer 로 넘긴다
         *          (`_elementSize` 가 2 면 uint16 인덱스).
         */
        virtual RHIBufferHandle createBuffer( const RHIBufferDesc& desc )
        {
            const bool bHasVertexInitialData = EnumUtil::hasFlag( desc._usage, RHIBufferUsage::Vertex ) && desc._pInitialData != nullptr && desc._sizeBytes > 0;
            if ( bHasVertexInitialData )
                return createVertexBuffer( desc._pInitialData, desc._sizeBytes );
            const bool bHasIndexInitialData = EnumUtil::hasFlag( desc._usage, RHIBufferUsage::Index ) && desc._pInitialData != nullptr && desc._sizeBytes > 0;
            if ( bHasIndexInitialData )
                return createIndexBuffer( desc._pInitialData, desc._sizeBytes );
            if ( EnumUtil::hasFlag( desc._usage, RHIBufferUsage::Constant ) )
                return createConstantBuffer( desc._sizeBytes > 0 ? desc._sizeBytes : 256u );
            const uint32    elemSize                  = desc._elementSize > 0 ? desc._elementSize : 4u;
            const uint32    elemCount                 = desc._elementCount > 0
                                                          ? desc._elementCount
                                                          : ( desc._sizeBytes > 0 ? ( desc._sizeBytes / elemSize ) : 1u );
            RHIBufferHandle buffer                    = createStructuredBuffer( elemSize, elemCount );
            const bool      bHasStructuredInitialData = buffer != 0 && desc._pInitialData != nullptr && desc._sizeBytes > 0;
            if ( bHasStructuredInitialData )
                updateStructuredBuffer( buffer, desc._pInitialData, desc._sizeBytes );
            return buffer;
        }

        /**
         * @brief 내용을 담아 인덱스 버퍼(uint16, uint32)를 만듭니다. 인덱스 크기는 바인딩할 때(`setIndexBuffer`) 다시 알려 줍니다.
         * @details 백엔드마다 **인덱스 버퍼 용도**로 만들어야 한다(D3D11_BIND_INDEX_BUFFER, VK_BUFFER_USAGE_INDEX_BUFFER_BIT 등).
         *          주의: 구조화 버퍼로 대신하면 안 된다(D3D11 은 BUFFER_STRUCTURED 에 BIND_INDEX_BUFFER 를 붙일 수 없고, Vulkan 은
         *          검증 레이어가 용도 위반으로 잡는다). 드라이버가 받아 줘서 그려지는 백엔드도 있어 이미지만으로는 드러나지 않는다
         *          (RHIDeviceTest.IndexedIndirectDrawReadsInstanceSlotStream).
         */
        virtual RHIBufferHandle createIndexBuffer( const void* pData, uint32 sizeBytes ) = 0;

        /** @brief GPU 버퍼 리소스를 삭제합니다. */
        virtual void destroyBuffer( RHIBufferHandle buffer ) = 0;

        /**
         * @brief GPU 에서 CPU 로 되읽을 읽기 전용 버퍼(@p elementSize x @p elementCount 바이트)를 만듭니다. 만들지 못하면 0 입니다.
         * @details 비동기 되읽기의 도착지다 — `IRHICommandList::copyBufferToReadback` 이 채우고, 그 프레임이 끝난 뒤(`IRHIDevice::enqueueGPURelease` 콜백) `readReadbackBuffer` 로 읽는다.
         *          원본이 구조화 버퍼면 같은 원소 크기로 만든다(DX11 은 구조화 버퍼끼리만 복사한다). 지우는 것은 `destroyBuffer` 다.
         */
        [[nodiscard]] virtual RHIBufferHandle createReadbackBuffer( uint32 elementSize, uint32 elementCount ) = 0;

        /**
         * @brief 읽기 버퍼의 앞쪽 @p sizeBytes 를 @p pDestination 에 복사합니다. 기다리지 않습니다.
         * @details **복사를 기록한 프레임의 GPU 작업이 끝난 뒤에만** 부른다 — `IRHIDevice::enqueueGPURelease` 콜백 안이 그 자리다. 콜백은 완료를 확인하는 스레드에서 불리므로
         *          이 함수는 어느 스레드에서 불러도 안전하다(DX11 은 즉시 컨텍스트를 잠그고 GL 은 컨텍스트를 빌린다). 끝나기 전에 부르면 결과가 정해져 있지 않다(DX11 은 false, DX12 · Vulkan 은 이전 내용이거나 0, GL 은 드라이버가 암묵적으로 기다린다).
         * @return 핸들이 읽기 버퍼가 아니거나 크기가 버퍼를 넘거나 내용을 가져오지 못하면 false 입니다(이유는 로그). 읽기 버퍼가 아닌 핸들은 네 백엔드가 같이 거절한다
         *         (DX11 스테이징 버퍼, DX12 읽기 힙, Vulkan 복사 도착지 전용 버퍼, GL `GL_STREAM_READ` 만 읽기 버퍼로 본다).
         */
        [[nodiscard]] virtual bool readReadbackBuffer( RHIBufferHandle readback, uint32 sizeBytes, void* pDestination ) = 0;

        /** @brief 2D 텍스처(렌더 타깃 포함)를 만듭니다. */
        virtual RHITextureHandle createTexture2D( const RHITextureDesc& desc ) = 0;

        /** @brief GPU 텍스처 리소스를 삭제합니다. */
        virtual void destroyTexture( RHITextureHandle texture ) = 0;

        /**
         * @brief 텍스처가 실제로 만들어진 RHIFormat 을 반환합니다(없는 핸들이면 Unknown).
         * @details 렌더 타깃에 그리는 PSO 는 **대상의 실제 포맷**으로 만들어야 한다. 언리얼이 PSO 초기화자의
         *          RenderTargetFormats 를 바인딩된 FRHITexture::GetFormat() 에서 뽑는 것과 같은 방식이다.
         *          백버퍼(핸들 0)는 여기가 아니라 `IRHIDevice::getBackBufferFormat()` 이 알려 준다.
         */
        virtual RHIFormat getTextureFormat( RHITextureHandle texture ) const = 0;

        // ------------------------------------------------------------------------------
        // 전송: 제출 → 티켓 → 폴링 (docs/plans/RhiAsyncUploads.md 2.2)
        // ------------------------------------------------------------------------------
        // 규칙: 이 인터페이스에는 GPU 완료를 기다리는 가상 함수를 새로 만들지 않는다. 기다리는 변형은 Engine 의 `RHITransferUtil::...AndWait` 헬퍼이고
        //       vtable 밖에 있다. 소비자는 `pollTransfer` 가 `Complete` 를 돌려준 뒤에야 텍스처를 bindless 에 등록한다.
        // 계약 (제출 함수 공통):
        //   1. 반환하기 전에 소스 메모리를 복사한다. 호출자는 바로 버퍼를 재사용한다.
        //   2. GPU 완료를 기다리지 않는다. 스테이징이 모자라면 `Busy`(티켓 무효)로 돌아오고 호출자가 다음 프레임에 다시 제출한다.
        //      검사에 걸리면 `Rejected` 이고 이유를 로그로 남기며 다시 시도하지 않는다.
        //   3. 순서: 제출한 전송은 그 뒤에 제출되는 모든 프레임 커맨드보다 먼저 실행된다. 같은 프레임에 이미 기록한 앞선 드로우가 어느 내용을 보는지는
        //      정해져 있지 않으므로 호출자는 업로드를 그리기 기록 전에 제출한다. 같은 텍스처에 대한 제출끼리는 제출 순서대로 실행된다.
        //   4. 스레드: `RHICapabilities::_bThreadSafeResourceCreation` 이 참이면 어느 스레드에서나 제출한다. 거짓(GL)이면 컨텍스트를 가진 스레드에서만 제출한다.

        /**
         * @brief 2D 텍스처에 픽셀을 올리도록 제출합니다. 밉 0 부터 차례로, 행은 빈틈없이(DDS 배치) 받습니다.
         * @details createTexture2D 가 _bIsShaderResource 로 만든 컬러 포맷(비압축, BC)만 받고, 깊이와 Unknown 은
         *          받지 않는다(getRHIFormatBlockInfo 참고). 밉 크기와 오프셋 규칙은 resolveTextureUploadMips 한 곳이 정한다.
         *          매 프레임 갱신하는 용도가 아니다.
         * @return `Accepted` 면 티켓으로 `pollTransfer` 한다. 포맷이 업로드할 수 없는 것이거나 데이터가 모자라면 `Rejected` 다.
         */
        [[nodiscard]] virtual RHITransferSubmit submitTextureUpload( RHITextureHandle texture, const RHITextureUploadDesc& desc ) = 0;

        /**
         * @brief 2D 텍스처의 밉 하나, 면 하나의 사각형 구간에 픽셀을 올리도록 제출합니다(비압축 컬러 포맷만). 나머지 픽셀은 그대로 둡니다.
         * @details 글리프 아틀라스처럼 **가끔, 작게** 바뀌는 텍스처용이고, 매 프레임 큰 구간을 올리는 용도가 아니다.
         *          검사(포맷, 범위, 데이터 크기)는 validateTextureRegionUpload 한 곳이 한다.
         * @return 검사에 걸리면 `Rejected` 입니다(이유는 로그에 남깁니다).
         */
        [[nodiscard]] virtual RHITransferSubmit submitTextureRegionUpload( RHITextureHandle texture, const RHITextureRegionUploadDesc& desc ) = 0;

        /**
         * @brief 텍스처의 면(`arraySlice`: 배열 원소 또는 큐브 면, 2D 는 0) 하나의 밉 하나를 CPU 로 읽도록 요청합니다. 행은 빈틈없이(업로드와 같은 배치,
         *        BC 는 블록 행) 채웁니다.
         * @param outLayout 밉 크기와 행 바이트입니다. **제출 시점에 바로 채웁니다**(_pData 는 쓰지 않는다). 결과 바이트는 `takeTextureReadback` 이 준다.
         * @details 깊이 텍스처는 네 백엔드 모두 `kRHIDepthReadbackFormat` 배치(텍셀마다 0..1 깊이 float32, 스텐실 없음)로 채운다.
         *          요청 시점까지 제출된 이 텍스처의 쓰기가 읽기에 보인다(순서 규칙 3).
         * @return 포맷이 대상이 아니거나 밉이나 면이 범위 밖이면 `Rejected` 다.
         */
        [[nodiscard]] virtual RHITransferSubmit submitTextureReadback( RHITextureHandle texture, uint32 mip, uint32 arraySlice, RHITextureMipSpan& outLayout ) = 0;

        /**
         * @brief 제출한 전송의 진행 상태를 값싸게 묻습니다. 기다리지 않습니다.
         * @details `Complete` 는 완료 워터마크 이상으로 판정하므로 `Complete` 를 본 뒤 같은 티켓을 다시 물어도 `Complete` 이고, 티켓을 해제하는 함수는 없다.
         *          무효 티켓은 `Failed` 다. 어느 스레드에서나 부를 수 있다(GL 은 컨텍스트를 빌려 묻는다).
         */
        [[nodiscard]] virtual RHITransferStatus pollTransfer( RHITransferTicket ticket ) = 0;

        /**
         * @brief 완료된 읽기 결과를 **한 번** 가져갑니다.
         * @param ticket `submitTextureReadback` 이 돌려준 티켓입니다. `pollTransfer` 가 `Complete` 를 돌려준 뒤에 부릅니다.
         * @param outBytes 결과 바이트입니다(제출 때 받은 `outLayout` 의 배치).
         * @return 읽기 티켓이 아니거나, 아직 `Complete` 가 아니거나, 이미 가져갔으면 false 입니다(이유는 로그).
         */
        [[nodiscard]] virtual bool takeTextureReadback( RHITransferTicket ticket, vector<uint8>& outBytes ) = 0;

        // ------------------------------------------------------------------------------
        // Bindless: 텍스처/버퍼/UAV 등록과 해제
        // ------------------------------------------------------------------------------
        /**
         * @brief Bindless 테이블에 텍스처를 등록하고 SRV 인덱스를 발급합니다.
         * @details 텍스처 인덱스와 버퍼 인덱스는 **서로 다른 공간**이다. DX12 만 하나의 셰이더 가시
         *          힙을 공유하고, DX11/OpenGL/Vulkan 은 텍스처 테이블과 버퍼 테이블을 따로 둔다. 같은 정수가
         *          양쪽에서 각각 다른 리소스를 가리킬 수 있다. 그래서 해제도 종류별로 나뉜다.
         */
        virtual RHIDescriptorIndex registerBindlessTexture( RHITextureHandle texture ) = 0;

        /**
         * @brief registerBindlessTexture 가 발급한 텍스처 SRV 인덱스를 해제합니다.
         * @details 버퍼 인덱스를 여기에 넘기거나 텍스처 인덱스를 unregisterBindlessResource 에 넘기면
         *          안 된다. 후자는 실제로 사고가 난 적이 있다. 트랜지언트 텍스처 SRV 0, 1, 2 가 버퍼
         *          프리리스트로 들어가 살아 있는 패스 상수 버퍼 슬롯 0, 1, 2 를 빈 것으로 만들었고, 다음에
         *          등록된 인스턴스 구조 버퍼가 슬롯 2 를 차지해 Vulkan set 0 에 STORAGE 세트가 바인딩되었다.
         *          destroyTexture 는 등록을 스스로 정리하므로 파괴 직전이라면 이 호출은 생략해도 된다.
         */
        virtual void unregisterBindlessTexture( RHIDescriptorIndex index ) = 0;

        /** @brief Bindless 테이블에 버퍼를 등록하고 인덱스를 발급합니다. */
        virtual RHIDescriptorIndex registerBindlessBufferSrv( RHIBufferHandle buffer ) = 0;

        /**
         * @brief registerBindlessBufferSrv 가 발급한 **버퍼** 인덱스를 해제합니다.
         * @details 이미 비어 있는 슬롯(다른 종류의 인덱스, 이중 해제)은 프리리스트에 다시 넣지 않는다.
         *          한 번이라도 넣으면 같은 인덱스가 두 리소스에 발급되어 조용히 엉뚱한 버퍼가 바인딩된다.
         */
        virtual void unregisterBindlessResource( RHIDescriptorIndex index ) = 0;

        /** @brief bindless UAV 를 등록하고 인덱스를 발급합니다. */
        virtual RHIDescriptorIndex registerBindlessBufferUav( RHIBufferHandle buffer ) = 0;

        /**
         * @brief 텍스처(생성 시 _bIsUnorderedAccess)를 컴퓨트 RW 텍스처로 등록하고 인덱스를 발급합니다.
         * @details DX12 와 Vulkan 은 RW 텍스처 배열(g_SwBindlessRWTex2D)의 원소 인덱스라 셰이더가 그 값으로 고른다(루트 상수 등으로 넘긴다).
         *          DX11 과 GL 은 bindComputeUav( index, shaderslot::kComputeTextureUav0 + 서수 ) 로 슬롯에 바인딩하고 셰이더는 서수를 쓴다.
         *          해제는 unregisterBindlessUav 이고, 버퍼 UAV 와 같은 인덱스 공간을 쓴다.
         */
        virtual RHIDescriptorIndex registerBindlessTextureUav( RHITextureHandle texture ) = 0;

        /** @brief bindless UAV 등록을 해제합니다. */
        virtual void unregisterBindlessUav( RHIDescriptorIndex index ) = 0;
    };
} // namespace sw
