#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Foundation/EnginePlatformHeaders.h"
#include "Engine/Graphics/RHI/IRHICommandContext.h"

#if defined( SW_PLATFORM_WINDOWS )
namespace sw
{
    struct D3D11RecordingState;

    class D3D11RHIDevice;

    /**
     * @class D3D11RHICommandContext
     * @brief 실제 D3D11 API 호출을 내는 구현체입니다. `_pContext` 로 "어떤 `ID3D11DeviceContext` 에
     *        기록할지" 를 주입받습니다. 디바이스의 즉시 컨텍스트(프레임 스트림)와, `D3D11RHICommandList`(리스트별
     *        네이티브 Deferred Context)가 각자 자신의 `ID3D11DeviceContext*` 로 이 클래스를 구성해 재사용합니다.
     * @note 즉시 컨텍스트로 만든 것(프레임 스트림)은 렌더 스레드만 씁니다. 다른 스레드의 즉시 컨텍스트 사용은 `D3D11ImmediateContext::lock()` 으로 직렬화됩니다.
     */
    class D3D11RHICommandContext : public IRHICommandContext
    {
    public:
        /** @brief 디바이스의 기록 상태를 쓰는 즉시 컨텍스트로 만듭니다. */
        D3D11RHICommandContext( D3D11RHIDevice* pDevice, ID3D11DeviceContext* pContext );
        /** @brief 리스트가 자기 기록 상태를 넘겨 만드는 컨텍스트입니다. 병렬로 기록해도 서로 간섭하지 않습니다. */
        D3D11RHICommandContext( D3D11RHIDevice* pDevice, ID3D11DeviceContext* pContext, D3D11RecordingState* pState );
        ~D3D11RHICommandContext() override = default;

        void blitTextureNative( RHITextureHandle src, RHITextureHandle dst ) override;
        void bindShaderResource( RHIDescriptorIndex index, uint32 slot ) override;
        void prepareTextureForShaderReadNative( RHITextureHandle texture ) override;
        /**
         * @brief D3D11 에 상태 전이는 없지만 바인딩 해저드는 있습니다. 이 텍스처가 바인딩된 PS SRV 슬롯을 해제합니다.
         * @details 그대로 두면 뒤따르는 `OMSetRenderTargets` 가 그 SRV 를 NULL 로 강제하고 디버그 레이어가 해저드를 냅니다
         *          ("still bound on input"). 그래프가 첨부로 쓰기 전에 이것을 부르므로(레벨 프롤로그, 직렬 패스 앞) 패스의 바인딩을 지우지 않습니다.
         */
        void prepareTextureForRenderTargetNative( RHITextureHandle texture ) override;
        /** @brief D3D11 은 UAV 해저드를 런타임이 풀어 의도적으로 아무것도 하지 않습니다. */
        void prepareTextureForUnorderedAccessNative( RHITextureHandle texture ) override { (void)texture; }
        void bindComputeUAV( RHIDescriptorIndex index, uint32 slot ) override;
        void bindGraphicsUAV( RHIDescriptorIndex index, uint32 slot ) override;
        void setVertexBuffer( uint32 slot, RHIBufferHandle buffer, uint32 stride, uint32 offset = 0 ) override;
        void drawNative( uint32 vertexCount, uint32 startVertex = 0 ) override;
        void drawInstancedNative( uint32 vertexCount, uint32 instanceCount, uint32 startVertex = 0, uint32 startInstance = 0 ) override;
        void bindConstantBuffer( RHIDescriptorIndex constantBufferIndex, uint32 slot ) override;
        /** @brief 이 컨텍스트에 `Map(WRITE_DISCARD)` 합니다. Deferred Context 면 런타임이 리스트 단위로 버저닝하고, 즉시 컨텍스트(프레임 스트림)면 다른 스레드의 팩토리 갱신과 같은 버퍼를 다툴 수 있어 `D3D11ImmediateContext::lock()` 으로 묶습니다. */
        void updateConstantBuffer( RHIBufferHandle buffer, const void* pData, uint32 size ) override;
        void bindStructuredBuffer( RHIDescriptorIndex index, uint32 slot ) override;
        void bindComputeConstantBuffer( RHIDescriptorIndex constantBufferIndex, uint32 slot ) override;
        void bindComputeShaderResource( RHIDescriptorIndex index, uint32 slot ) override;
        void dispatchComputeNative( uint32 threadGroupCountX, uint32 threadGroupCountY, uint32 threadGroupCountZ ) override;
        void setViewport( const RHIViewport& viewport ) override;
        void setScissorRect( const RHIScissorRect& rect ) override;

