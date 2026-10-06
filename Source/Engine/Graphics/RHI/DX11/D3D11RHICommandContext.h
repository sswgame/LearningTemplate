#pragma once
#include "Core/Common/Types.h"

#include "Engine/Common/EnginePlatformHeaders.h"
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
     */
    class D3D11RHICommandContext : public IRHICommandContext
    {
    public:
        /** @brief 디바이스의 기록 상태를 쓰는 즉시 컨텍스트로 만듭니다. */
        D3D11RHICommandContext( D3D11RHIDevice* pDevice, ID3D11DeviceContext* pContext );
        /** @brief 리스트가 자기 기록 상태를 넘겨 만드는 컨텍스트입니다. 병렬로 기록해도 서로 간섭하지 않습니다. */
        D3D11RHICommandContext( D3D11RHIDevice* pDevice, ID3D11DeviceContext* pContext, D3D11RecordingState* pState );
        ~D3D11RHICommandContext() override = default;

        void blitTexture( RHITextureHandle src, RHITextureHandle dst ) override;
        void bindShaderResource( RHIDescriptorIndex index, uint32 slot ) override;
        void prepareTextureForShaderRead( RHITextureHandle texture ) override;
        /**
         * @brief D3D11 에 상태 전이는 없지만 바인딩 해저드는 있습니다 — 이 텍스처가 걸린 PS SRV 슬롯을 뗍니다.
         * @details 그대로 두면 뒤따르는 `OMSetRenderTargets` 가 그 SRV 를 NULL 로 강제하고 디버그 레이어가 해저드를 냅니다
         *          ("still bound on input"). 그래프가 첨부로 쓰기 전에 이것을 부르므로(레벨 프롤로그 · 직렬 패스 앞) 패스의 바인딩을 지우지 않습니다.
         */
        void prepareTextureForRenderTarget( RHITextureHandle texture ) override;
        /** @brief D3D11 은 UAV 해저드를 런타임이 풀어 의도적으로 아무것도 하지 않습니다. */
        void prepareTextureForUnorderedAccess( RHITextureHandle texture ) override { (void)texture; }
        void bindComputeUav( RHIDescriptorIndex index, uint32 slot ) override;
        void setVertexBuffer( uint32 slot, RHIBufferHandle buffer, uint32 stride, uint32 offset = 0 ) override;
        void draw( uint32 vertexCount, uint32 startVertex = 0 ) override;
        void drawInstanced( uint32 vertexCount, uint32 instanceCount, uint32 startVertex = 0, uint32 startInstance = 0 ) override;
        void bindConstantBuffer( RHIDescriptorIndex constantBufferIndex, uint32 slot ) override;
        /** @brief 이 컨텍스트에 `Map(WRITE_DISCARD)` 합니다 — Deferred Context 면 런타임이 리스트 단위로 버저닝하고, 즉시 컨텍스트(프레임 스트림)면 `_immediateContextMutex` 를 잡습니다. */
        void updateConstantBuffer( RHIBufferHandle buffer, const void* pData, uint32 size ) override;
        void bindStructuredBuffer( RHIDescriptorIndex index, uint32 slot ) override;
        void bindComputeConstantBuffer( RHIDescriptorIndex constantBufferIndex, uint32 slot ) override;
        void bindComputeShaderResource( RHIDescriptorIndex index, uint32 slot ) override;
        void dispatchCompute( uint32 threadGroupCountX, uint32 threadGroupCountY, uint32 threadGroupCountZ ) override;
        void setViewport( const RHIViewport& viewport ) override;
        void setScissorRect( const RHIScissorRect& rect ) override;
        /** @brief 슬롯 1(인스턴스 슬롯 스트림)을 겁니다(걸려 있을 때만). */
        void bindInstanceSlotStream();

        /**
         * @brief 드로우 직전에 그래픽스 파이프라인을 **실제로 겁니다**. 걸 수 없으면 false 입니다(드로우하지 말 것).
         *
         * @details DX11 은 `setPipelineState` 가 **핸들만 기록**하고, VS/PS/InputLayout · 정점 버퍼 · 토폴로지는
         *          드로우 시점에 겁니다. 그래서 드로우 진입점(`draw` · `drawInstanced` · `drawIndirect` ·
         *          `drawIndexedIndirect`)마다 이것을 불러야 합니다. 주의: 한 진입점이라도 빠뜨리면 그 경로의
         *          드로우가 **셰이더도 정점 버퍼도 없이 나가** 화면에 클리어 색만 남습니다. 새 진입점도 이것을 부를 것.
         */
        bool bindGraphicsPipelineForDraw();
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
        /** @brief beginEventMarker · endEventMarker 용 어노테이션 인터페이스를 처음 한 번만 QueryInterface 해 캐시합니다. */
        ID3DUserDefinedAnnotation* getAnnotation();
        /** @brief bindless 인덱스가 가리키는 구조버퍼의 SRV 를 반환합니다. 범위 밖 · 미등록이면 nullptr 입니다. 그래픽스 · 컴퓨트 바인딩이 같은 조회를 씁니다. */
        ID3D11ShaderResourceView* findBindlessBufferSrv( RHIDescriptorIndex index ) const;
        /**
         * @brief 루트 상수를 흉내 냅니다. 그림자 배열에 쓰고 계약 슬롯의 작은 상수버퍼를 다시 채워 겁니다.
         * @details DX11 에는 루트 상수가 없습니다. 그래픽스 · 컴퓨트 진입점이 이것을 함께 쓰고 다른 것은 어느
         *          스테이지에 거는가뿐입니다(VS+PS / CS). 상한 검사 · WRITE_DISCARD 재명명 규칙이 두 벌이면 한쪽만 고쳐집니다.
         */
        void                 writeRootConstants( uint32 num32BitValues, const void* pData, uint32 destOffsetIn32BitValues, bool bCompute );
        D3D11RHIDevice*      _pDevice;
        ID3D11DeviceContext* _pContext;
        /// @brief 이 컨텍스트가 갱신할 기록 상태입니다.
        D3D11RecordingState* _pState;
        /** @brief _pContext 가 사는 동안 바뀌지 않아 첫 QueryInterface 결과를 다시 씁니다(마커마다 QI 하지 않습니다). */
        Microsoft::WRL::ComPtr<ID3DUserDefinedAnnotation> _annotation;
        bool                                              _bAnnotationQueried;
    };
} // namespace sw
#endif
