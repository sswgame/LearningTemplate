#pragma once
#include "Core/Graphics/RHI/IRHIDevice.h"
#include "Core/Graphics/RHI/RHIReleaseQueue.h"

#include "Core/Common/PlatformHeaders.h"
#include "Core/Common/CommonHeaders.h"

/**
 * @file D3D11RHIDevice.h
 * @brief Direct3D 11 API 기반 RHI 백엔드 클래스 정의
 */

#if defined( SW_PLATFORM_WINDOWS )

namespace sw
{
	/**
	 * @class D3D11RHIDevice
	 * @brief Direct3D 11 그래픽스 디바이스 구현체
	 */
	class D3D11RHIDevice : public IRHIDevice
	{
	public:
		D3D11RHIDevice();
		~D3D11RHIDevice() override;

		/** @brief Direct3D 11 디바이스, 임베디드 스왑체인 및 렌더 타깃 뷰 생성 */
		bool initializeInternal( const RHISwapChainDesc& desc ) override;

		/** @brief D3D11 자원 해제 */
		void shutdownInternal() override;

		/** @brief 스왑체인 뷰포트 크기 변경 */
		void resize( uint32 width, uint32 height ) override;

		/** @brief 프레임 시작 (백버퍼 렌더 타깃 클리어) */
		void beginFrame( float32 clearColor[4] ) override;

		/** @brief Offscreen color target에 렌더 시작 (Game View 등). colorTarget==0 이면 beginFrame과 동일. */
		void beginOffscreenPass( RHITextureHandle colorTarget, float32 clearColor[4] ) override;

		/** @brief Offscreen 패스 종료 */
		void endOffscreenPass( RHITextureHandle colorTarget ) override;

		/** @brief 스왑체인 Present 실행 */
		void endFrame( bool vsync = true ) override;

		/** @brief 백엔드 타입 반환 (DirectX11) */
		RHIBackend getBackendType() const override { return RHIBackend::D3D11; }

		/** @brief Direct3D 11 백엔드는 네이티브 Bindless(Unbounded Array)를 지원하지 않음 (false 반환) */
		bool supportsBindless() const override { return false; }

		/** @brief 백엔드 문자열 반환 */
		const utf8* getBackendName() const override { return "Direct3D 11"; }

		/** @brief ID3D11Device 인터페이스 포인터 반환 */
		void* getNativeDevice() const override { return _device.Get(); }

		/** @brief ID3D11DeviceContext 인터페이스 포인터 반환 */
		void* getNativeContext() const override { return _deviceContext.Get(); }

		/** @brief IDXGISwapChain 인터페이스 포인터 반환 */
		void* getNativeSwapChain() const override { return _swapChain.Get(); }

		/** @brief D3D11은 단일 큐 모델로 커맨드 큐 포인터가 없음 (nullptr) */
		void* getNativeCommandQueue() const override { return nullptr; }

		/** @brief D3D11 그래픽스 파이프라인 상태 레코드 생성 */
		RHIPipelineStateHandle createPipelineState( const RHIPipelineStateDesc& desc ) override;

		/** @brief D3D11 컴퓨트 파이프라인 상태 생성 */
		RHIPipelineStateHandle createComputePipelineState( const std::string& shaderPath, const std::string& entryPoint = "CSMain" ) override;

		/** @brief 파이프라인 상태 자원 해제 */
		void destroyPipelineState( RHIPipelineStateHandle pso ) override;

		/** @brief D3D11 그래픽스 셰이더 및 라스터라이저 바인딩 */
		void setPipelineState( RHIPipelineStateHandle pso ) override;

		/** @brief D3D11 컴퓨트 셰이더 바인딩 */
		void setComputePipelineState( RHIPipelineStateHandle pso ) override;

		/** @brief D3D11 렌더 패스 정보 등록 */
		RHIRenderPassHandle createRenderPass( const RHIRenderPassDesc& desc ) override;

		/** @brief 렌더 패스 정보 제거 */
		void destroyRenderPass( RHIRenderPassHandle pass ) override;

		/** @brief 렌더 패스 바인딩 및 렌더 타깃 설정 */
		void beginRenderPass( const RHIRenderPassBeginInfo& beginInfo ) override;

		/** @brief 렌더 패스 바인딩 해제 */
		void endRenderPass() override;

