/**
 * @file RHICommandListForwarder.h
 * @brief 자기 커맨드 컨텍스트로 기록 API 를 그대로 넘기는 IRHICommandList 기반 클래스
 */
#pragma once
#include "Engine/Graphics/RHI/IRHICommandList.h"

namespace sw
{
    /**
     * @class RHICommandListForwarder
     * @brief `IRHICommandList` 의 기록 API 전부를 파생이 지정한 컨텍스트로 넘깁니다.
     * @tparam ContextT 백엔드의 커맨드 컨텍스트 구체 타입
     * @details 네 백엔드의 CommandList 는 전부 "자기 컨텍스트로 그대로 넘기기" 만 한다. 그 26개
     *          메서드를 백엔드마다 복사해 두면 기록 API 를 하나 추가할 때마다 네 곳을 똑같이 고쳐야
     *          하고, 시그니처가 어긋나도 컴파일 전까지 모른다.
     *
     *          예전에는 이걸 매크로(`SW_FORWARD_RHI_COMMAND_LIST`)로 풀었는데, 그러면
     *          "DX12 에서 setViewport 가 어디 구현됐나" 를 정의로 점프해서 찾을 수 없다 —
     *          읽으라고 만든 저장소에서 치르기엔 큰 비용이다. 템플릿 기반 클래스로 바꾸면 같은
     *          중복 제거를 얻으면서 평범한 C++ 로 남는다.
     *
     *          `ContextT` 가 구체 타입이라 호출은 **정적 디스패치**로 인라인된다 — 매크로와 같은
     *          코드가 나온다. 컨텍스트는 파생 클래스의 다른 멤버(기록 상태 등)에 의존해 만들어지므로
     *          기반이 소유하지 않고, 파생이 생성자에서 `_pContext` 를 자기 것으로 가리키게 한다.
     *
     *          특정 메서드만 다르게 처리해야 하면 파생에서 그것만 다시 override 하면 된다.
     */
    template <typename ContextT>
    class RHICommandListForwarder : public IRHICommandList
    {
    public:
        void setViewport( const RHIViewport& viewport ) override { _pContext->setViewport( viewport ); }
        void setPipelineState( RHIPipelineStateHandle pso ) override { _pContext->setPipelineState( pso ); }
        void beginRenderPass( const RHIRenderPassBeginInfo& beginInfo ) override { _pContext->beginRenderPass( beginInfo ); }
        void endRenderPass() override { _pContext->endRenderPass(); }
        void setVertexBuffer( uint32 slot, RHIBufferHandle buffer, uint32 stride, uint32 offset = 0 ) override
        {
            _pContext->setVertexBuffer( slot, buffer, stride, offset );
        }
        void draw( uint32 vertexCount, uint32 startVertex = 0 ) override { _pContext->draw( vertexCount, startVertex ); }
        void drawInstanced( uint32 vertexCount, uint32 instanceCount, uint32 startVertex = 0, uint32 startInstance = 0 ) override
        {
            _pContext->drawInstanced( vertexCount, instanceCount, startVertex, startInstance );
        }
        void setIndexBuffer( RHIBufferHandle buffer, uint32 indexStride = 4, uint32 offset = 0 ) override
        {
            _pContext->setIndexBuffer( buffer, indexStride, offset );
        }
        void setComputePipelineState( RHIPipelineStateHandle pso ) override { _pContext->setComputePipelineState( pso ); }
        void dispatchCompute( uint32 x, uint32 y, uint32 z ) override { _pContext->dispatchCompute( x, y, z ); }
        void setComputeRootConstants( uint32 rootParameterIndex, uint32 num32BitValues, const void* pData,
                                      uint32 destOffsetIn32BitValues = 0 ) override
        {
            _pContext->setComputeRootConstants( rootParameterIndex, num32BitValues, pData, destOffsetIn32BitValues );
        }
        void bindComputeUAV( RHIDescriptorIndex index, uint32 slot ) override { _pContext->bindComputeUAV( index, slot ); }
        void bindShaderResource( RHIDescriptorIndex index, uint32 slot ) override { _pContext->bindShaderResource( index, slot ); }
        void bindConstantBuffer( RHIDescriptorIndex cb, uint32 slot ) override { _pContext->bindConstantBuffer( cb, slot ); }
        void bindStructuredBuffer( RHIDescriptorIndex index, uint32 slot ) override { _pContext->bindStructuredBuffer( index, slot ); }
        void bindComputeConstantBuffer( RHIDescriptorIndex cb, uint32 slot ) override { _pContext->bindComputeConstantBuffer( cb, slot ); }
        void bindComputeShaderResource( RHIDescriptorIndex index, uint32 slot ) override
        {
            _pContext->bindComputeShaderResource( index, slot );
        }
        void prepareTextureForShaderRead( RHITextureHandle texture ) override { _pContext->prepareTextureForShaderRead( texture ); }
        void prepareTextureForRenderTarget( RHITextureHandle texture ) override { _pContext->prepareTextureForRenderTarget( texture ); }
        void blitTexture( RHITextureHandle src, RHITextureHandle dst ) override { _pContext->blitTexture( src, dst ); }
        void drawIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset = 0 ) override
        {
            _pContext->drawIndirect( argumentBuffer, argumentBufferOffset );
        }
        void dispatchIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset = 0 ) override
        {
            _pContext->dispatchIndirect( argumentBuffer, argumentBufferOffset );
        }
        void transitionBuffer( RHIBufferHandle buffer, RHIBufferState newState ) override { _pContext->transitionBuffer( buffer, newState ); }
        void drawIndexedIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset = 0 ) override
        {
            _pContext->drawIndexedIndirect( argumentBuffer, argumentBufferOffset );
        }
        void multiDrawIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset, uint32 maxCommandCount,
                                RHIBufferHandle countBuffer = 0, uint32 countBufferOffset = 0 ) override
        {
            _pContext->multiDrawIndirect( argumentBuffer, argumentBufferOffset, maxCommandCount, countBuffer, countBufferOffset );
        }
        void beginEventMarker( const utf8* pName ) override { _pContext->beginEventMarker( pName ); }
        void endEventMarker() override { _pContext->endEventMarker(); }

    protected:
        /// @brief 파생 클래스가 생성자에서 자기 컨텍스트를 가리키게 설정한다.
        ContextT* _pContext{ nullptr };
    };
} // namespace sw
