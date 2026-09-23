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
        /** @brief 디바이스의 기록 상태를 쓰는 컨텍스트를 만듭니다. 프레임 스트림 컨텍스트와 커맨드 리스트가 모두 이 생성자를 씁니다. */
        explicit OpenGLRHICommandContext( OpenGLRHIDevice* pDevice );
        /** @brief 기록 상태를 따로 받는 생성자입니다. GL 은 실제 상태가 하나라 지금 부르는 곳은 없습니다. */
        OpenGLRHICommandContext( OpenGLRHIDevice* pDevice, OpenGLRecordingState* pState );
        ~OpenGLRHICommandContext() override = default;

        void blitTexture( RHITextureHandle src, RHITextureHandle dst ) override;
        void bindShaderResource( RHIDescriptorIndex index, uint32 slot ) override;
        void prepareTextureForShaderRead( RHITextureHandle texture ) override;
        /** @brief OpenGL 은 리소스 상태를 추적하지 않습니다. 의도적으로 아무것도 하지 않습니다. */
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
        /** @brief _meshVao 를 바인딩하고, 공용 정점 속성 표(constant::arrVertexAttribute)대로 vbo 와 인스턴스 슬롯 스트림의 속성을 겁니다.
         *         드로우 진입점마다 복사돼 있던 블록을 합친 것입니다. draw 뒤의 언바인드는 부르는 쪽이 각자 맡습니다. */
        void bindMeshVaoAttribs( uint32 vbo );
        /**
         * @brief 드로우가 쓸 프로그램과 토폴로지를 고릅니다. PSO 가 정하고, PSO 가 없으면 디바이스 기본 프로그램과 GL_TRIANGLES 입니다.
         * @details 드로우 진입점 넷이 같은 열 줄을 각자 들고 있었습니다(예전 멀티 드로우 경로는 이것을 빠뜨리고 GL_TRIANGLES 로 굳혔었습니다).
         * @return 프로그램이 0 이라 그릴 수 없으면 false.
         */
        bool             resolveDrawProgram( uint32& outProgram, uint32& outMode ) const;
        OpenGLRHIDevice* _pDevice;
        /// @brief 이 컨텍스트가 갱신하는 기록 상태입니다.
        OpenGLRecordingState* _pState;
    };
} // namespace sw
