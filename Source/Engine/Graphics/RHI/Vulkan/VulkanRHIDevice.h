/**
 * @file VulkanRHIDevice.h
 * @brief Vulkan 1.2 RHI 백엔드 디바이스입니다.
 */
#pragma once
#include "Core/Concurrency/mutex.h"
#include "Core/Memory/Memory.h"

#include "Engine/Config/RHIBackendType.h"
#include "Engine/EngineMinimal.h"
#include "Engine/Graphics/RHI/Backend/RHIBindlessRegistry.h"
#include "Engine/Graphics/RHI/Backend/RHIConstantBufferMirror.h"
#include "Engine/Graphics/RHI/Backend/RHIFrameFenceRing.h"
#include "Engine/Graphics/RHI/Backend/RHIHandleTable.h"
#include "Engine/Graphics/RHI/Backend/RHIReleaseQueue.h"
#include "Engine/Graphics/RHI/Backend/RHISwapChainSet.h"
#include "Engine/Graphics/RHI/Backend/RHITimestampRing.h"
#include "Engine/Graphics/RHI/Backend/RHITrackedState.h"
#include "Engine/Graphics/RHI/Diagnostics/RHITimestampFrame.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHITimestampProvider.h"
#include "Engine/Graphics/RHI/RHIBackendInfo.h"
#include "Engine/Graphics/RHI/RHIGeometryBindings.h"
#include "Engine/Graphics/RHI/Transfer/RHIStagingAllocator.h"
#include "Engine/Graphics/RHI/Transfer/RHITransferSerials.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHIHandle.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHIRenderPassCache.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHISwapChain.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"

#include <shared_mutex>

namespace sw
{
    struct RHIMemoryBudget;

    class VulkanRHICommandContext;
    class VulkanRHIResourceFactory;

    /**
     * @struct VulkanDescriptorPoolSet
     * @brief 커맨드 버퍼 하나가 슬롯 세트(set 0)를 할당받는 디스크립터 풀 묶음입니다. 언리얼의 `FVulkanDescriptorPoolSetContainer` 에 해당합니다.
     * @details VkDescriptorPool 은 외부 동기화 대상이라, 여러 리스트가 여러 스레드에서 동시에 기록하려면 **풀도 리스트마다
     *          따로** 있어야 한다(커맨드 풀과 같은 제약). 디바이스 전역 풀을 뮤텍스로 잠그면 바인딩이 바뀌는 드로우마다
     *          락이 걸려 레벨 병렬 기록이 직렬화된다.
     *          그래서 커맨드 버퍼 쌍(VulkanCommandListEntry)이 자기 풀 묶음을 가지고 다니고, 그 버퍼가 GPU 펜스를 통과해
     *          재사용 풀로 돌아온 뒤에야 통째로 리셋된다. 락이 없다. 디바이스 프레임 스트림은 링 슬롯마다 하나를 쓴다.
     *          풀 하나가 차면 다음 풀을 만든다(상한 없음 — 한 번의 기록이 쓴 세트 수만큼 자란다. 만들지 못하면 멈춘다).
     */
    struct VulkanDescriptorPoolSet
    {
        vector<VkDescriptorPool> _listPool;
        uint32                   _cursor{ 0 }; ///< 지금 할당 중인 풀
    };

    /**
     * @enum VulkanBreadcrumbMode
     * @brief GPU 브레드크럼을 남기는 확장입니다. 장치를 만들 때 확장 목록(능력)이 한 번 정하고 그 뒤로 바뀌지 않는다.
     * @details NV 체크포인트가 있으면 그것을(단계마다 마지막 값을 드라이버가 돌려준다), 없고 `VK_AMD_buffer_marker` 가 있으면 호스트 보임 버퍼 두 칸에 표지를 쓴다
     *          (DX12 `WriteBufferImmediate` 와 같은 모양 — [0] 시작 표지 TOP_OF_PIPE, [1] 끝 표지 BOTTOM_OF_PIPE). 둘 다 없으면 None 이고 CPU 가 마지막으로 기록한 패스만 보고한다.
     *          언리얼 Vulkan RHI 도 같은 순서로 고른다(NV 체크포인트 → AMD 버퍼 마커, 기억에 의존한 비교).
     */
    enum class VulkanBreadcrumbMode : uint8
    {
        None,
        NVCheckpoints,   ///< VK_NV_device_diagnostic_checkpoints
        AMDBufferMarker, ///< VK_AMD_buffer_marker — AMD · 인텔 Mesa 드라이버가 낸다
    };
} // namespace sw

namespace sw
{
    /**
     * @struct VulkanCommandListEntry
     * @brief `VulkanRHICommandList` 가 빌려 쓰는 커맨드 풀과 커맨드 버퍼 쌍입니다.
     * @details `VkCommandPool` 은 외부 동기화 대상이라 여러 스레드가 동시에 기록하려면 **리스트마다
     *          전용 풀**이 있어야 한다. 다 쓴 쌍은 GPU 펜스를 통과한 뒤에 풀로 돌아간다.
     */
    struct VulkanCommandListEntry
    {
        VkCommandPool   _pool{ nullptr };
        VkCommandBuffer _buffer{ nullptr };
        /// @brief 이 버퍼 전용 슬롯 세트 풀 묶음입니다. 디바이스가 소유하고(_listCmdListDescriptorPoolSet) 쌍과 함께 빌려 줍니다.
        VulkanDescriptorPoolSet* _pDescriptorPoolSet{ nullptr };
    };
} // namespace sw

namespace sw
{
    /// @brief 슬롯 세트의 원소 하나입니다. 어떤 버퍼의 어느 구간이 바인딩되어 있는지 나타냅니다.
    struct VulkanSlotBinding
    {
        VkBuffer _buffer{ nullptr };
        uint64   _offset{ 0 };
        uint64   _range{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @struct VulkanSlotState
     * @brief 바인드 포인트(그래픽스, 컴퓨트) 하나의 슬롯 세트 상태입니다. 언리얼 Vulkan RHI 의 파이프라인별 디스크립터 상태에 해당합니다.
     * @details PSO 를 바인딩할 때 비워지고(이전 패스의 t/u 슬롯이 다음 세트로 새지 않도록), 드로우나 디스패치 직전에 flushSlotSet 이
     *          바뀐 것을 세트로 확정한다.
     */
    struct VulkanSlotState
    {
        /// @brief binding(종류별 시프트 + 레지스터)마다 바인딩된 버퍼입니다.
        VulkanSlotBinding _arrSlot[shaderslot::vk::kSlotBindingCount]{};
        /// @brief _arrSlot 중 바인딩된 슬롯의 비트입니다(binding 번호가 비트 위치입니다).
        uint64 _slotSetMask{ 0 };
        /// @brief 마지막으로 확정한 세트 이후 바뀌었는지 여부입니다.
        uint8 _bDirty{ SW_TRUE };
    };
} // namespace sw

namespace sw
{
    /**
     * @struct VulkanRecordingState
     * @brief "지금 이 커맨드 버퍼에 무엇이 바인딩되어 있나" 를 나타냅니다. 커맨드 버퍼(기록 스트림)마다 있어야 하는 상태입니다.
     * @details 디바이스에 두면 여러 리스트가 동시에 기록할 때 서로의 바인딩 캐시를 덮어쓴다.
     *          DX12 의 `D3D12RecordingState` 와 같은 역할이며, "기록 상태는 리스트가 소유하고 디바이스는
     *          진짜 전역 자원만 가진다" 는 구조다.
     */
    struct VulkanRecordingState
    {
        RHIPipelineStateHandle _activeGraphicsPSO;
        RHIPipelineStateHandle _activeComputePSO; ///< `setComputePipelineState` 가 마지막으로 받은 핸들. 디스패치는 이 핸들의 레코드가 없으면 멈춘다(드로우의 그래픽스 PSO 와 같은 계약).

        RHIGeometryBindings _geometry; ///< setVertexBuffer · setIndexBuffer 로 건 정점 슬롯과 인덱스 버퍼(드로우 때 건다 — RHIGeometryBindings)
        /// @brief 이 스트림에 렌더 패스가 열려 있는지 여부입니다.
        uint8 _bRenderPassActive : 1;
        /// @brief 텍스처 배열 세트(set 1)를 이 버퍼의 두 바인드 포인트에 이미 바인딩했는지 여부입니다.
        uint8                  _bTextureSetBound : 1;
        [[maybe_unused]] uint8 _reserved         : 6;

        /// @brief 바인드 포인트별 슬롯 세트 상태입니다([0] 그래픽스, [1] 컴퓨트). 서로 독립이라 디스패치가 드로우의 바인딩을 지우지 않습니다.
        VulkanSlotState _arrSlotState[2];

        /** @brief 아무것도 바인딩되지 않은 상태로 시작합니다. */
        VulkanRecordingState()
            : _activeGraphicsPSO{ 0 }
            , _activeComputePSO{ 0 }
            , _geometry{}
            , _bRenderPassActive{ SW_FALSE }
            , _bTextureSetBound{ SW_FALSE }
            , _reserved{ 0 }
            , _arrSlotState{}
        {
        }
    };
} // namespace sw

namespace sw
{
    class VulkanRHICommandList;

    /**
     * @class VulkanRHIDevice
     * @brief Vulkan 1.2 그래픽스와 컴퓨트 디바이스 구현입니다(Descriptor Indexing, bindless).
     * @details 선택 능력(타임스탬프)은 상속하지 않는다. 디바이스가 가진 어댑터 멤버가 구현해 이 디바이스로 넘기고,
     *          `findTimestampProvider` 가 그 멤버를 준다(언리얼 RHI 도 선택 기능을 디바이스의 기반 클래스로 늘리지 않는다).
     */
    class VulkanRHIDevice : public IRHIDevice
    {
        friend class VulkanRHICommandContext;
        friend class VulkanRHICommandList;

    public:
        friend class VulkanRHIResourceFactory;
        /** @brief 빈 Vulkan 디바이스를 만듭니다. */
        /** @brief @p deviceGeneration 은 등록표가 붙인 이 장치의 세대입니다(전송 티켓에 싣는다). */
        explicit VulkanRHIDevice( uint32 deviceGeneration );
        /** @brief 인스턴스, 디바이스, 스왑체인을 정리합니다. */
        virtual ~VulkanRHIDevice() override;

        /** @brief Vulkan 인스턴스, 서피스, 디바이스, 스왑체인, 커맨드 풀, 동기화 객체를 만듭니다. */
        [[nodiscard]] bool initializeInternal( const RHISwapChainDesc& desc ) override;

        /** @brief Vulkan 파이프라인과 자원을 해제합니다. */
        void shutdownInternal() override;

        /** @brief 종료 3단계입니다. 프레임 스트림 컨텍스트를 해제하고 살아 있는 커맨드 리스트를 분리합니다. */
        void detachCommandRecordingInternal() override;

        /** @brief GPU 가 끝날 때까지 기다리고(vkDeviceWaitIdle) 지연 해제 큐를 비웁니다. */
        void waitIdleInternal() override;

        /**
         * @brief `VK_EXT_memory_budget` 으로 이 프로세스의 힙별 사용량과 예산을 채웁니다. 사용량은 모든 힙의 합이고, 예산은 DEVICE_LOCAL 힙의 합입니다.
         * @details 디바이스가 확장을 지원하지 않으면 false 입니다(드라이버 값은 "모름").
         */
        [[nodiscard]] bool queryGPUMemoryBudgetInternal( RHIMemoryBudget& outBudget ) override;

