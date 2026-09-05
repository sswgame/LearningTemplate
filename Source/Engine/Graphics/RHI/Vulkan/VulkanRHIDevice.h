/**
 * @file VulkanRHIDevice.h
 * @brief Vulkan 1.3 API 기반 RHI 백엔드 클래스 정의 및 C 타입 전방 선언
 */
#pragma once
#include "Engine/EngineMinimal.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/RHIHandleTable.h"
#include "Engine/Graphics/RHI/RHIReleaseQueue.h"

#include <shared_mutex>

/** @brief SW_VK_DEFINE_HANDLE 매크로 정의입니다. */

#define SW_VK_DEFINE_HANDLE( object ) typedef struct object##_T* object;
SW_VK_DEFINE_HANDLE( VkInstance )
SW_VK_DEFINE_HANDLE( VkPhysicalDevice )
SW_VK_DEFINE_HANDLE( VkDevice )
SW_VK_DEFINE_HANDLE( VkQueue )
SW_VK_DEFINE_HANDLE( VkRenderPass )
SW_VK_DEFINE_HANDLE( VkSurfaceKHR )
SW_VK_DEFINE_HANDLE( VkSwapchainKHR )
SW_VK_DEFINE_HANDLE( VkImage )
SW_VK_DEFINE_HANDLE( VkImageView )
SW_VK_DEFINE_HANDLE( VkFramebuffer )
SW_VK_DEFINE_HANDLE( VkCommandPool )
SW_VK_DEFINE_HANDLE( VkCommandBuffer )
SW_VK_DEFINE_HANDLE( VkSemaphore )
SW_VK_DEFINE_HANDLE( VkFence )
SW_VK_DEFINE_HANDLE( VkPipelineLayout )
SW_VK_DEFINE_HANDLE( VkDescriptorSetLayout )
SW_VK_DEFINE_HANDLE( VkDescriptorPool )
SW_VK_DEFINE_HANDLE( VkDescriptorSet )
SW_VK_DEFINE_HANDLE( VkBuffer )
SW_VK_DEFINE_HANDLE( VkDeviceMemory )
SW_VK_DEFINE_HANDLE( VkPipeline )
SW_VK_DEFINE_HANDLE( VkPipelineCache )
SW_VK_DEFINE_HANDLE( VkDebugUtilsMessengerEXT )
SW_VK_DEFINE_HANDLE( VkSampler )

namespace sw
{
    class VulkanRHICommandContext;
    class VulkanRHIResource;
    class VulkanRHISwapChain;

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
        /// @brief 이번 프레임에 스왑체인 렌더패스를 이미 한 번 열었는가(=clear 를 이미 했는가).
        /// 첫 오픈만 CLEAR 변형을 쓰고 이후 재오픈은 LOAD 변형을 써야 이미 그린 내용이 안 지워진다.
        uint8 _bSwapchainCleared : 1;
        /// @brief 이 스트림에 오프스크린 패스가 열려 있는가.
        uint8 _bOffscreenPassActive : 1;
        /// @brief set 1(bindless 텍스처)·set 4(정적 샘플러)를 이 버퍼에 이미 바인딩했는가.
        uint8                  _bStaticGraphicsSetsBound : 1;
        [[maybe_unused]] uint8 _reserved                 : 4;

        RHITextureHandle       _activeOffscreenTarget{ 0 };
        RHIPipelineStateHandle _activeGraphicsPso{ 0 };

        RHIBufferHandle _boundMeshVb{ 0 };
        uint32          _boundMeshStride{ sizeof( RHIVertex ) };
        uint32          _boundMeshOffset{ 0 };
        RHIBufferHandle _boundIndexBuffer{ 0 };
        uint32          _boundIndexStride{ 4 };
        uint32          _boundIndexOffset{ 0 };

        /// @brief 마지막으로 바인딩한 set 0(PassCB) — 인덱스가 실제로 바뀔 때만 재바인딩하기 위한 캐시.
        VkDescriptorSet _lastBoundGraphicsSet0{ nullptr };

