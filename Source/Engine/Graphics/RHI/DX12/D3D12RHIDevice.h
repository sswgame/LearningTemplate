/**
 * @file D3D12RHIDevice.h
 * @brief Direct3D 12 RHI 백엔드 디바이스입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"

#include "Engine/Common/EnginePlatformHeaders.h"
#include "Engine/Config/RHIBackendType.h"
#include "Engine/Graphics/RHI/DX/RHIDxResultCheck.h"
#include "Engine/Graphics/RHI/DX12/D3D12RHISwapChain.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHITimestampProvider.h"
#include "Engine/Graphics/RHI/Support/FrameResourceRing.h"
#include "Engine/Graphics/RHI/Support/RHIConstantBufferMirror.h"
#include "Engine/Graphics/RHI/Support/RHIHandleTable.h"
#include "Engine/Graphics/RHI/Support/RHIMemoryLedger.h"
#include "Engine/Graphics/RHI/Support/RHIReleaseQueue.h"
#include "Engine/Graphics/RHI/Support/RHIRenderPassLedger.h"
#include "Engine/Graphics/RHI/Support/RHIStagingAllocator.h"
#include "Engine/Graphics/RHI/Support/RHITimestamp.h"
#include "Engine/Graphics/RHI/Support/RHITransferSerials.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"

#include <shared_mutex>

#if defined( SW_PLATFORM_WINDOWS )

namespace sw
{
    class D3D12RHICommandContext;
    class D3D12RHICommandList;
    class D3D12RHIResourceFactory;

    /**
     * @struct D3D12SlotTableState
     * @brief 바인드 포인트(그래픽스, 컴퓨트) 하나의 t/u 슬롯 테이블 상태입니다. 언리얼 FD3D12DescriptorCache 의 SRV/UAV 캐시에 해당합니다.
     * @details bind*() 는 오프라인 힙의 뷰 핸들을 슬롯에 기록해 두기만 하고, 드로우나 디스패치 직전에 flushSlotTables 가 바뀐
     *          테이블만 온라인(셰이더 가시) 힙 블록에 복사해 루트 테이블로 바인딩한다. 바인딩되지 않은 슬롯은 null 뷰로 채운다.
     */
    struct D3D12SlotTableState
    {
        D3D12_CPU_DESCRIPTOR_HANDLE _arrSrv[shaderslot::kSrvSlotCount]{};        ///< t# 의 오프라인 뷰. ptr 이 0이면 바인딩되지 않은 슬롯이다.
        D3D12_CPU_DESCRIPTOR_HANDLE _arrUav[shaderslot::kComputeUavSlotCount]{}; ///< u# 의 오프라인 뷰. ptr 이 0이면 바인딩되지 않은 슬롯이다.
        uint8                       _bSrvDirty{ SW_TRUE };                       ///< 새 리스트는 테이블이 없으므로 첫 드로우에서 반드시 확정한다.
        uint8                       _bUavDirty{ SW_TRUE };
    };
} // namespace sw

namespace sw
{
    /**
     * @struct D3D12RecordingState
     * @brief "지금 이 커맨드 리스트가 기록 중" 인 상태입니다. 디바이스 전역이 아니라 리스트(컨텍스트)마다 있어야 합니다.
     * @details `D3D12RHICommandList` 가 자기 것을 소유하므로 리스트끼리 독립이고 병렬로 기록할 수 있다. 디바이스 전역에
     *          두면 모든 기록이 한 상태를 나눠 써서 병렬 기록이 서로의 바인딩을 덮어쓴다. 스왑체인 리소스 상태처럼 "실제 GPU
     *          리소스의 상태" 를 나타내는 것은 여기 넣지 않는다. 그것은 디바이스와 리소스 전역에 속한다.
     */
    struct D3D12RecordingState
    {
        /// @brief 바인드 포인트별 슬롯 테이블 상태입니다([0] 그래픽스, [1] 컴퓨트). 서로 독립이라 디스패치가 드로우의 바인딩을 지우지 않습니다.
        D3D12SlotTableState _arrSlotState[2];
        /// @brief 이 리스트가 빌린 온라인 힙 블록들입니다. 리스트가 닫힐 때 GPU 펜스 뒤에 반납합니다(releaseOnlineBlocksDeferred).
        vector<uint32> _listOnlineBlock;
        /// @brief 지금 쓰는 온라인 블록의 커서와 끝입니다(셰이더 가시 힙 인덱스). 같으면 블록이 없다는 뜻입니다.
        uint32                 _onlineCursor;
        uint32                 _onlineEnd;
        RHIBufferHandle        _boundMeshVb;
        uint32                 _boundMeshStride;
        uint32                 _boundMeshOffset;
        RHIBufferHandle        _boundInstanceSlotVb; ///< 슬롯 1: 인스턴스 슬롯 스트림(0이면 바인딩 안 됨)
        uint32                 _boundInstanceSlotOffset;
        RHIBufferHandle        _boundIndexBuffer;
        uint32                 _boundIndexStride;
        uint32                 _boundIndexOffset;
        RHIPipelineStateHandle _activeGraphicsPSO;
        /** @brief _pCmdList 에 실제로 SetPipelineState 가 나간 PSO 핸들입니다.
         *  드로우가 _activeGraphicsPSO 와 같으면 다시 설정하지 않습니다. Reset() 직후에는
         *  0 으로 되돌려야 합니다(그 리스트에는 아직 아무 PSO 도 설정되지 않아 캐시가 무효입니다). */
        RHIPipelineStateHandle _boundNativeGraphicsPSO;
        RHITextureHandle       _arrActiveColorTarget[kMaxColorAttachments];
        RHITextureHandle       _activeDepthTarget;
        uint8                  _bActiveSwapchainRT : 1;
        uint8                  _bRecording         : 1;
        /**
         * @brief 이 리스트에 명령이 하나라도 기록되었는지 나타냅니다(컨텍스트의 `commandListForRecord` 가 설정합니다).
         * @details 프레임 스트림 조각이 비어 있으면 `executeCommandList` 가 자르지 않는다. 레벨 배리어는 패스 리스트에
         *          들어가므로 레벨 사이의 조각은 항상 비어 있는데, 잘라서 내보내면 큐에 빈 리스트가 나가고 그 제출이 리스트당 약 7µs 다.
         */
        uint8                  _bRecordedAny : 1;
        [[maybe_unused]] uint8 _reserved     : 5;

        /** @brief 아무것도 기록하지 않은 상태로 만듭니다. */
        D3D12RecordingState()
            : _arrSlotState{}
            , _listOnlineBlock{}
            , _onlineCursor{ 0 }
            , _onlineEnd{ 0 }
            , _boundMeshVb{ 0 }
            , _boundMeshStride{ 0 }
            , _boundMeshOffset{ 0 }
            , _boundInstanceSlotVb{ 0 }
            , _boundInstanceSlotOffset{ 0 }
            , _boundIndexBuffer{ 0 }
            , _boundIndexStride{ 4 }
            , _boundIndexOffset{ 0 }
            , _activeGraphicsPSO{ 0 }
            , _boundNativeGraphicsPSO{ 0 }
            , _arrActiveColorTarget{}
            , _activeDepthTarget{ 0 }
            , _bActiveSwapchainRT{ SW_FALSE }
            , _bRecording{ SW_FALSE }
            , _bRecordedAny{ SW_FALSE }
            , _reserved{ 0 }
        {
        }
    };
} // namespace sw

namespace sw
{
    /**
     * @struct D3D12CommandListEntry
     * @brief 커맨드 리스트와 **그 리스트 전용** 얼로케이터 한 쌍입니다.
     * @details D3D12 는 하나의 얼로케이터에 동시에 두 리스트를 기록할 수 없고, 기록 중인 리스트가
     *          있으면 Reset 도 할 수 없다. `RenderGraph::executeParallel` 이 패스마다 리스트를 만들어
     *          여러 스레드에서 동시에 기록하므로, 리스트는 반드시 자기 얼로케이터를 가져야 한다.
     */
    struct D3D12CommandListEntry
    {
        Microsoft::WRL::ComPtr<ID3D12CommandAllocator>    _allocator;
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> _list;
    };
} // namespace sw

namespace sw
{
    /**
     * @class D3D12RHIDevice
     * @brief Direct3D 12 그래픽스 디바이스 구현입니다(bindless 지원).
     */
    class D3D12RHIDevice : public IRHIDevice, public IRHITimestampProvider
    {
    public:
        friend class D3D12RHIResourceFactory;
        friend class D3D12RHICommandContext;
        friend class D3D12RHICommandList;
        // ------------------------------------------------------------------------------
        // 1) 수명: 디바이스, 큐, 스왑체인, 프레임
        // ------------------------------------------------------------------------------
        /** @brief 빈 D3D12 디바이스를 만듭니다. */
        D3D12RHIDevice();
        /** @brief D3D12 자원과 펜스를 해제합니다. */
        virtual ~D3D12RHIDevice() override;