        /** @brief 기록 중인 프레임의 시리얼(`_frameFenceRing.getRecordingSerial()`)로 해제 큐에 넣습니다. */
        void   enqueueGPURelease( const RHIResourceReleaseDelegate& releaseDelegate ) override;
        uint32 getPendingReleaseCount() const override { return _releaseQueue.getPendingReleaseCount(); }
        /**
         * @brief 장치를 잃은 상태로 둡니다(개발 명령 `rhi.removeDevice`). Vulkan 에는 장치를 제거하는 API 가 없어, 큐 호출이 `VK_ERROR_DEVICE_LOST` 를 처음 돌려준
         *        뒤와 같은 상태(`isDeviceLost`)를 만든다. 그 뒤로 제출 · 대기 · 표시를 하지 않고 프레임을 건너뛰며, 종료는 기다리지 않고 끝난다.
         *        손실 뒤에는 펜스 · 큐 · 장치 대기를 부르지 않는다 — 사양은 손실 뒤 대기가 유한 시간에 돌아온다고 하지만 TDR 중의 드라이버 안 대기는 그 보장 밖이다.
         */
        bool debugRemoveDevice() override;
        /** @brief 장치 손실 표시를 든 전송 시리얼입니다(`IRHIDevice::isDeviceLost` 가 읽는다). */
        const RHITransferSerials& getTransferSerials() const override { return _transferSerials; }
        uint32                    readLastBreadcrumbs( uint32* pOutID, uint32 capacity ) override;

        /** @brief 크기를 기록하고, 스왑체인 재생성은 beginFrame 까지 미룹니다. */
        void                         resizeInternal( uint32 width, uint32 height ) override;
        [[nodiscard]] IRHISwapChain* createSwapChainInternal( const RHISwapChainDesc& desc ) override;
        void                         resizeSwapChainInternal( IRHISwapChain& swapChain, uint32 width, uint32 height ) override;
        void                         destroySwapChainInternal( IRHISwapChain& swapChain ) override;
        [[nodiscard]] bool           acquireSwapChainImageInternal( IRHISwapChain& swapChain ) override { return useSwapChain( &swapChain ) != nullptr; }
        /** @brief present 모드를 새 VSync 값으로 바꾸도록 스왑체인을 다음 `beginFrame` 에서 다시 만들게 표시합니다. */
        void               applyVSyncInternal() override;
        [[nodiscard]] bool createRenderPass();
        /** @brief 프레임을 엽니다(펜스 대기, 스왑체인 이미지 획득, 커맨드 버퍼 기록 시작). */
        void beginFrameInternal() override;

        /** @brief 프레임을 닫습니다(vkQueueSubmit, bPresent 면 vkQueuePresentKHR). */
        void endFrameInternal( bool vsync, bool bPresent ) override;

        void               setTimestampEnabled( bool bEnabled ) { _bTimestampEnabled.store( bEnabled ? SW_TRUE : SW_FALSE, std::memory_order_relaxed ); }
        uint32             getTimestampSlotCount() const;
        [[nodiscard]] bool readTimestamps( RHITimestampFrame& outFrame );
        [[nodiscard]] bool readGPUClockNanos( int64& outGPUNanos );
        bool               isGPUClockReadCheap() const { return false; }
        /**
         * @brief 컴퓨트 큐 타임스탬프가 그래픽스 큐와 같은 시계인지입니다. 비동기 컴퓨트 큐가 그래픽스 큐와 같은 큐 가족이라(`createAsyncComputeCommandList`)
         *        `timestampValidBits` · `timestampPeriod` 를 함께 쓰고, 그 가족의 유효 비트는 풀을 만들 때 봤다(`ensureTimestampPool`). 리셋 순서는 `submitAsyncCompute` 주석.
         */
        bool isComputeQueueTimestampComparable() const;

        /** @brief 타임스탬프 쿼리 풀입니다. 준비되지 않았으면 VK_NULL_HANDLE 입니다. */
        VkQueryPool getTimestampPool() const
        {
            _renderThread.assumeHeldByJoinedTask(); // 커맨드 리스트 기록(병렬 패스 태스크 포함)이 부른다 — 렌더 스레드가 합류를 기다린다
            return _timestampPool;
        }
        /** @brief 이번 프레임이 쓰는 쿼리 구간의 시작 인덱스입니다. */
        /** @brief 기록 중인 프레임의 인플라이트 칸(0..kMaxFrameCountInFlight-1)입니다. 어느 스레드에서나 읽는다. */
        uint32 getFrameSlot() const { return _frameFenceRing.getRecordingSlot(); }
        uint32 getTimestampBase() const { return getFrameSlot() * constant::render::kMaxGPUTimestampSlot; }
        /** @brief 슬롯 하나를 기록했다고 표시합니다. 여러 패스 스레드가 동시에 부를 수 있습니다. */
        void markTimestampWritten( uint32 slotIndex ) { _timestampRing.markWritten( slotIndex ); }
        /** @brief 리소스 생성과 파괴 인터페이스(VulkanRHIResourceFactory)를 반환합니다. */
        IRHIResourceFactory* getResourceFactory() override;
        /** @brief 프레임 스트림 컨텍스트입니다. 백버퍼 패스와 Present 가 여기에 기록합니다. */
        IRHICommandContext* getFrameStreamContext() override;

        /** @brief 백엔드 타입(Vulkan)을 반환합니다. */
        RHIBackend getBackendType() const override { return RHIBackend::Vulkan; }

        /** @brief 물리 장치의 형식 조회로 샘플링할 수 있는지 답합니다. */
        bool supportsTextureFormat( RHIFormat format ) const override;

        RHICapabilities getCapabilities() const override
        {
            RHICapabilities caps        = RHIAvailability::query( RHIBackend::Vulkan );
            caps._bMultiDrawIndirect    = _bMultiDrawIndirect;
            caps._bNativeBindless       = _textureSet != nullptr ? 1u : 0u;
            caps._bWireframeFill        = _bFillModeNonSolid;
            caps._bMaterialTessellation = _bTessellationShader != 0 ? SW_TRUE : SW_FALSE;
            caps._bGeometryStage        = _bGeometryShader != 0 ? SW_TRUE : SW_FALSE;
            caps._bTessellationStages   = _bTessellationShader != 0 ? SW_TRUE : SW_FALSE;
            caps._bGPUBreadcrumbs       = _breadcrumbMode != VulkanBreadcrumbMode::None ? SW_TRUE : SW_FALSE;
            caps._bAsyncCompute         = _asyncComputeQueue != nullptr ? SW_TRUE : SW_FALSE;
            caps._bGraphicsShaderWrites = _bFragmentStores;
            caps._bPlacedTextures       = SW_TRUE; // 메모리 하나에 이미지를 겹쳐 묶고 해제 큐가 힙 참조를 펜스까지 든다(`VulkanHeapMemory`)
#if defined( SW_PLATFORM_ANDROID )
            caps._bSurfacePreRotation = SW_TRUE; // 휴대 기기 화면만 회전한다 — 데스크톱 서피스의 currentTransform 은 늘 IDENTITY 다
#endif
            return caps;
        }

        /** @brief 스왑체인을 만든 서피스 회전입니다(회전한 Android 화면만 Identity 가 아니다 — `VulkanRHISwapChain::create`). */
        RHISurfaceTransform getSurfaceTransform() const override { return _swapChain.getSurfaceTransform(); }

        /** @brief 깊이 이미지의 aspect 마스크(VkImageAspectFlags)를 반환합니다. */
        uint32 depthAspectMask() const;

        /** @brief 텍스처 배열 세트(set 1, g_SwBindlessTex2D[])가 준비됐는지 여부입니다. */
        bool supportsNativeBindlessSampling() const override { return _textureSet != nullptr; }

        /** @brief VS 가 슬롯 세트의 t4(g_SwInstances)로 GPUScene 인스턴스 버퍼를 읽을 수 있어 true 입니다. */
        bool supportsInstancedSceneDraw() const override { return true; }

        /** @brief 텍스처는 배열 원소가 사용 중(해제 대기 아님)이고 배열 안이면, 버퍼는 인덱스 → 버퍼 표에 버퍼가 있으면 true 입니다. */
        bool isBindlessIndexLive( RHIBindlessIndexSpace space, RHIDescriptorIndex index ) const override;

        /** @brief 백엔드 버전 문자열을 반환합니다. */
        const utf8* getBackendName() const override { return "Vulkan 1.2"; }

        /** @brief 독립 커맨드 리스트를 만듭니다. */
        unique_ptr<IRHICommandList> createCommandList() override;

        /** @brief 커맨드 리스트를 프레임 스트림의 제출 순서에 연결합니다. 즉시 모드면 바로 제출합니다. */
        void executeCommandList( IRHICommandList* pCmdList ) override;
        void executeCommandListImmediate( IRHICommandList* pCmdList ) override;
        /** @brief 그래픽스 패밀리의 두 번째 큐에 낼 리스트를 만듭니다(패밀리가 같아 일반 리스트와 같은 풀 · 같은 바인딩이다). 그 큐가 없으면 오류를 남기고 nullptr 입니다. */
        unique_ptr<IRHICommandList> createAsyncComputeCommandList() override;
        /** @brief 그래픽스 일을 큐에 내며 세마포어를 신호하고, 두 번째 큐가 그것을 기다려 리스트를 실행한 뒤 다른 세마포어를 신호합니다. 그 세마포어의 번호를 돌려준다. */
        RHIQueueSyncPoint submitAsyncCompute( IRHICommandList* pCmdList ) override;
        /** @brief 그 전 그래픽스 일을 큐에 낸 뒤, 다음 그래픽스 제출이 그 세마포어를 기다리게 합니다. */
        void waitForAsyncCompute( RHIQueueSyncPoint point ) override;

        /** @brief 서피스 제약으로 계약 포맷을 낼 수 없는 경우가 있으므로 실제로 채택한 포맷을 보고합니다. */
        RHIFormat getBackBufferFormat() const override { return _swapChain.getActualBackBufferFormat(); }
        /**
         * @brief 큐 호출(`vkQueueSubmit` · `vkQueueWaitIdle` · `vkWaitForFences`)의 결과를 봅니다. 성공이면 아무것도 하지 않습니다.
         * @details 실패면 어디서(@p pWhere) · 무슨 `VkResult` 인지와 마지막으로 제출 순서에 넣은 패스(이벤트 마커)를 오류 로그로 남긴다.
         *          `VK_ERROR_DEVICE_LOST` 면 처음 한 번만 VK_EXT_device_fault 의 결함 정보(설명 · 주소 · 공급사 코드)를 덧붙인다(확장이 켜졌을 때).
         *          어느 스레드에서 불러도 된다(일회성 업로드는 로더 스레드에서 온다).
         * @param result `VkResult` 값(이 헤더는 vulkan.h 를 들이지 않아 정수로 받는다)
         */
        void reportQueueResult( int32 result, const utf8* pWhere );

    private:
        /** @brief 타임스탬프 계측 능력입니다. 디바이스의 같은 이름 함수로 넘깁니다(수명은 디바이스와 같다). */
        class TimestampProvider final : public IRHITimestampProvider
        {
        public:
            explicit TimestampProvider( VulkanRHIDevice& owner )
                : _owner{ owner }
            {
            }

