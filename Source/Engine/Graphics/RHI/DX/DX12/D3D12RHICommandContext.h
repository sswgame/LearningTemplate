#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Foundation/EnginePlatformHeaders.h"
#include "Engine/Graphics/RHI/IRHICommandContext.h"

#if defined( SW_PLATFORM_WINDOWS )

namespace sw
{
    struct D3D12RecordingState;

    class D3D12RHIDevice;

    /**
     * @class D3D12RHICommandContext
     * @brief 실제 D3D12 API 호출을 내는 구현체입니다. `_pCmdList`/`_pState` 로 "어떤 커맨드 리스트에,
     *        어떤 기록 상태로" 기록할지를 주입받습니다. 디바이스의 프레임 스트림 리스트와, `D3D12RHICommandList`
     *        (리스트별 네이티브 커맨드 리스트)가 각자 자신의 (cmdList, state) 쌍으로 이 클래스를 구성해 재사용합니다.
     */
    class D3D12RHICommandContext : public IRHICommandContext
    {
    public:
        D3D12RHICommandContext( D3D12RHIDevice* pDevice, ID3D12GraphicsCommandList* pCmdList, D3D12RecordingState* pState )
            : _pDevice{ pDevice }
            , _pCmdList{ pCmdList }
            , _pState{ pState }
            , _bComputeQueueList{ SW_FALSE }
        {
        }
        ~D3D12RHICommandContext() override = default;

        /** @brief 아직 기록 시작 전이면 얼로케이터와 리스트를 Reset 하고 기록 상태로 표시합니다. */
        void ensureRecording();
        /** @brief 이 컨텍스트가 기록 중인 네이티브 커맨드 리스트를 반환합니다. */
        ID3D12GraphicsCommandList* getNativeCommandList() const { return _pCmdList; }
        /** @brief 기록 대상 네이티브 리스트를 교체합니다(소유자가 얼로케이터 쌍을 바꿀 때). */
        void rebindCommandList( ID3D12GraphicsCommandList* pCmdList ) { _pCmdList = pCmdList; }
        /** @brief 이 컨텍스트가 비동기 컴퓨트 큐(COMPUTE) 리스트에 기록하는지 정합니다. 그 리스트의 배리어는 컴퓨트가 아는 상태만 쓴다(toQueueState). */
        void setComputeQueueList( bool bComputeQueue ) { _bComputeQueueList = bComputeQueue ? SW_TRUE : SW_FALSE; }
        /**
         * @brief 명령을 기록할 네이티브 리스트를 반환합니다. 부르는 것 자체가 "이 리스트에 뭔가 적었다" 는 표시입니다.
         * @details 모든 기록 호출이 이 함수를 지납니다(`_pCmdList->` 직접 접근은 널 검사와 루트 상태 바인딩뿐입니다). 프레임 스트림
         *          조각이 비어 있는지를 디바이스가 이 표시로 압니다. 빈 조각은 잘라 내보내지 않습니다(D3D12RecordingState).
         */
        ID3D12GraphicsCommandList* commandListForRecord();

        void blitTextureNative( RHITextureHandle src, RHITextureHandle dst ) override;
        void bindShaderResource( RHIDescriptorIndex index, uint32 slot ) override;
        void prepareTextureForShaderReadNative( RHITextureHandle texture ) override;
        void prepareTextureForRenderTargetNative( RHITextureHandle texture ) override;
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
        void                     setComputeRootConstants( uint32 rootParameterIndex, uint32 num32BitValues, const void* pData,
                                                          uint32 destOffsetIn32BitValues = 0 ) override;
        void                     setGraphicsRootConstants( uint32 rootParameterIndex, uint32 num32BitValues, const void* pData,
                                                           uint32 destOffsetIn32BitValues = 0 ) override;
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
         * @brief `blitTexture` 의 복사 대상을 한 상태에서 다른 상태로 전이합니다.
         * @details 스왑체인 백버퍼(`dst == 0`)의 상태는 스왑체인 객체만 바꾼다. 여기서 배리어를 따로 쏘고 상태를 직접 대입하면
         *          그 두 벌이 어긋날 수 있다. 오프스크린만 자기 레코드를 갱신한다.
         */
        void transitionBlitDst( RHITextureHandle dst, ID3D12Resource* pDstResource, D3D12_RESOURCE_STATES stateBefore, D3D12_RESOURCE_STATES stateAfter );

