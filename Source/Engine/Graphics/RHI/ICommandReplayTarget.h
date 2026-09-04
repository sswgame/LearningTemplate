/**
 * @file ICommandReplayTarget.h
 * @brief RHIDeferredCommandList가 replay 시 디바이스에 커맨드를 제출하는 순수 인터페이스
 *
 * @details
 * IRHIDevice가 이 인터페이스를 상속해 replay 대상이 됩니다.
 * 메서드는 protected라 일반 코드에서 직접 호출하지 못하고,
 * RHIDeferredCommandList만 friend로 접근합니다.
 *
 * 백엔드 구현체(D3D12RHIDevice 등)는 IRHIDevice를 통해 한 번만 구현하면 됩니다.
 */
#pragma once
#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
    class RHIDeferredCommandList;

    /**
     * @class ICommandReplayTarget
     * @brief Deferred 커맨드 리스트 replay 전용 디바이스 측 수신 인터페이스
     *        IRHIDevice만 상속하며, 직접 인스턴스화하지 않습니다.
     */
    class SW_API ICommandReplayTarget
    {
    public:
        /** @brief 가상 소멸. */
        virtual ~ICommandReplayTarget()                                = default;
        ICommandReplayTarget()                                         = default;
        ICommandReplayTarget( const ICommandReplayTarget& )            = delete;
        ICommandReplayTarget& operator=( const ICommandReplayTarget& ) = delete;

        // ------------------------------------------------------------------------------
        // 1) Replay — 뷰포트 · PSO · 렌더 패스
        // ------------------------------------------------------------------------------
        /** @brief 리플레이: 뷰포트를 설정합니다. */
        virtual void setViewport( const RHIViewport& viewport ) = 0;
        /** @brief 리플레이: 그래픽스 파이프라인을 바인딩합니다. */
        virtual void setPipelineState( RHIPipelineStateHandle pso ) = 0;
        /** @brief 리플레이: 컴퓨트 파이프라인을 바인딩합니다. */
        virtual void setComputePipelineState( RHIPipelineStateHandle pso ) = 0;
        /** @brief 리플레이: 렌더 패스를 시작합니다. */
        virtual void beginRenderPass( const RHIRenderPassBeginInfo& beginInfo ) = 0;
        /** @brief 리플레이: 렌더 패스를 끝냅니다. */
        virtual void endRenderPass() = 0;

        // ------------------------------------------------------------------------------
        // 2) Replay — 드로우 · 버퍼 바인드
        // ------------------------------------------------------------------------------
        /** @brief 리플레이: 버텍스 버퍼를 바인딩합니다. */
        virtual void setVertexBuffer( uint32 slot, RHIBufferHandle buffer, uint32 stride, uint32 offset = 0 ) = 0;
        /** @brief 리플레이: 삼각형 리스트를 그립니다. */
        virtual void draw( uint32 vertexCount, uint32 startVertex = 0,
                           RHIDescriptorIndex passCbDescriptorIndex     = kInvalidDescriptorIndex,
                           RHIDescriptorIndex materialCbDescriptorIndex = kInvalidDescriptorIndex ) = 0;
        /** @brief 리플레이: 삼각형 리스트를 인스턴스드로 그립니다. 기본은 draw 로 폴백. */
        virtual void drawInstanced( uint32 vertexCount, uint32 instanceCount, uint32 startVertex = 0, uint32 startInstance = 0 )
        {
            (void)instanceCount;
            (void)startInstance;
            draw( vertexCount, startVertex );
        }
        /** @brief 리플레이: 인덱스 버퍼를 바인딩합니다. */
        virtual void setIndexBuffer( RHIBufferHandle buffer, uint32 indexStride = 4, uint32 offset = 0 ) = 0;

        // ------------------------------------------------------------------------------
        // 3) Replay — 컴퓨트 · UAV · SRV
        // ------------------------------------------------------------------------------
        /** @brief 리플레이: 컴퓨트를 디스패치합니다. */
        virtual void dispatchCompute( uint32 threadGroupCountX, uint32 threadGroupCountY, uint32 threadGroupCountZ ) = 0;
        /** @brief 리플레이: 컴퓨트 루트 상수를 푸시합니다. */
        virtual void setComputeRootConstants( uint32 rootParameterIndex, uint32 num32BitValues, const void* pData,
                                              uint32 destOffsetIn32BitValues = 0 ) = 0;
        /** @brief 리플레이: 컴퓨트 UAV를 바인딩합니다. */
        virtual void bindComputeUAV( RHIDescriptorIndex index, uint32 slot ) = 0;
        /** @brief 리플레이: 셰이더 텍스처 슬롯을 바인딩합니다. */
        virtual void bindShaderResource( RHIDescriptorIndex index, uint32 slot )
        {
            (void)index;
            (void)slot;
        }
        /** @brief 리플레이: 상수 버퍼를 레지스터 슬롯(bN)에 바인딩합니다. (ShaderBindingBinder 리플렉션 구동) */
        virtual void bindConstantBuffer( RHIDescriptorIndex cb, uint32 slot )
        {
            (void)cb;
            (void)slot;
        }
        /** @brief 리플레이: 구조적 SRV 버퍼를 레지스터 슬롯(tN)에 바인딩합니다. */
        virtual void bindStructuredBuffer( RHIDescriptorIndex index, uint32 slot )
        {
            (void)index;
            (void)slot;
        }
        /** @brief 리플레이: 컴퓨트 스테이지에 상수 버퍼를 레지스터 슬롯(bN)에 바인딩합니다. */
        virtual void bindComputeConstantBuffer( RHIDescriptorIndex cb, uint32 slot )
        {
            (void)cb;
            (void)slot;
        }
        /** @brief 리플레이: 컴퓨트 스테이지에 구조적 SRV 버퍼를 레지스터 슬롯(tN)에 바인딩합니다. */
        virtual void bindComputeShaderResource( RHIDescriptorIndex index, uint32 slot )
        {
            (void)index;
            (void)slot;
        }

        // ------------------------------------------------------------------------------
        // 4) Replay — 배리어 · blit · 인디렉트
        // ------------------------------------------------------------------------------
        /** @brief 리플레이: 텍스처를 샘플링 가능 상태로 전환합니다. */
        virtual void prepareTextureForShaderRead( RHITextureHandle texture ) { (void)texture; }
        /** @brief 리플레이: 텍스처를 blit합니다. */
        virtual void blitTexture( RHITextureHandle src, RHITextureHandle dst )
        {
            (void)src;
            (void)dst;
        }

        /** @brief 리플레이: GPU 인디렉트 드로우. */
        virtual void drawIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset = 0,
                                   RHIDescriptorIndex passCbDescriptorIndex     = kInvalidDescriptorIndex,
                                   RHIDescriptorIndex materialCbDescriptorIndex = kInvalidDescriptorIndex ) = 0;
        /** @brief 리플레이: GPU 인디렉트 컴퓨트 디스패치. */
        virtual void dispatchIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset = 0 ) = 0;
        /** @brief 리플레이: 버퍼 상태를 전이합니다. */
        virtual void transitionBuffer( RHIBufferHandle buffer, RHIBufferState newState )
        {
            (void)buffer;
            (void)newState;
        }

        /** @brief 리플레이: 인덱스 기반 GPU 인디렉트 드로우. */
        virtual void drawIndexedIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset = 0 ) = 0;
        /** @brief 리플레이: 멀티 인디렉트 드로우. */
        virtual void multiDrawIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset,
                                        uint32 maxCommandCount, RHIBufferHandle countBuffer = 0,
                                        uint32 countBufferOffset = 0 )
        {
            (void)argumentBuffer;
            (void)argumentBufferOffset;
            (void)maxCommandCount;
            (void)countBuffer;
            (void)countBufferOffset;
        }

        // ------------------------------------------------------------------------------
        // 5) Replay — GPU 이벤트 마커
        // ------------------------------------------------------------------------------
        /** @brief 리플레이: GPU 이벤트 마커를 시작합니다. */
        virtual void beginEventMarker( const utf8* pName ) = 0;
        /** @brief 리플레이: GPU 이벤트 마커를 끝냅니다. */
        virtual void endEventMarker() = 0;

        /** RHIDeferredCommandList만 protected 메서드에 접근 가능 */
        friend class RHIDeferredCommandList;
    };
} // namespace sw
