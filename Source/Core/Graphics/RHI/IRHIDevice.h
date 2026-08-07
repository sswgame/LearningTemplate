#pragma once

#include "Core/CoreMinimal.h"
#include "RHITypes.h"
#include "Core/Window/IWindow.h"
#include "Core/Graphics/RenderPass/RenderPassManager.h"

/**
 * @file IRHIDevice.h
 * @brief Render Hardware Interface (RHI) 명령 기록 커맨드 리스트 및 RHI 디바이스 추상화 인터페이스
 */

namespace sw
{
	/**
	 * @class IRHICommandList
	 * @brief GPU 랜더링 및 컴퓨트 명령을 기록(Record)하는 커맨드 리스트 추상 인터페이스
	 */
	class SW_API IRHICommandList
	{
	public:
		virtual ~IRHICommandList()							 = default;
		IRHICommandList()									 = default;
		IRHICommandList( const IRHICommandList& )			 = delete;
		IRHICommandList& operator=( const IRHICommandList& ) = delete;

		/** @brief 커맨드 리스트 기록 시작 */
		virtual void beginCommandList() = 0;

		/** @brief 커맨드 리스트 기록 종료 */
		virtual void endCommandList()	= 0;

		/** @brief 뷰포트 영역 설정 */
		virtual void setViewport( const RHIViewport& viewport ) = 0;

		/** @brief 그래픽스 파이프라인 상태 객체(PSO) 바인딩 */
		virtual void setPipelineState( RHIPipelineStateHandle pso ) = 0;

		/** @brief 렌더 패스 수행 시작 */
		virtual void beginRenderPass( const RHIRenderPassBeginInfo& beginInfo ) = 0;

		/** @brief 렌더 패스 수행 종료 */
		virtual void endRenderPass() = 0;

		/** @brief 기본 삼각형 인덱싱 드로우 호출 */
		virtual void drawTriangle( RHIDescriptorIndex materialDescriptorIndex ) = 0;

		/** @brief 컴퓨트 셰이더 디스패치 실행 */
		virtual void dispatchCompute( uint32 threadGroupCountX, uint32 threadGroupCountY, uint32 threadGroupCountZ ) = 0;

		/** @brief 컴퓨트 루트 상수를 파이프라인에 푸시 */
		virtual void setComputeRootConstants( uint32 rootParameterIndex, uint32 num32BitValues, const void* data, uint32 destOffsetIn32BitValues = 0 ) = 0;

		/** @brief GPU 인디렉트 버퍼에 기반한 그래픽스 인디렉트 드로우 실행 */
		virtual void drawIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset = 0 ) = 0;

