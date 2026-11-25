#pragma once
#include "Core/Concurrency/mutex.h"
#include "Core/Container/unordered_map.h"
#include "Core/Foundation/Types.h"

#include "Engine/Foundation/EnginePlatformHeaders.h"
#include "Engine/Graphics/RHI/Backend/RHIBindlessRegistry.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"

#include <shared_mutex>

#if defined( SW_PLATFORM_WINDOWS )
    #include <D3D12MemAlloc.h> // Windows SDK 의 d3d12.h 뒤다(ThirdParty/d3d12-memory-allocator — HEADERS_ALREADY_INCLUDED)

namespace sw
{
    class D3D12RHIDevice;

    class D3D12RHIResourceFactory : public IRHIResourceFactory
    {
    public:
        explicit D3D12RHIResourceFactory( D3D12RHIDevice* pDevice )
            : _pDevice{ pDevice } {}
        RHIPipelineStateHandle createPipelineStateNative( const RHIPipelineStateDesc& desc, const RHICompiledGraphicsStages& stages ) override;
        RHIPipelineStateHandle createComputePipelineStateNative( const RHICompiledShaderStage& computeStage, string_view label ) override;
        [[nodiscard]] bool     destroyPipelineStateNative( RHIPipelineStateHandle pso ) override;
        RHIBufferHandle        createConstantBuffer( uint32 size ) override;
        /** @brief 기록 경로의 상수 버퍼 쓰기입니다(커맨드 컨텍스트만 부른다 — 두 번째 쓰기 진단은 `IRHICommandContext::updateConstantBuffer` 가 했다). */
        void                            writeConstantBuffer( RHIBufferHandle buffer, const void* pData, uint32 size );
        RHIBufferHandle                 createConstantBufferPage( uint32 size ) override;
        void                            writeConstantBufferSlice( const RHIBufferSlice& slice, const void* pData, uint32 size ) override;
        RHIBufferHandle                 createStructuredBuffer( uint32 elementSize, uint32 elementCount ) override;
        void                            updateStructuredBufferRegionsNative( RHIBufferHandle buffer, const void* pBaseSource,
                                                                             const RHIBufferCopyRegion* pRegions, uint32 regionCount ) override;
        RHIBufferHandle                 createVertexBuffer( const void* pData, uint32 sizeBytes ) override;
        RHIBufferHandle                 createGPUVertexBuffer( uint32 sizeBytes ) override;
        void                            updateGeometryBufferRegionsNative( RHIBufferHandle buffer, const void* pBaseSource, const RHIBufferCopyRegion* pRegions, uint32 regionCount ) override;
        RHIBufferHandle                 createIndexBuffer( const void* pData, uint32 sizeBytes ) override;
        [[nodiscard]] bool              destroyBufferNative( RHIBufferHandle buffer ) override;
        RHIBufferHandle                 createReadbackBuffer( uint32 elementSize, uint32 elementCount ) override;
        [[nodiscard]] bool              readReadbackBuffer( RHIBufferHandle readback, uint32 sizeBytes, void* pDestination ) override;
        RHITextureHandle                createTexture2D( const RHITextureDesc& desc ) override;
        RHITextureAllocationInfo        getTextureAllocationInfo( const RHITextureDesc& desc ) override;
        RHITransientHeapHandle          createTransientHeap( uint64 sizeBytes ) override;
        [[nodiscard]] bool              destroyTransientHeapNative( RHITransientHeapHandle heap ) override;
        RHITextureHandle                createPlacedTexture2D( const RHITextureDesc& desc, RHITransientHeapHandle heap, uint64 offsetBytes ) override;
        void                            setTextureDebugName( RHITextureHandle texture, const utf8* pName ) override;
        [[nodiscard]] bool              destroyTextureNative( RHITextureHandle texture ) override;
        [[nodiscard]] RHITransferSubmit submitTextureUploadNative( RHITextureHandle texture, const RHITextureUploadDesc& desc ) override;
        [[nodiscard]] RHITransferSubmit submitTextureRegionUploadNative( RHITextureHandle texture, const RHITextureRegionUploadDesc& desc ) override;
        [[nodiscard]] RHITransferSubmit submitTextureReadbackNative( RHITextureHandle texture, uint32 mip, uint32 arraySlice, RHITextureMipSpan& outLayout ) override;
        [[nodiscard]] RHITransferStatus pollTransfer( RHITransferTicket ticket ) override;
        [[nodiscard]] bool              takeTextureReadback( RHITransferTicket ticket, vector<uint8>& outBytes ) override;
        void                            discardTextureReadback( RHITransferTicket ticket ) override;
        RHIFormat                       getTextureFormat( RHITextureHandle texture ) const override;
        RHITextureSRVIndex              registerBindlessTexture( RHITextureHandle texture ) override;
        [[nodiscard]] bool              rebindBindlessTexture( RHITextureSRVIndex index, RHITextureHandle texture ) override;
        RHITextureSRVIndex              reserveBindlessTextureIndex() override;
        [[nodiscard]] bool              activateReservedBindlessTexture( RHITextureSRVIndex index, RHITextureHandle texture ) override;
        bool                            isTextureReadyToSample( RHITextureHandle texture ) override;
        void                            unregisterBindlessTexture( RHITextureSRVIndex index ) override;
        RHIBufferSRVIndex               registerBindlessBufferSRV( RHIBufferHandle buffer ) override;
        RHIBufferSRVIndex               registerBindlessBufferSRV( const RHIBufferSlice& slice ) override;
        void                            unregisterBindlessResource( RHIBufferSRVIndex index ) override;
        RHIUnorderedAccessIndex         registerBindlessBufferUAV( RHIBufferHandle buffer ) override;
        RHIUnorderedAccessIndex         registerBindlessTextureUAV( RHITextureHandle texture ) override;
        void                            unregisterBindlessUAV( RHIUnorderedAccessIndex index ) override;

