/**
 * @file VulkanRHIDevice.h
 * @brief Vulkan 1.3 API 기반 RHI 백엔드 클래스 정의
 */
#pragma once
#include "Engine/EngineMinimal.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/Support/RHIHandleTable.h"
#include "Engine/Graphics/RHI/Support/RHIReleaseQueue.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHIHandle.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHISwapChain.h"

#include <shared_mutex>

namespace sw
{
    class VulkanRHICommandContext;
    class VulkanRHIResource;

    /**
     * @struct VulkanCommandListEntry
     * @brief `VulkanRHICommandList` 가 빌려 쓰는 커맨드 풀 + 커맨드 버퍼 쌍.
     * @details `VkCommandPool` 은 외부 동기화 대상이라 여러 스레드가 동시에 기록하려면 **리스트마다
     *          전용 풀**이어야 한다. 다 쓴 쌍은 GPU 펜스를 통과한 뒤에 풀로 돌아간다.
     */
    struct VulkanCommandListEntry
    {
        VkCommandPool   _pool{ nullptr };
        VkCommandBuffer _buffer{ nullptr };
    };

    /**
     * @struct VulkanRecordingState
     * @brief "지금 이 커맨드 버퍼에 무엇이 걸려 있나" — 커맨드 버퍼(=기록 스트림)마다 있어야 하는 상태.
     * @details 예전엔 이 필드들이 `VulkanRHIDevice` 에 있었다. 커맨드 버퍼가 프레임당 하나뿐이라는
     *          전제에서는 문제가 없었지만, 그 전제 때문에 여러 리스트가 동시에 기록할 수 없었다
     *          (서로의 바인딩 캐시를 덮어쓴다). DX12 의 `D3D12RecordingState` 와 같은 역할이며,
     *          "기록 상태는 리스트가 소유하고 디바이스는 진짜 전역 자원만 갖는다"는 구조로 맞춘 것이다.
     */
    struct VulkanRecordingState
    {
        /// @brief 이 스트림에 렌더패스가 열려 있는가.
        uint8 _bRenderPassActive : 1;
        /// @brief set 1(bindless 텍스처)·set 4(정적 샘플러)를 이 버퍼에 이미 바인딩했는가.
        uint8 _bStaticGraphicsSetsBound : 1;
        /// @brief 지금 열려 있는 렌더패스가 스왑체인(백버퍼) 렌더패스인가. PSO 가 등록돼 있지 않을 때
        ///        폴백 파이프라인을 고르는 기준 — 렌더패스 호환성 때문에 백버퍼용/오프스크린용이 다르다.
        uint8                  _bActiveSwapchainRT : 1;
        [[maybe_unused]] uint8 _reserved           : 5;

        RHIPipelineStateHandle _activeGraphicsPso{ 0 };

        RHIBufferHandle _boundMeshVb{ 0 };
        uint32          _boundMeshStride{ sizeof( RHIVertex ) };
        uint32          _boundMeshOffset{ 0 };
        RHIBufferHandle _boundIndexBuffer{ 0 };
        uint32          _boundIndexStride{ 4 };
        uint32          _boundIndexOffset{ 0 };

        /// @brief 마지막으로 바인딩한 set 0(PassCB) — 인덱스가 실제로 바뀔 때만 재바인딩하기 위한 캐시.
        VkDescriptorSet _lastBoundGraphicsSet0{ nullptr };
        /// @brief 마지막으로 바인딩한 MaterialCB(b1) 세트 — 연속 드로우에서 재바인딩을 건너뛴다.
        VkDescriptorSet _lastBoundMaterialSet{ nullptr };

        /** @brief 아무것도 안 걸린 상태로 시작합니다. */
        VulkanRecordingState()
            : _bRenderPassActive{ 0 }
            , _bStaticGraphicsSetsBound{ 0 }
            , _bActiveSwapchainRT{ 0 }
            , _reserved{ 0 }
        {
        }
    };

    /**
     * @class VulkanRHIDevice
     * @brief Vulkan 1.3 그래픽스 및 컴퓨트 디바이스 구현체 (Descriptor Indexing / Bindless 지원)
     */
    class VulkanRHIDevice : public IRHIDevice
    {
        friend class VulkanRHICommandContext;
        friend class VulkanRHICommandList;

