/**
 * @file IRHICommandContext.h
 * @brief RHI 커맨드 컨텍스트 인터페이스 및 관련 선언
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "Engine/Graphics/RHI/RHICommandListDefaults.h"
#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
    /**
     * @class IRHICommandList
     * @brief GPU 그래픽스/컴퓨트 명령을 기록하는 커맨드 리스트
     */
    class SW_API IRHICommandList
    {
    public:
        // ------------------------------------------------------------------------------
        // 1) 수명 — 복사 금지
        // ------------------------------------------------------------------------------
        virtual ~IRHICommandList()                           = default;
        IRHICommandList()                                    = default;
        IRHICommandList( const IRHICommandList& )            = delete;
        IRHICommandList& operator=( const IRHICommandList& ) = delete;

        // ------------------------------------------------------------------------------
        // 2) 기록 범위 · 뷰포트 · PSO · 렌더 패스
        // ------------------------------------------------------------------------------
        virtual void beginCommandList()                                         = 0;
        virtual void endCommandList()                                           = 0;
        virtual void setViewport( const RHIViewport& viewport )                 = 0;
        virtual void setPipelineState( RHIPipelineStateHandle pso )             = 0;
        virtual void beginRenderPass( const RHIRenderPassBeginInfo& beginInfo ) = 0;
        virtual void endRenderPass()                                            = 0;

        // ------------------------------------------------------------------------------
        // 3) 드로우 — 메시 버텍스/인덱스, 머티리얼 CB
        // ------------------------------------------------------------------------------
        virtual void setVertexBuffer( uint32 slot, RHIBufferHandle buffer, uint32 stride, uint32 offset = 0 ) = 0;
        /**
         * @brief 삼각형 리스트를 그립니다.
         * @param passCbDescriptorIndex b0(PassCB) 에 바인딩할 상수 버퍼.
         * @param materialCbDescriptorIndex b1(MaterialCB) 에 바인딩할 상수 버퍼.
         */
        virtual void draw( uint32 vertexCount, uint32 startVertex = 0,
                           RHIDescriptorIndex passCbDescriptorIndex     = kInvalidDescriptorIndex,
                           RHIDescriptorIndex materialCbDescriptorIndex = kInvalidDescriptorIndex ) = 0;
        /**
         * @brief 삼각형 리스트를 인스턴스드로 그립니다 (GPUScene 인스턴스 버퍼 경로).
         * @param instanceCount 인스턴스 개수. VS 는 `SV_InstanceID` 로 `g_SwInstances[g_InstanceBase + id]` 를 읽습니다.
         * @param startInstance 시작 인스턴스 위치. 크로스 백엔드 일관성을 위해 셰이더 오프셋은 `g_InstanceBase` 로 넘기고
         *                      이 값은 0 을 권장합니다 (SV_InstanceID 는 백엔드마다 base 포함 여부가 다름).
         */
        virtual void drawInstanced( uint32 vertexCount, uint32 instanceCount, uint32 startVertex = 0, uint32 startInstance = 0 ) = 0;
        virtual void setIndexBuffer( RHIBufferHandle buffer, uint32 indexStride = 4, uint32 offset = 0 )                         = 0;

        // ------------------------------------------------------------------------------
        // 4) 컴퓨트 — PSO, 디스패치, 루트 상수, UAV
        // ------------------------------------------------------------------------------
        virtual void setComputePipelineState( RHIPipelineStateHandle pso )                                           = 0;
        virtual void dispatchCompute( uint32 threadGroupCountX, uint32 threadGroupCountY, uint32 threadGroupCountZ ) = 0;
        /**
         * @brief 컴퓨트 루트/푸시 상수를 씁니다.
         * @details 백엔드마다 실제 용량(dword)이 다르다 — DX11=64, OpenGL=64, Vulkan=32, DX12=16
         *          (각 백엔드 헤더의 kMaxComputeRootConstantDwords 참고). 4개 백엔드 모두에서 안전한
         *          상한은 constant::kMinComputeRootConstantDwords(=DX12 기준) — 그 이상을 쓰면 DX12에서
         *          조용히 잘리거나 덮어써질 수 있다.
         */
        virtual void setComputeRootConstants( uint32 rootParameterIndex, uint32 num32BitValues, const void* pData, uint32 destOffsetIn32BitValues = 0 ) = 0;
        virtual void bindComputeUAV( RHIDescriptorIndex index, uint32 slot )                                                                            = 0;
        virtual void bindShaderResource( RHIDescriptorIndex index, uint32 slot )                                                                        = 0;

        // ------------------------------------------------------------------------------
        // 4-1) 리플렉션 구동 바인딩 — 셰이더가 선언한 레지스터로 CB/SRV 버퍼를 바인딩
        //      (ShaderBindingBinder 가 ShaderBindingLayout 의 _registerIndex 를 slot 으로 전달)
        // ------------------------------------------------------------------------------
        /**
         * @brief 상수 버퍼를 지정한 레지스터 슬롯(bN)에 바인딩합니다.
         * @param cb   bindless 디스크립터 인덱스 (엔진/머티리얼 CB).
         * @param slot HLSL `register(bN)` 의 N. 네이티브 bindless 백엔드는 루트상수/CB에 인덱스만 기록해도 됩니다.
         */
        virtual void bindConstantBuffer( RHIDescriptorIndex cb, uint32 slot ) = 0;

        /**
         * @brief 구조적/바이트주소 SRV 버퍼를 지정한 레지스터 슬롯(tN)에 바인딩합니다.
         * @param index bindless 디스크립터 인덱스.
         * @param slot  HLSL `register(tN)` 의 N.
         */
        virtual void bindStructuredBuffer( RHIDescriptorIndex index, uint32 slot ) = 0;

        /**
         * @brief 컴퓨트 스테이지에 상수 버퍼를 지정한 레지스터 슬롯(bN)에 바인딩합니다.
         * @details gpucull 등 컴퓨트 패스 전용 — bindConstantBuffer 는 그래픽스 스테이지만 대상으로 합니다.
         * @param cb   bindless 디스크립터 인덱스.
         * @param slot HLSL `register(bN)` 의 N.
         */
        virtual void bindComputeConstantBuffer( RHIDescriptorIndex cb, uint32 slot ) = 0;

        /**
         * @brief 컴퓨트 스테이지에 읽기전용 구조적 SRV 버퍼를 지정한 레지스터 슬롯(tN)에 바인딩합니다.
         * @param index bindless 디스크립터 인덱스.
         * @param slot  HLSL `register(tN)` 의 N.
         */
        virtual void bindComputeShaderResource( RHIDescriptorIndex index, uint32 slot ) = 0;

        // ------------------------------------------------------------------------------
        // 5) 배리어 · blit — 샘플링 가능 전환, 컬러 복사 (dst==0은 스왑체인)
        // ------------------------------------------------------------------------------
        virtual void prepareTextureForShaderRead( RHITextureHandle texture )   = 0;
        virtual void blitTexture( RHITextureHandle src, RHITextureHandle dst ) = 0;

        // ------------------------------------------------------------------------------
        // 6) 인디렉트 — 드로우/디스패치, 버퍼 상태 전이, 멀티 드로우
        // ------------------------------------------------------------------------------
        virtual void drawIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset = 0,
                                   RHIDescriptorIndex passCbDescriptorIndex     = kInvalidDescriptorIndex,
                                   RHIDescriptorIndex materialCbDescriptorIndex = kInvalidDescriptorIndex ) = 0;
        virtual void dispatchIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset = 0 )    = 0;
        virtual void transitionBuffer( RHIBufferHandle buffer, RHIBufferState newState )                    = 0;
        virtual void drawIndexedIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset = 0 ) = 0;
        virtual void multiDrawIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset, uint32 maxCommandCount,
                                        RHIBufferHandle countBuffer = 0, uint32 countBufferOffset = 0 )
        {
            defaultMultiDrawIndirect( argumentBuffer, argumentBufferOffset, maxCommandCount, countBuffer, countBufferOffset,
                                      [this]( RHIBufferHandle buf, uint32 off )
            { drawIndirect( buf, off ); } );
        }

        // ------------------------------------------------------------------------------
        // 7) GPU 디버그 마커
        // ------------------------------------------------------------------------------
        virtual void beginEventMarker( const utf8* pName ) = 0;
        virtual void endEventMarker()                      = 0;
    };

    /**
     * @class IRHICommandContext
     * @brief 즉시 실행 가능한 커맨드 컨텍스트 인터페이스.
     * @details `IRHICommandList`와 같은 기록 API 표면을 공유합니다 — `beginCommandList`/`endCommandList`는
     *          컨텍스트에는 "기록 범위" 개념이 없어 no-op으로 막아 둡니다(리스트 쪽 구현체만 의미 있게 씀).
     */
    class SW_API IRHICommandContext : public IRHICommandList
    {
    public:
        IRHICommandContext()                                       = default;
        ~IRHICommandContext() override                             = default;
        IRHICommandContext( const IRHICommandContext& )            = delete;
        IRHICommandContext& operator=( const IRHICommandContext& ) = delete;

        void beginCommandList() override {}
        void endCommandList() override {}
    };

} // namespace sw
