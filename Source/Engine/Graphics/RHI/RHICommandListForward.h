/**
 * @file RHICommandListForward.h
 * @brief IRHICommandList 구현이 자기 커맨드 컨텍스트로 그대로 넘기는 포워딩 정의
 */
#pragma once
/**
 * @brief IRHICommandList 의 기록 API 전부를 지정한 컨텍스트 멤버로 포워딩합니다.
 * @param ContextMember 포워딩 대상 컨텍스트 멤버 (예: _context)
 * @details 네 백엔드의 CommandList 는 전부 "자기 컨텍스트로 그대로 넘기기"만 한다. 예전에는 그
 *          26개 메서드를 백엔드마다 복사해 두어서, 기록 API 를 하나 추가할 때마다 네 곳을 똑같이
 *          고쳐야 했고 시그니처가 어긋나도 컴파일 전까지 알 수 없었다. 정의를 한곳에 모은다.
 *          백엔드가 특정 메서드만 다르게 처리해야 하면 이 매크로를 쓰지 말고 직접 구현하면 된다.
 */

#define SW_FORWARD_RHI_COMMAND_LIST( ContextMember )                                                                                          \
    void setViewport( const RHIViewport& viewport ) override { ContextMember.setViewport( viewport ); }                                       \
    void setPipelineState( RHIPipelineStateHandle pso ) override { ContextMember.setPipelineState( pso ); }                                   \
    void beginRenderPass( const RHIRenderPassBeginInfo& beginInfo ) override { ContextMember.beginRenderPass( beginInfo ); }                  \
    void endRenderPass() override { ContextMember.endRenderPass(); }                                                                          \
    void setVertexBuffer( uint32 slot, RHIBufferHandle buffer, uint32 stride, uint32 offset = 0 ) override                                    \
    {                                                                                                                                         \
        ContextMember.setVertexBuffer( slot, buffer, stride, offset );                                                                        \
    }                                                                                                                                         \
    void draw( uint32 vertexCount, uint32 startVertex = 0,                                                                                    \
               RHIDescriptorIndex passCbDescriptorIndex     = kInvalidDescriptorIndex,                                                        \
               RHIDescriptorIndex materialCbDescriptorIndex = kInvalidDescriptorIndex ) override                                              \
    {                                                                                                                                         \
        ContextMember.draw( vertexCount, startVertex, passCbDescriptorIndex, materialCbDescriptorIndex );                                     \
    }                                                                                                                                         \
    void drawInstanced( uint32 vertexCount, uint32 instanceCount, uint32 startVertex = 0, uint32 startInstance = 0 ) override                 \
    {                                                                                                                                         \
        ContextMember.drawInstanced( vertexCount, instanceCount, startVertex, startInstance );                                                \
    }                                                                                                                                         \
    void setIndexBuffer( RHIBufferHandle buffer, uint32 indexStride = 4, uint32 offset = 0 ) override                                         \
    {                                                                                                                                         \
        ContextMember.setIndexBuffer( buffer, indexStride, offset );                                                                          \
    }                                                                                                                                         \
    void setComputePipelineState( RHIPipelineStateHandle pso ) override { ContextMember.setComputePipelineState( pso ); }                     \
    void dispatchCompute( uint32 x, uint32 y, uint32 z ) override { ContextMember.dispatchCompute( x, y, z ); }                               \
    void setComputeRootConstants( uint32 rootParameterIndex, uint32 num32BitValues, const void* pData,                                        \
                                  uint32 destOffsetIn32BitValues = 0 ) override                                                               \
    {                                                                                                                                         \
        ContextMember.setComputeRootConstants( rootParameterIndex, num32BitValues, pData, destOffsetIn32BitValues );                          \
    }                                                                                                                                         \
    void bindComputeUAV( RHIDescriptorIndex index, uint32 slot ) override { ContextMember.bindComputeUAV( index, slot ); }                    \
    void bindShaderResource( RHIDescriptorIndex index, uint32 slot ) override { ContextMember.bindShaderResource( index, slot ); }            \
    void bindConstantBuffer( RHIDescriptorIndex cb, uint32 slot ) override { ContextMember.bindConstantBuffer( cb, slot ); }                  \
    void bindStructuredBuffer( RHIDescriptorIndex index, uint32 slot ) override { ContextMember.bindStructuredBuffer( index, slot ); }        \
    void bindComputeConstantBuffer( RHIDescriptorIndex cb, uint32 slot ) override { ContextMember.bindComputeConstantBuffer( cb, slot ); }    \
    void bindComputeShaderResource( RHIDescriptorIndex index, uint32 slot ) override                                                          \
    {                                                                                                                                         \
        ContextMember.bindComputeShaderResource( index, slot );                                                                               \
    }                                                                                                                                         \
    void prepareTextureForShaderRead( RHITextureHandle texture ) override { ContextMember.prepareTextureForShaderRead( texture ); }           \
    void blitTexture( RHITextureHandle src, RHITextureHandle dst ) override { ContextMember.blitTexture( src, dst ); }                        \
    void drawIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset = 0,                                                       \
                       RHIDescriptorIndex passCbDescriptorIndex     = kInvalidDescriptorIndex,                                                \
                       RHIDescriptorIndex materialCbDescriptorIndex = kInvalidDescriptorIndex ) override                                      \
    {                                                                                                                                         \
        ContextMember.drawIndirect( argumentBuffer, argumentBufferOffset, passCbDescriptorIndex, materialCbDescriptorIndex );                 \
    }                                                                                                                                         \
    void dispatchIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset = 0 ) override                                         \
    {                                                                                                                                         \
        ContextMember.dispatchIndirect( argumentBuffer, argumentBufferOffset );                                                               \
    }                                                                                                                                         \
    void transitionBuffer( RHIBufferHandle buffer, RHIBufferState newState ) override { ContextMember.transitionBuffer( buffer, newState ); } \
    void drawIndexedIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset = 0 ) override                                      \
    {                                                                                                                                         \
        ContextMember.drawIndexedIndirect( argumentBuffer, argumentBufferOffset );                                                            \
    }                                                                                                                                         \
    void multiDrawIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset, uint32 maxCommandCount,                              \
                            RHIBufferHandle countBuffer = 0, uint32 countBufferOffset = 0 ) override                                          \
    {                                                                                                                                         \
        ContextMember.multiDrawIndirect( argumentBuffer, argumentBufferOffset, maxCommandCount, countBuffer, countBufferOffset );             \
    }                                                                                                                                         \
    void beginEventMarker( const utf8* pName ) override { ContextMember.beginEventMarker( pName ); }                                          \
    void endEventMarker() override { ContextMember.endEventMarker(); }
