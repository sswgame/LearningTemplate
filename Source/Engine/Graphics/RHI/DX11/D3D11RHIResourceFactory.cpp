#include "pch.h"

#include "Engine/Graphics/RHI/DX11/D3D11RHIResourceFactory.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/RHI/DX/RHIDxgiFormat.h"
#include "Engine/Graphics/RHI/DX11/D3D11RHIDevice.h"
#include "Engine/Graphics/RHI/Support/RHIBufferSize.h"
#include "Engine/Graphics/RHI/Support/RHIIndexFreeList.h"
#include "Engine/Graphics/RHI/Support/RHIMemoryLedger.h"
#include "Engine/Graphics/Shader/Compile/ShaderCache.h"

#if defined( SW_PLATFORM_WINDOWS )
namespace sw
{
    SW_LOG_CALLER( "D3D11" );

    RHIBufferHandle D3D11RHIResourceFactory::createConstantBuffer( uint32 size )
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

    void D3D11RHIResourceFactory::updateConstantBuffer( RHIBufferHandle buffer, const void* pData, uint32 size )
    {
        if ( buffer == 0 || pData == nullptr || _pDevice->_deviceContext == nullptr )
            return;
        ID3D11Buffer* pResource = _pDevice->resolveBuffer( buffer );
        if ( pResource == nullptr )
            return;
        // 기록 밖(에셋 · 머티리얼 파라미터)의 갱신이다 — 즉시 컨텍스트에 쓴다. 즉시 컨텍스트는 스레드 안전하지 않으므로 잠근다.
        // 기록 중의 갱신은 리스트의 Deferred Context 로 가는 `IRHICommandList::updateConstantBuffer` 다.
        D3D11_MAPPED_SUBRESOURCE mapped{};
        std::scoped_lock<mutex>  lock{ _pDevice->_immediateContextMutex };
        if ( SUCCEEDED( _pDevice->_deviceContext->Map( pResource, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped ) ) )
        {
            Memory::copy( mapped.pData, pData, size );
            _pDevice->_deviceContext->Unmap( pResource, 0 );
        }
    }

    RHIBufferHandle D3D11RHIResourceFactory::createStructuredBuffer( uint32 elementSize, uint32 elementCount )
    {
        if ( elementSize == 0 || elementCount == 0 )
            return 0;

        // 32비트 API 다. 담기지 않으면 만들지 않는다(RHIBufferSize 가 세 백엔드의 규칙 하나).
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

        // 그래픽스 VS/PS 가 StructuredBuffer 로 읽을 수 있도록 SRV 를 만들어 둔다 (GPUScene 인스턴스 버퍼 등).
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format              = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension       = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements  = elementCount;
        return _pDevice->storeBufferWithSrv( std::move( buffer ), srvDesc );
    }

    RHIBufferHandle D3D11RHIResourceFactory::createBuffer( const RHIBufferDesc& desc )
    {
        // 인다이렉트 인자 버퍼만 따로 만든다. D3D11 은 `DRAWINDIRECT_ARGS` 를 `BUFFER_STRUCTURED` 와
        // **함께 쓸 수 없다.** 기본 경로(createStructuredBuffer)는 항상 STRUCTURED 로 만들므로, 간접 인자
        // 버퍼가 그리로 가면 DrawInstancedIndirect 가 조용히 아무것도 하지 않는다(디버그 레이어를 켜지
        // 않으면 흔적도 없다).
        if ( EnumUtil::hasFlag( desc._usage, RHIBufferUsage::IndirectArgs ) == false )
            return IRHIResourceFactory::createBuffer( desc );

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

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format                = DXGI_FORMAT_R32_TYPELESS;
        srvDesc.ViewDimension         = D3D11_SRV_DIMENSION_BUFFEREX;
        srvDesc.BufferEx.FirstElement = 0;
        srvDesc.BufferEx.NumElements  = sizeBytes / 4;
        srvDesc.BufferEx.Flags        = D3D11_BUFFEREX_SRV_FLAG_RAW;
        return _pDevice->storeBufferWithSrv( std::move( buffer ), srvDesc );
    }

