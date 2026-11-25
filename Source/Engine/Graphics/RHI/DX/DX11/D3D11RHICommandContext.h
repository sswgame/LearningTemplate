#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Foundation/EnginePlatformHeaders.h"
#include "Engine/Graphics/RHI/DX/DX11/D3D11RHIDevice.h"
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
        void prepareTextureForShaderReadNative( RHITextureHandle texture ) override;
        /**
         * @brief D3D11 에 상태 전이는 없지만 바인딩 해저드는 있습니다. 이 텍스처가 바인딩된 PS SRV 슬롯을 해제합니다.
         * @details 그대로 두면 뒤따르는 `OMSetRenderTargets` 가 그 SRV 를 NULL 로 강제하고 디버그 레이어가 해저드를 냅니다
         *          ("still bound on input"). 그래프가 첨부로 쓰기 전에 이것을 부르므로(레벨 프롤로그, 직렬 패스 앞) 패스의 바인딩을 지우지 않습니다.
         */
        void prepareTextureForRenderTargetNative( RHITextureHandle texture ) override;
        /** @brief D3D11 은 UAV 해저드를 런타임이 풀어 의도적으로 아무것도 하지 않습니다. */
        void prepareTextureForUnorderedAccessNative( RHITextureHandle texture ) override { (void)texture; }
        void drawNative( uint32 vertexCount, uint32 startVertex = 0 ) override;
        void drawInstancedNative( uint32 vertexCount, uint32 instanceCount, uint32 startVertex = 0, uint32 startInstance = 0 ) override;
        /** @brief 이 컨텍스트에 `Map(WRITE_DISCARD)` 합니다. Deferred Context 면 런타임이 리스트 단위로 버저닝하고, 즉시 컨텍스트(프레임 스트림)면 다른 스레드의 팩토리 갱신과 같은 버퍼를 다툴 수 있어 `D3D11ImmediateContext::lock()` 으로 묶습니다. */
        void updateConstantBufferNative( RHIBufferHandle buffer, const void* pData, uint32 size ) override;
        void dispatchComputeNative( uint32 threadGroupCountX, uint32 threadGroupCountY, uint32 threadGroupCountZ ) override;
        void setViewportNative( const RHIViewport& viewport ) override;
        void setScissorRectNative( const RHIScissorRect& rect ) override;

        /** @brief 새 기록 스트림입니다(리스트 시작) — 건 것이 없고 네이티브 컨텍스트도 기본 상태다. */
        void beginRecording();
        /** @brief 네이티브 컨텍스트 상태가 지워졌습니다(`ClearState` · `ExecuteCommandList( FALSE )` · 바깥 코드) — 걸린 것을 다음 드로우 · 디스패치가 다시 겁니다. */
        void                     onNativeStateCleared();
        void                     drawIndirectNative( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset = 0, uint32 drawCount = 1,
                                                     RHIBufferHandle countBuffer = 0, uint32 countBufferOffset = 0 ) override;
        void                     drawIndexedIndirectNative( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset = 0, uint32 drawCount = 1, RHIBufferHandle countBuffer = 0,
                                                            uint32 countBufferOffset = 0 ) override;
        void                     drawIndexedInstancedNative( uint32 indexCount, uint32 instanceCount, uint32 startIndex = 0, int32 baseVertex = 0, uint32 startInstance = 0 ) override;
        void                     dispatchIndirectNative( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset = 0 ) override;
        void                     beginEventMarker( const utf8* pName ) override;
        void                     writeBreadcrumb( uint32 markerID ) override;
        void                     endEventMarker() override;
        IRHIDevice&              getContextDevice() const override;
        RHIRenderPassBeginResult beginRenderPassNative( const RHIRenderPassInfo& beginInfo, RHIRenderPassExtent& outTargetExtent ) override;
        void                     endRenderPassNative() override;
        void                     transitionBufferNative( RHIBufferHandle buffer, RHIBufferState newState ) override;
        void                     copyBufferToReadbackNative( RHIBufferHandle source, RHIBufferHandle readback, uint32 sizeBytes ) override;
        uint64                   findBufferBytes( RHIBufferHandle buffer ) const override;
        void                     copyBufferRegionNative( RHIBufferHandle destination, uint64 destinationOffset, RHIBufferHandle source, uint64 sourceOffset, uint64 sizeBytes ) override;
        void                     uavBarrierNative( RHIBufferHandle buffer ) override;
        void                     uavBarrierTextureNative( RHITextureHandle texture ) override;

    private:
        /**
         * @brief 드로우 본문의 한 길입니다. 그래픽스 파이프라인(@p bIndexed 면 인덱스 버퍼도)을 걸고, 걸었을 때만 @p issue( ID3D11DeviceContext& ) 로 드로우를 냅니다.
         * @details `setPipelineState` 는 핸들만 기록하고(공용 기반), 셰이더 · 렌더 상태 · 슬롯 · 정점 버퍼는 드로우 시점에 바뀐 것만 바인딩합니다.
         *          예전에는 드로우 진입점마다 바인딩을 손으로 불렀고, 한 곳이라도 빠뜨리면 그 경로의 드로우가 셰이더도 정점 버퍼도 없이 나가
         *          클리어 색만 남았다. 이제 바인딩 함수는 이 템플릿만 부르고 진입점은 본문만 넘깁니다(언리얼 D3D11 RHI 의 `CommitGraphicsResourceTables` 단일 경로).
         *          템플릿 · 인라인이라 드로우마다 간접 호출 · 할당이 늘지 않습니다.
         */
        template <bool bIndexed, typename TIssue>
        void drawWith( TIssue&& issue );
        /** @brief 드로우 직전에 그래픽스 파이프라인을 실제로 바인딩합니다. 바인딩할 수 없으면 false 입니다(드로우하지 말 것). `drawWith` 만 부릅니다. */
        [[nodiscard]] bool bindGraphicsPipelineForDraw();
        /** @brief 건 인덱스 버퍼를 겁니다. 걸지 않았거나 부서졌으면 알리고 false 이며 드로우를 버립니다(네 백엔드가 같다 — `RHIBufferBinding::bindForDraw`). `drawWith` 만 부릅니다. */
        [[nodiscard]] bool bindIndexBufferForDraw();
        /** @brief beginEventMarker 와 endEventMarker 용 어노테이션 인터페이스를 처음 한 번만 QueryInterface 해 캐시합니다. */
        ID3DUserDefinedAnnotation* getAnnotation();
        /** @brief bindless 인덱스가 가리키는 구조 버퍼의 SRV 를 반환합니다. 범위 밖이거나 미등록이면 nullptr 입니다. 그래픽스와 컴퓨트 바인딩이 같은 조회를 씁니다. */
        ID3D11ShaderResourceView* findBindlessBufferSRV( RHIBufferSRVIndex index ) const;
        /**
         * @brief 루트 상수를 흉내 냅니다. 바뀌었으면 계약 슬롯의 작은 상수 버퍼(바인드 포인트마다 하나)를 다시 채워 바인딩합니다.
         * @details DX11 에는 루트 상수가 없습니다. 그래픽스와 컴퓨트가 이것을 함께 쓰고, 다른 점은 어느 스테이지에 바인딩하는가뿐입니다(VS+PS+HS+DS / CS).
         */
        void flushRootConstants( RHIBindPoint point );
        /** @brief 바뀐 셰이더 슬롯을 겁니다(뗀 칸은 뗀다). 등록되지 않은 번호는 알리고 칸을 비운다. */
        void flushShaderSlots( RHIBindPoint point );
        /** @brief t 슬롯 하나를 그래픽스 스테이지에 겁니다(nullptr 이면 뗀다). 버퍼 칸은 VS · PS · HS · DS, 텍스처 칸은 PS 와 정점 쪽이 읽는 칸만 VS · HS · DS 다. */
        void setGraphicsShaderResource( uint32 slot, ID3D11ShaderResourceView* pSRV );
        /** @brief u 슬롯 하나를 픽셀 단계에 겁니다(nullptr 이면 뗀다). */
        void setGraphicsUnorderedAccess( uint32 slot, ID3D11UnorderedAccessView* pUAV );
        /** @brief 디스패치 직전에 컴퓨트 PSO · 슬롯 · 루트 상수를 겁니다. 등록된 컴퓨트 PSO 가 없으면 오류로 멈춥니다(드로우의 그래픽스 PSO 와 같은 계약 — 네 백엔드가 같습니다). */
        void bindComputePipelineForDispatch();
        /** @brief 즉시 컨텍스트를 바깥 코드가 만졌으면 그림자를 버립니다(`D3D11RHIDevice::_nativeContextSerial`). */
        void syncNativeState();
        /**
         * @brief 번호 @p index 의 상수 버퍼를 @p slot 에 겁니다(그래픽스면 VS · PS · HS · DS, 컴퓨트면 CS). 무효 번호면 뗍니다. 페이지 구간이면 `*SetConstantBuffers1` 오프셋으로 겁니다(감사 R-4).
         */
        void bindConstantBufferAt( RHIDescriptorIndex index, uint32 slot, RHIBindPoint point );
        /**
         * @brief 스테이지 @p stage(`D3D11ConstantBufferBindings` 순서)의 @p slot 에 상수 버퍼를 겁니다. @p firstConstant 가 `kWholeBuffer` 면 버퍼 전체, 아니면 구간입니다.
         * @details 그림자와 같으면 건너뛴다. 같은 버퍼를 오프셋만 바꿔 걸 때, 드라이버가 커맨드 리스트를 지원하지 않는 지연 컨텍스트에서만 빈 칸을 먼저 건다
         *          (`VSSetConstantBuffers1` 비고 — 런타임 에뮬레이션이 같은 버퍼 호출을 중복으로 버린다).
         */
        void setConstantBuffer( uint32 stage, uint32 slot, ID3D11Buffer* pCb, UINT firstConstant, UINT constantCount );
        /** @brief D3D11.1 컨텍스트를 받고, 빈 칸을 먼저 걸어야 하는 지연 컨텍스트인지 정합니다(생성자 둘이 함께 쓴다). */
        void initializeConstantBufferBinding();

        /// @brief `D3D11ConstantBufferBindings` 의 스테이지 칸 순서입니다.
        static constexpr uint32 kVSStage = 0;
        static constexpr uint32 kPSStage = 1;
        static constexpr uint32 kCSStage = 2;
        static constexpr uint32 kHSStage = 3;
        static constexpr uint32 kDSStage = 4;
        static_assert( kDSStage + 1 == D3D11ConstantBufferBindings::kStageCount, "스테이지 칸 수가 그림자 표와 맞아야 한다" );

    private:
        D3D11RHIDevice*      _pDevice;
        ID3D11DeviceContext* _pContext;
        /// @brief 이 컨텍스트가 갱신할 기록 상태입니다.
        D3D11RecordingState* _pState;
        /** @brief _pContext 가 사는 동안 바뀌지 않아 첫 QueryInterface 결과를 다시 씁니다(마커마다 QI 하지 않습니다). */
        Microsoft::WRL::ComPtr<ID3DUserDefinedAnnotation> _annotation;
        uint8                                             _bAnnotationQueried      : 1;
        uint8                                             _bNullBeforeOffsetRebind : 1; ///< 커맨드 리스트를 런타임이 흉내 내는 지연 컨텍스트다(같은 버퍼 · 다른 오프셋이면 빈 칸을 먼저 건다)
        /** @brief 상수 버퍼 구간 바인딩용 D3D11.1 컨텍스트입니다. 장치가 D3D11.1 구간 능력을 요구하므로 만들 때 한 번 받는다. */
        Microsoft::WRL::ComPtr<ID3D11DeviceContext1> _context1;
    };
} // namespace sw
#endif
