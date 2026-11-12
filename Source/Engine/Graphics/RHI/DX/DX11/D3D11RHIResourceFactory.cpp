#include "pch.h"

#include "Engine/Graphics/RHI/DX/DX11/D3D11RHIResourceFactory.h"

#include "Core/Concurrency/mutex.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/RHI/Backend/RHIBufferSizeUtil.h"
#include "Engine/Graphics/RHI/Backend/RHIShaderRequest.h"
#include "Engine/Graphics/RHI/DX/DX11/D3D11RHIDevice.h"
#include "Engine/Graphics/RHI/DX/DXGIFormat.h"
#include "Engine/Graphics/RHI/DX/RHIDxRenderState.h"
#include "Engine/Graphics/RHI/Diagnostics/RHIMemoryTracker.h"
#include "Engine/Graphics/RHI/RHIFormatInfo.h"
#include "Engine/Graphics/Shader/Compile/ShaderCache.h"
#include "Engine/Graphics/Shader/Compile/ShaderCompiler.h"

#if defined( SW_PLATFORM_WINDOWS )
namespace sw
{
    RHIBufferHandle D3D11RHIResourceFactory::createConstantBuffer( uint32 size )
    {
        const bool bInvalidConstantBufferRequest = _pDevice == nullptr || _pDevice->_device == nullptr || size == 0;
        if ( bInvalidConstantBufferRequest )
        {
            SW_LOG_ERROR_ID( "D3D11.CreateConstantBufferInvalidDeviceSize", "createConstantBuffer: invalid device or size=%#", size );
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
            SW_LOG_ERROR_ID( "D3D11.CreateBufferConstantFailedHr", "CreateBuffer(constant) failed hr=0x%# size=%# aligned=%#",
                             static_cast<uint32>( hr ), size, alignedSize );
            return 0;
        }

        const RHIBufferHandle handle = _pDevice->storeBuffer( std::move( buffer ) );
        if ( handle == 0 )
            SW_LOG_ERROR_ID( "D3D11.StoreBufferReturnedAfterCreateBuffer", "storeBuffer returned 0 after CreateBuffer success" );
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
        // Map 실패는 오류로 남긴다 — 조용히 건너뛰면 머티리얼 파라미터가 옛 값으로 남는다(장치 제거는 다음 Present 가 분실로 처리한다).
        if ( isHrSuccess( context->Map( pResource, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped ), "ID3D11DeviceContext::Map (constant buffer, factory)" ) )
        {
            Memory::copy( mapped.pData, pData, size );
            context->Unmap( pResource, 0 );
        }
    }

