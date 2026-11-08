/**
 * @file WebGPURHICommandContext.h
 * @brief WebGPU 기록 컨텍스트와 패스별 커맨드 리스트입니다. 슬롯은 DX11 처럼 기록 상태에 적었다가 그리기 · 디스패치 직전에 바인드 그룹 0 으로 짓습니다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Graphics/RHI/IRHICommandContext.h"
#include "Engine/Graphics/RHI/RHICommandListForwarder.h"
#include "Engine/Graphics/RHI/RHIGeometryBindings.h"
#include "Engine/Graphics/RHI/WebGPU/WebGPURHIUtil.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"

namespace sw
{
    class WebGPURHIDevice;
    struct WebGPUPipelineRecord;

    /** @brief 지금 건 슬롯 · 루트 상수입니다. 그래픽스와 컴퓨트를 따로 든다(DX11 의 VS · PS 와 CS 슬롯이 따로인 것과 같다). */
    struct WebGPUSlotState
    {
        RHIDescriptorIndex     _arrConstantBuffer[shaderslot::wgpu::kBRange];
        RHIDescriptorIndex     _arrShaderResource[shaderslot::wgpu::kTRange]; ///< t 슬롯 — 머티리얼 · 엔진 텍스처 칸(t16..)은 텍스처 번호, 그 앞은 버퍼 번호
        RHIDescriptorIndex     _arrUnorderedAccess[shaderslot::wgpu::kURange];
        uint32                 _arrRootConstant[shaderslot::kRootConstantDwords];
        WGPUBuffer             _rootSliceBuffer; ///< 지금 루트 상수를 담은 상수 링 칸(같은 값이면 그리기마다 새 칸을 받지 않는다 — 바인드 그룹 캐시가 맞으려면 칸이 같아야 한다)
        uint64                 _rootSliceOffset;
        uint64                 _rootSliceSerial;      ///< 그 칸을 받은 프레임 시리얼(다른 프레임이면 링이 다시 쓰였다)
        uint8                  _bRootConstantSet : 1; ///< 루트 상수를 한 번이라도 적었다(아니면 b2 는 0 으로 채운 칸이다)
        uint8                  _bRootSliceDirty  : 1; ///< 칸을 받은 뒤 루트 상수가 바뀌었다
        [[maybe_unused]] uint8 _reserved         : 6;

        WebGPUSlotState();
    };

    /** @brief 기록 하나의 상태입니다(프레임 스트림 · 리스트마다 하나). */
    struct WebGPURecordingState
    {
        RHIGeometryBindings    _geometry{};
        WebGPUSlotState        _graphics{};
        WebGPUSlotState        _compute{};
        RHIPipelineStateHandle _graphicsPSO{ 0 };
        RHIPipelineStateHandle _computePSO{ 0 };
        RHIPipelineStateHandle _boundRenderPSO{ 0 }; ///< 열린 패스에 실제로 건 파이프라인(바뀔 때만 다시 건다)
        WGPURenderPassEncoder  _renderPass{ nullptr };
        WGPUComputePassEncoder _computePass{ nullptr };
    };

    /**
     * @class WebGPURHICommandContext
     * @brief WebGPU 명령을 내는 구현체입니다. 프레임 스트림은 디바이스의 프레임 인코더에, 리스트는 자기 인코더에 기록합니다(`_bOwnEncoder`).
     */
    class WebGPURHICommandContext : public IRHICommandContext
    {
    public:
        /** @brief @p bOwnEncoder 가 거짓이면 디바이스의 프레임 인코더에 기록하는 프레임 스트림입니다. 참이면 `beginCommandList` 가 자기 인코더를 연다. */
        WebGPURHICommandContext( WebGPURHIDevice* pDevice, bool bOwnEncoder );
        ~WebGPURHICommandContext() override;
        WebGPURHICommandContext( const WebGPURHICommandContext& )            = delete;
        WebGPURHICommandContext& operator=( const WebGPURHICommandContext& ) = delete;

        void beginCommandList() override;
        void endCommandList() override;
        /** @brief `endCommandList` 가 닫은 커맨드 버퍼를 넘깁니다(소유가 옮겨 간다). 없으면 nullptr 입니다. */
        [[nodiscard]] WGPUCommandBuffer takeFinishedCommandBuffer();
        /** @brief 디바이스가 먼저 내려갔다 — 들고 있는 WebGPU 객체를 놓고 디바이스를 잊습니다. */
        void detachFromDevice();
        /** @brief 열린 계산 패스를 닫습니다(렌더 패스 · 복사 앞, 리스트 끝). */
        void endComputePass();
        /** @brief 이 지점의 GPU 시각을 @p slotIndex 에 적습니다(빈 계산 패스의 시작 시각). 렌더 패스 안이면 적지 않고 오류로 남긴다 — 엔진은 패스 사이에서만 적는다. */
        void writeTimestamp( uint32 slotIndex ) override;

        void setViewport( const RHIViewport& viewport ) override;
        void setScissorRect( const RHIScissorRect& rect ) override;
        void setPipelineState( RHIPipelineStateHandle pso ) override;
        void setVertexBuffer( uint32 slot, RHIBufferHandle buffer, uint32 stride, uint32 offset = 0 ) override;
        void setIndexBuffer( RHIBufferHandle buffer, uint32 indexStride = 4, uint32 offset = 0 ) override;
        void setComputeRootConstants( uint32 rootParameterIndex, uint32 num32BitValues, const void* pData, uint32 destOffsetIn32BitValues = 0 ) override;
        void setGraphicsRootConstants( uint32 rootParameterIndex, uint32 num32BitValues, const void* pData, uint32 destOffsetIn32BitValues = 0 ) override;
        void bindComputeUAV( RHIDescriptorIndex index, uint32 slot ) override;
        void bindGraphicsUAV( RHIDescriptorIndex index, uint32 slot ) override;
        void bindShaderResource( RHIDescriptorIndex index, uint32 slot ) override;
        void bindConstantBuffer( RHIDescriptorIndex constantBufferIndex, uint32 slot ) override;
        void updateConstantBuffer( RHIBufferHandle buffer, const void* pData, uint32 size ) override;
        void bindStructuredBuffer( RHIDescriptorIndex index, uint32 slot ) override;
        void bindComputeConstantBuffer( RHIDescriptorIndex constantBufferIndex, uint32 slot ) override;
        void bindComputeShaderResource( RHIDescriptorIndex index, uint32 slot ) override;
        void beginEventMarker( const utf8* pName ) override;
        void endEventMarker() override;
        /** @brief WebGPU 에는 GPU 브레드크럼이 없다(`_bGPUBreadcrumbs` 거짓) — 디버그 표지만 남긴다. */
        void writeBreadcrumb( uint32 markerID ) override;

    protected:
        IRHIDevice&              getContextDevice() const override;
        RHIRenderPassBeginResult beginRenderPassNative( const RHIRenderPassBeginInfo& beginInfo ) override;
        void                     endRenderPassNative() override;
        void                     drawNative( uint32 vertexCount, uint32 startVertex ) override;
        void                     drawInstancedNative( uint32 vertexCount, uint32 instanceCount, uint32 startVertex, uint32 startInstance ) override;
        void                     drawIndexedInstancedNative( uint32 indexCount, uint32 instanceCount, uint32 startIndex, int32 baseVertex, uint32 startInstance ) override;
        void                     drawIndirectNative( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset, uint32 drawCount, RHIBufferHandle countBuffer, uint32 countBufferOffset ) override;
        void                     drawIndexedIndirectNative( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset, uint32 drawCount, RHIBufferHandle countBuffer, uint32 countBufferOffset ) override;
        void                     setComputePipelineStateNative( RHIPipelineStateHandle pso ) override;
        void                     dispatchComputeNative( uint32 threadGroupCountX, uint32 threadGroupCountY, uint32 threadGroupCountZ ) override;
        void                     dispatchIndirectNative( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset ) override;
        /**
         * @brief WebGPU 는 자원 상태를 구현이 따라간다 — 전이가 없다(언리얼 · 유니티의 WebGPU 백엔드도 같다, 기억). 깊이 텍스처는 여기서 R32 실수 사본을 채운다.
         * @details 깊이를 읽는 셰이더는 사본을 읽는다(`WebGPURHIDevice::createDepthSampleCopy`). 다른 백엔드도 깊이를 읽기 전에 이 함수를 부르는 계약이라(Vulkan 레이아웃 전이) 자리가 같다.
         */
        void prepareTextureForShaderReadNative( RHITextureHandle texture ) override;
        void prepareTextureForRenderTargetNative( RHITextureHandle texture ) override { (void)texture; }
        void prepareTextureForUnorderedAccessNative( RHITextureHandle texture ) override { (void)texture; }
        void blitTextureNative( RHITextureHandle src, RHITextureHandle dst ) override;
        void transitionBufferNative( RHIBufferHandle buffer, RHIBufferState newState ) override;
        void copyBufferToReadbackNative( RHIBufferHandle source, RHIBufferHandle readback, uint32 sizeBytes ) override;
        /** @brief 같은 패스 안의 디스패치 사이는 구현이 순서를 지킨다 — 배리어가 없다. */
        void uavBarrierNative( RHIBufferHandle buffer ) override { (void)buffer; }

    private:
        /** @brief 지금 기록할 인코더입니다(프레임 스트림은 디바이스 것, 리스트는 자기 것). */
        WGPUCommandEncoder getEncoder() const;
        /** @brief 계산 패스를 엽니다(없으면). 렌더 패스가 열려 있으면 기반이 이미 닫았다. */
        WGPUComputePassEncoder ensureComputePass();
        /**
         * @brief 파이프라인의 그룹 0 을 지금 슬롯으로 찾거나 짓습니다(장치의 프레임 캐시 — 그룹은 캐시가 가진다). 셰이더가 자원을 쓰지 않으면 nullptr 입니다(걸지 않는다).
         * @details 루트 상수 칸은 값이 바뀔 때만 새로 받는다(`WebGPUSlotState::_bRootSliceDirty`) — 받을 때마다 다른 칸이면 같은 재질의 그리기도 캐시가 맞지 않는다.
         */
        [[nodiscard]] WGPUBindGroup findBindGroup( const WebGPUPipelineRecord& pipeline, WebGPUSlotState& slots );
        /** @brief 텍스처 칸 @p binding 에 걸 뷰입니다. 깊이 텍스처는 이 프레임에 채운 R32 사본이어야 한다 — 아니면 오류로 남긴다(`prepareTextureForShaderRead` 누락). */
        [[nodiscard]] WGPUTextureView resolveSampledView( RHIDescriptorIndex index ) const;
        /** @brief 그리기 직전에 파이프라인 · 그룹 · 정점 버퍼를 겁니다. 걸 수 없으면 false 이고 그리기를 버린다. */
        [[nodiscard]] bool prepareDraw();
        /** @brief 인덱스 버퍼를 겁니다(인덱스 그리기). 걸지 않았거나 부서졌으면 false 입니다. */
        [[nodiscard]] bool prepareIndexBuffer();
        /** @brief 디스패치 직전에 계산 파이프라인과 그룹을 겁니다. 등록된 계산 PSO 가 없으면 오류로 멈춘다(네 백엔드와 같은 계약). */
        [[nodiscard]] WGPUComputePassEncoder prepareDispatch();
        /** @brief 루트 상수 @p num32BitValues 개를 @p slots 의 그림자에 적습니다. 범위를 넘으면 오류로 멈춘다. */
        static void writeRootConstants( WebGPUSlotState& slots, uint32 num32BitValues, const void* pData, uint32 destOffsetIn32BitValues );

    private:
        WebGPURHIDevice*       _pDevice;
        WGPUCommandEncoder     _ownEncoder;
        WGPUCommandBuffer      _finishedCommandBuffer;
        WebGPURecordingState   _state;
        uint8                  _bOwnEncoder : 1;
        [[maybe_unused]] uint8 _reserved    : 7;
    };

    /**
     * @class WebGPURHICommandList
     * @brief 패스별 커맨드 리스트입니다. 자기 인코더에 기록하고, 디바이스의 `executeCommandList` 가 프레임 스트림 순서에 끼워 제출합니다.
     */
    class WebGPURHICommandList : public RHICommandListForwarder<WebGPURHICommandContext>
    {
    public:
        explicit WebGPURHICommandList( WebGPURHIDevice* pDevice );
        ~WebGPURHICommandList() override;
        WebGPURHICommandList( const WebGPURHICommandList& )            = delete;
        WebGPURHICommandList& operator=( const WebGPURHICommandList& ) = delete;

        void beginCommandList() override { _context.beginCommandList(); }
        void endCommandList() override { _context.endCommandList(); }
        void writeTimestamp( uint32 slotIndex ) override { _context.writeTimestamp( slotIndex ); }
        /** @brief 닫은 커맨드 버퍼를 넘깁니다(디바이스가 제출 목록에 넣는다). */
        [[nodiscard]] WGPUCommandBuffer takeFinishedCommandBuffer() { return _context.takeFinishedCommandBuffer(); }
        /** @brief 디바이스가 먼저 내려갔다 — 리스트의 객체를 놓습니다(`RHILiveCommandListUtil::detachAll`). */
        void detachFromDevice();

    private:
        WebGPURHIDevice*        _pDevice;
        WebGPURHICommandContext _context;
    };
} // namespace sw
