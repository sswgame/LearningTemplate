#pragma once
#include "Core/Common/Types.h"

#include "Engine/Common/EnginePlatformHeaders.h"
#include "Engine/Graphics/RHI/IRHICommandContext.h"

#if defined( SW_PLATFORM_WINDOWS )

namespace sw
{
    struct D3D12RecordingState;

    class D3D12RHIDevice;

    /**
     * @class D3D12RHICommandContext
     * @brief 실제 D3D12 API 호출을 내는 구현체입니다. `_pCmdList`/`_pState` 로 "어떤 커맨드 리스트에,
     *        어떤 기록 상태로" 기록할지를 주입받습니다. 디바이스의 프레임 스트림 리스트와, `D3D12RHICommandList`
     *        (리스트별 네이티브 커맨드 리스트)가 각자 자신의 (cmdList, state) 쌍으로 이 클래스를 구성해 재사용합니다.
     */
    class D3D12RHICommandContext : public IRHICommandContext
    {
    public:
        D3D12RHICommandContext( D3D12RHIDevice* pDevice, ID3D12GraphicsCommandList* pCmdList, D3D12RecordingState* pState )
            : _pDevice{ pDevice }
            , _pCmdList{ pCmdList }
            , _pState{ pState }
        {
        }
        ~D3D12RHICommandContext() override = default;

        /** @brief 아직 기록 시작 전이면 얼로케이터/리스트를 Reset 하고 기록 상태로 표시합니다. */
        void ensureRecording();
        /** @brief 이 컨텍스트가 기록 중인 네이티브 커맨드 리스트를 반환합니다. */
        ID3D12GraphicsCommandList* getNativeCommandList() const { return _pCmdList; }
        /** @brief 기록 대상 네이티브 리스트를 교체합니다(소유자가 얼로케이터 쌍을 바꿔 낄 때). */
        void rebindCommandList( ID3D12GraphicsCommandList* pCmdList ) { _pCmdList = pCmdList; }
        /**
         * @brief 명령을 기록할 네이티브 리스트를 반환합니다. 부르는 것 자체가 "이 리스트에 뭔가 적었다" 는 표시입니다.
         * @details 모든 기록 호출이 이 창구를 지납니다(`_pCmdList->` 직접 접근은 널 검사와 루트 상태 바인딩뿐입니다). 프레임 스트림
         *          조각이 비어 있는지를 디바이스가 이 표시로 압니다. 빈 조각은 잘라 내보내지 않습니다(D3D12RecordingState).
         */
        ID3D12GraphicsCommandList* commandListForRecord();