        /** @brief Direct3D 12 디바이스, 커맨드 큐, DXGI 스왑체인, 힙 리소스를 초기화합니다. */
        [[nodiscard]] bool initializeInternal( const RHISwapChainDesc& desc ) override;

        /** @brief D3D12 자원과 펜스 동기화 객체를 해제합니다. */
        void shutdownInternal() override;
        /** @brief 스왑체인의 렌더 스레드 역할도 같은 소유자로 맞춥니다. */
        void onRenderThreadRoleChanged( std::thread::id owner ) override;

        /** @brief 종료 3단계입니다. 프레임 스트림 컨텍스트를 해제하고 살아 있는 커맨드 리스트를 분리합니다. */
        void detachCommandRecordingInternal() override;

        /** @brief GPU 가 제출된 명령을 모두 끝낼 때까지 펜스로 기다립니다. */
        void waitIdleInternal() override;

        /** @brief DXGI `QueryVideoMemoryInfo` 로 이 프로세스의 사용량(로컬과 비로컬)과 로컬 예산을 채웁니다. 어댑터를 찾지 못하면 false 입니다. */
        [[nodiscard]] bool queryGPUMemoryBudgetInternal( RHIMemoryBudget& outBudget ) override;

        /** @brief 이번 프레임이 Signal 할 펜스 값(`_fenceValue`)을 기준으로 해제 큐에 넣습니다. */
        void   enqueueGPURelease( const RHIResourceReleaseDelegate& releaseDelegate ) override;
        uint32 getPendingReleaseCount() const override { return _releaseQueue.getPendingReleaseCount(); }
        /** @brief `ID3D12Device5::RemoveDevice` 로 장치를 제거합니다. 이후 펜스는 끝난 값(UINT64_MAX)을 돌려주고 호출은 DXGI_ERROR_DEVICE_REMOVED 로 실패한다. */
        bool   debugRemoveDevice() override;
        uint32 getValidationMessageCount() const override { return _validationMessageCount.load( std::memory_order_relaxed ); }

        /** @brief 리소스 생성과 파괴 인터페이스(D3D12RHIResourceFactory)를 반환합니다. */
        IRHIResourceFactory* getResourceFactory() override;
        /** @brief 프레임 스트림 컨텍스트입니다. 백버퍼 패스와 Present 가 여기에 기록합니다. */
        IRHICommandContext* getFrameStreamContext() override;

        /** @brief 백엔드 종류(DirectX12)를 반환합니다. */
        RHIBackend getBackendType() const override { return RHIBackend::DirectX12; }

        /**
         * @brief 온라인(셰이더 가시) 힙 블록 하나를 빌립니다. 슬롯 테이블을 확정할 때 씁니다. 없으면 UINT32_MAX 이며 로그는 한 번만 남깁니다.
         * @details 언리얼의 FD3D12SubAllocatedOnlineHeap 처럼 전역 힙의 뒤쪽 구간을 블록으로 잘라 컨텍스트마다 빌려 준다.
         *          테이블과 텍스처 배열이 같은 힙에 있어야 하기 때문이다(CBV_SRV_UAV 힙은 한 번에 하나만 바인딩할 수 있다).
         *          여러 태스크 스레드가 부르므로 잠근다(블록 단위라 드로우마다 잠기지는 않는다).
         */
        uint32 acquireOnlineBlock();
        /**
         * @brief 기록 상태가 빌린 온라인 블록들을 현재 펜스 뒤에 프리리스트로 돌려보냅니다(리스트가 닫힐 때).
         * @details 블록 벡터를 **통째로 옮겨** 묶음에 넣는다. 주의: 벡터를 복사해 람다에 캡처하면 그 람다가 SBO 를 넘어
         *          힙으로 간다(리스트마다 프레임마다 두 번). 상태는 빈 벡터(용량은 남아 있다)를 돌려받는다.
         */
        void releaseOnlineBlocksDeferred( D3D12RecordingState& state );
        /** @brief 펜스가 지난 묶음의 블록을 프리리스트로 돌려보냅니다. 펜스 완료 값을 읽는 지점에서 부릅니다. */
        void recycleCompletedOnlineBlocks( uint64 completedFence );
        /** @brief `recycleCompletedOnlineBlocks` 의 본문입니다. `_onlineBlockMutex` 를 잡은 쪽이 부릅니다(`acquireOnlineBlock` 이 바닥났을 때). */
        void recycleCompletedOnlineBlocksLocked( uint64 completedFence ) SW_REQUIRES_LOCK( _onlineBlockMutex );
        /** @brief 오프라인(CPU 전용) 뷰 힙의 index 번째 핸들을 반환합니다. */
        D3D12_CPU_DESCRIPTOR_HANDLE offlineDescriptorAt( uint32 index ) const;
        /** @brief 셰이더 가시 힙의 index 번째 CPU 핸들을 반환합니다(복사 목적지). */
        D3D12_CPU_DESCRIPTOR_HANDLE shaderVisibleCPUAt( uint32 index ) const;
        /** @brief 셰이더 가시 힙의 index 번째 GPU 핸들을 반환합니다(루트 테이블 인자). */
        D3D12_GPU_DESCRIPTOR_HANDLE shaderVisibleGPUAt( uint32 index ) const;
        /** @brief 스왑체인이 만든 백버퍼 포맷입니다. 백버퍼 PSO 의 렌더 타깃 포맷이 여기서 나옵니다. */
        RHIFormat getBackBufferFormat() const override { return _swapChain.getFormat(); }

        /** @brief 루트 시그니처의 무제한 텍스처 배열 테이블(t0 space1)로 g_SwBindlessTex2D[] 를 샘플링합니다. SM6.6 힙 인덱싱이 아닙니다. */
        bool supportsNativeBindlessSampling() const override { return _bBindlessRootSignature != SW_FALSE; }

        /** @brief VS 가 슬롯 테이블의 t4(g_SwInstances)에서 인스턴스를 읽을 수 있으면 true 입니다(루트 시그니처가 만들어졌을 때). */
        bool supportsInstancedSceneDraw() const override { return _bBindlessRootSignature != SW_FALSE; }

    #if !defined( SW_SHIPPING )
        /** @brief 힙 슬롯 @p index 에 그 종류(텍스처 · 버퍼)의 뷰가 등록돼 있으면 true 입니다. 텍스처와 버퍼는 힙 하나를 나눠 쓴다. */
        bool isBindlessIndexLive( RHIBindlessIndexSpace space, RHIDescriptorIndex index ) const override;
    #endif

        /** @brief 정적 테이블에 런타임 값(네이티브 bindless)을 반영해 반환합니다. */
        RHICapabilities getCapabilities() const override
        {
            RHICapabilities caps  = RHIAvailability::query( RHIBackend::DirectX12 );
            caps._bNativeBindless = _bBindlessRootSignature != SW_FALSE ? 1u : 0u;
            // 텍스처 업로드는 영속 매핑 스테이징 링으로만 간다(링이 없으면 초기화가 실패한다). 되읽기는 읽기마다 자기 읽기 힙 버퍼를 만들어 되읽기 링이 없다.
            caps._bPersistentStagingRing = _uploadRing._buffer != nullptr ? SW_TRUE : SW_FALSE;
            return caps;
        }

        /** @brief 백엔드 이름을 반환합니다. */
        const utf8* getBackendName() const override { return "Direct3D 12"; }

    protected:
        // 네이티브 핸들 getter 는 기반처럼 보호 구역이다 — 바깥은 종류 확인이 든 `IRHIDevice::queryNativeHandles` · `tryGetNativeTexturePointer` 로만 읽는다.
        /** @brief ID3D12Device 포인터를 반환합니다. */
        void* getNativeDevice() const override { return _device.Get(); }

        /** @brief ID3D12CommandQueue 포인터를 반환합니다. */
        void* getNativeCommandQueue() const override { return _commandQueue.Get(); }

        /** @brief 네이티브 텍스처 포인터(ID3D12Resource*)를 반환합니다. */
        void* getNativeTexturePointer( RHITextureHandle texture ) const override;

    public:
        /** @brief 독립 커맨드 리스트를 만듭니다. */
        unique_ptr<IRHICommandList> createCommandList() override;

        /** @brief 살아 있는 커맨드 리스트를 등록합니다(소유하지 않는 참조). */
        void registerCommandList( D3D12RHICommandList* pCmdList );
        /** @brief 등록을 해제합니다. */
        void unregisterCommandList( D3D12RHICommandList* pCmdList );

        /** @brief 프레임을 엽니다. 링 슬롯이 풀릴 때까지 기다리고 프레임 스트림 리스트를 엽니다. 백버퍼 클리어는 beginRenderPass(핸들 0)가 합니다. */
        void beginFrame() override;
        void endFrame( bool vsync = true, bool bPresent = true ) override;