        /** @brief 아무것도 안 걸린 상태로 시작합니다. */
        VulkanRecordingState()
            : _bRenderPassActive{ 0 }
            , _bSwapchainCleared{ 0 }
            , _bOffscreenPassActive{ 0 }
            , _bStaticGraphicsSetsBound{ 0 }
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

    public:
        friend class VulkanRHISwapChain;
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
        void resize( uint32 width, uint32 height );
        bool createRenderPass();
        /** @brief 프레임 시작 (vkAcquireNextImageKHR 및 커맨드버퍼 기록 시작) */
        void beginFrame( const float4& clearColor );

        /** @brief 프레임 종료 (vkQueueSubmit 및 vkQueuePresentKHR 제출) */
        void endFrame( bool vsync, bool bPresent = true );

        /** @brief 오프스크린 패스를 시작합니다. */
        IRHISwapChain* getSwapChain() override;
        IRHIResource*  getResource() override;
        /** @brief Present/offscreen/replay Immediate Context. */
        IRHICommandContext* getImmediateContext() override;
        /** @brief Mode=Deferred CL 바인딩용 soft Deferred Context. */
        IRHICommandContext* getDeferredCommandContext() override;

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
        void* getNativeSwapChain() const override { return _swapChain; }

        /** @brief Native VkQueue 핸들 반환 */
        void* getNativeCommandQueue() const override { return _graphicsQueue; }

        /** @brief Vulkan 그래픽스 파이프라인(VkPipeline) 생성 */