            void               setTimestampEnabled( bool bEnabled ) override { _owner.setTimestampEnabled( bEnabled ); }
            uint32             getTimestampSlotCount() const override { return _owner.getTimestampSlotCount(); }
            [[nodiscard]] bool readTimestamps( RHITimestampFrame& outFrame ) override { return _owner.readTimestamps( outFrame ); }
            [[nodiscard]] bool readGPUClockNanos( int64& outGPUNanos ) override { return _owner.readGPUClockNanos( outGPUNanos ); }
            bool               isGPUClockReadCheap() const override { return _owner.isGPUClockReadCheap(); }
            bool               isComputeQueueTimestampComparable() const override { return _owner.isComputeQueueTimestampComparable(); }

        private:
            VulkanRHIDevice& _owner;
        };
        struct VulkanBufferRecord;
        struct VulkanTextureRecord;

        /** @brief 텍스처 배열 세트(set 1)의 원소 수입니다. DX12 는 힙 하나(kMaxShaderVisibleDescriptors)를 쓰므로 값이 다릅니다. */
        static constexpr uint32 kBindlessTextureCount = shaderslot::bindless::kVkTextureCount;
        /** @brief RW 텍스처 배열(set 1 binding 3)의 원소 수입니다. UAV 인덱스가 이 값 미만이어야 등록됩니다. */
        static constexpr uint32 kBindlessStorageImageCount = shaderslot::bindless::kVkStorageImageCount;
        /**
         * @brief SRV/CB · UAV 버퍼 번호 공간 하나의 크기입니다(기록 경로의 고정 읽기 표 `_arrBufferReadSlot` · `_arrUAVBufferReadSlot`).
         * @details 버퍼는 그리는 순간 슬롯 세트에 쓰므로 디스크립터 배열 크기 상한이 없지만, 표가 늘면 락 없는 읽기가 매달린다. DX12 의 bindless 힙 용량
         *          (`D3D12RHIDevice::kBindlessDescriptorCapacity`)과 같은 값이라 DX12 에서 들어가는 콘텐츠는 여기서도 들어간다. 차면 등록이 실패하고 오류를 남긴다.
         */
        static constexpr uint32 kBindlessBufferCapacity = 32768;
        /** @brief 슬롯 세트(set 0) 풀 하나의 세트 수입니다. 바인딩 상태가 바뀔 때마다 하나씩 쓰며(배치, 패스 단위), 차면 풀을 하나 더 만듭니다. */
        static constexpr uint32 kSlotSetsPerPool = 4096;

        /// @brief VkBuffer 와 메모리, 사용 플래그입니다.
        struct VulkanBufferRecord
        {
            VkBuffer       _buffer{ nullptr };
            VkDeviceMemory _memory{ nullptr };
            uint32         _sizeBytes{ 0 };
            uint32         _usage{ 0 };      ///< VkBufferUsageFlags
            uint32         _cbSlotSize{ 0 }; ///< 링 상수 버퍼(`createConstantBuffer`)의 프레임 칸 크기, 아니면 0. 표에 넣기 전에 정하고 바뀌지 않아 기록 경로가 락 없이 읽는다
            RHIBufferState _state = RHIBufferState::Common;
        };

        /**
         * @brief 트랜지언트 힙(`IRHIResourceFactory::createTransientHeap`)의 메모리입니다. 힙 핸들 표와 그 위의 placed 텍스처 레코드 · 해제 대리자가 `shared_ptr` 로 함께 붙든다.
         * @details 마지막 참조가 사라질 때 메모리를 놓는다(소멸자). placed 텍스처는 해제 큐의 펜스 뒤에 참조를 놓으므로, 힙 핸들을 먼저 놓아도 그 텍스처를 그린 프레임이 끝나기 전에는
         *          메모리가 남는다 — 해제 큐에 넣은 메모리 참조 세기다. DX12 는 placed 리소스가 힙을 COM 참조로 붙든다(`SetPrivateDataInterface`), 언리얼 `FRHITransientHeap` 도 같은 자리다.
         */
        struct VulkanHeapMemory
        {
            VkDevice       _device{ nullptr }; // 헤더는 vulkan.h 를 보지 않는다(VulkanRHIHandle.h) — VK_NULL_HANDLE 대신 nullptr
            VkDeviceMemory _memory{ nullptr };
            uint64         _sizeBytes{ 0 };
            uint32         _memoryTypeIndex{ 0 };

            VulkanHeapMemory()                                     = default;
            VulkanHeapMemory( const VulkanHeapMemory& )            = delete;
            VulkanHeapMemory& operator=( const VulkanHeapMemory& ) = delete;
            ~VulkanHeapMemory();
        };

        /// @brief VkImage 와 뷰, 현재 레이아웃입니다.
        struct VulkanTextureRecord
        {
            VkImage        _image{ nullptr };
            VkImageView    _imageView{ nullptr };
            VkImageView    _sampleView{ nullptr }; ///< 깊이 텍스처 샘플링용 DEPTH 단일 aspect 뷰입니다. 컬러는 nullptr 이며 `_imageView` 를 그대로 씁니다.
            VkDeviceMemory _memory{ nullptr };     ///< 자기 메모리입니다. placed 텍스처는 비어 있고 `_heapMemory` 가 메모리를 붙든다.
            /// @brief placed 텍스처(`createPlacedTexture2D`)가 놓인 힙입니다. 파괴의 해제 대리자가 이 참조를 펜스 뒤까지 들고 간다.
            shared_ptr<VulkanHeapMemory> _heapMemory;
            VkFramebuffer                _framebuffer{ nullptr };
            VkRenderPass                 _renderPass{ nullptr };
            uint32                       _format{ 0 };    ///< VkFormat
            uint32                       _rhiFormat{ 0 }; ///< RHIFormat. 업로드 때 픽셀 크기를 계산하는 데 쓴다(VkFormat 은 깊이 선택에 따라 달라질 수 있다).
            /// @brief VkImageLayout(UNDEFINED=0)입니다. 병렬 기록 워커가 락 없이 비교 교환으로 바꾼다(`transitionTextureLayout` — DX12 `TextureRecord::_state` 와 같은 형식).
            RHITrackedState<uint32> _layout;
            uint32                  _width{ 0 };
            uint32                  _height{ 0 };
            uint32                  _mipLevels{ 1 };
            uint32                  _arrayLayers{ 1 }; ///< 면 수(배열 원소 · 큐브 면)
            RHITextureDimension     _dimension{ RHITextureDimension::Texture2D };
            /// 이 텍스처에 마지막으로 제출한 쓰기 전송(업로드)의 시리얼입니다. 0 이면 전송한 적이 없습니다. bindless 등록이 이 전송이 끝났는지 확인합니다(`RHITransferSerials::isTextureReadyToRegister`).
            RHITextureUploadSerials _uploadSerials;
            /// 이 텍스처에서 마지막으로 읽은 전송(되읽기)의 시리얼입니다. bindless 등록은 보지 않고, 파괴가 이 복사가 끝날 때까지 이미지를 남길 때 씁니다(전송 계약 6).
            atomic<uint64> _lastReadbackSerial{ 0 };
            uint8          _bRenderTarget : 1;
            uint8          _bDepthStencil : 1;
            uint8          _reserved      : 6;
            /// @brief 면이 여럿일 때 면마다의 2D 뷰(렌더 패스 첨부용)와 단일 RT 프레임버퍼입니다. 이때 `_imageView` 는 셰이더가 보는 배열 또는 큐브 뷰입니다.
            vector<VkImageView>   _listSliceView;
            vector<VkFramebuffer> _listSliceFramebuffer;

            /** @brief 면 `slice` 를 렌더 패스 첨부로 쓸 뷰입니다. 없으면 nullptr 입니다. */
            VkImageView findAttachmentView( uint32 slice ) const
            {
                if ( _listSliceView.empty() )
                    return slice == 0 ? _imageView : nullptr;
                return slice < _listSliceView.size() ? _listSliceView[slice] : nullptr;
            }
            /** @brief 면 `slice` 하나를 타깃으로 하는 오프스크린 프레임버퍼입니다. 없으면 nullptr 입니다. */
            VkFramebuffer findSliceFramebuffer( uint32 slice ) const
            {
                if ( _listSliceFramebuffer.empty() )
                    return slice == 0 ? _framebuffer : nullptr;
                return slice < _listSliceFramebuffer.size() ? _listSliceFramebuffer[slice] : nullptr;
            }
        };

        /// @brief VkPipeline 과 레이아웃입니다.
        struct VulkanPipelineStateRecord
        {
            VkPipeline      _pipeline{ nullptr };
            RHIVertexLayout _vertexLayout; ///< 셰이더가 읽는 정점 버퍼 슬롯과 입력을 지은 메시 정점 형식(드로우가 `RHIGeometryBindings::bindVertexBuffersForDraw` 에 넘긴다)
        };
        /// @brief 텍스처 하나를 옮길 레이아웃 요청입니다(`transitionTextureLayouts` 의 한 원소).
        struct VulkanLayoutRequest
        {
            VulkanTextureRecord* _pRecord{ nullptr };
            uint32               _targetLayout{ 0 }; ///< VkImageLayout
            uint32               _aspect{ 0 };       ///< VkImageAspectFlags
        };
        /// @brief 프레임 밖에서 기록된 리스트가 바꾼 레이아웃의 원래 값입니다.
        struct VulkanPendingLayout
        {
            VkCommandBuffer _cmd{ nullptr };
            VkImage         _image{ nullptr };
            uint32          _oldLayout{ 0 };
        };

        /**
         * @struct StructuredUploadSlot
         * @brief updateStructuredBufferRegions 가 쓰는 프레임 슬롯별 호스트 가시 스테이징(bump 할당)입니다.
         * @details 목적 버퍼에 vkMapMemory 로 직접 쓰면 GPU 가 직전 프레임을 아직 읽는 중인 메모리를 CPU 가
         *          덮어써 프레임 간 찢어짐이 생긴다. 그래서 스테이징에 쓰고
         *          vkCmdCopyBuffer 를 프레임 커맨드 버퍼에 기록한다. 그러면 복사는 GPU 큐 순서상 앞 프레임의
         *          읽기 뒤, 이번 프레임의 드로우 앞에 놓인다(DX12 와 같은 모델). 슬롯은 beginFrame 이
         *          그 슬롯의 펜스를 기다린 뒤에만 다시 쓰이고, 같은 펜스 구간 안에서는 오프셋을 이어서 쓴다.
         */
        struct StructuredUploadSlot
        {
            VkBuffer       _buffer{ nullptr };
            VkDeviceMemory _memory{ nullptr };
            void*          _pMapped{ nullptr };
            uint64         _capacity{ 0 };
            uint64         _uploadOffset{ 0 };
            uint64         _resetFence{ UINT64_MAX }; ///< 마지막으로 오프셋을 되감은 프레임 시리얼(`_frameFenceRing.getRecordingSerial()`)
        };

        /**
         * @struct VulkanTransferJob
         * @brief 텍스처 전송 제출 하나의 커맨드 버퍼와 펜스입니다. 제출 뒤에도 살아 있다가 펜스를 지나면 `_listFreeTransferJob` 으로 돌아가 다시 쓰인다.
         * @details 버퍼는 `_transferCommandPool` 에서 받는다(`_transferMutex` 가 풀을 지킨다). 펜스는 다시 쓰기 직전에 리셋한다.
         */
        struct VulkanTransferJob
        {
            VkCommandBuffer _commandBuffer{ nullptr };
            VkFence         _fence{ nullptr };
        };

