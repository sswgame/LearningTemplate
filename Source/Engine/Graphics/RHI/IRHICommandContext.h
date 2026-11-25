/**
 * @file IRHICommandContext.h
 * @brief 커맨드 컨텍스트 — 네 백엔드가 같은 렌더 패스 규칙으로 기록하는 기반입니다. 백엔드는 네이티브 동작(`...Native`)만 구현합니다.
 */
#pragma once
#include "Engine/Graphics/RHI/IRHICommandList.h"

namespace sw
{
    class IRHIDevice;

    /** @brief 렌더 패스를 시작하려고 한 결과입니다(`IRHICommandContext::beginRenderPassNative`). */
    enum class RHIRenderPassBeginResult : uint8
    {
        Begun,        ///< 시작했다 — 드로우를 받는다
        NoBackBuffer, ///< 그릴 백버퍼가 없다(최소화 · 스왑체인 재생성 중) — 이 패스의 드로우는 알리지 않고 버린다
        Failed,       ///< 타깃을 걸지 못했다(없는 핸들 · 면 범위 밖) — 이 패스의 드로우는 알리고 버린다
    };

    /**
     * @class IRHICommandContext
     * @brief 곧바로 기록하는 커맨드 컨텍스트입니다. 렌더 패스의 규칙을 네 백엔드가 이 한 곳에서 같이 지킵니다.
     * @details 규칙(언리얼 RHI 의 렌더 패스 · Vulkan 과 같은 모양):
     *          - 패스는 겹치지 않는다. `beginRenderPass` 는 열린 패스를 먼저 닫는다.
     *          - **패스 밖의 일**(텍스처 · 버퍼 상태 전이, 블릿, 되읽기 복사, UAV 배리어, 컴퓨트 PSO · 디스패치)은 열린 패스를 먼저 닫는다 — Vulkan · D3D12 네이티브
     *            패스는 그 안에 배리어 · 복사 · 디스패치를 기록할 수 없다. 열린 패스를 이렇게 닫으면 그 패스의 남은 드로우가 빠지므로 알린다
     *            (`RHI.RenderPassInterrupted`, 언리얼 RHI 검증과 같다). 부르는 쪽은 `endRenderPass` 를 먼저 부르거나 입력을 패스 전에 건다.
     *          - **드로우는 열린 패스 안에서만** 나간다. 패스가 없으면 알리고 버린다(`RHI.DrawSkippedNoRenderPass`). 백버퍼가 없던 패스(`RHIRenderPassBeginResult::NoBackBuffer`)만
     *            조용히 버린다.
     *          예전에는 백엔드마다 따로 지켜, Vulkan 만 패스 안의 업로드가 패스를 닫고 그 뒤 드로우를 조용히 버렸고(에디터 UI 가 화면에서 사라졌다) DX11 · DX12 · GL 은
     *          같은 위반이 그냥 지나가 시나리오가 놓쳤다. 이제 어느 백엔드로 돌려도 같은 위반이 같은 오류다. `beginCommandList` · `endCommandList` 는 리스트 쪽에만 뜻이 있다.
     */
    class SW_API IRHICommandContext : public IRHICommandList
    {
    public:
        IRHICommandContext();
        ~IRHICommandContext() override                             = default;
        IRHICommandContext( const IRHICommandContext& )            = delete;
        IRHICommandContext& operator=( const IRHICommandContext& ) = delete;

        void beginCommandList() override {}
        void endCommandList() override {}

        // ------------------------------------------------------------------------------
        // 렌더 패스
        // ------------------------------------------------------------------------------
        void beginRenderPass( const RHIRenderPassBeginInfo& beginInfo ) final;
        void endRenderPass() final;
        /** @brief 지금 렌더 패스가 열려 있으면 true 입니다(패스 안의 업로드 판정 — `IRHIDevice::beginBufferUpload`). */
        bool isRenderPassOpen() const { return _bRenderPassOpen; }

