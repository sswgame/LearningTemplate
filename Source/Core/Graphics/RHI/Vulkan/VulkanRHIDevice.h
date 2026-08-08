#pragma once

#include "Core/CoreMinimal.h"
#include "Core/Graphics/RHI/IRHIDevice.h"
#include "Core/Common/PlatformHeaders.h"
#include "Core/Common/CommonHeaders.h"

/**
 * @file VulkanRHIDevice.h
 * @brief Vulkan 1.3 API 기반 RHI 백엔드 클래스 정의 및 C 타입 전방 선언
 */

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
SW_VK_DEFINE_HANDLE( VkDebugUtilsMessengerEXT )
SW_VK_DEFINE_HANDLE( VkSampler )

namespace sw
{
	/**
	 * @class VulkanRHIDevice
	 * @brief Vulkan 1.3 그래픽스 및 컴퓨트 디바이스 구현체 (Descriptor Indexing / Bindless 지원)
	 */
	class VulkanRHIDevice : public IRHIDevice
	{
	public:
		VulkanRHIDevice();
		~VulkanRHIDevice() override;

		/** @brief Vulkan 인스턴스, 서피스, 디바이스, 스왑체인, 커맨드풀 및 동기화 객체 생성 */
		virtual bool initializeInternal( const RHISwapChainDesc& desc ) override;

		/** @brief Vulkan 파이프라인 및 자원 해제 */
		virtual void shutdownInternal() override;

		/** @brief Vulkan vkDeviceWaitIdle 실행 */
		virtual void waitIdle() override;

		/** @brief 스왑체인 재창조 */
		void resize( uint32 width, uint32 height ) override;

		/** @brief 프레임 시작 (vkAcquireNextImageKHR 및 커맨드버퍼 기록 시작) */
		void beginFrame( float32 clearColor[4] ) override;

		/** @brief 프레임 종료 (vkQueueSubmit 및 vkQueuePresentKHR 제출) */
		void endFrame( bool vsync ) override;

		void beginOffscreenPass( RHITextureHandle colorTarget, float32 clearColor[4] ) override;
		void endOffscreenPass( RHITextureHandle colorTarget ) override;

		/** @brief 백엔드 타입 반환 (Vulkan) */
		RHIBackend getBackendType() const override { return RHIBackend::Vulkan; }

		bool supportsBindless() const override { return true; }

		/** @brief 백엔드 버전 문자열 반환 */
		const utf8* getBackendName() const override { return "Vulkan 1.3"; }

		/** @brief Native VkDevice 핸들 반환 */
		void* getNativeDevice() const override { return reinterpret_cast<void*>( _device ); }

		/** @brief Native VkCommandBuffer 핸들 반환 */
		void* getNativeContext() const override
		{
			VkCommandBuffer cmd = currentCommandBuffer();
			return reinterpret_cast<void*>( cmd );
		}

		/** @brief Native VkSwapchainKHR 핸들 반환 */
		void* getNativeSwapChain() const override { return reinterpret_cast<void*>( _swapChain ); }

		/** @brief Native VkQueue 핸들 반환 */
		void* getNativeCommandQueue() const override { return reinterpret_cast<void*>( _graphicsQueue ); }

		/** @brief Vulkan 그래픽스 파이프라인(VkPipeline) 생성 */
		RHIPipelineStateHandle createPipelineState( const RHIPipelineStateDesc& desc ) override;

		/** @brief Vulkan 컴퓨트 파이프라인 생성 */
		RHIPipelineStateHandle createComputePipelineState( const std::string& shaderPath, const std::string& entryPoint = "CSMain" ) override;

		/** @brief 파이프라인 상태 해제 */
		void destroyPipelineState( RHIPipelineStateHandle pso ) override;

		/** @brief 그래픽스 파이프라인 바인딩 */
		void setPipelineState( RHIPipelineStateHandle pso ) override;

		/** @brief 컴퓨트 파이프라인 바인딩 */
		void setComputePipelineState( RHIPipelineStateHandle pso ) override;