        /** @brief 남은 트랜지언트 힙 핸들을 모두 놓습니다(장치 종료 — 할당기보다 먼저). */
        void releaseTransientHeaps() { _mapTransientHeap.clear(); }

    private:
        /**
         * @brief 텍스처 리소스와 뷰 · 레코드를 만듭니다. @p pHeap 이 있으면 그 힙의 @p offsetBytes 에 놓고(placed — 리소스가 힙을 붙든다), 없으면 자기 메모리를 잡는다(`D3D12RHIDevice::createResource`).
         * @details `createTexture2D` 와 `createPlacedTexture2D` 가 같은 몸을 쓴다 — 서술 · 뷰 · 상태 규칙이 두 벌로 갈리지 않게.
         */
        RHITextureHandle createTextureResource( const RHITextureDesc& desc, D3D12MA::Allocation* pHeap, uint64 offsetBytes );
        /** @brief default 힙 버퍼를 만들고 @p pData 를 스테이징 복사로 올립니다. 올린 뒤 @p readState 에 둔다(정점 · 인덱스 — 정적 자원을 시스템 메모리에서 그리지 않는다). */
        RHIBufferHandle createDefaultBufferWithData( const void* pData, uint32 sizeBytes, D3D12_RESOURCE_STATES readState );
        /**
         * @brief bindless 힙에서 디스크립터 인덱스를 하나 가져옵니다.
         * @param lock 부르는 쪽이 **이미 잡고 있는** `_bindlessMutex` 잠금.
         * @return 힙이 가득 찼으면 `kInvalidDescriptorIndex`.
         * @details 여기서 잠그지 않고 이미 잡은 잠금을 받는 이유는, 인덱스를 가져오는 것과 **그 자리에 뷰를 만드는 것**이
         *          한 임계 구역이어야 합니다. 여기서 잠갔다 풀면 그 틈에 다른 스레드가 같은 인덱스를 받습니다.
         *          잠금 객체를 인자로 받아 그 전제가 부르는 쪽에게 보입니다.
         * @note 네 등록 함수(`registerBindlessTexture`, `registerBindlessBufferSRV`, `registerBindlessBufferUAV`,
         *       `registerBindlessTextureUAV`)가 이것을 같이 씁니다. 용량 검사와 프리리스트 정책이 여러 벌이면 한 곳만
         *       고치고 넘어가기 쉽고, 그 결과는 **디스크립터가 어긋나 화면이 조용히 깨지는 것**입니다.
         */
        RHIDescriptorIndex acquireBindlessIndex( UniqueLock<shared_mutex>& lock ); // 잠금 자체를 받는 것이 전제의 증거다 — `requires_capability` 는 capability 형식(뮤텍스)만 받아 std 잠금 객체에는 걸 수 없다

        /** @brief 힙 인덱스 하나가 가리키는 두 디스크립터입니다. 온라인 힙의 CPU 핸들과 오프라인 힙의 CPU 핸들입니다. */
        struct BindlessHandleSet
        {
            D3D12_CPU_DESCRIPTOR_HANDLE _cpu{};
            D3D12_CPU_DESCRIPTOR_HANDLE _offline{};
        };
        /**
         * @brief 힙 인덱스의 디스크립터 핸들 쌍을 계산합니다.
         * @details 뷰는 온라인 힙과 오프라인 힙에 **같이** 만듭니다(슬롯 테이블 t#/u# 은 오프라인에서 온라인 블록으로 복사합니다).
         *          네 등록 함수가 같이 씁니다. 한 곳이 오프라인 힙을 빠뜨리면 그 종류의 리소스만 슬롯 테이블에서 사라집니다.
         */
        BindlessHandleSet bindlessHandlesAt( RHIDescriptorIndex index ) const;

        /**
         * @brief 어느 bindless 레지스트리인지 나타냅니다. **힙 인덱스 공간은 하나**지만 기록은 둘로 나뉩니다.
         * @details 둘이 같은 번호 발급기를 쓰므로(`_bindlessIndexAllocator`) 인덱스는 섞이지 않습니다.
         *          나뉘는 것은 그 슬롯이 무엇을 보관했는지뿐입니다.
         */
        enum class BindlessRegistry : uint8
        {
            ShaderResource, ///< SRV/CBV 레지스트리입니다.
            UnorderedAccess ///< UAV 레지스트리입니다.
        };