    public:
        friend class VulkanRHIResource;
        /** @brief 빈 Vulkan 디바이스. */
        VulkanRHIDevice();
        /** @brief 인스턴스/디바이스/스왑체인을 정리합니다. */
        virtual ~VulkanRHIDevice() override;

        /** @brief Vulkan 인스턴스, 서피스, 디바이스, 스왑체인, 커맨드풀 및 동기화 객체 생성 */
        bool initializeInternal( const RHISwapChainDesc& desc ) override;

        /** @brief Vulkan 파이프라인 및 자원 해제 */
        void shutdownInternal() override;

        /** @brief Vulkan vkDeviceWaitIdle 실행 */
        void waitIdle() override;

        /** @brief 스왑체인 재창조 */
        void resize( uint32 width, uint32 height ) override;
        bool createRenderPass();
        /** @brief 프레임 시작 (vkAcquireNextImageKHR 및 커맨드버퍼 기록 시작) */
        void beginFrame( const float4& clearColor ) override;

        /** @brief 프레임 종료 (vkQueueSubmit 및 vkQueuePresentKHR 제출) */
        void endFrame( bool vsync, bool bPresent = true ) override;

        /** @brief 오프스크린 패스를 시작합니다. */
        IRHIResource* getResource() override;
        /** @brief Present/offscreen/replay Immediate Context. */
        IRHICommandContext* getFrameStreamContext() override;
        /** @brief Mode=Deferred CL 바인딩용 soft Deferred Context. */

        /** @brief 백엔드 타입 반환 (Vulkan) */
        RHIBackend getBackendType() const override { return RHIBackend::Vulkan; }

        bool supportsBindless() const override { return true; }

        RHICapabilities getCapabilities() const override
        {
            RHICapabilities caps     = RHIAvailability::query( RHIBackend::Vulkan );
            caps._bMultiDrawIndirect = _bMultiDrawIndirect;
            caps._bNativeBindless    = _bindlessTextureSet != nullptr ? 1u : 0u;
            return caps;
        }

        /** @brief 디바이스가 선택한 depth/stencil VkFormat */
        uint32 getDepthFormat() const { return _depthFormat; }
        /** @brief depth 포맷에 stencil plane이 있으면 true */
        bool depthFormatHasStencil() const { return _bDepthHasStencil != 0; }
        /** @brief depth 이미지 aspect 마스크 (VkImageAspectFlags) */
        uint32 depthAspectMask() const;

        /** @brief Descriptor-indexing texture array (g_BindlessTextures[]) */
        bool supportsNativeBindlessSampling() const override { return _bindlessTextureSet != nullptr; }

        /** @brief VS 가 storage buffer(g_SwInstances, set 6)로 GPUScene 인스턴스 버퍼를 읽는다. */
        bool supportsInstancedSceneDraw() const override { return true; }

        /** @brief Offscreen MRT (color×N + optional depth) via composite framebuffers. */
        bool supportsMultiRenderTarget() const override { return true; }

        /** @brief 백엔드 버전 문자열 반환 */
        const utf8* getBackendName() const override { return "Vulkan 1.3"; }

        /** @brief Native VkDevice 핸들 반환 */
        void* getNativeDevice() const override { return _device; }

        /** @brief Native VkCommandBuffer 핸들 반환 */
        void* getNativeContext() const override
        {
            VkCommandBuffer cmd = currentCommandBuffer();
            return cmd;
        }

        /** @brief Native VkSwapchainKHR 핸들 반환 */
        void* getNativeSwapChain() const override { return _swapChain.getNative(); }

        /** @brief Native VkQueue 핸들 반환 */
        void* getNativeCommandQueue() const override { return _graphicsQueue; }

        /** @brief Vulkan 그래픽스 파이프라인(VkPipeline) 생성 */

        void drawIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset = 0,
                           RHIDescriptorIndex materialDescriptorIndex = kInvalidDescriptorIndex );

        /** @brief vkCmdDrawIndexedIndirect 실행 */

