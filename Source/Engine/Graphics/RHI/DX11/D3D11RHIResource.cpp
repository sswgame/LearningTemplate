#include "pch.h"

#include "Engine/Graphics/RHI/DX11/D3D11RHIResource.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/RHI/DX/RHIDxgiFormat.h"
#include "Engine/Graphics/RHI/DX11/D3D11RHIDevice.h"
#include "Engine/Graphics/RHI/Support/RHIIndexFreeList.h"
#include "Engine/Graphics/Shader/ShaderCache.h"

#if defined( SW_PLATFORM_WINDOWS )
namespace sw
{
    namespace
    {
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "D3D11" );

    RHIBufferHandle D3D11RHIResource::createConstantBuffer( uint32 size )
    {
        if ( _pDevice == nullptr || _pDevice->_device == nullptr || size == 0 )
        {
            SW_LOG_ERROR( "createConstantBuffer: invalid device or size=%#", size );
            return 0;
        }

        const UINT alignedSize = MathUtil::max( MathUtil::align( size, 16u ), 16u );

        D3D11_BUFFER_DESC bd{};
        bd.Usage          = D3D11_USAGE_DYNAMIC;
        bd.ByteWidth      = alignedSize;
        bd.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
        const HRESULT                        hr = _pDevice->_device->CreateBuffer( &bd, nullptr, buffer.GetAddressOf() );
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
        bd.Usage               = D3D11_USAGE_DEFAULT;
        bd.ByteWidth           = elementSize * elementCount;
        bd.BindFlags           = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
        bd.MiscFlags           = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bd.StructureByteStride = elementSize;

        Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
        if ( FAILED( _pDevice->_device->CreateBuffer( &bd, nullptr, buffer.GetAddressOf() ) ) )
            return 0;

        ID3D11Buffer* pBuffer = buffer.Get();

        // 그래픽스 VS/PS 가 StructuredBuffer 로 읽을 수 있도록 SRV 를 만들어 둔다 (GPUScene 인스턴스 버퍼 등).
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format              = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension       = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements  = elementCount;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
        _pDevice->_device->CreateShaderResourceView( pBuffer, &srvDesc, srv.GetAddressOf() );

        const RHIBufferHandle handle = _pDevice->storeBuffer( std::move( buffer ) );
        if ( handle != 0 && srv )
            _pDevice->_mapBufferSrv[handle] = std::move( srv );
        return handle;
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
        bd.Usage          = D3D11_USAGE_DEFAULT;
        bd.ByteWidth      = sizeBytes;
        bd.BindFlags      = D3D11_BIND_VERTEX_BUFFER;
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
        if ( buffer == _pDevice->_recordingState._boundMeshVb )
            _pDevice->_recordingState._boundMeshVb = 0;

        Microsoft::WRL::ComPtr<ID3D11Buffer> owned;
        if ( _pDevice->_gpuBuffers.take( buffer, owned ) == false )
            return;

        _pDevice->_mapBufferSrv.erase( buffer );

        std::unique_lock<std::shared_mutex> registryLock{ _pDevice->_bindlessMutex };
        for ( size_t bindlessIndex = 0; bindlessIndex < _pDevice->_listRegisteredBindless.size(); ++bindlessIndex )
        {
            if ( _pDevice->_listRegisteredBindless[bindlessIndex] != buffer )
                continue;
            releaseFreeListIndex( _pDevice->_listRegisteredBindless, _pDevice->_listBindlessFree,
                                  static_cast<uint32>( bindlessIndex ), RHIBufferHandle{ 0 } );
        }
        for ( size_t bufferIndex = 0; bufferIndex < _pDevice->_listUavSourceBuffer.size(); ++bufferIndex )
        {
            if ( _pDevice->_listUavSourceBuffer[bufferIndex] != buffer )
                continue;
            _pDevice->_listRegisteredUAV[bufferIndex].Reset();
            releaseFreeListIndex( _pDevice->_listUavSourceBuffer, _pDevice->_listUavFree,
                                  static_cast<uint32>( bufferIndex ), RHIBufferHandle{ 0 } );
        }

        auto releaseCb = [owned]()
        { (void)owned.Get(); };
        _pDevice->_releaseQueue.enqueueRelease( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, releaseCb ) );
    }

    bool D3D11RHIResource::uploadTexture2D( RHITextureHandle texture, const RHITextureUploadDesc& desc )
    {
        D3D11RHIDevice::TextureRecord* pRecord = _pDevice->resolveTexture( texture );
        if ( pRecord == nullptr || pRecord->_texture == nullptr || _pDevice->_deviceContext == nullptr )
            return false;
        if ( pRecord->_bDepth != 0 )
            return false;

        D3D11_TEXTURE2D_DESC texDesc{};
        pRecord->_texture->GetDesc( &texDesc );

        RHITextureMipSpan arrMip[constant::kMaxTextureMipCount]{};
        const uint32      mipCount = resolveTextureUploadMips( desc, fromDxgiFormat( texDesc.Format ), texDesc.Width, texDesc.Height,
                                                               texDesc.MipLevels, arrMip, constant::kMaxTextureMipCount );
        if ( mipCount == 0 )
        {
            SW_LOG_ERROR( "uploadTexture2D: unsupported format or not enough data (%# bytes for %#x%#, %# mips)",
                          desc._sizeBytes, texDesc.Width, texDesc.Height, texDesc.MipLevels );
            return false;
        }

        // UpdateSubresource 는 즉시 컨텍스트 큐에 순서대로 들어가므로 뒤이은 드로우보다 먼저 실행된다.
        for ( uint32 mip = 0; mip < mipCount; ++mip )
        {
            const RHITextureMipSpan& span = arrMip[mip];
            _pDevice->_deviceContext->UpdateSubresource( pRecord->_texture.Get(), span._mip, nullptr, span._pData, span._rowBytes, span._sizeBytes );
        }
        return true;
    }

    RHITextureHandle D3D11RHIResource::createTexture2D( const RHITextureDesc& desc )
    {
        if ( _pDevice == nullptr || desc._width == 0 || desc._height == 0 )
            return 0;

        const bool bDepth = desc._bIsDepthStencil != 0;

        D3D11_TEXTURE2D_DESC texDesc{};
        texDesc.Width     = desc._width;
        texDesc.Height    = desc._height;
        texDesc.MipLevels = desc._mipLevels;
        texDesc.ArraySize = 1;
        // Typeless so we can create both DSV and depth SRV for shadow sampling.
        texDesc.Format             = bDepth ? DXGI_FORMAT_R24G8_TYPELESS : toDxgiFormat( desc._format );
        texDesc.SampleDesc.Count   = 1;
        texDesc.SampleDesc.Quality = 0;
        texDesc.Usage              = D3D11_USAGE_DEFAULT;
        texDesc.BindFlags          = 0;
        texDesc.CPUAccessFlags     = 0;
        texDesc.MiscFlags          = 0;

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
        record._width    = desc._width;
        record._height   = desc._height;
        record._bDepth   = bDepth ? 1 : 0;
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
            dsvDesc.Format             = toDxgiFormat( constant::kDepthStencilFormat );
            dsvDesc.ViewDimension      = D3D11_DSV_DIMENSION_TEXTURE2D;
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
            srvDesc.ViewDimension       = D3D11_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MipLevels = desc._mipLevels;
            if ( bDepth )
                srvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
            else
                srvDesc.Format = toDxgiFormat( desc._format );

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

        {
            std::unique_lock<std::shared_mutex> registryLock{ _pDevice->_bindlessMutex };
            for ( size_t textureIndex = 0; textureIndex < _pDevice->_listRegisteredTexture.size(); ++textureIndex )
            {
                if ( _pDevice->_listRegisteredTexture[textureIndex] != texture )
                    continue;
                releaseFreeListIndex( _pDevice->_listRegisteredTexture, _pDevice->_listTextureFree,
                                      static_cast<uint32>( textureIndex ), RHITextureHandle{ 0 } );
            }
        }

        D3D11RHIDevice::TextureRecord owned;
        if ( _pDevice->_gpuTextures.take( texture, owned ) == false )
            return;

        auto releaseCb = [owned]()
        { (void)owned._texture.Get(); };
        _pDevice->_releaseQueue.enqueueRelease( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, releaseCb ) );
    }
} // namespace sw
#endif