        void               setTimestampEnabled( bool bEnabled ) override { _bTimestampEnabled.store( bEnabled ? SW_TRUE : SW_FALSE, std::memory_order_relaxed ); }
        uint32             getTimestampSlotCount() const override;
        [[nodiscard]] bool readTimestamps( RHITimestampFrame& outFrame ) override;
        [[nodiscard]] bool readGPUClockNanos( int64& outGPUNanos ) override;
        bool               isGPUClockReadCheap() const override { return true; }

        /** @brief 타임스탬프 쿼리 힙을 반환합니다. 준비되지 않았으면 nullptr 입니다. */
        ID3D12QueryHeap* getTimestampHeap() const { return _timestampHeap.Get(); }
        /** @brief 이번 프레임이 쓰는 쿼리 구간의 시작 인덱스를 반환합니다. */
        uint32 getTimestampBase() const { return _frameRing.currentIndex() * constant::kMaxGPUTimestampSlot; }
        /** @brief 슬롯 하나를 기록했다고 표시합니다. 여러 패스 스레드가 동시에 부를 수 있습니다. */
        void markTimestampWritten( uint32 slotIndex )
        {
            _timestampWrittenMask.fetch_or( makeGPUTimestampSlotBit( slotIndex ), std::memory_order_relaxed );
        }

    protected:
        /**
         * @brief 지금 기록 중인 ID3D12GraphicsCommandList 포인터(프레임 스트림의 활성 세그먼트)입니다. 밖으로는 `withNativeContext` 로만 나갑니다.
         * @details 에디터 ImGui 백엔드가 렌더 스레드(present 훅)에서 이 리스트에 직접 드로우를 기록한다. 리스트와 `_pActiveFrameList` 는 렌더 스레드만 쓰므로
         *          잡을 잠금이 없다. _commandList 는 '첫 세그먼트' 일 뿐이고 커맨드 리스트가 제출될 때마다 닫힌다. 주의: 그것을 그대로 반환하면 닫힌 리스트에
         *          기록하게 되어 UI 가 통째로 사라진다.
         */
        void* getNativeContext() const override { return _pActiveFrameList != nullptr ? _pActiveFrameList : _commandList.Get(); }

    private:
        /** @brief 쿼리 힙과 읽기 버퍼를 한 번만 만듭니다. */
        void ensureTimestampResources();
        /** @brief 방금 펜스를 통과한 링 슬롯의 결과를 마이크로초로 변환합니다. */
        void collectTimestampsForSlot();

    public:
        void resizeInternal( uint32 width, uint32 height ) override;

        void executeCommandList( IRHICommandList* pCmdList ) override;
        void executeCommandListImmediate( IRHICommandList* pCmdList ) override;

    private:
        /**
         * @brief 이전 프레임이 끝날 때까지 기다립니다.
         */
        void waitForPreviousFrame();
        /**
         * @brief 링 슬롯이 GPU 에서 풀릴 때까지 기다린 뒤 다음 슬롯으로 진행합니다.
         */
        void waitForRingSlot();
        /**
         * @brief 펜스가 fenceValue 에 닿을 때까지 기다립니다(이미 지났으면 바로 true). 2초 안에 닿지 못하면 false 입니다.
         * @details 링 슬롯 대기와 달리 **특정 값 하나**만 본다. "이 얼로케이터의 마지막 제출이 끝났는가" 처럼
         *          프레임 링과 무관한 경우의 대기다. Signal 되지 않은 값을 넘기면 타임아웃으로 끝난다.
         */
        bool waitForFenceValue( uint64 fenceValue );
        /**
         * @brief 현재 링 슬롯에 펜스를 기록하고 해제 큐를 진행합니다. GPU 를 기다리지 않습니다.
         */
        void signalCurrentFrame();
        /** @brief 현재 링 슬롯의 커맨드 얼로케이터입니다(디바이스 프레임 스트림 전용). */
        ID3D12CommandAllocator* currentAllocator();

    public:
        /**
         * @brief 프레임 스트림의 다음 세그먼트를 열고 기록 대상으로 만듭니다.
         * @details 커맨드 리스트가 제출될 때마다 스트림을 여기서 자른다(executeCommandList 참고).
         */
        ID3D12GraphicsCommandList* beginNextFrameSegment();

        /**
         * @brief 병렬 기록용 커맨드 리스트와 전용 얼로케이터 한 쌍을 풀에서 빌립니다.
         * @details 풀에 남은 것이 없으면 새로 만든다. 풀로 돌아온 항목은 이미 GPU 펜스를 통과한 것이라
         *          곧바로 Reset 해도 안전하다. 여러 태스크 스레드가 동시에 부르므로 안에서 잠근다.
         */
        D3D12CommandListEntry acquireCommandListEntry();
        /**
         * @brief 다 쓴 리스트와 얼로케이터 쌍을 GPU 가 끝낸 뒤 풀로 돌려보냅니다.
         * @details 제출 직후 파괴되더라도 GPU 는 아직 그 얼로케이터의 커맨드 메모리를 읽고 있다.
         *          그래서 해제 큐에 실어 현재 펜스가 통과한 다음에 재사용 풀로 되돌린다.
         */
        void recycleCommandListEntryDeferred( const D3D12CommandListEntry& entry );