        void multiDrawIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset, uint32 maxCommandCount,
                                RHIBufferHandle countBuffer = 0, uint32 countBufferOffset = 0 );

        /** @brief GPU 이벤트 디버그 마커 시작 */

        unique_ptr<IRHICommandList> createCommandList() override;

        /** @brief 커맨드 리스트 실행 제출 */
        void executeCommandList( IRHICommandList* pCmdList ) override;
        void executeCommandListImmediate( IRHICommandList* pCmdList ) override;

        /** @brief ImGui용 네이티브 Vulkan 핸들을 조회합니다. */
        VkInstance       getInstance() const { return _instance; }
        VkPhysicalDevice getPhysicalDevice() const { return _physicalDevice; }
        VkDevice         getDevice() const { return _device; }
        VkRenderPass     getRenderPass() const { return _renderPass; }

        /** @brief 서피스 제약으로 계약 포맷을 못 냈을 수 있으므로 실제 채택한 포맷을 보고합니다. */
        RHIFormat getBackBufferFormat() const override { return _swapChain.getActualBackBufferFormat(); }

        bool queryVulkanImGuiNative( RHIVulkanImGuiNative& out ) const override
        {
            out._pInstance       = _instance;
            out._pPhysicalDevice = _physicalDevice;
            out._pDevice         = _device;
            out._pGraphicsQueue  = _graphicsQueue;
            out._pRenderPass     = _renderPass;
            out._queueFamily     = _graphicsQueueFamilyIndex;
            // 실제 스왑체인 이미지 수를 그대로 알린다 — 매직 2 를 쓰면 백버퍼 개수 계약이 바뀔 때
            // ImGui 쪽만 옛 값으로 남는다.
            out._imageCount    = ( _swapChain.getImageCount() == 0 ) ? constant::kMaxFrameCountInFlight
                                                                     : _swapChain.getImageCount();
            out._minImageCount = out._imageCount;
            return _device != nullptr;
        }

        /** @brief 텍스처의 VkImageView를 조회합니다. */
        bool queryVulkanTextureView( RHITextureHandle texture, void*& pOutImageView ) const override;

        /** @brief 네이티브 텍스처 포인터 반환 (VkImageView) */
        void* getNativeTexturePointer( RHITextureHandle texture ) const override;

    private:
        /**
         * @brief Vulkan validation layer 지원을 확인합니다
         */
        bool checkValidationLayerSupport();
        /**
         * @brief VkInstance를 만듭니다.
         */
        bool createInstance();
        /**
         * @brief 디버그 메신저를 설정합니다
         */
        void setupDebugMessenger();
        /**
         * @brief 물리 디바이스를 선택합니다
         */
        bool pickPhysicalDevice();
        /**
         * @brief 논리 디바이스와 큐를 만듭니다.
         */
        bool createLogicalDevice();
        /**
         * @brief 그래픽스 커맨드 풀을 만듭니다.
         */
        bool createCommandPool();
        /**
         * @brief 프레임별 커맨드 버퍼를 할당합니다.
         */
        bool createCommandBuffers();
        /**
         * @brief 인플라이트 슬롯의 펜스를 만듭니다. 스왑체인 세마포어는 스왑체인이 만듭니다.
         */
        bool createFrameFences();
        /**
         * @brief 인플라이트 펜스를 파괴하고 컨테이너를 비웁니다.
         */
        void destroyFrameFences();
        /**
         * @brief GPU가 지원하는 depth/stencil 포맷을 선택합니다.
         */
        bool selectDepthFormat();
        /**
         * @brief 풀스크린 삼각형 버텍스 버퍼를 만듭니다.
         */

        /**
         * @brief 스왑체인을 통째로 다시 만듭니다 (창 크기가 바뀌었거나 present 가 OUT_OF_DATE 를 냈을 때).
         * @details 이미지 개수가 달라질 수 있어 세마포어와 이미지별 펜스 표까지 함께 갱신합니다.
         */
        void recreateSwapChain();

        /** @brief 파이프라인 캐시를 초기화합니다. */
        bool initPipelineCache();
        /** @brief 파이프라인 캐시를 디스크에 저장하고 정리합니다. */
        void savePipelineCache();

        /**
         * @brief 메모리 타입을 찾습니다. 찾으면 true와 함께 outIndex를 채웁니다.
         */
        bool findMemoryType( uint32 typeFilter, uint32 properties, uint32& outIndex );
        /** @brief 불투명 버퍼 핸들을 VulkanBufferRecord로 풉니다. */
        struct VulkanBufferRecord;
        struct VulkanTextureRecord;
        VulkanBufferRecord*       resolveAllocatedBuffer( RHIBufferHandle handle );
        const VulkanBufferRecord* resolveAllocatedBuffer( RHIBufferHandle handle ) const;
        /** @brief 불투명 텍스처 핸들을 VulkanTextureRecord로 풉니다. */
        VulkanTextureRecord*       resolveTexture( RHITextureHandle handle );
        const VulkanTextureRecord* resolveTexture( RHITextureHandle handle ) const;

        /** @brief 실제 bindless 텍스처 용량 — DX12(D3D12RHIDevice.h의 kBindlessTextureCount)와 값이
         *         다르다. 콘텐츠는 더 작은 쪽(DX12)을 기준으로 삼을 것 — RHITypes.h의
         *         constant::kMinComputeRootConstantDwords 옆 주석 참고. */
        static constexpr uint32 kBindlessTextureCount = 4096;
        /** @brief setComputeRootConstants 실제 용량(dword). RHITypes.h의
         *         constant::kMinComputeRootConstantDwords(=DX12 기준, 4개 백엔드 공통 안전값) 참고. */
        static constexpr uint32 kMaxComputeRootConstantDwords = 32;

        /**
         * @brief b0(PassCB)·b1(MaterialCB) 이 들어가는 디스크립터 세트 인덱스.
         * @details Vulkan 은 세트 단위로 바인딩하므로 상수 버퍼 슬롯마다 세트가 하나씩 필요하다.
         *          `Resource/engine/shaders/common.hlsli` 의 SW_VK_CB_SET_* 와 **같은 값이어야
         *          한다** — 어긋나면 셰이더가 파이프라인 레이아웃에 없는 세트를 참조한다.
         *          b1 은 원래 푸시 상수로 우회하고 있었는데, 그러면 리플렉션이 b1 을 실제 상수
         *          버퍼로 보고해도 Vulkan 만 값을 못 받는 예외가 생긴다.
         */
        static constexpr uint32 kPassCbSetIndex     = 0;
        static constexpr uint32 kMaterialCbSetIndex = 10;
        /// @brief 파이프라인 레이아웃이 요구하는 디스크립터 세트 수 (기기 한계와 비교한다).
        static constexpr uint32 kBoundDescriptorSetCount = kMaterialCbSetIndex + 1;

        /// @brief VkBuffer + 메모리 + 사용 플래그
        struct VulkanBufferRecord
        {
            VkBuffer       _buffer{ nullptr };
            VkDeviceMemory _memory{ nullptr };
            uint32         _size{ 0 };
            uint32         _usage{ 0 }; ///< VkBufferUsageFlags
            RHIBufferState _state = RHIBufferState::Common;
        };

        /// @brief VkImage + 뷰 + 현재 레이아웃
        struct VulkanTextureRecord
        {
            VkImage            _image{ nullptr };
            VkImageView        _imageView{ nullptr };
            VkDeviceMemory     _memory{ nullptr };
            VkFramebuffer      _framebuffer{ nullptr };
            VkRenderPass       _renderPass{ nullptr };
            uint32             _format{ 0 }; ///< VkFormat
            uint32             _layout{ 0 }; ///< VkImageLayout (UNDEFINED=0)
            uint32             _width{ 0 };
            uint32             _height{ 0 };
            uint8              _bRenderTarget : 1;
            uint8              _bDepthStencil : 1;
            uint8              _reserved      : 6;
            RHIDescriptorIndex _bindlessIndex{ kInvalidDescriptorIndex };
        };

        /// @brief MRT 프레임버퍼 캐시 키 (컬러+깊이 핸들)
        struct CompositeFbKey
        {
            RHITextureHandle _arrColor[kMaxColorAttachments]{};
            uint32           _colorCount{ 0 };
            RHITextureHandle _depth{ 0 };
            uint8            _arrColorLoadOp[kMaxColorAttachments]{};
            uint8            _depthLoadOp{ 0 };
            /** @brief 같으면 true를 반환합니다. */
            bool operator==( const CompositeFbKey& other ) const
            {
                if ( _colorCount != other._colorCount || _depth != other._depth || _depthLoadOp != other._depthLoadOp )
                    return false;
                for ( uint32 colorIndex = 0; colorIndex < _colorCount; ++colorIndex )
                {
                    if ( _arrColor[colorIndex] != other._arrColor[colorIndex] || _arrColorLoadOp[colorIndex] != other._arrColorLoadOp[colorIndex] )
                        return false;
                }
                return true;
            }
        };

        /// @brief CompositeFbKey 해시
        struct CompositeFbKeyHash
        {
            /** @brief 호출 연산자입니다. */
            size_t operator()( const CompositeFbKey& key ) const
            {
                size_t h = static_cast<size_t>( key._depth ) * 1315423911u;
                h ^= static_cast<size_t>( key._colorCount ) + 0x9e3779b9u;
                h ^= static_cast<size_t>( key._depthLoadOp ) + 0x9e3779b9u;
                for ( uint32 colorIndex = 0; colorIndex < key._colorCount; ++colorIndex )
                {
                    h ^= static_cast<size_t>( key._arrColor[colorIndex] ) + 0x9e3779b9u + ( h << 6 ) + ( h >> 2 );
                    h ^= static_cast<size_t>( key._arrColorLoadOp[colorIndex] ) + 0x9e3779b9u;
                }
                return h;
            }
        };

        /// @brief 캐시된 합성 프레임버퍼
        struct CompositeFbRecord
        {
            VkRenderPass  _renderPass{ nullptr };
            VkFramebuffer _framebuffer{ nullptr };
            uint32        _width{ 0 };
            uint32        _height{ 0 };
        };

        /// @brief PSO에 묶인 렌더 패스 포맷 키
        struct PipelineRpKey
        {
            uint32 _colorCount{ 1 };
            uint32 _arrColorFormat[kMaxColorAttachments]{}; ///< VkFormat
            uint32 _depthFormat{ 0 };                       ///< VkFormat; 0 = no depth
            /** @brief 같으면 true를 반환합니다. */
            bool operator==( const PipelineRpKey& other ) const
            {
                if ( _colorCount != other._colorCount || _depthFormat != other._depthFormat )
                    return false;
                for ( uint32 colorIndex = 0; colorIndex < _colorCount; ++colorIndex )
                {
                    if ( _arrColorFormat[colorIndex] != other._arrColorFormat[colorIndex] )
                        return false;
                }
                return true;
            }
        };

        /// @brief PipelineRpKey 해시
        struct PipelineRpKeyHash
        {
            /** @brief 호출 연산자입니다. */
            size_t operator()( const PipelineRpKey& key ) const
            {
                size_t h = static_cast<size_t>( key._depthFormat ) * 1315423911u;
                h ^= static_cast<size_t>( key._colorCount ) + 0x9e3779b9u;
                for ( uint32 colorIndex = 0; colorIndex < key._colorCount; ++colorIndex )
                {
                    h ^= static_cast<size_t>( key._arrColorFormat[colorIndex] ) + 0x9e3779b9u + ( h << 6 ) + ( h >> 2 );
                }
                return h;
            }
        };

        /// @brief VkPipeline + 레이아웃
        struct VulkanPipelineStateRecord
        {
            VkPipeline _pipeline{ nullptr };
        };

        /// @brief VkRenderPass + 소유권 (desc로 만든 RP만 destroy)
        struct VulkanRenderPassRecord
        {
            VkRenderPass _renderPass{ nullptr };
            uint8        _bOwned{ 0 }; ///< 1 = createRenderPass(desc)가 소유, 0 = swapchain RP alias
        };

        /** @brief bindless 텍스처 배열을 확보합니다. */
        bool ensureBindlessTextureArray();
        /** @brief set 0(Pass/Material UBO) 폴백용 기본 디스크립터 셋(_descriptorSet)을 확보합니다. */
        bool ensureDefaultDescriptorSet();
        /** @brief 디스크립터 풀·셋 레이아웃·파이프라인 레이아웃을 생성합니다. */
        bool createDescriptorResources();
        /** @brief bindless 텍스처 슬롯을 갱신합니다. */
        void writeBindlessTextureSlot( RHIDescriptorIndex index, VkImageView view );
        /** @brief 현재 프레임 커맨드 버퍼. */
        VkCommandBuffer currentCommandBuffer() const;
        /** @brief 리스트 전용 (풀, 버퍼) 쌍을 빌립니다. 풀이 비면 새로 만듭니다. */
        VulkanCommandListEntry acquireCommandListEntry();
        /** @brief 빌린 쌍을 GPU 펜스 통과 후 재사용 풀로 돌려보냅니다. */
        void recycleCommandListEntryDeferred( VulkanCommandListEntry entry );
        /** @brief 프레임 스트림의 다음 세그먼트 버퍼를 얻어 기록을 시작합니다. */
        VkCommandBuffer beginNextFrameSegment();

        // ------------------------------------------------------------------------------
        // bindless 레지스트리 접근자 — 락을 여기 한 곳에 모아둔다(원시 vector 직접 인덱싱 금지).
        // ------------------------------------------------------------------------------
        /** @brief 등록된 상수버퍼 디스크립터셋 수입니다. */
        size_t registeredDescriptorSetCount() const;
        /** @brief 인덱스의 상수버퍼 디스크립터셋입니다. 범위 밖이면 VK_NULL_HANDLE 입니다. */
        VkDescriptorSet registeredDescriptorSetAt( RHIDescriptorIndex index ) const;
        /** @brief 인덱스의 UAV 디스크립터셋입니다. 범위 밖이면 VK_NULL_HANDLE 입니다. */
        VkDescriptorSet registeredUavSetAt( RHIDescriptorIndex index ) const;
        /** @brief 인덱스의 텍스처 디스크립터셋입니다. 범위 밖이면 VK_NULL_HANDLE 입니다. */
        VkDescriptorSet registeredTextureSetAt( RHIDescriptorIndex index ) const;
        /** @brief 이미지 레이아웃을 배리어로 전환합니다. */
        bool transitionImageLayout( VkCommandBuffer cmd, VkImage image, uint32 oldLayout, uint32 newLayout, uint32 aspect );

        /**
         * @brief 텍스처를 `targetLayout` 으로 전이합니다 — **확인·배리어·기록이 한 덩어리입니다.**
         * @details 예전엔 호출 지점마다 "현재 레이아웃을 읽고 → 배리어를 쏘고 → 레코드를 갱신" 을
         *          손으로 복사해 뒀다(7곳). 병렬 패스 기록에서는 그 셋이 갈라지면 두 스레드가 같은
         *          "이전 레이아웃" 을 보고 각자 배리어를 쏘거나, 한쪽이 이미 바꿔 놓은 뒤라 실제
         *          레이아웃과 기록이 어긋난다 — `vkQueueSubmit` 이 "이 이미지가 X 레이아웃일 것으로
         *          기대했는데 아니다" 로 거부한다.
         * @note 이미 그 레이아웃이면 아무것도 하지 않습니다.
         */
        void transitionTextureLayout( VkCommandBuffer cmd, VulkanTextureRecord& record, uint32 targetLayout, uint32 aspect );
        /** @brief 오프스크린 VkRenderPass를 확보합니다. */
        bool ensureOffscreenRenderPass( uint32 vkFormat );
        /** @brief 오프스크린 텍스처용 프레임버퍼를 만듭니다. */
        bool createOffscreenFramebuffer( VulkanTextureRecord& record );
        /** @brief 오프스크린 프레임버퍼를 파괴합니다. */
        void destroyOffscreenFramebuffer( VulkanTextureRecord& record );
        /** @brief 프레임버퍼(및 이 프레임버퍼가 소유한 렌더패스)를 GPU 펜스 통과 후 파괴하도록 큐에 넣습니다. */
        void enqueueFramebufferRelease( VkFramebuffer framebuffer, VkRenderPass ownedRenderPass );
        /** @brief 파이프라인용 VkRenderPass를 확보합니다. */
        VkRenderPass ensurePipelineRenderPass( const RHIPipelineStateDesc& desc );
        /** @brief 합성 프레임버퍼를 확보합니다. */
        bool ensureCompositeFramebuffer( const CompositeFbKey& key, CompositeFbRecord& outRecord );
        /** @brief 해당 뷰를 쓰는 합성 프레임버퍼를 파괴합니다. */
        void destroyCompositeFramebuffersUsing( RHITextureHandle texture );
        /** @brief usageFlags로 VkBuffer를 만들고 초기 데이터를 올립니다. */
        RHIBufferHandle createVulkanBuffer( uint32 sizeBytes, uint32 usageFlags, const void* pInitialData );

        VkInstance               _instance;
        VkDebugUtilsMessengerEXT _debugMessenger;
        VkPhysicalDevice         _physicalDevice;
        VkDevice                 _device;
        VkQueue                  _graphicsQueue;
        uint32                   _graphicsQueueFamilyIndex;

        /// @brief 창 하나의 서피스·스왑체인·백버퍼. 이미지/뷰/프레임버퍼/세마포어가 전부 여기 있다.
        VulkanRHISwapChain _swapChain;

        VkRenderPass _renderPass;           ///< 스왑체인 렌더패스 (loadOp=CLEAR)
        VkRenderPass _renderPassLoad;       ///< 같은 스왑체인 렌더패스의 loadOp=LOAD 변종. 한 프레임에 백버퍼를
                                            ///< 두 번 이상 열 때(그래프가 그린 뒤 UI 를 얹을 때) 앞의 내용을 보존한다.
        VkRenderPass  _offscreenRenderPass; ///< R8G8B8A8_UNORM color-only pass for Game View / RT draws
        VkCommandPool _commandPool;

        /// @brief 컴포지트 프레임버퍼 캐시 보호용. RenderGraph::executeParallel 이 여러 스레드에서
        ///        동시에 beginRenderPass 를 부르면 이 맵에 동시 삽입이 일어난다.
        mutable mutex _compositeFbMutex;
        /// @brief 텍스처 레이아웃 확인+전이 보호용 — transitionTextureLayout 참고.
        mutable mutex _imageLayoutMutex;
        /// @brief 리스트에 빌려주는 (풀, 버퍼) 쌍의 재사용 풀. 펜스를 통과한 것만 들어 있다.
        mutable mutex                  _cmdListPoolMutex;
        vector<VulkanCommandListEntry> _listFreeCmdListEntry;
        /// @brief 이번 프레임에 큐에 넣을 커맨드 버퍼들 — 기록 순서 = 실행 순서.
        vector<VkCommandBuffer> _listPendingSubmit;
        /// @brief 프레임 스트림을 리스트 제출 지점마다 잘라 쓰는 추가 세그먼트 버퍼(프레임 슬롯별 재사용).
        vector<VkCommandBuffer> _arrFrameSegment[constant::kMaxFrameCountInFlight];
        uint32                  _frameSegmentCursor{ 0 };
        /// @brief 이번 프레임의 acquire 세마포어 대기가 아직 소비되지 않았는가 (첫 제출만 건다).
        uint8 _bFrameAcquireWaitPending{ 0 };
        /// @brief b1(MaterialCB) 푸시 상수 경로 안내를 한 번만 남기기 위한 래치.
        uint8 _bMaterialCbSlotWarned{ 0 };
        /// @brief 지금 기록 중인 프레임 세그먼트. beginFrame 이 첫 세그먼트로 세운다.
        VkCommandBuffer _activeFrameBuffer{ nullptr };

        vector<VkCommandBuffer> _listCommandBuffer;
        vector<VkFence>         _listInFlightFence;
        /// @brief 스왑체인 이미지 인덱스별로, 그 이미지를 마지막으로 쓴 인플라이트 펜스(소유하지 않는 사본).
        vector<VkFence> _listImagesInFlight;
        /// @brief 링 슬롯별로 마지막 제출에 매긴 _frameFenceCounter 값 — beginFrame이 그 슬롯의 펜스를
        ///        기다린 뒤 _releaseQueue.tickCompleted(이 값)을 불러 실제 GPU 완료를 확인하고 해제한다.
        vector<uint64> _listRingFrameNumber;

        void* _pHWnd;
        void* _pDisplayHandle;
        /// @brief 인플라이트 프레임 슬롯(0..kMaxFrameCountInFlight-1). 스왑체인 이미지 인덱스와 **다른 값**이다 —
        ///        이미지 인덱스는 스왑체인이 acquire 로 받아 들고 있다(`_swapChain.getImageIndex()`).
        uint32 _currentFrame;
        /// @brief 스왑체인 제출마다 1씩 증가하는 단조 세대 번호. 해제 큐가 실제 GPU 펜스 완료 기준으로
        ///        해제하도록(enqueueGpuRelease) 프레임 카운트 대신 이 값을 쓴다.
        uint64                  _frameFenceCounter;
        uint32                  _width;
        uint32                  _height;
        uint32                  _depthFormat; ///< VkFormat — createTexture2D/RP 공통 depth
        uint16                  _bFrameStarted           : 1;
        uint16                  _bEnableValidationLayers : 1;
        uint16                  _bMultiDrawIndirect      : 1;
        uint16                  _bDrawIndirectCount      : 1;
        uint16                  _bSwapChainDirty         : 1; ///< resize/present 결과로 예약된 스왑체인 재생성 요청
        uint16                  _bDepthHasStencil        : 1; ///< _depthFormat에 stencil plane 포함
        uint16                  _linuxWsi                : 2; ///< 0=없음, 1=xlib, 2=xcb (Linux만)
        [[maybe_unused]] uint16 _reservedVulkan          : 6;

        VkSampler _defaultSampler;

        VkPipelineLayout      _pipelineLayout;
        VkDescriptorSetLayout _descriptorSetLayout;
        VkDescriptorSetLayout _uavDescriptorSetLayout;
        VkDescriptorPool      _descriptorPool;
        VkDescriptorSet       _descriptorSet;
        /// @brief set 4 = 정적 샘플러(VK_DESCRIPTOR_TYPE_SAMPLER, immutable). binding.hlsli 의
        ///        `g_SwSamplerLinearWrap` 이 매 드로우 set 0/1 과 함께 바인딩된다 (bindGraphicsMaterialSets).
        VkSampler             _staticSamplerLinearWrap;
        VkDescriptorSetLayout _samplerSetLayout;
        VkDescriptorSet       _staticSamplerSet;
        VkBuffer              _dummyUBO;
        VkDeviceMemory        _dummyUBOMemory;
        VkPipeline            _pipeline;
        VkPipeline            _offscreenPipeline; ///< Same shaders as `_pipeline`, bound to `_offscreenRenderPass`
        VkBuffer              _vertexBuffer;      ///< 풀스크린 포스트 (정점 3개)
        vector<uint32>        _listBindlessFree;

        RHIHandleTable<VulkanBufferRecord>     _gpuBuffers;
        unordered_map<RHIBufferHandle, uint32> _mapCbSlotSize;
        /// @brief 즉시 컨텍스트(스왑체인 begin/end·오프스크린)가 쓰는 기록 상태. 리스트 기반 기록은
        /// 각자 자기 것을 갖는다 — 여기 있는 건 "디바이스가 직접 여는 버퍼" 전용이다.
        VulkanRecordingState _recordingState;
        /// @brief 디스크립터 레지스트리 보호용. 커맨드 기록 경로가 인덱스로 이 목록들을 읽는 동안
        /// register/unregister 가 resize 로 재할당하면 기록 스레드가 쓰레기를 읽는다
        /// (gv_useRenderThread 기본 true 라 렌더/게임 스레드 사이에서 이미 성립하는 레이스).
        /// 읽기는 공유 락이라 서로를 막지 않는다.
        mutable std::shared_mutex _bindlessMutex;

        vector<VkDescriptorSet> _listRegisteredDescriptorSet;
        /// @brief 링 상수버퍼용 프레임별 디스크립터 셋. 인덱스 i 의 셋들은
        ///        [i * kMaxFrameCountInFlight, (i+1) * kMaxFrameCountInFlight) 구간에 놓이고,
        ///        각각 자기 프레임 슬롯 오프셋을 가리키도록 등록 시점에 한 번만 기록된다.
        ///        링이 아닌(= 구조버퍼 등) 인덱스 구간은 VK_NULL_HANDLE 로 남는다.
        vector<VkDescriptorSet> _listRegisteredCbSetRing;
        vector<RHIBufferHandle> _listBindlessSourceBuffer;
        vector<VkDescriptorSet> _listRegisteredUAV;
        vector<RHIBufferHandle> _listUavSourceBuffer;
        vector<uint32>          _listUavFree;

        RHIHandleTable<VulkanTextureRecord> _gpuTextures;
        RHIReleaseQueue                     _releaseQueue;

        unordered_map<CompositeFbKey, CompositeFbRecord, CompositeFbKeyHash> _mapCompositeFramebuffer;
        unordered_map<PipelineRpKey, VkRenderPass, PipelineRpKeyHash>        _mapPipelineRenderPass;

        VkDescriptorSetLayout   _textureDescriptorSetLayout;
        vector<VkDescriptorSet> _listRegisteredTexture; ///< 레거시 텍스처별 set (슬롯 바인드 폴백)
        vector<uint32>          _listTextureFree;

        VkDescriptorSetLayout _bindlessTextureArrayLayout;
        VkDescriptorSet       _bindlessTextureSet;
        VkImage               _bindlessDummyImage;
        VkImageView           _bindlessDummyView;
        VkDeviceMemory        _bindlessDummyMemory;

        RHIHandleTable<VulkanPipelineStateRecord> _pipelineStates;
        vector<VulkanRenderPassRecord>            _listRenderPass;
        VkPipelineCache                           _pipelineCache;

        sw::unique_ptr<VulkanRHICommandContext> _frameStreamContext;
        sw::unique_ptr<VulkanRHIResource>       _resourceImpl;
    };
} // namespace sw
