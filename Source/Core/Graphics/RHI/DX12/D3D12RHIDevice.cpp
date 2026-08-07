/**
 * @file D3D12RHIDevice.cpp
 * @brief D3D12RHIDevice 구현
 */
#include "D3D12RHIDevice.h"

#if defined( SW_PLATFORM_WINDOWS )
	#include <d3dcompiler.h>
	#if defined( SW_DEBUG )
		#include <d3d12sdklayers.h>
	#endif

	#include "Core/Graphics/Shader/ShaderCache.h"
	#include "Core/Utility/Log/Logger.h"

namespace sw
{
	namespace
	{
		DXGI_FORMAT toDxgiFormat( RHIFormat format )
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
			}
			SW_LOG_ASSERT( false, "Unsupported RHIFormat: %#", static_cast<uint32>( format ) );
			return DXGI_FORMAT_UNKNOWN;
		}
	}

	D3D12RHIDevice::D3D12RHIDevice() = default;

	D3D12RHIDevice::~D3D12RHIDevice()
	{
		shutdown();
	}

	bool D3D12RHIDevice::initializeInternal( const RHISwapChainDesc& desc )
	{
		_hWnd		 = static_cast<HWND>( desc._windowHandle );
		_width		 = desc._width;
		_height		 = desc._height;
		_bufferCount = desc._bufferCount;

#if defined( SW_DEBUG )
		{
			Microsoft::WRL::ComPtr<ID3D12Debug> debugController;
			if ( SUCCEEDED( D3D12GetDebugInterface( IID_PPV_ARGS( debugController.GetAddressOf() ) ) ) )
			{
				debugController->EnableDebugLayer();
				SW_LOG_INFO( "[D3D12] Debug layer enabled." );
			}
		}
#endif

		Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
		if ( FAILED( CreateDXGIFactory1( IID_PPV_ARGS( factory.GetAddressOf() ) ) ) )
			return false;

		if ( FAILED( D3D12CreateDevice( nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS( _device.GetAddressOf() ) ) ) )
			return false;

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

		Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain1;
		if ( FAILED( factory->CreateSwapChainForHwnd( _commandQueue.Get(), _hWnd, &scDesc, nullptr, nullptr, swapChain1.GetAddressOf() ) ) )
			return false;

		swapChain1.As( &_swapChain );
		_frameIndex = _swapChain->GetCurrentBackBufferIndex();

		D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
		rtvHeapDesc.NumDescriptors = _bufferCount + kMaxOffscreenRtvs;
		rtvHeapDesc.Type		   = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		rtvHeapDesc.Flags		   = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		if ( FAILED( _device->CreateDescriptorHeap( &rtvHeapDesc, IID_PPV_ARGS( _rtvHeap.GetAddressOf() ) ) ) )
			return false;

		_rtvDescriptorSize = _device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_RTV );

		D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc{};
		cbvHeapDesc.NumDescriptors = 256;
		cbvHeapDesc.Type		   = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		cbvHeapDesc.Flags		   = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		if ( FAILED( _device->CreateDescriptorHeap( &cbvHeapDesc, IID_PPV_ARGS( _cbvHeap.GetAddressOf() ) ) ) )
			return false;

		_cbvDescriptorSize = _device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );

		if ( FAILED( _device->CreateCommandAllocator( D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS( _commandAllocator.GetAddressOf() ) ) ) )
			return false;

		if ( FAILED( _device->CreateCommandList( 0, D3D12_COMMAND_LIST_TYPE_DIRECT, _commandAllocator.Get(), nullptr, IID_PPV_ARGS( _commandList.GetAddressOf() ) ) ) )
			return false;

		_commandList->Close();

		createRenderTargets();

		if ( FAILED( _device->CreateFence( 0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS( _fence.GetAddressOf() ) ) ) )
			return false;
		_fenceValue = 1;
		_fenceEvent = CreateEvent( nullptr, FALSE, FALSE, nullptr );

		if ( createTriangleResources() == false )
			return false;

		return true;
	}

	void D3D12RHIDevice::flushDebugMessages( const utf8* stage )
	{
#if defined( SW_DEBUG )
		Microsoft::WRL::ComPtr<ID3D12InfoQueue> infoQueue;
		if ( FAILED( _device.As( &infoQueue ) ) || infoQueue == nullptr )
			return;

		const uint64 messageCount = infoQueue->GetNumStoredMessages();
		for ( uint64 i = 0; i < messageCount; ++i )
		{
			SIZE_T messageLength = 0;
			infoQueue->GetMessage( static_cast<UINT64>( i ), nullptr, &messageLength );
			std::vector<uint8> bytes( messageLength );
			auto* message = reinterpret_cast<D3D12_MESSAGE*>( bytes.data() );
			if ( SUCCEEDED( infoQueue->GetMessage( static_cast<UINT64>( i ), message, &messageLength ) ) )
			{
				SW_LOG_ERROR( "[D3D12 InfoQueue:%#] %#", stage, message->pDescription );
			}
		}
		infoQueue->ClearStoredMessages();
#else
		(void)stage;
#endif
	}

	bool D3D12RHIDevice::createTriangleResources()
	{

		ShaderCompileDesc vsDesc{};
		vsDesc._filePath			 = "Shaders/BindlessTriangle.hlsl";
		vsDesc._entryPoint			 = "VSMain";
		vsDesc._stage				 = ShaderStage::Vertex;
		vsDesc._targetFormat		 = ShaderTargetFormat::DXIL_D3D12;
		ShaderCompileResult vsResult = ShaderCache::getOrCompile( vsDesc );

		ShaderCompileDesc psDesc{};
		psDesc._filePath			 = "Shaders/BindlessTriangle.hlsl";
		psDesc._entryPoint			 = "PSMain";
		psDesc._stage				 = ShaderStage::Pixel;
		psDesc._targetFormat		 = ShaderTargetFormat::DXIL_D3D12;
		ShaderCompileResult psResult = ShaderCache::getOrCompile( psDesc );

		if ( vsResult._bSuccess == false || psResult._bSuccess == false )
		{
			SW_LOG_ERROR( "[D3D12] Failed to compile BindlessTriangle.hlsl!" );
			return false;
		}

		D3D12_DESCRIPTOR_RANGE descriptorRanges[6]{};
		descriptorRanges[0].RangeType						  = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
		descriptorRanges[0].NumDescriptors					  = 256;
		descriptorRanges[0].BaseShaderRegister				  = 0;
		descriptorRanges[0].RegisterSpace					  = 0;
		descriptorRanges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

		for ( uint32 i = 0; i < 4; ++i )
		{
			descriptorRanges[1 + i].RangeType						  = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
			descriptorRanges[1 + i].NumDescriptors					  = 1;
			descriptorRanges[1 + i].BaseShaderRegister				  = i;
			descriptorRanges[1 + i].RegisterSpace					  = 0;
			descriptorRanges[1 + i].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
		}

		D3D12_ROOT_PARAMETER rootParameters[6]{};
		rootParameters[0].ParameterType			   = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
		rootParameters[0].Constants.ShaderRegister = 0;
		rootParameters[0].Constants.RegisterSpace  = 1;
		rootParameters[0].Constants.Num32BitValues = 1;
		rootParameters[0].ShaderVisibility		   = D3D12_SHADER_VISIBILITY_ALL;

		rootParameters[1].ParameterType						  = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		rootParameters[1].DescriptorTable.NumDescriptorRanges = 1;
		rootParameters[1].DescriptorTable.pDescriptorRanges	  = &descriptorRanges[0];
		rootParameters[1].ShaderVisibility					  = D3D12_SHADER_VISIBILITY_ALL;

		for ( uint32 i = 0; i < 4; ++i )
		{
			rootParameters[2 + i].ParameterType						  = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
			rootParameters[2 + i].DescriptorTable.NumDescriptorRanges = 1;
			rootParameters[2 + i].DescriptorTable.pDescriptorRanges	  = &descriptorRanges[1 + i];
			rootParameters[2 + i].ShaderVisibility					  = D3D12_SHADER_VISIBILITY_ALL;
		}

		D3D12_ROOT_SIGNATURE_DESC rootSigDesc{};
		rootSigDesc.NumParameters = 6;
		rootSigDesc.pParameters	  = rootParameters;
		rootSigDesc.Flags		  = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

		Microsoft::WRL::ComPtr<ID3DBlob> signature;
		Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
		if ( FAILED( D3D12SerializeRootSignature( &rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errorBlob ) ) )
			return false;

		if ( FAILED( _device->CreateRootSignature( 0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS( _rootSignature.GetAddressOf() ) ) ) )
			return false;

		// SampleIndirect.hlsl (non-BINDLESS): RWByteAddressBuffer u0 space1, u1 space2
		D3D12_DESCRIPTOR_RANGE computeUavRanges[2]{};
		computeUavRanges[0].RangeType						  = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
		computeUavRanges[0].NumDescriptors					  = 1;
		computeUavRanges[0].BaseShaderRegister				  = 0;
		computeUavRanges[0].RegisterSpace					  = 1;
		computeUavRanges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

		computeUavRanges[1].RangeType						  = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
		computeUavRanges[1].NumDescriptors					  = 1;
		computeUavRanges[1].BaseShaderRegister				  = 1;
		computeUavRanges[1].RegisterSpace					  = 2;
		computeUavRanges[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

		D3D12_ROOT_PARAMETER computeRootParameters[2]{};
		for ( uint32 i = 0; i < 2; ++i )
		{
			computeRootParameters[i].ParameterType						 = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
			computeRootParameters[i].DescriptorTable.NumDescriptorRanges = 1;
			computeRootParameters[i].DescriptorTable.pDescriptorRanges	 = &computeUavRanges[i];
			computeRootParameters[i].ShaderVisibility					 = D3D12_SHADER_VISIBILITY_ALL;
		}

		D3D12_ROOT_SIGNATURE_DESC computeRootSigDesc{};
		computeRootSigDesc.NumParameters = 2;
		computeRootSigDesc.pParameters	 = computeRootParameters;
		computeRootSigDesc.Flags		 = D3D12_ROOT_SIGNATURE_FLAG_NONE;

		Microsoft::WRL::ComPtr<ID3DBlob> computeSignature;
		Microsoft::WRL::ComPtr<ID3DBlob> computeErrorBlob;
		if ( FAILED( D3D12SerializeRootSignature( &computeRootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &computeSignature, &computeErrorBlob ) ) )
			return false;

		if ( FAILED( _device->CreateRootSignature( 0, computeSignature->GetBufferPointer(), computeSignature->GetBufferSize(), IID_PPV_ARGS( _computeRootSignature.GetAddressOf() ) ) ) )
			return false;

		D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = {
			{"POSITION", 0,	 DXGI_FORMAT_R32G32B32_FLOAT, 0,	 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
			{	  "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
		 };

		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
		psoDesc.InputLayout										 = { inputElementDescs, _countof( inputElementDescs ) };
		psoDesc.pRootSignature									 = _rootSignature.Get();
		psoDesc.VS												 = { vsResult._bytecode.data(), vsResult._bytecode.size() };
		psoDesc.PS												 = { psResult._bytecode.data(), psResult._bytecode.size() };
		psoDesc.RasterizerState.FillMode						 = D3D12_FILL_MODE_SOLID;
		psoDesc.RasterizerState.CullMode						 = D3D12_CULL_MODE_NONE;
		psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
		psoDesc.DepthStencilState.DepthEnable					 = FALSE;
		psoDesc.SampleMask										 = UINT_MAX;
		psoDesc.PrimitiveTopologyType							 = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.NumRenderTargets								 = 1;
		psoDesc.RTVFormats[0]									 = DXGI_FORMAT_R8G8B8A8_UNORM;
		psoDesc.SampleDesc.Count								 = 1;

		if ( FAILED( _device->CreateGraphicsPipelineState( &psoDesc, IID_PPV_ARGS( _pipelineState.GetAddressOf() ) ) ) )
			return false;

		RHIVertex vertices[] = {
			{  { 0.0f, 0.5f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }},
			{ { 0.5f, -0.5f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }},
			{{ -0.5f, -0.5f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }}
		   };

		const UINT			  vertexBufferSize = sizeof( vertices );
		D3D12_HEAP_PROPERTIES heapProps{};
		heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

		D3D12_RESOURCE_DESC resDesc{};
		resDesc.Dimension		 = D3D12_RESOURCE_DIMENSION_BUFFER;
		resDesc.Width			 = vertexBufferSize;
		resDesc.Height			 = 1;
		resDesc.DepthOrArraySize = 1;
		resDesc.MipLevels		 = 1;
		resDesc.Format			 = DXGI_FORMAT_UNKNOWN;
		resDesc.SampleDesc.Count = 1;
		resDesc.Layout			 = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

		if ( FAILED( _device->CreateCommittedResource( &heapProps, D3D12_HEAP_FLAG_NONE, &resDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS( _vertexBuffer.GetAddressOf() ) ) ) )
			return false;

		void* pVertexData = nullptr;
		_vertexBuffer->Map( 0, nullptr, &pVertexData );
		memcpy( pVertexData, vertices, sizeof( vertices ) );
		_vertexBuffer->Unmap( 0, nullptr );

		_vertexBufferView.BufferLocation = _vertexBuffer->GetGPUVirtualAddress();
		_vertexBufferView.StrideInBytes	 = sizeof( RHIVertex );
		_vertexBufferView.SizeInBytes	 = vertexBufferSize;

		D3D12_INDIRECT_ARGUMENT_DESC drawArg{};
		drawArg.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;

		D3D12_COMMAND_SIGNATURE_DESC drawCmdSigDesc{};
		drawCmdSigDesc.ByteStride		= sizeof( D3D12_DRAW_ARGUMENTS );
		drawCmdSigDesc.NumArgumentDescs = 1;
		drawCmdSigDesc.pArgumentDescs	= &drawArg;
		_device->CreateCommandSignature( &drawCmdSigDesc, nullptr, IID_PPV_ARGS( _drawCommandSignature.GetAddressOf() ) );

		D3D12_INDIRECT_ARGUMENT_DESC dispatchArg{};
		dispatchArg.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;

		D3D12_COMMAND_SIGNATURE_DESC dispatchCmdSigDesc{};
		dispatchCmdSigDesc.ByteStride		= sizeof( D3D12_DISPATCH_ARGUMENTS );
		dispatchCmdSigDesc.NumArgumentDescs = 1;
		dispatchCmdSigDesc.pArgumentDescs	= &dispatchArg;
		_device->CreateCommandSignature( &dispatchCmdSigDesc, nullptr, IID_PPV_ARGS( _dispatchCommandSignature.GetAddressOf() ) );

		return true;
	}

	RHIBufferHandle D3D12RHIDevice::createConstantBuffer( uint32 size )
	{
		UINT				  alignedSize = ( size + 255u ) & ~255u;
		D3D12_HEAP_PROPERTIES heapProps{};
		heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

		D3D12_RESOURCE_DESC resDesc{};
		resDesc.Dimension		 = D3D12_RESOURCE_DIMENSION_BUFFER;
		resDesc.Width			 = alignedSize;
		resDesc.Height			 = 1;
		resDesc.DepthOrArraySize = 1;
		resDesc.MipLevels		 = 1;
		resDesc.Format			 = DXGI_FORMAT_UNKNOWN;
		resDesc.SampleDesc.Count = 1;
		resDesc.Layout			 = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

		Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
		if ( FAILED( _device->CreateCommittedResource( &heapProps, D3D12_HEAP_FLAG_NONE, &resDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS( buffer.GetAddressOf() ) ) ) )
			return 0;

		_constantBuffers.push_back( buffer );
		return reinterpret_cast<RHIBufferHandle>( buffer.Get() );
	}

	void D3D12RHIDevice::updateConstantBuffer( RHIBufferHandle buffer, const void* data, uint32 size )
	{
		if ( buffer == 0 || data == nullptr )
			return;
		auto* res	 = reinterpret_cast<ID3D12Resource*>( buffer );
		void* mapped = nullptr;
		if ( SUCCEEDED( res->Map( 0, nullptr, &mapped ) ) )
		{
			memcpy( mapped, data, size );
			res->Unmap( 0, nullptr );
		}
	}

	RHIBufferHandle D3D12RHIDevice::createStructuredBuffer( uint32 elementSize, uint32 elementCount )
	{
		UINT				  alignedSize = elementSize * elementCount;
		D3D12_HEAP_PROPERTIES heapProps{};
		heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

		heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

		D3D12_RESOURCE_DESC resDesc{};
		resDesc.Dimension		 = D3D12_RESOURCE_DIMENSION_BUFFER;
		resDesc.Width			 = alignedSize;
		resDesc.Height			 = 1;
		resDesc.DepthOrArraySize = 1;
		resDesc.MipLevels		 = 1;
		resDesc.Format			 = DXGI_FORMAT_UNKNOWN;
		resDesc.SampleDesc.Count = 1;
		resDesc.Layout			 = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		resDesc.Flags            = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

		Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
		if ( FAILED( _device->CreateCommittedResource( &heapProps, D3D12_HEAP_FLAG_NONE, &resDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS( buffer.GetAddressOf() ) ) ) )
			return 0;

		_constantBuffers.push_back( buffer );
		return reinterpret_cast<RHIBufferHandle>( buffer.Get() );
	}

	void D3D12RHIDevice::updateStructuredBuffer( RHIBufferHandle buffer, const void* data, uint32 size )
	{
		(void)buffer;
		(void)data;
		(void)size;
	}

	void D3D12RHIDevice::destroyBuffer( RHIBufferHandle buffer )
	{
		if ( buffer == 0 )
			return;
		auto* res = reinterpret_cast<ID3D12Resource*>( buffer );
		for ( auto it = _constantBuffers.begin(); it != _constantBuffers.end(); ++it )
		{
			if ( it->Get() == res )
			{
				_constantBuffers.erase( it );
				break;
			}
		}
	}

	RHITextureHandle D3D12RHIDevice::createTexture2D( const RHITextureDesc& desc )
	{
		D3D12_HEAP_PROPERTIES heapProps{};
		heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

		D3D12_RESOURCE_DESC resDesc{};
		resDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		resDesc.Alignment = 0;
		resDesc.Width = desc._width;
		resDesc.Height = desc._height;
		resDesc.DepthOrArraySize = 1;
		resDesc.MipLevels = static_cast<UINT16>( desc._mipLevels );
		resDesc.Format = toDxgiFormat( desc._format );
		resDesc.SampleDesc.Count = 1;
		resDesc.SampleDesc.Quality = 0;
		resDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

		D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;
		if ( desc._bIsRenderTarget )
			flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
		if ( desc._bIsDepthStencil )
			flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
		if ( desc._bIsUnorderedAccess )
			flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

		resDesc.Flags = flags;

		D3D12_CLEAR_VALUE clearValue{};
		D3D12_CLEAR_VALUE* pClearValue = nullptr;
		if ( desc._bIsRenderTarget )
		{
			clearValue.Format = resDesc.Format;
			clearValue.Color[0] = desc._clearColor[0];
			clearValue.Color[1] = desc._clearColor[1];
			clearValue.Color[2] = desc._clearColor[2];
			clearValue.Color[3] = desc._clearColor[3];
			pClearValue = &clearValue;
		}
		else if ( desc._bIsDepthStencil )
		{
			clearValue.Format = resDesc.Format;
			clearValue.DepthStencil.Depth = desc._clearDepth;
			clearValue.DepthStencil.Stencil = desc._clearStencil;
			pClearValue = &clearValue;
		}

		Microsoft::WRL::ComPtr<ID3D12Resource> texture;
		if ( FAILED( _device->CreateCommittedResource( &heapProps, D3D12_HEAP_FLAG_NONE, &resDesc,
			D3D12_RESOURCE_STATE_COMMON, pClearValue, IID_PPV_ARGS( texture.GetAddressOf() ) ) ) )
		{
			return 0;
		}

		_textures.push_back( texture );

		const RHITextureHandle handle = reinterpret_cast<RHITextureHandle>( texture.Get() );
		if ( desc._bIsRenderTarget && _rtvHeap != nullptr && _nextOffscreenRtvIndex < kMaxOffscreenRtvs )
		{
			OffscreenTextureRecord record{};
			record._resource = texture.Get();
			record._rtvIndex = _bufferCount + _nextOffscreenRtvIndex++;
			record._rtvHandle = _rtvHeap->GetCPUDescriptorHandleForHeapStart();
			record._rtvHandle.ptr += static_cast<SIZE_T>( record._rtvIndex ) * _rtvDescriptorSize;
			record._state  = D3D12_RESOURCE_STATE_COMMON;
			record._width  = desc._width;
			record._height = desc._height;
			_device->CreateRenderTargetView( texture.Get(), nullptr, record._rtvHandle );
			_offscreenTextures[handle] = record;
		}

		return handle;
	}

	void D3D12RHIDevice::destroyTexture( RHITextureHandle texture )
	{
		if ( texture == 0 )
			return;
		_offscreenTextures.erase( texture );
		auto* res = reinterpret_cast<ID3D12Resource*>( texture );
		for ( auto it = _textures.begin(); it != _textures.end(); ++it )
		{
			if ( it->Get() == res )
			{
				_textures.erase( it );
				break;
			}
		}
	}

	void D3D12RHIDevice::beginOffscreenPass( RHITextureHandle colorTarget, float32 clearColor[4] )
	{
		if ( colorTarget == 0 )
		{
			beginFrame( clearColor );
			return;
		}

		auto it = _offscreenTextures.find( colorTarget );
		if ( it == _offscreenTextures.end() || _commandList == nullptr )
			return;

		OffscreenTextureRecord& record = it->second;
		_commandAllocator->Reset();
		_commandList->Reset( _commandAllocator.Get(), nullptr );

		if ( record._state != D3D12_RESOURCE_STATE_RENDER_TARGET )
		{
			D3D12_RESOURCE_BARRIER barrier{};
			barrier.Type				   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barrier.Transition.pResource   = record._resource;
			barrier.Transition.StateBefore = record._state;
			barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
			barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			_commandList->ResourceBarrier( 1, &barrier );
			record._state = D3D12_RESOURCE_STATE_RENDER_TARGET;
		}

		_commandList->OMSetRenderTargets( 1, &record._rtvHandle, FALSE, nullptr );
		_commandList->ClearRenderTargetView( record._rtvHandle, clearColor, 0, nullptr );

		D3D12_VIEWPORT vp{};
		vp.Width	= static_cast<float32>( record._width );
		vp.Height	= static_cast<float32>( record._height );
		vp.MinDepth = 0.0f;
		vp.MaxDepth = 1.0f;
		_commandList->RSSetViewports( 1, &vp );

		D3D12_RECT scissorRect{ 0, 0, static_cast<LONG>( record._width ), static_cast<LONG>( record._height ) };
		_commandList->RSSetScissorRects( 1, &scissorRect );
	}

	void D3D12RHIDevice::endOffscreenPass( RHITextureHandle colorTarget )
	{
		if ( colorTarget == 0 || _commandList == nullptr )
			return;

		auto it = _offscreenTextures.find( colorTarget );
		if ( it == _offscreenTextures.end() )
			return;

		OffscreenTextureRecord& record = it->second;
		if ( record._state != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE )
		{
			D3D12_RESOURCE_BARRIER barrier{};
			barrier.Type				   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barrier.Transition.pResource   = record._resource;
			barrier.Transition.StateBefore = record._state;
			barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
			barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			_commandList->ResourceBarrier( 1, &barrier );
			record._state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		}

		_commandList->Close();
		ID3D12CommandList* lists[] = { _commandList.Get() };
		_commandQueue->ExecuteCommandLists( 1, lists );
		waitForPreviousFrame();
	}

	RHIDescriptorIndex D3D12RHIDevice::registerBindlessTexture( RHITextureHandle texture )
	{
		if ( texture == 0 || _cbvHeap == nullptr )
			return kInvalidDescriptorIndex;

		auto* res = reinterpret_cast<ID3D12Resource*>( texture );
		RHIDescriptorIndex index;
		if ( _bindlessFreeList.empty() == false )
		{
			index = _bindlessFreeList.back();
			_bindlessFreeList.pop_back();
		}
		else
		{
			index = _allocatedDescriptorsCount++;
		}

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = res->GetDesc().Format;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.MipLevels = res->GetDesc().MipLevels;
		srvDesc.Texture2D.PlaneSlice = 0;
		srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

		D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle( _cbvHeap->GetCPUDescriptorHandleForHeapStart() );
		cpuHandle.ptr += index * _cbvDescriptorSize;

		D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle( _cbvHeap->GetGPUDescriptorHandleForHeapStart() );
		gpuHandle.ptr += index * _cbvDescriptorSize;

		_device->CreateShaderResourceView( res, &srvDesc, cpuHandle );

		if ( index >= _registeredBindlessVector.size() )
		{
			_registeredBindlessVector.resize( index + 1 );
		}
		_registeredBindlessVector[index] = { res, cpuHandle, gpuHandle };

		return index;
	}

	RHIDescriptorIndex D3D12RHIDevice::registerBindlessResource( RHIBufferHandle buffer )
	{
		if ( buffer == 0 || _cbvHeap == nullptr )
			return kInvalidDescriptorIndex;

		auto* res	= reinterpret_cast<ID3D12Resource*>( buffer );
		RHIDescriptorIndex index;
		if ( _bindlessFreeList.empty() == false )
		{
			index = _bindlessFreeList.back();
			_bindlessFreeList.pop_back();
		}
		else
		{
			index = _allocatedDescriptorsCount++;
		}

		D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc{};
		cbvDesc.BufferLocation = res->GetGPUVirtualAddress();
		cbvDesc.SizeInBytes	   = static_cast<UINT>( res->GetDesc().Width );

		D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle( _cbvHeap->GetCPUDescriptorHandleForHeapStart() );
		cpuHandle.ptr += index * _cbvDescriptorSize;

		D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle( _cbvHeap->GetGPUDescriptorHandleForHeapStart() );
		gpuHandle.ptr += index * _cbvDescriptorSize;

		_device->CreateConstantBufferView( &cbvDesc, cpuHandle );

		if ( index >= _registeredBindlessVector.size() )
		{
			_registeredBindlessVector.resize( index + 1 );
		}
		_registeredBindlessVector[index] = { res, cpuHandle, gpuHandle };

		return index;
	}

	void D3D12RHIDevice::unregisterBindlessResource( RHIDescriptorIndex index )
	{
		if ( index < _registeredBindlessVector.size() )
		{
			_registeredBindlessVector[index]._resource = nullptr;
			_bindlessFreeList.push_back( index );
		}
	}

	RHIDescriptorIndex D3D12RHIDevice::registerBindlessUAV( RHIBufferHandle buffer )
	{
		if ( buffer == 0 || _cbvHeap == nullptr )
			return kInvalidDescriptorIndex;

		ID3D12Resource* res = reinterpret_cast<ID3D12Resource*>( buffer );
		RHIDescriptorIndex descriptorIndex = 0;
		if ( _uavFreeList.empty() == false )
		{
			descriptorIndex = _uavFreeList.back();
			_uavFreeList.pop_back();
		}
		else
		{
			descriptorIndex = static_cast<RHIDescriptorIndex>( _registeredUAVs.size() );
			_registeredUAVs.push_back( BindlessResourceRecord{} );
		}

		// RWByteAddressBuffer / RAW UAV: R32_TYPELESS + RAW, StructureByteStride must be 0.
		const RHIDescriptorIndex heapSlot = _allocatedDescriptorsCount++;
		D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = _cbvHeap->GetCPUDescriptorHandleForHeapStart();
		cpuHandle.ptr += heapSlot * _cbvDescriptorSize;

		D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = _cbvHeap->GetGPUDescriptorHandleForHeapStart();
		gpuHandle.ptr += heapSlot * _cbvDescriptorSize;

		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
		uavDesc.ViewDimension				  = D3D12_UAV_DIMENSION_BUFFER;
		uavDesc.Format						  = DXGI_FORMAT_R32_TYPELESS;
		uavDesc.Buffer.FirstElement			  = 0;
		uavDesc.Buffer.NumElements			  = static_cast<UINT>( res->GetDesc().Width / 4 );
		uavDesc.Buffer.StructureByteStride	  = 0;
		uavDesc.Buffer.Flags				  = D3D12_BUFFER_UAV_FLAG_RAW;

		_device->CreateUnorderedAccessView( res, nullptr, &uavDesc, cpuHandle );

		if ( descriptorIndex >= _registeredUAVs.size() )
		{
			_registeredUAVs.resize( descriptorIndex + 1 );
		}
		_registeredUAVs[descriptorIndex] = { res, cpuHandle, gpuHandle };

		return descriptorIndex;
	}

	void D3D12RHIDevice::unregisterBindlessUAV( RHIDescriptorIndex index )
	{
		if ( index < _registeredUAVs.size() )
		{
			_registeredUAVs[index]._resource = nullptr;
			_uavFreeList.push_back( index );
		}
	}

	void D3D12RHIDevice::bindComputeUAV( RHIDescriptorIndex index, uint32 slot )
	{
		ID3D12RootSignature* computeRootSig = _computeRootSignature ? _computeRootSignature.Get() : _rootSignature.Get();
		if ( _commandList != nullptr && computeRootSig != nullptr && index < _registeredUAVs.size() && _registeredUAVs[index]._resource != nullptr && slot < 2 )
		{
			ID3D12DescriptorHeap* heaps[] = { _cbvHeap.Get() };
			_commandList->SetDescriptorHeaps( 1, heaps );
			_commandList->SetComputeRootSignature( computeRootSig );
			_commandList->SetComputeRootDescriptorTable( slot, _registeredUAVs[index]._gpuHandle );
		}
	}

	void D3D12RHIDevice::drawTriangle( RHIDescriptorIndex materialDescriptorIndex )
	{
		if ( _commandList == nullptr || _pipelineState == nullptr || _rootSignature == nullptr )
			return;

		_commandList->SetPipelineState( _pipelineState.Get() );
		_commandList->SetGraphicsRootSignature( _rootSignature.Get() );

		ID3D12DescriptorHeap* heaps[] = { _cbvHeap.Get() };
		_commandList->SetDescriptorHeaps( 1, heaps );

		_commandList->SetGraphicsRoot32BitConstant( 0, materialDescriptorIndex, 0 );

		_commandList->SetGraphicsRootDescriptorTable( 1, _cbvHeap->GetGPUDescriptorHandleForHeapStart() );

		_commandList->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
		_commandList->IASetVertexBuffers( 0, 1, &_vertexBufferView );
		_commandList->DrawInstanced( 3, 1, 0, 0 );
	}

	void D3D12RHIDevice::createRenderTargets()
	{
		_renderTargets.resize( _bufferCount );
		D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle( _rtvHeap->GetCPUDescriptorHandleForHeapStart() );

		for ( UINT bufferIndex = 0; bufferIndex < _bufferCount; ++bufferIndex )
		{
			_swapChain->GetBuffer( bufferIndex, IID_PPV_ARGS( _renderTargets[bufferIndex].GetAddressOf() ) );
			_device->CreateRenderTargetView( _renderTargets[bufferIndex].Get(), nullptr, rtvHandle );
			rtvHandle.ptr += _rtvDescriptorSize;
		}
	}

	void D3D12RHIDevice::cleanupRenderTargets()
	{
		for ( auto& rt : _renderTargets )
			rt.Reset();
		_renderTargets.clear();
	}

	void D3D12RHIDevice::waitForPreviousFrame()
	{
		if ( _commandQueue == nullptr || _fence == nullptr )
			return;

		const UINT64 fenceToWait = _fenceValue;
		const HRESULT signalHr	 = _commandQueue->Signal( _fence.Get(), fenceToWait );
		_fenceValue++;

		if ( FAILED( signalHr ) )
		{
			SW_LOG_ERROR( "[D3D12] Fence Signal failed hr=0x%X (DeviceRemoved=0x%X)",
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
			{
				SW_LOG_ERROR( "[D3D12] Fence wait timed out (result=%#, fence=%#)", static_cast<uint32>( waitResult ), static_cast<uint32>( fenceToWait ) );
			}
		}

		if ( _swapChain != nullptr )
			_frameIndex = _swapChain->GetCurrentBackBufferIndex();
	}

	void D3D12RHIDevice::waitIdle()
	{
		waitForPreviousFrame();
	}

	void D3D12RHIDevice::shutdownInternal()
	{
		waitForPreviousFrame();

		_offscreenTextures.clear();
		_textures.clear();
		_pipelineStates.clear();
		_renderPasses.clear();
		_registeredBindlessVector.clear();
		_bindlessFreeList.clear();
		_registeredUAVs.clear();
		_uavFreeList.clear();
		_constantBuffers.clear();
		_nextOffscreenRtvIndex = 0;
		_allocatedDescriptorsCount = 0;

		cleanupRenderTargets();
		_vertexBuffer.Reset();
		_vertexBufferView = {};
		_pipelineState.Reset();
		_rootSignature.Reset();
		_computeRootSignature.Reset();
		_drawCommandSignature.Reset();
		_dispatchCommandSignature.Reset();
		_cbvHeap.Reset();
		_rtvHeap.Reset();
		_commandList.Reset();
		_commandAllocator.Reset();
		_fence.Reset();
		_swapChain.Reset();
		_commandQueue.Reset();
		_device.Reset();

		if ( _fenceEvent )
		{
			CloseHandle( _fenceEvent );
			_fenceEvent = nullptr;
		}

		_fenceValue = 0;
		_frameIndex = 0;
		_rtvDescriptorSize = 0;
		_cbvDescriptorSize = 0;
	}

	void D3D12RHIDevice::resize( uint32 width, uint32 height )
	{
		if ( _swapChain == nullptr || ( width == 0 && height == 0 ) )
			return;
		_width	= width;
		_height = height;

		waitForPreviousFrame();
		cleanupRenderTargets();
		_swapChain->ResizeBuffers( _bufferCount, width, height, DXGI_FORMAT_UNKNOWN, 0 );
		createRenderTargets();
		_frameIndex = _swapChain->GetCurrentBackBufferIndex();
	}

	void D3D12RHIDevice::beginFrame( float32 clearColor[4] )
	{
		_commandAllocator->Reset();
		_commandList->Reset( _commandAllocator.Get(), nullptr );

		D3D12_RESOURCE_BARRIER barrier{};
		barrier.Type				   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource   = _renderTargets[_frameIndex].Get();
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
		barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		_commandList->ResourceBarrier( 1, &barrier );

		D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle( _rtvHeap->GetCPUDescriptorHandleForHeapStart() );
		rtvHandle.ptr += ( _frameIndex * _rtvDescriptorSize );

		_commandList->OMSetRenderTargets( 1, &rtvHandle, FALSE, nullptr );
		_commandList->ClearRenderTargetView( rtvHandle, clearColor, 0, nullptr );

		constexpr float32 kDefaultViewportX		   = 0.0f;
		constexpr float32 kDefaultViewportY		   = 0.0f;
		constexpr float32 kDefaultViewportMinDepth = 0.0f;
		constexpr float32 kDefaultViewportMaxDepth = 1.0f;

		D3D12_VIEWPORT vp{};
		vp.Width	= static_cast<float32>( _width );
		vp.Height	= static_cast<float32>( _height );
		vp.MinDepth = kDefaultViewportMinDepth;
		vp.MaxDepth = kDefaultViewportMaxDepth;
		vp.TopLeftX = kDefaultViewportX;
		vp.TopLeftY = kDefaultViewportY;
		_commandList->RSSetViewports( 1, &vp );

		D3D12_RECT scissorRect{ 0, 0, static_cast<LONG>( _width ), static_cast<LONG>( _height ) };
		_commandList->RSSetScissorRects( 1, &scissorRect );
	}

	void D3D12RHIDevice::endFrame( bool vsync )
	{

		D3D12_RESOURCE_BARRIER barrier{};
		barrier.Type				   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource   = _renderTargets[_frameIndex].Get();
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
		barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		_commandList->ResourceBarrier( 1, &barrier );

		_commandList->Close();
		ID3D12CommandList* ppCommandLists[] = { _commandList.Get() };
		_commandQueue->ExecuteCommandLists( 1, ppCommandLists );

		const HRESULT presentHr = _swapChain->Present( vsync ? 1 : 0, 0 );
		if ( FAILED( presentHr ) )
		{
			const HRESULT removed = _device->GetDeviceRemovedReason();
			SW_LOG_ERROR( "[D3D12] Present failed hr=0x%X, DeviceRemovedReason=0x%X", static_cast<uint32>( presentHr ), static_cast<uint32>( removed ) );
			flushDebugMessages( "after Present" );
		}
		waitForPreviousFrame();
	}

	class D3D12CommandList final : public IRHICommandList
	{
	public:
		D3D12CommandList( D3D12RHIDevice* device )
			: _device{ device }
		{
		}

		void beginCommandList() override {}
		void endCommandList() override {}

		void setViewport( const RHIViewport& vp ) override
		{
			(void)vp;
		}

		void setPipelineState( RHIPipelineStateHandle pso ) override
		{
			if ( _device != nullptr )
				_device->setPipelineState( pso );
		}
		void beginRenderPass( const RHIRenderPassBeginInfo& beginInfo ) override
		{
			if ( _device != nullptr )
				_device->beginRenderPass( beginInfo );
		}
		void endRenderPass() override
		{
			if ( _device != nullptr )
				_device->endRenderPass();
		}

		void drawTriangle( RHIDescriptorIndex materialDescriptorIndex ) override
		{
			if ( _device != nullptr )
			{
				_device->drawTriangle( materialDescriptorIndex );
			}
		}

		void dispatchCompute( uint32 threadGroupCountX, uint32 threadGroupCountY, uint32 threadGroupCountZ ) override
		{
			if ( _device != nullptr )
			{
				_device->dispatchCompute( threadGroupCountX, threadGroupCountY, threadGroupCountZ );
			}
		}

		void setComputeRootConstants( uint32 rootParameterIndex, uint32 num32BitValues, const void* data, uint32 destOffsetIn32BitValues = 0 ) override
		{
			if ( _device != nullptr )
			{
				_device->setComputeRootConstants( rootParameterIndex, num32BitValues, data, destOffsetIn32BitValues );
			}
		}

		void drawIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset = 0 ) override
		{
			if ( _device != nullptr )
			{
				_device->drawIndirect( argumentBuffer, argumentBufferOffset );
			}
		}

		void dispatchIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset = 0 ) override
		{
			if ( _device != nullptr )
			{
				_device->dispatchIndirect( argumentBuffer, argumentBufferOffset );
			}
		}

		void beginEventMarker( const utf8* name ) override
		{
			if ( _device != nullptr )
			{
				_device->beginEventMarker( name );
			}
		}

		void endEventMarker() override
		{
			if ( _device != nullptr )
			{
				_device->endEventMarker();
			}
		}

	private:
		D3D12RHIDevice* _device;
	};

	void D3D12RHIDevice::dispatchCompute( uint32 threadGroupCountX, uint32 threadGroupCountY, uint32 threadGroupCountZ )
	{
		if ( _commandList != nullptr )
		{
			_commandList->Dispatch( threadGroupCountX, threadGroupCountY, threadGroupCountZ );
		}
	}

	void D3D12RHIDevice::setComputeRootConstants( uint32 rootParameterIndex, uint32 num32BitValues, const void* data, uint32 destOffsetIn32BitValues )
	{
		if ( _commandList != nullptr )
		{
			_commandList->SetComputeRoot32BitConstants( rootParameterIndex, num32BitValues, data, destOffsetIn32BitValues );
		}
	}

	void D3D12RHIDevice::drawIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset )
	{
		if ( _commandList != nullptr && argumentBuffer != 0 && _drawCommandSignature != nullptr )
		{
			auto* res = reinterpret_cast<ID3D12Resource*>( argumentBuffer );
			_commandList->ExecuteIndirect( _drawCommandSignature.Get(), 1, res, argumentBufferOffset, nullptr, 0 );
		}
	}

	void D3D12RHIDevice::dispatchIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset )
	{
		if ( _commandList != nullptr && argumentBuffer != 0 && _dispatchCommandSignature != nullptr )
		{
			auto* res = reinterpret_cast<ID3D12Resource*>( argumentBuffer );
			_commandList->ExecuteIndirect( _dispatchCommandSignature.Get(), 1, res, argumentBufferOffset, nullptr, 0 );
		}
	}

	void D3D12RHIDevice::beginEventMarker( const utf8* name )
	{
		(void)name;
	}

	void D3D12RHIDevice::endEventMarker()
	{
	}

	std::unique_ptr<IRHICommandList> D3D12RHIDevice::createCommandList()
	{
		return std::make_unique<D3D12CommandList>( this );
	}

	void D3D12RHIDevice::executeCommandList( IRHICommandList* cmdList )
	{
		(void)cmdList;
	}

	RHIPipelineStateHandle D3D12RHIDevice::createPipelineState( const RHIPipelineStateDesc& desc )
	{
		Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
		ShaderCompileDesc vsDesc{};
		vsDesc._filePath		= desc._vertexShaderPath;
		vsDesc._entryPoint		= "VSMain";
		vsDesc._stage			= ShaderStage::Vertex;
		vsDesc._targetFormat	= ShaderTargetFormat::DXIL_D3D12;
		ShaderCompileResult vsResult = ShaderCache::getOrCompile( vsDesc );

		ShaderCompileDesc psDesc{};
		psDesc._filePath		= desc._pixelShaderPath;
		psDesc._entryPoint		= "PSMain";
		psDesc._stage			= ShaderStage::Pixel;
		psDesc._targetFormat	= ShaderTargetFormat::DXIL_D3D12;
		ShaderCompileResult psResult = ShaderCache::getOrCompile( psDesc );

		if ( vsResult._bSuccess && psResult._bSuccess )
		{
			D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = {
				{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
				{"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
			};

			D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
			psoDesc.InputLayout										 = { inputElementDescs, _countof( inputElementDescs ) };
			psoDesc.pRootSignature									 = _rootSignature.Get();
			psoDesc.VS												 = { vsResult._bytecode.data(), vsResult._bytecode.size() };
			psoDesc.PS												 = { psResult._bytecode.data(), psResult._bytecode.size() };
			psoDesc.RasterizerState.FillMode						 = D3D12_FILL_MODE_SOLID;
			psoDesc.RasterizerState.CullMode						 = D3D12_CULL_MODE_NONE;
			psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
			psoDesc.DepthStencilState.DepthEnable					 = FALSE;
			psoDesc.SampleMask										 = UINT_MAX;
			psoDesc.PrimitiveTopologyType							 = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
			psoDesc.NumRenderTargets								 = 1;
			psoDesc.RTVFormats[0]									 = DXGI_FORMAT_R8G8B8A8_UNORM;
			psoDesc.SampleDesc.Count								 = 1;

			_device->CreateGraphicsPipelineState( &psoDesc, IID_PPV_ARGS( pso.GetAddressOf() ) );
		}

		_pipelineStates.push_back( { pso } );
		return static_cast<RHIPipelineStateHandle>( _pipelineStates.size() );
	}

	RHIPipelineStateHandle D3D12RHIDevice::createComputePipelineState( const std::string& shaderPath, const std::string& entryPoint )
	{
		Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
		if ( !shaderPath.empty() )
		{
			ShaderCompileDesc csDesc{};
			csDesc._filePath		= shaderPath;
			csDesc._entryPoint		= entryPoint;
			csDesc._stage			= ShaderStage::Compute;
			csDesc._targetFormat	= ShaderTargetFormat::DXIL_D3D12;
			ShaderCompileResult res = ShaderCache::getOrCompile( csDesc );
			if ( res._bSuccess )
			{
				D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
				psoDesc.pRootSignature = _computeRootSignature ? _computeRootSignature.Get() : _rootSignature.Get();
				psoDesc.CS = { res._bytecode.data(), res._bytecode.size() };

				const HRESULT hr = _device->CreateComputePipelineState( &psoDesc, IID_PPV_ARGS( pso.GetAddressOf() ) );
				if ( FAILED( hr ) )
				{
					SW_LOG_ERROR( "[D3D12] CreateComputePipelineState failed hr=0x%X", static_cast<uint32>( hr ) );
					flushDebugMessages( "CreateComputePipelineState" );
					return 0;
				}
			}
			else
			{
				return 0;
			}
		}
		_pipelineStates.push_back( { pso } );
		return static_cast<RHIPipelineStateHandle>( _pipelineStates.size() );
	}

	void D3D12RHIDevice::destroyPipelineState( RHIPipelineStateHandle pso )
	{
		if ( pso == 0 || pso > _pipelineStates.size() )
			return;
		_pipelineStates[pso - 1].pso.Reset();
	}

	void D3D12RHIDevice::setPipelineState( RHIPipelineStateHandle pso )
	{
		if ( pso == 0 || pso > _pipelineStates.size() || _commandList == nullptr )
			return;

		const auto& record = _pipelineStates[pso - 1];
		if ( record.pso )
			_commandList->SetPipelineState( record.pso.Get() );
	}

	void D3D12RHIDevice::setComputePipelineState( RHIPipelineStateHandle pso )
	{
		if ( pso == 0 || pso > _pipelineStates.size() || _commandList == nullptr )
			return;

		const auto& record = _pipelineStates[pso - 1];
		if ( record.pso )
		{
			if ( _computeRootSignature )
				_commandList->SetComputeRootSignature( _computeRootSignature.Get() );
			_commandList->SetPipelineState( record.pso.Get() );
		}
	}

	RHIRenderPassHandle D3D12RHIDevice::createRenderPass( const RHIRenderPassDesc& desc )
	{
		D3D12RenderPassRecord record{ desc };
		_renderPasses.push_back( record );
		return static_cast<RHIRenderPassHandle>( _renderPasses.size() );
	}

	void D3D12RHIDevice::destroyRenderPass( RHIRenderPassHandle pass )
	{
		(void)pass;
	}

	void D3D12RHIDevice::beginRenderPass( const RHIRenderPassBeginInfo& beginInfo )
	{
		if ( _commandList == nullptr || _rtvHeap == nullptr || _renderTargets.empty() )
			return;

		D3D12_RESOURCE_BARRIER barrier{};
		barrier.Type				   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource   = _renderTargets[_frameIndex].Get();
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
		barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		_commandList->ResourceBarrier( 1, &barrier );

		D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle( _rtvHeap->GetCPUDescriptorHandleForHeapStart() );
		rtvHandle.ptr += ( _frameIndex * _rtvDescriptorSize );

		_commandList->OMSetRenderTargets( 1, &rtvHandle, FALSE, nullptr );
		_commandList->ClearRenderTargetView( rtvHandle, beginInfo._clearColor, 0, nullptr );

		D3D12_VIEWPORT vp{};
		vp.Width	= static_cast<float32>( beginInfo._width > 0 ? beginInfo._width : _width );
		vp.Height	= static_cast<float32>( beginInfo._height > 0 ? beginInfo._height : _height );
		vp.MinDepth = 0.0f;
		vp.MaxDepth = 1.0f;
		_commandList->RSSetViewports( 1, &vp );

		D3D12_RECT scissorRect{ 0, 0, static_cast<LONG>( vp.Width ), static_cast<LONG>( vp.Height ) };
		_commandList->RSSetScissorRects( 1, &scissorRect );
	}

	void D3D12RHIDevice::endRenderPass()
	{
		if ( _commandList == nullptr || _renderTargets.empty() )
			return;

		D3D12_RESOURCE_BARRIER barrier{};
		barrier.Type				   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource   = _renderTargets[_frameIndex].Get();
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
		barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		_commandList->ResourceBarrier( 1, &barrier );
	}
}
#endif