		/** @brief Vulkan VkRenderPass 생성 */
		RHIRenderPassHandle createRenderPass( const RHIRenderPassDesc& desc ) override;

		/** @brief 렌더 패스 해제 */
		void destroyRenderPass( RHIRenderPassHandle pass ) override;

		/** @brief 렌더 패스 커맨드 기록 시작 (vkCmdBeginRenderPass) */
		void beginRenderPass( const RHIRenderPassBeginInfo& beginInfo ) override;

		/** @brief 렌더 패스 기록 종료 (vkCmdEndRenderPass) */
		void endRenderPass() override;

		/** @brief Vulkan UBO (Uniform Buffer Object) 생성 */
		RHIBufferHandle createConstantBuffer( uint32 size ) override;

		/** @brief UBO 메모리 Map/Unmap 갱신 */
		void updateConstantBuffer( RHIBufferHandle buffer, const void* data, uint32 size ) override;

		/** @brief Vulkan SSBO (Shader Storage Buffer Object) 생성 */
		RHIBufferHandle createStructuredBuffer( uint32 elementSize, uint32 elementCount ) override;

		/** @brief SSBO 메모리 Map/Unmap 갱신 */
		void updateStructuredBuffer( RHIBufferHandle buffer, const void* data, uint32 size ) override;

		/** @brief Vulkan VkBuffer 및 메모리 해제 */
		void destroyBuffer( RHIBufferHandle buffer ) override;

		RHITextureHandle   createTexture2D( const RHITextureDesc& desc ) override;
		void			   destroyTexture( RHITextureHandle texture ) override;
		RHIDescriptorIndex registerBindlessTexture( RHITextureHandle texture ) override;

		/** @brief Descriptor Set 내 Bindless UBO/SSBO 바인딩 등록 */
		RHIDescriptorIndex registerBindlessResource( RHIBufferHandle buffer ) override;

		/** @brief Bindless 리소스 등록 해제 */
		void unregisterBindlessResource( RHIDescriptorIndex index ) override;

		/** @brief Descriptor Set 내 Bindless Storage Buffer (UAV) 등록 */
		RHIDescriptorIndex registerBindlessUAV( RHIBufferHandle buffer ) override;

		/** @brief Bindless UAV 등록 해제 */
		void unregisterBindlessUAV( RHIDescriptorIndex index ) override;

		/** @brief Explicit Set Slot 내 UAV 바인딩 */
		void bindComputeUAV( RHIDescriptorIndex index, uint32 slot ) override;

		/** @brief 삼각형 그리기 (vkCmdDraw) */
		void drawTriangle( RHIDescriptorIndex materialDescriptorIndex ) override;

		/** @brief vkCmdDispatch 컴퓨트 디스패치 실행 */
		void dispatchCompute( uint32 threadGroupCountX, uint32 threadGroupCountY, uint32 threadGroupCountZ ) override;

		/** @brief vkCmdSetViewport */
		void setViewport( const RHIViewport& viewport ) override;

		/** @brief vkCmdPushConstants 푸시 상수 설정 */
		void setComputeRootConstants( uint32 rootParameterIndex, uint32 num32BitValues, const void* data, uint32 destOffsetIn32BitValues = 0 ) override;

		/** @brief vkCmdDrawIndirect 실행 */
		void drawIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset = 0 ) override;

