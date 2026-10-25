#include "pch.h"

#include "Engine/Graphics/RHI/DX11/D3D11RHIResourceFactory.h"

#include "Core/Container/IndexFreeList.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/RHI/DX/RHIDxgiFormat.h"
#include "Engine/Graphics/RHI/DX11/D3D11RHIDevice.h"
#include "Engine/Graphics/RHI/Support/RHIBufferSize.h"
#include "Engine/Graphics/RHI/Support/RHIMemoryLedger.h"
#include "Engine/Graphics/Shader/Compile/ShaderCache.h"

#if defined( SW_PLATFORM_WINDOWS )
namespace sw
{
    SW_LOG_CALLER( "D3D11" );

    RHIBufferHandle D3D11RHIResourceFactory::createConstantBuffer( uint32 size )
    {
        const bool bInvalidConstantBufferRequest = _pDevice == nullptr || _pDevice->_device == nullptr || size == 0;
        if ( bInvalidConstantBufferRequest )
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
        const bool bInvalidConstantBufferUpdate = buffer == 0 || pData == nullptr || _pDevice->_immediateContext.isValid() == false;
        if ( bInvalidConstantBufferUpdate )
            return;
        ID3D11Buffer* pResource = _pDevice->resolveBuffer( buffer );
        if ( pResource == nullptr )
            return;
        // 기록 밖(에셋 · 머티리얼 파라미터)의 갱신이다 — 즉시 컨텍스트에 쓴다. Map 과 Unmap 사이에 렌더 스레드의 드로우가 끼지 않게 잠근다.
        // 기록 중의 갱신은 리스트의 Deferred Context 로 가는 `IRHICommandList::updateConstantBuffer` 다.
        D3D11_MAPPED_SUBRESOURCE                mapped{};
        const D3D11ImmediateContext::ScopedLock context = _pDevice->_immediateContext.lock();
        if ( SUCCEEDED( context->Map( pResource, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped ) ) )
        {
            Memory::copy( mapped.pData, pData, size );
            context->Unmap( pResource, 0 );
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
            SW_LOG_ERROR( "createBuffer: failed to create the indirect argument buffer (%# bytes)", sizeBytes );
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
        const bool bInvalidBufferRegionUpdate = buffer == 0 || pBaseSource == nullptr || pRegions == nullptr || regionCount == 0 || _pDevice->_immediateContext.isValid() == false;
        if ( bInvalidBufferRegionUpdate )
            return;
        ID3D11Buffer* pResource = _pDevice->resolveBuffer( buffer );
        if ( pResource == nullptr )
            return;

        const uint8*                            pBase   = static_cast<const uint8*>( pBaseSource );
        const D3D11ImmediateContext::ScopedLock context = _pDevice->_immediateContext.lock();
        for ( uint32 regionIndex = 0; regionIndex < regionCount; ++regionIndex )
        {
            const RHIBufferCopyRegion& region = pRegions[regionIndex];
            if ( region._sizeBytes == 0 )
                continue;

            // 상자 없는 UpdateSubresource 는 버퍼 **전체**(ByteWidth)를 원본에서 읽는다 — 원본이 버퍼보다 짧으면(용량을 남겨 둔 풀이 줄었을 때)
            // 원본 뒤를 넘어 읽어 드라이버 안에서 죽는다. 버퍼 전체를 덮는 조각일 때만 상자를 뺀다.
            if ( region._dstOffset == 0 && regionCount == 1 )
            {
                D3D11_BUFFER_DESC desc{};
                pResource->GetDesc( &desc );
                if ( region._sizeBytes >= desc.ByteWidth )
                {
                    context->UpdateSubresource( pResource, 0, nullptr, pBase, region._sizeBytes, 0 );
                    continue;
                }
            }

            // 부분 갱신은 상자로 준다. 버퍼는 1차원이므로 x 만 쓰고 y · z 는 1 이다.
            D3D11_BOX box{};
            box.left   = region._dstOffset;
            box.right  = region._dstOffset + region._sizeBytes;
            box.top    = 0;
            box.bottom = 1;
            box.front  = 0;
            box.back   = 1;
            context->UpdateSubresource( pResource, 0, &box, pBase + region._srcOffset, region._sizeBytes, 0 );
        }
    }

    RHIBufferHandle D3D11RHIResourceFactory::createVertexBuffer( const void* pData, uint32 sizeBytes )
    {
        return createFilledBuffer( pData, sizeBytes, D3D11_BIND_VERTEX_BUFFER );
    }

    void D3D11RHIResourceFactory::updateVertexBufferRegions( RHIBufferHandle buffer, const void* pBaseSource, const RHIBufferCopyRegion* pRegions, uint32 regionCount )
    {
        // 정점 버퍼도 DEFAULT 사용 버퍼다(createFilledBuffer) — 즉시 컨텍스트의 UpdateSubresource 상자 갱신이 그대로 맞고, 순서는 드라이버가 맞춘다.
        // 범위는 여기서 본다. 상자가 버퍼를 넘으면 D3D11 은 호출을 조용히 버린다.
        ID3D11Buffer* pResource = ( buffer != 0 && _pDevice->_immediateContext.isValid() ) ? _pDevice->resolveBuffer( buffer ) : nullptr;
        if ( pResource == nullptr || pRegions == nullptr )
            return;
        D3D11_BUFFER_DESC desc{};
        pResource->GetDesc( &desc );
        for ( uint32 regionIndex = 0; regionIndex < regionCount; ++regionIndex )
        {
            const RHIBufferCopyRegion& region = pRegions[regionIndex];
            if ( static_cast<uint64>( region._dstOffset ) + region._sizeBytes > desc.ByteWidth )
            {
                SW_LOG_ERROR( "updateVertexBufferRegions: region [%#, +%#) is outside the %#-byte vertex buffer; skipped", region._dstOffset, region._sizeBytes, desc.ByteWidth );
                continue;
            }
            updateStructuredBufferRegions( buffer, pBaseSource, &region, 1 );
        }
    }

    RHIBufferHandle D3D11RHIResourceFactory::createIndexBuffer( const void* pData, uint32 sizeBytes )
    {
        // 인덱스 크기는 걸 때(setIndexBuffer) 정한다. D3D11 규칙상 인덱스 버퍼는 BIND_INDEX_BUFFER 로 만들어야 하고
        // 구조버퍼(BUFFER_STRUCTURED)에는 그 플래그를 붙일 수 없다. 주의: 구조버퍼로 만들어도 드라이버가 받아 줘 그려지므로 틀려도 티가 안 난다.
        return createFilledBuffer( pData, sizeBytes, D3D11_BIND_INDEX_BUFFER );
    }

    RHIBufferHandle D3D11RHIResourceFactory::createFilledBuffer( const void* pData, uint32 sizeBytes, uint32 bindFlags )
    {
        const bool bInvalidFilledBufferRequest = _pDevice == nullptr || pData == nullptr || sizeBytes == 0;
        if ( bInvalidFilledBufferRequest )
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

        D3D11RHIDevice::BufferRecord owned;
        if ( _pDevice->_gpuBuffers.take( buffer, owned ) == false )
            return;

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

        // 장부는 자원을 실제로 놓을 때 줄인다(해제 요청 시점이 아니라). SRV 도 버퍼와 함께 그때 놓인다.
        _pDevice->_releaseQueue.enqueueLedgerRelease<RHIMemoryKeySpace::Buffer>( owned, _pDevice->getMemoryLedger(), buffer );
    }

    RHIBufferHandle D3D11RHIResourceFactory::createReadbackBuffer( uint32 elementSize, uint32 elementCount )
    {
        uint32     totalBytes{ 0 };
        const bool bInvalidReadbackRequest = _pDevice->_device == nullptr || elementSize == 0 || elementCount == 0 || RHIBufferSize::computeStructuredBytes( elementSize, elementCount, totalBytes ) == false;
        if ( bInvalidReadbackRequest )
            return 0;

        // 원본이 구조화 버퍼라 같은 모양(구조화 · 원소 크기)의 스테이징 버퍼여야 CopySubresourceRegion 이 받는다.
        D3D11_BUFFER_DESC bufferDesc{};
        bufferDesc.Usage               = D3D11_USAGE_STAGING;
        bufferDesc.ByteWidth           = static_cast<UINT>( totalBytes );
        bufferDesc.BindFlags           = 0;
        bufferDesc.CPUAccessFlags      = D3D11_CPU_ACCESS_READ;
        bufferDesc.MiscFlags           = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bufferDesc.StructureByteStride = elementSize;

        Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
        if ( FAILED( _pDevice->_device->CreateBuffer( &bufferDesc, nullptr, buffer.GetAddressOf() ) ) )
            return 0;
        return _pDevice->storeBuffer( std::move( buffer ) );
    }

    bool D3D11RHIResourceFactory::readReadbackBuffer( RHIBufferHandle readback, uint32 sizeBytes, void* pDestination )
    {
        ID3D11Buffer* pBuffer     = _pDevice->resolveBuffer( readback );
        const bool    bUnreadable = pBuffer == nullptr || pDestination == nullptr || sizeBytes == 0 || _pDevice->_immediateContext.isValid() == false;
        if ( bUnreadable )
            return false;
        D3D11_BUFFER_DESC bufferDesc{};
        pBuffer->GetDesc( &bufferDesc );
        const bool bNotReadback = bufferDesc.Usage != D3D11_USAGE_STAGING || ( bufferDesc.CPUAccessFlags & D3D11_CPU_ACCESS_READ ) == 0 || sizeBytes > bufferDesc.ByteWidth;
        if ( bNotReadback )
        {
            SW_LOG_ERROR( "readReadbackBuffer: buffer %# is not a readback buffer of at least %# bytes", readback, sizeBytes );
            return false;
        }
        // Map 에서 Unmap 까지 한 덩어리로 잠근다. 부르는 쪽 계약이 "GPU 작업이 끝난 뒤" 라서(완료 콜백 안) 기다리는 Map 이다 — `DO_NOT_WAIT` 는 끝난 복사에도
        // `DXGI_ERROR_WAS_STILL_DRAWING` 을 낸다(런타임 · 드라이버의 자원 사용 추적이 쿼리 완료보다 늦게 풀린다, takeTextureReadback 과 같은 결정 5-5).
        // 기다리는 Map 은 그 추적만 맞추고 곧 돌아온다. 장치를 잃었으면 Map 이 오류로 바로 돌아와 무한히 기다리지 않는다.
        D3D11_MAPPED_SUBRESOURCE                mapped{};
        const D3D11ImmediateContext::ScopedLock context = _pDevice->_immediateContext.lock();
        if ( FAILED( context->Map( pBuffer, 0, D3D11_MAP_READ, 0, &mapped ) ) || mapped.pData == nullptr )
            return false;
        Memory::copy( pDestination, mapped.pData, sizeBytes );
        context->Unmap( pBuffer, 0 );
        return true;
    }

    RHITransferSubmit D3D11RHIResourceFactory::submitTextureUpload( RHITextureHandle texture, const RHITextureUploadDesc& desc )
    {
        D3D11RHIDevice::TextureRecord* pRecord               = _pDevice->resolveTexture( texture );
        const bool                     bTextureNotUploadable = pRecord == nullptr || pRecord->_texture == nullptr || _pDevice->_immediateContext.isValid() == false;
        if ( bTextureNotUploadable )
        {
            SW_LOG_ERROR( "submitTextureUpload: texture %# or the device context is not usable", texture );
            return RHITransferSubmit::rejected();
        }
        // 장치를 잃은 뒤에는 전송이 끝나지 않아 스테이징 예산도 돌아오지 않는다 — Busy 를 영원히 되풀이하지 않게 진입에서 거절한다(계약 2).
        if ( _pDevice->isDeviceLost() )
        {
            SW_LOG_ERROR( "submitTextureUpload: the device was lost" );
            return RHITransferSubmit::rejected();
        }
        if ( pRecord->_bDepth != SW_FALSE )
        {
            SW_LOG_ERROR( "submitTextureUpload: texture %# is a depth texture", texture );
            return RHITransferSubmit::rejected();
        }

        D3D11_TEXTURE2D_DESC texDesc{};
        pRecord->_texture->GetDesc( &texDesc );
        if ( desc._arraySlice >= texDesc.ArraySize )
        {
            SW_LOG_ERROR( "submitTextureUpload: slice %# is out of range (%# slices)", desc._arraySlice, texDesc.ArraySize );
            return RHITransferSubmit::rejected();
        }

        RHITextureMipSpan arrMip[constant::kMaxTextureMipCount]{};
        const uint32      mipCount = resolveTextureUploadMips( desc, fromDxgiFormat( texDesc.Format ), texDesc.Width, texDesc.Height,
                                                               texDesc.MipLevels, arrMip, constant::kMaxTextureMipCount );
        if ( mipCount == 0 )
        {
            SW_LOG_ERROR( "submitTextureUpload: unsupported format or not enough data (%# bytes for %#×%#, %# mips)",
                          desc._sizeBytes, texDesc.Width, texDesc.Height, texDesc.MipLevels );
            return RHITransferSubmit::rejected();
        }

        uint64 uploadBytes{ 0 };
        for ( uint32 mip = 0; mip < mipCount; ++mip )
        {
            uploadBytes += arrMip[mip]._sizeBytes;
        }
        if ( uploadBytes > _pDevice->_directUploadMaxBytes )
        {
            // 큰 업로드는 소스 복사를 즉시 컨텍스트 잠금 밖(스테이징 생성)으로 뺀다. 렌더 스레드의 ExecuteCommandList · Present 가 그 복사를 기다리지 않는다.
            D3D11_SUBRESOURCE_DATA arrInitialData[constant::kMaxTextureMipCount]{};
            for ( uint32 mip = 0; mip < mipCount; ++mip )
            {
                arrInitialData[mip].pSysMem          = arrMip[mip]._pData;
                arrInitialData[mip].SysMemPitch      = arrMip[mip]._rowBytes;
                arrInitialData[mip].SysMemSlicePitch = arrMip[mip]._sizeBytes;
            }
            return _pDevice->submitStagedUpload( "submitTextureUpload", *pRecord, texDesc.Width, texDesc.Height, mipCount, arrInitialData,
                                                 D3D11CalcSubresource( 0, desc._arraySlice, texDesc.MipLevels ), 0, 0 );
        }

        // 작은 업로드(`_directUploadMaxBytes` 이하)는 UpdateSubresource 가 소스를 그 자리에서 복사한다(계약 1). 잠금 안의 복사가 짧아 스테이징 생성보다 싸다.
        // 즉시 컨텍스트 큐에 순서대로 들어가므로 앞서 제출된 프레임 뒤, 뒤이은 드로우 앞에서 실행된다(계약 3).
        // 끝은 큐 뒤에 단 이벤트 쿼리가 알린다. 시리얼은 같은 락 안에서 매긴다(계약 5).
        const D3D11ImmediateContext::ScopedLock context = _pDevice->_immediateContext.lock();
        Microsoft::WRL::ComPtr<ID3D11Query>     query   = _pDevice->acquireTransferQuery( context );
        if ( query == nullptr )
            return RHITransferSubmit::rejected();
        for ( uint32 mip = 0; mip < mipCount; ++mip )
        {
            const RHITextureMipSpan& span        = arrMip[mip];
            const UINT               subresource = D3D11CalcSubresource( span._mip, desc._arraySlice, texDesc.MipLevels );
            context->UpdateSubresource( pRecord->_texture.Get(), subresource, nullptr, span._pData, span._rowBytes, span._sizeBytes );
        }
        const uint64 serial = _pDevice->endTransfer( context, std::move( query ) );
        pRecord->_lastTransferSerial.store( serial );
        return _pDevice->_transferSerials.makeAccepted( serial );
    }

    RHITransferSubmit D3D11RHIResourceFactory::submitTextureRegionUpload( RHITextureHandle texture, const RHITextureRegionUploadDesc& desc )
    {
        D3D11RHIDevice::TextureRecord* pRecord                     = _pDevice->resolveTexture( texture );
        const bool                     bTextureRegionNotUploadable = pRecord == nullptr || pRecord->_texture == nullptr || _pDevice->_immediateContext.isValid() == false || pRecord->_bDepth != SW_FALSE;
        if ( bTextureRegionNotUploadable )
        {
            SW_LOG_ERROR( "submitTextureRegionUpload: texture %# is missing, a depth texture, or the device context is not usable", texture );
            return RHITransferSubmit::rejected();
        }
        // 장치를 잃은 뒤에는 전송이 끝나지 않아 스테이징 예산도 돌아오지 않는다 — Busy 를 영원히 되풀이하지 않게 진입에서 거절한다(계약 2).
        if ( _pDevice->isDeviceLost() )
        {
            SW_LOG_ERROR( "submitTextureRegionUpload: the device was lost" );
            return RHITransferSubmit::rejected();
        }

        D3D11_TEXTURE2D_DESC texDesc{};
        pRecord->_texture->GetDesc( &texDesc );
        uint32 rowBytes{ 0 };
        if ( validateTextureRegionUpload( fromDxgiFormat( texDesc.Format ), texDesc.Width, texDesc.Height, texDesc.MipLevels, texDesc.ArraySize, desc, rowBytes ) == false )
            return RHITransferSubmit::rejected();

        const UINT   subresource = D3D11CalcSubresource( desc._mip, desc._arraySlice, texDesc.MipLevels );
        const uint32 uploadBytes = rowBytes * desc._height;
        if ( uploadBytes > _pDevice->_directUploadMaxBytes )
        {
            // 큰 구간은 소스 복사를 즉시 컨텍스트 잠금 밖(스테이징 생성)으로 뺀다. 스테이징은 구간 크기 한 장이고 (x, y) 에 복사한다.
            const D3D11_SUBRESOURCE_DATA initialData{ desc._pData, rowBytes, uploadBytes };
            return _pDevice->submitStagedUpload( "submitTextureRegionUpload", *pRecord, desc._width, desc._height, 1, &initialData, subresource, desc._x, desc._y );
        }

        // 작은 구간(글리프 아틀라스 등)은 UpdateSubresource 로 바로 올린다. 박스는 밉 안의 픽셀 좌표다(끝은 포함하지 않는다).
        // 즉시 컨텍스트 큐에 순서대로 들어가 앞서 제출된 프레임 뒤, 뒤이은 드로우 앞에서 실행된다.
        const D3D11_BOX                         box{ desc._x, desc._y, 0, desc._x + desc._width, desc._y + desc._height, 1 };
        const D3D11ImmediateContext::ScopedLock context = _pDevice->_immediateContext.lock();
        Microsoft::WRL::ComPtr<ID3D11Query>     query   = _pDevice->acquireTransferQuery( context );
        if ( query == nullptr )
            return RHITransferSubmit::rejected();
        context->UpdateSubresource( pRecord->_texture.Get(), subresource, &box, desc._pData, rowBytes, uploadBytes );
        const uint64 serial = _pDevice->endTransfer( context, std::move( query ) );
        pRecord->_lastTransferSerial.store( serial );
        return _pDevice->_transferSerials.makeAccepted( serial );
    }

    RHITransferStatus D3D11RHIResourceFactory::pollTransfer( RHITransferTicket ticket )
    {
        const RHITransferStatus status = _pDevice->_transferSerials.classify( ticket );
        if ( status != RHITransferStatus::Pending || _pDevice->_immediateContext.isValid() == false )
            return status;
        // 즉시 컨텍스트에 묻는다(플래그 0 이라 명령을 GPU 로 내보낸다). 프레임을 돌리지 않아도 되풀이하면 끝에 이른다.
        {
            const D3D11ImmediateContext::ScopedLock context = _pDevice->_immediateContext.lock();
            _pDevice->retireFinishedTransfers( context );
        }
        return _pDevice->_transferSerials.classify( ticket );
    }

    bool D3D11RHIResourceFactory::takeTextureReadback( RHITransferTicket ticket, vector<uint8>& outBytes )
    {
        if ( pollTransfer( ticket ) != RHITransferStatus::Complete )
        {
            SW_LOG_ERROR( "takeTextureReadback: ticket %# has not completed", ticket._serial );
            return false;
        }
        D3D11RHIDevice::TextureReadbackRecord readback;
        if ( _pDevice->_readbackTable.take( ticket._serial, readback ) == false )
        {
            SW_LOG_ERROR( "takeTextureReadback: ticket %# is not a pending texture readback (an upload ticket, or already taken or discarded)", ticket._serial );
            return false;
        }

        // 이벤트 쿼리가 복사 뒤를 지났으므로 GPU 는 끝났다. 그래도 `D3D11_MAP_FLAG_DO_NOT_WAIT` 는 쓰지 않는다 — 런타임 · 드라이버의 자원 사용 추적이
        // 쿼리 완료보다 늦게 풀려, 끝난 복사인데도 `DXGI_ERROR_WAS_STILL_DRAWING` 이 나온다(Release · Debug 의 면 되읽기에서 열 번에 몇 번).
        // 기다리는 Map 은 그 추적만 맞추고 곧 돌아온다. 잠금은 Map 과 Unmap 호출에만 잡는다. Map 이 준 포인터는 Unmap 까지 어느 스레드에서 읽어도 되는 CPU 메모리이고, 이 스테이징은 표에서 꺼낸
        // 이 읽기만 가지므로 사이에 다른 스레드가 내는 즉시 컨텍스트 명령이 건드리지 않는다. 행 복사(4K RGBA16F 면 60 MiB 남짓)가 잠금 밖이라
        // 렌더 스레드의 ExecuteCommandList · Present 가 그 복사를 기다리지 않는다.
        D3D11_MAPPED_SUBRESOURCE mapped{};
        {
            const D3D11ImmediateContext::ScopedLock context = _pDevice->_immediateContext.lock();
            const HRESULT                           hr      = context->Map( readback._staging.Get(), 0, D3D11_MAP_READ, 0, &mapped );
            if ( FAILED( hr ) || mapped.pData == nullptr )
            {
                SW_LOG_ERROR( "takeTextureReadback: Map of the staging texture failed hr=0x%# for a completed ticket %#", static_cast<uint32>( hr ), ticket._serial );
                return false;
            }
        }
        outBytes.assign( readback._sizeBytes, 0 );
        const uint32 rowCount = readback._sizeBytes / readback._rowBytes;
        for ( uint32 row = 0; row < rowCount; ++row )
        {
            Memory::copy( outBytes.data() + static_cast<uint64>( row ) * readback._rowBytes,
                          static_cast<const uint8*>( mapped.pData ) + static_cast<uint64>( row ) * mapped.RowPitch, readback._rowBytes );
        }
        {
            const D3D11ImmediateContext::ScopedLock context = _pDevice->_immediateContext.lock();
            context->Unmap( readback._staging.Get(), 0 );
        }
        if ( readback._bDepth != SW_FALSE )
            convertRHIDepth24ToFloat( outBytes.data(), readback._width * readback._height );
        return true;
    }

    void D3D11RHIResourceFactory::discardTextureReadback( RHITransferTicket ticket )
    {
        if ( ticket._deviceGeneration != _pDevice->_transferSerials.getGeneration() )
            return;
        // 진행 중인 복사가 쓰는 스테이징은 COM 참조가 GPU 가 끝날 때까지 붙들어 준다. 표에서 빼고 참조를 놓으면 된다.
        D3D11RHIDevice::TextureReadbackRecord readback;
        (void)_pDevice->_readbackTable.take( ticket._serial, readback ); // 없으면 업로드 티켓이거나 이미 가져감 · 버림이다 — 계약상 아무 일도 하지 않는다
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

    RHITransferSubmit D3D11RHIResourceFactory::submitTextureReadback( RHITextureHandle texture, uint32 mip, uint32 arraySlice, RHITextureMipSpan& outLayout )
    {
        D3D11RHIDevice::TextureRecord* pRecord             = _pDevice->resolveTexture( texture );
        const bool                     bTextureNotReadable = pRecord == nullptr || pRecord->_texture == nullptr || _pDevice->_device == nullptr || _pDevice->_immediateContext.isValid() == false;
        if ( bTextureNotReadable || _pDevice->isDeviceLost() ) // 장치를 잃었으면 진입에서 Rejected 다(계약 2)
        {
            SW_LOG_ERROR( "submitTextureReadback: texture %# or the device is not usable (uninitialized or lost)", texture );
            return RHITransferSubmit::rejected();
        }

        D3D11_TEXTURE2D_DESC texDesc{};
        pRecord->_texture->GetDesc( &texDesc );
        if ( mip >= texDesc.MipLevels || arraySlice >= texDesc.ArraySize )
        {
            SW_LOG_ERROR( "submitTextureReadback: mip %# / slice %# is out of range (%# mips, %# slices)", mip, arraySlice, texDesc.MipLevels, texDesc.ArraySize );
            return RHITransferSubmit::rejected();
        }
        // 깊이는 typeless(R24G8)로 만들어져 있다. 같은 포맷의 스테이징으로 통째로 복사하고 가져갈 때 0..1 float32 로 바꾼다(kRHIDepthReadbackFormat).
        const bool      bDepth       = pRecord->_bDepth != SW_FALSE;
        const RHIFormat layoutFormat = bDepth ? kRHIDepthReadbackFormat : fromDxgiFormat( texDesc.Format );
        if ( computeRHITextureMipLayout( layoutFormat, texDesc.Width, texDesc.Height, mip, outLayout ) == false )
        {
            SW_LOG_ERROR( "submitTextureReadback: unsupported format for %#x%# mip %#", texDesc.Width, texDesc.Height, mip );
            return RHITransferSubmit::rejected();
        }

        // 밉 하나 크기의 스테이징 텍스처로 복사하고, 이벤트 쿼리가 그 복사 뒤를 지나면 가져갈 수 있다(`takeTextureReadback` 이 기다리는 Map 으로 읽는다).
        D3D11_TEXTURE2D_DESC stagingDesc = texDesc;
        stagingDesc.Width                = outLayout._width;
        stagingDesc.Height               = outLayout._height;
        stagingDesc.MipLevels            = 1;
        stagingDesc.ArraySize            = 1;
        stagingDesc.Usage                = D3D11_USAGE_STAGING;
        stagingDesc.BindFlags            = 0;
        stagingDesc.CPUAccessFlags       = D3D11_CPU_ACCESS_READ;
        stagingDesc.MiscFlags            = 0;
        D3D11RHIDevice::TextureReadbackRecord readback;
        if ( FAILED( _pDevice->_device->CreateTexture2D( &stagingDesc, nullptr, readback._staging.GetAddressOf() ) ) )
        {
            SW_LOG_ERROR( "submitTextureReadback: failed to create the staging texture (%#x%#)", outLayout._width, outLayout._height );
            return RHITransferSubmit::rejected();
        }
        readback._width     = outLayout._width;
        readback._height    = outLayout._height;
        readback._rowBytes  = outLayout._rowBytes;
        readback._sizeBytes = outLayout._sizeBytes;
        readback._bDepth    = bDepth ? SW_TRUE : SW_FALSE;

        const D3D11ImmediateContext::ScopedLock context = _pDevice->_immediateContext.lock();
        Microsoft::WRL::ComPtr<ID3D11Query>     query   = _pDevice->acquireTransferQuery( context );
        if ( query == nullptr )
            return RHITransferSubmit::rejected();
        context->CopySubresourceRegion( readback._staging.Get(), 0, 0, 0, 0, pRecord->_texture.Get(), D3D11CalcSubresource( mip, arraySlice, texDesc.MipLevels ), nullptr );
        const uint64 serial = _pDevice->endTransfer( context, std::move( query ) );
        _pDevice->_readbackTable.add( serial, std::move( readback ) );
        return _pDevice->_transferSerials.makeAccepted( serial );
    }

    // 길이 유지(긴 함수 정리 기준): 서술 채우기 · 리소스 생성 · 뷰의 순서가 곧 의미라 나누지 않는다.
    RHITextureHandle D3D11RHIResourceFactory::createTexture2D( const RHITextureDesc& desc )
    {
        const bool bInvalidTextureRequest = _pDevice == nullptr || desc._width == 0 || desc._height == 0;
        if ( bInvalidTextureRequest )
            return 0;
        if ( isRHITextureShapeValid( desc ) == false )
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
        _pDevice->_releaseQueue.enqueueLedgerRelease<RHIMemoryKeySpace::Texture>( owned, _pDevice->getMemoryLedger(), texture );
    }
} // namespace sw
#endif