        /**
         * @brief 드로우 직전에 그래픽스 파이프라인을 **실제로 바인딩합니다**. 바인딩할 수 없으면 false 입니다(드로우하지 말 것).
         *
         * @details DX11 은 `setPipelineState` 가 **핸들만 기록**하고, VS/PS/InputLayout, 정점 버퍼, 토폴로지는
         *          드로우 시점에 바인딩합니다. 그래서 드로우 진입점(`draw`, `drawInstanced`, `drawIndirect`,
         *          `drawIndexedIndirect`)마다 이것을 불러야 합니다. 주의: 한 진입점이라도 빠뜨리면 그 경로의
         *          드로우가 **셰이더도 정점 버퍼도 없이 나가** 화면에 클리어 색만 남습니다. 새 진입점도 이것을 불러야 합니다.
         */
        [[nodiscard]] bool bindGraphicsPipelineForDraw();
        /** @brief 건 인덱스 버퍼를 겁니다. 걸지 않았거나 부서졌으면 알리고 false 이며 부르는 쪽은 드로우를 버립니다(네 백엔드가 같다 — `RHIBufferBinding::bindForDraw`). */
        [[nodiscard]] bool bindIndexBufferForDraw();
        /** @brief 디스패치 직전에 `setComputePipelineState` 로 건 컴퓨트 PSO 가 등록된 레코드인지 봅니다. 아니면 오류로 멈춥니다(드로우의 그래픽스 PSO 와 같은 계약 — 네 백엔드가 같습니다). */
        void                     requireComputePipelineForDispatch() const;
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
        /** @brief beginEventMarker 와 endEventMarker 용 어노테이션 인터페이스를 처음 한 번만 QueryInterface 해 캐시합니다. */
        ID3DUserDefinedAnnotation* getAnnotation();
        /** @brief bindless 인덱스가 가리키는 구조 버퍼의 SRV 를 반환합니다. 범위 밖이거나 미등록이면 nullptr 입니다. 그래픽스와 컴퓨트 바인딩이 같은 조회를 씁니다. */
        ID3D11ShaderResourceView* findBindlessBufferSRV( RHIDescriptorIndex index ) const;
        /**
         * @brief 루트 상수를 흉내 냅니다. 그림자 배열에 쓰고 계약 슬롯의 작은 상수 버퍼를 다시 채워 바인딩합니다.
         * @details DX11 에는 루트 상수가 없습니다. 그래픽스와 컴퓨트 진입점이 이것을 함께 쓰고, 다른 점은 어느
         *          스테이지에 바인딩하는가뿐입니다(VS+PS / CS). 상한 검사와 WRITE_DISCARD 재명명 규칙이 두 벌이면 한쪽만 고쳐집니다.
         */
        void writeRootConstants( uint32 num32BitValues, const void* pData, uint32 destOffsetIn32BitValues, bool bCompute );

    private:
        D3D11RHIDevice*      _pDevice;
        ID3D11DeviceContext* _pContext;
        /// @brief 이 컨텍스트가 갱신할 기록 상태입니다.
        D3D11RecordingState* _pState;
        /** @brief _pContext 가 사는 동안 바뀌지 않아 첫 QueryInterface 결과를 다시 씁니다(마커마다 QI 하지 않습니다). */
        Microsoft::WRL::ComPtr<ID3DUserDefinedAnnotation> _annotation;
        uint8                                             _bAnnotationQueried : 1;
    };
} // namespace sw
#endif
