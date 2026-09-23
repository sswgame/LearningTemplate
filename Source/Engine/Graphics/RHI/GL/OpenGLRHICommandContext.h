#pragma once
#include "Core/Common/Types.h"

#include "Engine/Graphics/RHI/IRHICommandContext.h"

namespace sw
{
    struct OpenGLRecordingState;

    class OpenGLRHIDevice;

    class OpenGLRHICommandContext : public IRHICommandContext
    {
    public:
        /** @brief 디바이스의 기록 상태를 쓰는 즉시 컨텍스트. */
        explicit OpenGLRHICommandContext( OpenGLRHIDevice* pDevice );
        /** @brief 리스트가 자기 기록 상태를 넘겨 만드는 컨텍스트. */
        OpenGLRHICommandContext( OpenGLRHIDevice* pDevice, OpenGLRecordingState* pState );
        ~OpenGLRHICommandContext() override = default;

        void blitTexture( RHITextureHandle src, RHITextureHandle dst ) override;
        void bindShaderResource( RHIDescriptorIndex index, uint32 slot ) override;
        void prepareTextureForShaderRead( RHITextureHandle texture ) override;
        /** @brief OpenGL 은 리소스 상태를 추적하지 않는다 — 의도적 no-op. */
        void prepareTextureForRenderTarget( RHITextureHandle texture ) override { (void)texture; }
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
        void setComputeRootConstants( uint32 rootParameterIndex, uint32 num32BitValues, const void* pData, uint32 destOffsetIn32BitValues = 0 ) override;
        void setGraphicsRootConstants( uint32 rootParameterIndex, uint32 num32BitValues, const void* pData, uint32 destOffsetIn32BitValues = 0 ) override;
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
        /** @brief _meshVao를 바인딩하고 position(0)/color(1) 정점 attrib를 vbo 기준으로 세팅한다.
         *         draw류 5곳에 복붙돼 있던 블록 통합 — 호출자가 draw 후 언바인드는 각자 책임진다. */
        void bindMeshVaoAttribs( uint32 vbo );
        /**
         * @brief 드로우가 쓸 프로그램과 토폴로지 — PSO 가 정하고, PSO 가 없으면 디바이스 기본 프로그램과 GL_TRIANGLES.
         * @details 드로우 진입점 넷이 같은 열 줄을 각자 들고 있었다(예전 멀티 드로우 경로는 이걸 빠뜨리고 GL_TRIANGLES 로 굳혔었다).
         * @return 프로그램이 0 이면 false — 그릴 수 없다.
         */
        bool             resolveDrawProgram( uint32& outProgram, uint32& outMode ) const;
        OpenGLRHIDevice* _pDevice;
        /// @brief 이 컨텍스트가 갱신할 기록 상태.
        OpenGLRecordingState* _pState;
    };
} // namespace sw