        /**
         * @brief 등록된 bindless 인덱스의 버퍼 GPU 주소입니다. 루트 CBV 에 그대로 바인딩합니다(t 와 u 는 슬롯 테이블이라 이 경로를 쓰지 않습니다).
         * @details 레지스트리는 기록 중 불변이라 락 없이 읽습니다(IRHIDevice::setParallelRecording 참고).
         * @param bUAV true 면 UAV 레지스트리, false 면 SRV/CBV 레지스트리입니다.
         * @param bConstantBuffer true 면 링 상수버퍼의 이번 프레임 슬롯 오프셋을 더합니다.
         * @return 인덱스가 범위 밖이거나 슬롯이 비어 있으면 0 입니다.
         */
        D3D12_GPU_VIRTUAL_ADDRESS resolveBufferAddress( RHIDescriptorIndex index, bool bUAV, bool bConstantBuffer ) const;
        /** @brief 등록된 bindless 인덱스의 오프라인 뷰 핸들입니다(슬롯 테이블 복사 원본). 없으면 ptr 0 입니다. */
        D3D12_CPU_DESCRIPTOR_HANDLE resolveOfflineView( RHIDescriptorIndex index, bool bUAV ) const;
        /**
         * @brief 바뀐 t/u 슬롯 테이블을 온라인 블록에 확정해 루트 테이블로 바인딩합니다. 드로우와 디스패치 직전에 부릅니다(Vulkan 의 flushSlotSet 에 해당합니다).
         * @param bCompute true 면 컴퓨트 바인드 포인트(t 와 u), false 면 그래픽스(t 만).
         * @return 바뀐 테이블을 모두 바인딩했으면 true 입니다. 온라인 블록을 빌리지 못해 바인딩하지 못했으면 false 이고, 부르는 쪽은 그 드로우나 디스패치를 건너뜁니다.
         *         주의: 실패를 삼키고 그리면 이전 드로우의 테이블, 이 리스트에서 처음이면 **바인딩된 적 없는 테이블**을 읽어 장치 제거로 이어집니다.
         */
        bool flushSlotTables( bool bCompute );
        /** @brief 슬롯 배열을 온라인 블록에 복사하고 테이블 GPU 핸들을 반환합니다. 바인딩되지 않은 슬롯은 nullView 로 채웁니다. */
        [[nodiscard]] bool writeSlotTable( const D3D12_CPU_DESCRIPTOR_HANDLE* pSlots, uint32 count, D3D12_CPU_DESCRIPTOR_HANDLE nullView, D3D12_GPU_DESCRIPTOR_HANDLE& outTable );
        /** @brief 이 리스트의 온라인 블록에서 count 개를 bump 할당합니다. 블록이 차면 디바이스에서 하나 더 빌립니다. */
        bool allocateOnlineDescriptors( uint32 count, uint32& outBase );
        /** @brief 활성 PSO 가 읽는 정점 버퍼 슬롯(메시 · 인스턴스 슬롯 스트림)만 바인딩합니다. 읽는 슬롯에 버퍼가 없거나 설정한 버퍼가 파괴됐으면 알리고 false 를 반환하며, 부르는 쪽은 드로우를 버립니다(네 백엔드가 같습니다). */
        [[nodiscard]] bool bindMeshVertexBufferForDraw();
        /** @brief 활성 그래픽스 PSO 를 네이티브 리스트에 바인딩합니다(바뀌었을 때만). PSO 가 없으면 false 이고, 드로우를 내지 않습니다. */
        [[nodiscard]] bool bindActiveGraphicsPSO();
        /** @brief 디스패치 직전에 `setComputePipelineState` 로 건 컴퓨트 PSO 가 등록된 레코드인지 봅니다. 아니면 오류로 멈춥니다(드로우의 그래픽스 PSO 와 같은 계약 — 네 백엔드가 같습니다). */
        void requireComputePipelineForDispatch() const;
        /** @brief 건 인덱스 버퍼를 겁니다. 걸지 않았거나 부서졌으면 알리고 false 이며 부르는 쪽은 드로우를 버립니다(네 백엔드가 같다 — `RHIBufferBinding::bindForDraw`). */
        [[nodiscard]] bool bindIndexBufferForDraw();
        void               transitionTexture( RHITextureHandle texture, D3D12_RESOURCE_STATES newState );
        /**
         * @brief 컴퓨트 큐 리스트면 그래픽스 전용 상태(PIXEL_SHADER_RESOURCE)를 빼고 NON_PIXEL 로 둡니다. 그래픽스 리스트면 그대로입니다.
         * @details D3D12 COMPUTE 리스트의 배리어는 픽셀 셰이더 · 렌더 타깃 · 깊이 상태를 쓸 수 없다. 나중에 그래픽스가 픽셀 셰이더로 읽는 버퍼는 큐 경계에서 COMMON 으로 감쇠하고
         *          거기서 암묵 승격된다(버퍼 규칙). 텍스처는 감쇠하지 않으므로 컴퓨트 큐에서 읽을 텍스처는 생산자(그래픽스)가 컴퓨트가 아는 상태(RW · NON_PIXEL)로 넘겨 둔다.
         */
        D3D12_RESOURCE_STATES toQueueState( D3D12_RESOURCE_STATES state ) const;

    private:
        D3D12RHIDevice*            _pDevice;
        ID3D12GraphicsCommandList* _pCmdList;
        D3D12RecordingState*       _pState;
        uint8                      _bComputeQueueList; ///< COMPUTE 리스트에 기록하는가(setComputeQueueList)
    };
} // namespace sw
#endif