        /**
         * @struct VulkanStagingRing
         * @brief 영속 매핑한 호스트 가시 · 일관 버퍼 하나와 그 구간 할당기입니다. 업로드 링과 되읽기 링이 하나씩 있다.
         * @details 업로드 구간은 시리얼을 붙여(`markSubmitted`) 워터마크가 지나면 돌아온다. 되읽기 구간은 결과를 가져갈 때(`free`) 돌아오고, 끝나기 전에 버린 구간은
         *          그때 시리얼을 붙여 복사가 끝난 뒤에 돌아온다 — 구간을 GPU 가 아직 쓰는 동안 다음 되읽기가 같은 자리를 받지 않는다.
         *          링보다 큰 한 장은 할당기가 단독 구간(`RHIStagingSpan::_bSolo`, 오프셋 0)으로 내주고, 그 크기의 일회성 버퍼(`_solo*`)를 만들어 쓴다. 할당기가 단독 구간을
         *          한 번에 하나만 내주므로 링마다 하나다. 할당기가 돌려받은 뒤(`hasLiveSolo` 거짓) `retireFinishedTransfers` 가 지운다.
         */
        struct VulkanStagingRing
        {
            RHIStagingAllocator _allocator;
            VkBuffer            _buffer{ nullptr };
            VkDeviceMemory      _memory{ nullptr };
            uint8*              _pMapped{ nullptr };
            mutex               _soloMutex; ///< 단독 버퍼 칸(아래 셋)을 지키는 잎 락. 만드는 쪽(제출 스레드)과 지우는 쪽(아무 스레드의 회수)이 다르다
            VkBuffer            _soloBuffer{ nullptr };
            VkDeviceMemory      _soloMemory{ nullptr };
            uint8*              _pSoloMapped{ nullptr };

            /** @brief @p span 을 담은 버퍼입니다(단독 구간이면 단독 버퍼). 구간 안의 오프셋은 `span._offsetBytes` 에서 센다. */
            [[nodiscard]] VkBuffer getSpanBuffer( const RHIStagingSpan& span ) const { return span._bSolo != SW_FALSE ? _soloBuffer : _buffer; }
            /** @brief @p span 을 담은 버퍼의 매핑 시작 주소입니다. 구간의 바이트는 여기에 `span._offsetBytes` 이상의 오프셋을 더해 읽고 쓴다. */
            [[nodiscard]] uint8* getSpanMappedBase( const RHIStagingSpan& span ) const { return span._bSolo != SW_FALSE ? _pSoloMapped : _pMapped; }
        };

        /** @brief 가져가지 않은 텍스처 읽기 하나입니다. 결과 바이트는 되읽기 링에 그대로 있고 `takeTextureReadback` 이 그때 복사해 간다. */
        struct VulkanPendingReadback
        {
            RHIStagingSpan _span{};
            uint64         _dataOffsetBytes{ 0 };   ///< 복사가 쓴 링 오프셋(구간 시작을 복사 정렬로 올린 값)
            uint32         _sizeBytes{ 0 };         ///< 결과 바이트 수(`RHITextureMipSpan::_sizeBytes`)
            uint32         _depth24TexelCount{ 0 }; ///< D24 깊이를 float32 로 바꿀 텍셀 수. 0 이면 바꾸지 않는다
        };

    protected:
        /**
         * @brief 지금 열려 있는 프레임 세그먼트의 VkCommandBuffer 입니다. 프레임 밖이면 null 입니다. 밖으로는 `withNativeContext` 로만 나갑니다.
         * @details 프레임 세그먼트는 렌더 스레드만 기록하므로 잡을 잠금이 없다(큐 제출은 `_queueMutex` 가 따로 지킨다).
         */
        void* getNativeContext() const override { return currentCommandBuffer(); }

    private:
        /** @brief 쿼리 풀을 한 번만 만듭니다. */
        void ensureTimestampPool();
        /** @brief 방금 펜스를 통과한 링 슬롯의 결과를 마이크로초로 변환합니다. */
        void collectTimestampsForSlot();

    protected:
        // 네이티브 핸들 getter 는 기반처럼 보호 구역이다 — 바깥은 종류 확인이 든 `IRHIDevice::queryNativeHandles` · `tryGetNativeTexturePointer` 로만 읽는다.
        /** @brief 네이티브 VkDevice 핸들을 반환합니다. */
        void* getNativeDevice() const override { return _device; }

        /** @brief 네이티브 VkQueue 핸들을 반환합니다. */
        void* getNativeCommandQueue() const override { return _graphicsQueue; }

        /**
         * @brief ImGui Vulkan 백엔드 초기화에 필요한 핸들(인스턴스, 물리 디바이스, 큐 패밀리, 백버퍼 렌더 패스, 이미지 수)까지 채웁니다.
         * @details 에디터는 이 클래스를 모른다. `IRHIDevice::queryNativeHandles` 가 버전을 대조한 뒤 이 훅을 부른다.
         */
        [[nodiscard]] bool queryNativeHandlesInternal( RHINativeHandles& outHandles ) const override
        {
            if ( IRHIDevice::queryNativeHandlesInternal( outHandles ) == false )
                return false;
            outHandles._pInstance       = _instance;
            outHandles._pPhysicalDevice = _physicalDevice;
            outHandles._pRenderPass     = _renderPass;
            outHandles._pQueueMutex     = &_queueMutex;
            outHandles._queueFamily     = _graphicsQueueFamilyIndex;
            // 실제 스왑체인 이미지 수를 그대로 알린다. 매직 넘버 2 를 쓰면 백버퍼 개수 계약이 바뀔 때
            // ImGui 쪽만 옛 값으로 남는다.
            outHandles._imageCount    = ( _swapChain.getImageCount() == 0 ) ? constant::render::kMaxFrameCountInFlight
                                                                            : _swapChain.getImageCount();
            outHandles._minImageCount = outHandles._imageCount;
            return true;
        }

        /** @brief 네이티브 텍스처 포인터(VkImageView)를 반환합니다. */
        void* getNativeTexturePointer( RHITextureHandle texture ) const override;

    private:
        /**
         * @brief Vulkan 검증 레이어 지원을 확인합니다.
         */
        bool supportsValidationLayer();
        /**
         * @brief VkInstance 를 만듭니다.
         */
        [[nodiscard]] bool createInstance();
        /**
         * @brief 디버그 메신저를 설정합니다.
         */
        void createDebugMessenger();
        /**
         * @brief 물리 디바이스를 고릅니다. @p bSoftwareAdapter 면 CPU 디바이스(lavapipe, SwiftShader)만 후보로 둡니다.
         */
        bool pickPhysicalDevice( bool bSoftwareAdapter, bool bPreferMinimumPower );
        /**
         * @brief 논리 디바이스와 큐를 만듭니다.
         */
        [[nodiscard]] bool createLogicalDevice();
        /** @brief 장치를 만든 뒤 고른 브레드크럼 확장(`_breadcrumbMode`)의 함수와 표지 버퍼를 준비합니다. 하나라도 없으면 None 으로 내리고 경고를 남깁니다. */
        void initBreadcrumbs();
        /** @brief AMD 표지 버퍼(uint32 두 칸, 호스트 일관 메모리)를 만들고 영속 매핑합니다. 실패하면 false — 부르는 쪽이 `destroyBreadcrumbBuffer` 로 치운다. */
        [[nodiscard]] bool createBreadcrumbBuffer();
        /** @brief AMD 표지 버퍼를 풀고 지웁니다. 여러 번 불러도 된다. */
        void destroyBreadcrumbBuffer();
        /** @brief 브레드크럼 칸 수입니다 — [0] 시작 표지 · [1] 끝 표지(DX12 와 같다). */
        static constexpr uint32 kBreadcrumbSlotCount       = 4; ///< 큐마다 둘([시작 · 끝]) — 그래픽스 [0,1] · 비동기 컴퓨트 [2,3]. 두 큐가 같은 칸을 쓰면 서로 기다리지 않는 쓰기다
        static constexpr uint32 kComputeBreadcrumbSlotBase = 2; ///< 비동기 컴퓨트 리스트의 칸 시작
        /**
         * @brief 그래픽스 커맨드 풀을 만듭니다.
         */
        [[nodiscard]] bool createCommandPool();
        /**
         * @brief 프레임별 커맨드 버퍼를 할당합니다.
         */
        [[nodiscard]] bool createCommandBuffers();
        /**
         * @brief 인플라이트 슬롯의 펜스를 만듭니다. 스왑체인 세마포어는 스왑체인이 만듭니다.
         */
        [[nodiscard]] bool createFrameFences();
        /**
         * @brief 인플라이트 펜스를 파괴하고 컨테이너를 비웁니다.
         */
        void destroyFrameFences();
        /**
         * @brief GPU 가 지원하는 깊이와 스텐실 포맷을 고릅니다.
         */
        bool selectDepthFormat();
        /** @brief @p device 가 텍스처 세트(네이티브 bindless 배열 둘)의 기능과 update-after-bind 한도를 갖췄는지 봅니다. 못 미치면 이유를 남기고 false 입니다(장치 고르기가 건너뛴다). */
        [[nodiscard]] static bool isBindlessCapable( VkPhysicalDevice device, const utf8* pDeviceName );

        /**
         * @brief 엔진이 고정으로 쓰는 컬러 첨부 포맷 중 Vulkan 필수 목록에 없는 것(`R16G16_SNORM` — G버퍼 법선)을 이 GPU 가 첨부로 쓸 수 있는지 확인합니다.
         * @details 없으면 false 이고 폴백은 없습니다(장치 생성이 멈춘다).
         */
        bool verifyRequiredColorAttachmentFormats() const;

        /**
         * @brief 스왑체인을 통째로 다시 만듭니다. 창 크기가 바뀌었거나 present 가 OUT_OF_DATE 를 냈을 때 부릅니다.
         * @details 이미지 개수가 달라질 수 있어 세마포어와 이미지별 펜스 테이블까지 함께 갱신한다.
         */
        [[nodiscard]] bool recreateSwapChain();
        /** @brief 표면을 잃었다(Android `APP_CMD_TERM_WINDOW`) — GPU 를 쉬게 하고 스왑체인 · 표면을 놓습니다. */
        void releaseSurfaceInternal() override;
        /** @brief 표면이 돌아왔다 — 새 네이티브 창으로 표면을 다시 짓고 다음 프레임에 스왑체인을 다시 만듭니다. */
        [[nodiscard]] bool restoreSurfaceInternal() override;
        /**
         * @brief 렌더 패스가 고른 스왑체인(`RHIRenderPassBeginInfo::_pSwapChain`)을 이 백엔드의 것으로 풉니다. nullptr 은 메인 창입니다.
         * @details 다른 창은 이 프레임에 처음 쓰일 때 이미지를 획득하고(낡았으면 그 자리에서 다시 만든다) 그 신호를 이 프레임의 첫 제출이 기다리게 하며,
         *          `endFrame` 의 신호 · 표시 목록에 오른다. 이번 프레임에 그릴 수 없으면 nullptr 입니다.
         */
        VulkanRHISwapChain* useSwapChain( IRHISwapChain* pSwapChain );
        /** @brief 다른 창의 스왑체인 · 프레임버퍼 · 세마포어를 새 크기로 다시 만듭니다. GPU 는 비어 있어야 합니다. */
        [[nodiscard]] bool rebuildWindowSwapChain( VulkanRHISwapChain& swapChain, uint32 width, uint32 height );
        /** @brief `useSwapChain` 이 낡았다고 본 창들을 한 번의 GPU 대기로 다시 만듭니다(`beginFrame` 첫머리 — 메인 창 재생성과 같은 자리). */
        void rebuildPendingWindowSwapChains();
        /** @brief 이번 제출이 기다릴 세마포어 · 단계를 재사용 목록(`_listSubmitWait*`)에 모읍니다(acquire 는 색 출력, 비동기 컴퓨트는 모든 단계). 프레임마다 할당하지 않는다. */
        void gatherSubmitWaits();
        /**
         * @brief 서피스를 잃었을 때(VK_ERROR_SURFACE_LOST_KHR) 같은 창 위에 서피스와 스왑체인을 다시 만듭니다. 스왑체인만이 아니라 서피스부터 만들어야 합니다.
         * @return 다시 만들었으면 true 입니다. 창이 사라졌거나 새 서피스를 그래픽스 큐가 프레젠트할 수 없으면 false 이며, 다음 프레임에 다시 시도합니다.
         */
        [[nodiscard]] bool recreateSurfaceAndSwapChain();
        /** @brief 서피스를 잃은 것을 세고 경고로 남깁니다(잃은 횟수와 그때의 프레임). 다음에 관찰했을 때 환경 탓인지 가리는 데 씁니다. */
        void recordSurfaceLost( const utf8* pWhere );
        /** @brief 제출 순서에 들어간 리스트 · 프레임 세그먼트의 마지막 이벤트 마커를 적어 둡니다(디바이스 손실 로그용). 비어 있으면 그대로 둡니다. */
        void recordSubmittedEventMarker( const utf8* pMarker );
        /** @brief VK_EXT_device_fault 로 디바이스 손실의 결함 정보를 읽어 로그로 남깁니다. 확장이 없으면 없다고 한 줄 남깁니다. */
        void reportDeviceFault();

