/**
 * @file IRHIDevice.h
 * @brief RHI 커맨드 리스트 기록과 디바이스 추상화
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Graphics/RHI/RHICapabilities.h"

namespace sw
{
	class IWindow;
	class IRHICommandContext;
	class IRHICommandList;
	class IRHIResource;
	class IRHISwapChain;
	class RHIDeferredCommandList;
	class RenderPassManager;

	/**
	 * @class IRHIDevice
	 * @brief DX11/DX12/Vulkan/OpenGL 하드웨어 디바이스 추상화
	 * @details Context(제출/기록 슬롯)와 CommandList Mode(언제 replay/flush할지)는 별개다.
	 *          Immediate Context = present/offscreen/Deferred CL replay 대상.
	 *          Deferred Context = Mode=Deferred일 때 CL 바인딩용(soft 또는 네이티브).
	 */
	class SW_API IRHIDevice
	{
	public:
		// ------------------------------------------------------------------------------
		// 8) 수명 — initialize/shutdown, resize
		// ------------------------------------------------------------------------------
		/** @brief 가상 소멸. */
		virtual ~IRHIDevice();
		/** @brief 빈 디바이스. initialize 전에 setInitWindow. */
		IRHIDevice();
		/** @brief 복사를 금지합니다. */
		IRHIDevice( const IRHIDevice& ) = delete;
		/** @brief 대입을 금지합니다. */
		IRHIDevice& operator=( const IRHIDevice& ) = delete;

		/** @brief 스왑체인 서술로 디바이스를 초기화합니다. */
		virtual bool initialize();
		/** @brief 디바이스와 렌더 패스 매니저를 종료합니다. */
		virtual void shutdown();

		/** @brief RHI 디바이스와 스왑체인을 초기화합니다. */
		virtual bool initializeInternal( const RHISwapChainDesc& desc ) = 0;

		/** @brief 디바이스를 종료하고 관련 리소스를 정리합니다. */
		virtual void shutdownInternal() = 0;

		/** @brief GPU의 대기 중인 작업을 모두 끝낼 때까지 기다립니다. */
		virtual void waitIdle() {}

		/** @brief 스왑체인과 백버퍼 크기를 바꿉니다. */
		// ------------------------------------------------------------------------------
		// 9) 오프스크린 검증 — Present 없는 파이프라인 smoke
		// ------------------------------------------------------------------------------
		/**
		 * @brief Present 없이 오프스크린 RT로 파이프라인을 검증합니다.
		 * @details createTexture2D → beginRenderPass → setPSO → drawFullscreen → destroy.
		 * @return 성공 시 true. pso==0 이면 false.
		 */
		bool executeOffscreenPipelineSmoke( RHIPipelineStateHandle pso,
											RHIDescriptorIndex	   materialCb = kInvalidDescriptorIndex,
											uint32				   width	  = 64,
											uint32				   height	  = 64 );

		// ------------------------------------------------------------------------------
		// 10) 능력 · 스레드 — 백엔드 종류, bindless, 컨텍스트 소유
		// ------------------------------------------------------------------------------
		/** @brief 백엔드 capability를 조회합니다.
		 * @note 정적 표는 RHIAvailability::query. DX12/VK native bindless는
		 *       supportsNativeBindlessSampling() / override getCapabilities()가 런타임 확정.
		 */
		virtual RHICapabilities getCapabilities() const { return RHIAvailability::query( getBackendType() ); }
		virtual IRHISwapChain*	getSwapChain() { return nullptr; }
		virtual IRHIResource*	getResource() { return nullptr; }

		/**
		 * @brief Present / offscreen / Deferred CommandList replay 대상 Immediate Context.
		 * @note CommandList Mode와 혼동하지 말 것 — 이것은 Context 슬롯이다.
		 */
		virtual IRHICommandContext* getImmediateContext() = 0;

		/**
		 * @brief Mode=Deferred일 때 CL에 바인딩하는 Deferred Context (기록용; present 대상 아님).
		 * @note 기본 구현은 Immediate로 폴백. 백엔드는 soft 두 번째 래퍼 또는 네이티브 deferred를 제공한다.
		 */
		virtual IRHICommandContext* getDeferredCommandContext() { return getImmediateContext(); }

		/**
		 * @brief 현재 기본 CommandList Mode에 맞는 Context (Immediate Mode→imm, Deferred Mode→deferred).
		 * @note “기본 컨텍스트”이지 Deferred Context 전용이 아니다.
		 */
		IRHICommandContext* getDefaultCommandContext() { return getCommandContextForMode( _defaultCommandListMode ); }

		/** @brief Mode에 대응하는 Context (createCommandList / FrameRenderer 공유). */
		IRHICommandContext* getCommandContextForMode( RHICommandListMode mode )
		{
			return mode == RHICommandListMode::Immediate ? getImmediateContext() : getDeferredCommandContext();
		}

		/** @brief 현재 RHI 백엔드 종류를 반환합니다. */
		virtual RHIBackend getBackendType() const = 0;

		/** @brief 현재 백엔드가 Bindless(무제한 리소스 배열)를 지원하는지 반환합니다. */
		virtual bool supportsBindless() const = 0;

		/**
		 * @brief GPU 셰이더가 디스크립터 인덱스로 텍스처를 샘플링하는지 (DX12 힙 / VK indexing).
		 * @note false면 CPU가 그 인덱스로 슬롯을 바인딩해야 합니다 (DX11/GL 에뮬레이션).
		 */
		virtual bool supportsNativeBindlessSampling() const { return false; }

		/** @brief beginRenderPass에서 다중 컬러 RT를 동시에 바인딩할 수 있는지 (MRT GBuffer 등). */
		virtual bool supportsMultiRenderTarget() const { return true; }

		/** @brief 컴퓨트 루트/푸시 상수(setComputeRootConstants) 네이티브 지원 여부. */
		virtual bool supportsComputeRootConstants() const { return getCapabilities()._bComputeRootConstants != 0; }

		/**
		 * @brief 드로우/Present 컨텍스트를 소유 스레드 하나에서만 써야 하면 true.
		 * @details DX11 immediate context + OpenGL (wgl/egl MakeCurrent). DX12/Vulkan: false.
		 */
		virtual bool requiresExclusiveContextThread() const { return false; }

		/**
		 * @brief 호출 스레드에 그래픽스 컨텍스트를 붙입니다 (RenderThread 진입).
		 * @details OpenGL: wglMakeCurrent / glXMakeCurrent. DX11: 소유 표시만 (컨텍스트 자체에
		 *          MakeCurrent 없음 — 다른 스레드에서 호출하지 않는 것이 배타성).
		 *          DX12/Vulkan: no-op.
		 */
		virtual bool bindGraphicsContext() { return true; }

		/** @brief 스레드 바인딩을 해제합니다 (OpenGL MakeCurrent(null); DX11은 소유 표시 해제). */
		virtual void unbindGraphicsContext() {}

		/** @brief 백엔드 이름과 버전 포맷 문자열을 반환합니다. */
		virtual const utf8* getBackendName() const = 0;

		// ------------------------------------------------------------------------------
		// 11) 네이티브 핸들 — 디바이스/컨텍스트/스왑체인/큐, ImGui Vulkan
		// ------------------------------------------------------------------------------
		/** @brief 네이티브 디바이스 포인터 (ID3D12Device, VkDevice 등). */
		virtual void* getNativeDevice() const = 0;

		/** @brief 네이티브 컨텍스트 포인터 (ID3D11DeviceContext, EGLContext 등). */
		virtual void* getNativeContext() const = 0;

		/** @brief 네이티브 스왑체인 포인터. */
		virtual void* getNativeSwapChain() const = 0;

		/** @brief 네이티브 커맨드 큐 포인터. */
		virtual void* getNativeCommandQueue() const = 0;

		/**
		 * @brief RHI 텍스처 → 백엔드 native texture name (OpenGL GLuint 등). 미지원 시 0.
		 * @note Editor MODULE이 RHI_* device MODULE concrete 타입에 링크하지 않도록 가상화.
		 */
		virtual uint32 getNativeTextureName( RHITextureHandle texture ) const
		{
			(void)texture;
			return 0;
		}

		/**
		 * @brief 네이티브 텍스처 리소스 포인터 (D3D12 ID3D12Resource*, D3D11 ID3D11Texture2D* 등). 미지원 시 nullptr.
		 * @note Editor MODULE이 RHI_* device MODULE concrete 타입에 링크하지 않도록 가상화.
		 */
		virtual void* getNativeTexturePointer( RHITextureHandle texture ) const
		{
			(void)texture;
			return nullptr;
		}

		/**
		 * @brief Vulkan ImGui 초기화용 native 핸들. 비-Vulkan이면 false.
		 */
		virtual bool queryVulkanImGuiNative( RHIVulkanImGuiNative& out ) const
		{
			(void)out;
			return false;
		}

		/**
		 * @brief Vulkan ImGui 텍스처 등록용 VkImageView. 비-Vulkan/미존재면 false.
		 */
		virtual bool queryVulkanTextureView( RHITextureHandle texture, void*& outImageView ) const
		{
			(void)texture;
			outImageView = nullptr;
			return false;
		}

		/** @brief initializeInternal에 넘길 윈도우를 저장합니다. */
		void setInitWindow( IWindow* pWindow ) { _pInitWindow = pWindow; }
		/** @brief CLI에 --VSYNC가 없을 때 쓸 스왑체인 VSync입니다. */
		void setPreferredVSync( bool bVSync ) { _bPreferredVSync = bVSync; }
		/** @brief 디바이스가 소유한 RenderPassManager를 반환합니다. */
		RenderPassManager& getRenderPassManager() const;

		// ------------------------------------------------------------------------------
		// 14) 커맨드 리스트 — 기본 모드, 생성, 그래픽스 스레드에서만 execute
		// ------------------------------------------------------------------------------
		/** @brief createCommandList()가 쓸 기본 모드를 설정합니다. */
		void setDefaultCommandListMode( RHICommandListMode mode ) { _defaultCommandListMode = mode; }
		/** @brief createCommandList()가 쓸 기본 모드를 반환합니다. */
		RHICommandListMode getDefaultCommandListMode() const { return _defaultCommandListMode; }

		/** @brief 독립 커맨드 리스트를 만듭니다 (기록 전용; GPU 적용은 executeCommandList). */
		virtual unique_ptr<IRHICommandList> createCommandList( RHICommandListMode mode ) = 0;
		/** @brief 기본 모드로 커맨드 리스트를 만듭니다. */
		unique_ptr<IRHICommandList> createCommandList();

		/** @brief 독립 커맨드 리스트를 제출하고 커맨드 큐에서 실행합니다 (그래픽스 실행 스레드에서만). */
		virtual void executeCommandList( IRHICommandList* pCmdList ) = 0;

	protected:
		IWindow*					  _pInitWindow;
		unique_ptr<RenderPassManager> _renderPassManager;
		RHICommandListMode			  _defaultCommandListMode;
		bool						  _bPreferredVSync;
	};
} // namespace sw