		/** @brief D3D11 Constant Buffer 생성 */
		RHIBufferHandle createConstantBuffer( uint32 size ) override;

		/** @brief D3D11 Constant Buffer 데이터 Map/Unmap 갱신 */
		void updateConstantBuffer( RHIBufferHandle buffer, const void* data, uint32 size ) override;

		/** @brief D3D11 Structured Buffer 생성 */
		RHIBufferHandle createStructuredBuffer( uint32 elementSize, uint32 elementCount ) override;

		/** @brief D3D11 Structured Buffer 데이터 갱신 */
		void updateStructuredBuffer( RHIBufferHandle buffer, const void* data, uint32 size ) override;

		/** @brief D3D11 버퍼 해제 */
		void destroyBuffer( RHIBufferHandle buffer ) override;

		/** @brief D3D11 2D 텍스처 (RenderTarget / ShaderResource) 생성 */
		RHITextureHandle createTexture2D( const RHITextureDesc& desc ) override;

		/** @brief D3D11 텍스처 해제 */
		void destroyTexture( RHITextureHandle texture ) override;

		RHIDescriptorIndex registerBindlessTexture( RHITextureHandle /*texture*/ ) override { return kInvalidDescriptorIndex; }

		/** @brief 에뮬레이션용 Bindless 버퍼 등록 */
		RHIDescriptorIndex registerBindlessResource( RHIBufferHandle buffer ) override;

		/** @brief 에뮬레이션용 Bindless 버퍼 해제 */
		void unregisterBindlessResource( RHIDescriptorIndex index ) override;

		/** @brief 에뮬레이션용 Bindless UAV 등록 */
		RHIDescriptorIndex registerBindlessUAV( RHIBufferHandle buffer ) override;

		/** @brief 에뮬레이션용 Bindless UAV 해제 */
		void unregisterBindlessUAV( RHIDescriptorIndex index ) override;

		/** @brief 컴퓨트 셰이더용 UAV 명시적 슬롯 바인딩 */
		void bindComputeUAV( RHIDescriptorIndex index, uint32 slot ) override;

		/** @brief 기본 삼각형 인덱싱 드로우 호출 */
		void drawTriangle( RHIDescriptorIndex materialDescriptorIndex ) override;

		/** @brief 컴퓨트 셰이더 Dispatch 호출 */
		void dispatchCompute( uint32 threadGroupCountX, uint32 threadGroupCountY, uint32 threadGroupCountZ ) override;

		/** @brief 컴퓨트 셰이더 루트 상수 설정 */
		void setComputeRootConstants( uint32 rootParameterIndex, uint32 num32BitValues, const void* data, uint32 destOffsetIn32BitValues = 0 );

		/** @brief D3D11 DrawInstancedIndirect 호출 */
		void drawIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset = 0 ) override;