    private:
        /** @brief 불투명 버퍼 핸들로 GPU 리소스를 찾습니다. */
        ID3D12Resource* resolveBuffer( RHIBufferHandle handle ) const;
        /** @brief 불투명 텍스처 핸들로 GPU 리소스를 찾습니다. */
        ID3D12Resource* resolveTexture( RHITextureHandle handle ) const;
        /**
         * @brief 텍스처의 추적 상태입니다. 상태를 추적하는 것은 오프스크린 렌더 타깃뿐이라 그 밖의 텍스처는 COMMON 입니다.
         * @details `_resourceStateMutex` 를 잡고 읽는다. 복사 전이는 이 상태에서 출발해 같은 상태로 돌아와야 SRV 바인딩
         *          경로(COMMON 암묵 승격)가 그대로 맞는다.
         */
        D3D12_RESOURCE_STATES getTrackedTextureState( RHITextureHandle texture );
        struct OffscreenTargetView;
        /**
         * @brief 오프스크린 레코드를 `_resourceStateMutex` 안에서 찾아 @p slice 면의 뷰와 함께 복사합니다. 오프스크린 레코드가 없으면 false 입니다.
         * @details 텍스처 생성(`createTexture2D`)이 다른 스레드에서 이 테이블을 재해시하는 동안 기록 워커가 읽으면 안 된다. 테이블의 이터레이터를 기록 경로에 가지고 다니지 않는다.
         */
        [[nodiscard]] bool findOffscreenTargetView( RHITextureHandle texture, uint32 slice, OffscreenTargetView& outView );
        /** @brief RTV 힙의 `rtvIndex` 번 디스크립터입니다(스왑체인 버퍼 수를 포함한 절대 번호). */
        D3D12_CPU_DESCRIPTOR_HANDLE getOffscreenRtvHandle( uint32 rtvIndex ) const;
        /** @brief DSV 힙의 `dsvIndex` 번 디스크립터입니다. */
        D3D12_CPU_DESCRIPTOR_HANDLE getOffscreenDsvHandle( uint32 dsvIndex ) const;
        /** @brief ComPtr 을 핸들 테이블에 넣고 핸들을 반환합니다. GPU 메모리 장부의 Buffer 항목에 할당 크기를 올리는 유일한 지점입니다. */
        RHIBufferHandle storeBuffer( Microsoft::WRL::ComPtr<ID3D12Resource> buffer );
        /**
         * @brief 업로드 힙(GENERIC_READ)에 버퍼를 만들고 CPU 주소로 매핑합니다. 실패하면 false 이고 `outBuffer` 는 비웁니다. 로그는 호출하는 쪽이 남깁니다.
         * @details 상수 버퍼 링, 정점/인덱스 업로드 버퍼, 전체 화면 삼각형, 업로드 스테이징 네 곳이 "만들기, 매핑" 을 이 함수로 처리한다.
         */
        [[nodiscard]] bool createMappedUploadBuffer( uint64 sizeBytes, Microsoft::WRL::ComPtr<ID3D12Resource>& outBuffer, void*& pOutMapped );
        /** @brief ComPtr 을 핸들 테이블에 넣고 핸들을 반환합니다. GPU 메모리 장부의 `kind` 항목에 할당 크기를 올리는 유일한 지점입니다. */
        RHITextureHandle storeTexture( Microsoft::WRL::ComPtr<ID3D12Resource> texture, RHIMemoryKind kind );
    #if defined( SW_DEBUG )
        /** @brief 리소스에 `SW <종류> <핸들> (<폭>x<높이> fmt <포맷>)` 이름을 붙입니다. DRED 페이지 폴트 출력이 폴트 주소의 할당을 이 이름으로 보여 줍니다(Debug 전용). */
        static void nameResourceForDiagnostics( ID3D12Resource* pResource, const utf16* pKind, uint64 handle );
    #endif
        /** @brief 자원 하나가 차지하는 실제 할당 크기입니다(`GetResourceAllocationInfo`이며 정렬과 패딩을 포함합니다). */
        uint64 computeAllocationBytes( ID3D12Resource* pResource ) const;
        /**
         * @brief 핸들 테이블에서 꺼낸 자원을 지금 펜스 뒤에 해제하고, 그때 장부에서 뺍니다. destroyBuffer 와 destroyTexture 가 공통으로 쓰는 해제 함수입니다.
         * @details 장부는 GPU 가 실제로 해제한 뒤에 줄어야 한다. 해제 요청 시점에 빼면 아직 GPU 메모리를 쥔 자원이 장부에서 먼저 사라진다.
         *          끝나지 않은 전송이 있는 텍스처는 이 함수가 아니라 `releaseTextureAfterTransfer` 를 거쳐 온다(전송 완료 뒤에야 여기로 넘어온다).
         */
        void releaseTrackedResourceDeferred( Microsoft::WRL::ComPtr<ID3D12Resource> owned, const RHIMemoryKey& key );
        /**
         * @brief 루트 시그니처(루트 CBV, t/u 슬롯 테이블, 텍스처 배열 테이블, 루트 상수, 정적 샘플러), 커맨드 시그니처, 풀스크린 정점 버퍼를 만듭니다.
         */
        [[nodiscard]] bool createGlobalResources();
        /**
         * @brief 새로 연 커맨드 리스트에 셰이더 가시 힙, 루트 시그니처(그래픽스/컴퓨트), 텍스처 배열 테이블을 바인딩합니다.
         * @details 리스트가 열릴 때 한 번이면 된다. 이후 드로우는 루트 CBV, 슬롯 테이블, 루트 상수만 바꾼다. 힙을 바인딩하는 4곳
         *          (프레임 스트림 begin, 세그먼트, 리스트 begin, 프레임 스트림 컨텍스트의 ensureRecording)이 모두 이 함수를 부른다.
         */
        void bindBindlessRootState( ID3D12GraphicsCommandList* pList );
        /**
         * @brief 링 상수 버퍼들의 힙 CBV 를 이번 프레임 슬롯으로 맞춥니다. 프레임당 한 번, 기록 시작 전에 부릅니다.
         * @details 주소는 프레임 링 슬롯에만 의존하므로 프레임당 한 번이면 충분하다. 주의: updateConstantBuffer 에서
         *          드로우마다 레지스트리를 훑으면 O(등록 수 × 드로우 수)가 되어 드로우 경로를 지배한다.
         */
        void refreshConstantBufferViews();
        /**
         * @brief 링 상수 버퍼의 `slot` 슬롯에 씁니다. `_bindlessMutex` 를 (읽기로라도) 잡고 부릅니다.
         * @details 만들 때 크기를 넘는 쓰기는 자른다. 그대로 복사하면 다음 슬롯(다음 프레임의 값)까지 덮어쓴다.
         */
        void writeConstantBufferSlot( RHIBufferHandle buffer, uint32 slot, const void* pData, uint32 size );
        /** @brief 링이 넘어온 슬롯에 옛 값이 남은 상수 버퍼를 마지막 값으로 채웁니다(`RHIConstantBufferMirror`). `waitForRingSlot` 이 부릅니다. */
        void fillConstantBufferSlot();
        /**
         * @brief D3D12 InfoQueue 메시지를 로그로 비웁니다.
         */
        void flushDebugMessages( const utf8* pStage );
        /**
         * @brief 디버그 레이어가 메시지를 내는 그 자리에서 부르는 콜백입니다(`ID3D12InfoQueue1::RegisterMessageCallback`, Debug 만). 경고 이상을 세고 앞의 몇 개를 로그로 남깁니다.
         * @details 기록 스레드에서 API 호출 안에서 불린다. 그래서 로그 바로 앞의 패스 구간 로그와 함께 읽으면 어느 패스의 호출인지 보인다.
         */
        static void CALLBACK onValidationMessage( D3D12_MESSAGE_CATEGORY category, D3D12_MESSAGE_SEVERITY severity, D3D12_MESSAGE_ID id, LPCSTR pDescription,
                                                  void* pContext );

        static constexpr uint32 kMaxOffscreenRtvs = 128; ///< 면마다 하나씩 쓴다. 큐브 하나가 여섯을 쓴다.
        static constexpr uint32 kMaxOffscreenDsvs = 64;
        // 루트 시그니처. 언리얼 FD3D12RootSignature 와 같은 배치다. b# 는 루트 CBV(GPU 주소)이고, t#/u# 슬롯은 **디스크립터 테이블**이다
        // (오프라인 힙의 뷰를 드로우 직전 온라인 블록에 복사해 바인딩한다. FD3D12DescriptorCache). 텍스처 배열(t0 space1)은 힙 시작을
        // 가리키는 테이블이다. 예산은 shaderslot::dx12 (25/64 dword)이고, 슬롯을 늘려도 테이블 안이라 예산이 늘지 않는다.
        // 슬롯 번호는 bindingslots.hlsli(shaderslot)가 정한다. 드로우별 데이터는 GPUScene 버퍼의 원소라 바인딩은 배치마다 한 번이다.
        static constexpr uint32 kCbvRootParam0             = 0;                                                     ///< b0..b(N-1) 루트 CBV
        static constexpr uint32 kSrvTableParam             = kCbvRootParam0 + shaderslot::kConstantBufferSlotCount; ///< t0..t(N-1) space0 테이블
        static constexpr uint32 kUavTableParam             = kSrvTableParam + 1;                                    ///< u0..u(N-1) space0 테이블
        static constexpr uint32 kBindlessTextureTableParam = kUavTableParam + 1;                                    ///< t0/u0 space1 무제한 텍스처 배열 테이블
        static constexpr uint32 kRootConstantsParam        = kBindlessTextureTableParam + 1;                        ///< b0 space2 32비트 루트 상수 (set*RootConstants)
        static constexpr uint32 kRootParameterCount        = kRootConstantsParam + 1;
        static_assert( kRootParameterCount == shaderslot::dx12::kRootCbvCount + shaderslot::dx12::kRootTableCount + 1,
                       "루트 파라미터 배치가 shaderslot::dx12 예산 계산과 어긋난다" );
        /// @brief 슬롯 테이블 하나의 최대 원소 수입니다(t 테이블과 u 테이블 중 큰 쪽).
        static constexpr uint32 kMaxSlotTableSize = shaderslot::kSrvSlotCount > shaderslot::kComputeUavSlotCount ? shaderslot::kSrvSlotCount : shaderslot::kComputeUavSlotCount;

        /// @brief 오프스크린 텍스처와 RTV, SRV 핸들입니다.
        struct OffscreenTextureRecord
        {
            D3D12_CPU_DESCRIPTOR_HANDLE _rtvHandle{};
            D3D12_CPU_DESCRIPTOR_HANDLE _dsvHandle{};
            uint32                      _rtvIndex{ 0 };
            uint32                      _dsvIndex{ 0 };
            D3D12_RESOURCE_STATES       _state  = D3D12_RESOURCE_STATE_COMMON;
            DXGI_FORMAT                 _format = DXGI_FORMAT_UNKNOWN;
            uint32                      _width{ 0 };
            uint32                      _height{ 0 };
            /// @brief 면 1부터의 RTV, DSV 힙 인덱스입니다(면 0 은 `_rtvIndex`, `_dsvIndex`). 면이 하나면 비어 있습니다.
            vector<uint32>      _listExtraRtvIndex;
            vector<uint32>      _listExtraDsvIndex;
            uint32              _arraySize{ 1 };
            RHITextureDimension _dimension{ RHITextureDimension::Texture2D };
            uint8               _bHasRtv  : 1;
            uint8               _bHasDsv  : 1;
            uint8               _reserved : 6;
        };

        /// @brief 기록 경로가 오프스크린 레코드에서 읽는 항목입니다. `findOffscreenTargetView` 가 잠근 채 복사해 줍니다.
        struct OffscreenTargetView
        {
            D3D12_CPU_DESCRIPTOR_HANDLE _rtvHandle{};                          ///< 고른 면의 RTV(없으면 0)
            D3D12_CPU_DESCRIPTOR_HANDLE _dsvHandle{};                          ///< 고른 면의 DSV(없으면 0)
            D3D12_RESOURCE_STATES       _state  = D3D12_RESOURCE_STATE_COMMON; ///< 복사한 순간의 상태(전이는 `transitionTexture` 가 다시 잠그고 처리한다)
            DXGI_FORMAT                 _format = DXGI_FORMAT_UNKNOWN;
            uint8                       _bHasRtv{ SW_FALSE };
            uint8                       _bHasDsv{ SW_FALSE };
            uint8                       _bSliceInRange{ SW_FALSE }; ///< 고른 면이 면 수 안에 있고 그 면의 뷰가 있다.
        };

        /// @brief 네이티브 PSO 와 루트 시그니처입니다.
        struct D3D12PipelineStateRecord
        {
            Microsoft::WRL::ComPtr<ID3D12PipelineState> _pso;
        };

