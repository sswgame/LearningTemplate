#pragma once
#include "Core/Graphics/RHI/IRHIDevice.h"
#include "Core/Graphics/RHI/RHIReleaseQueue.h"

#include "Core/Common/PlatformHeaders.h"
#include "Core/Common/CommonHeaders.h"

/**
 * @file D3D12RHIDevice.h
 * @brief Direct3D 12 API 기반 RHI 백엔드 클래스 정의
 */

#if defined( SW_PLATFORM_WINDOWS )

namespace sw
{
	/**
	 * @class D3D12RHIDevice
	 * @brief Direct3D 12 그래픽스 디바이스 구현체 (Bindless 지원)
	 */
	class D3D12RHIDevice : public IRHIDevice
	{
	public:
		D3D12RHIDevice();
		~D3D12RHIDevice() override;

		/** @brief Direct3D 12 디바이스, 커맨드 큐, DXGI 스왑체인 및 힙 리소스 초기화 */
		bool initializeInternal( const RHISwapChainDesc& desc ) override;

		/** @brief D3D12 자원 및 펜스 동기화 객체 해제 */
		void shutdownInternal() override;

		/** @brief GPU 명령 완료 대기 (Fence Sync) */
		void waitIdle() override;

		/** @brief 스왑체인 버퍼 해제 후 크기 재설정 */
		void resize( uint32 width, uint32 height ) override;

		/** @brief 프레임 시작 (백버퍼 Resource Barrier 전환 & Clear) */
		void beginFrame( float32 clearColor[4] ) override;
		void endFrame( bool vsync = true ) override;

		void beginOffscreenPass( RHITextureHandle colorTarget, float32 clearColor[4] ) override;
		void endOffscreenPass( RHITextureHandle colorTarget ) override;

		/** @brief 백엔드 타입 반환 (DirectX12) */
		RHIBackend getBackendType() const override { return RHIBackend::D3D12; }

		/** @brief D3D12는 네이티브 Bindless(Unbounded Descriptor Table)를 지원함 (true 반환) */
		bool supportsBindless() const override { return true; }

		/** @brief 백엔드 문자열 반환 */
		const utf8* getBackendName() const override { return "Direct3D 12"; }

		/** @brief ID3D12Device 포인터 반환 */
		void* getNativeDevice() const override { return _device.Get(); }

		/** @brief ID3D12GraphicsCommandList 포인터 반환 */
		void* getNativeContext() const override { return _commandList.Get(); }

		/** @brief IDXGISwapChain3 포인터 반환 */
		void* getNativeSwapChain() const override { return _swapChain.Get(); }

		/** @brief ID3D12CommandQueue 포인터 반환 */
		void* getNativeCommandQueue() const override { return _commandQueue.Get(); }

		/** @brief D3D12 파이프라인 상태 객체(PSO) 생성 */
		RHIPipelineStateHandle createPipelineState( const RHIPipelineStateDesc& desc ) override;

		/** @brief D3D12 컴퓨트 파이프라인 상태 객체(PSO) 생성 */
		RHIPipelineStateHandle createComputePipelineState( const std::string& shaderPath, const std::string& entryPoint = "CSMain" ) override;

		/** @brief 파이프라인 상태 자원 해제 */
		void destroyPipelineState( RHIPipelineStateHandle pso ) override;

		/** @brief 그래픽스 파이프라인 및 루트 시그니처 바인딩 */
		void setPipelineState( RHIPipelineStateHandle pso ) override;

		/** @brief 컴퓨트 파이프라인 및 컴퓨트 루트 시그니처 바인딩 */
		void setComputePipelineState( RHIPipelineStateHandle pso ) override;

		/** @brief D3D12 렌더 패스 정보 등록 */
		RHIRenderPassHandle createRenderPass( const RHIRenderPassDesc& desc ) override;

		/** @brief 렌더 패스 제거 */
		void destroyRenderPass( RHIRenderPassHandle pass ) override;

		/** @brief 렌더 타깃 어태치먼트 바인딩 */
		void beginRenderPass( const RHIRenderPassBeginInfo& beginInfo ) override;

		/** @brief 렌더 패스 종료 */
		void endRenderPass() override;

