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
     * @brief 실제 D3D12 API 호출을 issue 하는 구현체. `_pCmdList`/`_pState` 로 "어떤 커맨드 리스트에,
     *        어떤 기록 상태로" 기록할지를 주입받는다 — 디바이스의 프레임 스트림 공유 리스트(Immediate/
     *        Deferred Context)와, `D3D12RHICommandList`(리스트별 진짜 네이티브 커맨드 리스트)가 각자
     *        자신의 (cmdList, state) 쌍으로 이 클래스를 구성해서 재사용한다.
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
        /** @brief 이 컨텍스트가 기록 중인 네이티브 커맨드 리스트. */
        ID3D12GraphicsCommandList* getNativeCommandList() const { return _pCmdList; }
        /** @brief 기록 대상 네이티브 리스트를 교체합니다(소유자가 얼로케이터 쌍을 바꿔 낄 때). */
        void rebindCommandList( ID3D12GraphicsCommandList* pCmdList ) { _pCmdList = pCmdList; }

        void blitTexture( RHITextureHandle src, RHITextureHandle dst ) override;
        void bindShaderResource( RHIDescriptorIndex index, uint32 slot ) override;
        void prepareTextureForShaderRead( RHITextureHandle texture ) override;
        void prepareTextureForRenderTarget( RHITextureHandle texture ) override;
        void bindComputeUAV( RHIDescriptorIndex index, uint32 slot ) override;
        void setVertexBuffer( uint32 slot, RHIBufferHandle buffer, uint32 stride, uint32 offset = 0 ) override;
        void draw( uint32 vertexCount, uint32 startVertex = 0,
                   RHIDescriptorIndex passCbDescriptorIndex     = kInvalidDescriptorIndex,
                   RHIDescriptorIndex materialCbDescriptorIndex = kInvalidDescriptorIndex ) override;
        void drawInstanced( uint32 vertexCount, uint32 instanceCount, uint32 startVertex = 0, uint32 startInstance = 0 ) override;
        void bindConstantBuffer( RHIDescriptorIndex cb, uint32 slot ) override;
        void bindStructuredBuffer( RHIDescriptorIndex index, uint32 slot ) override;
        void bindComputeConstantBuffer( RHIDescriptorIndex cb, uint32 slot ) override;
        void bindComputeShaderResource( RHIDescriptorIndex index, uint32 slot ) override;
        void dispatchCompute( uint32 threadGroupCountX, uint32 threadGroupCountY, uint32 threadGroupCountZ ) override;
        void setViewport( const RHIViewport& viewport ) override;
        void drawIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset = 0,
                           RHIDescriptorIndex passCbDescriptorIndex     = kInvalidDescriptorIndex,
                           RHIDescriptorIndex materialCbDescriptorIndex = kInvalidDescriptorIndex ) override;
        void multiDrawIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset, uint32 maxCommandCount,
                                RHIBufferHandle countBuffer = 0, uint32 countBufferOffset = 0 ) override;
        void setComputeRootConstants( uint32 rootParameterIndex, uint32 num32BitValues, const void* pData,
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

    private:
        /**
         * @brief 등록된 bindless 슬롯의 GPU 디스크립터 핸들을 **값으로** 꺼내 옵니다.
         * @details 레지스트리(`_listRegisteredBindless` / `_listRegisteredUAV`)는 다른 스레드가
         *          register/unregister 로 **resize** 할 수 있다. 참조를 들고 락 밖으로 나오면 그 사이
         *          재할당에 dangling 이 되고, GPU 가 쓰레기 디스크립터를 읽어 PageFault(VA=0) →
         *          DEVICE_HUNG 으로 이어진다. 그래서 공유 락 안에서 핸들만 복사해 나온다.
         * @param bUav true 면 UAV 레지스트리, false 면 SRV/CBV 레지스트리.
         * @return 인덱스가 범위를 벗어나거나 슬롯이 비어 있으면 false.
         */
        bool tryGetBindlessGpuHandle( RHIDescriptorIndex index, bool bUav, D3D12_GPU_DESCRIPTOR_HANDLE& outHandle ) const;
        void bindDescriptorHeaps();
        void bindPassAndMaterialCbv( RHIDescriptorIndex passCbDescriptorIndex, RHIDescriptorIndex materialCbDescriptorIndex );
        void bindMeshVertexBuffer();
        /** @brief 메시 정점버퍼가 걸려 있으면 그것을, 없으면 풀스크린 버퍼를 바인딩합니다(Vulkan 과 같은 이름·의미). */
        void bindMeshVertexBufferOrFallback();
        void bindFullscreenVertexBuffer();
        void bindBoundIndexBuffer();
        void transitionTexture( RHITextureHandle texture, D3D12_RESOURCE_STATES newState );

        D3D12RHIDevice*            _pDevice;
        ID3D12GraphicsCommandList* _pCmdList;
        D3D12RecordingState*       _pState;
    };
} // namespace sw
#endif
