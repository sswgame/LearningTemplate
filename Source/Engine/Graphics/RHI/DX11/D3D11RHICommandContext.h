#pragma once
#include "Core/Common/Types.h"

#include "Engine/Common/EnginePlatformHeaders.h"
#include "Engine/Graphics/RHI/IRHICommandContext.h"

#if defined( SW_PLATFORM_WINDOWS )
namespace sw
{
    class D3D11RHIDevice;

    /**
     * @class D3D11RHICommandContext
     * @brief 실제 D3D11 API 호출을 issue 하는 구현체. `_pContext` 로 "어떤 `ID3D11DeviceContext` 에
     *        기록할지" 를 주입받는다 — 디바이스의 단일 Immediate Context(레거시 Immediate/Deferred Context
     *        슬롯)와, `D3D11RHICommandList`(리스트별 진짜 네이티브 Deferred Context)가 각자 자신의
     *        `ID3D11DeviceContext*` 로 이 클래스를 구성해서 재사용한다.
     */
    class D3D11RHICommandContext : public IRHICommandContext
    {
    public:
        D3D11RHICommandContext( D3D11RHIDevice* pDevice, ID3D11DeviceContext* pContext )
            : _pDevice{ pDevice }
            , _pContext{ pContext } {}
        ~D3D11RHICommandContext() override = default;

        void blitTexture( RHITextureHandle src, RHITextureHandle dst ) override;
        void bindShaderResource( RHIDescriptorIndex index, uint32 slot ) override;
        void prepareTextureForShaderRead( RHITextureHandle texture ) override;
        void bindComputeUAV( RHIDescriptorIndex index, uint32 slot ) override;
        void setVertexBuffer( uint32 slot, RHIBufferHandle buffer, uint32 stride, uint32 offset = 0 ) override;
        void draw( uint32 vertexCount, uint32 startVertex = 0, RHIDescriptorIndex passCbDescriptorIndex = kInvalidDescriptorIndex,
                   RHIDescriptorIndex materialCbDescriptorIndex = kInvalidDescriptorIndex ) override;
        void drawInstanced( uint32 vertexCount, uint32 instanceCount, uint32 startVertex = 0, uint32 startInstance = 0 ) override;
        void bindConstantBuffer( RHIDescriptorIndex cb, uint32 slot ) override;
        void bindStructuredBuffer( RHIDescriptorIndex index, uint32 slot ) override;
        void bindComputeConstantBuffer( RHIDescriptorIndex cb, uint32 slot ) override;
        void bindComputeShaderResource( RHIDescriptorIndex index, uint32 slot ) override;
        void dispatchCompute( uint32 threadGroupCountX, uint32 threadGroupCountY, uint32 threadGroupCountZ ) override;
        void setViewport( const RHIViewport& viewport ) override;
        void drawIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset = 0, RHIDescriptorIndex passCbDescriptorIndex = kInvalidDescriptorIndex,
                           RHIDescriptorIndex materialCbDescriptorIndex = kInvalidDescriptorIndex ) override;
        void setComputeRootConstants( uint32 rootParameterIndex, uint32 num32BitValues, const void* pData, uint32 destOffsetIn32BitValues = 0 ) override;
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
        void bindPassAndMaterialCb( RHIDescriptorIndex passCbDescriptorIndex, RHIDescriptorIndex materialCbDescriptorIndex );
        /** @brief beginEventMarker/endEventMarker용 어노테이션 인터페이스를 최초 1회만 QI해 캐시합니다. */
        ID3DUserDefinedAnnotation* getAnnotation();
        D3D11RHIDevice*            _pDevice;
        ID3D11DeviceContext*       _pContext;
        RHIDescriptorIndex         _lastBoundMaterialDescriptor{ kInvalidDescriptorIndex };
        /** @brief _pContext 수명 동안 불변이라 최초 QueryInterface 결과를 재사용한다(마커마다 QI 방지). */
        Microsoft::WRL::ComPtr<ID3DUserDefinedAnnotation> _annotation;
        bool                                              _bAnnotationQueried{ false };
    };
} // namespace sw
#endif
