#include "pch.h"

#include "Engine/Graphics/RHI/DX12/D3D12RHIDevice.h"

#include "Engine/Graphics/RHI/DX12/D3D12RHICommandContext.h"
#include "Engine/Graphics/RHI/DX12/D3D12RHIResource.h"
#include "Engine/Graphics/RHI/DX12/D3D12RHISwapChain.h"

#if defined( SW_PLATFORM_WINDOWS )
	#include "Engine/Common/EnginePlatformHeaders.h"
	#include "Engine/Config/EngineData.h"
	#include "Engine/Graphics/RHI/RHIDeferredCommandList.h"
	#include "Engine/Graphics/Shader/ShaderCache.h"

namespace sw
{
	SW_LOG_CALLER( "D3D12" );

	DXGI_FORMAT toDxgiFormatD3D12( RHIFormat format )
	{
		switch ( format )
		{
			case RHIFormat::R8G8B8A8_UNORM:
				return DXGI_FORMAT_R8G8B8A8_UNORM;
			case RHIFormat::B8G8R8A8_UNORM:
				return DXGI_FORMAT_B8G8R8A8_UNORM;
			case RHIFormat::R16G16B16A16_FLOAT:
				return DXGI_FORMAT_R16G16B16A16_FLOAT;
			case RHIFormat::D24_UNORM_S8_UINT:
				return DXGI_FORMAT_D24_UNORM_S8_UINT;
			case RHIFormat::R32G32B32_FLOAT:
				return DXGI_FORMAT_R32G32B32_FLOAT;
			case RHIFormat::R32G32_FLOAT:
				return DXGI_FORMAT_R32G32_FLOAT;
			case RHIFormat::R32_FLOAT:
				return DXGI_FORMAT_R32_FLOAT;
			default:
				break;
		}
		SW_LOG_ASSERT( false, "Unsupported RHIFormat: %#", static_cast<uint32>( format ) );
		return DXGI_FORMAT_UNKNOWN;
	}

	D3D12RHIDevice::D3D12RHIDevice()
		: _device{ nullptr }
		, _commandQueue{ nullptr }
		, _swapChain{ nullptr }
		, _rtvHeap{ nullptr }
		, _dsvHeap{ nullptr }
		, _cbvHeap{ nullptr }
		, _rootSignature{ nullptr }
		, _computeRootSignature{ nullptr }
		, _vertexBuffer{ nullptr }
		, _drawCommandSignature{ nullptr }
		, _drawIndexedCommandSignature{ nullptr }
		, _dispatchCommandSignature{ nullptr }
		, _arrCommandAllocators{}
		, _commandList{ nullptr }
		, _frameRing{}
		, _listRenderTarget{}
		, _gpuBuffers{}
		, _gpuTextures{}
		, _boundMeshVb{ 0 }
		, _boundMeshStride{ sizeof( RHIVertex ) }
		, _boundMeshOffset{ 0 }
		, _boundIndexBuffer{ 0 }
		, _boundIndexStride{ 4 }
		, _boundIndexOffset{ 0 }
		, _mapStructuredBufferState{}
		, _mapOffscreenTexture{}
		, _nextOffscreenRtvIndex{ 0 }
		, _nextOffscreenDsvIndex{ 0 }
		, _listFreeOffscreenRtvIndex{}
		, _listFreeOffscreenDsvIndex{}
		, _mapCbAlignedSize{}
		, _mapCbMapped{}
		, _pipelineStates{}
		, _listRenderPass{}
		, _activeGraphicsPso{ 0 }
		, _arrActiveColorTarget{}
		, _activeDepthTarget{ 0 }
		, _activeColorTargetCount{ 0 }
		, _swapchainState{ D3D12_RESOURCE_STATE_PRESENT }
		, _bActiveSwapchainRT{ SW_FALSE }
		, _bHeapDirectlyIndexed{ SW_FALSE }
		, _bRecording{ SW_FALSE }
		, _reservedPassFlags{ 0 }
		, _listRegisteredBindless{}
		, _listFreeBindless{}
		, _listRegisteredUAV{}
		, _listFreeUav{}
		, _rtvDescriptorSize{ 0 }
		, _cbvDescriptorSize{ 0 }
		, _allocatedDescriptorsCount{ 0 }
		, _frameIndex{ 0 }
		, _fenceEvent{ nullptr }
		, _fence{ nullptr }
		, _fenceValue{ 0 }
		, _pHWnd{ nullptr }
		, _width{ 0 }
		, _height{ 0 }
		, _bufferCount{ 2 }
		, _releaseQueue{ 3 }
		, _immContext{ nullptr }
		, _deferredContext{ nullptr }
		, _swapChainImpl{ nullptr }
		, _resourceImpl{ nullptr }
	{
		_swapChainImpl = sw::make_unique<D3D12RHISwapChain>( this );
		_resourceImpl  = sw::make_unique<D3D12RHIResource>( this );
	}