        /** @brief 파이프라인 캐시를 초기화합니다. */
        [[nodiscard]] bool initializePipelineCache();
        /** @brief 파이프라인 캐시를 디스크에 저장하고 정리합니다. */
        void savePipelineCache();

        /**
         * @brief 메모리 타입을 찾습니다. 찾으면 true 를 반환하며 outIndex 를 채웁니다.
         */
        [[nodiscard]] bool findMemoryType( uint32 typeFilter, uint32 properties, uint32& outIndex );
        /** @brief 불투명 버퍼 핸들로 VulkanBufferRecord 를 찾습니다. */
        VulkanBufferRecord*       resolveAllocatedBuffer( RHIBufferHandle handle );
        const VulkanBufferRecord* resolveAllocatedBuffer( RHIBufferHandle handle ) const;
        /**
         * @brief 링 상수 버퍼의 `slot` 슬롯에 씁니다. 칸 크기는 레코드(`VulkanBufferRecord::_cbSlotSize`)에서 읽는다 — 링 상수 버퍼가 아니면 아무것도 하지 않는다.
         * @details 만들 때 크기를 넘는 쓰기는 자른다. 자르지 않으면 다음 슬롯(다음 프레임의 값)까지 덮어쓴다.
         */
        void writeConstantBufferSlot( RHIBufferHandle buffer, uint32 slot, const void* pData, uint32 size );
        /** @brief 링이 넘어온 슬롯에 옛 값이 남은 상수 버퍼를 마지막 값으로 채웁니다(`RHIConstantBufferMirror`). 슬롯의 펜스를 지난 뒤 부릅니다. */
        void fillConstantBufferSlot();
        /** @brief 불투명 텍스처 핸들로 VulkanTextureRecord 를 찾습니다. */
        VulkanTextureRecord*       resolveTexture( RHITextureHandle handle );
        const VulkanTextureRecord* resolveTexture( RHITextureHandle handle ) const;

        /** @brief 서술대로 VkRenderPass 를 만듭니다. 실패하면 VK_NULL_HANDLE 을 반환합니다. */
        VkRenderPass createRenderPassFromSpec( const VulkanRHIRenderPassCache::RenderPassSpec& spec ) const;

        /** @brief 텍스처 배열 세트(set 1: 무제한 텍스처 배열과 immutable sampler)를 확보합니다. */
        [[nodiscard]] bool ensureTextureSet();
        /** @brief 정적 샘플러, 세트 레이아웃 2개, 파이프라인 레이아웃, 풀(텍스처용 1개와 프레임별 슬롯용), 더미 UBO 를 만듭니다. */
        [[nodiscard]] /** @brief 파이프라인 레이아웃의 푸시 상수 · 슬롯 세트가 보이는 스테이지입니다. 레이아웃과 `vkCmdPushConstants` 가 같은 값을 써야 합니다. */
        uint32
             getAllShaderStages() const;
        bool createDescriptorResources();
        /** @brief 텍스처 배열(set 1 binding 0)의 원소 하나를 갱신합니다. */
        void writeBindlessTextureSlot( RHIDescriptorIndex index, VkImageView view, uint32 imageLayout );
        /** @brief RW 텍스처 배열(set 1 binding 3)의 원소 하나를 갱신합니다(GENERAL 레이아웃). */
        void writeBindlessStorageImageSlot( RHIDescriptorIndex index, VkImageView view );
        /**
         * @brief 풀 묶음에서 슬롯 세트(set 0) 하나를 할당합니다. 풀이 차면 다음 풀을 만듭니다(상한 없음). 풀 · 세트를 만들지 못하면(장치 메모리 부족) 멈춥니다 — 앞 세트로 그리지 않습니다.
         * @details 락이 없다. 풀 묶음은 커맨드 버퍼 하나(기록 스레드 하나)의 것이다.
         */
        VkDescriptorSet allocateSlotSet( VulkanDescriptorPoolSet& poolSet );
        /** @brief 슬롯 세트 풀 하나를 만듭니다(kSlotSetsPerPool 개의 세트이며, 세트마다 b/t/u 슬롯을 모두 담습니다). */
        VkDescriptorPool createSlotPool();
        /** @brief 풀 묶음의 풀을 모두 비웁니다. 그 버퍼의 GPU 펜스가 지난 뒤에만 부릅니다(프레임 링 슬롯 대기 뒤, 재사용 풀 반환 뒤). */
        void resetDescriptorPoolSet( VulkanDescriptorPoolSet& poolSet );
        /** @brief 풀 묶음의 풀을 모두 파괴합니다(shutdown). */
        void destroyDescriptorPoolSet( VulkanDescriptorPoolSet& poolSet );
        /** @brief 디바이스 프레임 스트림이 이번 링 슬롯에서 쓰는 풀 묶음입니다. */
        VulkanDescriptorPoolSet& currentFrameDescriptorPoolSet() { return _arrFrameDescriptorPoolSet[getFrameSlot() % constant::render::kMaxFrameCountInFlight]; }
        /** @brief SRV/CB 번호의 원본 버퍼입니다(없으면 0). 락 없이 고정 읽기 표를 본다(`_arrBufferReadSlot`). */
        RHIBufferHandle bindlessSourceBufferAt( RHIDescriptorIndex index ) const;
        /** @brief UAV 번호의 원본 버퍼입니다(없거나 RW 텍스처면 0). 락 없이 고정 읽기 표를 본다(`_arrUAVBufferReadSlot`). */
        RHIBufferHandle uavSourceBufferAt( RHIDescriptorIndex index ) const;
        /** @brief 번호 @p index 의 읽기 표 칸에 @p buffer 를 공개합니다(0 이면 비운다). `_bindlessMutex` 를 배타로 쥔 등록 · 해제가 부른다. */
        void publishBufferReadSlot( RHIDescriptorIndex index, bool bUAV, RHIBufferHandle buffer );
        /** @brief 지금 열려 있는 프레임 세그먼트 버퍼입니다. 프레임 밖이면 VK_NULL_HANDLE 입니다. */
        VkCommandBuffer currentCommandBuffer() const;
        /** @brief 리스트 전용 (풀, 버퍼) 쌍을 빌립니다. 풀이 비어 있으면 새로 만듭니다. */
        VulkanCommandListEntry acquireCommandListEntry();
        /** @brief 빌린 쌍을 GPU 펜스를 통과한 뒤 재사용 풀로 돌려보냅니다. */
        void recycleCommandListEntryDeferred( VulkanCommandListEntry entry );
        /** @brief 빌린 쌍을 **지금** 파괴합니다. 디바이스가 종료되면서 살아 있는 리스트를 분리할 때만 씁니다. */
        void destroyCommandListEntryImmediate( VulkanCommandListEntry& entry );
        /** @brief 살아 있는 리스트를 기록해 둡니다. 디바이스가 먼저 종료되면 종료할 때 연결을 끊어 줍니다. */
        void registerCommandList( VulkanRHICommandList* pCmdList );
        /** @brief 리스트가 소멸할 때 목록에서 뺍니다. */
        void unregisterCommandList( VulkanRHICommandList* pCmdList );
        /** @brief 프레임 스트림의 다음 세그먼트 버퍼를 얻어 기록을 시작합니다. */
        VkCommandBuffer beginNextFrameSegment();
        /** @brief 프레임 스트림의 새 세그먼트를 열고 기록 상태 · 뷰포트 · 시저를 다시 깝니다(`executeCommandList` · `submitFrameJobNow` 가 함께 쓴다). 못 열면 프레임을 멈춘다. */
        void openNextFrameSegment();
        /**
         * @brief 대기 중인 그래픽스 버퍼(`_listPendingSubmit`)를 곧바로 그래픽스 큐에 냅니다. 남은 acquire 대기와 비동기 컴퓨트 대기를 이 제출이 기다리고, @p signal 이 있으면 신호합니다.
         * @details 즉시 모드 비우기 · 포크 · 조인이 함께 쓴다. 부르는 쪽이 큐 락을 쥐지 않는다.
         */
        void submitPendingGraphics( VkSemaphore signal );
        /** @brief 지금 세그먼트를 닫아 대기열에 넣고 대기열을 곧바로 큐에 낸 뒤 새 세그먼트를 엽니다. @p signal 은 `submitPendingGraphics` 와 같다. */
        void submitFrameJobNow( VkSemaphore signal );
        /** @brief 이번 프레임 슬롯의 이진 세마포어를 하나 빌립니다(슬롯의 펜스가 지나면 다시 쓴다). 만들지 못하면 멈춘다. */
        VkSemaphore acquireAsyncSemaphore();

        /** @brief 이미지 레이아웃을 배리어로 전환합니다. */
        bool transitionImageLayout( VkCommandBuffer cmd, VkImage image, uint32 oldLayout, uint32 newLayout, uint32 aspect );
        /** @brief 레이아웃 전이 하나의 이미지 배리어(모든 밉 · 면)와 그 앞뒤 단계를 채웁니다. `transitionImageLayout` 과 묶음(`transitionTextureLayouts`)이 같은 표를 쓴다. */
        static void fillImageLayoutBarrier( VkImage image, uint32 oldLayoutU32, uint32 newLayoutU32, uint32 aspectU32, VkImageMemoryBarrier& barrier, VkPipelineStageFlags& srcStage,
                                            VkPipelineStageFlags& dstStage );
        /** @brief 프레임 밖 기록이면 @p cmd 가 처음 바꾼 @p image 의 옛 레이아웃을 버린 프레임 기록에 적습니다(다음 `beginFrame` 이 되돌린다). 프레임 안이면 아무것도 하지 않는다. */
        void rememberDroppedFrameLayout( VkCommandBuffer cmd, VkImage image, uint32 layoutBefore );