    void D3D11RHIResourceFactory::updateStructuredBufferRegions( RHIBufferHandle buffer, const void* pBaseSource,
                                                                 const RHIBufferCopyRegion* pRegions, uint32 regionCount )
    {
        if ( buffer == 0 || pBaseSource == nullptr || pRegions == nullptr || regionCount == 0 || _pDevice->_deviceContext == nullptr )
            return;
        ID3D11Buffer* pResource = _pDevice->resolveBuffer( buffer );
        if ( pResource == nullptr )
            return;

        const uint8*            pBase = static_cast<const uint8*>( pBaseSource );
        std::scoped_lock<mutex> lock{ _pDevice->_immediateContextMutex };
        for ( uint32 regionIndex = 0; regionIndex < regionCount; ++regionIndex )
        {
            const RHIBufferCopyRegion& region = pRegions[regionIndex];
            if ( region._size == 0 )
                continue;

            // 상자 없는 UpdateSubresource 는 버퍼 **전체**(ByteWidth)를 원본에서 읽는다 — 원본이 버퍼보다 짧으면(용량을 남겨 둔 풀이 줄었을 때)
            // 원본 뒤를 넘어 읽어 드라이버 안에서 죽는다. 버퍼 전체를 덮는 조각일 때만 상자를 뺀다.
            if ( region._dstOffset == 0 && regionCount == 1 )
            {
                D3D11_BUFFER_DESC desc{};
                pResource->GetDesc( &desc );
                if ( region._size >= desc.ByteWidth )
                {
                    _pDevice->_deviceContext->UpdateSubresource( pResource, 0, nullptr, pBase, region._size, 0 );
                    continue;
                }
            }

            // 부분 갱신은 상자로 준다. 버퍼는 1차원이므로 x 만 쓰고 y · z 는 1 이다.
            D3D11_BOX box{};
            box.left   = region._dstOffset;
            box.right  = region._dstOffset + region._size;
            box.top    = 0;
            box.bottom = 1;
            box.front  = 0;
            box.back   = 1;
            _pDevice->_deviceContext->UpdateSubresource( pResource, 0, &box, pBase + region._srcOffset, region._size, 0 );
        }
    }

    RHIBufferHandle D3D11RHIResourceFactory::createVertexBuffer( const void* pData, uint32 sizeBytes )
    {
        return createFilledBuffer( pData, sizeBytes, D3D11_BIND_VERTEX_BUFFER );
    }

    RHIBufferHandle D3D11RHIResourceFactory::createIndexBuffer( const void* pData, uint32 sizeBytes, uint32 indexStride )
    {
        // 인덱스 크기는 걸 때(setIndexBuffer) 정한다. D3D11 규칙상 인덱스 버퍼는 BIND_INDEX_BUFFER 로 만들어야 하고
        // 구조버퍼(BUFFER_STRUCTURED)에는 그 플래그를 붙일 수 없다. 주의: 구조버퍼로 만들어도 드라이버가 받아 줘 그려지므로 틀려도 티가 안 난다.
        (void)indexStride;
        return createFilledBuffer( pData, sizeBytes, D3D11_BIND_INDEX_BUFFER );
    }

    RHIBufferHandle D3D11RHIResourceFactory::createFilledBuffer( const void* pData, uint32 sizeBytes, uint32 bindFlags )
    {
        if ( _pDevice == nullptr || pData == nullptr || sizeBytes == 0 )
            return 0;

        D3D11_BUFFER_DESC bufferDesc{};
        bufferDesc.Usage          = D3D11_USAGE_DEFAULT;
        bufferDesc.ByteWidth      = sizeBytes;
        bufferDesc.BindFlags      = bindFlags;
        bufferDesc.CPUAccessFlags = 0;

        D3D11_SUBRESOURCE_DATA init{};
        init.pSysMem = pData;

        Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
        if ( FAILED( _pDevice->_device->CreateBuffer( &bufferDesc, &init, buffer.GetAddressOf() ) ) )
            return 0;

        return _pDevice->storeBuffer( std::move( buffer ) );
    }

    void D3D11RHIResourceFactory::destroyBuffer( RHIBufferHandle buffer )
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