	D3D12RHIDevice::~D3D12RHIDevice()
	{
		shutdown();
	}

	bool D3D12RHIDevice::initializeInternal( const RHISwapChainDesc& desc )
	{
		_pHWnd		 = static_cast<HWND>( desc._pWindowHandle );
		_width		 = desc._width;
		_height		 = desc._height;
		_bufferCount = desc._bufferCount;

	#if defined( SW_DEBUG )
		{
			Microsoft::WRL::ComPtr<ID3D12Debug> debugController;
			if ( SUCCEEDED( D3D12GetDebugInterface( IID_PPV_ARGS( debugController.GetAddressOf() ) ) ) )
			{
				debugController->EnableDebugLayer();
				SW_LOG_INFO( "Debug layer enabled." );
			}

			Microsoft::WRL::ComPtr<ID3D12DeviceRemovedExtendedDataSettings> dredSettings;
			if ( SUCCEEDED( D3D12GetDebugInterface( IID_PPV_ARGS( dredSettings.GetAddressOf() ) ) ) )
			{
				dredSettings->SetAutoBreadcrumbsEnablement( D3D12_DRED_ENABLEMENT_FORCED_ON );
				dredSettings->SetPageFaultEnablement( D3D12_DRED_ENABLEMENT_FORCED_ON );
				SW_LOG_INFO( "DRED (Device Removed Extended Data) diagnostic enabled." );
			}
		}
	#endif

		Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
		if ( FAILED( CreateDXGIFactory1( IID_PPV_ARGS( factory.GetAddressOf() ) ) ) )
			return false;

		if ( FAILED( D3D12CreateDevice( nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS( _device.GetAddressOf() ) ) ) )
			return false;

		D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
		if ( SUCCEEDED( _device->CheckFeatureSupport( D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof( options ) ) ) )
		{
			if ( options.ResourceBindingTier >= D3D12_RESOURCE_BINDING_TIER_3 )
			{
				SW_LOG_TRACE( "Device supports Resource Binding Tier 3 (Bindless)." );
			}
			else
			{
				SW_LOG_WARNING( "Device does NOT support Resource Binding Tier 3. Fallback may be required." );
			}
		}

	#if defined( SW_DEBUG )
		{
			Microsoft::WRL::ComPtr<ID3D12InfoQueue> infoQueue;
			if ( SUCCEEDED( _device.As( &infoQueue ) ) )
			{
				infoQueue->SetBreakOnSeverity( D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE );
				infoQueue->SetBreakOnSeverity( D3D12_MESSAGE_SEVERITY_ERROR, FALSE );
			}
		}
	#endif

		D3D12_COMMAND_QUEUE_DESC queueDesc{};
		queueDesc.Type	= D3D12_COMMAND_LIST_TYPE_DIRECT;
		queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		if ( FAILED( _device->CreateCommandQueue( &queueDesc, IID_PPV_ARGS( _commandQueue.GetAddressOf() ) ) ) )
			return false;

		DXGI_SWAP_CHAIN_DESC1 scDesc{};
		scDesc.BufferCount		= _bufferCount;
		scDesc.Width			= _width;
		scDesc.Height			= _height;
		scDesc.Format			= DXGI_FORMAT_R8G8B8A8_UNORM;
		scDesc.BufferUsage		= DXGI_USAGE_RENDER_TARGET_OUTPUT;
		scDesc.SwapEffect		= DXGI_SWAP_EFFECT_FLIP_DISCARD;
		scDesc.SampleDesc.Count = 1;

		if ( _pHWnd != nullptr && _width > 0 && _height > 0 )
		{
			Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain1;
			if ( FAILED( factory->CreateSwapChainForHwnd( _commandQueue.Get(), _pHWnd, &scDesc, nullptr, nullptr, swapChain1.GetAddressOf() ) ) )
				return false;

			swapChain1.As( &_swapChain );
			_frameIndex = _swapChain->GetCurrentBackBufferIndex();
		}

		_bActiveSwapchainRT		= 0;
		_bHeapDirectlyIndexed	= 0;
		_activeColorTargetCount = 0;
		_activeDepthTarget		= 0;
		_swapchainState			= D3D12_RESOURCE_STATE_PRESENT;

		D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
		rtvHeapDesc.NumDescriptors = _bufferCount + kMaxOffscreenRtvs;
		rtvHeapDesc.Type		   = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		rtvHeapDesc.Flags		   = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		if ( FAILED( _device->CreateDescriptorHeap( &rtvHeapDesc, IID_PPV_ARGS( _rtvHeap.GetAddressOf() ) ) ) )
			return false;

		_rtvDescriptorSize = _device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_RTV );