        void blitTexture( RHITextureHandle src, RHITextureHandle dst ) override;
        void bindShaderResource( RHIDescriptorIndex index, uint32 slot ) override;
        void prepareTextureForShaderRead( RHITextureHandle texture ) override;
        void prepareTextureForRenderTarget( RHITextureHandle texture ) override;
        void prepareTextureForUnorderedAccess( RHITextureHandle texture ) override;
        void bindComputeUav( RHIDescriptorIndex index, uint32 slot ) override;
        void setVertexBuffer( uint32 slot, RHIBufferHandle buffer, uint32 stride, uint32 offset = 0 ) override;
        void draw( uint32 vertexCount, uint32 startVertex = 0 ) override;
        void drawInstanced( uint32 vertexCount, uint32 instanceCount, uint32 startVertex = 0, uint32 startInstance = 0 ) override;
        void bindConstantBuffer( RHIDescriptorIndex constantBufferIndex, uint32 slot ) override;
        void bindStructuredBuffer( RHIDescriptorIndex index, uint32 slot ) override;
        void bindComputeConstantBuffer( RHIDescriptorIndex constantBufferIndex, uint32 slot ) override;
        void bindComputeShaderResource( RHIDescriptorIndex index, uint32 slot ) override;
        void dispatchCompute( uint32 threadGroupCountX, uint32 threadGroupCountY, uint32 threadGroupCountZ ) override;
        void setViewport( const RHIViewport& viewport ) override;
        void drawIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset = 0, uint32 drawCount = 1,
                           RHIBufferHandle countBuffer = 0, uint32 countBufferOffset = 0 ) override;
        void setComputeRootConstants( uint32 rootParameterIndex, uint32 num32BitValues, const void* pData,
                                      uint32 destOffsetIn32BitValues = 0 ) override;
        void setGraphicsRootConstants( uint32 rootParameterIndex, uint32 num32BitValues, const void* pData,
                                       uint32 destOffsetIn32BitValues = 0 ) override;
        void drawIndexedIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset = 0 ) override;
        void dispatchIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset = 0 ) override;
        void beginEventMarker( const utf8* pName ) override;
        void endEventMarker() override;
        void setPipelineState( RHIPipelineStateHandle pso ) override;
        void setComputePipelineState( RHIPipelineStateHandle pso ) override;
        void beginRenderPass( const RHIRenderPassBeginInfo& beginInfo ) override;
        void endRenderPass() override;
        void setIndexBuffer( RHIBufferHandle buffer, uint32 indexStride = 4, uint32 offset = 0 ) override;
        void transitionBuffer( RHIBufferHandle buffer, RHIBufferState newState ) override;
        void uavBarrier( RHIBufferHandle buffer ) override;

    private:
        /**
         * @brief 등록된 bindless 인덱스의 버퍼 GPU 주소입니다. 루트 CBV 에 그대로 겁니다(t · u 는 슬롯 테이블이라 이 길을 쓰지 않습니다).
         * @details 레지스트리는 기록 중 불변이라 락 없이 읽습니다(IRHIDevice::setParallelRecording 참고).
         * @param bUav true 면 UAV 레지스트리, false 면 SRV/CBV 레지스트리.
         * @param bConstantBuffer true 면 링 상수버퍼의 이번 프레임 슬롯 오프셋을 더합니다.
         * @return 인덱스가 범위 밖이거나 슬롯이 비어 있으면 0.
         */
        D3D12_GPU_VIRTUAL_ADDRESS resolveBufferAddress( RHIDescriptorIndex index, bool bUav, bool bConstantBuffer ) const;
        /** @brief 등록된 bindless 인덱스의 오프라인 뷰 핸들입니다(슬롯 테이블 복사 원본). 없으면 ptr 0 입니다. */
        D3D12_CPU_DESCRIPTOR_HANDLE resolveOfflineView( RHIDescriptorIndex index, bool bUav ) const;
        /**
         * @brief 바뀐 t/u 슬롯 테이블을 온라인 블록에 굳혀 루트 테이블로 겁니다. 드로우 · 디스패치 직전에 부릅니다(Vulkan 의 flushSlotSet 과 같은 자리).
         * @param bCompute true 면 컴퓨트 바인드 포인트(t 와 u), false 면 그래픽스(t 만).
         */
        void flushSlotTables( bool bCompute );
        /** @brief 슬롯 배열을 온라인 블록에 복사하고 테이블 GPU 핸들을 반환합니다. 안 걸린 슬롯은 nullView 로 채웁니다. */
        bool writeSlotTable( const D3D12_CPU_DESCRIPTOR_HANDLE* pSlots, uint32 count, D3D12_CPU_DESCRIPTOR_HANDLE nullView, D3D12_GPU_DESCRIPTOR_HANDLE& outTable );
        /** @brief 이 리스트의 온라인 블록에서 count 개를 bump 할당합니다. 블록이 차면 디바이스에서 하나 더 빌립니다. */
        bool allocateOnlineDescriptors( uint32 count, uint32& outBase );
        void bindMeshVertexBuffer();
        /** @brief 메시 정점버퍼가 걸려 있으면 그것을, 없으면 풀스크린 버퍼를 바인딩합니다(Vulkan 과 같은 이름·의미). */
        void bindMeshVertexBufferOrFallback();
        /** @brief 활성 그래픽스 PSO 를 네이티브 리스트에 겁니다(바뀌었을 때만). PSO 가 없으면 false 이고, 드로우를 내지 않습니다. */
        bool bindActiveGraphicsPso();
        void bindFullscreenVertexBuffer();
        void bindBoundIndexBuffer();
        void transitionTexture( RHITextureHandle texture, D3D12_RESOURCE_STATES newState );

        D3D12RHIDevice*            _pDevice;
        ID3D12GraphicsCommandList* _pCmdList;
        D3D12RecordingState*       _pState;
    };
} // namespace sw
#endif