        // 셰이더 가시 힙 하나는 [레지스트리 구간 | 온라인 블록 구간]으로 나뉜다. 레지스트리는 bindless 인덱스가 곧 힙 슬롯이다(텍스처 배열이 힙 시작을
        // 가리킨다). 온라인 구간은 슬롯 테이블을 확정하는 블록들이다(컨텍스트가 빌리고 펜스 뒤 반납한다). 언리얼의 bindless 힙과 서브할당
        // 온라인 힙을 나누는 방식과 같다. 오프라인(CPU 전용) 힙은 레지스트리와 같은 인덱스 공간에 null 뷰 둘을 더한 것이다.
        static constexpr uint32 kMaxShaderVisibleDescriptors = shaderslot::bindless::kDx12DescriptorHeapSize; ///< 셰이더가 첨자를 이 크기로 마스크한다
        static constexpr uint32 kBindlessDescriptorCapacity  = 32768;
        static constexpr uint32 kOnlineBlockDescriptorCount  = 512;
        static constexpr uint32 kOnlineBlockCount            = ( kMaxShaderVisibleDescriptors - kBindlessDescriptorCapacity ) / kOnlineBlockDescriptorCount;
        static constexpr uint32 kOfflineNullSrvIndex         = kBindlessDescriptorCapacity;     ///< 바인딩되지 않은 t 슬롯을 채우는 null 버퍼 SRV
        static constexpr uint32 kOfflineNullUavIndex         = kBindlessDescriptorCapacity + 1; ///< 바인딩되지 않은 u 슬롯을 채우는 null 버퍼 UAV
        static constexpr uint32 kOfflineDescriptorCount      = kBindlessDescriptorCapacity + 2;
        /// @brief 레지스트리 0번은 null Texture2D SRV 로 비워 둡니다. 등록은 1번부터 받습니다.
        /// @details 이 힙은 CBV, 버퍼 SRV, 텍스처 SRV, UAV 가 한 인덱스 공간을 씁니다. 0 으로 초기화된 텍스처 인덱스(머티리얼 없는 배치의 폴백 원소 등)가
        ///          그 위치의 상수 버퍼나 구조 버퍼를 Texture2D 로 읽으면 값이 정의되지 않습니다(채널마다 NaN). null 뷰는 0 을 읽습니다.
        static constexpr uint32 kNullTextureBindlessIndex = 0;
        static_assert( kOnlineBlockDescriptorCount >= kMaxSlotTableSize, "온라인 블록이 슬롯 테이블 하나보다 작다" );

        /// @brief 힙 슬롯에 등록된 버퍼와 텍스처입니다.
        struct BindlessResourceRecord
        {
            Microsoft::WRL::ComPtr<ID3D12Resource> _resource;
            D3D12_CPU_DESCRIPTOR_HANDLE            _cpuHandle{};        ///< 셰이더 가시 힙의 핸들(텍스처 배열이 인덱스로 읽는 위치)
            D3D12_CPU_DESCRIPTOR_HANDLE            _offlineCPUHandle{}; ///< 오프라인 힙의 같은 뷰. 슬롯 테이블 복사 원본이다(가시 힙은 복사 원본이 될 수 없다).
            RHIBufferHandle                        _buffer{ 0 };
            RHITextureHandle                       _texture{ 0 };
        };

        // 구조 버퍼 갱신(updateStructuredBufferRegions) 전용 프레임 링 슬롯. 매 호출마다 업로드 힙과 커맨드 리스트를
        // 새로 만들지 않도록 재사용한다. 이 슬롯은 waitForRingSlot() 이 이미 보장한 프레임 링 안전성에
        // 편승한다(같은 인덱스를 다시 쓸 때는 constant::kMaxFrameCountInFlight 프레임 전 제출이 이미 GPU 에서 끝났다).
        // 텍스처 전송(submitTexture*)은 이 슬롯을 쓰지 않는다 — 전송 작업과 스테이징 링이 따로 있다(`D3D12TransferJob`, `D3D12StagingRing`).
        /**
         * @struct StructuredUploadSlot
         * @brief 구조 버퍼 갱신(updateStructuredBufferRegions)이 쓰는 프레임 링 슬롯 하나입니다. 스테이징 힙과 복사 얼로케이터, 리스트로 이루어집니다.
         * @details 한 프레임 안에서 여러 번 불린다(GPUScene 만 해도 인스턴스, 배치 테이블, 간접 인자, 머티리얼 그룹). 그래서
         *          얼로케이터는 **펜스 구간마다 한 번만** Reset 하고(_resetFence), 스테이징은 bump
         *          오프셋으로 이어서 쓴다(_uploadOffset). 주의: 호출마다 둘 다 처음부터 다시 쓰면 두 번째 호출이
         *          첫 번째 복사가 아직 실행 중인 얼로케이터를 Reset 한다("allocator is being reset [in use]"
         *          후 디바이스 제거, 세그폴트).
         */
        struct StructuredUploadSlot
        {
            Microsoft::WRL::ComPtr<ID3D12Resource>            _uploadHeap;
            Microsoft::WRL::ComPtr<ID3D12CommandAllocator>    _copyAllocator;
            Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> _copyCommandList;
            void*                                             _pMapped{ nullptr };
            uint64                                            _capacity{ 0 };
            uint64                                            _uploadOffset{ 0 };        ///< 이번 펜스 구간에서 쓴 스테이징 바이트
            uint64                                            _resetFence{ UINT64_MAX }; ///< 마지막으로 얼로케이터를 Reset 한 펜스 구간
            uint8                                             _bListOpen{ SW_FALSE };    ///< 복사 리스트가 열려 있고 아직 제출하지 않은 복사가 있다.
        };
        /**
         * @brief 열어 둔 구조 버퍼 복사 리스트를 닫아 제출합니다. **프레임에 한 번**입니다.
         * @details 갱신 호출은 리스트를 열어 두고 기록만 하고, 여기서 한 번 닫는다. 갱신은 인스턴스, 배치 테이블, 뷰마다의
         *          간접 인자, 머티리얼 그룹까지 프레임에 대여섯 번이고 제출 하나가 수십µs 라, 호출마다 `ExecuteCommandLists` 를
         *          부르면 렌더 스레드 비용이 된다. 큐 순서가 곧 실행 순서라 프레임 리스트 **앞에** 넣으면 갱신이 그 프레임의
         *          드로우보다 먼저 실행된다. 텍스처 전송은 이 리스트를 거치지 않는다(`submitTransferJob`).
         * @param bExecuteNow true 면 지금 큐에 넣습니다(펜스 대기 직전). false 면 `_listPendingSubmit` 맨 앞에 끼워
         *                    endFrame 의 한 번의 제출에 같이 나갑니다.
         */
        void flushPendingUploads( bool bExecuteNow );

        /**
         * @struct D3D12TransferJob
         * @brief 텍스처 전송 하나의 커맨드 얼로케이터와 리스트입니다. 전송마다 자기 얼로케이터를 쥐고, 전송 펜스가 `_fenceValue` 에 닿으면 놀고 있는 목록으로 돌아가 다시 쓰인다.
         * @details 프레임 링 슬롯(`StructuredUploadSlot`)과 달리 얼로케이터를 다시 쓰려고 프레임 펜스를 기다리는 일이 없다 — 이 작업의 전송 펜스 값을 지난 것만 돌아온다.
         */
        struct D3D12TransferJob
        {
            Microsoft::WRL::ComPtr<ID3D12CommandAllocator>    _allocator;
            Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> _list;
            UINT64                                            _fenceValue{ 0 }; ///< 제출 뒤 전송 펜스에 Signal 한 값. 0 이면 아직 내지 않았다
        };

        /**
         * @struct D3D12StagingRing
         * @brief 영속 매핑한 업로드 힙 버퍼 하나와 그 구간 장부입니다. 텍스처 업로드가 소스를 여기에 복사한 뒤 복사 리스트가 이 버퍼에서 읽습니다.
         * @details 구간은 시리얼을 붙여(`markSubmitted`) 워터마크가 지나면 돌아온다. 링보다 큰 한 장은 장부가 단독 구간(`RHIStagingSpan::_bSolo`, 오프셋 0)으로 내주고,
         *          그 크기의 일회성 업로드 힙 버퍼(`_solo*`)를 만들어 쓴다. 장부가 단독 구간을 한 번에 하나만 내주므로 링마다 하나다. 장부가 돌려받은 뒤(`hasLiveSolo` 거짓)
         *          `retireCompletedTransfers` 가 지운다. 구간 시작은 `D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT`(512)의 배수라 텍스처 복사의 배치 오프셋 규칙을 그대로 만족한다.
         */
        struct D3D12StagingRing
        {
            RHIStagingAllocator                    _ledger;
            Microsoft::WRL::ComPtr<ID3D12Resource> _buffer;
            uint8*                                 _pMapped{ nullptr };
            mutex                                  _soloMutex; ///< 단독 버퍼 칸(아래 둘)을 지키는 잎 락. 만드는 쪽(제출 스레드)과 지우는 쪽(아무 스레드의 회수)이 다르다
            Microsoft::WRL::ComPtr<ID3D12Resource> _soloBuffer;
            uint8*                                 _pSoloMapped{ nullptr };

