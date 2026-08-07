#pragma once
#include "Core/Graphics/RHI/IRHIDevice.h"
#include "Core/Common/PlatformHeaders.h"
#include "Core/Common/CommonHeaders.h"

/**
 * @file OpenGLRHIDevice.h
 * @brief OpenGL 4.6 Core Profile 기반 RHI 백엔드 클래스 정의
 * @note GLAD/OpenGL 심볼은 OpenGLRHIDevice.cpp 에서만 include합니다.
 */

namespace sw
{
	/**
	 * @class OpenGLRHIDevice
	 * @brief OpenGL 4.6 그래픽스 및 컴퓨트 디바이스 구현체 (SSBO/UBO 바인딩 지원)
	 */
	class OpenGLRHIDevice : public IRHIDevice
	{
	public:
		OpenGLRHIDevice();
		virtual ~OpenGLRHIDevice() override;

		/** @brief OpenGL 렌더링 컨텍스트 (wglCreateContext / EGL) 및 GLAD 로드 */
		virtual bool initializeInternal( const RHISwapChainDesc& desc ) override;

		/** @brief OpenGL 컨텍스트 및 GL 객체 해제 */
		virtual void shutdownInternal() override;

		/** @brief glViewport 크기 변경 */
		virtual void resize( uint32 width, uint32 height ) override;

		/** @brief 프레임 시작 (glClearColor 및 glClear) */
		virtual void beginFrame( float32 clearColor[4] ) override;

		/** @brief 프레임 종료 (SwapBuffers / wglSwapBuffers) */
		virtual void endFrame( bool vsync ) override;

		/** @brief 백엔드 타입 반환 (OpenGL) */
		RHIBackend	getBackendType() const override { return RHIBackend::OpenGL; }

		/** @brief OpenGL은 SSBO/UBO 인덱스 바인딩을 지원함 (true 반환) */
		bool				supportsBindless() const override { return true; }

		/** @brief 백엔드 이름 문자열 반환 */
		virtual const utf8* getBackendName() const override { return "OpenGL (glad 4.6 Core)"; }

		/** @brief Native DC / Display 포인터 반환 */
		virtual void* getNativeDevice() const override { return _hDC; }

		/** @brief Native HGLRC 컨텍스트 포인터 반환 */
		virtual void* getNativeContext() const override { return _hRC; }

		/** @brief Native DC 포인터 반환 */
		virtual void* getNativeSwapChain() const override { return _hDC; }

		/** @brief OpenGL은 커맨드 큐가 없음 (nullptr) */
		virtual void* getNativeCommandQueue() const override { return nullptr; }

		/** @brief GLSL 셰이더 프로토콜 및 VAO/PSO 레코드 생성 */
		RHIPipelineStateHandle createPipelineState( const RHIPipelineStateDesc& desc ) override;

		/** @brief GLSL 컴퓨트 셰이더 프로그램 생성 */
		RHIPipelineStateHandle createComputePipelineState( const std::string& shaderPath, const std::string& entryPoint = "CSMain" ) override;

		/** @brief GLSL 프로그램 객체 삭제 (glDeleteProgram) */
		void				   destroyPipelineState( RHIPipelineStateHandle pso ) override;

		/** @brief GLSL 프로그램 바인딩 (glUseProgram) 및 VAO 바인딩 */
		void				   setPipelineState( RHIPipelineStateHandle pso ) override;

		/** @brief GLSL 컴퓨트 프로그램 바인딩 */
		void				   setComputePipelineState( RHIPipelineStateHandle pso ) override;

		/** @brief OpenGL 렌더 패스 서술체 생성 */
		RHIRenderPassHandle	   createRenderPass( const RHIRenderPassDesc& desc ) override;

		/** @brief 렌더 패스 구조체 해제 */
		void				   destroyRenderPass( RHIRenderPassHandle pass ) override;

		/** @brief 프레임버퍼 어태치먼트 설정 */
		void				   beginRenderPass( const RHIRenderPassBeginInfo& beginInfo ) override;

		/** @brief 렌더 패스 종료 */
		void				   endRenderPass() override;

		/** @brief OpenGL UBO (Uniform Buffer Object) 생성 (glGenBuffers) */
		RHIBufferHandle	   createConstantBuffer( uint32 size ) override;

