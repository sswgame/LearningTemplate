/**
 * @file D3D11RHIDevice.cpp
 * @brief Direct3D 11 RHI 디바이스 구현
 */
#include "D3D11RHIDevice.h"

#if defined( SW_PLATFORM_WINDOWS )
	#include <d3dcompiler.h>

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
	} // namespace

	D3D11RHIDevice::D3D11RHIDevice() = default;

	D3D11RHIDevice::~D3D11RHIDevice()
	{
		shutdown();
	}

	bool D3D11RHIDevice::initializeInternal( const RHISwapChainDesc& desc )
	{
		_hWnd	= static_cast<HWND>( desc._windowHandle );
		_width	= desc._width;
		_height = desc._height;

		// Use FLIP_DISCARD to match DX12 (and DXGI HWND rules): after a flip-model
		// swapchain has been created for an HWND, subsequent DISCARD/blt chains on the
		// same window can Present without updating what the user sees (frozen frame).
		DXGI_SWAP_CHAIN_DESC sd{};
		sd.BufferCount						  = ( desc._bufferCount < 2 ) ? 2 : desc._bufferCount;
		sd.BufferDesc.Width					  = _width;
		sd.BufferDesc.Height				  = _height;
		sd.BufferDesc.Format				  = DXGI_FORMAT_R8G8B8A8_UNORM;
		sd.BufferDesc.RefreshRate.Numerator	  = 60;
		sd.BufferDesc.RefreshRate.Denominator = 1;
		sd.BufferUsage						  = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		sd.OutputWindow						  = _hWnd;
		sd.SampleDesc.Count					  = 1;
		sd.SampleDesc.Quality				  = 0;
		sd.Windowed							  = TRUE;
		sd.SwapEffect						  = DXGI_SWAP_EFFECT_FLIP_DISCARD;

		UINT createDeviceFlags = 0;
	#if defined( SW_DEBUG )
		createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
	#endif

		D3D_FEATURE_LEVEL		featureLevel;
		const D3D_FEATURE_LEVEL featureLevelArray[2] = {
			D3D_FEATURE_LEVEL_11_0,
			D3D_FEATURE_LEVEL_10_0,
		};

		HRESULT hr = D3D11CreateDeviceAndSwapChain(
			nullptr,
			D3D_DRIVER_TYPE_HARDWARE,
			nullptr,
			createDeviceFlags,
			featureLevelArray,
			2,
			D3D11_SDK_VERSION,
			&sd,
			_swapChain.GetAddressOf(),
			_device.GetAddressOf(),
			&featureLevel,
			_deviceContext.GetAddressOf() );

		if ( FAILED( hr ) )
		{
			SW_LOG_ERROR( "Failed to create Direct3D 11 Device and SwapChain! HRESULT: %#", hr );
			return false;
		}

		createRenderTargetView();

		if ( createTriangleResources() == false )
			return false;

		SW_LOG_INFO( "Direct3D 11 RHI Backend Device Initialized Successfully (FLIP_DISCARD)." );
		return true;
	}

	bool D3D11RHIDevice::createTriangleResources()
	{
		ShaderCompileDesc vsDesc{};
		vsDesc._filePath			 = "Shaders/BindlessTriangle.hlsl";
		vsDesc._entryPoint			 = "VSMain";
		vsDesc._stage				 = ShaderStage::Vertex;
		vsDesc._targetFormat		 = ShaderTargetFormat::DXBC_D3D11;
		ShaderCompileResult vsResult = ShaderCache::getOrCompile( vsDesc );

		ShaderCompileDesc psDesc{};
		psDesc._filePath			 = "Shaders/BindlessTriangle.hlsl";
		psDesc._entryPoint			 = "PSMain";
		psDesc._stage				 = ShaderStage::Pixel;
		psDesc._targetFormat		 = ShaderTargetFormat::DXBC_D3D11;
		ShaderCompileResult psResult = ShaderCache::getOrCompile( psDesc );

		if ( vsResult._bSuccess == false || psResult._bSuccess == false )
		{
			SW_LOG_ERROR( "[D3D11] Failed to compile BindlessTriangle.hlsl via ShaderCache!" );
			return false;
		}

		if ( FAILED( _device->CreateVertexShader( vsResult._bytecode.data(), vsResult._bytecode.size(), nullptr, _vertexShader.GetAddressOf() ) ) )
			return false;

		if ( FAILED( _device->CreatePixelShader( psResult._bytecode.data(), psResult._bytecode.size(), nullptr, _pixelShader.GetAddressOf() ) ) )
			return false;

		D3D11_INPUT_ELEMENT_DESC inputElementDescs[] = {
			{"POSITION", 0,	 DXGI_FORMAT_R32G32B32_FLOAT, 0,	 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
			{	  "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0}
		  };

		if ( FAILED( _device->CreateInputLayout( inputElementDescs, _countof( inputElementDescs ), vsResult._bytecode.data(), vsResult._bytecode.size(), _inputLayout.GetAddressOf() ) ) )
			return false;

		RHIVertex vertices[] = {
			{  { 0.0f, 0.5f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }},
			{ { 0.5f, -0.5f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }},
			{{ -0.5f, -0.5f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }}
		   };

		D3D11_BUFFER_DESC bd{};
		bd.Usage		  = D3D11_USAGE_DEFAULT;
		bd.ByteWidth	  = sizeof( vertices );
		bd.BindFlags	  = D3D11_BIND_VERTEX_BUFFER;
		bd.CPUAccessFlags = 0;

		D3D11_SUBRESOURCE_DATA initData{};
		initData.pSysMem = vertices;

		if ( FAILED( _device->CreateBuffer( &bd, &initData, _vertexBuffer.GetAddressOf() ) ) )
			return false;

		return true;
	}

	RHIBufferHandle D3D11RHIDevice::createConstantBuffer( uint32 size )
	{
		UINT			  alignedSize = ( size + 15u ) & ~15u;
		D3D11_BUFFER_DESC bd{};
		bd.Usage		  = D3D11_USAGE_DYNAMIC;
		bd.ByteWidth	  = alignedSize;
		bd.BindFlags	  = D3D11_BIND_CONSTANT_BUFFER;
		bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

		Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
		if ( FAILED( _device->CreateBuffer( &bd, nullptr, buffer.GetAddressOf() ) ) )
			return 0;

		_constantBuffers.push_back( buffer );
		return reinterpret_cast<RHIBufferHandle>( buffer.Get() );
	}

	void D3D11RHIDevice::updateConstantBuffer( RHIBufferHandle buffer, const void* data, uint32 size )
	{
		if ( buffer == 0 || data == nullptr || _deviceContext == nullptr )
			return;
		auto*					 res = reinterpret_cast<ID3D11Buffer*>( buffer );
		D3D11_MAPPED_SUBRESOURCE mapped{};
		if ( SUCCEEDED( _deviceContext->Map( res, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped ) ) )
		{
			memcpy( mapped.pData, data, size );
			_deviceContext->Unmap( res, 0 );
		}
	}

	RHIBufferHandle D3D11RHIDevice::createStructuredBuffer( uint32 elementSize, uint32 elementCount )
	{
		D3D11_BUFFER_DESC bd{};
		bd.Usage			   = D3D11_USAGE_DEFAULT;
		bd.ByteWidth		   = elementSize * elementCount;
		bd.BindFlags		   = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
		bd.MiscFlags		   = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		bd.StructureByteStride = elementSize;

		Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
		if ( FAILED( _device->CreateBuffer( &bd, nullptr, buffer.GetAddressOf() ) ) )
			return 0;

		_structuredBuffers.push_back( buffer );
		return reinterpret_cast<RHIBufferHandle>( buffer.Get() );
	}

	void D3D11RHIDevice::updateStructuredBuffer( RHIBufferHandle buffer, const void* data, uint32 size )
	{
		if ( buffer == 0 || data == nullptr || _deviceContext == nullptr )
			return;
		auto* res = reinterpret_cast<ID3D11Buffer*>( buffer );
		_deviceContext->UpdateSubresource( res, 0, nullptr, data, size, 0 );
	}

	void D3D11RHIDevice::destroyBuffer( RHIBufferHandle buffer )
	{
		if ( buffer == 0 )
			return;
		auto* res = reinterpret_cast<ID3D11Buffer*>( buffer );
		for ( auto it = _constantBuffers.begin(); it != _constantBuffers.end(); ++it )
		{
			if ( it->Get() == res )
			{
				_constantBuffers.erase( it );
				return;
			}
		}
		for ( auto it = _structuredBuffers.begin(); it != _structuredBuffers.end(); ++it )
		{
			if ( it->Get() == res )
			{
				_structuredBuffers.erase( it );
				return;
			}
		}
	}

	RHITextureHandle D3D11RHIDevice::createTexture2D( const RHITextureDesc& desc )
	{
		if ( _device == nullptr || desc._width == 0 || desc._height == 0 )
			return 0;

		D3D11_TEXTURE2D_DESC texDesc{};
		texDesc.Width			   = desc._width;
		texDesc.Height			   = desc._height;
		texDesc.MipLevels		   = desc._mipLevels;
		texDesc.ArraySize		   = 1;
		texDesc.Format			   = toDxgiFormat( desc._format );
		texDesc.SampleDesc.Count   = 1;
		texDesc.SampleDesc.Quality = 0;
		texDesc.Usage			   = D3D11_USAGE_DEFAULT;
		texDesc.BindFlags		   = 0;
		texDesc.CPUAccessFlags	   = 0;
		texDesc.MiscFlags		   = 0;

		if ( desc._bIsRenderTarget )
			texDesc.BindFlags |= D3D11_BIND_RENDER_TARGET;
		if ( desc._bIsShaderResource )
			texDesc.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
		if ( desc._bIsDepthStencil )
			texDesc.BindFlags |= D3D11_BIND_DEPTH_STENCIL;
		if ( desc._bIsUnorderedAccess )
			texDesc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;

		if ( texDesc.BindFlags == 0 )
			texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

		TextureRecord record{};
		record._width  = desc._width;
		record._height = desc._height;

		if ( FAILED( _device->CreateTexture2D( &texDesc, nullptr, record._texture.GetAddressOf() ) ) )
		{
			SW_LOG_ERROR( "[D3D11] Failed to create Texture2D (%ux%u).", desc._width, desc._height );
			return 0;
		}

		if ( desc._bIsRenderTarget )
		{
			if ( FAILED( _device->CreateRenderTargetView( record._texture.Get(), nullptr, record._rtv.GetAddressOf() ) ) )
			{
				SW_LOG_ERROR( "[D3D11] Failed to create RTV for Texture2D." );
				return 0;
			}
		}

		if ( desc._bIsShaderResource )
		{
			if ( FAILED( _device->CreateShaderResourceView( record._texture.Get(), nullptr, record._srv.GetAddressOf() ) ) )
			{
				SW_LOG_ERROR( "[D3D11] Failed to create SRV for Texture2D." );
				return 0;
			}
		}

		const RHITextureHandle handle = reinterpret_cast<RHITextureHandle>( record._texture.Get() );
		_textures.emplace( handle, std::move( record ) );
		return handle;
	}

	void D3D11RHIDevice::destroyTexture( RHITextureHandle texture )
	{
		if ( texture == 0 )
			return;
		_textures.erase( texture );
	}

	void D3D11RHIDevice::beginOffscreenPass( RHITextureHandle colorTarget, float32 clearColor[4] )
	{
		if ( colorTarget == 0 )
		{
			beginFrame( clearColor );
			return;
		}

		if ( _deviceContext == nullptr )
			return;

		auto it = _textures.find( colorTarget );
		if ( it == _textures.end() || it->second._rtv == nullptr )
			return;

		TextureRecord& record = it->second;
		_deviceContext->ClearRenderTargetView( record._rtv.Get(), clearColor );
		_deviceContext->OMSetRenderTargets( 1, record._rtv.GetAddressOf(), nullptr );

		D3D11_VIEWPORT vp{};
		vp.Width	= static_cast<float32>( record._width );
		vp.Height	= static_cast<float32>( record._height );
		vp.MinDepth = 0.0f;
		vp.MaxDepth = 1.0f;
		vp.TopLeftX = 0.0f;
		vp.TopLeftY = 0.0f;
		_deviceContext->RSSetViewports( 1, &vp );
	}

	void D3D11RHIDevice::endOffscreenPass( RHITextureHandle colorTarget )
	{
		if ( colorTarget == 0 || _deviceContext == nullptr )
			return;

		ID3D11RenderTargetView* nullRtv = nullptr;
		_deviceContext->OMSetRenderTargets( 1, &nullRtv, nullptr );

		// Unbind possible SRV uses of the offscreen color target before ImGui samples it.
		ID3D11ShaderResourceView* nullSrvs[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
		_deviceContext->PSSetShaderResources( 0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, nullSrvs );
	}

	RHIDescriptorIndex D3D11RHIDevice::registerBindlessResource( RHIBufferHandle buffer )
	{
		if ( buffer == 0 )
			return kInvalidDescriptorIndex;

		auto*			   res = reinterpret_cast<ID3D11Buffer*>( buffer );
		RHIDescriptorIndex index;
		if ( _bindlessFreeList.empty() == false )
		{
			index = _bindlessFreeList.back();
			_bindlessFreeList.pop_back();
			_registeredBindlessVector[index] = res;
		}
		else
		{
			index = static_cast<RHIDescriptorIndex>( _registeredBindlessVector.size() );
			_registeredBindlessVector.push_back( res );
		}
		return index;
	}

	void D3D11RHIDevice::unregisterBindlessResource( RHIDescriptorIndex index )
	{
		if ( index < _registeredBindlessVector.size() )
		{
			_registeredBindlessVector[index] = nullptr;
			_bindlessFreeList.push_back( index );
		}
	}

	RHIDescriptorIndex D3D11RHIDevice::registerBindlessUAV( RHIBufferHandle buffer )
	{
		if ( buffer == 0 )
			return kInvalidDescriptorIndex;
		auto* res = reinterpret_cast<ID3D11Buffer*>( buffer );

		D3D11_BUFFER_DESC desc;
		res->GetDesc( &desc );

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
		uavDesc.ViewDimension		= D3D11_UAV_DIMENSION_BUFFER;
		uavDesc.Format				= DXGI_FORMAT_UNKNOWN;
		uavDesc.Buffer.FirstElement = 0;
		uavDesc.Buffer.NumElements	= desc.ByteWidth / desc.StructureByteStride;

		Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> uav;
		if ( FAILED( _device->CreateUnorderedAccessView( res, &uavDesc, uav.GetAddressOf() ) ) )
			return kInvalidDescriptorIndex;

		RHIDescriptorIndex index;
		if ( _uavFreeList.empty() == false )
		{
			index = _uavFreeList.back();
			_uavFreeList.pop_back();
			_registeredUAVs[index] = uav;
		}
		else
		{
			index = static_cast<RHIDescriptorIndex>( _registeredUAVs.size() );
			_registeredUAVs.push_back( uav );
		}
		return index;
	}

	void D3D11RHIDevice::unregisterBindlessUAV( RHIDescriptorIndex index )
	{
		if ( index < _registeredUAVs.size() )
		{
			_registeredUAVs[index] = nullptr;
			_uavFreeList.push_back( index );
		}
	}

	void D3D11RHIDevice::bindComputeUAV( RHIDescriptorIndex index, uint32 slot )
	{
		if ( _deviceContext != nullptr && index < _registeredUAVs.size() && _registeredUAVs[index] != nullptr )
		{
			ID3D11UnorderedAccessView* uav = _registeredUAVs[index].Get();
			_deviceContext->CSSetUnorderedAccessViews( slot, 1, &uav, nullptr );
		}
	}

	void D3D11RHIDevice::drawTriangle( RHIDescriptorIndex materialDescriptorIndex )
	{
		if ( _deviceContext == nullptr || _vertexShader == nullptr || _pixelShader == nullptr )
			return;

		if ( materialDescriptorIndex < static_cast<RHIDescriptorIndex>( _registeredBindlessVector.size() ) )
		{
			ID3D11Buffer* cb = _registeredBindlessVector[materialDescriptorIndex];
			if ( cb != nullptr )
			{
				_deviceContext->PSSetConstantBuffers( 0, 1, &cb );
			}
		}

		UINT		  stride = sizeof( RHIVertex );
		UINT		  offset = 0;
		ID3D11Buffer* vb	 = _vertexBuffer.Get();

		_deviceContext->IASetVertexBuffers( 0, 1, &vb, &stride, &offset );
		_deviceContext->IASetInputLayout( _inputLayout.Get() );
		_deviceContext->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );

		_deviceContext->VSSetShader( _vertexShader.Get(), nullptr, 0 );
		_deviceContext->PSSetShader( _pixelShader.Get(), nullptr, 0 );

		_deviceContext->Draw( 3, 0 );
	}

	void D3D11RHIDevice::shutdownInternal()
	{
		cleanupRenderTargetView();
		_textures.clear();
		_vertexBuffer.Reset();
		_inputLayout.Reset();
		_vertexShader.Reset();
		_pixelShader.Reset();
		_registeredBindlessVector.clear();
		_constantBuffers.clear();
		_swapChain.Reset();
		_deviceContext.Reset();
		_device.Reset();
	}

	void D3D11RHIDevice::createRenderTargetView()
	{
		if ( _swapChain == nullptr )
			return;

		Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
		HRESULT									hr = _swapChain->GetBuffer( 0, IID_PPV_ARGS( backBuffer.GetAddressOf() ) );
		if ( SUCCEEDED( hr ) )
			_device->CreateRenderTargetView( backBuffer.Get(), nullptr, _renderTargetView.GetAddressOf() );
	}

	void D3D11RHIDevice::cleanupRenderTargetView()
	{
		_renderTargetView.Reset();
	}

	void D3D11RHIDevice::resize( uint32 width, uint32 height )
	{
		if ( _swapChain == nullptr || ( width == 0 && height == 0 ) )
			return;

		_width	= width;
		_height = height;

		cleanupRenderTargetView();
		_swapChain->ResizeBuffers( 0, width, height, DXGI_FORMAT_UNKNOWN, 0 );
		createRenderTargetView();
	}

	void D3D11RHIDevice::beginFrame( float32 clearColor[4] )
	{
		if ( _deviceContext == nullptr || _swapChain == nullptr )
			return;

		// FLIP_DISCARD rotates the back buffer; reacquire RTV each frame so we clear/draw the one Present will show.
		cleanupRenderTargetView();
		createRenderTargetView();
		if ( _renderTargetView == nullptr )
			return;

		_deviceContext->ClearRenderTargetView( _renderTargetView.Get(), clearColor );
		_deviceContext->OMSetRenderTargets( 1, _renderTargetView.GetAddressOf(), nullptr );

		constexpr float32 kDefaultViewportX		   = 0.0f;
		constexpr float32 kDefaultViewportY		   = 0.0f;
		constexpr float32 kDefaultViewportMinDepth = 0.0f;
		constexpr float32 kDefaultViewportMaxDepth = 1.0f;

		D3D11_VIEWPORT vp;
		vp.Width	= static_cast<float32>( _width );
		vp.Height	= static_cast<float32>( _height );
		vp.MinDepth = kDefaultViewportMinDepth;
		vp.MaxDepth = kDefaultViewportMaxDepth;
		vp.TopLeftX = kDefaultViewportX;
		vp.TopLeftY = kDefaultViewportY;
		_deviceContext->RSSetViewports( 1, &vp );
	}

	void D3D11RHIDevice::endFrame( bool vsync )
	{
		if ( _swapChain == nullptr )
			return;

		// Release RTV before Present so DXGI can flip the buffer freely.
		cleanupRenderTargetView();

		const HRESULT hr = _swapChain->Present( vsync ? 1 : 0, 0 );
		if ( FAILED( hr ) )
			SW_LOG_ERROR( "[D3D11] Present failed hr=0x%X", static_cast<uint32>( hr ) );
	}

	class D3D11CommandList final : public IRHICommandList
	{
	public:
		D3D11CommandList( D3D11RHIDevice* device )
			: _device{ device }
		{
		}

		void beginCommandList() override {}
		void endCommandList() override {}

		void setViewport( const RHIViewport& vp ) override
		{
			if ( _device != nullptr && _device->getNativeContext() != nullptr )
			{
				ID3D11DeviceContext* ctx = static_cast<ID3D11DeviceContext*>( _device->getNativeContext() );
				D3D11_VIEWPORT		 d3dvp{};
				d3dvp.TopLeftX = vp._x;
				d3dvp.TopLeftY = vp._y;
				d3dvp.Width	   = vp._width;
				d3dvp.Height   = vp._height;
				d3dvp.MinDepth = vp._minDepth;
				d3dvp.MaxDepth = vp._maxDepth;
				ctx->RSSetViewports( 1, &d3dvp );
			}
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
		D3D11RHIDevice* _device;
	};

	void D3D11RHIDevice::dispatchCompute( uint32 threadGroupCountX, uint32 threadGroupCountY, uint32 threadGroupCountZ )
	{
		if ( _deviceContext != nullptr )
		{
			_deviceContext->Dispatch( threadGroupCountX, threadGroupCountY, threadGroupCountZ );
		}
	}

	void D3D11RHIDevice::setComputeRootConstants( uint32 rootParameterIndex, uint32 num32BitValues, const void* data, uint32 destOffsetIn32BitValues )
	{

		(void)rootParameterIndex;
		(void)num32BitValues;
		(void)data;
		(void)destOffsetIn32BitValues;
	}

	void D3D11RHIDevice::drawIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset )
	{
		if ( _deviceContext != nullptr && argumentBuffer != 0 )
		{
			ID3D11Buffer* buf = reinterpret_cast<ID3D11Buffer*>( argumentBuffer );
			_deviceContext->DrawInstancedIndirect( buf, argumentBufferOffset );
		}
	}

	void D3D11RHIDevice::dispatchIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset )
	{
		if ( _deviceContext != nullptr && argumentBuffer != 0 )
		{
			ID3D11Buffer* buf = reinterpret_cast<ID3D11Buffer*>( argumentBuffer );
			_deviceContext->DispatchIndirect( buf, argumentBufferOffset );
		}
	}

	void D3D11RHIDevice::beginEventMarker( const utf8* name )
	{
		(void)name;
	}

	void D3D11RHIDevice::endEventMarker()
	{
	}

	std::unique_ptr<IRHICommandList> D3D11RHIDevice::createCommandList()
	{
		return std::make_unique<D3D11CommandList>( this );
	}

	void D3D11RHIDevice::executeCommandList( IRHICommandList* cmdList )
	{
		(void)cmdList;
	}

	RHIPipelineStateHandle D3D11RHIDevice::createPipelineState( const RHIPipelineStateDesc& desc )
	{
		D3D11PipelineStateRecord pso{};
		if ( !desc._vertexShaderPath.empty() )
		{
			ShaderCompileDesc vsDesc{};
			vsDesc._filePath		= desc._vertexShaderPath;
			vsDesc._entryPoint		= desc._vertexEntryPoint;
			vsDesc._stage			= ShaderStage::Vertex;
			vsDesc._targetFormat	= ShaderTargetFormat::DXBC_D3D11;
			ShaderCompileResult res = ShaderCache::getOrCompile( vsDesc );
			if ( res._bSuccess )
			{
				_device->CreateVertexShader( res._bytecode.data(), res._bytecode.size(), nullptr, pso.vs.GetAddressOf() );
				D3D11_INPUT_ELEMENT_DESC inputElementDescs[] = {
					{"POSITION", 0,	 DXGI_FORMAT_R32G32B32_FLOAT, 0,	 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
					{	  "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0}
				  };
				_device->CreateInputLayout( inputElementDescs, _countof( inputElementDescs ), res._bytecode.data(), res._bytecode.size(), pso.inputLayout.GetAddressOf() );
			}
		}
		if ( !desc._pixelShaderPath.empty() )
		{
			ShaderCompileDesc psDesc{};
			psDesc._filePath		= desc._pixelShaderPath;
			psDesc._entryPoint		= desc._pixelEntryPoint;
			psDesc._stage			= ShaderStage::Pixel;
			psDesc._targetFormat	= ShaderTargetFormat::DXBC_D3D11;
			ShaderCompileResult res = ShaderCache::getOrCompile( psDesc );
			if ( res._bSuccess )
			{
				_device->CreatePixelShader( res._bytecode.data(), res._bytecode.size(), nullptr, pso.ps.GetAddressOf() );
			}
		}
		if ( !desc._computeShaderPath.empty() )
		{
			ShaderCompileDesc csDesc{};
			csDesc._filePath		= desc._computeShaderPath;
			csDesc._entryPoint		= desc._computeEntryPoint;
			csDesc._stage			= ShaderStage::Compute;
			csDesc._targetFormat	= ShaderTargetFormat::DXBC_D3D11;
			ShaderCompileResult res = ShaderCache::getOrCompile( csDesc );
			if ( res._bSuccess )
			{
				_device->CreateComputeShader( res._bytecode.data(), res._bytecode.size(), nullptr, pso.cs.GetAddressOf() );
			}
		}

		D3D11_RASTERIZER_DESC rd{};
		rd.FillMode		   = ( desc._fillMode == RHIFillMode::Wireframe ) ? D3D11_FILL_WIREFRAME : D3D11_FILL_SOLID;
		rd.CullMode		   = ( desc._cullMode == RHICullMode::Front ) ? D3D11_CULL_FRONT : ( ( desc._cullMode == RHICullMode::Back ) ? D3D11_CULL_BACK : D3D11_CULL_NONE );
		rd.DepthClipEnable = TRUE;
		if ( _device != nullptr )
			_device->CreateRasterizerState( &rd, pso.rasterizerState.GetAddressOf() );

		_pipelineStates.push_back( pso );
		return static_cast<RHIPipelineStateHandle>( _pipelineStates.size() );
	}

	RHIPipelineStateHandle D3D11RHIDevice::createComputePipelineState( const std::string& shaderPath, const std::string& entryPoint )
	{
		D3D11PipelineStateRecord pso{};
		if ( !shaderPath.empty() )
		{
			ShaderCompileDesc csDesc{};
			csDesc._filePath		= shaderPath;
			csDesc._entryPoint		= entryPoint;
			csDesc._stage			= ShaderStage::Compute;
			csDesc._targetFormat	= ShaderTargetFormat::DXBC_D3D11;
			ShaderCompileResult res = ShaderCache::getOrCompile( csDesc );
			if ( res._bSuccess == false || FAILED( _device->CreateComputeShader( res._bytecode.data(), res._bytecode.size(), nullptr, pso.cs.GetAddressOf() ) ) )
			{
				return 0;
			}
		}
		_pipelineStates.push_back( pso );
		return static_cast<RHIPipelineStateHandle>( _pipelineStates.size() );
	}

	void D3D11RHIDevice::destroyPipelineState( RHIPipelineStateHandle pso )
	{
		if ( pso == 0 || pso > _pipelineStates.size() )
			return;
		_pipelineStates[pso - 1] = D3D11PipelineStateRecord{};
	}

	void D3D11RHIDevice::setPipelineState( RHIPipelineStateHandle pso )
	{
		if ( pso == 0 || pso > _pipelineStates.size() || _deviceContext == nullptr )
			return;

		const auto& record = _pipelineStates[pso - 1];
		if ( record.vs )
			_deviceContext->VSSetShader( record.vs.Get(), nullptr, 0 );
		if ( record.ps )
			_deviceContext->PSSetShader( record.ps.Get(), nullptr, 0 );
		if ( record.cs )
			_deviceContext->CSSetShader( record.cs.Get(), nullptr, 0 );
		if ( record.inputLayout )
			_deviceContext->IASetInputLayout( record.inputLayout.Get() );
		if ( record.rasterizerState )
			_deviceContext->RSSetState( record.rasterizerState.Get() );
	}

	void D3D11RHIDevice::setComputePipelineState( RHIPipelineStateHandle pso )
	{
		setPipelineState( pso );
	}

	RHIRenderPassHandle D3D11RHIDevice::createRenderPass( const RHIRenderPassDesc& desc )
	{
		D3D11RenderPassRecord record{ desc };
		_renderPasses.push_back( record );
		return static_cast<RHIRenderPassHandle>( _renderPasses.size() );
	}

	void D3D11RHIDevice::destroyRenderPass( RHIRenderPassHandle pass )
	{
		(void)pass;
	}

	void D3D11RHIDevice::beginRenderPass( const RHIRenderPassBeginInfo& beginInfo )
	{
		if ( _deviceContext == nullptr || _renderTargetView == nullptr )
			return;

		_deviceContext->ClearRenderTargetView( _renderTargetView.Get(), beginInfo._clearColor );
		_deviceContext->OMSetRenderTargets( 1, _renderTargetView.GetAddressOf(), nullptr );

		D3D11_VIEWPORT vp{};
		vp.Width	= static_cast<float32>( beginInfo._width > 0 ? beginInfo._width : _width );
		vp.Height	= static_cast<float32>( beginInfo._height > 0 ? beginInfo._height : _height );
		vp.MinDepth = 0.0f;
		vp.MaxDepth = 1.0f;
		_deviceContext->RSSetViewports( 1, &vp );
	}

	void D3D11RHIDevice::endRenderPass()
	{
	}
} // namespace sw
#endif
