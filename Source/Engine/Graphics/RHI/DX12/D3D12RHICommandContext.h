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
     *        어떤 기록 상태로" 기록할지를 주입받는다 — 디바이스의 레거시 단일 공유 리스트(Immediate/
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

        void blitTexture( RHITextureHandle src, RHITextureHandle dst ) override;
        void bindShaderResource( RHIDescriptorIndex index, uint32 slot ) override;
        void prepareTextureForShaderRead( RHITextureHandle texture ) override;
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
        void beginOffscreenPass( RHITextureHandle colorTarget, const float4& clearColor ) override;
        void endOffscreenPass( RHITextureHandle colorTarget ) override;

    private:
        void bindDescriptorHeaps();
        void bindPassAndMaterialCbv( RHIDescriptorIndex passCbDescriptorIndex, RHIDescriptorIndex materialCbDescriptorIndex );
        void bindMeshVertexBuffer();
        void bindFullscreenVertexBuffer();
        void bindBoundIndexBuffer();
        void transitionTexture( RHITextureHandle texture, D3D12_RESOURCE_STATES newState );

        D3D12RHIDevice*            _pDevice;
        ID3D12GraphicsCommandList* _pCmdList;
        D3D12RecordingState*       _pState;
    };
} // namespace sw
#endif