		/** @brief D3D11 DispatchIndirect 호출 */
		void dispatchIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset = 0 ) override;

		/** @brief GPU 디버그 이벤트 마커 시작 */
		void beginEventMarker( const utf8* name ) override;

		/** @brief GPU 디버그 이벤트 마커 종료 */
		void endEventMarker() override;

		/** @brief 커맨드 리스트 생성 */
		std::unique_ptr<IRHICommandList> createCommandList() override;

		/** @brief 커맨드 리스트 제출 */
		void executeCommandList( IRHICommandList* cmdList ) override;

	private:
		/**
		 * @brief RenderTargetView을(를) 생성합니다
		 */
		void createRenderTargetView();
		/**
		 * @brief 렌더 타깃 뷰를 정리합니다
		 */
		void cleanupRenderTargetView();
		/**
		 * @brief TriangleResources을(를) 생성합니다
		 */
		bool createTriangleResources();

	private:
		Microsoft::WRL::ComPtr<ID3D11Device>		   _device;
		Microsoft::WRL::ComPtr<ID3D11DeviceContext>	   _deviceContext;
		Microsoft::WRL::ComPtr<IDXGISwapChain>		   _swapChain;
		Microsoft::WRL::ComPtr<ID3D11RenderTargetView> _renderTargetView;

		Microsoft::WRL::ComPtr<ID3D11InputLayout>  _inputLayout;
		Microsoft::WRL::ComPtr<ID3D11VertexShader> _vertexShader;
		Microsoft::WRL::ComPtr<ID3D11PixelShader>  _pixelShader;
		Microsoft::WRL::ComPtr<ID3D11Buffer>	   _vertexBuffer;

		std::vector<Microsoft::WRL::ComPtr<ID3D11Buffer>> _constantBuffers;
		std::vector<Microsoft::WRL::ComPtr<ID3D11Buffer>> _structuredBuffers;

		std::vector<ID3D11Buffer*> _registeredBindlessVector;
		std::vector<uint32>		   _bindlessFreeList;

		std::vector<Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView>> _registeredUAVs;
		std::vector<uint32>											   _uavFreeList;

		struct D3D11PipelineStateRecord
		{
			Microsoft::WRL::ComPtr<ID3D11VertexShader>	  vs;
			Microsoft::WRL::ComPtr<ID3D11PixelShader>	  ps;
			Microsoft::WRL::ComPtr<ID3D11ComputeShader>	  cs;
			Microsoft::WRL::ComPtr<ID3D11InputLayout>	  inputLayout;
			Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterizerState;
		};

		struct D3D11RenderPassRecord
		{
			RHIRenderPassDesc desc;
		};

		std::vector<D3D11PipelineStateRecord> _pipelineStates;
		std::vector<D3D11RenderPassRecord>	  _renderPasses;

		struct TextureRecord
		{
			Microsoft::WRL::ComPtr<ID3D11Texture2D>			 _texture;
			Microsoft::WRL::ComPtr<ID3D11RenderTargetView>	 _rtv;
			Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> _srv;
			uint32											 _width	 = 0;
			uint32											 _height = 0;
		};
		std::unordered_map<RHITextureHandle, TextureRecord> _textures;

		HWND   _hWnd   = nullptr;
		uint32 _width  = 0;
		uint32 _height = 0;

		RHIReleaseQueue _releaseQueue{ 3 };
	};
} // namespace sw
#else
namespace sw
{
	/** @brief 비-Windows 환경용 스텁 D3D11RHIDevice */
	class D3D11RHIDevice : public IRHIDevice
	{
	public:
		D3D11RHIDevice()		   = default;
		~D3D11RHIDevice() override = default;

		bool initializeInternal( const RHISwapChainDesc& ) override { return false; }
		void shutdownInternal() override {}
		void resize( uint32, uint32 ) override {}
		void beginFrame( float32[4] ) override {}
		void endFrame( bool ) override {}

		RHIBackend	getBackendType() const override { return RHIBackend::D3D11; }
		bool		supportsBindless() const override { return false; }
		const utf8* getBackendName() const override { return "Direct3D 11 (Not Supported on non-Windows)"; }

		void* getNativeDevice() const override { return nullptr; }
		void* getNativeContext() const override { return nullptr; }
		void* getNativeSwapChain() const override { return nullptr; }
		void* getNativeCommandQueue() const override { return nullptr; }

		RHIBufferHandle createConstantBuffer( uint32 ) override { return 0; }
		void			updateConstantBuffer( RHIBufferHandle, const void*, uint32 ) override {}
		RHIBufferHandle createStructuredBuffer( uint32, uint32 ) override { return 0; }
		void			updateStructuredBuffer( RHIBufferHandle, const void*, uint32 ) override {}
		void			destroyBuffer( RHIBufferHandle ) override {}

		RHITextureHandle   createTexture2D( const RHITextureDesc& ) override { return 0; }
		void			   destroyTexture( RHITextureHandle ) override {}
		RHIDescriptorIndex registerBindlessTexture( RHITextureHandle ) override { return kInvalidDescriptorIndex; }

		RHIDescriptorIndex registerBindlessResource( RHIBufferHandle ) override { return kInvalidDescriptorIndex; }
		void			   unregisterBindlessResource( RHIDescriptorIndex ) override {}
		RHIDescriptorIndex registerBindlessUAV( RHIBufferHandle ) override { return kInvalidDescriptorIndex; }
		void			   unregisterBindlessUAV( RHIDescriptorIndex ) override {}
		void			   bindComputeUAV( RHIDescriptorIndex, uint32 ) override {}
		void			   drawTriangle( RHIDescriptorIndex ) override {}
	};
} // namespace sw
#endif
