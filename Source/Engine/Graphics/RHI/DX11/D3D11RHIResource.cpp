#include "pch.h"

#include "Engine/Graphics/RHI/DX11/D3D11RHIResource.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/RHI/DX/RHIDxgiFormat.h"
#include "Engine/Graphics/RHI/DX11/D3D11RHIDevice.h"
#include "Engine/Graphics/RHI/Support/RHIBufferSize.h"
#include "Engine/Graphics/RHI/Support/RHIIndexFreeList.h"
#include "Engine/Graphics/Shader/Compile/ShaderCache.h"

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

        D3D11_BUFFER_DESC bufferDesc{};
        bufferDesc.Usage          = D3D11_USAGE_DYNAMIC;
        bufferDesc.ByteWidth      = alignedSize;
        bufferDesc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
        bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
        const HRESULT                        hr = _pDevice->_device->CreateBuffer( &bufferDesc, nullptr, buffer.GetAddressOf() );
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
        // **이 경로는 드로우마다 불린다.** 기록 중인 스레드는 **자기 Deferred Context** 에 쓴다 —
        // D3D11 런타임이 커맨드 리스트 단위로 이 버퍼를 버저닝하므로 그 리스트의 드로우가 기록
        // 시점의 값을 보고, 컨텍스트가 스레드마다 따로라 락도 필요 없다. 그것이 D3D11 이 문서화한
        // 동적 버퍼 갱신 방식이다(`D3D11RHIDevice::bindRecordingContext` 주석).
        ID3D11DeviceContext*     pRecording = D3D11RHIDevice::getRecordingContext();
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if ( pRecording != nullptr )
        {
            if ( SUCCEEDED( pRecording->Map( pRes, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped ) ) )
            {
                Memory::copy( mapped.pData, pData, size );
                pRecording->Unmap( pRes, 0 );
            }
            return;
        }

        // 기록 중이 아니다(프레임 시드·셋업). 즉시 컨텍스트는 스레드 안전하지 않으므로 잠근다.
        std::scoped_lock<mutex> lock{ _pDevice->_immediateContextMutex };
        if ( SUCCEEDED( _pDevice->_deviceContext->Map( pRes, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped ) ) )
        {
            Memory::copy( mapped.pData, pData, size );
            _pDevice->_deviceContext->Unmap( pRes, 0 );
        }
    }

    RHIBufferHandle D3D11RHIResource::createStructuredBuffer( uint32 elementSize, uint32 elementCount )
    {
        if ( elementSize == 0 || elementCount == 0 )
            return 0;

        // 32비트 API 다 — 담기지 않으면 만들지 않는다(RHIBufferSize 가 세 백엔드의 규칙 하나).
        uint32 totalBytes{ 0 };
        if ( RHIBufferSize::computeStructuredBytes( elementSize, elementCount, totalBytes ) == false )
            return 0;

        D3D11_BUFFER_DESC bufferDesc{};
        bufferDesc.Usage               = D3D11_USAGE_DEFAULT;
        bufferDesc.ByteWidth           = static_cast<UINT>( totalBytes );
        bufferDesc.BindFlags           = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
        bufferDesc.MiscFlags           = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bufferDesc.StructureByteStride = elementSize;

        Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
        if ( FAILED( _pDevice->_device->CreateBuffer( &bufferDesc, nullptr, buffer.GetAddressOf() ) ) )
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

    RHIBufferHandle D3D11RHIResource::createBuffer( const RHIBufferDesc& desc )
    {
        // 인다이렉트 인자 버퍼만 따로 만든다. D3D11 은 `DRAWINDIRECT_ARGS` 를 `BUFFER_STRUCTURED` 와
        // **함께 쓸 수 없다** — 기본 경로(createStructuredBuffer)가 항상 STRUCTURED 로 만들기 때문에
        // GPUScene 의 간접 인자 버퍼가 DrawInstancedIndirect 에 쓸 수 없는 버퍼였고, 드로우가 조용히
        // 아무것도 하지 않았다(디버그 레이어를 켜지 않으면 흔적도 없다).
        if ( EnumUtil::hasFlag( desc._usage, RHIBufferUsage::IndirectArgs ) == false )
            return IRHIResource::createBuffer( desc );

        uint32 sizeBytes = desc._sizeBytes;
        if ( sizeBytes == 0 )
        {
            const uint32 elemSize  = desc._elementSize > 0 ? desc._elementSize : 4u;
            const uint32 elemCount = desc._elementCount > 0 ? desc._elementCount : 1u;
            sizeBytes              = elemSize * elemCount;
        }
        if ( sizeBytes == 0 || _pDevice->_device == nullptr )
            return 0;

        D3D11_BUFFER_DESC bufferDesc{};
        bufferDesc.Usage     = D3D11_USAGE_DEFAULT;
        bufferDesc.ByteWidth = sizeBytes;
        bufferDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
        // RAW 뷰는 허용된다(구조화와 달리). 컴퓨트 컬링은 D3D11 에서 끄지만(RHICapabilities 참고)
        // UAV 등록 경로가 이 플래그를 보고 raw UAV 를 만든다.
        bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS | D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;

        D3D11_SUBRESOURCE_DATA  initData{};
        D3D11_SUBRESOURCE_DATA* pInitData = nullptr;
        if ( desc._pInitialData != nullptr )
        {
            initData.pSysMem = desc._pInitialData;
            pInitData        = &initData;
        }

        Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
        if ( FAILED( _pDevice->_device->CreateBuffer( &bufferDesc, pInitData, buffer.GetAddressOf() ) ) )
        {
            SW_LOG_ERROR( "createBuffer: 인다이렉트 인자 버퍼 생성 실패 (%# bytes)", sizeBytes );
            return 0;
        }

        ID3D11Buffer* pBuffer = buffer.Get();

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format                = DXGI_FORMAT_R32_TYPELESS;
        srvDesc.ViewDimension         = D3D11_SRV_DIMENSION_BUFFEREX;
        srvDesc.BufferEx.FirstElement = 0;
        srvDesc.BufferEx.NumElements  = sizeBytes / 4;
        srvDesc.BufferEx.Flags        = D3D11_BUFFEREX_SRV_FLAG_RAW;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
        _pDevice->_device->CreateShaderResourceView( pBuffer, &srvDesc, srv.GetAddressOf() );

        const RHIBufferHandle handle = _pDevice->storeBuffer( std::move( buffer ) );
        if ( handle != 0 && srv )
            _pDevice->_mapBufferSrv[handle] = std::move( srv );
        return handle;
    }

    void D3D11RHIResource::updateStructuredBufferRegions( RHIBufferHandle buffer, const void* pBaseSource,
                                                          const RHIBufferCopyRegion* pRegions, uint32 regionCount )
    {
        if ( buffer == 0 || pBaseSource == nullptr || pRegions == nullptr || regionCount == 0 || _pDevice->_deviceContext == nullptr )
            return;
        ID3D11Buffer* pRes = _pDevice->resolveBuffer( buffer );
        if ( pRes == nullptr )
            return;

        const uint8*            pBase = static_cast<const uint8*>( pBaseSource );
        std::scoped_lock<mutex> lock{ _pDevice->_immediateContextMutex };
        for ( uint32 regionIndex = 0; regionIndex < regionCount; ++regionIndex )
        {
            const RHIBufferCopyRegion& region = pRegions[regionIndex];
            if ( region._size == 0 )
                continue;

            if ( region._dstOffset == 0 && regionCount == 1 )
            {
                _pDevice->_deviceContext->UpdateSubresource( pRes, 0, nullptr, pBase, region._size, 0 );
                continue;
            }

            // 부분 갱신은 상자로 준다 — 버퍼는 1차원이므로 x 만 쓰고 y·z 는 1 이다.
            D3D11_BOX box{};
            box.left   = region._dstOffset;
            box.right  = region._dstOffset + region._size;
            box.top    = 0;
            box.bottom = 1;
            box.front  = 0;
            box.back   = 1;
            _pDevice->_deviceContext->UpdateSubresource( pRes, 0, &box, pBase + region._srcOffset, region._size, 0 );
        }
    }

    RHIBufferHandle D3D11RHIResource::createVertexBuffer( const void* pData, uint32 sizeBytes )
    {
        if ( _pDevice == nullptr || pData == nullptr || sizeBytes == 0 )
            return 0;

        D3D11_BUFFER_DESC bufferDesc{};
        bufferDesc.Usage          = D3D11_USAGE_DEFAULT;
        bufferDesc.ByteWidth      = sizeBytes;
        bufferDesc.BindFlags      = D3D11_BIND_VERTEX_BUFFER;
        bufferDesc.CPUAccessFlags = 0;

        D3D11_SUBRESOURCE_DATA init{};
        init.pSysMem = pData;

        Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
        if ( FAILED( _pDevice->_device->CreateBuffer( &bufferDesc, &init, buffer.GetAddressOf() ) ) )
            return 0;

        return _pDevice->storeBuffer( std::move( buffer ) );
    }

    void D3D11RHIResource::destroyBuffer( RHIBufferHandle buffer )
    {
        if ( buffer == 0 )
            return;
        _pDevice->forgetBufferInRecordingStates( buffer );

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
        if ( pRecord->_bDepth != SW_FALSE )
            return false;

        D3D11_TEXTURE2D_DESC texDesc{};
        pRecord->_texture->GetDesc( &texDesc );

        RHITextureMipSpan arrMip[constant::kMaxTextureMipCount]{};
        const uint32      mipCount = resolveTextureUploadMips( desc, fromDxgiFormat( texDesc.Format ), texDesc.Width, texDesc.Height,
                                                               texDesc.MipLevels, arrMip, constant::kMaxTextureMipCount );
        if ( mipCount == 0 )
        {
            SW_LOG_ERROR( "uploadTexture2D: unsupported format or not enough data (%# bytes for %#×%#, %# mips)",
                          desc._sizeBytes, texDesc.Width, texDesc.Height, texDesc.MipLevels );
            return false;
        }

        // UpdateSubresource 는 즉시 컨텍스트 큐에 순서대로 들어가므로 뒤이은 드로우보다 먼저 실행된다.
        std::scoped_lock<mutex> lock{ _pDevice->_immediateContextMutex };
        for ( uint32 mip = 0; mip < mipCount; ++mip )
        {
            const RHITextureMipSpan& span = arrMip[mip];
            _pDevice->_deviceContext->UpdateSubresource( pRecord->_texture.Get(), span._mip, nullptr, span._pData, span._rowBytes, span._sizeBytes );
        }
        return true;
    }

    RHIFormat D3D11RHIResource::getTextureFormat( RHITextureHandle texture ) const
    {
        const D3D11RHIDevice::TextureRecord* pRecord = _pDevice->resolveTexture( texture );
        if ( pRecord == nullptr || pRecord->_texture == nullptr )
            return RHIFormat::Unknown;
        D3D11_TEXTURE2D_DESC texDesc{};
        pRecord->_texture->GetDesc( &texDesc );
        // 깊이는 typeless 로 만들어져 DXGI 역변환이 Unknown 을 준다 — 레코드 플래그로 되돌린다.
        if ( pRecord->_bDepth != SW_FALSE )
            return RHIFormat::D24_UNORM_S8_UINT;
        return fromDxgiFormat( texDesc.Format );
    }

    bool D3D11RHIResource::readbackTexture2D( RHITextureHandle texture, uint32 mip, vector<uint8>& outBytes, RHITextureMipSpan& outLayout )
    {
        D3D11RHIDevice::TextureRecord* pRecord = _pDevice->resolveTexture( texture );
        if ( pRecord == nullptr || pRecord->_texture == nullptr || _pDevice->_device == nullptr || _pDevice->_deviceContext == nullptr )
            return false;
        if ( pRecord->_bDepth != SW_FALSE )
            return false;

        D3D11_TEXTURE2D_DESC texDesc{};
        pRecord->_texture->GetDesc( &texDesc );
        if ( mip >= texDesc.MipLevels )
            return false;
        if ( computeRhiTextureMipLayout( fromDxgiFormat( texDesc.Format ), texDesc.Width, texDesc.Height, mip, outLayout ) == false )
            return false;

        // 밉 하나 크기의 스테이징 텍스처로 복사한 뒤 Map — Map 이 GPU 를 기다린다.
        D3D11_TEXTURE2D_DESC stagingDesc = texDesc;
        stagingDesc.Width                = outLayout._width;
        stagingDesc.Height               = outLayout._height;
        stagingDesc.MipLevels            = 1;
        stagingDesc.ArraySize            = 1;
        stagingDesc.Usage                = D3D11_USAGE_STAGING;
        stagingDesc.BindFlags            = 0;
        stagingDesc.CPUAccessFlags       = D3D11_CPU_ACCESS_READ;
        stagingDesc.MiscFlags            = 0;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
        if ( FAILED( _pDevice->_device->CreateTexture2D( &stagingDesc, nullptr, staging.GetAddressOf() ) ) )
            return false;

        std::scoped_lock<mutex> lock{ _pDevice->_immediateContextMutex };
        _pDevice->_deviceContext->CopySubresourceRegion( staging.Get(), 0, 0, 0, 0, pRecord->_texture.Get(), mip, nullptr );

        D3D11_MAPPED_SUBRESOURCE mapped{};
        if ( FAILED( _pDevice->_deviceContext->Map( staging.Get(), 0, D3D11_MAP_READ, 0, &mapped ) ) )
            return false;
        outBytes.assign( outLayout._sizeBytes, 0 );
        const uint32 rowCount = outLayout._sizeBytes / outLayout._rowBytes;
        for ( uint32 row = 0; row < rowCount; ++row )
            Memory::copy( outBytes.data() + static_cast<uint64>( row ) * outLayout._rowBytes,
                          static_cast<const uint8*>( mapped.pData ) + static_cast<uint64>( row ) * mapped.RowPitch, outLayout._rowBytes );
        _pDevice->_deviceContext->Unmap( staging.Get(), 0 );
        return true;
    }

    RHITextureHandle D3D11RHIResource::createTexture2D( const RHITextureDesc& desc )
    {
        if ( _pDevice == nullptr || desc._width == 0 || desc._height == 0 )
            return 0;

        const bool bDepth = desc._bIsDepthStencil != SW_FALSE;

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
            SW_LOG_ERROR( "Failed to create Texture2D (%#×%#).", desc._width, desc._height );
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
