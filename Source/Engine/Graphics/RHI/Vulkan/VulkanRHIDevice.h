/**
 * @file VulkanRHIDevice.h
 * @brief Vulkan 1.3 RHI 백엔드 디바이스입니다.
 */
#pragma once
#include "Engine/Config/RHIBackendType.h"
#include "Engine/EngineMinimal.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHITimestampProvider.h"
#include "Engine/Graphics/RHI/Support/RHIConstantBufferMirror.h"
#include "Engine/Graphics/RHI/Support/RHIHandleTable.h"
#include "Engine/Graphics/RHI/Support/RHIReleaseQueue.h"
#include "Engine/Graphics/RHI/Support/RHITimestamp.h"
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
     *          풀 하나가 차면 다음 풀을 만든다(kMaxPoolsPerDescriptorPoolSet 까지).
     */
    struct VulkanDescriptorPoolSet
    {
        vector<VkDescriptorPool> _listPool;
        uint32                   _cursor{ 0 };                  ///< 지금 할당 중인 풀
        uint8                    _bExhaustedLogged{ SW_FALSE }; ///< 상한에 도달했다는 로그를 한 번만 남긴다.
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

        RHIBufferHandle _boundMeshVb;
        uint32          _boundMeshStride;
        uint32          _boundMeshOffset;
        RHIBufferHandle _boundInstanceSlotVb; ///< 슬롯 1: 인스턴스 슬롯 스트림(0이면 바인딩 안 됨)
        uint32          _boundInstanceSlotOffset;
        /// @brief 이 스트림에 렌더 패스가 열려 있는지 여부입니다.
        uint8 _bRenderPassActive : 1;
        /// @brief 텍스처 배열 세트(set 1)를 이 버퍼의 두 바인드 포인트에 이미 바인딩했는지 여부입니다.
        uint8 _bTextureSetBound : 1;
        /// @brief 지금 열려 있는 렌더 패스가 스왑체인(백버퍼) 렌더 패스인지 여부입니다. PSO 가 등록되어 있지 않을 때
        ///        폴백 파이프라인을 고르는 기준입니다. 렌더 패스 호환성 때문에 백버퍼용과 오프스크린용이 다릅니다.
        uint8                  _bActiveSwapchainRT : 1;
        [[maybe_unused]] uint8 _reserved           : 5;
        RHIBufferHandle        _boundIndexBuffer;
        uint32                 _boundIndexStride;
        uint32                 _boundIndexOffset;

        /// @brief 바인드 포인트별 슬롯 세트 상태입니다([0] 그래픽스, [1] 컴퓨트). 서로 독립이라 디스패치가 드로우의 바인딩을 지우지 않습니다.
        VulkanSlotState _arrSlotState[2];

        /** @brief 아무것도 바인딩되지 않은 상태로 시작합니다. */
        VulkanRecordingState()
            : _activeGraphicsPSO{ 0 }
            , _boundMeshVb{ 0 }
            , _boundMeshStride{ sizeof( RHIVertex ) }
            , _boundMeshOffset{ 0 }
            , _boundInstanceSlotVb{ 0 }
            , _boundInstanceSlotOffset{ 0 }
            , _bRenderPassActive{ SW_FALSE }
            , _bTextureSetBound{ SW_FALSE }
            , _bActiveSwapchainRT{ SW_FALSE }
            , _reserved{ 0 }
            , _boundIndexBuffer{ 0 }
            , _boundIndexStride{ 4 }
            , _boundIndexOffset{ 0 }
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
     * @brief Vulkan 1.3 그래픽스와 컴퓨트 디바이스 구현입니다(Descriptor Indexing, bindless).
     */
    class VulkanRHIDevice : public IRHIDevice, public IRHITimestampProvider
    {
        friend class VulkanRHICommandContext;
        friend class VulkanRHICommandList;

    public:
        friend class VulkanRHIResourceFactory;
        /** @brief 빈 Vulkan 디바이스를 만듭니다. */
        VulkanRHIDevice();
        /** @brief 인스턴스, 디바이스, 스왑체인을 정리합니다. */
        virtual ~VulkanRHIDevice() override;

        /** @brief Vulkan 인스턴스, 서피스, 디바이스, 스왑체인, 커맨드 풀, 동기화 객체를 만듭니다. */
        bool initializeInternal( const RHISwapChainDesc& desc ) override;

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

        /** @brief 이번 프레임이 제출할 세대(`_frameFenceCounter + 1`)로 해제 큐에 넣습니다. */
        void   enqueueGPURelease( const RHIResourceReleaseDelegate& releaseDelegate ) override;
        uint32 getPendingReleaseCount() const override { return _releaseQueue.getPendingReleaseCount(); }
        /**
         * @brief 장치를 잃은 상태로 둡니다(개발 명령 `rhi.removeDevice`). Vulkan 에는 장치를 제거하는 API 가 없어, 큐 호출이 `VK_ERROR_DEVICE_LOST` 를 처음 돌려준
         *        뒤와 같은 상태(`isDeviceLost`)를 만든다. 그 뒤로 제출 · 대기 · 표시를 하지 않고 프레임을 건너뛰며, 종료는 기다리지 않고 끝난다.
         */
        bool debugRemoveDevice() override;
        /** @brief 큐 호출이 `VK_ERROR_DEVICE_LOST` 를 돌려줬거나 `debugRemoveDevice` 가 불렸으면 true 입니다. 어느 스레드에서 읽어도 된다. */
        bool isDeviceLost() const { return _bDeviceLost.load( std::memory_order_acquire ) != SW_FALSE; }

        /** @brief 크기를 기록하고, 스왑체인 재생성은 beginFrame 까지 미룹니다. */
        void resizeInternal( uint32 width, uint32 height ) override;
        /** @brief present 모드를 새 VSync 값으로 바꾸도록 스왑체인을 다음 `beginFrame` 에서 다시 만들게 표시합니다. */
        void               applyVSyncInternal() override;
        [[nodiscard]] bool createRenderPass();
        /** @brief 프레임을 엽니다(펜스 대기, 스왑체인 이미지 획득, 커맨드 버퍼 기록 시작). */
        void beginFrame() override;

        /** @brief 프레임을 닫습니다(vkQueueSubmit, bPresent 면 vkQueuePresentKHR). */
        void endFrame( bool vsync, bool bPresent = true ) override;

        void               setTimestampEnabled( bool bEnabled ) override { _bTimestampEnabled = bEnabled ? SW_TRUE : SW_FALSE; }
        uint32             getTimestampSlotCount() const override;
        [[nodiscard]] bool readTimestamps( RHITimestampFrame& outFrame ) override;
        [[nodiscard]] bool readGPUClockNanos( int64& outGPUNanos ) override;
        bool               isGPUClockReadCheap() const override { return false; }

        /** @brief 타임스탬프 쿼리 풀입니다. 준비되지 않았으면 VK_NULL_HANDLE 입니다. */
        VkQueryPool getTimestampPool() const { return _timestampPool; }
        /** @brief 이번 프레임이 쓰는 쿼리 구간의 시작 인덱스입니다. */
        uint32 getTimestampBase() const { return _currentFrame * constant::kMaxGPUTimestampSlot; }

    private:
        /** @brief 쿼리 풀을 한 번만 만듭니다. */
        void ensureTimestampPool();
        /** @brief 방금 펜스를 통과한 링 슬롯의 결과를 마이크로초로 변환합니다. */
        void collectTimestampsForSlot();

    public:
        /** @brief 리소스 생성과 파괴 인터페이스(VulkanRHIResourceFactory)를 반환합니다. */
        IRHIResourceFactory* getResourceFactory() override;
        /** @brief 프레임 스트림 컨텍스트입니다. 백버퍼 패스와 Present 가 여기에 기록합니다. */
        IRHICommandContext* getFrameStreamContext() override;

        /** @brief 백엔드 타입(Vulkan)을 반환합니다. */
        RHIBackend getBackendType() const override { return RHIBackend::Vulkan; }

        RHICapabilities getCapabilities() const override
        {
            RHICapabilities caps     = RHIAvailability::query( RHIBackend::Vulkan );
            caps._bMultiDrawIndirect = _bMultiDrawIndirect;
            caps._bNativeBindless    = _textureSet != nullptr ? 1u : 0u;
            return caps;
        }

        /** @brief 깊이 이미지의 aspect 마스크(VkImageAspectFlags)를 반환합니다. */
        uint32 depthAspectMask() const;

        /** @brief 텍스처 배열 세트(set 1, g_SwBindlessTex2D[])가 준비됐는지 여부입니다. */
        bool supportsNativeBindlessSampling() const override { return _textureSet != nullptr; }

        /** @brief VS 가 슬롯 세트의 t4(g_SwInstances)로 GPUScene 인스턴스 버퍼를 읽을 수 있어 true 입니다. */
        bool supportsInstancedSceneDraw() const override { return true; }

#if !defined( SW_SHIPPING )
        /** @brief 텍스처는 배열 원소가 사용 중(해제 대기 아님)이고 배열 안이면, 버퍼는 인덱스 → 버퍼 표에 버퍼가 있으면 true 입니다. */
        bool isBindlessIndexLive( RHIBindlessIndexSpace space, RHIDescriptorIndex index ) const override;
#endif

        /** @brief 백엔드 버전 문자열을 반환합니다. */
        const utf8* getBackendName() const override { return "Vulkan 1.3"; }

        /** @brief 네이티브 VkDevice 핸들을 반환합니다. */
        void* getNativeDevice() const override { return _device; }

        /** @brief 지금 열려 있는 프레임 세그먼트의 VkCommandBuffer 를 반환합니다. 프레임 밖이면 null 입니다. */
        void* getNativeContext() const override
        {
            VkCommandBuffer cmd = currentCommandBuffer();
            return cmd;
        }

        /** @brief 네이티브 VkQueue 핸들을 반환합니다. */
        void* getNativeCommandQueue() const override { return _graphicsQueue; }

        /** @brief 독립 커맨드 리스트를 만듭니다. */
        unique_ptr<IRHICommandList> createCommandList() override;

        /** @brief 커맨드 리스트를 프레임 스트림의 제출 순서에 연결합니다. 즉시 모드면 바로 제출합니다. */
        void executeCommandList( IRHICommandList* pCmdList ) override;
        void executeCommandListImmediate( IRHICommandList* pCmdList ) override;

        /** @brief 서피스 제약으로 계약 포맷을 낼 수 없는 경우가 있으므로 실제로 채택한 포맷을 보고합니다. */
        RHIFormat getBackBufferFormat() const override { return _swapChain.getActualBackBufferFormat(); }

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
            outHandles._imageCount    = ( _swapChain.getImageCount() == 0 ) ? constant::kMaxFrameCountInFlight
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
        bool pickPhysicalDevice( bool bSoftwareAdapter );
        /**
         * @brief 논리 디바이스와 큐를 만듭니다.
         */
        [[nodiscard]] bool createLogicalDevice();
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

        /**
         * @brief 스왑체인을 통째로 다시 만듭니다. 창 크기가 바뀌었거나 present 가 OUT_OF_DATE 를 냈을 때 부릅니다.
         * @details 이미지 개수가 달라질 수 있어 세마포어와 이미지별 펜스 테이블까지 함께 갱신한다.
         */
        [[nodiscard]] bool recreateSwapChain();
        /**
         * @brief 서피스를 잃었을 때(VK_ERROR_SURFACE_LOST_KHR) 같은 창 위에 서피스와 스왑체인을 다시 만듭니다. 스왑체인만이 아니라 서피스부터 만들어야 합니다.
         * @return 다시 만들었으면 true 입니다. 창이 사라졌거나 새 서피스를 그래픽스 큐가 프레젠트할 수 없으면 false 이며, 다음 프레임에 다시 시도합니다.
         */
        [[nodiscard]] bool recreateSurfaceAndSwapChain();
        /** @brief 서피스를 잃은 것을 세고 경고로 남깁니다(잃은 횟수와 그때의 프레임). 다음에 관찰했을 때 환경 탓인지 가리는 데 씁니다. */
        void noteSurfaceLost( const utf8* pWhere );

    public:
        /**
         * @brief 큐 호출(`vkQueueSubmit` · `vkQueueWaitIdle` · `vkWaitForFences`)의 결과를 봅니다. 성공이면 아무것도 하지 않습니다.
         * @details 실패면 어디서(@p pWhere) · 무슨 `VkResult` 인지와 마지막으로 제출 순서에 넣은 패스(이벤트 마커)를 오류 로그로 남긴다.
         *          `VK_ERROR_DEVICE_LOST` 면 처음 한 번만 VK_EXT_device_fault 의 결함 정보(설명 · 주소 · 공급사 코드)를 덧붙인다(확장이 켜졌을 때).
         *          어느 스레드에서 불러도 된다(일회성 업로드는 로더 스레드에서 온다).
         * @param result `VkResult` 값(이 헤더는 vulkan.h 를 들이지 않아 정수로 받는다)
         */
        void reportQueueResult( int32 result, const utf8* pWhere );

    private:
        /** @brief 제출 순서에 들어간 리스트 · 프레임 세그먼트의 마지막 이벤트 마커를 적어 둡니다(디바이스 손실 로그용). 비어 있으면 그대로 둡니다. */
        void noteSubmittedEventMarker( const utf8* pMarker );
        /** @brief VK_EXT_device_fault 로 디바이스 손실의 결함 정보를 읽어 로그로 남깁니다. 확장이 없으면 없다고 한 줄 남깁니다. */
        void reportDeviceFault();

        /** @brief 파이프라인 캐시를 초기화합니다. */
        bool initializePipelineCache();
        /** @brief 파이프라인 캐시를 디스크에 저장하고 정리합니다. */
        void savePipelineCache();

        /**
         * @brief 메모리 타입을 찾습니다. 찾으면 true 를 반환하며 outIndex 를 채웁니다.
         */
        bool findMemoryType( uint32 typeFilter, uint32 properties, uint32& outIndex );
        struct VulkanBufferRecord;
        struct VulkanTextureRecord;
        /** @brief 불투명 버퍼 핸들로 VulkanBufferRecord 를 찾습니다. */
        VulkanBufferRecord*       resolveAllocatedBuffer( RHIBufferHandle handle );
        const VulkanBufferRecord* resolveAllocatedBuffer( RHIBufferHandle handle ) const;
        /**
         * @brief 링 상수 버퍼의 `slot` 슬롯에 씁니다. `_bindlessMutex` 를 (읽기로라도) 잡고 부릅니다.
         * @details 만들 때 크기를 넘는 쓰기는 자른다. 자르지 않으면 다음 슬롯(다음 프레임의 값)까지 덮어쓴다.
         */
        void writeConstantBufferSlot( RHIBufferHandle buffer, uint32 slot, const void* pData, uint32 size );
        /** @brief 링이 넘어온 슬롯에 옛 값이 남은 상수 버퍼를 마지막 값으로 채웁니다(`RHIConstantBufferMirror`). 슬롯의 펜스를 지난 뒤 부릅니다. */
        void fillConstantBufferSlot();
        /** @brief 불투명 텍스처 핸들로 VulkanTextureRecord 를 찾습니다. */
        VulkanTextureRecord*       resolveTexture( RHITextureHandle handle );
        const VulkanTextureRecord* resolveTexture( RHITextureHandle handle ) const;

        /** @brief 텍스처 배열 세트(set 1)의 원소 수입니다. DX12 는 힙 하나(kMaxShaderVisibleDescriptors)를 쓰므로 값이 다릅니다. */
        static constexpr uint32 kBindlessTextureCount = shaderslot::bindless::kVkTextureCount;
        /** @brief RW 텍스처 배열(set 1 binding 3)의 원소 수입니다. UAV 인덱스가 이 값 미만이어야 등록됩니다. */
        static constexpr uint32 kBindlessStorageImageCount = shaderslot::bindless::kVkStorageImageCount;
        /** @brief 슬롯 세트(set 0) 풀 하나의 세트 수입니다. 바인딩 상태가 바뀔 때마다 하나씩 쓰며(배치, 패스 단위), 차면 풀을 하나 더 만듭니다. */
        static constexpr uint32 kSlotSetsPerPool = 4096;
        /** @brief 풀 묶음 하나가 가질 수 있는 최대 풀 수입니다. 넘으면 에러 로그를 남기고 직전 세트로 그립니다. */
        static constexpr uint32 kMaxPoolsPerDescriptorPoolSet = 16;

        /// @brief VkBuffer 와 메모리, 사용 플래그입니다.
        struct VulkanBufferRecord
        {
            VkBuffer       _buffer{ nullptr };
            VkDeviceMemory _memory{ nullptr };
            uint32         _sizeBytes{ 0 };
            uint32         _usage{ 0 }; ///< VkBufferUsageFlags
            RHIBufferState _state = RHIBufferState::Common;
        };

        /// @brief VkImage 와 뷰, 현재 레이아웃입니다.
        struct VulkanTextureRecord
        {
            VkImage             _image{ nullptr };
            VkImageView         _imageView{ nullptr };
            VkImageView         _sampleView{ nullptr }; ///< 깊이 텍스처 샘플링용 DEPTH 단일 aspect 뷰입니다. 컬러는 nullptr 이며 `_imageView` 를 그대로 씁니다.
            VkDeviceMemory      _memory{ nullptr };
            VkFramebuffer       _framebuffer{ nullptr };
            VkRenderPass        _renderPass{ nullptr };
            uint32              _format{ 0 };    ///< VkFormat
            uint32              _rhiFormat{ 0 }; ///< RHIFormat. 업로드 때 픽셀 크기를 계산하는 데 쓴다(VkFormat 은 깊이 선택에 따라 달라질 수 있다).
            uint32              _layout{ 0 };    ///< VkImageLayout (UNDEFINED=0)
            uint32              _width{ 0 };
            uint32              _height{ 0 };
            uint32              _mipLevels{ 1 };
            uint32              _arrayLayers{ 1 }; ///< 면 수(배열 원소 · 큐브 면)
            RHITextureDimension _dimension{ RHITextureDimension::Texture2D };
            uint8               _bRenderTarget : 1;
            uint8               _bDepthStencil : 1;
            uint8               _reserved      : 6;
            RHIDescriptorIndex  _bindlessIndex{ kInvalidDescriptorIndex };
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
            VkPipeline _pipeline{ nullptr };
        };

        /** @brief 서술대로 VkRenderPass 를 만듭니다. 실패하면 VK_NULL_HANDLE 을 반환합니다. */
        VkRenderPass createRenderPassFromSpec( const VulkanRHIRenderPassCache::RenderPassSpec& spec ) const;

        /** @brief 텍스처 배열 세트(set 1: 무제한 텍스처 배열과 immutable sampler)를 확보합니다. */
        bool ensureTextureSet();
        /** @brief 정적 샘플러, 세트 레이아웃 2개, 파이프라인 레이아웃, 풀(텍스처용 1개와 프레임별 슬롯용), 더미 UBO 를 만듭니다. */
        [[nodiscard]] bool createDescriptorResources();
        /** @brief 텍스처 배열(set 1 binding 0)의 원소 하나를 갱신합니다. */
        void writeBindlessTextureSlot( RHIDescriptorIndex index, VkImageView view, uint32 imageLayout );
        /** @brief RW 텍스처 배열(set 1 binding 3)의 원소 하나를 갱신합니다(GENERAL 레이아웃). */
        void writeBindlessStorageImageSlot( RHIDescriptorIndex index, VkImageView view );
        /**
         * @brief 풀 묶음에서 슬롯 세트(set 0) 하나를 할당합니다. 풀이 차면 다음 풀을 만듭니다(kMaxPoolsPerDescriptorPoolSet 까지).
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
        VulkanDescriptorPoolSet& currentFrameDescriptorPoolSet() { return _arrFrameDescriptorPoolSet[_currentFrame % constant::kMaxFrameCountInFlight]; }
        /** @brief SRV/CB 레지스트리 인덱스에서 버퍼 핸들을 찾습니다(없으면 0). */
        RHIBufferHandle bindlessSourceBufferAt( RHIDescriptorIndex index ) const;
        /** @brief UAV 레지스트리 인덱스에서 버퍼 핸들을 찾습니다(없으면 0). */
        RHIBufferHandle uavSourceBufferAt( RHIDescriptorIndex index ) const;
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

        /** @brief 이미지 레이아웃을 배리어로 전환합니다. */
        bool transitionImageLayout( VkCommandBuffer cmd, VkImage image, uint32 oldLayout, uint32 newLayout, uint32 aspect );

        /**
         * @brief 텍스처를 `targetLayout` 으로 전이합니다. **확인, 배리어, 기록이 한 덩어리입니다.**
         * @details "현재 레이아웃을 읽고, 배리어를 쏘고, 레코드를 갱신" 하는 세 동작을 호출 지점에서 따로 하지 않는다.
         *          병렬 패스 기록에서 그 셋이 갈라지면 두 스레드가 같은
         *          "이전 레이아웃" 을 보고 각자 배리어를 쏘거나, 한쪽이 이미 바꿔 놓은 뒤라 실제
         *          레이아웃과 기록이 어긋난다. `vkQueueSubmit` 이 "이 이미지가 X 레이아웃일 것으로
         *          기대했는데 아니다" 라며 거부한다.
         * @note 이미 그 레이아웃이면 아무것도 하지 않습니다.
         */
        void transitionTextureLayout( VkCommandBuffer cmd, VulkanTextureRecord& record, uint32 targetLayout, uint32 aspect );
        /** @brief 오프스크린 VkRenderPass 를 확보합니다. */
        bool ensureOffscreenRenderPass( uint32 vkFormat );
        /** @brief 오프스크린 텍스처용 프레임버퍼를 만듭니다. */
        [[nodiscard]] bool createOffscreenFramebuffer( VulkanTextureRecord& record );
        /** @brief 오프스크린 프레임버퍼를 파괴합니다. */
        void destroyOffscreenFramebuffer( VulkanTextureRecord& record );
        /** @brief 프레임버퍼(와 이 프레임버퍼가 소유한 렌더 패스)를 GPU 펜스를 통과한 뒤 파괴하도록 큐에 넣습니다. */
        void enqueueFramebufferRelease( VkFramebuffer framebuffer, VkRenderPass ownedRenderPass );
        /** @brief 파이프라인용 VkRenderPass 를 확보합니다. */
        VkRenderPass ensurePipelineRenderPass( const RHIPipelineStateDesc& desc );
        /** @brief 합성 프레임버퍼를 확보합니다. */
        bool ensureCompositeFramebuffer( const VulkanRHIRenderPassCache::CompositeKey& key, VulkanRHIRenderPassCache::CompositeRecord& outRecord );
        /** @brief 이 텍스처를 쓰는 합성 프레임버퍼를 캐시에서 빼서 GPU 펜스 뒤에 파괴합니다. */
        void destroyCompositeFramebuffersUsing( RHITextureHandle texture );
        /** @brief usageFlags 로 VkBuffer 를 만들고 초기 데이터를 올립니다. */
        RHIBufferHandle createVulkanBuffer( uint32 sizeBytes, uint32 usageFlags, const void* pInitialData );

        VkInstance               _instance;
        VkDebugUtilsMessengerEXT _debugMessenger;
        VkPhysicalDevice         _physicalDevice;
        VkDevice                 _device;
        VkQueue                  _graphicsQueue;
        uint32                   _graphicsQueueFamilyIndex;
        /// @brief 인플라이트 프레임 슬롯(0..kMaxFrameCountInFlight-1)입니다. 스왑체인 이미지 인덱스와 **다른 값**입니다.
        ///        이미지 인덱스는 스왑체인이 acquire 로 받아 가지고 있습니다(`_swapChain.getImageIndex()`).
        uint32 _currentFrame;

        /// @brief 창 하나의 서피스, 스왑체인, 백버퍼입니다. 이미지, 뷰, 프레임버퍼, 세마포어가 모두 여기 있습니다.
        VulkanRHISwapChain _swapChain;

        VkRenderPass _renderPass;           ///< 스왑체인 렌더 패스(loadOp=CLEAR)
        VkRenderPass _renderPassLoad;       ///< 같은 스왑체인 렌더 패스의 loadOp=LOAD 변형이다. 한 프레임에 백버퍼를
                                            ///< 두 번 이상 열 때(그래프가 그린 뒤 UI 를 올릴 때) 앞의 내용을 보존한다.
        VkRenderPass  _offscreenRenderPass; ///< 게임 뷰와 RT 드로우용 R8G8B8A8_UNORM 컬러 전용 패스
        VkCommandPool _commandPool;         ///< 프레임 버퍼와 프레임 세그먼트용 풀이다. 렌더 스레드만 쓴다.
        /**
         * @brief 프레임 밖 일회성 제출(텍스처 업로드, 리드백, 초기 버퍼 복사) 전용 풀입니다. `_oneShotMutex` 가 보호합니다.
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

        /// @brief 텍스처 레이아웃 확인과 전이를 보호합니다(transitionTextureLayout 참고).
        mutable mutex _imageLayoutMutex;
        /// @brief 프레임 밖에서 기록된 리스트가 바꾼 레이아웃의 원래 값입니다.
        struct VulkanPendingLayout
        {
            VkCommandBuffer _cmd{ nullptr };
            VkImage         _image{ nullptr };
            uint32          _oldLayout{ 0 };
        };
        /**
         * @brief 시작되지 못한 프레임(최소화, 스왑체인 재생성 중)에 기록되다 버려진 리스트가 바꾼 레이아웃의 원래 값입니다.
         * @details 리스트가 제출되지 않으므로 `VulkanTextureRecord::_layout` 만 앞서 나간다. 다음 `beginFrame` 이 되돌린다.
         *          프레임 밖 일회성 제출(`executeCommandListImmediate`)은 제출한 리스트의 기록을 지운다. `_imageLayoutMutex` 가 보호한다.
         */
        vector<VulkanPendingLayout> _listDroppedFrameLayout;
        /** @brief `_listDroppedFrameLayout` 의 기록을 텍스처 레코드에 되돌리고 비웁니다. 새 프레임의 기록이 시작되기 전에만 부릅니다. */
        void restoreDroppedFrameLayouts();
        /// @brief 리스트에 빌려주는 (풀, 버퍼) 쌍의 재사용 풀입니다. 펜스를 통과한 것만 들어 있습니다.
        mutable mutex                  _cmdListPoolMutex;
        vector<VulkanCommandListEntry> _listFreeCmdListEntry;
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
        vector<VkCommandBuffer> _arrFrameSegment[constant::kMaxFrameCountInFlight];

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
            uint64         _resetFence{ UINT64_MAX }; ///< 마지막으로 오프셋을 되감은 _frameFenceCounter
        };
        StructuredUploadSlot _arrStructuredUploadSlot[constant::kMaxFrameCountInFlight];
        uint32               _frameSegmentCursor;
        float32              _timestampPeriod; ///< 타임스탬프 한 슬롯의 나노초(아래 `_timestampPool` 의 값)
        /// @brief 지금 기록 중인 프레임 세그먼트입니다. beginFrame 이 첫 세그먼트로 설정합니다.
        VkCommandBuffer _activeFrameBuffer;

        /**
         * @brief GPU 타임스탬프입니다. 링 슬롯마다 `constant::kMaxGPUTimestampSlot` 개의 슬롯을 씁니다.
         * @details 읽기는 `vkWaitForFences` 를 통과한 **직후**에 한다. 그 슬롯의 GPU 작업이 이미
         *          끝났음이 보장된 유일한 지점이라, 측정하려고 파이프라인을 멈춰 세우지 않는다.
         */
        VkQueryPool       _timestampPool;  ///< 없으면 VK_NULL_HANDLE 이다(이 헤더는 vulkan.h 를 include 하지 않는다).
        VkQueryPool       _clockQueryPool; ///< GPU 시계 읽기(`readGPUClockNanos`)용 슬롯 하나다. 처음 읽을 때 만든다.
        RHITimestampFrame _timestampFrame; ///< 마지막으로 읽은 프레임(`readTimestamps`)

        vector<VkCommandBuffer> _listCommandBuffer;
        vector<VkFence>         _listInFlightFence;
        /// @brief 스왑체인 이미지 인덱스별로, 그 이미지를 마지막으로 쓴 인플라이트 펜스입니다(소유하지 않는 사본).
        vector<VkFence> _listImagesInFlight;
        /// @brief 링 슬롯마다 마지막 제출에 매긴 _frameFenceCounter 값입니다. beginFrame 이 그 슬롯의 펜스를
        ///        기다린 뒤 _releaseQueue.tickCompleted(이 값)을 불러 실제 GPU 완료를 확인하고 해제합니다.
        vector<uint64> _listRingFrameNumber;

        void*             _pHWnd;
        void*             _pDisplayHandle;
        NativeSurfaceKind _surfaceKind; ///< 두 핸들의 창 시스템(리눅스 WSI 를 고른다 — X11 이면 xlib · xcb, Wayland 면 wayland)
        /// @brief 스왑체인 제출마다 1씩 증가하는 단조 세대 번호입니다. 해제 큐가 실제 GPU 펜스 완료를 기준으로
        ///        해제하도록(enqueueGPURelease) 프레임 카운트 대신 이 값을 씁니다.
        uint64 _frameFenceCounter;
        uint32 _width;
        uint32 _height;
        uint32 _depthFormat;      ///< VkFormat. createTexture2D 와 렌더 패스가 함께 쓰는 깊이 포맷
        uint32 _surfaceLostCount; ///< 이 디바이스가 서피스를 잃은 횟수(진단용)
        uint16 _bFrameStarted           : 1;
        uint16 _bEnableValidationLayers : 1;
        uint16 _bMultiDrawIndirect      : 1;
        uint16 _bDrawIndirectCount      : 1;
        uint16 _bSamplerAnisotropy      : 1; ///< 정적 샘플러 ANISO_WRAP 을 실제로 이방성으로 만들 수 있는가
        uint16 _bFillModeNonSolid       : 1; ///< VK_POLYGON_MODE_LINE 을 쓸 수 있는가(와이어프레임 뷰 모드)
        uint16 _bSwapChainDirty         : 1; ///< resize 나 present 결과로 예약된 스왑체인 재생성 요청
        uint16 _bDepthHasStencil        : 1; ///< _depthFormat 에 스텐실 plane 이 있는가
        uint16 _bSwapChainImageHeld     : 1; ///< 획득했지만 아직 present 하지 않은 스왑체인 이미지를 쥐고 있는가
        uint16 _bMemoryBudget           : 1; ///< 디바이스에 VK_EXT_memory_budget 을 켰는가(GPU 메모리 사용량과 예산 질의)
        uint16 _linuxWsi                : 2; ///< 0은 없음, 1은 xlib, 2는 xcb, 3은 wayland(Linux 전용)
        uint16 _bSurfaceLost            : 1; ///< 서피스를 잃었고 아직 다시 만들지 못했다(다음 beginFrame 이 다시 시도한다).
        uint16 _bDeviceFault            : 1; ///< 디바이스에 VK_EXT_device_fault 를 켰는가(디바이스 손실 때 결함 주소 · 공급사 코드 질의)
        // 1바이트 필드는 위 비트필드 뒤에 모은다. 8바이트 필드 사이에 끼면 필드마다 패딩이 생긴다.
        uint8 _bSwapChainRecreateFailing; ///< 재생성 실패를 한 번만 알린다(성공하면 내린다). 실패하면 프레임마다 다시 시도한다.
        /// @brief 이번 프레임의 acquire 세마포어 대기가 아직 소비되지 않았는지 여부입니다(첫 제출에서만 바인딩합니다).
        uint8 _bFrameAcquireWaitPending;
        uint8 _bTimestampEnabled;                                       ///< 엔진이 켜기 전에는 풀도 만들지 않는다.
        uint8 _arrTimestampSubmitted[constant::kMaxFrameCountInFlight]; ///< 링 슬롯마다 타임스탬프 쿼리를 제출했는가

        VkSampler _defaultSampler;

        VkPipelineLayout _pipelineLayout;
        VkDescriptorPool _descriptorPool;
        VkSampler        _arrStaticSampler[shaderslot::kStaticSamplerCount]; ///< bindingslots.hlsli 4 의 정적 샘플러 세트(set 1 immutable)
        VkPipeline       _pipeline;
        VkPipeline       _offscreenPipeline; ///< `_pipeline` 과 같은 셰이더를 `_offscreenRenderPass` 에 맞춘 것
        VkBuffer         _vertexBuffer;      ///< 풀스크린 삼각형(정점 3개)
        vector<uint32>   _listBindlessFree;

        RHIHandleTable<VulkanBufferRecord> _gpuBuffers;
        /// @brief 링 상수 버퍼에서 슬롯 하나의 크기입니다. 게임 스레드가 만들고 부수는 동안 렌더 스레드가 드로우마다 읽으므로 `_bindlessMutex` 로 보호합니다.
        unordered_map<RHIBufferHandle, uint32> _mapCbSlotSize;
        RHIConstantBufferMirror                _constantBufferMirror; ///< 한 번 쓴 상수 버퍼를 나머지 링 슬롯에도 채운다.
        /// @brief 프레임 스트림 컨텍스트(백버퍼 패스, Present, 프레임 세그먼트)가 쓰는 기록 상태입니다. 리스트는
        /// 각자 자기 것을 가지므로, 여기 있는 것은 "디바이스가 직접 여는 버퍼" 전용입니다.
        VulkanRecordingState _recordingState;
        /// @brief 디스크립터 레지스트리를 보호합니다. 커맨드 기록 경로가 인덱스로 이 목록들을 읽는 동안
        /// register/unregister 가 resize 로 재할당하면 기록 스레드가 쓰레기를 읽습니다
        /// (gv_useRenderThread 기본 true 라 렌더 스레드와 게임 스레드 사이에서 이미 성립하는 레이스).
        /// 읽기는 공유 락이라 서로를 막지 않습니다.
        mutable std::shared_mutex _bindlessMutex;

        /// @brief SRV/CB 버퍼 인덱스에서 원본 버퍼를 찾는 목록입니다(0이면 빈 슬롯). 인덱스 공간의 기준이며 프리리스트와 짝을 이룹니다.
        vector<RHIBufferHandle> _listBindlessSourceBuffer;
        vector<RHIBufferHandle> _listUavSourceBuffer;
        /// @brief UAV 인덱스에서 원본 텍스처를 찾는 목록입니다(RW 텍스처 배열 원소, 0이면 버퍼 또는 빈 슬롯). 버퍼 UAV 와 같은 인덱스 공간을 씁니다.
        vector<RHITextureHandle> _listUavSourceTexture;
        vector<uint32>           _listUavFree;

        RHIHandleTable<VulkanTextureRecord> _gpuTextures;
        RHIReleaseQueue                     _releaseQueue;
        /// @brief PSO 호환 렌더 패스, 합성 프레임버퍼, desc 렌더 패스 캐시입니다. Vulkan 만 가지고 있습니다(스왑체인 RP 는 위 `_renderPass`).
        VulkanRHIRenderPassCache _renderPassCache;

        vector<uint8>  _listTextureUsed; ///< 텍스처 인덱스 공간(1은 사용 중, 2는 해제 대기). 프리리스트와 짝을 이룬다.
        vector<uint32> _listTextureFree;

        VkDescriptorSetLayout _slotSetLayout;    ///< set 0: 슬롯 세트(b 0..15 UBO, t 16..31 SSBO, u 32..47 SSBO)
        VkDescriptorSetLayout _textureSetLayout; ///< set 1: 텍스처 배열과 immutable sampler
        VkDescriptorSet       _textureSet;       ///< set 1. 커맨드 버퍼마다 한 번 바인딩한다.
        /// @brief 디바이스 프레임 스트림의 링 슬롯별 풀 묶음입니다. beginFrame 이 그 슬롯의 펜스를 기다린 뒤 통째로 리셋합니다.
        VulkanDescriptorPoolSet _arrFrameDescriptorPoolSet[constant::kMaxFrameCountInFlight];
        /// @brief 리스트 쌍(VulkanCommandListEntry)이 빌려 쓰는 풀 묶음들의 소유자입니다. 쌍과 수명이 같으며(재사용 풀에 남고 shutdown 에서 파괴됩니다).
        vector<unique_ptr<VulkanDescriptorPoolSet>> _listCmdListDescriptorPoolSet;
        VkImage                                     _bindlessDummyImage;
        VkImageView                                 _bindlessDummyView;
        VkDeviceMemory                              _bindlessDummyMemory;
        VkBuffer                                    _dummyUBO; ///< 셰이더가 정적으로 참조하지만 바인딩되지 않은 b# 슬롯이 가리키는 0으로 채운 256바이트
        VkDeviceMemory                              _dummyUBOMemory;

        RHIHandleTable<VulkanPipelineStateRecord> _pipelineStates;
        VkPipelineCache                           _pipelineCache;

        sw::unique_ptr<VulkanRHICommandContext>  _frameStreamContext;
        sw::unique_ptr<VulkanRHIResourceFactory> _resourceImpl;

        /// @brief 제출 순서에 마지막으로 들어간 패스 이름입니다(디바이스 손실 로그용). `_submittedMarkerMutex` 가 지킵니다.
        fixed_string<constant::kMaxBuffer64> _lastSubmittedEventMarker;
        mutable mutex                        _submittedMarkerMutex;
        /**
         * @brief 장치를 잃었는가(`isDeviceLost`). 처음 세운 쪽만 결함 정보를 남긴다 — 손실 뒤의 모든 호출이 같은 덤프를 반복하지 않게 한다.
         * @details 세운 뒤에는 펜스 · 큐 · 장치 대기를 부르지 않는다. 사양은 손실 뒤의 대기가 유한 시간에 돌아온다고 하지만 TDR 중의 드라이버 안 대기는
         *          그 보장 밖이다(DX12 가 `GetDeviceRemovedReason` 을 보고 대기를 건너뛰는 것과 같다).
         */
        atomic<uint8> _bDeviceLost;
    };
} // namespace sw
