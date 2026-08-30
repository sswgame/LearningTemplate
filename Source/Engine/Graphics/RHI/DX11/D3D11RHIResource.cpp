#include "pch.h"

#include "Engine/Graphics/RHI/DX11/D3D11RHIResource.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/RHI/DX11/D3D11RHIDevice.h"
#include "Engine/Graphics/Shader/ShaderCache.h"

#if defined( SW_PLATFORM_WINDOWS )
namespace sw
{
	namespace
	{
		ShaderCompileResult compileShader( const ShaderCompileDesc& desc )
		{
			if ( engine::areEngineServicesBound() )
				return engine::getShaderCache().getOrCompile( desc );
			return ShaderCompiler::compileHLSL( desc );
		}

		struct D3D11RHIResourceInternal
		{
			static DXGI_FORMAT toDxgiFormatD3D11( RHIFormat format )
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
		};
	} // namespace
} // namespace sw

namespace sw
{
	SW_LOG_CALLER( "D3D11" );

	RHIPipelineStateHandle D3D11RHIResource::createPipelineState( const RHIPipelineStateDesc& desc )
	{
		auto fillDefines = [&]( ShaderCompileDesc& cd )
		{
			for ( const string& def : desc._listShaderDefine )
			{
				ShaderMacroDefine m{};
				const size_t	  eq = def.find( '=' );
				if ( eq == string::npos )
				{
					m._name	 = def;
					m._value = "1";
				}
				else
				{
					m._name	 = def.substr( 0, eq );
					m._value = def.substr( eq + 1 );
				}
				cd._listDefine.push_back( std::move( m ) );
			}
		};

		D3D11RHIDevice::D3D11PipelineStateRecord pso{};
		if ( desc._vertexShaderPath.empty() == false )
		{
			ShaderCompileDesc vsDesc{};
			vsDesc._filePath	 = desc._vertexShaderPath;
			vsDesc._entryPoint	 = desc._vertexEntryPoint.empty() ? "VSMain" : desc._vertexEntryPoint;
			vsDesc._stage		 = ShaderStage::Vertex;
			vsDesc._targetFormat = ShaderTargetFormat::DXBC_D3D11;
			fillDefines( vsDesc );
			ShaderCompileResult res = compileShader( vsDesc );
			if ( res._bSuccess )
			{
				_pDevice->_device->CreateVertexShader( res._bytecode.data(), res._bytecode.size(), nullptr, pso._vs.GetAddressOf() );
				D3D11_INPUT_ELEMENT_DESC inputElementDescs[] = {
					{"POSITION", 0,	 DXGI_FORMAT_R32G32B32_FLOAT, 0,	 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
					{	  "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0}
				  };
				_pDevice->_device->CreateInputLayout( inputElementDescs, _countof( inputElementDescs ), res._bytecode.data(), res._bytecode.size(), pso._inputLayout.GetAddressOf() );
			}
		}
		if ( desc._pixelShaderPath.empty() == false )
		{
			ShaderCompileDesc psDesc{};
			psDesc._filePath	 = desc._pixelShaderPath;
			psDesc._entryPoint	 = desc._pixelEntryPoint.empty() ? "PSMain" : desc._pixelEntryPoint;
			psDesc._stage		 = ShaderStage::Pixel;
			psDesc._targetFormat = ShaderTargetFormat::DXBC_D3D11;
			fillDefines( psDesc );
			ShaderCompileResult res = compileShader( psDesc );
			if ( res._bSuccess )
				_pDevice->_device->CreatePixelShader( res._bytecode.data(), res._bytecode.size(), nullptr, pso._ps.GetAddressOf() );
		}
		if ( desc._computeShaderPath.empty() == false )
		{
			ShaderCompileDesc csDesc{};
			csDesc._filePath	 = desc._computeShaderPath;
			csDesc._entryPoint	 = desc._computeEntryPoint.empty() ? "CSMain" : desc._computeEntryPoint;
			csDesc._stage		 = ShaderStage::Compute;
			csDesc._targetFormat = ShaderTargetFormat::DXBC_D3D11;
			fillDefines( csDesc );
			ShaderCompileResult res = compileShader( csDesc );
			if ( res._bSuccess )
				_pDevice->_device->CreateComputeShader( res._bytecode.data(), res._bytecode.size(), nullptr, pso._cs.GetAddressOf() );
		}

		D3D11_RASTERIZER_DESC rd{};
		rd.FillMode		   = ( desc._fillMode == RHIFillMode::Wireframe ) ? D3D11_FILL_WIREFRAME : D3D11_FILL_SOLID;
		rd.CullMode		   = ( desc._cullMode == RHICullMode::Front ) ? D3D11_CULL_FRONT : ( ( desc._cullMode == RHICullMode::Back ) ? D3D11_CULL_BACK : D3D11_CULL_NONE );
		rd.DepthClipEnable = TRUE;
		if ( _pDevice != nullptr )
			_pDevice->_device->CreateRasterizerState( &rd, pso._rasterizerState.GetAddressOf() );

		if ( _pDevice != nullptr )
		{
			D3D11_BLEND_DESC blendDesc{};
			blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
			if ( desc._bEnableBlend != 0 )
			{
				blendDesc.RenderTarget[0].BlendEnable	 = TRUE;
				blendDesc.RenderTarget[0].SrcBlend		 = D3D11_BLEND_SRC_ALPHA;
				blendDesc.RenderTarget[0].DestBlend		 = D3D11_BLEND_INV_SRC_ALPHA;
				blendDesc.RenderTarget[0].BlendOp		 = D3D11_BLEND_OP_ADD;
				blendDesc.RenderTarget[0].SrcBlendAlpha	 = D3D11_BLEND_ONE;
				blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
				blendDesc.RenderTarget[0].BlendOpAlpha	 = D3D11_BLEND_OP_ADD;
			}
			_pDevice->_device->CreateBlendState( &blendDesc, pso._blendState.GetAddressOf() );

			D3D11_DEPTH_STENCIL_DESC dsDesc{};
			dsDesc.DepthEnable	  = ( desc._bEnableDepthTest != 0 ) ? TRUE : FALSE;
			dsDesc.DepthWriteMask = ( desc._bEnableDepthWrite != 0 ) ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
			dsDesc.DepthFunc	  = D3D11_COMPARISON_LESS_EQUAL;
			_pDevice->_device->CreateDepthStencilState( &dsDesc, pso._depthStencilState.GetAddressOf() );
		}

		return _pDevice->_pipelineStates.insert( std::move( pso ) );
	}

	RHIPipelineStateHandle D3D11RHIResource::createComputePipelineState( string_view shaderPath, string_view entryPoint )
	{
		D3D11RHIDevice::D3D11PipelineStateRecord pso{};
		if ( shaderPath.empty() == false )
		{
			ShaderCompileDesc csDesc{};
			csDesc._filePath		= shaderPath;
			csDesc._entryPoint		= entryPoint;
			csDesc._stage			= ShaderStage::Compute;
			csDesc._targetFormat	= ShaderTargetFormat::DXBC_D3D11;
			ShaderCompileResult res = compileShader( csDesc );
			if ( res._bSuccess == false || FAILED( _pDevice->_device->CreateComputeShader( res._bytecode.data(), res._bytecode.size(), nullptr, pso._cs.GetAddressOf() ) ) )
				return 0;
		}
		return _pDevice->_pipelineStates.insert( std::move( pso ) );
	}

	void D3D11RHIResource::destroyPipelineState( RHIPipelineStateHandle pso )
	{
		if ( pso == 0 )
			return;
		_pDevice->_pipelineStates.erase( pso );
		if ( _pDevice->_activeGraphicsPso == pso )
			_pDevice->_activeGraphicsPso = 0;
	}

	RHIRenderPassHandle D3D11RHIResource::createRenderPass( const RHIRenderPassDesc& desc )
	{
		D3D11RHIDevice::D3D11RenderPassRecord record{};
		record._desc   = desc;
		record._bAlive = 1;
		_pDevice->_listRenderPass.push_back( record );
		return _pDevice->_listRenderPass.size();
	}

	void D3D11RHIResource::destroyRenderPass( RHIRenderPassHandle pass )
	{
		if ( pass == 0 || pass > _pDevice->_listRenderPass.size() )
			return;
		_pDevice->_listRenderPass[pass - 1]._bAlive = 0;
	}

	RHIBufferHandle D3D11RHIResource::createConstantBuffer( uint32 size )
	{
		if ( _pDevice == nullptr || _pDevice->_device == nullptr || size == 0 )
		{
			SW_LOG_ERROR( "createConstantBuffer: invalid device or size=%#", size );
			return 0;
		}

		UINT alignedSize = ( size + 15u ) & ~15u;
		if ( alignedSize == 0 )
			alignedSize = 16u;

		D3D11_BUFFER_DESC bd{};
		bd.Usage		  = D3D11_USAGE_DYNAMIC;
		bd.ByteWidth	  = alignedSize;
		bd.BindFlags	  = D3D11_BIND_CONSTANT_BUFFER;
		bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

		Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
		const HRESULT						 hr = _pDevice->_device->CreateBuffer( &bd, nullptr, buffer.GetAddressOf() );
		if ( FAILED( hr ) )
		{
			SW_LOG_ERROR( "CreateBuffer(constant) failed hr=0x%# size=%# aligned=%#",
						  static_cast<uint32>( hr ), size, alignedSize );
			return 0;
		}

		const RHIBufferHandle handle = _pDevice->storeBuffer( std::move( buffer ) );
		if ( handle == 0 )
			SW_LOG_ERROR( "storeBuffer returned 0 after CreateBuffer success" );
		return handle;
	}

	void D3D11RHIResource::updateConstantBuffer( RHIBufferHandle buffer, const void* pData, uint32 size )
	{
		if ( buffer == 0 || pData == nullptr || _pDevice->_deviceContext == nullptr )
			return;
		ID3D11Buffer* pRes = _pDevice->resolveBuffer( buffer );
		if ( pRes == nullptr )
			return;
		D3D11_MAPPED_SUBRESOURCE mapped{};
		if ( SUCCEEDED( _pDevice->_deviceContext->Map( pRes, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped ) ) )
		{
			Memory::copy( mapped.pData, pData, size );
			_pDevice->_deviceContext->Unmap( pRes, 0 );
		}
	}

	RHIBufferHandle D3D11RHIResource::createStructuredBuffer( uint32 elementSize, uint32 elementCount )
	{
		D3D11_BUFFER_DESC bd{};
		bd.Usage			   = D3D11_USAGE_DEFAULT;
		bd.ByteWidth		   = elementSize * elementCount;
		bd.BindFlags		   = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
		bd.MiscFlags		   = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		bd.StructureByteStride = elementSize;

		Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
		if ( FAILED( _pDevice->_device->CreateBuffer( &bd, nullptr, buffer.GetAddressOf() ) ) )
			return 0;

		return _pDevice->storeBuffer( std::move( buffer ) );
	}

	void D3D11RHIResource::updateStructuredBuffer( RHIBufferHandle buffer, const void* pData, uint32 size )
	{
		if ( buffer == 0 || pData == nullptr || _pDevice->_deviceContext == nullptr )
			return;
		ID3D11Buffer* pRes = _pDevice->resolveBuffer( buffer );
		if ( pRes == nullptr )
			return;
		_pDevice->_deviceContext->UpdateSubresource( pRes, 0, nullptr, pData, size, 0 );
	}

	RHIBufferHandle D3D11RHIResource::createVertexBuffer( const void* pData, uint32 sizeBytes )
	{
		if ( _pDevice == nullptr || pData == nullptr || sizeBytes == 0 )
			return 0;

		D3D11_BUFFER_DESC bd{};
		bd.Usage		  = D3D11_USAGE_DEFAULT;
		bd.ByteWidth	  = sizeBytes;
		bd.BindFlags	  = D3D11_BIND_VERTEX_BUFFER;
		bd.CPUAccessFlags = 0;

		D3D11_SUBRESOURCE_DATA init{};
		init.pSysMem = pData;

		Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
		if ( FAILED( _pDevice->_device->CreateBuffer( &bd, &init, buffer.GetAddressOf() ) ) )
			return 0;

		return _pDevice->storeBuffer( std::move( buffer ) );
	}

	void D3D11RHIResource::destroyBuffer( RHIBufferHandle buffer )
	{
		if ( buffer == 0 )
			return;
		if ( buffer == _pDevice->_boundMeshVb )
			_pDevice->_boundMeshVb = 0;

		Microsoft::WRL::ComPtr<ID3D11Buffer> owned;
		if ( _pDevice->_gpuBuffers.take( buffer, owned ) == false )
			return;

		for ( size_t bindlessIndex = 0; bindlessIndex < _pDevice->_listRegisteredBindlessVector.size(); ++bindlessIndex )
		{
			if ( _pDevice->_listRegisteredBindlessVector[bindlessIndex] != buffer )
				continue;
			_pDevice->_listRegisteredBindlessVector[bindlessIndex] = 0;
			_pDevice->_listBindlessFree.push_back( static_cast<uint32>( bindlessIndex ) );
		}
		for ( size_t bufferIndex = 0; bufferIndex < _pDevice->_listUavSourceBuffer.size(); ++bufferIndex )
		{
			if ( _pDevice->_listUavSourceBuffer[bufferIndex] != buffer )
				continue;
			_pDevice->_listRegisteredUAV[bufferIndex].Reset();
			_pDevice->_listUavSourceBuffer[bufferIndex] = 0;
			_pDevice->_listUavFree.push_back( static_cast<uint32>( bufferIndex ) );
		}

		auto releaseCb = [owned]()
		{ (void)owned.Get(); };
		_pDevice->_releaseQueue.enqueueRelease( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, releaseCb ) );
	}

	RHITextureHandle D3D11RHIResource::createTexture2D( const RHITextureDesc& desc )
	{
		if ( _pDevice == nullptr || desc._width == 0 || desc._height == 0 )
			return 0;

		const bool bDepth = desc._bIsDepthStencil != 0;

		D3D11_TEXTURE2D_DESC texDesc{};
		texDesc.Width	  = desc._width;
		texDesc.Height	  = desc._height;
		texDesc.MipLevels = desc._mipLevels;
		texDesc.ArraySize = 1;
		// Typeless so we can create both DSV and depth SRV for shadow sampling.
		texDesc.Format			   = bDepth ? DXGI_FORMAT_R24G8_TYPELESS : D3D11RHIResourceInternal::toDxgiFormatD3D11( desc._format );
		texDesc.SampleDesc.Count   = 1;
		texDesc.SampleDesc.Quality = 0;
		texDesc.Usage			   = D3D11_USAGE_DEFAULT;
		texDesc.BindFlags		   = 0;
		texDesc.CPUAccessFlags	   = 0;
		texDesc.MiscFlags		   = 0;

		if ( desc._bIsRenderTarget && bDepth == false )
			texDesc.BindFlags |= D3D11_BIND_RENDER_TARGET;
		if ( desc._bIsShaderResource )
			texDesc.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
		if ( bDepth )
			texDesc.BindFlags |= D3D11_BIND_DEPTH_STENCIL;
		if ( desc._bIsUnorderedAccess )
			texDesc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;

		if ( texDesc.BindFlags == 0 )
			texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

		D3D11RHIDevice::TextureRecord record{};
		record._width	 = desc._width;
		record._height	 = desc._height;
		record._bDepth	 = bDepth ? 1 : 0;
		record._reserved = 0;

		if ( FAILED( _pDevice->_device->CreateTexture2D( &texDesc, nullptr, record._texture.GetAddressOf() ) ) )
		{
			SW_LOG_ERROR( "Failed to create Texture2D (%#x%#).", desc._width, desc._height );
			return 0;
		}

		if ( desc._bIsRenderTarget && bDepth == false )
		{
			if ( FAILED( _pDevice->_device->CreateRenderTargetView( record._texture.Get(), nullptr, record._rtv.GetAddressOf() ) ) )
			{
				SW_LOG_ERROR( "Failed to create RTV for Texture2D." );
				return 0;
			}
		}

		if ( bDepth )
		{
			D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
			dsvDesc.Format			   = DXGI_FORMAT_D24_UNORM_S8_UINT;
			dsvDesc.ViewDimension	   = D3D11_DSV_DIMENSION_TEXTURE2D;
			dsvDesc.Texture2D.MipSlice = 0;
			if ( FAILED( _pDevice->_device->CreateDepthStencilView( record._texture.Get(), &dsvDesc, record._dsv.GetAddressOf() ) ) )
			{
				SW_LOG_ERROR( "Failed to create DSV for Texture2D." );
				return 0;
			}
		}

		if ( desc._bIsShaderResource )
		{
			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
			srvDesc.ViewDimension		= D3D11_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MipLevels = desc._mipLevels;
			if ( bDepth )
				srvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
			else
				srvDesc.Format = D3D11RHIResourceInternal::toDxgiFormatD3D11( desc._format );

			if ( FAILED( _pDevice->_device->CreateShaderResourceView( record._texture.Get(), bDepth ? &srvDesc : nullptr, record._srv.GetAddressOf() ) ) )
			{
				SW_LOG_ERROR( "Failed to create SRV for Texture2D." );
				return 0;
			}
		}

		return _pDevice->storeTexture( std::move( record ) );
	}

	void D3D11RHIResource::destroyTexture( RHITextureHandle texture )
	{
		if ( texture == 0 )
			return;

		D3D11RHIDevice::TextureRecord* pSlot = _pDevice->resolveTexture( texture );
		if ( pSlot == nullptr )
			return;

		for ( size_t textureIndex = 0; textureIndex < _pDevice->_listRegisteredTexture.size(); ++textureIndex )
		{
			if ( _pDevice->_listRegisteredTexture[textureIndex] != texture )
				continue;
			_pDevice->_listRegisteredTexture[textureIndex] = 0;
			_pDevice->_listTextureFree.push_back( static_cast<uint32>( textureIndex ) );
		}

		D3D11RHIDevice::TextureRecord owned;
		if ( _pDevice->_gpuTextures.take( texture, owned ) == false )
			return;

		auto releaseCb = [owned]()
		{ (void)owned._texture.Get(); };
		_pDevice->_releaseQueue.enqueueRelease( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, releaseCb ) );
	}