		/** @brief vkCmdDispatchIndirect 실행 */
		void dispatchIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset = 0 ) override;

		/** @brief GPU 이벤트 디버그 마커 시작 */
		void beginEventMarker( const utf8* name ) override;

		/** @brief GPU 이벤트 디버그 마커 종료 */
		void endEventMarker() override;

		/** @brief 커맨드 리스트 객체 생성 */
		std::unique_ptr<IRHICommandList> createCommandList() override;

		/** @brief 커맨드 리스트 실행 제출 */
		void executeCommandList( IRHICommandList* cmdList ) override;

		VkInstance		 getInstance() const { return _instance; }
		VkPhysicalDevice getPhysicalDevice() const { return _physicalDevice; }
		VkDevice		 getDevice() const { return _device; }
		VkQueue			 getGraphicsQueue() const { return _graphicsQueue; }
		VkRenderPass	 getRenderPass() const { return _renderPass; }

		bool queryVulkanImGuiNative( RHIVulkanImGuiNative& out ) const override
		{
			out._instance		= _instance;
			out._physicalDevice = _physicalDevice;
			out._device			= _device;
			out._graphicsQueue	= _graphicsQueue;
			out._renderPass		= _renderPass;
			out._queueFamily	= _graphicsQueueFamilyIndex;
			out._minImageCount	= 2;
			out._imageCount		= static_cast<uint32>( _swapChainImages.empty() ? 2 : _swapChainImages.size() );
			return _device != nullptr;
		}

		bool queryVulkanTextureView( RHITextureHandle texture, void*& outImageView ) const override;

	private:
		/**
		 * @brief Vulkan validation layer 지원을 확인합니다
		 */
		bool checkValidationLayerSupport();
		/**
		 * @brief Instance을(를) 생성합니다
		 */
		bool createInstance();
		/**
		 * @brief 디버그 메신저를 설정합니다
		 */
		void setupDebugMessenger();
		/**
		 * @brief Surface을(를) 생성합니다
		 */
		bool createSurface();
		/**
		 * @brief 물리 디바이스를 선택합니다
		 */
		bool pickPhysicalDevice();
		/**
		 * @brief LogicalDevice을(를) 생성합니다
		 */
		bool createLogicalDevice();
		/**
		 * @brief 스왑체인을 생성합니다
		 */
		bool createSwapChain();
		/**
		 * @brief ImageViews을(를) 생성합니다
		 */
		bool createImageViews();
		/**
		 * @brief RenderPass을(를) 생성합니다
		 */
		bool createRenderPass();
		/**
		 * @brief Framebuffers을(를) 생성합니다
		 */
		bool createFramebuffers();
		/**
		 * @brief CommandPool을(를) 생성합니다
		 */
		bool createCommandPool();
		/**
		 * @brief CommandBuffers을(를) 생성합니다
		 */
		bool createCommandBuffers();
		/**
		 * @brief SyncObjects을(를) 생성합니다
		 */
		bool createSyncObjects();
		/**
		 * @brief TriangleResources을(를) 생성합니다
		 */
		bool createTriangleResources();

		/**
		 * @brief 스왑체인을 재생성합니다
		 */
		void recreateSwapChain();
		/**
		 * @brief 스왑체인을 정리합니다
		 */
		void cleanupSwapChain();

		/**
		 * @brief 메모리 타입을 찾습니다
		 */
		uint32 findMemoryType( uint32 typeFilter, uint32 properties );

	private:
		VkInstance				 _instance				   = nullptr;
		VkDebugUtilsMessengerEXT _debugMessenger		   = nullptr;
		VkSurfaceKHR			 _surface				   = nullptr;
		VkPhysicalDevice		 _physicalDevice		   = nullptr;
		VkDevice				 _device				   = nullptr;
		VkQueue					 _graphicsQueue			   = nullptr;
		uint32					 _graphicsQueueFamilyIndex = 0;

		VkSwapchainKHR			   _swapChain = nullptr;
		std::vector<VkImage>	   _swapChainImages;
		uint32					   _swapChainImageFormat  = 0;
		uint32					   _swapChainExtentWidth  = 0;
		uint32					   _swapChainExtentHeight = 0;
		std::vector<VkImageView>   _swapChainImageViews;
		std::vector<VkFramebuffer> _swapChainFramebuffers;

		VkRenderPass  _renderPass  = nullptr;
		VkCommandPool _commandPool = nullptr;

		std::vector<VkCommandBuffer> _commandBuffers;
		std::vector<VkSemaphore>	 _imageAvailableSemaphores;
		std::vector<VkSemaphore>	 _renderFinishedSemaphores;
		std::vector<VkFence>		 _inFlightFences;
		std::vector<VkFence>		 _imagesInFlight;

		void*				   _hWnd		  = nullptr;
		void*				   _displayHandle = nullptr;
		uint32				   _currentFrame  = 0;
		uint32				   _imageIndex	  = 0;
		uint32				   _width		  = 0;
		uint32				   _height		  = 0;
		uint8				   _bFrameStarted			: 1;
		uint8				   _bOffscreenPassActive	: 1;
		uint8				   _bEnableValidationLayers : 1;
		[[maybe_unused]] uint8 _reservedFlags			: 5;

		VkCommandBuffer _offscreenCommandBuffer = nullptr;
		VkFence			_offscreenFence			= nullptr;
		VkSampler		_defaultSampler			= nullptr;

		VkPipelineLayout	  _pipelineLayout				  = nullptr;
		VkDescriptorSetLayout _descriptorSetLayout			  = nullptr;
		VkDescriptorSetLayout _uavDescriptorSetLayout		  = nullptr;
		VkDescriptorSetLayout _explicitUavDescriptorSetLayout = nullptr;
		VkDescriptorPool	  _descriptorPool				  = nullptr;
		VkDescriptorSet		  _descriptorSet				  = nullptr;
		VkBuffer			  _dummyUBO						  = nullptr;
		VkDeviceMemory		  _dummyUBOMemory				  = nullptr;
		VkPipeline			  _pipeline						  = nullptr;
		VkBuffer			  _vertexBuffer					  = nullptr;
		VkDeviceMemory		  _vertexBufferMemory			  = nullptr;
		std::vector<uint32>	  _bindlessFreeList;
		struct VulkanBufferRecord
		{
			VkBuffer	   buffer = nullptr;
			VkDeviceMemory memory = nullptr;
			uint32		   size	  = 0;
		};

		std::vector<VulkanBufferRecord> _allocatedBuffers;
		std::vector<VkDescriptorSet>	_registeredDescriptorSets;
		std::vector<VkDescriptorSet>	_registeredUAVs;
		std::vector<uint32>				_uavFreeList;

		struct VulkanTextureRecord
		{
			VkImage			   image		 = nullptr;
			VkImageView		   imageView	 = nullptr;
			VkDeviceMemory	   memory		 = nullptr;
			VkFramebuffer	   framebuffer	 = nullptr;
			VkRenderPass	   renderPass	 = nullptr;
			uint32			   format		 = 0; ///< VkFormat
			uint32			   layout		 = 0; ///< VkImageLayout (UNDEFINED=0)
			uint32			   width		 = 0;
			uint32			   height		 = 0;
			uint8			   bRenderTarget : 1;
			uint8			   bDepthStencil : 1;
			uint8			   reserved		 : 6;
			RHIDescriptorIndex bindlessIndex = kInvalidDescriptorIndex;
		};
		std::unordered_map<RHITextureHandle, VulkanTextureRecord> _textures;
		uint64													  _nextTextureId = 1;

		VkDescriptorSetLayout		 _textureDescriptorSetLayout = nullptr;
		std::vector<VkDescriptorSet> _registeredTextures;
		std::vector<uint32>			 _textureFreeList;

		struct VulkanPipelineStateRecord
		{
			VkPipeline pipeline = nullptr;
		};

		struct VulkanRenderPassRecord
		{
			VkRenderPass renderPass = nullptr;
		};

		std::vector<VulkanPipelineStateRecord> _pipelineStates;
		std::vector<VulkanRenderPassRecord>	   _renderPasses;

		VkCommandBuffer currentCommandBuffer() const;
		bool			transitionImageLayout( VkCommandBuffer cmd, VkImage image, uint32 oldLayout, uint32 newLayout, uint32 aspect );
		bool			createOffscreenFramebuffer( VulkanTextureRecord& record );
		void			destroyOffscreenFramebuffer( VulkanTextureRecord& record );
	};
} // namespace sw