            /** @brief @p span 을 담은 버퍼입니다(단독 구간이면 단독 버퍼). 구간 안의 오프셋은 `span._offsetBytes` 에서 센다. */
            [[nodiscard]] ID3D12Resource* getSpanResource( const RHIStagingSpan& span ) const { return span._bSolo != SW_FALSE ? _soloBuffer.Get() : _buffer.Get(); }
            /** @brief @p span 을 담은 버퍼의 매핑 시작 주소입니다. 구간의 바이트는 여기에 `span._offsetBytes` 이상의 오프셋을 더해 읽고 쓴다. */
            [[nodiscard]] uint8* getSpanMappedBase( const RHIStagingSpan& span ) const { return span._bSolo != SW_FALSE ? _pSoloMapped : _pMapped; }
        };

        /**
         * @brief 전송 전용 스테이징 링(`RHISwapChainDesc::_stagingRingBytes`)을 만듭니다. 용량이 0 이면 오류를 남기고 false 입니다(기본값으로 바꾸지 않는다).
         * @param desc 업로드 링 용량과 링보다 큰 한 장의 상한(`_stagingSoloMaxBytes`)을 읽습니다.
         */
        [[nodiscard]] bool initializeTransfers( const RHISwapChainDesc& desc );
        /** @brief 남은 전송 작업, 전송을 기다리던 해제, 가져가지 않은 읽기, 링을 놓습니다. GPU 가 쉰 뒤(종료 2단계 뒤)에 부릅니다. */
        void shutdownTransfers();
        /** @brief 업로드 힙에 @p capacityBytes 의 버퍼를 만들어 영속 매핑하고 구간 장부를 초기화합니다. 실패하면 이유를 로그로 남기고 false 입니다. */
        [[nodiscard]] bool createStagingRing( D3D12StagingRing& ring, uint64 capacityBytes, uint64 soloMaxBytes );
        /** @brief 링 버퍼와 단독 버퍼를 놓고 장부를 비웁니다. 없는 링이면 아무것도 하지 않습니다. */
        void destroyStagingRing( D3D12StagingRing& ring );
        /** @brief 업로드 힙 버퍼를 만들어 영속 매핑하고 메모리 장부의 Staging 항목에 올립니다(링과 단독 버퍼가 같이 쓴다). 실패하면 두 출력은 비어 있다. */
        [[nodiscard]] bool createMappedStagingBuffer( uint64 sizeBytes, Microsoft::WRL::ComPtr<ID3D12Resource>& outBuffer, uint8*& pOutMapped );
        /** @brief `createMappedStagingBuffer` 로 만든 버퍼를 놓고 메모리 장부에서 내립니다. 비어 있으면 아무것도 하지 않습니다. */
        void destroyMappedStagingBuffer( Microsoft::WRL::ComPtr<ID3D12Resource>& rBuffer, uint8*& pMapped );
        /**
         * @brief @p ring 에서 구간을 잡습니다. 차 있으면 끝난 전송을 한 번 걷은 뒤 다시 봅니다. 기다리지 않습니다.
         * @details 할당 결과를 제출 결과로 옮기고 못 받은 구간을 놓고 이유를 남기는 곳은 `RHIStagingRingUtil::settleAllocation` 하나다(잃은 장치면 Full 도 Rejected). 단독 구간이면 그 크기의
         *          일회성 버퍼를 만들어 붙인다(앞선 단독 버퍼가 남아 있으면 먼저 지운다 — 장부가 새 단독 구간을 내줬다면 앞 구간은 이미 돌아왔다).
         *          Rejected 면 이유를 로그로 남기고 잡은 구간은 놓는다. 어느 스레드에서나, 전송 제출 락 밖에서 부른다.
         */
        [[nodiscard]] RHISubmitResult allocateStagingSpan( D3D12StagingRing& ring, uint64 sizeBytes, const utf8* pCaller, RHIStagingSpan& outSpan );
        /** @brief 장부가 단독 구간을 돌려받았으면(`hasLiveSolo` 거짓) @p ring 의 단독 버퍼를 지웁니다. `retireCompletedTransfers` 가 부릅니다. */
        void destroyIdleSoloBuffer( D3D12StagingRing& ring );
        /**
         * @brief 놀고 있는 전송 작업을 꺼내(없으면 만들어) 얼로케이터와 리스트를 열어 줍니다. 어느 스레드에서나, 락 없이 부른다(작업은 호출자 것이다).
         * @return 열었으면 true. 만들거나 Reset 하지 못했으면 이유를 로그로 남기고 false 입니다.
         */
        [[nodiscard]] bool beginTransferJob( D3D12TransferJob& outJob );
        /**
         * @brief 리스트를 닫아 즉시 큐에 내고 전송 펜스를 올린 뒤 시리얼을 매깁니다. 완료를 기다리지 않습니다(계약 2).
         * @details 큐 제출 · 펜스 올리기 · 시리얼 발급을 `_transferSubmitMutex` 한 락 안에서 한다(계약 5: 락 밖에서 매기면 시리얼 순서가 큐 순서와 어긋나 워터마크가 끝나지 않은 제출을 덮는다).
         *          이 락은 프레임 제출(`_uploadSlotMutex`)과 따로라 전송이 프레임 제출을 막지 않는다. 큐 객체는 free-threaded 라 두 락의 제출은 서로 순서만 갖는다.
         * @return 시리얼입니다. 내지 못했으면(장치 손실, Close · Signal 실패) 이유를 로그로 남기고 0 이며 작업은 돌려놓습니다. 어느 스레드에서나 부릅니다.
         */
        [[nodiscard]] uint64 submitTransferJob( D3D12TransferJob job, const utf8* pWhere );
        /** @brief 전송 펜스를 지난(또는 내지 못한) 작업을 놀고 있는 작업 목록으로 돌려놓습니다. */
        void recycleTransferJob( D3D12TransferJob&& job );
        /**
         * @brief 전송 펜스가 지난 작업을 앞에서부터 걷어 워터마크를 올리고, 전송을 기다리던 해제를 풀어 줍니다. 기다리지 않습니다. 어느 스레드에서나 부릅니다.
         * @details `pollTransfer` · 링이 찬 제출 · 프레임 끝(`signalCurrentFrame`) · `waitIdleInternal` 이 부른다. 잃은 장치의 펜스는 묻지 않는다(끝난 값을 돌려줘 잃은 전송이 `Complete` 로 보인다).
         */
        void retireCompletedTransfers();
        /**
         * @brief 전송 중인 텍스처의 해제를 마지막 전송이 끝난 뒤로 미룹니다(계약 6). 끝나면 그때의 프레임 펜스 뒤로 `_releaseQueue` 에 넘어가 이 텍스처를 그린 프레임도 지나게 한다.
         * @param lastSerial 이 텍스처에 마지막으로 제출한 전송의 시리얼입니다(끝나지 않은 것).
         */
        void releaseTextureAfterTransfer( Microsoft::WRL::ComPtr<ID3D12Resource> owned, RHITextureHandle texture, uint64 lastSerial );
        /**
         * @brief 런타임에 장치 제거 이유(`GetDeviceRemovedReason`)를 묻고, 제거됐으면 전송 손실로 표시합니다(`RHITransferSerials::markLost`). 어느 스레드에서나 부릅니다.
         * @details DXGI_ERROR_DEVICE_REMOVED · DEVICE_RESET · DEVICE_HUNG 등 제거 이유가 실패인 모든 경우가 이 한 곳으로 표시된다 — 호출 결과를 보는 자리마다 따로 표시하면 빠뜨린 자리의 티켓이
         *          손실 뒤에도 `Pending` 으로 남는다. 장치가 없으면 false 입니다.
         */
        bool isDeviceRemoved();
        /** @brief 장치를 잃었으면 true 입니다(이미 표시했거나 `isDeviceRemoved`). 전송 제출 함수가 진입에서 보고 `Rejected` 를 돌려줍니다(계약 2). */
        bool isDeviceLost();
        /** @brief 이 텍스처에 마지막으로 제출한 전송(업로드 또는 읽기 원본)의 시리얼을 기록합니다(bindless 등록 보호와 파괴 지연이 읽는다). */
        void noteTextureTransfer( RHITextureHandle texture, uint64 serial );
        /** @brief 이 텍스처에 마지막으로 제출한 전송의 시리얼입니다. 없으면 0 입니다. */
        [[nodiscard]] uint64 findTextureTransferSerial( RHITextureHandle texture );
        /** @brief 텍스처를 파괴할 때 전송 시리얼 칸을 지웁니다. */
        void eraseTextureTransfer( RHITextureHandle texture );
        /**
         * @brief 즉시 제출 모드(`setImmediateSubmit`)면 모아 둔 리스트를 지금 큐에 넣습니다. 그 앞에 여기까지 기록된 업로드 복사를 끼웁니다.
         * @details 즉시 모드가 아니면 아무것도 하지 않는다(endFrame 의 한 번의 제출에 같이 나간다). 잘라 담은 순서 그대로 내보내므로 실행
         *          순서는 같고 제출 시점만 앞당긴다. `executeCommandList` 의 두 분기(빈 조각과 자른 조각)가 함께 쓴다.
         */
        void submitPendingIfImmediate();
        /// @brief 구조 버퍼 복사 리스트(`StructuredUploadSlot`)의 기록과, 프레임 리스트 제출 · 프레임 펜스 Signal 과 값 올리기를 지킵니다. 열어 두는 동안 잠급니다.
        /// @details 텍스처 전송은 이 락을 잡지 않습니다(`_transferSubmitMutex`). 그래서 큰 텍스처의 소스 복사 · 풋프린트 계산 · 리스트 기록이 렌더 스레드의 프레임 제출과 겨루지 않는다.
        mutex _uploadSlotMutex;

