#pragma once
#include "Core/Common/Types.h"

#include "Engine/Common/EnginePlatformHeaders.h"
#include "Engine/Graphics/RHI/DX11/D3D11RHIDevice.h"
#include "Engine/Graphics/RHI/IRHICommandContext.h"

#if defined( SW_PLATFORM_WINDOWS )
namespace sw
{

    /**
     * @class D3D11RHICommandContext
     * @brief 실제 D3D11 API 호출을 issue 하는 구현체. `_pContext` 로 "어떤 `ID3D11DeviceContext` 에
     *        기록할지" 를 주입받는다 — 디바이스의 단일 Immediate Context(프레임 스트림
     *        슬롯)와, `D3D11RHICommandList`(리스트별 진짜 네이티브 Deferred Context)가 각자 자신의
     *        `ID3D11DeviceContext*` 로 이 클래스를 구성해서 재사용한다.
     */
    class D3D11RHICommandContext : public IRHICommandContext
    {
    public:
        /** @brief 디바이스의 기록 상태를 쓰는 즉시 컨텍스트. */
        D3D11RHICommandContext( D3D11RHIDevice* pDevice, ID3D11DeviceContext* pContext );
        /** @brief 리스트가 자기 기록 상태를 넘겨 만드는 컨텍스트 — 병렬 기록 시 서로 간섭하지 않는다. */
        D3D11RHICommandContext( D3D11RHIDevice* pDevice, ID3D11DeviceContext* pContext, D3D11RecordingState* pState );
        ~D3D11RHICommandContext() override = default;

        void blitTexture( RHITextureHandle src, RHITextureHandle dst ) override;
        void bindShaderResource( RHIDescriptorIndex index, uint32 slot ) override;
        void prepareTextureForShaderRead( RHITextureHandle texture ) override;
        /** @brief D3D11 은 리소스 상태를 추적하지 않는다 — 의도적 no-op. */
        void prepareTextureForRenderTarget( RHITextureHandle texture ) override { (void)texture; }
        /** @brief D3D11 은 UAV 해저드를 런타임이 푼다 — 의도적 no-op. */
        void prepareTextureForUnorderedAccess( RHITextureHandle texture ) override { (void)texture; }
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
        /** @brief 슬롯 1(인스턴스 슬롯 스트림)을 겁니다 — 걸려 있을 때만. */
        void bindInstanceSlotStream();

        /**
         * @brief 드로우 직전에 그래픽스 파이프라인을 **실제로 겁니다**. 걸 수 없으면 false(드로우하지 말 것).
         *
         * @details DX11 은 `setPipelineState` 가 **핸들만 기록**하고, VS/PS/InputLayout·정점 버퍼·토폴로지는
         *          드로우 시점에 건다. 그래서 드로우 진입점마다 이 블록이 필요하다 — 그런데 예전에
         *          `drawIndirect` 에만 빠져 있어서 **GPU 드리븐 경로의 모든 드로우가 셰이더도 정점 버퍼도
         *          없이 나갔다**(화면과 트랜지언트가 클리어 색만 남았다).
         *
         *          진입점이 넷(`draw` · `drawInstanced` · `drawIndirect` · `drawIndexedIndirect`)인데 블록은
         *          세 벌로 복사돼 있었고, **넷째(`drawIndexedIndirect`)는 여전히 빠진 채였다** — 엔진에서
         *          아무도 부르지 않아 드러나지 않았을 뿐이다. 한 곳으로 모으면 새 진입점이 같은 실수를 할
         *          자리가 없어진다.
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
        /** @brief beginEventMarker/endEventMarker용 어노테이션 인터페이스를 최초 1회만 QI해 캐시합니다. */
        ID3DUserDefinedAnnotation* getAnnotation();
        /** @brief bindless 인덱스가 가리키는 구조버퍼의 SRV — 범위 밖 · 미등록이면 nullptr. 그래픽스 · 컴퓨트 바인딩이 같은 조회다. */
        ID3D11ShaderResourceView* findBindlessBufferSrv( RHIDescriptorIndex index ) const;
        /**
         * @brief 루트 상수 흉내 — 그림자 배열에 쓰고 계약 슬롯의 작은 상수버퍼를 다시 채워 겁니다.
         * @details DX11 에는 루트 상수가 없다. 그래픽스·컴퓨트 진입점이 이 스무 줄을 각자 갖고 있었고 다른 것은 어느
         *          스테이지에 거는가뿐이다(VS+PS / CS). 상한 검사·WRITE_DISCARD 재명명 규칙이 두 벌이면 한쪽만 고쳐진다.
         */
        void                 writeRootConstants( uint32 num32BitValues, const void* pData, uint32 destOffsetIn32BitValues, bool bCompute );
        D3D11RHIDevice*      _pDevice;
        ID3D11DeviceContext* _pContext;
        /// @brief 이 컨텍스트가 갱신할 기록 상태.
        D3D11RecordingState* _pState;
        /** @brief _pContext 수명 동안 불변이라 최초 QueryInterface 결과를 재사용한다(마커마다 QI 방지). */
        Microsoft::WRL::ComPtr<ID3DUserDefinedAnnotation> _annotation;
        bool                                              _bAnnotationQueried;
    };
} // namespace sw
#endif