		D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc{};
		dsvHeapDesc.NumDescriptors = kMaxOffscreenDsvs;
		dsvHeapDesc.Type		   = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
		dsvHeapDesc.Flags		   = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		if ( FAILED( _device->CreateDescriptorHeap( &dsvHeapDesc, IID_PPV_ARGS( _dsvHeap.GetAddressOf() ) ) ) )
			return false;

		D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc{};
		cbvHeapDesc.NumDescriptors = kMaxShaderVisibleDescriptors;
		cbvHeapDesc.Type		   = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		cbvHeapDesc.Flags		   = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		if ( FAILED( _device->CreateDescriptorHeap( &cbvHeapDesc, IID_PPV_ARGS( _cbvHeap.GetAddressOf() ) ) ) )
			return false;

		_cbvDescriptorSize = _device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );

		for ( uint32 frameIndex = 0; frameIndex < FrameResourceRing::kFrameCount; ++frameIndex )
		{
			if ( FAILED( _device->CreateCommandAllocator( D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS( _arrCommandAllocators[frameIndex].GetAddressOf() ) ) ) )
				return false;
		}

		if ( FAILED( _device->CreateCommandList( 0, D3D12_COMMAND_LIST_TYPE_DIRECT, _arrCommandAllocators[0].Get(), nullptr, IID_PPV_ARGS( _commandList.GetAddressOf() ) ) ) )
			return false;

		_commandList->Close();
		_bRecording = 0;
		_frameRing.reset( 0 );

		createRenderTargets();

		if ( FAILED( _device->CreateFence( 0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS( _fence.GetAddressOf() ) ) ) )
			return false;
		_fenceValue = 1;
		_fenceEvent = CreateEvent( nullptr, FALSE, FALSE, nullptr );

		if ( createGlobalResources() == false )
			return false;

		_immContext		 = sw::make_unique<D3D12RHICommandContext>( this );
		_deferredContext = sw::make_unique<D3D12RHICommandContext>( this );

		return true;
	}

	void D3D12RHIDevice::shutdownInternal()
	{
		waitForPreviousFrame();
		_releaseQueue.flushAll();

		_mapOffscreenTexture.clear();
		_pipelineStates.clear();
		_listRenderPass.clear();
		_listRegisteredBindless.clear();
		_listFreeBindless.clear();
		_listRegisteredUAV.clear();
		_listFreeUav.clear();
		_mapStructuredBufferState.clear();
		_gpuBuffers.clear();
		_gpuTextures.clear();
		_boundMeshVb			   = 0;
		_boundMeshStride		   = sizeof( RHIVertex );
		_boundMeshOffset		   = 0;
		_boundIndexBuffer		   = 0;
		_boundIndexStride		   = 4;
		_boundIndexOffset		   = 0;
		_nextOffscreenRtvIndex	   = 0;
		_allocatedDescriptorsCount = 0;

		cleanupRenderTargets();
		_vertexBuffer.Reset();
		_rootSignature.Reset();
		_computeRootSignature.Reset();
		_drawCommandSignature.Reset();
		_drawIndexedCommandSignature.Reset();
		_dispatchCommandSignature.Reset();
		_cbvHeap.Reset();
		_dsvHeap.Reset();
		_rtvHeap.Reset();
		_nextOffscreenDsvIndex = 0;
		_commandList.Reset();
		for ( Microsoft::WRL::ComPtr<ID3D12CommandAllocator>& allocator : _arrCommandAllocators )
		{
			allocator.Reset();
		}
		_bRecording = 0;
		_immContext.reset();
		_deferredContext.reset();
		_bHeapDirectlyIndexed = 0;
		_fence.Reset();
		_swapChain.Reset();
		_commandQueue.Reset();
		_device.Reset();

		if ( _fenceEvent != nullptr )
		{
			CloseHandle( _fenceEvent );
			_fenceEvent = nullptr;
		}

		_fenceValue		   = 0;
		_frameIndex		   = 0;
		_rtvDescriptorSize = 0;
		_cbvDescriptorSize = 0;
	}

	void D3D12RHIDevice::waitIdle()
	{
		waitForPreviousFrame();
		_releaseQueue.flushAll();
	}

	void* D3D12RHIDevice::getNativeTexturePointer( RHITextureHandle texture ) const
	{
		return resolveTexture( texture );
	}

	// ------------------------------------------------------------------------------
	// D3D12RHISwapChain Implementation
	// ------------------------------------------------------------------------------

	// ------------------------------------------------------------------------------
	// D3D12RHIResource Implementation
	// ------------------------------------------------------------------------------

	unique_ptr<IRHICommandList> D3D12RHIDevice::createCommandList( RHICommandListMode mode )
	{
		unique_ptr<RHIDeferredCommandList> list = make_unique<RHIDeferredCommandList>( mode, getCommandContextForMode( mode ) );
		return list;
	}

	void D3D12RHIDevice::executeCommandList( IRHICommandList* pCmdList )
	{
		RHIDeferredCommandList::execute( this, pCmdList );
	}

	void D3D12RHIDevice::createRenderTargets()
	{
		if ( _swapChain == nullptr || _device == nullptr || _rtvHeap == nullptr )
			return;

		_listRenderTarget.resize( _bufferCount );
		D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle( _rtvHeap->GetCPUDescriptorHandleForHeapStart() );

		for ( UINT bufferIndex = 0; bufferIndex < _bufferCount; ++bufferIndex )
		{
			const HRESULT getHr = _swapChain->GetBuffer( bufferIndex, IID_PPV_ARGS( _listRenderTarget[bufferIndex].GetAddressOf() ) );
			if ( FAILED( getHr ) )
			{
				SW_LOG_ERROR( "GetBuffer(%#) failed hr=0x%#", bufferIndex, static_cast<uint32>( getHr ) );
				continue;
			}
			_device->CreateRenderTargetView( _listRenderTarget[bufferIndex].Get(), nullptr, rtvHandle );
			rtvHandle.ptr += _rtvDescriptorSize;
		}
	}

	void D3D12RHIDevice::cleanupRenderTargets()
	{
		for ( Microsoft::WRL::ComPtr<ID3D12Resource>& rt : _listRenderTarget )
		{
			rt.Reset();
		}
		_listRenderTarget.clear();
	}

	void D3D12RHIDevice::waitForPreviousFrame()
	{
		if ( _commandQueue == nullptr || _fence == nullptr )
			return;

		if ( _device != nullptr && FAILED( _device->GetDeviceRemovedReason() ) )
			return;

		const UINT64  fenceToWait = _fenceValue;
		const HRESULT signalHr	  = _commandQueue->Signal( _fence.Get(), fenceToWait );
		_fenceValue++;

		if ( FAILED( signalHr ) )
		{
			SW_LOG_ERROR( "Fence Signal failed hr=0x%# (DeviceRemoved=0x%#)",
						  static_cast<uint32>( signalHr ),
						  static_cast<uint32>( _device ? _device->GetDeviceRemovedReason() : S_OK ) );
			return;
		}

		if ( _fence->GetCompletedValue() < fenceToWait )
		{
			if ( _fenceEvent == nullptr )
				return;

			_fence->SetEventOnCompletion( fenceToWait, _fenceEvent );
			const DWORD waitResult = WaitForSingleObject( _fenceEvent, 2000 );
			if ( waitResult != WAIT_OBJECT_0 )
				SW_LOG_ERROR( "Fence wait timed out (result=%#, fence=%#)", static_cast<uint32>( waitResult ), static_cast<uint32>( fenceToWait ) );
		}

		if ( _swapChain != nullptr && ( _device == nullptr || SUCCEEDED( _device->GetDeviceRemovedReason() ) ) )
			_frameIndex = _swapChain->GetCurrentBackBufferIndex();
	}

	void D3D12RHIDevice::waitForRingSlot()
	{
		if ( _fence == nullptr )
			return;

		if ( _device != nullptr && FAILED( _device->GetDeviceRemovedReason() ) )
			return;

		const uint64 completed = _fence->GetCompletedValue();
		if ( _frameRing.beginFrame( completed ) )
			return;

		const uint32 nextIndex = ( _frameRing.currentIndex() + 1 ) % FrameResourceRing::kFrameCount;
		const uint64 waitValue = _frameRing.getFenceValue( nextIndex );
		if ( _fence->GetCompletedValue() < waitValue && _fenceEvent != nullptr )
		{
			_fence->SetEventOnCompletion( waitValue, _fenceEvent );
			WaitForSingleObject( _fenceEvent, 2000 );
		}
		_frameRing.beginFrame( _fence->GetCompletedValue() );
	}

	void D3D12RHIDevice::signalCurrentFrame()
	{
		if ( _commandQueue == nullptr || _fence == nullptr )
			return;

		if ( _device != nullptr && FAILED( _device->GetDeviceRemovedReason() ) )
			return;

		const UINT64  fenceToSignal = _fenceValue;
		const HRESULT signalHr		= _commandQueue->Signal( _fence.Get(), fenceToSignal );
		_fenceValue++;
		if ( FAILED( signalHr ) )
		{
			SW_LOG_ERROR( "Fence Signal failed hr=0x%#", static_cast<uint32>( signalHr ) );
			return;
		}
		_frameRing.setFenceValue( _frameRing.currentIndex(), fenceToSignal );
		_releaseQueue.tickCompleted( _fence->GetCompletedValue() );
	}

	ID3D12CommandAllocator* D3D12RHIDevice::currentAllocator()
	{
		return _arrCommandAllocators[_frameRing.currentIndex()].Get();
	}

	ID3D12Resource* D3D12RHIDevice::resolveBuffer( RHIBufferHandle handle ) const
	{
		const Microsoft::WRL::ComPtr<ID3D12Resource>* slot = _gpuBuffers.get( handle );
		return slot != nullptr ? slot->Get() : nullptr;
	}

	ID3D12Resource* D3D12RHIDevice::resolveTexture( RHITextureHandle handle ) const
	{
		const Microsoft::WRL::ComPtr<ID3D12Resource>* slot = _gpuTextures.get( handle );
		return slot != nullptr ? slot->Get() : nullptr;
	}

	RHIBufferHandle D3D12RHIDevice::storeBuffer( Microsoft::WRL::ComPtr<ID3D12Resource> buffer )
	{
		if ( buffer == nullptr )
			return 0;
		return _gpuBuffers.insert( std::move( buffer ) );
	}

	RHITextureHandle D3D12RHIDevice::storeTexture( Microsoft::WRL::ComPtr<ID3D12Resource> texture )
	{
		if ( texture == nullptr )
			return 0;
		return _gpuTextures.insert( std::move( texture ) );
	}

	bool D3D12RHIDevice::createGlobalResources()
	{
		D3D12_DESCRIPTOR_RANGE descriptorRanges[10]{};
		descriptorRanges[0].RangeType						  = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
		descriptorRanges[0].NumDescriptors					  = 1024;
		descriptorRanges[0].BaseShaderRegister				  = 0;
		descriptorRanges[0].RegisterSpace					  = 0;
		descriptorRanges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

		for ( uint32 subpassIndex = 0; subpassIndex < 4; ++subpassIndex )
		{
			descriptorRanges[1 + subpassIndex].RangeType						 = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
			descriptorRanges[1 + subpassIndex].NumDescriptors					 = 1;
			descriptorRanges[1 + subpassIndex].BaseShaderRegister				 = subpassIndex;
			descriptorRanges[1 + subpassIndex].RegisterSpace					 = 0;
			descriptorRanges[1 + subpassIndex].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
		}
		for ( uint32 subpassIndex = 0; subpassIndex < 4; ++subpassIndex )
		{
			descriptorRanges[5 + subpassIndex].RangeType						 = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
			descriptorRanges[5 + subpassIndex].NumDescriptors					 = 1;
			descriptorRanges[5 + subpassIndex].BaseShaderRegister				 = subpassIndex;
			descriptorRanges[5 + subpassIndex].RegisterSpace					 = 0;
			descriptorRanges[5 + subpassIndex].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
		}
		descriptorRanges[9].RangeType						  = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		descriptorRanges[9].NumDescriptors					  = 1024;
		descriptorRanges[9].BaseShaderRegister				  = 0;
		descriptorRanges[9].RegisterSpace					  = 1;
		descriptorRanges[9].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

		D3D12_ROOT_PARAMETER rootParameters[11]{};
		for ( uint32 paramIndex = 0; paramIndex < 10; ++paramIndex )
		{
			rootParameters[paramIndex].ParameterType					   = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
			rootParameters[paramIndex].DescriptorTable.NumDescriptorRanges = 1;
			rootParameters[paramIndex].DescriptorTable.pDescriptorRanges   = &descriptorRanges[paramIndex];
			rootParameters[paramIndex].ShaderVisibility					   = D3D12_SHADER_VISIBILITY_ALL;
		}
		rootParameters[kComputeRootConstantsParam].ParameterType			= D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
		rootParameters[kComputeRootConstantsParam].Constants.ShaderRegister = 0;
		rootParameters[kComputeRootConstantsParam].Constants.RegisterSpace	= 1; ///< bindless.hlsli: b0, space1 (g_BindlessCbIndex)
		rootParameters[kComputeRootConstantsParam].Constants.Num32BitValues = kMaxComputeRootConstantDwords;
		rootParameters[kComputeRootConstantsParam].ShaderVisibility			= D3D12_SHADER_VISIBILITY_ALL;

		D3D12_STATIC_SAMPLER_DESC staticSamplers[2]{};
		staticSamplers[0].Filter		   = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		staticSamplers[0].AddressU		   = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		staticSamplers[0].AddressV		   = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		staticSamplers[0].AddressW		   = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		staticSamplers[0].MipLODBias	   = 0.0f;
		staticSamplers[0].MaxAnisotropy	   = 1;
		staticSamplers[0].ComparisonFunc   = D3D12_COMPARISON_FUNC_ALWAYS;
		staticSamplers[0].BorderColor	   = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
		staticSamplers[0].MinLOD		   = 0.0f;
		staticSamplers[0].MaxLOD		   = D3D12_FLOAT32_MAX;
		staticSamplers[0].ShaderRegister   = 0;
		staticSamplers[0].RegisterSpace	   = 0;
		staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

		staticSamplers[1].Filter		   = D3D12_FILTER_MIN_MAG_MIP_POINT;
		staticSamplers[1].AddressU		   = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		staticSamplers[1].AddressV		   = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		staticSamplers[1].AddressW		   = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		staticSamplers[1].MipLODBias	   = 0.0f;
		staticSamplers[1].MaxAnisotropy	   = 1;
		staticSamplers[1].ComparisonFunc   = D3D12_COMPARISON_FUNC_ALWAYS;
		staticSamplers[1].BorderColor	   = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
		staticSamplers[1].MinLOD		   = 0.0f;
		staticSamplers[1].MaxLOD		   = D3D12_FLOAT32_MAX;
		staticSamplers[1].ShaderRegister   = 1;
		staticSamplers[1].RegisterSpace	   = 0;
		staticSamplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

		D3D12_ROOT_SIGNATURE_DESC rootSigDesc{};
		rootSigDesc.NumParameters	  = _countof( rootParameters );
		rootSigDesc.pParameters		  = rootParameters;
		rootSigDesc.NumStaticSamplers = _countof( staticSamplers );
		rootSigDesc.pStaticSamplers	  = staticSamplers;
		rootSigDesc.Flags			  = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
										D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;

		Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob;
		Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
		_bHeapDirectlyIndexed = 0;

		auto tryCreateRootSigs = [&]( D3D12_ROOT_SIGNATURE_FLAGS flags ) -> bool
		{
			rootSigDesc.Flags = flags;
			signatureBlob.Reset();
			errorBlob.Reset();
			if ( FAILED( D3D12SerializeRootSignature( &rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1_0, &signatureBlob, &errorBlob ) ) )
			{
				if ( errorBlob )
					SW_LOG_ERROR( "Root Signature Serialize Error: %s", static_cast<const utf8*>( errorBlob->GetBufferPointer() ) );
				return false;
			}
			_rootSignature.Reset();
			_computeRootSignature.Reset();
			if ( FAILED( _device->CreateRootSignature( 0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS( _rootSignature.GetAddressOf() ) ) ) )
				return false;
			if ( FAILED( _device->CreateRootSignature( 0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS( _computeRootSignature.GetAddressOf() ) ) ) )
			{
				_rootSignature.Reset();
				return false;
			}
			return true;
		};

		const D3D12_ROOT_SIGNATURE_FLAGS kIndexedFlags =
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
			D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;
		const D3D12_ROOT_SIGNATURE_FLAGS kFallbackFlags =
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

		if ( tryCreateRootSigs( kIndexedFlags ) )
			_bHeapDirectlyIndexed = 1;
		else if ( tryCreateRootSigs( kFallbackFlags ) )
		{
			// 정적 Caps의 native bindless는 후보; 런타임은 supportsNativeBindlessSampling()/getCapabilities().
			_bHeapDirectlyIndexed = 0;
			SW_LOG_TRACE( "Native heap indexing unavailable — bind-at-draw root signature (caps._bNativeBindless=0)" );
		}
		else
			return false;

		D3D12_INDIRECT_ARGUMENT_DESC drawArg{};
		drawArg.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;

		D3D12_COMMAND_SIGNATURE_DESC drawCmdSigDesc{};
		drawCmdSigDesc.ByteStride		= sizeof( D3D12_DRAW_ARGUMENTS );
		drawCmdSigDesc.NumArgumentDescs = 1;
		drawCmdSigDesc.pArgumentDescs	= &drawArg;
		_device->CreateCommandSignature( &drawCmdSigDesc, nullptr, IID_PPV_ARGS( _drawCommandSignature.GetAddressOf() ) );

		D3D12_INDIRECT_ARGUMENT_DESC drawIndexedArg{};
		drawIndexedArg.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;

		D3D12_COMMAND_SIGNATURE_DESC drawIndexedCmdSigDesc{};
		drawIndexedCmdSigDesc.ByteStride	   = sizeof( D3D12_DRAW_INDEXED_ARGUMENTS );
		drawIndexedCmdSigDesc.NumArgumentDescs = 1;
		drawIndexedCmdSigDesc.pArgumentDescs   = &drawIndexedArg;
		_device->CreateCommandSignature( &drawIndexedCmdSigDesc, nullptr, IID_PPV_ARGS( _drawIndexedCommandSignature.GetAddressOf() ) );

		D3D12_INDIRECT_ARGUMENT_DESC dispatchArg{};
		dispatchArg.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;

		D3D12_COMMAND_SIGNATURE_DESC dispatchCmdSigDesc{};
		dispatchCmdSigDesc.ByteStride		= sizeof( D3D12_DISPATCH_ARGUMENTS );
		dispatchCmdSigDesc.NumArgumentDescs = 1;
		dispatchCmdSigDesc.pArgumentDescs	= &dispatchArg;
		_device->CreateCommandSignature( &dispatchCmdSigDesc, nullptr, IID_PPV_ARGS( _dispatchCommandSignature.GetAddressOf() ) );

		{
			const RHIVertex arrFullscreenVerts[3] = {
				{{ -1.0f, -1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }},
				{ { 3.0f, -1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }},
				{ { -1.0f, 3.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }},
			};

			D3D12_HEAP_PROPERTIES heapProps{};
			heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

			D3D12_RESOURCE_DESC resDesc{};
			resDesc.Dimension		 = D3D12_RESOURCE_DIMENSION_BUFFER;
			resDesc.Width			 = sizeof( arrFullscreenVerts );
			resDesc.Height			 = 1;
			resDesc.DepthOrArraySize = 1;
			resDesc.MipLevels		 = 1;
			resDesc.Format			 = DXGI_FORMAT_UNKNOWN;
			resDesc.SampleDesc.Count = 1;
			resDesc.Layout			 = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

			if ( FAILED( _device->CreateCommittedResource( &heapProps, D3D12_HEAP_FLAG_NONE, &resDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
														   nullptr, IID_PPV_ARGS( _vertexBuffer.GetAddressOf() ) ) ) )
			{
				SW_LOG_ERROR( "Failed to create fullscreen vertex buffer." );
				return false;
			}

			void* pMapped{ nullptr };
			if ( FAILED( _vertexBuffer->Map( 0, nullptr, &pMapped ) ) || pMapped == nullptr )
			{
				SW_LOG_ERROR( "Failed to map fullscreen vertex buffer." );
				_vertexBuffer.Reset();
				return false;
			}
			Memory::copy( pMapped, arrFullscreenVerts, sizeof( arrFullscreenVerts ) );
			_vertexBuffer->Unmap( 0, nullptr );
		}

		return true;
	}

	void D3D12RHIDevice::flushDebugMessages( const utf8* pStage )
	{
	#if defined( SW_DEBUG )
		Microsoft::WRL::ComPtr<ID3D12InfoQueue> infoQueue;
		if ( SUCCEEDED( _device.As( &infoQueue ) ) && infoQueue != nullptr )
		{
			const uint64 messageCount = infoQueue->GetNumStoredMessages();
			for ( uint64 messageIndex = 0; messageIndex < messageCount; ++messageIndex )
			{
				SIZE_T messageLength{ 0 };
				infoQueue->GetMessage( messageIndex, nullptr, &messageLength );
				vector<uint8>  bytes( messageLength );
				D3D12_MESSAGE* pMessage = reinterpret_cast<D3D12_MESSAGE*>( bytes.data() );
				if ( SUCCEEDED( infoQueue->GetMessage( messageIndex, pMessage, &messageLength ) ) )
					SW_LOG_ERROR( "%#", pStage, pMessage->pDescription );
			}
			infoQueue->ClearStoredMessages();
		}

		if ( _device != nullptr && FAILED( _device->GetDeviceRemovedReason() ) )
		{
			Microsoft::WRL::ComPtr<ID3D12DeviceRemovedExtendedData> dred;
			if ( SUCCEEDED( _device.As( &dred ) ) && dred != nullptr )
			{
				D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT autoBreadcrumbsOutput{};
				if ( SUCCEEDED( dred->GetAutoBreadcrumbsOutput( &autoBreadcrumbsOutput ) ) )
				{
					const D3D12_AUTO_BREADCRUMB_NODE* pNode = autoBreadcrumbsOutput.pHeadAutoBreadcrumbNode;
					while ( pNode != nullptr )
					{
						const uint32 executed = ( pNode->pCommandHistory != nullptr && pNode->pLastBreadcrumbValue != nullptr )
												  ? *pNode->pLastBreadcrumbValue
												  : 0;
						SW_LOG_ERROR( "CommandList='%#', Total=%#, Executed=%#",
									  pNode->pCommandListDebugNameA ? pNode->pCommandListDebugNameA : "unnamed",
									  pNode->BreadcrumbCount, executed );
						if ( pNode->pCommandHistory != nullptr && 0 < executed && executed <= pNode->BreadcrumbCount )
						{
							SW_LOG_ERROR( "Last completed Op index=%#, OpType=%#",
										  executed - 1, static_cast<uint32>( pNode->pCommandHistory[executed - 1] ) );
							if ( executed < pNode->BreadcrumbCount )
							{
								SW_LOG_ERROR( "Failed/In-Flight Op index=%#, OpType=%#",
											  executed, static_cast<uint32>( pNode->pCommandHistory[executed] ) );
							}
						}
						pNode = pNode->pNext;
					}
				}

				D3D12_DRED_PAGE_FAULT_OUTPUT pageFaultOutput{};
				if ( SUCCEEDED( dred->GetPageFaultAllocationOutput( &pageFaultOutput ) ) )
				{
					SW_LOG_ERROR( "PageFault VA=0x%016llX", static_cast<uint64>( pageFaultOutput.PageFaultVA ) );
				}
			}
		}
	#else
		(void)pStage;
	#endif
	}

	IRHISwapChain*		D3D12RHIDevice::getSwapChain() { return _swapChainImpl.get(); }
	IRHIResource*		D3D12RHIDevice::getResource() { return _resourceImpl.get(); }
	IRHICommandContext* D3D12RHIDevice::getImmediateContext() { return _immContext.get(); }
	IRHICommandContext* D3D12RHIDevice::getDeferredCommandContext() { return _deferredContext.get(); }

} // namespace sw
#endif