        void drawIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset = 0,
                           RHIDescriptorIndex materialDescriptorIndex = kInvalidDescriptorIndex );

        /** @brief vkCmdDrawIndexedIndirect 실행 */

        void multiDrawIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset, uint32 maxCommandCount,
                                RHIBufferHandle countBuffer = 0, uint32 countBufferOffset = 0 );

        /** @brief GPU 이벤트 디버그 마커 시작 */

        unique_ptr<IRHICommandList> createCommandList( RHICommandListMode mode ) override;

        /** @brief 커맨드 리스트 실행 제출 */
        void executeCommandList( IRHICommandList* pCmdList ) override;

        /** @brief ImGui용 네이티브 Vulkan 핸들을 조회합니다. */
        VkInstance       getInstance() const { return _instance; }
        VkPhysicalDevice getPhysicalDevice() const { return _physicalDevice; }
        VkDevice         getDevice() const { return _device; }
        VkRenderPass     getRenderPass() const { return _renderPass; }

        bool queryVulkanImGuiNative( RHIVulkanImGuiNative& out ) const override
        {
            out._pInstance       = _instance;
            out._pPhysicalDevice = _physicalDevice;
            out._pDevice         = _device;
            out._pGraphicsQueue  = _graphicsQueue;
            out._pRenderPass     = _renderPass;
            out._queueFamily     = _graphicsQueueFamilyIndex;
            out._minImageCount   = 2;
            out._imageCount      = static_cast<uint32>( _listSwapChainImage.empty() ? 2 : _listSwapChainImage.size() );
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
         * @brief 윈도우 서피스를 만듭니다.
         */
        bool createSurface();
        /**
         * @brief 물리 디바이스를 선택합니다
         */
        bool pickPhysicalDevice();
        /**
         * @brief 논리 디바이스와 큐를 만듭니다.
         */
        bool createLogicalDevice();
        /**
         * @brief 스왑체인을 생성합니다
         */
        bool createSwapChain();
        /**
         * @brief 스왑체인 이미지 뷰를 만듭니다.
         */
        bool createImageViews();
        /**
         * @brief 스왑체인 프레임버퍼를 만듭니다.
         */
        bool createFramebuffers();
        /**
         * @brief 그래픽스 커맨드 풀을 만듭니다.
         */
        bool createCommandPool();
        /**
         * @brief 프레임별 커맨드 버퍼를 할당합니다.
         */
        bool createCommandBuffers();
        /**
         * @brief 세마포어/펜스를 만듭니다.
         */
        bool createSyncObjects();
        /**
         * @brief 세마포어/펜스를 파괴하고 컨테이너를 비웁니다.
         */
        void destroySyncObjects();
        /**
         * @brief GPU가 지원하는 depth/stencil 포맷을 선택합니다.
         */
        bool selectDepthFormat();
        /**
         * @brief 풀스크린 삼각형 버텍스 버퍼를 만듭니다.
         */

        /**
         * @brief 스왑체인을 재생성합니다
         */
        void recreateSwapChain();
        /**
         * @brief 스왑체인을 정리합니다
         */
        void cleanupSwapChain();

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
        /**
         * @brief 스왑체인 렌더패스가 안 열려 있으면 엽니다(프레임 첫 오픈만 CLEAR, 이후는 LOAD).
         * @details `beginFrame` 이 프레임 내내 렌더패스를 물고 있으면 패스별 secondary 커맨드 버퍼를
         *          `vkCmdExecuteCommands` 로 끼울 수 없어서, 실제로 스왑체인에 그릴 때 여기서 연다.
         */
        void ensureSwapchainRenderPass( VkCommandBuffer cmd, VulkanRecordingState& state );

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
        /** @brief 오프스크린 VkRenderPass를 확보합니다. */
        bool ensureOffscreenRenderPass( uint32 vkFormat );
        /** @brief 오프스크린 텍스처용 프레임버퍼를 만듭니다. */
        bool createOffscreenFramebuffer( VulkanTextureRecord& record );
        /** @brief 오프스크린 프레임버퍼를 파괴합니다. */
        void destroyOffscreenFramebuffer( VulkanTextureRecord& record );
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
        VkSurfaceKHR             _surface;
        VkPhysicalDevice         _physicalDevice;
        VkDevice                 _device;
        VkQueue                  _graphicsQueue;
        uint32                   _graphicsQueueFamilyIndex;

        VkSwapchainKHR        _swapChain;
        vector<VkImage>       _listSwapChainImage;
        uint32                _swapChainImageFormat;
        uint32                _swapChainExtentWidth;
        uint32                _swapChainExtentHeight;
        vector<VkImageView>   _listSwapChainImageView;
        vector<VkFramebuffer> _listSwapChainFramebuffer;

        VkRenderPass _renderPass;
        /// @brief beginFrame 이 받아둔 스왑체인 clear 색 — 실제 오픈 시점에 쓴다.
        float4 _swapchainClearColor{};
        /// @brief 스왑체인 렌더패스의 LOAD 변형 — 한 프레임에 두 번째 이후로 열 때 쓴다(내용 보존).
        VkRenderPass  _renderPassLoad;
        VkRenderPass  _offscreenRenderPass; ///< R8G8B8A8_UNORM color-only pass for Game View / RT draws
        VkCommandPool _commandPool;

        vector<VkCommandBuffer> _listCommandBuffer;
        vector<VkSemaphore>     _listImageAvailableSemaphore;
        vector<VkSemaphore>     _listRenderFinishedSemaphore;
        vector<VkFence>         _listInFlightFence;
        vector<VkFence>         _listImagesInFlight;
        /// @brief 링 슬롯별로 마지막 제출에 매긴 _frameFenceCounter 값 — beginFrame이 그 슬롯의 펜스를
        ///        기다린 뒤 _releaseQueue.tickCompleted(이 값)을 불러 실제 GPU 완료를 확인하고 해제한다.
        vector<uint64> _listRingFrameNumber;

        void*  _pHWnd;
        void*  _pDisplayHandle;
        uint32 _currentFrame;
        uint32 _imageIndex;
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

        VkCommandBuffer _offscreenCommandBuffer;
        VkFence         _offscreenFence;
        VkSampler       _defaultSampler;

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

        sw::unique_ptr<VulkanRHICommandContext> _immContext;
        sw::unique_ptr<VulkanRHICommandContext> _deferredContext;
        sw::unique_ptr<VulkanRHISwapChain>      _swapChainImpl;
        sw::unique_ptr<VulkanRHIResource>       _resourceImpl;
    };
} // namespace sw