        /**
         * @brief 텍스처를 `targetLayout` 으로 전이합니다. **확인, 배리어, 기록이 한 덩어리입니다.**
         * @details "현재 레이아웃을 읽고, 배리어를 쏘고, 레코드를 갱신" 하는 세 동작을 호출 지점에서 따로 하지 않는다.
         *          병렬 패스 기록에서 그 셋이 갈라지면 두 스레드가 같은
         *          "이전 레이아웃" 을 보고 각자 배리어를 쏘거나, 한쪽이 이미 바꿔 놓은 뒤라 실제
         *          레이아웃과 기록이 어긋난다. `vkQueueSubmit` 이 "이 이미지가 X 레이아웃일 것으로
         *          기대했는데 아니다" 라며 거부한다.
         *          세 동작은 `RHITrackedState::transitionTo` 의 비교 교환 하나다 — 이긴 워커만 배리어를 쏜다(락 없음).
         * @note 이미 그 레이아웃이면 아무것도 하지 않습니다.
         */
        void transitionTextureLayout( VkCommandBuffer cmd, VulkanTextureRecord& record, uint32 targetLayout, uint32 aspect );
        /**
         * @brief 여러 텍스처를 `vkCmdPipelineBarrier` 한 번으로 옮깁니다(`transitionTextureLayout` 의 묶음 — 단일도 이 길이다).
         * @details 원소마다 레코드 레이아웃을 비교 교환으로 옮기고, 이긴 것의 이미지 배리어만 모은다. 단계 마스크는 호출 하나에 하나라(명세 vkCmdPipelineBarrier — srcStageMask ·
         *          dstStageMask 가 모든 배리어의 범위) 원소별 단계의 합을 쓴다. 넓어질 뿐 좁아지지 않는다. 이미 그 레이아웃이면 건너뛴다.
         */
        void transitionTextureLayouts( VkCommandBuffer cmd, const VulkanLayoutRequest* pRequest, uint32 count );
        /**
         * @brief 힙에 겹쳐 놓은 @p record 를 이제부터 쓴다고 기록합니다(`IRHICommandList::beginTextureAlias`). 같은 바이트를 쓴 앞 이미지의 일이 끝난 뒤에 이 이미지를 쓰고, 내용은 버린다.
         * @details 앨리어스 사이의 쓰기는 메모리 의존으로 갈라야 하고, 다른 앨리어스가 쓴 이미지는 UNDEFINED 에서 옮겨야 한다(Vulkan 명세 Memory Aliasing —
         *          https://raw.githubusercontent.com/KhronosGroup/Vulkan-Docs/main/chapters/resources.adoc). 추적 레이아웃은 그대로 두고 UNDEFINED → 추적 레이아웃 전이로 내용을 버린다
         *          (추적이 UNDEFINED 면 다음 전이가 이미 UNDEFINED 에서 출발한다). DX12 는 앞 리소스를 특정하지 않는 앨리어싱 배리어다.
         */
        void beginImageAlias( VkCommandBuffer cmd, VulkanTextureRecord& record, uint32 aspect );
        /** @brief 오프스크린 VkRenderPass 를 확보합니다. */
        [[nodiscard]] bool ensureOffscreenRenderPass( uint32 vkFormat );
        /** @brief 오프스크린 텍스처용 프레임버퍼를 만듭니다. */
        [[nodiscard]] bool createOffscreenFramebuffer( VulkanTextureRecord& record );
        /** @brief 오프스크린 프레임버퍼를 파괴합니다. */
        void destroyOffscreenFramebuffer( VulkanTextureRecord& record );
        /** @brief 프레임버퍼(와 이 프레임버퍼가 소유한 렌더 패스)를 GPU 펜스를 통과한 뒤 파괴하도록 큐에 넣습니다. */
        void enqueueFramebufferRelease( VkFramebuffer framebuffer, VkRenderPass ownedRenderPass );
        /** @brief 파이프라인용 VkRenderPass 를 확보합니다. */
        VkRenderPass ensurePipelineRenderPass( const RHIPipelineStateDesc& desc );
        /** @brief 합성 프레임버퍼를 확보합니다. */
        [[nodiscard]] bool ensureCompositeFramebuffer( const VulkanRHIRenderPassCache::CompositeKey& key, VulkanRHIRenderPassCache::CompositeRecord& outRecord );
        /** @brief 이 텍스처를 쓰는 합성 프레임버퍼를 캐시에서 빼서 GPU 펜스 뒤에 파괴합니다. */
        void destroyCompositeFramebuffersUsing( RHITextureHandle texture );
        /** @brief usageFlags 로 VkBuffer 를 만들고 초기 데이터를 올립니다. @p cbSlotSize 는 링 상수 버퍼의 프레임 칸 크기(아니면 0)로, 레코드를 표에 넣기 전에 적는다. */
        RHIBufferHandle createVulkanBuffer( uint32 sizeBytes, uint32 usageFlags, const void* pInitialData, uint32 cbSlotSize );
        /** @brief `_listDroppedFrameLayout` 의 기록을 텍스처 레코드에 되돌리고 비웁니다. 새 프레임의 기록이 시작되기 전에만 부릅니다. */
        void restoreDroppedFrameLayouts();

        /**
         * @brief 전송 전용 커맨드 풀과 업로드 · 되읽기 스테이징 링을 만듭니다.
         * @param desc 업로드 링(`_stagingRingBytes`), 되읽기 링(`_readbackRingBytes`), 링보다 큰 한 장의 상한(`_stagingSoloMaxBytes`, 두 링에 같이)을 읽습니다.
         *             링 용량이 0 이면 오류를 남기고 false 입니다(기본값으로 바꾸지 않는다).
         */
        [[nodiscard]] bool initializeTransfers( const RHISwapChainDesc& desc );
        /** @brief 남은 전송 작업, 전송을 기다리던 해제, 링, 풀을 놓습니다. GPU 가 쉰 뒤(종료 2단계 뒤)에 부릅니다. */
        void shutdownTransfers();
        /** @brief @p ring 에 @p capacityBytes 의 호스트 가시 · 일관 버퍼를 만들어 영속 매핑하고 메모리 추적기의 Staging 항목에 올립니다. 단독 상한은 @p soloMaxBytes 입니다. */
        [[nodiscard]] bool createStagingRing( VulkanStagingRing& ring, uint64 capacityBytes, uint64 soloMaxBytes, uint32 usageFlags, const utf8* pName );
        /** @brief 링 버퍼와 단독 버퍼를 놓고 구간 할당기를 비웁니다. 없는 링이면 아무것도 하지 않습니다. */
        void destroyStagingRing( VulkanStagingRing& ring );
        /**
         * @brief @p capacityBytes 의 호스트 가시 · 일관 버퍼를 만들어 영속 매핑하고 메모리 추적기의 Staging 항목에 올립니다(링과 단독 버퍼가 같이 쓴다).
         * @return 만들었으면 true. 실패하면 이유를 로그로 남기고 만든 것을 놓은 뒤 false 이며 세 출력은 비어 있다.
         */
        [[nodiscard]] bool createMappedStagingBuffer( uint64 capacityBytes, uint32 usageFlags, const utf8* pName, VkBuffer& outBuffer, VkDeviceMemory& outMemory,
                                                      uint8*& outMapped );
        /** @brief `createMappedStagingBuffer` 로 만든 버퍼를 놓고 메모리 추적기에서 내립니다. 비어 있으면 아무것도 하지 않습니다. 세 칸을 비웁니다. */
        void destroyMappedStagingBuffer( VkBuffer& rBuffer, VkDeviceMemory& rMemory, uint8*& pMapped );
        /**
         * @brief @p ring 에서 구간을 잡습니다. 차 있으면 끝난 전송을 한 번 걷은 뒤 다시 봅니다. 기다리지 않습니다.
         * @details 할당 결과를 제출 결과로 옮기고 못 받은 구간을 놓고 이유를 남기는 곳은 `RHIStagingRingUtil::settleAllocation` 하나다(잃은 장치면 Full 도 Rejected). 단독 구간이면 그 크기의
         *          일회성 버퍼를 만들어 붙인다(앞선 단독 버퍼가 남아 있으면 먼저 지운다 — 할당기가 새 단독 구간을 내줬다면 앞 구간은 이미 돌아왔다).
         *          Rejected 면 이유를 로그로 남기고 잡은 구간은 놓는다. 어느 스레드에서나, 전송 락 밖에서 부른다.
         */
        [[nodiscard]] RHISubmitResult allocateStagingSpan( VulkanStagingRing& ring, uint64 sizeBytes, uint32 usageFlags, const utf8* pCaller, RHIStagingSpan& outSpan );
        /** @brief 할당기가 단독 구간을 돌려받았으면(`hasLiveSolo` 거짓) @p ring 의 단독 버퍼를 지웁니다. `retireFinishedTransfers` 가 부릅니다. */
        void destroyIdleSoloBuffer( VulkanStagingRing& ring );
        /** @brief 놀고 있는 전송 작업을 꺼내(없으면 만들어) 기록을 시작합니다. `_transferMutex` 를 쥐고 부릅니다. */
        [[nodiscard]] bool beginTransferJob( VulkanTransferJob& outJob );
        /**
         * @brief 기록을 끝내고 큐 락 안에서 작업의 펜스와 함께 제출한 뒤, 같은 락 안에서 시리얼을 매깁니다(계약 5). 완료를 기다리지 않습니다.
         * @return 시리얼입니다. 제출하지 못했으면 결과를 보고하고 작업을 돌려놓은 뒤 0 입니다. `_transferMutex` 를 쥐고 부릅니다.
         */
        [[nodiscard]] uint64 submitTransferJob( const VulkanTransferJob& job, const utf8* pWhere );
        /** @brief 펜스를 지난 작업을 놀고 있는 작업 목록으로 돌려놓습니다. */
        void recycleTransferJob( const VulkanTransferJob& job );
        /**
         * @brief 펜스를 지난 전송을 앞에서부터 걷어 워터마크를 올리고, 전송을 기다리던 해제를 프레임 해제 큐로 넘깁니다. 기다리지 않습니다.
         * @details 어느 스레드에서나 부른다(`pollTransfer`, 링이 찬 제출, `beginFrame`). 펜스는 하나씩 확인하므로 앞의 것이 늦게 끝나면 뒤가 끝나도 워터마크는 거기서 멈춘다.
         */
        void retireFinishedTransfers();

    private:
        VkInstance               _instance;
        VkDebugUtilsMessengerEXT _debugMessenger;
        VkPhysicalDevice         _physicalDevice;
        VkDevice                 _device;
        VkQueue                  _graphicsQueue;
        uint32                   _graphicsQueueFamilyIndex;
        /**
         * @brief 그래픽스 큐 패밀리의 두 번째 큐(비동기 컴퓨트)입니다. 패밀리에 큐가 하나뿐이거나 컴퓨트 비트가 없으면 비어 있고 능력이 거짓이다.
         * @details 같은 패밀리라 큐 패밀리 소유권 이전(release/acquire 배리어)이 없다 — 다른 패밀리의 전용 컴퓨트 큐를 쓰려면 버퍼마다 이전이 필요하다(지금 길이 아니다).
         *          제출은 `_queueMutex` 로 그래픽스 큐와 같이 직렬화한다.
         */
        VkQueue _asyncComputeQueue;
        /// @brief 프레임 시리얼 · 완료 워터마크 · 앞서가기 상한입니다. 인플라이트 칸(`getFrameSlot`)은 스왑체인 이미지 인덱스와 **다른 값**입니다
        ///        (이미지 인덱스는 스왑체인이 acquire 로 받아 가지고 있다). 렌더 스레드만 넘기고(`endFrame`), 다른 스레드도 칸 · 해제 키를 읽는다(원자).
        RHIFrameFenceRing _frameFenceRing;

        /// @brief 창 하나의 서피스, 스왑체인, 백버퍼입니다. 이미지, 뷰, 프레임버퍼, 세마포어가 모두 여기 있습니다.
        VulkanRHISwapChain _swapChain;