		/** @brief D3D12 Constant Buffer 리소스 생성 */
		RHIBufferHandle createConstantBuffer( uint32 size ) override;

		/** @brief Upload 힙을 통한 Constant Buffer 데이터 갱신 */
		void updateConstantBuffer( RHIBufferHandle buffer, const void* data, uint32 size ) override;

		/** @brief D3D12 Structured / Storage Buffer 생성 */
		RHIBufferHandle createStructuredBuffer( uint32 elementSize, uint32 elementCount ) override;

		/** @brief D3D12 Structured Buffer 데이터 갱신 */
		void updateStructuredBuffer( RHIBufferHandle buffer, const void* data, uint32 size ) override;

		/** @brief D3D12 GPU 리소스 삭제 */
		void destroyBuffer( RHIBufferHandle buffer ) override;

		/** @brief D3D12 2D 텍스처 리소스 생성 */
		RHITextureHandle createTexture2D( const RHITextureDesc& desc ) override;

		/** @brief D3D12 GPU 텍스처 리소스 삭제 */
		void destroyTexture( RHITextureHandle texture ) override;

		/** @brief D3D12 Bindless 리소스 테이블에 텍스처 등록 후 SRV 인덱스 발급 */
		RHIDescriptorIndex registerBindlessTexture( RHITextureHandle texture ) override;

		/** @brief Bindless Descriptor Heap에 CBV/SRV 등록 후 핸들 발급 */
		RHIDescriptorIndex registerBindlessResource( RHIBufferHandle buffer ) override;

		/** @brief Bindless 리소스 등록 해제 및 Heap 테이블 항목 반환 */
		void unregisterBindlessResource( RHIDescriptorIndex index ) override;

		/** @brief Bindless Descriptor Heap에 UAV 등록 후 핸들 발급 */
		RHIDescriptorIndex registerBindlessUAV( RHIBufferHandle buffer ) override;

		/** @brief Bindless UAV 등록 해제 */
		void unregisterBindlessUAV( RHIDescriptorIndex index ) override;

		/** @brief 명시적 Root Descriptor Slot에 UAV 테이블 바인딩 */
		void bindComputeUAV( RHIDescriptorIndex index, uint32 slot ) override;

		/** @brief 기본 삼각형 인덱싱 드로우 호출 */
		void drawTriangle( RHIDescriptorIndex materialDescriptorIndex ) override;

		/** @brief D3D12 Dispatch 컴퓨트 디스패치 실행 */
		void dispatchCompute( uint32 threadGroupCountX, uint32 threadGroupCountY, uint32 threadGroupCountZ ) override;

		/** @brief D3D12 SetComputeRoot32BitConstants 32비트 루트 상수 푸시 */
		void setComputeRootConstants( uint32 rootParameterIndex, uint32 num32BitValues, const void* data, uint32 destOffsetIn32BitValues = 0 );

		/** @brief D3D12 ExecuteIndirect 그래픽스 드로우 실행 */
		void drawIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset = 0 ) override;