	RHIDescriptorIndex D3D11RHIResource::registerBindlessTexture( RHITextureHandle texture )
	{
		if ( texture == 0 )
			return kInvalidDescriptorIndex;

		D3D11RHIDevice::TextureRecord* pRecord = _pDevice->resolveTexture( texture );
		if ( pRecord == nullptr || pRecord->_srv == nullptr )
			return kInvalidDescriptorIndex;

		RHIDescriptorIndex index;
		if ( _pDevice->_listTextureFree.empty() == false )
		{
			index = _pDevice->_listTextureFree.back();
			_pDevice->_listTextureFree.pop_back();
			_pDevice->_listRegisteredTexture[index] = texture;
		}
		else
		{
			index = static_cast<RHIDescriptorIndex>( _pDevice->_listRegisteredTexture.size() );
			_pDevice->_listRegisteredTexture.push_back( texture );
		}
		return index;
	}

	RHIDescriptorIndex D3D11RHIResource::registerBindlessResource( RHIBufferHandle buffer )
	{
		if ( buffer == 0 )
			return kInvalidDescriptorIndex;

		ID3D11Buffer* pRes = _pDevice->resolveBuffer( buffer );
		if ( pRes == nullptr )
			return kInvalidDescriptorIndex;
		RHIDescriptorIndex index;
		if ( _pDevice->_listBindlessFree.empty() == false )
		{
			index = _pDevice->_listBindlessFree.back();
			_pDevice->_listBindlessFree.pop_back();
			_pDevice->_listRegisteredBindlessVector[index] = buffer;
		}
		else
		{
			index = static_cast<RHIDescriptorIndex>( _pDevice->_listRegisteredBindlessVector.size() );
			_pDevice->_listRegisteredBindlessVector.push_back( buffer );
		}
		return index;
	}