    RHIBufferHandle D3D11RHIResourceFactory::createStructuredBuffer( uint32 elementSize, uint32 elementCount )
    {
        if ( elementSize == 0 || elementCount == 0 )
            return 0;

        // 32비트 API 다. 담기지 않으면 만들지 않는다(RHIBufferSizeUtil 가 세 백엔드의 규칙 하나).
        uint32 totalBytes{ 0 };
        if ( RHIBufferSizeUtil::computeStructuredBytes( elementSize, elementCount, totalBytes ) == false )
            return 0;

        D3D11_BUFFER_DESC bufferDesc{};
        bufferDesc.Usage               = D3D11_USAGE_DEFAULT;
        bufferDesc.ByteWidth           = static_cast<UINT>( totalBytes );
        bufferDesc.BindFlags           = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
        bufferDesc.MiscFlags           = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bufferDesc.StructureByteStride = elementSize;

        Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
        if ( isHrSuccess( _pDevice->_device->CreateBuffer( &bufferDesc, nullptr, buffer.GetAddressOf() ), "CreateBuffer" ) == false )
            return 0;

        // 그래픽스 VS/PS 가 StructuredBuffer 로 읽을 수 있도록 SRV 를 만들어 둔다 (GPUScene 인스턴스 버퍼 등).
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format              = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension       = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements  = elementCount;
        return _pDevice->storeBufferWithSRV( std::move( buffer ), srvDesc );
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
        if ( isHrSuccess( _pDevice->_device->CreateBuffer( &bufferDesc, pInitData, buffer.GetAddressOf() ), "CreateBuffer" ) == false )
        {
            SW_LOG_ERROR_ID( "D3D11.CreateBufferFailedCreateIndirect", "createBuffer: failed to create the indirect argument buffer (%# bytes)", sizeBytes );
            return 0;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format                = DXGI_FORMAT_R32_TYPELESS;
        srvDesc.ViewDimension         = D3D11_SRV_DIMENSION_BUFFEREX;
        srvDesc.BufferEx.FirstElement = 0;
        srvDesc.BufferEx.NumElements  = sizeBytes / 4;
        srvDesc.BufferEx.Flags        = D3D11_BUFFEREX_SRV_FLAG_RAW;
        return _pDevice->storeBufferWithSRV( std::move( buffer ), srvDesc );
    }

    void D3D11RHIResourceFactory::updateStructuredBufferRegions( RHIBufferHandle buffer, const void* pBaseSource,
                                                                 const RHIBufferCopyRegion* pRegions, uint32 regionCount )
    {
        _pDevice->requireNoOpenRenderPassForUpload( "updateStructuredBufferRegions" );
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

    void D3D11RHIResourceFactory::updateGeometryBufferRegions( RHIBufferHandle buffer, const void* pBaseSource, const RHIBufferCopyRegion* pRegions, uint32 regionCount )
    {
        _pDevice->requireNoOpenRenderPassForUpload( "updateGeometryBufferRegions" );
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
                SW_LOG_ERROR_ID( "D3D11.UpdateGeometryBufferRegionsRegionOutsideByte", "updateGeometryBufferRegions: region [%#, +%#) is outside the %#-byte vertex or index buffer; skipped", region._dstOffset, region._sizeBytes, desc.ByteWidth );
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
        if ( isHrSuccess( _pDevice->_device->CreateBuffer( &bufferDesc, &init, buffer.GetAddressOf() ), "CreateBuffer" ) == false )
            return 0;

        return _pDevice->storeBuffer( std::move( buffer ) );
    }

    void D3D11RHIResourceFactory::destroyBuffer( RHIBufferHandle buffer )
    {
        if ( buffer == 0 )
            return;
        // 아래 `removeBufferInRecordingStates` 는 다른 스레드가 기록 중인 리스트의 상태에 쓴다. 병렬 기록 중에는 부르지 않는 것이 계약이다.
        _pDevice->requireRegistryMutableNow( "destroyBuffer" );
        _pDevice->removeBufferInRecordingStates( buffer );

        D3D11RHIDevice::BufferRecord owned;
        if ( _pDevice->_gpuBuffers.take( buffer, owned ) == false )
            return;

        UniqueLock<shared_mutex> registryLock{ _pDevice->_bindlessMutex };
        const auto               ignore = []( RHIDescriptorIndex ) {};
        _pDevice->_bufferRegistry.releaseIf( RHIBindlessReuse::Immediately, [buffer]( RHIBufferHandle entry )
        { return entry == buffer; },
                                             ignore );
        _pDevice->_uavRegistry.releaseIf( RHIBindlessReuse::Immediately, [buffer]( const D3D11RHIDevice::UAVEntry& entry )
        { return entry._sourceBuffer == buffer; },
                                          ignore );

        // 추적기는 자원을 실제로 놓을 때 줄인다(해제 요청 시점이 아니라). SRV 도 버퍼와 함께 그때 놓인다.
        _pDevice->_releaseQueue.enqueueGPUTrackedRelease<RHIMemoryKeySpace::Buffer>( owned, _pDevice->getMemoryTracker(), buffer, _pDevice->_frameFenceRing.getRecordingSerial() );
    }

    RHIBufferHandle D3D11RHIResourceFactory::createReadbackBuffer( uint32 elementSize, uint32 elementCount )
    {
        uint32     totalBytes{ 0 };
        const bool bInvalidReadbackRequest = _pDevice->_device == nullptr || elementSize == 0 || elementCount == 0 || RHIBufferSizeUtil::computeStructuredBytes( elementSize, elementCount, totalBytes ) == false;
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
        if ( isHrSuccess( _pDevice->_device->CreateBuffer( &bufferDesc, nullptr, buffer.GetAddressOf() ), "CreateBuffer" ) == false )
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
            SW_LOG_ERROR_ID( "D3D11.ReadReadbackBufferBufferNotReadback", "readReadbackBuffer: buffer %# is not a readback buffer of at least %# bytes", readback, sizeBytes );
            return false;
        }
        // Map 에서 Unmap 까지 한 덩어리로 잠근다. 부르는 쪽 계약이 "GPU 작업이 끝난 뒤" 라서(완료 콜백 안) 기다리는 Map 이다 — `DO_NOT_WAIT` 는 끝난 복사에도
        // `DXGI_ERROR_WAS_STILL_DRAWING` 을 낸다(런타임 · 드라이버의 자원 사용 추적이 쿼리 완료보다 늦게 풀린다, takeTextureReadback 과 같은 결정 5-5).
        // 기다리는 Map 은 그 추적만 맞추고 곧 돌아온다. 장치를 잃었으면 Map 이 오류로 바로 돌아와 무한히 기다리지 않는다.
        D3D11_MAPPED_SUBRESOURCE                mapped{};
        const D3D11ImmediateContext::ScopedLock context = _pDevice->_immediateContext.lock();
        if ( isHrSuccess( context->Map( pBuffer, 0, D3D11_MAP_READ, 0, &mapped ), "ID3D11DeviceContext::Map (readback buffer)" ) == false || mapped.pData == nullptr )
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
            SW_LOG_ERROR_ID( "D3D11.SubmitTextureUploadTextureDeviceContext", "submitTextureUpload: texture %# or the device context is not usable", texture );
            return RHITransferSubmit::rejected();
        }
        // 장치를 잃은 뒤에는 전송이 끝나지 않아 스테이징 예산도 돌아오지 않는다 — Busy 를 영원히 되풀이하지 않게 진입에서 거절한다(계약 2).
        if ( _pDevice->pollDeviceLost() )
        {
            SW_LOG_ERROR_ID( "D3D11.SubmitTextureUploadDeviceLost", "submitTextureUpload: the device was lost" );
            return RHITransferSubmit::rejected();
        }
        if ( pRecord->_bDepth != SW_FALSE )
        {
            SW_LOG_ERROR_ID( "D3D11.SubmitTextureUploadTextureDepthTexture", "submitTextureUpload: texture %# is a depth texture", texture );
            return RHITransferSubmit::rejected();
        }

        D3D11_TEXTURE2D_DESC texDesc{};
        pRecord->_texture->GetDesc( &texDesc );
        if ( desc._arraySlice >= texDesc.ArraySize )
        {
            SW_LOG_ERROR_ID( "D3D11.SubmitTextureUploadSliceOutRange", "submitTextureUpload: slice %# is out of range (%# slices)", desc._arraySlice, texDesc.ArraySize );
            return RHITransferSubmit::rejected();
        }

        RHITextureMipSpan arrMip[constant::render::kMaxTextureMipCount]{};
        const uint32      mipCount = resolveTextureUploadMips( desc, fromDXGIFormat( texDesc.Format ), texDesc.Width, texDesc.Height,
                                                               texDesc.MipLevels, arrMip, constant::render::kMaxTextureMipCount );
        if ( mipCount == 0 )
        {
            SW_LOG_ERROR_ID( "D3D11.SubmitTextureUploadUnsupportedFormatNot", "submitTextureUpload: unsupported format or not enough data (%# bytes for %#×%#, %# mips)",
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
            D3D11_SUBRESOURCE_DATA arrInitialData[constant::render::kMaxTextureMipCount]{};
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
        pRecord->_uploadSerials.recordWrite( serial );
        return _pDevice->_transferSerials.makeAccepted( serial );
    }

    RHITransferSubmit D3D11RHIResourceFactory::submitTextureRegionUpload( RHITextureHandle texture, const RHITextureRegionUploadDesc& desc )
    {
        D3D11RHIDevice::TextureRecord* pRecord                     = _pDevice->resolveTexture( texture );
        const bool                     bTextureRegionNotUploadable = pRecord == nullptr || pRecord->_texture == nullptr || _pDevice->_immediateContext.isValid() == false || pRecord->_bDepth != SW_FALSE;
        if ( bTextureRegionNotUploadable )
        {
            SW_LOG_ERROR_ID( "D3D11.SubmitTextureRegionUploadTextureMissingDepth", "submitTextureRegionUpload: texture %# is missing, a depth texture, or the device context is not usable", texture );
            return RHITransferSubmit::rejected();
        }
        // 장치를 잃은 뒤에는 전송이 끝나지 않아 스테이징 예산도 돌아오지 않는다 — Busy 를 영원히 되풀이하지 않게 진입에서 거절한다(계약 2).
        if ( _pDevice->pollDeviceLost() )
        {
            SW_LOG_ERROR_ID( "D3D11.SubmitTextureRegionUploadDeviceLost", "submitTextureRegionUpload: the device was lost" );
            return RHITransferSubmit::rejected();
        }

        D3D11_TEXTURE2D_DESC texDesc{};
        pRecord->_texture->GetDesc( &texDesc );
        uint32 rowBytes{ 0 };
        if ( validateTextureRegionUpload( fromDXGIFormat( texDesc.Format ), texDesc.Width, texDesc.Height, texDesc.MipLevels, texDesc.ArraySize, desc, rowBytes ) == false )
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
        pRecord->_uploadSerials.recordWrite( serial );
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
            SW_LOG_ERROR_ID( "D3D11.TakeTextureReadbackTicketNotCompleted", "takeTextureReadback: ticket %# has not completed", ticket._serial );
            return false;
        }
        D3D11RHIDevice::TextureReadbackRecord readback;
        if ( _pDevice->_readbackTable.take( ticket._serial, readback ) == false )
        {
            SW_LOG_ERROR_ID( "D3D11.TakeTextureReadbackTicketNotPending", "takeTextureReadback: ticket %# is not a pending texture readback (an upload ticket, or already taken or discarded)", ticket._serial );
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
                SW_LOG_ERROR_ID( "D3D11.TakeTextureReadbackMapStagingTexture", "takeTextureReadback: Map of the staging texture failed hr=0x%# for a completed ticket %#", static_cast<uint32>( hr ), ticket._serial );
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
        // 링 구간 꼬리(`RHITextureTransferDriver::releaseDiscardedReadback`)를 쓰지 않는 까닭: D3D11 은 텍스처를 버퍼로 복사하지 못해 되읽기마다 스테이징 텍스처다 — 다시 쓸 구간이 없다.
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
        return fromDXGIFormat( texDesc.Format );
    }

    RHITransferSubmit D3D11RHIResourceFactory::submitTextureReadback( RHITextureHandle texture, uint32 mip, uint32 arraySlice, RHITextureMipSpan& outLayout )
    {
        D3D11RHIDevice::TextureRecord* pRecord             = _pDevice->resolveTexture( texture );
        const bool                     bTextureNotReadable = pRecord == nullptr || pRecord->_texture == nullptr || _pDevice->_device == nullptr || _pDevice->_immediateContext.isValid() == false;
        if ( bTextureNotReadable || _pDevice->pollDeviceLost() ) // 장치를 잃었으면 진입에서 Rejected 다(계약 2)
        {
            SW_LOG_ERROR_ID( "D3D11.SubmitTextureReadbackTextureDeviceNot", "submitTextureReadback: texture %# or the device is not usable (uninitialized or lost)", texture );
            return RHITransferSubmit::rejected();
        }

        D3D11_TEXTURE2D_DESC texDesc{};
        pRecord->_texture->GetDesc( &texDesc );
        if ( mip >= texDesc.MipLevels || arraySlice >= texDesc.ArraySize )
        {
            SW_LOG_ERROR_ID( "D3D11.SubmitTextureReadbackMipSliceOut", "submitTextureReadback: mip %# / slice %# is out of range (%# mips, %# slices)", mip, arraySlice, texDesc.MipLevels, texDesc.ArraySize );
            return RHITransferSubmit::rejected();
        }
        // 깊이는 typeless(R24G8)로 만들어져 있다. 같은 포맷의 스테이징으로 통째로 복사하고 가져갈 때 0..1 float32 로 바꾼다(kRHIDepthReadbackFormat).
        const bool      bDepth       = pRecord->_bDepth != SW_FALSE;
        const RHIFormat layoutFormat = bDepth ? kRHIDepthReadbackFormat : fromDXGIFormat( texDesc.Format );
        if ( computeRHITextureMipLayout( layoutFormat, texDesc.Width, texDesc.Height, mip, outLayout ) == false )
        {
            SW_LOG_ERROR_ID( "D3D11.SubmitTextureReadbackUnsupportedFormatX", "submitTextureReadback: unsupported format for %#x%# mip %#", texDesc.Width, texDesc.Height, mip );
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
        if ( isHrSuccess( _pDevice->_device->CreateTexture2D( &stagingDesc, nullptr, readback._staging.GetAddressOf() ), "CreateTexture2D" ) == false )
        {
            SW_LOG_ERROR_ID( "D3D11.SubmitTextureReadbackFailedCreateStaging", "submitTextureReadback: failed to create the staging texture (%#x%#)", outLayout._width, outLayout._height );
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

    void D3D11RHIResourceFactory::setTextureDebugName( RHITextureHandle texture, const utf8* pName )
    {
        const D3D11RHIDevice::TextureRecord* pRecord = _pDevice->resolveTexture( texture );
        if ( pRecord == nullptr || pRecord->_texture == nullptr || pName == nullptr )
            return;
        // WKPDID_D3DDebugObjectName(d3dcommon.h, dxguid.lib — sw_graphics_dx11_libs 가 링크한다). 그래픽스 디버거 · 디버그 레이어 메시지가 이 이름을 보인다.
        const string_view name = pName;
        pRecord->_texture->SetPrivateData( WKPDID_D3DDebugObjectName, static_cast<UINT>( name.size() ), name.data() );
    }

    // 길이 유지(긴 함수 정리 기준): 서술 채우기 · 리소스 생성 · 뷰의 순서가 곧 의미라 나누지 않는다.
    RHITextureHandle D3D11RHIResourceFactory::createTexture2D( const RHITextureDesc& desc )
    {
        const bool bInvalidTextureRequest = _pDevice == nullptr || desc._width == 0 || desc._height == 0;
        if ( bInvalidTextureRequest )
            return 0;
        if ( isRHITextureShapeValid( desc ) == false )
        {
            SW_LOG_ERROR_ID( "D3D11.CreateTexture2DDimensionSlicesX", "createTexture2D: dimension %# with %# slices (%#x%#) is not a valid texture shape", static_cast<uint32>( desc._dimension ),
                             desc._arraySize, desc._width, desc._height );
            return 0;
        }
        // 이 장치가 샘플링하지 못하는 압축 형식(데스크톱의 ASTC · ETC2)은 드라이버에 넘기지 않는다 — 넘기면 D3D12 는 실패, Vulkan 드라이버는 0 나누기로 죽었다.
        if ( isRHIFormatBlockCompressed( desc._format ) && _pDevice->supportsTextureFormat( desc._format ) == false )
        {
            SW_LOG_ERROR_ID( "D3D11.CreateTexture2DFormatUnsupported", "createTexture2D: this device cannot sample format %# (%#x%#) - the texture is not created", static_cast<uint32>( desc._format ), desc._width, desc._height );
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
        texDesc.Format             = bDepth ? DXGI_FORMAT_R24G8_TYPELESS : toDXGIFormat( desc._format );
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

        if ( isHrSuccess( _pDevice->_device->CreateTexture2D( &texDesc, nullptr, record._texture.GetAddressOf() ), "CreateTexture2D" ) == false )
        {
            SW_LOG_ERROR_ID( "D3D11.FailedCreateTexture2D", "Failed to create Texture2D (%#×%#).", desc._width, desc._height );
            return 0;
        }

        // 면이 여럿이면 렌더 패스가 면 하나를 타깃으로 고른다 — 면마다 배열 뷰(원소 하나)를 만든다. `_rtv` · `_dsv` 는 면 0 이다.
        if ( desc._bIsRenderTarget && bDepth == false )
        {
            for ( uint32 slice = 0; slice < desc._arraySize; ++slice )
            {
                D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
                rtvDesc.Format                         = toDXGIFormat( desc._format );
                rtvDesc.ViewDimension                  = bSliced ? D3D11_RTV_DIMENSION_TEXTURE2DARRAY : D3D11_RTV_DIMENSION_TEXTURE2D;
                rtvDesc.Texture2DArray.MipSlice        = 0;
                rtvDesc.Texture2DArray.FirstArraySlice = slice;
                rtvDesc.Texture2DArray.ArraySize       = 1;
                Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv;
                if ( isHrSuccess( _pDevice->_device->CreateRenderTargetView( record._texture.Get(), &rtvDesc, rtv.GetAddressOf() ), "CreateRenderTargetView" ) == false )
                {
                    SW_LOG_ERROR_ID( "D3D11.FailedCreateRTVTexture2D", "Failed to create RTV for Texture2D (slice %#).", slice );
                    return 0;
                }
                if ( slice == 0 )
                    record._rtv = rtv;
                if ( bSliced )
                    record._listSliceRTV.push_back( std::move( rtv ) );
            }
        }

        if ( bDepth )
        {
            for ( uint32 slice = 0; slice < desc._arraySize; ++slice )
            {
                D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
                dsvDesc.Format                         = toDXGIFormat( constant::render::kDepthStencilFormat );
                dsvDesc.ViewDimension                  = bSliced ? D3D11_DSV_DIMENSION_TEXTURE2DARRAY : D3D11_DSV_DIMENSION_TEXTURE2D;
                dsvDesc.Texture2DArray.MipSlice        = 0;
                dsvDesc.Texture2DArray.FirstArraySlice = slice;
                dsvDesc.Texture2DArray.ArraySize       = 1;
                Microsoft::WRL::ComPtr<ID3D11DepthStencilView> dsv;
                if ( isHrSuccess( _pDevice->_device->CreateDepthStencilView( record._texture.Get(), &dsvDesc, dsv.GetAddressOf() ), "CreateDepthStencilView" ) == false )
                {
                    SW_LOG_ERROR_ID( "D3D11.FailedCreateDSVTexture2D", "Failed to create DSV for Texture2D (slice %#).", slice );
                    return 0;
                }
                if ( slice == 0 )
                    record._dsv = dsv;
                if ( bSliced )
                    record._listSliceDSV.push_back( std::move( dsv ) );
            }
        }

        if ( desc._bIsShaderResource )
        {
            // 밉은 텍스처가 가진 전부다(-1). 서술체의 0 은 D3D11 에서 "전체 체인" 이라 숫자로 옮기면 안 된다.
            constexpr UINT                  kAllMips = static_cast<UINT>( -1 );
            D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.Format = bDepth ? DXGI_FORMAT_R24_UNORM_X8_TYPELESS : toDXGIFormat( desc._format );
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
            if ( isHrSuccess( _pDevice->_device->CreateShaderResourceView( record._texture.Get(), &srvDesc, record._srv.GetAddressOf() ), "CreateShaderResourceView" ) == false )
            {
                SW_LOG_ERROR_ID( "D3D11.FailedCreateSRVTexture2D", "Failed to create SRV for Texture2D." );
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
            UniqueLock<shared_mutex> registryLock{ _pDevice->_bindlessMutex };
            _pDevice->_textureRegistry.releaseIf( RHIBindlessReuse::Immediately, [texture]( RHITextureHandle entry )
            { return entry == texture; },
                                                  []( RHIDescriptorIndex ) {} );
        }

        D3D11RHIDevice::TextureRecord owned;
        if ( _pDevice->_gpuTextures.take( texture, owned ) == false )
            return;

        // 추적기는 자원을 실제로 놓을 때 줄인다(해제 요청 시점이 아니라).
        _pDevice->_releaseQueue.enqueueGPUTrackedRelease<RHIMemoryKeySpace::Texture>( owned, _pDevice->getMemoryTracker(), texture, _pDevice->_frameFenceRing.getRecordingSerial() );
    }

    bool D3D11RHIResourceFactory::isTextureReadyToSample( RHITextureHandle texture )
    {
        const D3D11RHIDevice::TextureRecord* pRecord = _pDevice->resolveTexture( texture );
        return pRecord != nullptr && _pDevice->_transferSerials.isFirstUploadComplete( pRecord->_uploadSerials );
    }

    RHIDescriptorIndex D3D11RHIResourceFactory::registerBindlessTexture( RHITextureHandle texture )
    {
        _pDevice->requireRegistryMutableNow( "registerBindlessTexture" );
        if ( texture == 0 )
            return kInvalidDescriptorIndex;

        D3D11RHIDevice::TextureRecord* pRecord = _pDevice->resolveTexture( texture );
        if ( pRecord == nullptr || pRecord->_srv == nullptr )
            return kInvalidDescriptorIndex;
        // 끝나지 않은 업로드가 남은 텍스처는 등록하지 않는다(소비자는 pollTransfer 가 Complete 를 준 뒤에 등록한다 — IRHIResourceFactory 전송 계약 머리말).
        if ( _pDevice->_transferSerials.isTextureReadyToRegister( pRecord->_uploadSerials, texture ) == false )
            return kInvalidDescriptorIndex;
        // 셰이더의 텍스처 슬롯은 Texture2D 로 선언돼 있다. 배열이나 큐브 뷰를 바인딩하면 셰이더가 읽지 못한다(RHITextureDimension 참고).
        if ( pRecord->_dimension != RHITextureDimension::Texture2D )
        {
            SW_LOG_ERROR_ID( "D3D11.RegisterBindlessTextureTextureTextureOnly", "registerBindlessTexture: texture %# is a %# texture — only Texture2D can be bound to shader texture slots yet", texture,
                             static_cast<uint32>( pRecord->_dimension ) );
            return kInvalidDescriptorIndex;
        }

        UniqueLock<shared_mutex> lock{ _pDevice->_bindlessMutex };
        return _pDevice->_textureRegistry.allocate( texture );
    }

    RHIDescriptorIndex D3D11RHIResourceFactory::registerBindlessBufferSRV( RHIBufferHandle buffer )
    {
        _pDevice->requireRegistryMutableNow( "registerBindlessBufferSRV" );
        if ( buffer == 0 )
            return kInvalidDescriptorIndex;

        ID3D11Buffer* pResource = _pDevice->resolveBuffer( buffer );
        if ( pResource == nullptr )
            return kInvalidDescriptorIndex;
        UniqueLock<shared_mutex> lock{ _pDevice->_bindlessMutex };
        return _pDevice->_bufferRegistry.allocate( buffer );
    }

    void D3D11RHIResourceFactory::unregisterBindlessResource( RHIDescriptorIndex index )
    {
        _pDevice->requireRegistryMutableNow( "unregisterBindlessResource" );
        UniqueLock<shared_mutex> lock{ _pDevice->_bindlessMutex };
        // 이중 해제 · 다른 공간 번호는 `RHIBindlessRegistry::release` 가 거절한다.
        (void)_pDevice->_bufferRegistry.release( index, RHIBindlessReuse::Immediately );
    }

    void D3D11RHIResourceFactory::unregisterBindlessTexture( RHIDescriptorIndex index )
    {
        _pDevice->requireRegistryMutableNow( "unregisterBindlessTexture" );
        UniqueLock<shared_mutex> lock{ _pDevice->_bindlessMutex };
        (void)_pDevice->_textureRegistry.release( index, RHIBindlessReuse::Immediately );
    }

    RHIDescriptorIndex D3D11RHIResourceFactory::registerBindlessBufferUAV( RHIBufferHandle buffer )
    {
        _pDevice->requireRegistryMutableNow( "registerBindlessBufferUAV" );
        if ( buffer == 0 )
            return kInvalidDescriptorIndex;
        ID3D11Buffer* pResource = _pDevice->resolveBuffer( buffer );
        if ( pResource == nullptr )
            return kInvalidDescriptorIndex;

        D3D11_BUFFER_DESC desc;
        pResource->GetDesc( &desc );

        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
        uavDesc.ViewDimension       = D3D11_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.FirstElement = 0;
        if ( ( desc.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS ) != 0 )
        {
            uavDesc.Format             = DXGI_FORMAT_R32_TYPELESS;
            uavDesc.Buffer.NumElements = desc.ByteWidth / 4;
            uavDesc.Buffer.Flags       = D3D11_BUFFER_UAV_FLAG_RAW;
        }
        else
        {
            if ( desc.StructureByteStride == 0 )
                return kInvalidDescriptorIndex;
            uavDesc.Format             = DXGI_FORMAT_UNKNOWN;
            uavDesc.Buffer.NumElements = desc.ByteWidth / desc.StructureByteStride;
        }

        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> uav;
        if ( isHrSuccess( _pDevice->_device->CreateUnorderedAccessView( pResource, &uavDesc, uav.GetAddressOf() ), "CreateUnorderedAccessView" ) == false )
            return kInvalidDescriptorIndex;

        return registerUAVView( uav.Get(), buffer );
    }

    RHIDescriptorIndex D3D11RHIResourceFactory::registerBindlessTextureUAV( RHITextureHandle texture )
    {
        _pDevice->requireRegistryMutableNow( "registerBindlessTextureUAV" );
        if ( texture == 0 )
            return kInvalidDescriptorIndex;
        D3D11RHIDevice::TextureRecord* pRecord = _pDevice->resolveTexture( texture );
        if ( pRecord == nullptr || pRecord->_texture == nullptr )
            return kInvalidDescriptorIndex;

        // 텍스처 UAV: bindComputeUAV( index, kComputeTextureUAV0 + 서수 ) 가 CSSetUnorderedAccessViews 로 바인딩한다.
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> uav;
        if ( isHrSuccess( _pDevice->_device->CreateUnorderedAccessView( pRecord->_texture.Get(), nullptr, uav.GetAddressOf() ), "CreateUnorderedAccessView" ) == false )
            return kInvalidDescriptorIndex;

        return registerUAVView( uav.Get(), RHIBufferHandle{ 0 } );
    }

    RHIDescriptorIndex D3D11RHIResourceFactory::registerUAVView( ID3D11UnorderedAccessView* pUAV, RHIBufferHandle sourceBuffer )
    {
        UniqueLock<shared_mutex> lock{ _pDevice->_bindlessMutex };
        D3D11RHIDevice::UAVEntry entry{};
        entry._uav          = pUAV;
        entry._sourceBuffer = sourceBuffer;
        return _pDevice->_uavRegistry.allocate( entry );
    }

    void D3D11RHIResourceFactory::unregisterBindlessUAV( RHIDescriptorIndex index )
    {
        _pDevice->requireRegistryMutableNow( "unregisterBindlessUAV" );
        UniqueLock<shared_mutex> lock{ _pDevice->_bindlessMutex };
        (void)_pDevice->_uavRegistry.release( index, RHIBindlessReuse::Immediately );
    }

    RHIPipelineStateHandle D3D11RHIResourceFactory::createPipelineStateNative( const RHIPipelineStateDesc& desc )
    {
        // 디스크립터 해석(진입점 기본값, define, 뎁스 전용 판정)은 RHIShaderRequest 하나가 한다. 백엔드는 받기만 한다.
        // (그래픽스 요청에는 컴퓨트 스테이지가 없다. 컴퓨트 경로는 아래에서 define 을 직접 푼다.)
        const RHIGraphicsShaderRequest request = RHIShaderRequest::resolveGraphics( desc, ShaderTargetFormat::DXBC_D3D11 );

        D3D11RHIDevice::D3D11PipelineStateRecord pso{};
        if ( desc._vertexShaderPath.empty() == false )
        {
            // 셰이더를 만들지 못하면 PSO 도 만들지 않는다(핸들 0). 다른 세 백엔드와 같은 규칙이다. 주의: 실패를 그릴 때 "PS 가 없으면
            // 건너뛴다" 로 삼키면 **원래 PS 가 없는** 깊이 전용 PSO(그림자, 깊이 프리패스)의 드로우까지 버려진다(bindGraphicsPipelineForDraw
            // 참고). 뎁스 전용(RT 0 개)이면 경로가 있어도 PS 를 붙이지 않는다.
            ShaderCompileResult vsResult{};
            ShaderCompileResult psResult{};
            const bool          bShaderCreationFailed = RHIShaderRequest::compileGraphics( request, vsResult, psResult ) == false ||
                                               isHrSuccess( _pDevice->_device->CreateVertexShader( vsResult._bytecode.data(), vsResult._bytecode.size(), nullptr, pso._vs.GetAddressOf() ), "CreateVertexShader" ) == false ||
                                               ( request._bHasPixelShader != SW_FALSE &&
                                                 isHrSuccess( _pDevice->_device->CreatePixelShader( psResult._bytecode.data(), psResult._bytecode.size(), nullptr, pso._ps.GetAddressOf() ), "CreatePixelShader" ) == false );
            if ( bShaderCreationFailed )
            {
                SW_LOG_WARNING_ID( "D3D11.CreatePipelineStateShaderCompileCreate", "createPipelineState: shader compile/create failed (vs=%# ps=%#) %#", vsResult._bSuccess, psResult._bSuccess,
                                   desc._vertexShaderPath.c_str() );
                return 0;
            }
            ShaderCompileResult gsResult{};
            ShaderCompileResult hsResult{};
            ShaderCompileResult dsResult{};
            const bool          bOptionalFailed =
                RHIShaderRequest::compileOptionalStages( request, gsResult, hsResult, dsResult ) == false ||
                ( request._bHasGeometryShader != SW_FALSE && isHrSuccess( _pDevice->_device->CreateGeometryShader( gsResult._bytecode.data(), gsResult._bytecode.size(), nullptr, pso._gs.GetAddressOf() ), "CreateGeometryShader" ) == false ) ||
                ( request._bHasTessellation != SW_FALSE && ( isHrSuccess( _pDevice->_device->CreateHullShader( hsResult._bytecode.data(), hsResult._bytecode.size(), nullptr, pso._hs.GetAddressOf() ), "CreateHullShader" ) == false ||
                                                             isHrSuccess( _pDevice->_device->CreateDomainShader( dsResult._bytecode.data(), dsResult._bytecode.size(), nullptr, pso._ds.GetAddressOf() ), "CreateDomainShader" ) == false ) );
            if ( bOptionalFailed )
            {
                SW_LOG_ERROR_ID( "D3D11.CreatePipelineStateOptionalStageFailed", "createPipelineState: optional stage compile/create failed (%#)", desc._vertexShaderPath.c_str() );
                return 0;
            }
            pso._topology = request._bHasTessellation != SW_FALSE ? D3D11_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST : D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
            // 입력 레이아웃은 메시 정점 형식의 공용 테이블에서 셰이더가 읽는 속성만 골라 만든다(`fillDXGIInputElements` — DX12 와 같은 함수, Vulkan · GL 도 같은 규칙).
            // 정점 입력이 없는 셰이더(SV_VertexID 로 정점을 만든다)는 빈 레이아웃이라 정점 버퍼 없이 그린다.
            const uint32             consumedMask = RHIShaderRequest::computeConsumedVertexAttributeMask( vsResult._bytecode, ShaderTargetFormat::DXBC_D3D11 );
            D3D11_INPUT_ELEMENT_DESC arrInputElement[constant::render::kVertexAttributeCount]{};
            const uint32             elementCount = fillDXGIInputElements<D3D11_INPUT_PER_VERTEX_DATA, D3D11_INPUT_PER_INSTANCE_DATA>( desc._meshVertexFormat, consumedMask, arrInputElement );
            pso._vertexLayout                     = RHIVertexLayout{ RHIShaderRequest::toVertexSlotMask( consumedMask ), desc._meshVertexFormat };
            if ( elementCount > 0 &&
                 isHrSuccess( _pDevice->_device->CreateInputLayout( arrInputElement, elementCount, vsResult._bytecode.data(), vsResult._bytecode.size(), pso._inputLayout.GetAddressOf() ), "CreateInputLayout" ) == false )
            {
                SW_LOG_ERROR_ID( "D3D11.CreatePipelineStateCreateInputLayoutFailed", "createPipelineState: CreateInputLayout failed '%#'", desc._vertexShaderPath.c_str() );
                return 0;
            }
        }
        if ( desc._computeShaderPath.empty() == false )
        {
            const ShaderCompileDesc csDesc = RHIShaderRequest::resolveCompute( desc._computeShaderPath, desc._computeEntryPoint, desc._listShaderDefine, ShaderTargetFormat::DXBC_D3D11 );
            ShaderCompileResult     res    = RHIShaderRequest::compile( csDesc );
            if ( res._bSuccess == false || isHrSuccess( _pDevice->_device->CreateComputeShader( res._bytecode.data(), res._bytecode.size(), nullptr, pso._cs.GetAddressOf() ), "CreateComputeShader" ) == false )
            {
                SW_LOG_ERROR_ID( "D3D11.CreatePipelineStateComputeShaderCompile", "createPipelineState: compute shader compile/create failed '%#'", desc._computeShaderPath.c_str() );
                return 0;
            }
        }

        const RenderState&    renderState = desc._renderState;
        D3D11_RASTERIZER_DESC rasterizerDesc{};
        rasterizerDesc.FillMode              = ( renderState._fillMode == FillMode::Wireframe ) ? D3D11_FILL_WIREFRAME : D3D11_FILL_SOLID;
        rasterizerDesc.CullMode              = ( renderState._cullMode == CullMode::Front ) ? D3D11_CULL_FRONT : ( ( renderState._cullMode == CullMode::Back ) ? D3D11_CULL_BACK : D3D11_CULL_NONE );
        rasterizerDesc.FrontCounterClockwise = renderState._frontFace == FrontFace::CounterClockwise ? TRUE : FALSE;
        rasterizerDesc.DepthClipEnable       = renderState._bEnableDepthClip != SW_FALSE ? TRUE : FALSE;
        // 깊이 바이어스 — 다른 백엔드와 같은 상수 · 기울기 두 칸이다(FixedRenderState::_depthBias).
        rasterizerDesc.DepthBias            = renderState._depthBias;
        rasterizerDesc.SlopeScaledDepthBias = renderState._slopeScaledDepthBias;
        // 가위는 항상 켠다. 뷰포트를 바인딩하는 곳(setViewport, beginRenderPass, beginFrame)이 가위도 뷰포트 전체로 함께 바인딩하므로
        // setScissorRect 를 부르지 않은 드로우는 지금과 같다(언리얼 D3D11 래스터라이저 상태와 같은 방식).
        rasterizerDesc.ScissorEnable = TRUE;
        // 상태 객체를 못 만들면 PSO 를 만들지 않는다(핸들 0). 빈 상태로 등록하면 앞 드로우의 래스터 · 블렌드 · 깊이 상태가 남아 그 상태로 그린다.
        if ( isHrSuccess( _pDevice->_device->CreateRasterizerState( &rasterizerDesc, pso._rasterizerState.GetAddressOf() ), "CreateRasterizerState" ) == false )
        {
            SW_LOG_ERROR_ID( "D3D11.CreatePipelineStateCreateRasterizerStateFailed", "createPipelineState: CreateRasterizerState failed '%#'", desc._vertexShaderPath.c_str() );
            return 0;
        }

        {
            // 렌더 타깃마다 따로 건다. 꺼 두면 0 번 값이 모든 타깃에 간다.
            D3D11_BLEND_DESC blendDesc{};
            blendDesc.IndependentBlendEnable = TRUE;
            const BlendTargetState unusedTarget{};
            for ( uint32 targetIndex = 0; targetIndex < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++targetIndex )
            {
                // 쓰지 않는 칸도 유효한 계수여야 한다(0 은 D3D11_BLEND 값이 아니다).
                const BlendTargetState&         source = targetIndex < kMaxColorAttachments ? renderState._arrBlendTarget[targetIndex] : unusedTarget;
                D3D11_RENDER_TARGET_BLEND_DESC& target = blendDesc.RenderTarget[targetIndex];
                target.RenderTargetWriteMask           = static_cast<UINT8>( source._colorWriteMask ); // 비트가 같다(ColorWriteMask)
                target.BlendEnable                     = source._bEnableBlend != SW_FALSE ? TRUE : FALSE;
                target.SrcBlend                        = RHIDxRenderState::toBlend<D3D11_BLEND>( source._srcColor );
                target.DestBlend                       = RHIDxRenderState::toBlend<D3D11_BLEND>( source._dstColor );
                target.BlendOp                         = RHIDxRenderState::toBlendOp<D3D11_BLEND_OP>( source._colorOp );
                target.SrcBlendAlpha                   = RHIDxRenderState::toBlend<D3D11_BLEND>( source._srcAlpha );
                target.DestBlendAlpha                  = RHIDxRenderState::toBlend<D3D11_BLEND>( source._dstAlpha );
                target.BlendOpAlpha                    = RHIDxRenderState::toBlendOp<D3D11_BLEND_OP>( source._alphaOp );
            }
            D3D11_DEPTH_STENCIL_DESC dsDesc{};
            dsDesc.DepthEnable                  = ( renderState._bEnableDepthTest != SW_FALSE ) ? TRUE : FALSE;
            dsDesc.DepthWriteMask               = ( renderState._bEnableDepthWrite != SW_FALSE ) ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
            dsDesc.DepthFunc                    = RHIDxRenderState::toCompare<D3D11_COMPARISON_FUNC>( renderState._depthCompare );
            dsDesc.StencilEnable                = renderState._bEnableStencil != SW_FALSE ? TRUE : FALSE;
            dsDesc.StencilReadMask              = renderState._stencilReadMask;
            dsDesc.StencilWriteMask             = renderState._stencilWriteMask;
            dsDesc.FrontFace.StencilFailOp      = RHIDxRenderState::toStencilOp<D3D11_STENCIL_OP>( renderState._stencilFrontFail );
            dsDesc.FrontFace.StencilDepthFailOp = RHIDxRenderState::toStencilOp<D3D11_STENCIL_OP>( renderState._stencilFrontDepthFail );
            dsDesc.FrontFace.StencilPassOp      = RHIDxRenderState::toStencilOp<D3D11_STENCIL_OP>( renderState._stencilFrontPass );
            dsDesc.FrontFace.StencilFunc        = RHIDxRenderState::toCompare<D3D11_COMPARISON_FUNC>( renderState._stencilFrontCompare );
            dsDesc.BackFace.StencilFailOp       = RHIDxRenderState::toStencilOp<D3D11_STENCIL_OP>( renderState._stencilBackFail );
            dsDesc.BackFace.StencilDepthFailOp  = RHIDxRenderState::toStencilOp<D3D11_STENCIL_OP>( renderState._stencilBackDepthFail );
            dsDesc.BackFace.StencilPassOp       = RHIDxRenderState::toStencilOp<D3D11_STENCIL_OP>( renderState._stencilBackPass );
            dsDesc.BackFace.StencilFunc         = RHIDxRenderState::toCompare<D3D11_COMPARISON_FUNC>( renderState._stencilBackCompare );
            pso._stencilRef                     = renderState._stencilRef;
            const bool bStateFailed             = isHrSuccess( _pDevice->_device->CreateBlendState( &blendDesc, pso._blendState.GetAddressOf() ), "CreateBlendState" ) == false ||
                                      isHrSuccess( _pDevice->_device->CreateDepthStencilState( &dsDesc, pso._depthStencilState.GetAddressOf() ), "CreateDepthStencilState" ) == false;
            if ( bStateFailed )
            {
                SW_LOG_ERROR_ID( "D3D11.CreatePipelineStateCreateBlendStateCreateDepthStencilStateFailed", "createPipelineState: CreateBlendState/CreateDepthStencilState failed '%#'", desc._vertexShaderPath.c_str() );
                return 0;
            }
        }

        return _pDevice->_pipelineStates.insert( std::move( pso ) );
    }

    RHIPipelineStateHandle D3D11RHIResourceFactory::createComputePipelineStateNative( string_view shaderPath, string_view entryPoint, const vector<string>& listDefine )
    {
        D3D11RHIDevice::D3D11PipelineStateRecord pso{};
        if ( shaderPath.empty() ) // 빈 레코드를 등록하지 않는다(다른 백엔드와 같다)
            return 0;
        {
            const ShaderCompileDesc csDesc = RHIShaderRequest::resolveCompute( shaderPath, entryPoint, listDefine, ShaderTargetFormat::DXBC_D3D11 );
            ShaderCompileResult     res    = RHIShaderRequest::compile( csDesc );
            if ( res._bSuccess == false || isHrSuccess( _pDevice->_device->CreateComputeShader( res._bytecode.data(), res._bytecode.size(), nullptr, pso._cs.GetAddressOf() ), "CreateComputeShader" ) == false )
                return 0;
        }
        return _pDevice->_pipelineStates.insert( std::move( pso ) );
    }

    void D3D11RHIResourceFactory::destroyPipelineState( RHIPipelineStateHandle pso )
    {
        if ( pso == 0 )
            return;
        // 아래 `removePipelineStateInRecordingStates` 는 다른 스레드가 기록 중인 리스트의 상태에 쓴다. 병렬 기록 중에는 부르지 않는 것이 계약이다.
        _pDevice->requireRegistryMutableNow( "destroyPipelineState" );
        _pDevice->_pipelineStates.erase( pso );
        _pDevice->removePipelineStateInRecordingStates( pso );
    }

    RHIRenderPassHandle D3D11RHIResourceFactory::createRenderPass( const RHIRenderPassDesc& desc )
    {
        return _pDevice->_renderPassRegistry.add( desc );
    }

    void D3D11RHIResourceFactory::destroyRenderPass( RHIRenderPassHandle pass )
    {
        _pDevice->_renderPassRegistry.retire( pass );
    }
} // namespace sw
#endif