        /**
         * @brief 레지스트리의 슬롯 하나를 비우고 인덱스를 펜스 뒤 회수에 맡깁니다.
         * @details **이미 빈 슬롯을 다시 돌려주면 같은 인덱스가 두 리소스에 발급됩니다.** 그 검사를 SRV/CBV 쪽과
         *          UAV 쪽이 같이 씁니다(다른 것은 레지스트리와 로그 문구뿐입니다).
         */
        void releaseBindlessRecord( BindlessRegistry registry, RHIDescriptorIndex index );
        /**
         * @brief 레지스트리의 슬롯 `index` 에 리소스, 핸들 쌍, 원래 핸들(버퍼나 텍스처 하나, 다른 쪽은 0)을 기록합니다. 슬롯이 모자라면 늘립니다.
         * @details 다섯 등록 경로(텍스처 SRV, 구조 버퍼 SRV, CBV, 버퍼 UAV, 텍스처 UAV)가 같이 씁니다.
         *          인덱스를 집은 잠금 안에서 부릅니다. @p link 는 번호가 자원의 것인가(등록) · 받은 쪽의 것인가(예약)다(`RHIBindlessLink`).
         */
        void writeBindlessRecord( BindlessRegistry registry, RHIDescriptorIndex index, ID3D12Resource* pResource, const BindlessHandleSet& handle,
                                  RHIBufferHandle buffer, RHITextureHandle texture, RHIBindlessLink link, uint32 sliceOffset = 0 );

        /** @brief 텍스처와 버퍼 공용 힙 슬롯을 비우고 프리리스트에 돌려줍니다(빈 슬롯은 무시합니다). */
        void releaseBindlessSlot( RHIDescriptorIndex index );
        /** @brief 인덱스를 GPU 펜스 뒤에 프리리스트로 돌려보냅니다. 실행 중인 리스트가 새 리소스를 읽지 않게 합니다. */
        void deferFreeBindlessIndex( RHIDescriptorIndex index );

        /** @brief 버퍼 복사를 어디에 기록하는가입니다. */
        enum class UploadOrder : uint8
        {
            FrameStream, ///< 프레임 스트림의 지금 자리 — 앞서 낸 리스트 뒤, 뒤에 내는 리스트 앞에 실행된다
            BeforeFrame, ///< 업로드 리스트 — 다음 제출의 맨 앞에 실행된다(프레임 밖 · 다른 스레드 · 새 버퍼의 첫 내용)
        };

        /** @brief 조각들을 스테이징에 모아 복사와 상태 전이를 @p order 자리에 기록합니다. */
        void recordBufferCopies( RHIBufferHandle buffer, const void* pBaseSource, const RHIBufferCopyRegion* pRegions, uint32 regionCount, UploadOrder order );
        /**
         * @brief 현재 프레임 링 슬롯의 스테이징 힙에서 sizeBytes 를 bump 할당합니다.
         * @details 같은 펜스 구간의 앞선 복사가 GPU 에서 도는 중일 수 있어 구간이 바뀔 때만 되감습니다. 부르는 쪽이 `_uploadSlotMutex` 를 쥡니다.
         */
        bool acquireUploadStaging( uint64 sizeBytes, uint64 alignment, uint32& outSlotIndex, uint64& outOffset, void*& pOutMapped );
        /**
         * @brief 슬롯 @p slotIndex 의 펜스 구간이 바뀌었으면 앞 구간의 복사가 끝나기를 기다려 얼로케이터를 Reset 하고 스테이징을 되감습니다. 부르는 쪽이 `_uploadSlotMutex` 를 쥡니다.
         * @details 그 슬롯의 복사 리스트가 앞 구간에서 열린 채면(프레임 제출과 Signal 사이에 연 복사) 내보내고, 프레임 스트림 복사가 남았으면 되감지 않는다.
         */
        [[nodiscard]] bool beginUploadPeriod( uint32 slotIndex );
        /** @brief 슬롯 @p slotIndex 의 복사 리스트를 엽니다(`beginUploadPeriod` 뒤). 다른 칸이 열려 있으면 먼저 내보냅니다. 닫고 제출하는 것은 `D3D12RHIDevice::flushPendingUploads`. */
        [[nodiscard]] bool openUploadList( uint32 slotIndex );

    public:
    private:
        D3D12RHIDevice* _pDevice;
        /// @brief 트랜지언트 힙(`createTransientHeap`)입니다. 핸들을 놓아도 그 위의 placed 텍스처가 각자 힙을 붙든다(`SetPrivateDataInterface`). 생성 · 파괴는 셋업 스레드다.
        unordered_map<RHITransientHeapHandle, Microsoft::WRL::ComPtr<D3D12MA::Allocation>> _mapTransientHeap;
        uint64                                                                             _nextTransientHeap{ 1 }; ///< 다음에 낼 힙 핸들의 packed 값
    };
} // namespace sw
#endif