		/** @brief UBO 데이터 갱신 (glNamedBufferSubData / glBufferSubData) */
		void			   updateConstantBuffer( RHIBufferHandle buffer, const void* data, uint32 size ) override;

		/** @brief OpenGL SSBO (Shader Storage Buffer Object) 생성 */
		RHIBufferHandle	   createStructuredBuffer( uint32 elementSize, uint32 elementCount ) override;

		/** @brief SSBO 데이터 갱신 */
		void			   updateStructuredBuffer( RHIBufferHandle buffer, const void* data, uint32 size ) override;

		/** @brief GL 버퍼 객체 삭제 (glDeleteBuffers) */
		void			   destroyBuffer( RHIBufferHandle buffer ) override;

		RHITextureHandle   createTexture2D( const RHITextureDesc& /*desc*/ ) override { return 0; }
		void			   destroyTexture( RHITextureHandle /*texture*/ ) override {}
		RHIDescriptorIndex registerBindlessTexture( RHITextureHandle /*texture*/ ) override { return kInvalidDescriptorIndex; }

		/** @brief UBO/SSBO 바인딩 인덱스 발급 (glBindBufferBase) */
		RHIDescriptorIndex registerBindlessResource( RHIBufferHandle buffer ) override;

		/** @brief 바인딩 리소스 제거 */
		void			   unregisterBindlessResource( RHIDescriptorIndex index ) override;

		/** @brief SSBO (UAV) 바인딩 인덱스 발급 */
		RHIDescriptorIndex registerBindlessUAV( RHIBufferHandle buffer ) override;

		/** @brief SSBO 바인딩 제거 */
		void			   unregisterBindlessUAV( RHIDescriptorIndex index ) override;

		/** @brief 명시적 바인딩 슬롯에 SSBO 연결 (glBindBufferBase) */
		void			   bindComputeUAV( RHIDescriptorIndex index, uint32 slot ) override;

		/** @brief 기본 삼각형 그리기 (glDrawArrays) */
		void			   drawTriangle( RHIDescriptorIndex materialDescriptorIndex ) override;

		/** @brief glDispatchCompute 컴퓨트 실행 */
		void dispatchCompute( uint32 threadGroupCountX, uint32 threadGroupCountY, uint32 threadGroupCountZ ) override;

		/** @brief glDrawArraysIndirect 실행 */
		void drawIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset = 0 ) override;

		/** @brief glDispatchComputeIndirect 실행 */
		void dispatchIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset = 0 ) override;

		/** @brief glPushDebugGroup 이벤트 마커 시작 */
		void beginEventMarker( const utf8* name ) override;

		/** @brief glPopDebugGroup 이벤트 마커 종료 */
		void endEventMarker() override;

		/** @brief 커맨드 리스트 객체 생성 */
		std::unique_ptr<IRHICommandList> createCommandList() override;

		/** @brief 커맨드 리스트 실행 */
		void							 executeCommandList( IRHICommandList* cmdList ) override;

	private:
		/**
		 * @brief TriangleResources을(를) 생성합니다
		 */
		bool createTriangleResources();

	private:
		void*  _hDC			  = nullptr;
		void*  _hRC			  = nullptr;
		void*  _hWnd		  = nullptr;
		uint32 _width		  = 1280;
		uint32 _height		  = 720;
		uint32 _shaderProgram = 0;
		uint32 _vao			  = 0;
		uint32 _vbo			  = 0;

		std::vector<uint32> _constantBuffers;

		struct BindlessResourceRecord
		{
			uint32 buffer;
		};
		std::vector<BindlessResourceRecord> _registeredBindlessVector;
		std::vector<uint32>					_bindlessFreeList;
		std::vector<BindlessResourceRecord> _registeredUAVs;
		std::vector<uint32>					_uavFreeList;
		std::vector<uint32> _structuredBuffers;

		struct OpenGLPipelineStateRecord
		{
			uint32 program = 0;
			uint32 vao	   = 0;
		};

		struct OpenGLRenderPassRecord
		{
			RHIRenderPassDesc desc;
		};

		std::vector<OpenGLPipelineStateRecord> _pipelineStates;
		std::vector<OpenGLRenderPassRecord>	   _renderPasses;

		uint8				   _bInitialized  : 1;
		[[maybe_unused]] uint8 _reservedFlags : 7;
	};
}