        VkRenderPass _renderPass;           ///< 스왑체인 렌더 패스(loadOp=CLEAR)
        VkRenderPass _renderPassLoad;       ///< 같은 스왑체인 렌더 패스의 loadOp=LOAD 변형이다. 한 프레임에 백버퍼를
                                            ///< 두 번 이상 열 때(그래프가 그린 뒤 UI 를 올릴 때) 앞의 내용을 보존한다.
        VkRenderPass  _offscreenRenderPass; ///< 게임 뷰와 RT 드로우용 R8G8B8A8_UNORM 컬러 전용 패스
        VkCommandPool _commandPool;         ///< 프레임 버퍼와 프레임 세그먼트용 풀이다. 렌더 스레드만 쓴다.
        /**
         * @brief 프레임 밖에서 제출하고 기다리는 일회성 제출(구조 버퍼 갱신, GPU 시계 읽기) 전용 풀입니다. `_oneShotMutex` 가 보호합니다. 텍스처 전송은 `_transferCommandPool` 을 쓴다.
         * @details 커맨드 풀은 외부 동기화 대상이다. 게임 스레드나 로더 스레드의 업로드가 렌더 스레드가 프레임 버퍼를 기록하는
         *          `_commandPool` 에서 할당하고 해제하면, 스트리밍 텍스처가 프레임 기록과 겹칠 때 풀이 깨진다.
         */
        VkCommandPool _oneShotCommandPool;
        mutex         _oneShotMutex;
        /**
         * @brief `_graphicsQueue` 를 쓰는 모든 호출(`vkQueueSubmit`, `vkQueueWaitIdle`, `vkQueuePresentKHR`, `vkDeviceWaitIdle`)을 보호합니다.
         * @details VkQueue 는 외부 동기화 대상이다. 렌더 스레드의 프레임 제출과 게임 스레드의 일회성 업로드 제출이 겹칠 수 있다.
         */
        mutable mutex _queueMutex;

        /// @brief 버린 프레임의 레이아웃 기록(`_listDroppedFrameLayout`)만 지킵니다. 레이아웃 자체는 레코드의 원자값이라 기록 경로가 잡지 않는다 — 프레임 밖 기록(드물다)만 잡는다.
        mutable mutex _droppedLayoutMutex;
        /**
         * @brief 시작되지 못한 프레임(최소화, 스왑체인 재생성 중)에 기록되다 버려진 리스트가 바꾼 레이아웃의 원래 값입니다.
         * @details 리스트가 제출되지 않으므로 `VulkanTextureRecord::_layout` 만 앞서 나간다. 다음 `beginFrame` 이 되돌린다.
         *          프레임 밖 일회성 제출(`executeCommandListImmediate`)은 제출한 리스트의 기록을 지운다. `_droppedLayoutMutex` 가 보호한다.
         */
        vector<VulkanPendingLayout> _listDroppedFrameLayout;
        /// @brief 리스트에 빌려주는 (풀, 버퍼) 쌍의 재사용 풀입니다. 펜스를 통과한 것만 들어 있습니다.
        mutable mutex                                        _cmdListPoolMutex;
        vector<VulkanCommandListEntry> _listFreeCmdListEntry SW_GUARDED_BY( _cmdListPoolMutex );
        /**
         * @brief 살아 있는 커맨드 리스트입니다(소유하지 않습니다). 종료할 때 연결을 끊어 줍니다.
         * @details 렌더 그래프가 리스트를 프레임을 넘겨 가지고 있으므로 디바이스가 먼저 종료될 수 있다. 끊지 않으면
         *          리스트 소멸자가 종료된 디바이스에 쌍을 반납하려 하고, 쌍의 풀이 누수되어 검증 레이어가 잡는다.
         *          DX12 와 DX11 도 같은 보호를 둔다.
         */
        mutex                         _liveCmdListMutex;
        vector<VulkanRHICommandList*> _listLiveCmd;
        /// @brief 이번 프레임에 큐에 넣을 커맨드 버퍼들입니다. 기록 순서가 곧 실행 순서입니다.
        vector<VkCommandBuffer> _listPendingSubmit;
        /// @brief 프레임 스트림을 리스트 제출 지점마다 잘라 쓰는 추가 세그먼트 버퍼입니다(프레임 슬롯별로 재사용합니다).
        vector<VkCommandBuffer> _arrFrameSegment[constant::render::kMaxFrameCountInFlight];
        StructuredUploadSlot    _arrStructuredUploadSlot[constant::render::kMaxFrameCountInFlight];
        uint32                  _frameSegmentCursor;
        float32                 _timestampPeriod; ///< 타임스탬프 한 슬롯의 나노초(아래 `_timestampPool` 의 값)
        /// @brief 지금 기록 중인 프레임 세그먼트입니다. beginFrame 이 첫 세그먼트로 설정합니다.
        VkCommandBuffer _activeFrameBuffer;

        /**
         * @brief GPU 타임스탬프입니다. 링 슬롯마다 `constant::render::kMaxGPUTimestampSlot` 개의 슬롯을 씁니다.
         * @details 읽기는 `vkWaitForFences` 를 통과한 **직후**에 한다. 그 슬롯의 GPU 작업이 이미
         *          끝났음이 보장된 유일한 지점이라, 측정하려고 파이프라인을 멈춰 세우지 않는다.
         */
        VkQueryPool _timestampPool SW_GUARDED_BY( _renderThread ); ///< 없으면 VK_NULL_HANDLE 이다(이 헤더는 vulkan.h 를 include 하지 않는다).
        VkQueryPool                _clockQueryPool;                ///< GPU 시계 읽기(`readGPUClockNanos`)용 슬롯 하나다. 처음 읽을 때 만든다.
        RHITimestampFrame          _timestampFrame;                ///< 마지막으로 읽은 프레임(`readTimestamps`)

        vector<VkCommandBuffer>            _listCommandBuffer;
        vector<VkFence> _listInFlightFence SW_GUARDED_BY( _renderThread );
        /// @brief 다른 창(`createSwapChain`) · 이 프레임에 이미지를 잡은 창(`useSwapChain`, `endFrame` 이 신호 · 표시한 뒤 비운다) · 다음 프레임에 다시 만들 창.
        RHISwapChainSet<VulkanRHISwapChain> _windowSwapChains;
        /// @brief 이 프레임에 새로 획득한 이미지의 acquire 세마포어입니다. 이 프레임의 첫 제출(즉시 모드 비우기 · `endFrame`)이 모두 기다리고 비운다.
        vector<VkSemaphore> _listPendingAcquireWait;
        /// @brief 조인이 남긴 컴퓨트 신호 세마포어입니다. 다음 그래픽스 제출(포크 · 조인의 비우기 · 즉시 모드 · `endFrame`)이 모두 기다리고 비운다.
        vector<VkSemaphore> _listPendingComputeWait;
        /// @brief 제출 하나가 기다리고 신호할 세마포어의 재사용 목록입니다(`gatherSubmitWaits` · `endFrame`). 렌더 스레드만 쓴다.
        vector<VkSemaphore>          _listSubmitWaitSemaphore;
        vector<VkPipelineStageFlags> _listSubmitWaitStage;
        vector<VkSemaphore>          _listSubmitSignalSemaphore;
        /// @brief 이번 프레임에 비동기 컴퓨트 제출이 신호한 세마포어입니다(번호 = 순번 + 1 이 `RHIQueueSyncPoint`). `endFrame` 이 아직 기다리지 않은 것을 모두 기다린다.
        vector<VkSemaphore> _listAsyncSignal;
        /// @brief `_listAsyncSignal` 마다 그래픽스가 이미 기다렸는지입니다(이진 세마포어는 신호 한 번에 대기 한 번이다).
        vector<uint8> _listAsyncSignalWaited;
        /// @brief 프레임 슬롯마다의 이진 세마포어 풀입니다. 슬롯의 펜스를 기다린 뒤(beginFrame) 커서를 0 으로 돌려 다시 쓴다.
        vector<vector<VkSemaphore>> _listAsyncSemaphorePool;
        uint32                      _asyncSemaphoreCursor; ///< 이번 프레임 슬롯 풀에서 다음에 빌릴 칸

        void*                   _pHWnd;
        void*                   _pDisplayHandle;
        NativeSurfaceKind       _surfaceKind; ///< 두 핸들의 창 시스템(리눅스 WSI 를 고른다 — X11 이면 xlib · xcb, Wayland 면 wayland)
        VulkanWsiKind           _wsiKind;     ///< 인스턴스를 만들 때 고른 서피스 확장(`createSurface` 가 이 값으로 만든다). 인스턴스 전에는 None
        uint32                  _width;
        uint32                  _height;
        uint32                  _depthFormat;      ///< VkFormat. createTexture2D 와 렌더 패스가 함께 쓰는 깊이 포맷
        uint32                  _surfaceLostCount; ///< 이 디바이스가 서피스를 잃은 횟수(진단용)
        uint16                  _bFrameStarted           : 1;
        uint16                  _bEnableValidationLayers : 1;
        uint16                  _bMultiDrawIndirect      : 1;
        uint16                  _bDrawIndirectCount      : 1;
        uint16                  _bSamplerAnisotropy      : 1; ///< 정적 샘플러 ANISO_WRAP 을 실제로 이방성으로 만들 수 있는가
        uint16                  _bFillModeNonSolid       : 1; ///< VK_POLYGON_MODE_LINE 을 쓸 수 있는가(와이어프레임 뷰 모드)
        uint16                  _bDepthClamp             : 1; ///< 깊이 클립을 끌 수 있는가(렌더 상태의 `_bEnableDepthClip`)
        uint16                  _bGeometryShader         : 1; ///< 지오메트리 스테이지를 쓸 수 있는가
        uint16                  _bTessellationShader     : 1; ///< 헐 · 도메인 스테이지를 쓸 수 있는가
        uint16                  _bSwapChainDirty         : 1; ///< resize 나 present 결과로 예약된 스왑체인 재생성 요청
        uint16                  _bDepthHasStencil        : 1; ///< _depthFormat 에 스텐실 plane 이 있는가
        [[maybe_unused]] uint16 _reservedSwapChainBit    : 1;
        uint16                  _bMemoryBudget           : 1; ///< 디바이스에 VK_EXT_memory_budget 을 켰는가(GPU 메모리 사용량과 예산 질의)
        uint16                  _bSurfaceLost            : 1; ///< 서피스를 잃었고 아직 다시 만들지 못했다(다음 beginFrame 이 다시 시도한다).
        uint16                  _bDeviceFault            : 1; ///< 디바이스에 VK_EXT_device_fault 를 켰는가(디바이스 손실 때 결함 주소 · 공급사 코드 질의)
        uint16                  _bFragmentStores         : 1; ///< 프래그먼트 셰이더가 스토리지 버퍼에 쓸 수 있는가(fragmentStoresAndAtomics — 셰이더 메시지 채널의 그래픽스 단계)
        // 1바이트 필드는 위 비트필드 뒤에 모은다. 8바이트 필드 사이에 끼면 필드마다 패딩이 생긴다.
        uint8 _bSwapChainRecreateFailing; ///< 재생성 실패를 한 번만 알린다(성공하면 내린다). 실패하면 프레임마다 다시 시도한다.
        /// @brief GPU 브레드크럼을 남기는 확장입니다(`VulkanBreadcrumbMode`). 장치를 만들 때 정하고, 함수나 표지 버퍼를 얻지 못하면 None 으로 내린다.
        VulkanBreadcrumbMode _breadcrumbMode;
        void*                _pCmdSetCheckpoint;     ///< `vkCmdSetCheckpointNV` 입니다(확장 함수라 void* 로 쥐고 cpp 가 형식을 정한다). NVCheckpoints 일 때만 읽는다
        void*                _pGetCheckpointData;    ///< `vkGetQueueCheckpointDataNV` 입니다. 위와 같다
        void*                _pCmdWriteBufferMarker; ///< `vkCmdWriteBufferMarkerAMD` 입니다. AMDBufferMarker 일 때만 읽는다
        /// @brief Vulkan 1.2 간접 개수 그리기 함수입니다. 링크하지 않고 장치에서 얻는다 — Android libvulkan 은 1.0 · 1.1 함수만 내보낸다. 얻지 못하면 `_bDrawIndirectCount` 를 끈다.
        void*                  _pCmdDrawIndirectCount;
        void*                  _pCmdDrawIndexedIndirectCount;
        VkBuffer               _breadcrumbBuffer; ///< AMD 표지 버퍼 — uint32 두 칸([0] 시작 · [1] 끝), 호스트 보임 · 일관 메모리를 영속 매핑해 소실 뒤 CPU 가 읽는다
        VkDeviceMemory         _breadcrumbMemory;
        const volatile uint32* _pBreadcrumbMapped; ///< `_breadcrumbBuffer` 의 매핑. GPU 가 쓰므로 volatile 로 읽는다
        atomic<uint8>          _bTimestampEnabled; ///< 엔진이 켜기 전에는 풀도 만들지 않는다. `setTimestampEnabled` 는 어느 스레드에서나 부를 수 있어 atomic 이다.
        RHITimestampRing       _timestampRing;     ///< 링 칸마다 적은 슬롯 — 빈 칸은 "리셋 + 제출" 을 거치지 않았으니 읽지 않는다

