#pragma once
#include "Core/Foundation/Types.h"

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
        ~OpenGLRHICommandContext() override = default;

        void blitTextureNative( RHITextureHandle src, RHITextureHandle dst ) override;
        void bindShaderResource( RHIDescriptorIndex index, uint32 slot ) override;
        void prepareTextureForShaderReadNative( RHITextureHandle texture ) override;
        /** @brief OpenGL 은 리소스 상태를 추적하지 않습니다. 의도적으로 아무것도 하지 않습니다. */
        void prepareTextureForRenderTargetNative( RHITextureHandle texture ) override { (void)texture; }
        void prepareTextureForUnorderedAccessNative( RHITextureHandle texture ) override;
        void bindComputeUAV( RHIDescriptorIndex index, uint32 slot ) override;
        void bindGraphicsUAV( RHIDescriptorIndex index, uint32 slot ) override;
        void setVertexBuffer( uint32 slot, RHIBufferHandle buffer, uint32 stride, uint32 offset = 0 ) override;
        void drawNative( uint32 vertexCount, uint32 startVertex = 0 ) override;
        void drawInstancedNative( uint32 vertexCount, uint32 instanceCount, uint32 startVertex = 0, uint32 startInstance = 0 ) override;
        void bindConstantBuffer( RHIDescriptorIndex constantBufferIndex, uint32 slot ) override;
        /** @brief 버퍼의 이번 프레임 슬롯에 씁니다. `IRHIResourceFactory::updateConstantBuffer` 와 같은 일이며, 이 백엔드의 커맨드 스트림은 버퍼 버전을 갖지 않습니다. */
        void                     updateConstantBuffer( RHIBufferHandle buffer, const void* pData, uint32 size ) override;
        void                     bindStructuredBuffer( RHIDescriptorIndex index, uint32 slot ) override;
        void                     bindComputeConstantBuffer( RHIDescriptorIndex constantBufferIndex, uint32 slot ) override;
        void                     bindComputeShaderResource( RHIDescriptorIndex index, uint32 slot ) override;
        void                     dispatchComputeNative( uint32 threadGroupCountX, uint32 threadGroupCountY, uint32 threadGroupCountZ ) override;
        void                     setViewport( const RHIViewport& viewport ) override;
        void                     setScissorRect( const RHIScissorRect& rect ) override;
        void                     drawIndirectNative( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset = 0, uint32 drawCount = 1,
                                                     RHIBufferHandle countBuffer = 0, uint32 countBufferOffset = 0 ) override;
        void                     setComputeRootConstants( uint32 rootParameterIndex, uint32 num32BitValues, const void* pData, uint32 destOffsetIn32BitValues = 0 ) override;
        void                     setGraphicsRootConstants( uint32 rootParameterIndex, uint32 num32BitValues, const void* pData, uint32 destOffsetIn32BitValues = 0 ) override;
        void                     drawIndexedIndirectNative( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset = 0, uint32 drawCount = 1, RHIBufferHandle countBuffer = 0,
                                                            uint32 countBufferOffset = 0 ) override;
        void                     drawIndexedInstancedNative( uint32 indexCount, uint32 instanceCount, uint32 startIndex = 0, int32 baseVertex = 0, uint32 startInstance = 0 ) override;
        void                     dispatchIndirectNative( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset = 0 ) override;
        void                     beginEventMarker( const utf8* pName ) override;
        void                     writeBreadcrumb( uint32 markerID ) override;
        void                     endEventMarker() override;
        void                     setPipelineState( RHIPipelineStateHandle pso ) override;
        void                     setComputePipelineStateNative( RHIPipelineStateHandle pso ) override;
        IRHIDevice&              getContextDevice() const override;
        RHIRenderPassBeginResult beginRenderPassNative( const RHIRenderPassBeginInfo& beginInfo ) override;
        void                     endRenderPassNative() override;
        void                     setIndexBuffer( RHIBufferHandle buffer, uint32 indexStride = 4, uint32 offset = 0 ) override;
        void                     transitionBufferNative( RHIBufferHandle buffer, RHIBufferState newState ) override;
        void                     copyBufferToReadbackNative( RHIBufferHandle source, RHIBufferHandle readback, uint32 sizeBytes ) override;
        void                     uavBarrierNative( RHIBufferHandle buffer ) override;

    private:
        /**
         * @brief 건 PSO 가 읽는 정점 슬롯만 VAO 로 겁니다(정점 입력이 없으면 빈 VAO). 읽는 슬롯에 버퍼가 없거나 건 버퍼가 부서졌으면 알리고 false 이며, 드로우를 버립니다(네 백엔드가 같다).
         * @details 모든 드로우 진입점이 이것을 씁니다. draw 뒤의 언바인드는 부르는 쪽이 각자 맡습니다.
         */
        [[nodiscard]] bool bindVertexInputsForDraw();
        /**
         * @brief 드로우가 쓸 프로그램과 토폴로지를 건 PSO 에서 고릅니다. 등록된 PSO 가 없으면 오류로 멈춥니다(기본 프로그램 없음).
         * @details 드로우 진입점 네 개가 모두 이것을 씁니다. 따로 고르는 진입점은 PSO 토폴로지를 놓쳐 GL_TRIANGLES 로 굳기 쉽습니다.
         */
        void resolveDrawProgram( uint32& outProgram, uint32& outMode ) const;
        /**
         * @brief 디스패치가 쓸 프로그램을 `setComputePipelineState` 로 건 컴퓨트 PSO 에서 고릅니다. 등록된 컴퓨트 PSO 가 없으면 오류로 멈춥니다(드로우의 그래픽스 PSO 와 같은 계약 — 네 백엔드가 같습니다).
         */
        [[nodiscard]] uint32 resolveComputeProgram() const;
        /**
         * @brief 인덱스 없는 드로우의 몸통입니다. `draw` 와 `drawInstanced` 가 함께 씁니다.
         * @details 메시 정점 버퍼가 설정되어 있으면 그 VAO 로, 아니면 디바이스 기본 VAO 로 그립니다. 인스턴스가 하나면 `glDrawArrays`,
         *          둘 이상이면 `glDrawArraysInstanced` 를 부릅니다(언리얼 OpenGL RHI 의 `RHIDrawPrimitive` 와 같은 분기입니다).
         */
        void drawArrays( uint32 vertexCount, uint32 instanceCount, uint32 startVertex );
        /**
         * @brief 인덱스 드로우(`drawIndexedInstanced` · `drawIndexedIndirect`)가 함께 쓰는 준비 — 프로그램 · 메시 VAO · 인덱스 버퍼를 건다.
         * @return 메시 정점이나 인덱스 버퍼가 없으면(부서졌으면 진단을 남기고) false 이고 그리지 않는다.
         */
        [[nodiscard]] bool bindIndexedMeshForDraw( uint32& outMode, uint32& outIndexType );
        /** @brief `bindIndexedMeshForDraw` 가 건 VAO · 버퍼를 뗍니다. */
        void unbindIndexedMesh();

    private:
        OpenGLRHIDevice* const _pDevice; ///< 생성자에서 받는다
        /// @brief 이 컨텍스트가 갱신하는 기록 상태입니다.
        OpenGLRecordingState* _pState;
    };
} // namespace sw