	void D3D11RHIResource::unregisterBindlessResource( RHIDescriptorIndex index )
	{
		if ( index < _pDevice->_listRegisteredBindlessVector.size() )
		{
			_pDevice->_listRegisteredBindlessVector[index] = 0;
			_pDevice->_listBindlessFree.push_back( index );
		}
	}

	RHIDescriptorIndex D3D11RHIResource::registerBindlessUAV( RHIBufferHandle buffer )
	{
		if ( buffer == 0 )
			return kInvalidDescriptorIndex;
		ID3D11Buffer* pRes = _pDevice->resolveBuffer( buffer );
		if ( pRes == nullptr )
			return kInvalidDescriptorIndex;

		D3D11_BUFFER_DESC desc;
		pRes->GetDesc( &desc );

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
		uavDesc.ViewDimension		= D3D11_UAV_DIMENSION_BUFFER;
		uavDesc.Buffer.FirstElement = 0;
		if ( ( desc.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS ) != 0 )
		{
			uavDesc.Format			   = DXGI_FORMAT_R32_TYPELESS;
			uavDesc.Buffer.NumElements = desc.ByteWidth / 4;
			uavDesc.Buffer.Flags	   = D3D11_BUFFER_UAV_FLAG_RAW;
		}
		else
		{
			if ( desc.StructureByteStride == 0 )
				return kInvalidDescriptorIndex;
			uavDesc.Format			   = DXGI_FORMAT_UNKNOWN;
			uavDesc.Buffer.NumElements = desc.ByteWidth / desc.StructureByteStride;
		}

		Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> uav;
		if ( FAILED( _pDevice->_device->CreateUnorderedAccessView( pRes, &uavDesc, uav.GetAddressOf() ) ) )
			return kInvalidDescriptorIndex;

		RHIDescriptorIndex index;
		if ( _pDevice->_listUavFree.empty() == false )
		{
			index = _pDevice->_listUavFree.back();
			_pDevice->_listUavFree.pop_back();
			_pDevice->_listRegisteredUAV[index]	  = uav;
			_pDevice->_listUavSourceBuffer[index] = buffer;
		}
		else
		{
			index = static_cast<RHIDescriptorIndex>( _pDevice->_listRegisteredUAV.size() );
			_pDevice->_listRegisteredUAV.push_back( uav );
			_pDevice->_listUavSourceBuffer.push_back( buffer );
		}
		return index;
	}

	void D3D11RHIResource::unregisterBindlessUAV( RHIDescriptorIndex index ) { (void)index; }
} // namespace sw
#endif