		/** @brief D3D12 ExecuteIndirect 컴퓨트 디스패치 실행 */
		void dispatchIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset = 0 ) override;

		/** @brief PIX / GPU 디버거 이벤트 마커 시작 */
		void beginEventMarker( const utf8* name ) override;

		/** @brief PIX / GPU 디버거 이벤트 마커 종료 */
		void endEventMarker() override;

		/** @brief 독립 커맨드 리스트 생성 */
		std::unique_ptr<IRHICommandList> createCommandList() override;

		/** @brief 독립 커맨드 리스트 제출 */
		void executeCommandList( IRHICommandList* cmdList ) override;

	private:
		/**
		 * @brief RenderTargets을(를) 생성합니다
		 */
		void createRenderTargets();
		/**
		 * @brief 렌더 타깃을 정리합니다
		 */
		void cleanupRenderTargets();
		/**
		 * @brief 이전 프레임 완료를 기다립니다
		 */
		void waitForPreviousFrame();
		/**
		 * @brief TriangleResources을(를) 생성합니다
		 */
		bool createTriangleResources();
		/**
		 * @brief D3D12 InfoQueue 메시지를 로그로 비웁니다.
		 */
		void flushDebugMessages( const utf8* stage );

	private:
		Microsoft::WRL::ComPtr<ID3D12Device>			  _device;
		Microsoft::WRL::ComPtr<ID3D12CommandQueue>		  _commandQueue;
		Microsoft::WRL::ComPtr<IDXGISwapChain3>			  _swapChain;
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>	  _rtvHeap;
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>	  _cbvHeap;
		Microsoft::WRL::ComPtr<ID3D12RootSignature>		  _rootSignature;
		Microsoft::WRL::ComPtr<ID3D12RootSignature>		  _computeRootSignature;
		Microsoft::WRL::ComPtr<ID3D12PipelineState>		  _pipelineState;
		Microsoft::WRL::ComPtr<ID3D12Resource>			  _vertexBuffer;
		D3D12_VERTEX_BUFFER_VIEW						  _vertexBufferView{};
		Microsoft::WRL::ComPtr<ID3D12CommandSignature>	  _drawCommandSignature;
		Microsoft::WRL::ComPtr<ID3D12CommandSignature>	  _dispatchCommandSignature;
		Microsoft::WRL::ComPtr<ID3D12CommandAllocator>	  _commandAllocator;
		Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> _commandList;

		std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> _renderTargets;
		std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> _constantBuffers;
		std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> _textures;

		struct OffscreenTextureRecord
		{
			ID3D12Resource*				_resource = nullptr;
			D3D12_CPU_DESCRIPTOR_HANDLE _rtvHandle{};
			uint32						_rtvIndex = 0;
			D3D12_RESOURCE_STATES		_state	  = D3D12_RESOURCE_STATE_COMMON;
			uint32						_width	  = 0;
			uint32						_height	  = 0;
		};
		std::unordered_map<RHITextureHandle, OffscreenTextureRecord> _offscreenTextures;
		uint32														 _nextOffscreenRtvIndex = 0;
		static constexpr uint32										 kMaxOffscreenRtvs		= 16;

		struct D3D12PipelineStateRecord
		{
			Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
		};
		struct D3D12RenderPassRecord
		{
			RHIRenderPassDesc desc;
		};
		std::vector<D3D12PipelineStateRecord> _pipelineStates;
		std::vector<D3D12RenderPassRecord>	  _renderPasses;

		struct BindlessResourceRecord
		{
			Microsoft::WRL::ComPtr<ID3D12Resource> _resource;
			D3D12_CPU_DESCRIPTOR_HANDLE			   _cpuHandle{};
			D3D12_GPU_DESCRIPTOR_HANDLE			   _gpuHandle{};
		};
		std::vector<BindlessResourceRecord> _registeredBindlessVector;
		std::vector<uint32>					_bindlessFreeList;

		std::vector<BindlessResourceRecord> _registeredUAVs;
		std::vector<uint32>					_uavFreeList;

		UINT _rtvDescriptorSize			= 0;
		UINT _cbvDescriptorSize			= 0;
		UINT _allocatedDescriptorsCount = 0;
		UINT _frameIndex				= 0;

		HANDLE								_fenceEvent = nullptr;
		Microsoft::WRL::ComPtr<ID3D12Fence> _fence;
		UINT64								_fenceValue = 0;

		HWND   _hWnd		= nullptr;
		uint32 _width		= 0;
		uint32 _height		= 0;
		uint32 _bufferCount = 2;

		RHIReleaseQueue _releaseQueue{ 3 };
	};
} // namespace sw
#else
namespace sw
{
	/** @brief 비-Windows 환경용 스텁 D3D12RHIDevice */
	class D3D12RHIDevice : public IRHIDevice
	{
	public:
		D3D12RHIDevice()		   = default;
		~D3D12RHIDevice() override = default;

		bool initialize( const RHISwapChainDesc& ) override { return false; }
		void shutdown() override {}
		void resize( uint32, uint32 ) override {}
		void beginFrame( float32[4] ) override {}
		void endFrame( bool ) override {}

		RHIBackend	getBackendType() const override { return RHIBackend::D3D12; }
		bool		supportsBindless() const override { return false; }
		const utf8* getBackendName() const override { return "Direct3D 12 (Not Supported on non-Windows)"; }

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