		/** @brief GPU 인디렉트 버퍼에 기반한 컴퓨트 인디렉트 디스패치 실행 */
		virtual void dispatchIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset = 0 ) = 0;

		/** @brief GPU 디버깅/프로파일링용 이벤트 마커 시작 */
		virtual void beginEventMarker( const utf8* name ) = 0;

		/** @brief GPU 디버깅/프로파일링용 이벤트 마커 종료 */
		virtual void endEventMarker() = 0;
	};

	/**
	 * @class IRHIDevice
	 * @brief 저기반 그래픽스 API (DX11, DX12, Vulkan, OpenGL) 하드웨어 디바이스 추상 인터페이스
	 */
	class SW_API IRHIDevice
	{
	public:
		virtual ~IRHIDevice()					   = default;
		IRHIDevice()							   = default;
		IRHIDevice( const IRHIDevice& )			   = delete;
		IRHIDevice& operator=( const IRHIDevice& ) = delete;

		void setInitWindow( IWindow* window ) { _initWindow = window; }

		virtual bool initialize()
		{
			if ( _initWindow == nullptr ) return false;

			_renderPassManager = std::make_unique<RenderPassManager>();
			if ( !_renderPassManager->initialize() ) return false;

			RHISwapChainDesc scDesc{};
			scDesc._windowHandle  = _initWindow->getNativeHandle();
			scDesc._windowDisplay = _initWindow->getNativeDisplay();
			scDesc._width		  = _initWindow->getWidth();
			scDesc._height		  = _initWindow->getHeight();
			scDesc._bufferCount   = 3;
			scDesc._vsync		  = true;

			return initializeInternal( scDesc );
		}

		virtual void shutdown()
		{
			if ( _renderPassManager )
			{
				_renderPassManager->shutdown();
				_renderPassManager.reset();
			}
			shutdownInternal();
		}

		RenderPassManager& getRenderPassManager() const
		{
			return *_renderPassManager;
		}

	protected:
		IWindow* _initWindow = nullptr;
		std::unique_ptr<RenderPassManager> _renderPassManager;

	public:

		/** @brief RHI 디바이스 및 스왑체인 초기화 */
		virtual bool initializeInternal( const RHISwapChainDesc& desc ) = 0;

		/**
		 * @brief 디바이스 종료 및 관련 리소스 정리
		 */
		virtual void shutdownInternal() = 0;

		/** @brief GPU의 모든 대기 작업 완료 대기 */
		virtual void waitIdle() {}

		/** @brief 스왑체인 및 백버퍼 크기 변경 */
		virtual void resize( uint32 width, uint32 height ) = 0;

		/** @brief 프레임 렌더링 시작 (백버퍼 지우기) */
		virtual void beginFrame( float32 clearColor[4] )   = 0;

		/** @brief 프레임 렌더링 종료 (스왑체인 Present 실행) */
		virtual void endFrame( bool vsync = true )		   = 0;

		/** @brief 현재 RHI 백엔드 종류 반환 */
		virtual RHIBackend	getBackendType() const = 0;

		/** @brief 현재 백엔드가 Bindless(무제한 리소스 배열) 기술을 지원하는지 여부 반환 */
		virtual bool supportsBindless() const = 0;

		/** @brief 백엔드 이름 및 버전 포맷 문자열 반환 */
		virtual const utf8* getBackendName() const = 0;

		/** @brief 기본 Native 디바이스 객체 포인터 반환 (ID3D12Device, VkDevice 등) */
		virtual void* getNativeDevice() const		= 0;

		/** @brief 기본 Native 컨텍스트 포인터 반환 (ID3D11DeviceContext, EGLContext 등) */
		virtual void* getNativeContext() const		= 0;

		/** @brief 기본 Native 스왑체인 객체 포인터 반환 */
		virtual void* getNativeSwapChain() const	= 0;

		/** @brief 기본 Native 커맨드 큐 객체 포인터 반환 */
		virtual void* getNativeCommandQueue() const = 0;

		/** @brief 그래픽스 파이프라인 상태 객체(PSO) 생성 */
		virtual RHIPipelineStateHandle createPipelineState( const RHIPipelineStateDesc& desc )	  = 0;

		/** @brief 컴퓨트 파이프라인 상태 객체(PSO) 생성 */
		virtual RHIPipelineStateHandle createComputePipelineState( const std::string& shaderPath, const std::string& entryPoint = "CSMain" ) = 0;

		/** @brief 파이프라인 상태 객체 해제 */
		virtual void				   destroyPipelineState( RHIPipelineStateHandle pso )		  = 0;

		/** @brief 그래픽스 파이프라인 바인딩 */
		virtual void				   setPipelineState( RHIPipelineStateHandle pso )			  = 0;

		/** @brief 컴퓨트 파이프라인 바인딩 */
		virtual void				   setComputePipelineState( RHIPipelineStateHandle pso )	  = 0;

		/** @brief 렌더 패스 객체 생성 */
		virtual RHIRenderPassHandle	   createRenderPass( const RHIRenderPassDesc& desc )		  = 0;

		/** @brief 렌더 패스 객체 해제 */
		virtual void				   destroyRenderPass( RHIRenderPassHandle pass )			  = 0;

		/** @brief 렌더 패스 바인딩 및 수행 시작 */
		virtual void				   beginRenderPass( const RHIRenderPassBeginInfo& beginInfo ) = 0;

		/** @brief 렌더 패스 수행 종료 */
		virtual void				   endRenderPass()											  = 0;

		/** @brief Constant Buffer 생성 */
		virtual RHIBufferHandle	   createConstantBuffer( uint32 size )											 = 0;

		/** @brief Constant Buffer 데이터 갱신 */
		virtual void			   updateConstantBuffer( RHIBufferHandle buffer, const void* data, uint32 size ) = 0;

		/** @brief Structured / Storage Buffer 생성 */
		virtual RHIBufferHandle	   createStructuredBuffer( uint32 elementSize, uint32 elementCount )			 = 0;

		/** @brief Structured / Storage Buffer 데이터 갱신 */
		virtual void			   updateStructuredBuffer( RHIBufferHandle buffer, const void* data, uint32 size ) = 0;

		/** @brief GPU 버퍼 리소스 삭제 */
		virtual void			   destroyBuffer( RHIBufferHandle buffer )										 = 0;

		/** @brief 2D 텍스처 (RenderTarget 포함) 리소스 생성 */
		virtual RHITextureHandle   createTexture2D( const RHITextureDesc& desc )								 = 0;

		/** @brief GPU 텍스처 리소스 삭제 */
		virtual void			   destroyTexture( RHITextureHandle texture )									 = 0;

		/** @brief Bindless 리소스 테이블에 텍스처 등록 후 SRV 인덱스 발급 */
		virtual RHIDescriptorIndex registerBindlessTexture( RHITextureHandle texture )							 = 0;

		/** @brief Bindless 리소스 테이블에 버퍼 등록 후 Bindless 인덱스 발급 */
		virtual RHIDescriptorIndex registerBindlessResource( RHIBufferHandle buffer )							 = 0;

		/** @brief Bindless 리소스 등록 해제 */
		virtual void			   unregisterBindlessResource( RHIDescriptorIndex index )						 = 0;

		/** @brief Bindless UAV(Unordered Access View) 등록 후 인덱스 발급 */
		virtual RHIDescriptorIndex registerBindlessUAV( RHIBufferHandle buffer )								 = 0;

		/** @brief Bindless UAV 등록 해제 */
		virtual void			   unregisterBindlessUAV( RHIDescriptorIndex index )							 = 0;

		/** @brief 명시적 슬롯(Explicit Slot)에 UAV 직접 바인딩 */
		virtual void			   bindComputeUAV( RHIDescriptorIndex index, uint32 slot )						 = 0;

		/** @brief 기본 삼각형 인덱싱 드로우 호출 */
		virtual void			   drawTriangle( RHIDescriptorIndex materialDescriptorIndex )					 = 0;

		/** @brief 컴퓨트 셰이더 디스패치 실행 */
		virtual void dispatchCompute( uint32 threadGroupCountX, uint32 threadGroupCountY, uint32 threadGroupCountZ ) = 0;

		/** @brief GPU 인디렉트 버퍼에 기반한 그래픽스 드로우 실행 */
		virtual void drawIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset = 0 ) = 0;

		/** @brief GPU 인디렉트 버퍼에 기반한 컴퓨트 디스패치 실행 */
		virtual void dispatchIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset = 0 ) = 0;

		/** @brief GPU 이벤트 마커 영역 시작 */
		virtual void beginEventMarker( const utf8* name ) = 0;

		/** @brief GPU 이벤트 마커 영역 종료 */
		virtual void endEventMarker() = 0;

		/** @brief 새 독립 커맨드 리스트 생성 */
		virtual std::unique_ptr<IRHICommandList> createCommandList()							= 0;

		/** @brief 독립 커맨드 리스트 제출 및 커맨드 큐 실행 */
		virtual void							 executeCommandList( IRHICommandList* cmdList ) = 0;
	};
}