        Microsoft::WRL::ComPtr<ID3D12Device>              _device;
        Microsoft::WRL::ComPtr<IDXGIAdapter3>             _memoryAdapter; ///< 디바이스가 쓰는 어댑터(LUID 로 찾는다). GPU 메모리 사용량과 예산을 질의하는 데 쓴다.
        Microsoft::WRL::ComPtr<ID3D12CommandQueue>        _commandQueue;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>      _rtvHeap;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>      _dsvHeap;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>      _cbvHeap;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>      _offlineViewHeap; ///< CPU 전용 뷰 힙. 슬롯 테이블 CopyDescriptors 의 원본과 null 뷰를 담는다.
        Microsoft::WRL::ComPtr<ID3D12RootSignature>       _rootSignature;   ///< 그래픽스와 컴퓨트가 같이 쓴다(같은 블롭).
        Microsoft::WRL::ComPtr<ID3D12Resource>            _vertexBuffer;    ///< 풀스크린 포스트용 정점 버퍼(정점 3개)
        Microsoft::WRL::ComPtr<ID3D12CommandSignature>    _drawCommandSignature;
        Microsoft::WRL::ComPtr<ID3D12CommandSignature>    _drawIndexedCommandSignature;
        Microsoft::WRL::ComPtr<ID3D12CommandSignature>    _dispatchCommandSignature;
        Microsoft::WRL::ComPtr<ID3D12CommandAllocator>    _arrCommandAllocator[constant::kMaxFrameCountInFlight];
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> _commandList;

        /// @brief 이번 프레임에 큐로 넘길 커맨드 리스트들입니다. 기록 순서가 곧 실행 순서입니다.
        vector<ID3D12CommandList*> _listPendingSubmit;
        /// @brief 프레임 스트림을 자를 때마다 풀에서 빌린 추가 세그먼트들입니다(프레임 끝에 반납합니다).
        vector<D3D12CommandListEntry> _listFrameSegment;
        /// @brief 지금 기록 중인 프레임 세그먼트입니다. beginFrame 이 _commandList 로 시작합니다.
        ID3D12GraphicsCommandList* _pActiveFrameList;
        /// @brief 병렬 기록용 리스트와 얼로케이터의 재사용 풀입니다. 태스크 스레드가 동시에 빌려 가므로 잠급니다.
        mutex                                               _cmdListPoolMutex;
        vector<D3D12CommandListEntry> _listFreeCmdListEntry SW_GUARDED_BY( _cmdListPoolMutex );

        /** @brief 살아 있는 커맨드 리스트입니다(소유하지 않습니다). 종료할 때 연결을 끊어 줍니다. */
        mutex                        _liveCmdListMutex;
        vector<D3D12RHICommandList*> _listLiveCmd;
        /// @brief 온라인 힙 블록 프리리스트입니다. 컨텍스트가 빌려 슬롯 테이블을 확정하고, 리스트가 닫히면 펜스 뒤에 돌아옵니다.
        mutex                               _onlineBlockMutex;
        vector<uint32> _listFreeOnlineBlock SW_GUARDED_BY( _onlineBlockMutex );
        /** @brief 펜스를 기다리는 온라인 블록 묶음입니다. 리스트가 닫힐 때 상태의 블록 벡터를 통째로 옮겨 옵니다. */
        struct OnlineBlockRecycleBatch
        {
            uint64         _fence{ 0 };
            vector<uint32> _listBlock;
        };
        vector<OnlineBlockRecycleBatch> _listPendingOnlineRecycle SW_GUARDED_BY( _onlineBlockMutex );
        vector<vector<uint32>> _listOnlineRecyclePool             SW_GUARDED_BY( _onlineBlockMutex ); ///< 돌아온 빈 벡터이며 용량을 남겨 둔다.
        FrameResourceRing                                         _frameRing;

        /**
         * @brief GPU 타임스탬프입니다. 링 슬롯마다 `constant::kMaxGPUTimestampSlot` 개의 슬롯을 씁니다.
         * @details 읽기는 `waitForRingSlot()` 이 그 슬롯의 펜스를 통과시킨 **직후**에 한다. 그때가
         *          "그 프레임의 GPU 작업이 끝났음" 이 이미 보장된 유일한 지점이라, 측정하려고 파이프라인을
         *          멈춰 세우는 일이 없다.
         */
        Microsoft::WRL::ComPtr<ID3D12QueryHeap> _timestampHeap;
        Microsoft::WRL::ComPtr<ID3D12Resource>  _timestampReadback;
        uint64                                  _timestampFrequency;
        /// @brief 이번 프레임에 실제로 기록된 슬롯 비트입니다. 패스가 병렬로 기록하므로 atomic 입니다.
        atomic<GPUTimestampSlotMask> _timestampWrittenMask;
        /// @brief 링 슬롯별로 확정한 비트입니다. 그 슬롯이 다시 돌아왔을 때 어느 슬롯이 진짜 값인지 가립니다.
        GPUTimestampSlotMask _arrTimestampMask[constant::kMaxFrameCountInFlight];
        RHITimestampFrame    _timestampFrame; ///< 마지막으로 읽은 프레임(`readTimestamps`)
        StructuredUploadSlot _arrStructuredUploadSlot[constant::kMaxFrameCountInFlight];

        RHIHandleTable<Microsoft::WRL::ComPtr<ID3D12Resource>> _gpuBuffers;
        RHIHandleTable<Microsoft::WRL::ComPtr<ID3D12Resource>> _gpuTextures;
        /// @brief 리소스 상태 전이 맵을 보호합니다. 여러 커맨드 리스트가 동시에 같은 자원을 전이할 수 있기 때문입니다.
        mutex                                                 _resourceStateMutex;
        unordered_map<RHIBufferHandle, D3D12_RESOURCE_STATES> _mapStructuredBufferState;
        /// @brief 구조 버퍼 핸들에서 원소 stride 를 찾는 맵입니다(bindless StructuredBuffer SRV, UAV 생성용). `_bindlessMutex` 가 보호합니다(등록이 그 배타 락 안에서 읽습니다).
        unordered_map<RHIBufferHandle, uint32> _mapStructuredStride;

        unordered_map<RHITextureHandle, OffscreenTextureRecord> _mapOffscreenTexture;
        uint32                                                  _nextOffscreenRtvIndex;
        uint32                                                  _nextOffscreenDsvIndex;
        vector<uint32>                                          _listFreeOffscreenRtvIndex;
        vector<uint32>                                          _listFreeOffscreenDsvIndex;
        unordered_map<RHIBufferHandle, uint32>                  _mapCbAlignedSize;
        unordered_map<RHIBufferHandle, void*>                   _mapCbMapped;
        RHIConstantBufferMirror _constantBufferMirror           SW_GUARDED_BY( _bindlessMutex ); ///< 한 번 쓴 상수 버퍼를 나머지 링 슬롯에도 채운다.

        RHIHandleTable<D3D12PipelineStateRecord> _pipelineStates;
        RHIRenderPassLedger                      _renderPassLedger; ///< 렌더 패스 핸들 장부. 자기 뮤텍스로 직렬화해 어느 스레드에서나 부른다.

        /// @brief 창 하나의 백버퍼 묶음입니다. 백버퍼, 인덱스, 리소스 상태, Present 가 모두 여기 모여 있습니다.
        D3D12RHISwapChain _swapChain;