        // 장부는 자원을 실제로 놓을 때 줄인다(해제 요청 시점이 아니라).
        RHIMemoryLedger* pLedger   = &_pDevice->getMemoryLedger();
        auto             releaseCb = [owned, pLedger, buffer]()
        {
            (void)owned.Get();
            pLedger->recordFree( RHIMemoryKey::makeBuffer( buffer ) );
        };
        _pDevice->_releaseQueue.enqueueRelease( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, releaseCb ) );
    }

    bool D3D11RHIResourceFactory::uploadTexture2D( RHITextureHandle texture, const RHITextureUploadDesc& desc )
    {
        D3D11RHIDevice::TextureRecord* pRecord = _pDevice->resolveTexture( texture );
        if ( pRecord == nullptr || pRecord->_texture == nullptr || _pDevice->_deviceContext == nullptr )
            return false;
        if ( pRecord->_bDepth != SW_FALSE )
            return false;

        D3D11_TEXTURE2D_DESC texDesc{};
        pRecord->_texture->GetDesc( &texDesc );
        if ( desc._arraySlice >= texDesc.ArraySize )
        {
            SW_LOG_ERROR( "uploadTexture2D: slice %# is out of range (%# slices)", desc._arraySlice, texDesc.ArraySize );
            return false;
        }

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
            const RHITextureMipSpan& span        = arrMip[mip];
            const UINT               subresource = D3D11CalcSubresource( span._mip, desc._arraySlice, texDesc.MipLevels );
            _pDevice->_deviceContext->UpdateSubresource( pRecord->_texture.Get(), subresource, nullptr, span._pData, span._rowBytes, span._sizeBytes );
        }
        return true;
    }

    RHIFormat D3D11RHIResourceFactory::getTextureFormat( RHITextureHandle texture ) const
    {
        const D3D11RHIDevice::TextureRecord* pRecord = _pDevice->resolveTexture( texture );
        if ( pRecord == nullptr || pRecord->_texture == nullptr )
            return RHIFormat::Unknown;
        D3D11_TEXTURE2D_DESC texDesc{};
        pRecord->_texture->GetDesc( &texDesc );
        // 깊이는 typeless 로 만들어져 DXGI 역변환이 Unknown 을 준다. 레코드 플래그로 되돌린다.
        if ( pRecord->_bDepth != SW_FALSE )
            return RHIFormat::D24_UNORM_S8_UINT;
        return fromDxgiFormat( texDesc.Format );
    }

    bool D3D11RHIResourceFactory::readbackTexture2D( RHITextureHandle texture, uint32 mip, uint32 arraySlice, vector<uint8>& outBytes, RHITextureMipSpan& outLayout )
    {
        D3D11RHIDevice::TextureRecord* pRecord = _pDevice->resolveTexture( texture );
        if ( pRecord == nullptr || pRecord->_texture == nullptr || _pDevice->_device == nullptr || _pDevice->_deviceContext == nullptr )
            return false;
        if ( pRecord->_bDepth != SW_FALSE )
            return false;

        D3D11_TEXTURE2D_DESC texDesc{};
        pRecord->_texture->GetDesc( &texDesc );
        if ( mip >= texDesc.MipLevels || arraySlice >= texDesc.ArraySize )
            return false;
        if ( computeRhiTextureMipLayout( fromDxgiFormat( texDesc.Format ), texDesc.Width, texDesc.Height, mip, outLayout ) == false )
            return false;

        // 밉 하나 크기의 스테이징 텍스처로 복사한 뒤 Map 한다. Map 이 GPU 를 기다린다.
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
        _pDevice->_deviceContext->CopySubresourceRegion( staging.Get(), 0, 0, 0, 0, pRecord->_texture.Get(), D3D11CalcSubresource( mip, arraySlice, texDesc.MipLevels ),
                                                         nullptr );

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

    RHITextureHandle D3D11RHIResourceFactory::createTexture2D( const RHITextureDesc& desc )
    {
        if ( _pDevice == nullptr || desc._width == 0 || desc._height == 0 )
            return 0;
        if ( isRhiTextureShapeValid( desc ) == false )
        {
            SW_LOG_ERROR( "createTexture2D: dimension %# with %# slices (%#x%#) is not a valid texture shape", static_cast<uint32>( desc._dimension ),
                          desc._arraySize, desc._width, desc._height );
            return 0;
        }

        const bool bDepth  = desc._bIsDepthStencil != SW_FALSE;
        const bool bCube   = desc._dimension == RHITextureDimension::TextureCube;
        const bool bSliced = desc._dimension != RHITextureDimension::Texture2D;

        D3D11_TEXTURE2D_DESC texDesc{};
        texDesc.Width     = desc._width;
        texDesc.Height    = desc._height;
        texDesc.MipLevels = desc._mipLevels;
        texDesc.ArraySize = desc._arraySize;
        // DSV 와 그림자 샘플링용 깊이 SRV 를 둘 다 만들 수 있게 typeless 로 만든다.
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
        if ( bCube )
            texDesc.MiscFlags |= D3D11_RESOURCE_MISC_TEXTURECUBE;

        D3D11RHIDevice::TextureRecord record{};
        record._width     = desc._width;
        record._height    = desc._height;
        record._arraySize = desc._arraySize;
        record._dimension = desc._dimension;
        record._bDepth    = bDepth ? 1 : 0;
        record._reserved  = 0;

        if ( FAILED( _pDevice->_device->CreateTexture2D( &texDesc, nullptr, record._texture.GetAddressOf() ) ) )
        {
            SW_LOG_ERROR( "Failed to create Texture2D (%#×%#).", desc._width, desc._height );
            return 0;
        }

        // 면이 여럿이면 렌더 패스가 면 하나를 타깃으로 고른다 — 면마다 배열 뷰(원소 하나)를 만든다. `_rtv` · `_dsv` 는 면 0 이다.
        if ( desc._bIsRenderTarget && bDepth == false )
        {
            for ( uint32 slice = 0; slice < desc._arraySize; ++slice )
            {
                D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
                rtvDesc.Format                         = toDxgiFormat( desc._format );
                rtvDesc.ViewDimension                  = bSliced ? D3D11_RTV_DIMENSION_TEXTURE2DARRAY : D3D11_RTV_DIMENSION_TEXTURE2D;
                rtvDesc.Texture2DArray.MipSlice        = 0;
                rtvDesc.Texture2DArray.FirstArraySlice = slice;
                rtvDesc.Texture2DArray.ArraySize       = 1;
                Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv;
                if ( FAILED( _pDevice->_device->CreateRenderTargetView( record._texture.Get(), &rtvDesc, rtv.GetAddressOf() ) ) )
                {
                    SW_LOG_ERROR( "Failed to create RTV for Texture2D (slice %#).", slice );
                    return 0;
                }
                if ( slice == 0 )
                    record._rtv = rtv;
                if ( bSliced )
                    record._listSliceRtv.push_back( std::move( rtv ) );
            }
        }

        if ( bDepth )
        {
            for ( uint32 slice = 0; slice < desc._arraySize; ++slice )
            {
                D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
                dsvDesc.Format                         = toDxgiFormat( constant::kDepthStencilFormat );
                dsvDesc.ViewDimension                  = bSliced ? D3D11_DSV_DIMENSION_TEXTURE2DARRAY : D3D11_DSV_DIMENSION_TEXTURE2D;
                dsvDesc.Texture2DArray.MipSlice        = 0;
                dsvDesc.Texture2DArray.FirstArraySlice = slice;
                dsvDesc.Texture2DArray.ArraySize       = 1;
                Microsoft::WRL::ComPtr<ID3D11DepthStencilView> dsv;
                if ( FAILED( _pDevice->_device->CreateDepthStencilView( record._texture.Get(), &dsvDesc, dsv.GetAddressOf() ) ) )
                {
                    SW_LOG_ERROR( "Failed to create DSV for Texture2D (slice %#).", slice );
                    return 0;
                }
                if ( slice == 0 )
                    record._dsv = dsv;
                if ( bSliced )
                    record._listSliceDsv.push_back( std::move( dsv ) );
            }
        }

        if ( desc._bIsShaderResource )
        {
            // 밉은 텍스처가 가진 전부다(-1). 서술체의 0 은 D3D11 에서 "전체 체인" 이라 숫자로 옮기면 안 된다.
            constexpr UINT                  kAllMips = static_cast<UINT>( -1 );
            D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.Format = bDepth ? DXGI_FORMAT_R24_UNORM_X8_TYPELESS : toDxgiFormat( desc._format );
            switch ( desc._dimension )
            {
                case RHITextureDimension::Texture2DArray:
                {
                    srvDesc.ViewDimension                  = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
                    srvDesc.Texture2DArray.MipLevels       = kAllMips;
                    srvDesc.Texture2DArray.FirstArraySlice = 0;
                    srvDesc.Texture2DArray.ArraySize       = desc._arraySize;
                    break;
                }
                case RHITextureDimension::TextureCube:
                {
                    srvDesc.ViewDimension         = D3D11_SRV_DIMENSION_TEXTURECUBE;
                    srvDesc.TextureCube.MipLevels = kAllMips;
                    break;
                }
                case RHITextureDimension::Texture2D:
                {
                    srvDesc.ViewDimension       = D3D11_SRV_DIMENSION_TEXTURE2D;
                    srvDesc.Texture2D.MipLevels = kAllMips;
                    break;
                }
            }
            if ( FAILED( _pDevice->_device->CreateShaderResourceView( record._texture.Get(), &srvDesc, record._srv.GetAddressOf() ) ) )
            {
                SW_LOG_ERROR( "Failed to create SRV for Texture2D." );
                return 0;
            }
        }

        return _pDevice->storeTexture( std::move( record ), desc );
    }

    void D3D11RHIResourceFactory::destroyTexture( RHITextureHandle texture )
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

        // 장부는 자원을 실제로 놓을 때 줄인다(해제 요청 시점이 아니라).
        RHIMemoryLedger* pLedger   = &_pDevice->getMemoryLedger();
        auto             releaseCb = [owned, pLedger, texture]()
        {
            (void)owned._texture.Get();
            pLedger->recordFree( RHIMemoryKey::makeTexture( texture ) );
        };
        _pDevice->_releaseQueue.enqueueRelease( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, releaseCb ) );
    }
} // namespace sw
#endif