        // ------------------------------------------------------------------------------
        // 패스 안의 일 — 열린 패스가 없으면 버린다
        // ------------------------------------------------------------------------------
        void draw( uint32 vertexCount, uint32 startVertex = 0 ) final;
        void drawInstanced( uint32 vertexCount, uint32 instanceCount, uint32 startVertex = 0, uint32 startInstance = 0 ) final;
        void drawIndexedInstanced( uint32 indexCount, uint32 instanceCount, uint32 startIndex = 0, int32 baseVertex = 0, uint32 startInstance = 0 ) final;
        void drawIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset = 0, uint32 drawCount = 1, RHIBufferHandle countBuffer = 0, uint32 countBufferOffset = 0 ) final;
        void drawIndexedIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset = 0, uint32 drawCount = 1, RHIBufferHandle countBuffer = 0,
                                  uint32 countBufferOffset = 0 ) final;

        // ------------------------------------------------------------------------------
        // 패스 밖의 일 — 열린 패스를 먼저 닫는다
        // ------------------------------------------------------------------------------
        void setComputePipelineState( RHIPipelineStateHandle pso ) final;
        void dispatchCompute( uint32 threadGroupCountX, uint32 threadGroupCountY, uint32 threadGroupCountZ ) final;
        void dispatchThreads( uint32 threadCountX, uint32 threadCountY, uint32 threadCountZ ) final;
        void dispatchIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset = 0 ) final;
        /**
         * @brief 컴퓨트 슬롯에 건 버퍼 번호를 기억합니다 — 다음 디스패치가 걸린 PSO 와 함께 `RHIComputeBindingLog` 에 적는다(버퍼 보기). 개발 구성에서만 부른다(`RHICommandListForwarder`).
         * @details 번호를 걸 때 적지 않는 것은 PSO 를 뒤에 거는 자리가 있어서다 — 디스패치 순간의 PSO 가 그 번호를 읽는 셰이더다. 디스패치가 적으면 비운다(앞 셰이더가 건 번호를 다음 PSO 의 것으로 적지 않게).
         */
        void trackComputeBinding( RHIDescriptorIndex index, bool bUAV, uint32 slot );
        void prepareTextureForShaderRead( RHITextureHandle texture ) final;
        void prepareTextureForRenderTarget( RHITextureHandle texture ) final;
        void prepareTextureForUnorderedAccess( RHITextureHandle texture ) final;
        void transitionTextures( const RHITextureTransition* pTransition, uint32 count ) final;
        void blitTexture( RHITextureHandle src, RHITextureHandle dst ) final;
        void transitionBuffer( RHIBufferHandle buffer, RHIBufferState newState ) final;
        void transitionBuffers( const RHIBufferTransition* pTransition, uint32 count ) final;
        void copyBufferToReadback( RHIBufferHandle source, RHIBufferHandle readback, uint32 sizeBytes ) final;
        void copyBufferRegion( RHIBufferHandle destination, uint64 destinationOffset, RHIBufferHandle source, uint64 sourceOffset, uint64 sizeBytes ) final;
        void uavBarrier( RHIBufferHandle buffer ) final;
        void uavBarrierTexture( RHITextureHandle texture ) final;

        // ------------------------------------------------------------------------------
        // 기록 경로의 상수 버퍼 쓰기 — 한 프레임에 한 버퍼는 한 번(`IRHIDevice::getConstantBufferWriteLog`)
        // ------------------------------------------------------------------------------
        /** @brief 이번 프레임에 이미 쓴 버퍼를 다시 쓰면 진단 오류를 남깁니다(DX12 · Vulkan · GL 은 앞 드로우도 마지막 값을 읽는다 — R-12). 쓰기는 그대로 낸다. */
        void updateConstantBuffer( RHIBufferHandle buffer, const void* pData, uint32 size ) final;

    protected:
        /** @brief 이 문맥을 만든 디바이스입니다. 드로우 진단(`IRHIDevice::getDrawDiagnostics`)을 거기서 받는다 — 진단 횟수는 디바이스 멤버다. */
        virtual IRHIDevice& getContextDevice() const = 0;
        /** @brief 네이티브 패스를 엽니다. 열린 패스는 기반이 이미 닫았다. 상태 전이는 여기서 패스를 열기 전에 끝낸다. */
        virtual RHIRenderPassBeginResult beginRenderPassNative( const RHIRenderPassBeginInfo& beginInfo ) = 0;
        /** @brief 열린 네이티브 패스를 닫습니다. 기반이 열린 패스가 있을 때만 부른다. */
        virtual void endRenderPassNative()                                                                                                                                      = 0;
        virtual void drawNative( uint32 vertexCount, uint32 startVertex )                                                                                                       = 0;
        virtual void drawInstancedNative( uint32 vertexCount, uint32 instanceCount, uint32 startVertex, uint32 startInstance )                                                  = 0;
        virtual void drawIndexedInstancedNative( uint32 indexCount, uint32 instanceCount, uint32 startIndex, int32 baseVertex, uint32 startInstance )                           = 0;
        virtual void drawIndirectNative( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset, uint32 drawCount, RHIBufferHandle countBuffer, uint32 countBufferOffset ) = 0;
        virtual void drawIndexedIndirectNative( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset, uint32 drawCount, RHIBufferHandle countBuffer,
                                                uint32 countBufferOffset )                                                                                                      = 0;
        virtual void setComputePipelineStateNative( RHIPipelineStateHandle pso )                                                                                                = 0;
        virtual void dispatchComputeNative( uint32 threadGroupCountX, uint32 threadGroupCountY, uint32 threadGroupCountZ )                                                      = 0;
        virtual void dispatchIndirectNative( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset )                                                                      = 0;
        virtual void prepareTextureForShaderReadNative( RHITextureHandle texture )                                                                                              = 0;
        virtual void prepareTextureForRenderTargetNative( RHITextureHandle texture )                                                                                            = 0;
        virtual void prepareTextureForUnorderedAccessNative( RHITextureHandle texture )                                                                                         = 0;
        /** @brief 여러 텍스처 전이를 냅니다. 기본은 텍스처마다 `prepareTextureFor*Native` 다 — 네이티브 배리어를 묶을 수 있는 백엔드(DX12 · Vulkan)가 덮어 한 번에 낸다. */
        virtual void transitionTexturesNative( const RHITextureTransition* pTransition, uint32 count );
        virtual void blitTextureNative( RHITextureHandle src, RHITextureHandle dst )           = 0;
        virtual void transitionBufferNative( RHIBufferHandle buffer, RHIBufferState newState ) = 0;
        /** @brief 여러 전이를 냅니다. 기본은 버퍼마다 `transitionBufferNative` 다 — 네이티브 배리어를 묶을 수 있는 백엔드(DX12 · Vulkan)가 덮어 한 번에 낸다. */
        virtual void transitionBuffersNative( const RHIBufferTransition* pTransition, uint32 count );
        virtual void copyBufferToReadbackNative( RHIBufferHandle source, RHIBufferHandle readback, uint32 sizeBytes ) = 0;
        /** @brief 버퍼의 바이트 크기입니다. 모르는(놓은) 핸들이면 0 입니다. 복사의 범위 검사(`copyBufferRegion` · `copyBufferToReadback`)가 백엔드마다 갈리지 않게 여기서 묻는다. */
        virtual uint64 findBufferBytes( RHIBufferHandle buffer ) const = 0;
        /** @brief `copyBufferRegion` 의 백엔드 몸통입니다(렌더 패스는 닫혀 있고 인자는 검사를 지났다). */
        virtual void copyBufferRegionNative( RHIBufferHandle destination, uint64 destinationOffset, RHIBufferHandle source, uint64 sourceOffset, uint64 sizeBytes ) = 0;
        virtual void uavBarrierNative( RHIBufferHandle buffer )                                                                                                     = 0;
        /** @brief `uavBarrierTexture` 의 백엔드 몸통입니다(핸들 0 은 기반이 걸렀고 렌더 패스는 닫혀 있다). */
        virtual void uavBarrierTextureNative( RHITextureHandle texture ) = 0;
        /** @brief 버퍼의 이번 프레임 슬롯(DX11 은 이 리스트의 판)에 씁니다. 두 번째 쓰기 진단은 기반이 이미 했다. */
        virtual void updateConstantBufferNative( RHIBufferHandle buffer, const void* pData, uint32 size ) = 0;

    private:
        /** @brief 패스 밖의 일 @p pOperation 앞에서 열린 패스를 닫습니다. 열려 있었으면 알린다(`RHIDrawDiagnostics::reportRenderPassInterrupted`) — 닫기는 그대로 한다. */
        void endRenderPassBefore( const utf8* pOperation );
        /** @brief 드로우를 내도 되는지 봅니다. 패스가 없으면 백버퍼가 없던 패스면 조용히, 아니면 알리고 false 입니다. */
        [[nodiscard]] bool isDrawAllowed() const;
        /** @brief 텍스처 핸들이 0 이 아니면 true 입니다. 0 이면 @p pOperation 이름으로 알리고 false 입니다(백버퍼는 `kRHIBackBufferTexture`). */
        [[nodiscard]] bool isTextureHandleValid( RHITextureHandle texture, const utf8* pOperation ) const;

        /** @brief (개발 구성) 이번 프레임이 @p buffer 의 전이 · 복사를 기록한다고 적습니다 — 다른 스레드의 갱신 판정(`IRHIDevice::beginBufferUpload`)이 읽는다. */
        void recordBufferUse( RHIBufferHandle buffer );

    private:
        static constexpr uint32 kTrackedComputeSlotCount = 8; ///< 슬롯 종류마다 기억하는 칸(binding_slots.hlsli 의 컴퓨트 슬롯보다 넉넉하다)

        /** @brief 앞 디스패치 뒤에 건 컴퓨트 버퍼를 지금 PSO 와 함께 기록하고(기록이 켜져 있을 때만) 비웁니다. */
        void recordComputeBindings();
        void clearTrackedComputeBindings();

    private:
        RHIDescriptorIndex     _arrTrackedCompute[2][kTrackedComputeSlotCount]; ///< [0] SRV · [1] UAV 슬롯에 걸린 번호
        RHIPipelineStateHandle _computePSO{ 0 };                                ///< 걸려 있는 컴퓨트 PSO
        RHIThreadGroupSize     _computeGroupSize{};                             ///< 걸려 있는 컴퓨트 PSO 의 그룹 크기(`setComputePipelineState`)
        uint8                  _bRenderPassOpen         : 1;
        uint8                  _bRenderPassNoBackBuffer : 1;
    };
} // namespace sw