        uint8                  _bBindlessRootSignature : 1; ///< 루트 시그니처(텍스처 배열 테이블 포함)를 만드는 데 성공했다. 네이티브 bindless 텍스처 샘플링이 가능하다.
        [[maybe_unused]] uint8 _reservedPassFlags      : 7;
        /// @brief 디바이스 제거(DEVICE_HUNG/REMOVED) 상태를 이미 한 번 로그로 남겼으면 true 입니다. 매
        /// 프레임 flushDebugMessages() 가 똑같은 검증 메시지 수십 줄을 끝없이 반복 출력하는 것을 막습니다.
        /// 렌더 스레드 · 기록 워커(리스트 생성) · PSO 생성이 함께 읽고 쓰므로 atomic 입니다(비트필드면 옆 비트와 한 바이트를 고쳐 쓴다).
        atomic<uint8> _bDeviceRemovedLogged;
        /// @brief blitTexture 포맷이나 크기 불일치 경고를 한 번만 남기기 위한 래치입니다. 병렬 기록 워커가 함께 쓰므로 atomic 입니다.
        atomic<uint8>                     _bBlitMismatchLogged;
        uint8 _bOnlineHeapExhaustedLogged SW_GUARDED_BY( _onlineBlockMutex ); ///< 온라인 힙이 바닥났다는 경고를 한 번만 남기기 위한 래치입니다.
        /// @brief 엔진이 켜기 전에는 힙도 만들지 않습니다. 계측에는 비용이 듭니다. `setTimestampEnabled` 는 어느 스레드에서나 부를 수 있어 atomic 입니다.
        atomic<uint8>  _bTimestampEnabled;
        atomic<uint32> _validationMessageCount;   ///< 디버그 레이어가 알린 경고 이상의 메시지 수(`onValidationMessage`)
        DWORD          _validationCallbackCookie; ///< `RegisterMessageCallback` 이 준 번호입니다. 0 이면 등록하지 않았다.

        /// @brief 디바이스 프레임 스트림(스왑체인 begin/endFrame 과 백버퍼 패스)의 기록 상태입니다.
        /// @details RenderThread 가 백버퍼 렌더 패스(beginRenderPass 핸들 0)를 여는 정식 경로입니다. 패스별
        ///          D3D12RHICommandList 와는 다른 스트림이라는 뜻일 뿐입니다.
        D3D12RecordingState _frameStreamState;

        /// @brief bindless 레지스트리, 프리리스트, 디스크립터 카운터를 보호합니다. 구조를 바꾸는 register/unregister 와
        /// 파괴만 배타 잠금을 잡습니다. 기록 중의 읽기(컨텍스트의 resolveBufferAddress, resolveOfflineView)는 잠그지 않습니다.
        /// 기록 중에는 레지스트리가 바뀌지 않는다는 규칙(IRHIDevice::setParallelRecording, assertRegistryMutableNow)이
        /// 그것을 보장합니다. 주의: 기록 중에 레지스트리를 락 없이 순회하는 동안 재할당이 일어나면 GPU 가 쓰레기 디스크립터를
        /// 읽어 PageFault(VA=0)가 나고 DEVICE_HUNG 으로 이어집니다.
        mutable shared_mutex           _bindlessMutex;
        vector<BindlessResourceRecord> _listRegisteredBindless;
        vector<uint32>                 _listFreeBindless;

        /// @brief UAV 레지스트리입니다. 인덱스는 SRV/CBV 와 **같은 힙 인덱스 공간**(_listFreeBindless 공유)입니다. 셰이더가
        ///        RWStructuredBuffer<T> name[] 을 그 인덱스로 고르므로 힙 슬롯 번호와 같아야 합니다.
        vector<BindlessResourceRecord> _listRegisteredUAV;

        UINT _rtvDescriptorSize;
        UINT _cbvDescriptorSize;
        UINT _allocatedDescriptorsCount;
        /// @brief 지금까지 새로 만든 (리스트, 얼로케이터) 쌍 수입니다. 풀이 끝없이 늘어나는지 확인하기 위한 계측입니다.
        atomic<uint32> _cmdListEntryCreated;

        Microsoft::WRL::ComPtr<ID3D12Fence> _fence;
        /// @brief 이번 프레임이 Signal 할 프레임 펜스 값입니다. 렌더 스레드가 Signal 과 함께 올리고, 다른 스레드(구조 버퍼 갱신 · 해제)가 읽으므로 atomic 입니다.
        /// @details Signal 과 올리기는 `_uploadSlotMutex` 안에서 한 덩어리로 한다. 구조 버퍼 갱신이 같은 락 안에서 이 값을 읽고 복사를 실행하므로
        ///          "복사가 끝나는 펜스" 가 Signal 순서와 어긋나지 않는다(락 밖이면 Signal(V) 뒤에 실행된 복사가 V 를 받는다).
        ///          텍스처 전송은 이 값에 기대지 않는다 — 전송 완료는 전송 펜스와 시리얼 워터마크가, 전송 중인 텍스처의 해제는 시리얼 키 해제 큐(`_transferReleaseQueue`)가 정한다.
        atomic<uint64> _fenceValue;

        /// @brief 읽기 하나가 들고 있는 것입니다. 읽기 힙 버퍼와 그 안의 배치, 결과를 꺼낼 때 필요한 배치입니다(`takeTextureReadback` 이 Map 해서 읽습니다).
        struct TextureReadbackRecord
        {
            Microsoft::WRL::ComPtr<ID3D12Resource> _readback;
            D3D12_PLACED_SUBRESOURCE_FOOTPRINT     _footprint{};
            RHITextureMipSpan                      _layout{};
            UINT                                   _rowCount{ 0 };
            UINT64                                 _rowSize{ 0 };
            uint8                                  _bDepth{ SW_FALSE };
        };
        /// @brief 전송 티켓의 시리얼 발급기와 완료 워터마크입니다. 시리얼은 큐 제출 락 안에서 `_transferFenceQueue` 가 매깁니다.
        RHITransferSerials _transferSerials;
        /// @brief 전송 전용 펜스입니다. 프레임 펜스와 따로 두어 전송 완료를 프레임 없이도 알 수 있습니다. 전송마다 한 값을 올립니다(`submitTransferJob`).
        Microsoft::WRL::ComPtr<ID3D12Fence> _transferFence;
        UINT64                              _transferFenceValue; ///< 마지막으로 올린 전송 펜스 값(`_transferSubmitMutex` 안에서만 올린다)
        /**
         * @brief 전송의 큐 제출 · 펜스 올리기 · 시리얼 발급을 직렬화합니다(계약 5). 프레임 제출 락(`_uploadSlotMutex`)과 서로 잡지 않습니다.
         * @details 잡는 순서는 `_transferSubmitMutex` → `_transferFenceQueue` 의 잎 락 하나입니다. 링 할당, 소스 복사, 리스트 기록은 이 락 밖에서 합니다.
         */
        mutex _transferSubmitMutex;
        /// @brief 제출 순서대로 쌓인 전송 작업입니다. 전송 펜스가 앞에서부터 연속으로 지난 만큼 워터마크를 올립니다(`retireCompletedTransfers`).
        RHITransferFenceQueue<D3D12TransferJob> _transferFenceQueue;
        /// @brief 업로드 스테이징 링입니다(`gv_rhiStagingRingMegabytes`).
        D3D12StagingRing _uploadRing;
        /// @brief 놀고 있는 전송 작업 목록입니다. 펜스 줄의 회수 콜백 안에서도 잡는 잎 락(`_transferJobMutex`)이 지킵니다.
        mutex                                         _transferJobMutex;
        vector<D3D12TransferJob> _listFreeTransferJob SW_GUARDED_BY( _transferJobMutex );
        /// @brief 텍스처 핸들에서 마지막 전송(업로드 또는 읽기 원본)의 시리얼을 찾는 표를 지킵니다(DX12 텍스처는 레코드 없이 리소스 포인터만 들고 있다).
        mutex _textureTransferMutex;
        /// @brief 텍스처 핸들에서 마지막 전송의 시리얼을 찾는 표입니다. `_textureTransferMutex` 가 지킵니다.
        unordered_map<RHITextureHandle, uint64> _mapTextureTransferSerial SW_GUARDED_BY( _textureTransferMutex );
        /// @brief 아직 가져가지 않은 텍스처 읽기입니다(시리얼로 찾습니다).
        RHIPendingReadbackTable<TextureReadbackRecord> _readbackTable;

        RHIReleaseQueue _releaseQueue;
        /**
         * @brief 끝나지 않은 전송이 쓰는 텍스처의 해제와 읽기 버퍼 유지입니다. 키는 전송 시리얼이고, 워터마크가 지나면 해제는 그때의 프레임 펜스 뒤로 `_releaseQueue` 에 넘어갑니다(계약 6).
         * @details `_releaseQueue` 보다 **뒤에** 선언합니다. 소멸은 선언 역순이라 이 큐가 먼저 비워지고, 그 콜백이 아직 살아 있는 `_releaseQueue` 에 넣습니다.
         */
        RHIReleaseQueue _transferReleaseQueue;

        /** @brief 프레임 스트림 컨텍스트입니다. 백버퍼 패스와 Present 가 여기에 기록합니다. */
        sw::unique_ptr<D3D12RHICommandContext> _frameStreamContext;
        /** @brief 리소스 생성과 파괴 구현입니다(getResourceFactory 가 반환하는 것). */
        sw::unique_ptr<D3D12RHIResourceFactory> _resourceImpl;
    };
} // namespace sw

#endif