        VkSampler _defaultSampler;

        VkPipelineLayout _pipelineLayout;
        VkDescriptorPool _descriptorPool;
        VkSampler        _arrStaticSampler[shaderslot::kStaticSamplerCount]; ///< binding_slots.hlsli 4 의 정적 샘플러 세트(set 1 immutable)

        RHIHandleTable<VulkanBufferRecord> _gpuBuffers;
        RHIConstantBufferMirror            _constantBufferMirror; ///< 한 번 쓴 상수 버퍼를 나머지 링 슬롯에도 채운다.
        /// @brief 프레임 스트림 컨텍스트(백버퍼 패스, Present, 프레임 세그먼트)가 쓰는 기록 상태입니다. 리스트는
        /// 각자 자기 것을 가지므로, 여기 있는 것은 "디바이스가 직접 여는 버퍼" 전용입니다.
        VulkanRecordingState _recordingState;
        /// @brief 디스크립터 레지스트리를 보호합니다. 커맨드 기록 경로가 인덱스로 이 목록들을 읽는 동안
        /// register/unregister 가 resize 로 재할당하면 기록 스레드가 쓰레기를 읽습니다
        /// (gv_useRenderThread 기본 true 라 렌더 스레드와 게임 스레드 사이에서 이미 성립하는 레이스).
        /// 읽기는 공유 락이라 서로를 막지 않습니다.
        mutable shared_mutex _bindlessMutex;

        /// @brief UAV 번호 하나의 원본입니다 — 버퍼 UAV 면 `_buffer`, RW 텍스처 배열 원소(set 1 binding 3)면 `_texture`.
        struct UAVEntry
        {
            RHIBufferHandle  _buffer{ 0 };
            RHITextureHandle _texture{ 0 };

            bool             operator==( const UAVEntry& other ) const { return _buffer == other._buffer && _texture == other._texture; }
            RHIBindlessOwner getBindlessOwner() const { return RHIBindlessOwner{ _buffer, _texture }; }
        };
        /// @brief SRV/CB 버퍼 번호 → 원본 버퍼(`_bindlessMutex` 아래). 디스크립터는 그리는 순간 슬롯 세트에 쓰므로 비운 번호도 이 프레임이 끝난 뒤 다시 낸다(`RHIBindlessReuse::AfterFence`).
        RHIBindlessRegistry<RHIBufferHandle, RHIBindlessBufferOwnerOf> _bufferRegistry;
        /// @brief UAV 번호(버퍼 UAV 와 RW 텍스처가 같은 공간) → 원본(`_bindlessMutex` 아래).
        RHIBindlessRegistry<UAVEntry> _uavRegistry;
        /**
         * @brief 기록 경로가 버퍼 번호로 읽는 원본 버퍼 핸들입니다(SRV/CB 와 UAV 각각, 번호마다 한 칸 · 크기 고정).
         * @details 바인딩마다 `_bindlessMutex` 읽기 락과 등록표 · 상수 버퍼 크기 해시 맵을 거치던 것을 걷어 낸 자리다(DX12 `BindlessReadSlot` 과 같은 모양).
         *          쓰기(등록 · 해제)는 배타 락 아래에서 하고 읽기는 락 없이 acquire 로 본다. 해제와 겹친 읽기가 옛 핸들을 받아도 핸들 표의 세대가 지운 버퍼를 가리고,
         *          버퍼 메모리는 GPU 펜스 뒤에 놓인다. 표가 늘지 않으므로 게임 스레드의 등록이 기록과 겹쳐도 읽는 쪽 참조가 매달리지 않는다.
         */
        FixedCountArray<atomic<uint64>, kBindlessBufferCapacity> _arrBufferReadSlot;
        FixedCountArray<atomic<uint64>, kBindlessBufferCapacity> _arrUAVBufferReadSlot;

        RHIHandleTable<VulkanTextureRecord> _gpuTextures;
        /// @brief 트랜지언트 힙 핸들 표입니다. 생성 · 파괴는 셋업 스레드다(DX12 와 같다). 종료가 텍스처 표보다 먼저 비워 마지막 참조가 장치가 살아 있을 때 놓이게 한다.
        unordered_map<RHITransientHeapHandle, shared_ptr<VulkanHeapMemory>> _mapTransientHeap;
        RHITransientHeapHandle                                              _nextTransientHeap{ 1 };
        RHIReleaseQueue                                                     _releaseQueue;
        /// @brief PSO 호환 렌더 패스, 합성 프레임버퍼, desc 렌더 패스 캐시입니다. Vulkan 만 가지고 있습니다(스왑체인 RP 는 위 `_renderPass`).
        VulkanRHIRenderPassCache _renderPassCache;

        /// @brief 텍스처 번호(set 1 binding 0 배열 원소) → 그 번호를 가진 텍스처(`_bindlessMutex` 아래). 등록마다 새 번호이고(다른 쪽의 해제가 내 번호를 놓지 않는다),
        ///        텍스처별 번호 목록은 등록표가 든다(`releaseTexture`). 비운 번호는 펜스 뒤에 원소를 더미로 되돌리고 다시 낸다(`RHIBindlessReuse::AfterFence`).
        RHIBindlessRegistry<RHITextureHandle, RHIBindlessTextureOwnerOf> _textureRegistry;

        VkDescriptorSetLayout _slotSetLayout;    ///< set 0: 슬롯 세트(b 0..15 UBO, t 16..31 SSBO, u 32..47 SSBO)
        VkDescriptorSetLayout _textureSetLayout; ///< set 1: 텍스처 배열과 immutable sampler
        VkDescriptorSet       _textureSet;       ///< set 1. 커맨드 버퍼마다 한 번 바인딩한다.
        /// @brief 디바이스 프레임 스트림의 링 슬롯별 풀 묶음입니다. beginFrame 이 그 슬롯의 펜스를 기다린 뒤 통째로 리셋합니다.
        VulkanDescriptorPoolSet _arrFrameDescriptorPoolSet[constant::render::kMaxFrameCountInFlight];
        /// @brief 리스트 쌍(VulkanCommandListEntry)이 빌려 쓰는 풀 묶음들의 소유자입니다. 쌍과 수명이 같으며(재사용 풀에 남고 shutdown 에서 파괴됩니다).
        vector<unique_ptr<VulkanDescriptorPoolSet>> _listCmdListDescriptorPoolSet SW_GUARDED_BY( _cmdListPoolMutex );
        VkImage                                                                   _bindlessDummyImage;
        VkImageView                                                               _bindlessDummyView;
        VkDeviceMemory                                                            _bindlessDummyMemory;
        VkBuffer                                                                  _dummyUBO; ///< 셰이더가 정적으로 참조하지만 바인딩되지 않은 b# 슬롯이 가리키는 0으로 채운 256바이트
        VkDeviceMemory                                                            _dummyUBOMemory;

        RHIHandleTable<VulkanPipelineStateRecord> _pipelineStates;
        VkPipelineCache                           _pipelineCache;

        /// @brief 전송 티켓의 시리얼 발급기와 완료 워터마크입니다. 시리얼은 큐 제출 락 안에서 `_transferFenceQueue` 가 매긴다.
        RHITransferSerials _transferSerials;
        /// @brief 아직 가져가지 않은 텍스처 읽기입니다(결과는 되읽기 링에 있다).
        RHIPendingReadbackTable<VulkanPendingReadback> _readbackTable;
        /// @brief 제출 순서대로 쌓인 전송 작업입니다. 펜스가 앞에서부터 연속으로 지난 만큼 워터마크를 올린다(`retireFinishedTransfers`).
        RHITransferFenceQueue<VulkanTransferJob> _transferFenceQueue;
        /// @brief 끝나지 않은 전송이 쓰는 텍스처의 해제입니다. 키는 전송 시리얼이고, 워터마크가 지나면 그때의 다음 프레임 펜스 뒤로 `_releaseQueue` 에 넘어간다(계약 6).
        RHIReleaseQueue   _transferReleaseQueue;
        VulkanStagingRing _uploadRing;   ///< 업로드 스테이징(`gv_rhiStagingRingMegabytes`)
        VulkanStagingRing _readbackRing; ///< 되읽기 스테이징(`gv_rhiReadbackRingMegabytes`)
        /**
         * @brief 텍스처 전송의 기록과 제출을 직렬화합니다. 전송 커맨드 풀, 전송이 바꾸는 레이아웃 기록의 순서를 지킨다.
         * @details 잡는 순서는 `_transferMutex` → `_queueMutex` → `_transferFenceQueue` 이다(레이아웃 기록은 레코드의 원자값이라 락이 없다). 링 할당과 소스 복사는 이 락 밖에서 한다.
         */
        mutex                                          _transferMutex;
        VkCommandPool                                  _transferCommandPool; ///< 텍스처 전송 전용 풀(RESET_COMMAND_BUFFER). `_transferMutex` 가 지킨다
        mutex                                          _transferJobMutex;    ///< `_listFreeTransferJob` 만 지키는 잎 락(펜스 줄의 회수 콜백 안에서 잡는다)
        vector<VulkanTransferJob> _listFreeTransferJob SW_GUARDED_BY( _transferJobMutex );

        sw::unique_ptr<VulkanRHICommandContext>  _frameStreamContext;
        sw::unique_ptr<VulkanRHIResourceFactory> _resourceImpl;
        /// @brief `IRHIDevice::_pTimestampProvider` 가 가리키는 능력 객체입니다.
        TimestampProvider _timestampProvider;

        /// @brief 제출 순서에 마지막으로 들어간 패스 이름입니다(디바이스 손실 로그용). `_submittedMarkerMutex` 가 지킵니다.
        fixed_string<constant::buffer::kMax64> _lastSubmittedEventMarker SW_GUARDED_BY( _submittedMarkerMutex );
        mutable mutex                                                    _submittedMarkerMutex;
    };
} // namespace sw
