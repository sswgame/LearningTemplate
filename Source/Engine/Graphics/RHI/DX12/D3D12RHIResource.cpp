#include "pch.h"

#include "Engine/Graphics/RHI/DX12/D3D12RHIResource.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Common/EnginePlatformHeaders.h"
#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/RHI/DX/RHIDxgiFormat.h"
#include "Engine/Graphics/RHI/DX12/D3D12RHIDevice.h"
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
    SW_LOG_CALLER( "D3D12RHIResource" );

    RHIBufferHandle D3D12RHIResource::createConstantBuffer( uint32 size )
    {
        const UINT            alignedSize = MathUtil::align( size, constant::kConstantBufferAlignment );
        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC resDesc{};
        resDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        resDesc.Width            = static_cast<UINT64>( alignedSize ) * constant::kMaxFrameCountInFlight;
        resDesc.Height           = 1;
        resDesc.DepthOrArraySize = 1;
        resDesc.MipLevels        = 1;
        resDesc.Format           = DXGI_FORMAT_UNKNOWN;
        resDesc.SampleDesc.Count = 1;
        resDesc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
        if ( FAILED( _pDevice->_device->CreateCommittedResource( &heapProps, D3D12_HEAP_FLAG_NONE, &resDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS( buffer.GetAddressOf() ) ) ) )
            return 0;

        void* pMapped{ nullptr };
        if ( FAILED( buffer->Map( 0, nullptr, &pMapped ) ) || pMapped == nullptr )
            return 0;

        const RHIBufferHandle handle        = _pDevice->storeBuffer( buffer );
        _pDevice->_mapCbAlignedSize[handle] = alignedSize;
        _pDevice->_mapCbMapped[handle]      = pMapped;
        return handle;
    }

    void D3D12RHIResource::updateConstantBuffer( RHIBufferHandle buffer, const void* pData, uint32 size )
    {
        if ( buffer == 0 || pData == nullptr )
            return;

        // CBV SizeInBytes must be a multiple of 256 (D3D12 requirement).
        uint32     alignedSize = MathUtil::align( size, constant::kConstantBufferAlignment );
        const auto sizeIt      = _pDevice->_mapCbAlignedSize.find( buffer );
        if ( sizeIt != _pDevice->_mapCbAlignedSize.end() )
            alignedSize = sizeIt->second;
        const uint32 slot   = _pDevice->_frameRing.currentIndex();
        const uint32 offset = slot * alignedSize;

        const auto mapIt = _pDevice->_mapCbMapped.find( buffer );
        if ( mapIt == _pDevice->_mapCbMapped.end() || mapIt->second == nullptr )
            return;
        Memory::copy( static_cast<uint8*>( mapIt->second ) + offset, pData, size );

        // 드로우마다 도는 경로다. 레지스트리는 기록 중 불변이므로 락을 걸지 않는다
        // (IRHIDevice::setParallelRecording 참고). 여러 스레드가 동시에 순회하므로 const 로 받아야
        // 한다 — 비-const 순회는 읽기여도 "쓰기" 로 취급되어 레이스로 잡힌다.
        const vector<D3D12RHIDevice::BindlessResourceRecord>& listBindless = _pDevice->_listRegisteredBindless;
        for ( const D3D12RHIDevice::BindlessResourceRecord& rec : listBindless )
        {
            if ( rec._buffer != buffer || rec._resource == nullptr )
                continue;
            D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc{};
            cbvDesc.BufferLocation = rec._resource->GetGPUVirtualAddress() + offset;
            cbvDesc.SizeInBytes    = alignedSize;
            _pDevice->_device->CreateConstantBufferView( &cbvDesc, rec._cpuHandle );
        }
    }

    RHIBufferHandle D3D12RHIResource::createStructuredBuffer( uint32 elementSize, uint32 elementCount )
    {
        UINT                  alignedSize = elementSize * elementCount;
        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC resDesc{};
        resDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        resDesc.Width            = alignedSize;
        resDesc.Height           = 1;
        resDesc.DepthOrArraySize = 1;
        resDesc.MipLevels        = 1;
        resDesc.Format           = DXGI_FORMAT_UNKNOWN;
        resDesc.SampleDesc.Count = 1;
        resDesc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        resDesc.Flags            = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
        if ( FAILED( _pDevice->_device->CreateCommittedResource( &heapProps, D3D12_HEAP_FLAG_NONE, &resDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS( buffer.GetAddressOf() ) ) ) )
            return 0;

        const RHIBufferHandle handle = _pDevice->storeBuffer( buffer );
        {
            std::scoped_lock<mutex> lock{ _pDevice->_resourceStateMutex };
            _pDevice->_mapStructuredBufferState[handle] = D3D12_RESOURCE_STATE_COMMON;
        }
        _pDevice->_mapStructuredStride[handle] = elementSize > 0 ? elementSize : 4u;
        return handle;
    }

    bool D3D12RHIResource::acquireUploadStaging( uint64 sizeBytes, uint64 alignment, uint32& outSlotIndex, uint64& outOffset, void*& pOutMapped )
    {
        if ( sizeBytes == 0 || _pDevice->_device == nullptr || _pDevice->_commandQueue == nullptr )
            return false;

        // 프레임 링 슬롯 하나를 재사용한다(매 호출마다 업로드 힙/얼로케이터/리스트를 새로 만들지 않음).
        // 이 슬롯을 다시 쓸 차례가 됐다는 건 waitForRingSlot()이 이미 constant::kMaxFrameCountInFlight 프레임 전 제출의
        // GPU 완료를 보장했다는 뜻이라 별도 대기(waitForPreviousFrame) 없이 안전하다 — **프레임 사이에는**.
        // 같은 프레임 안의 두 번째 호출은 첫 번째 복사가 GPU 에서 아직 도는 중일 수 있으므로, 펜스 구간이
        // 바뀌었을 때만 얼로케이터를 Reset 하고 스테이징은 오프셋을 이어 쓴다.
        const uint32                          slotIndex = _pDevice->_frameRing.currentIndex();
        D3D12RHIDevice::StructuredUploadSlot& slot      = _pDevice->_arrStructuredUploadSlot[slotIndex];

        const bool bNewFencePeriod = ( slot._resetFence != _pDevice->_fenceValue );
        if ( bNewFencePeriod )
            slot._uploadOffset = 0;

        uint64 stagingOffset = MathUtil::align( slot._uploadOffset, alignment );
        if ( slot._uploadHeap == nullptr || slot._capacity < stagingOffset + sizeBytes )
        {
            const uint64 newCapacity = MathUtil::align( ( stagingOffset + sizeBytes ) * 2, 65536ull );

            // 옛 힙은 이번 구간의 앞선 복사가 아직 읽고 있을 수 있다 — 펜스 뒤에 놓아준다.
            if ( slot._uploadHeap != nullptr )
            {
                Microsoft::WRL::ComPtr<ID3D12Resource> oldHeap = slot._uploadHeap;
                _pDevice->_releaseQueue.enqueueGpuRelease( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, [oldHeap]()
                { (void)oldHeap.Get(); } ),
                                                           _pDevice->_fenceValue );
                slot._uploadHeap   = nullptr;
                slot._pMapped      = nullptr;
                slot._uploadOffset = 0;
                stagingOffset      = 0;
            }

            D3D12_HEAP_PROPERTIES uploadHeap{};
            uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

            D3D12_RESOURCE_DESC uploadDesc{};
            uploadDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
            uploadDesc.Width            = newCapacity;
            uploadDesc.Height           = 1;
            uploadDesc.DepthOrArraySize = 1;
            uploadDesc.MipLevels        = 1;
            uploadDesc.Format           = DXGI_FORMAT_UNKNOWN;
            uploadDesc.SampleDesc.Count = 1;
            uploadDesc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

            Microsoft::WRL::ComPtr<ID3D12Resource> newHeap;
            if ( FAILED( _pDevice->_device->CreateCommittedResource( &uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc,
                                                                     D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS( newHeap.GetAddressOf() ) ) ) )
            {
                SW_LOG_ERROR( "acquireUploadStaging: failed to (re)create staging upload buffer (%# bytes)", newCapacity );
                return false;
            }

            void* pMapped{ nullptr };
            if ( FAILED( newHeap->Map( 0, nullptr, &pMapped ) ) || pMapped == nullptr )
            {
                SW_LOG_ERROR( "acquireUploadStaging: Map failed on staging buffer" );
                return false;
            }

            slot._uploadHeap = newHeap;
            slot._pMapped    = pMapped;
            slot._capacity   = newCapacity;
        }

        if ( slot._copyAllocator == nullptr )
        {
            if ( FAILED( _pDevice->_device->CreateCommandAllocator( D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS( slot._copyAllocator.GetAddressOf() ) ) ) )
            {
                SW_LOG_ERROR( "acquireUploadStaging: failed to create copy command allocator" );
                return false;
            }
            utf16 arrName[64]{};
            swprintf_s( arrName, L"StructuredUploadAllocator%u", slotIndex );
            slot._copyAllocator->SetName( arrName );
        }
        else if ( bNewFencePeriod && FAILED( slot._copyAllocator->Reset() ) )
        {
            SW_LOG_ERROR( "acquireUploadStaging: copy allocator Reset failed" );
            return false;
        }
        slot._resetFence = _pDevice->_fenceValue;

        if ( slot._copyCommandList == nullptr )
        {
            if ( FAILED( _pDevice->_device->CreateCommandList( 0, D3D12_COMMAND_LIST_TYPE_DIRECT, slot._copyAllocator.Get(), nullptr, IID_PPV_ARGS( slot._copyCommandList.GetAddressOf() ) ) ) )
            {
                SW_LOG_ERROR( "acquireUploadStaging: failed to create copy command list" );
                return false;
            }
        }
        else if ( FAILED( slot._copyCommandList->Reset( slot._copyAllocator.Get(), nullptr ) ) )
        {
            SW_LOG_ERROR( "acquireUploadStaging: copy command list Reset failed" );
            return false;
        }

        slot._uploadOffset = stagingOffset + sizeBytes;
        outSlotIndex       = slotIndex;
        outOffset          = stagingOffset;
        pOutMapped         = slot._pMapped;
        return true;
    }

    void D3D12RHIResource::submitUploadSlot( uint32 slotIndex )
    {
        D3D12RHIDevice::StructuredUploadSlot& slot = _pDevice->_arrStructuredUploadSlot[slotIndex];
        if ( slot._copyCommandList == nullptr )
            return;
        slot._copyCommandList->Close();
        ID3D12CommandList* arrList[] = { slot._copyCommandList.Get() };
        _pDevice->_commandQueue->ExecuteCommandLists( 1, arrList );
    }

    void D3D12RHIResource::updateStructuredBuffer( RHIBufferHandle buffer, const void* pData, uint32 size )
    {
        if ( buffer == 0 || pData == nullptr || size == 0 || _pDevice->_device == nullptr || _pDevice->_commandQueue == nullptr )
            return;

        ID3D12Resource* pDest = _pDevice->resolveBuffer( buffer );
        if ( pDest == nullptr )
            return;

        uint32 slotIndex{ 0 };
        uint64 stagingOffset{ 0 };
        void*  pMapped{ nullptr };
        if ( acquireUploadStaging( size, constant::kConstantBufferAlignment, slotIndex, stagingOffset, pMapped ) == false )
            return;
        Memory::copy( static_cast<uint8*>( pMapped ) + stagingOffset, pData, size );

        D3D12RHIDevice::StructuredUploadSlot& slot  = _pDevice->_arrStructuredUploadSlot[slotIndex];
        ID3D12GraphicsCommandList*            pList = slot._copyCommandList.Get();

        D3D12_RESOURCE_STATES stateBefore = D3D12_RESOURCE_STATE_COMMON;
        {
            std::scoped_lock<mutex>                                                     lock{ _pDevice->_resourceStateMutex };
            const unordered_map<RHIBufferHandle, D3D12_RESOURCE_STATES>::const_iterator stateIt = _pDevice->_mapStructuredBufferState.find( buffer );
            if ( stateIt != _pDevice->_mapStructuredBufferState.end() )
                stateBefore = stateIt->second;
        }

        if ( stateBefore != D3D12_RESOURCE_STATE_COPY_DEST )
        {
            D3D12_RESOURCE_BARRIER toCopyDest{};
            toCopyDest.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            toCopyDest.Transition.pResource   = pDest;
            toCopyDest.Transition.StateBefore = stateBefore;
            toCopyDest.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_DEST;
            toCopyDest.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            pList->ResourceBarrier( 1, &toCopyDest );
        }

        pList->CopyBufferRegion( pDest, 0, slot._uploadHeap.Get(), stagingOffset, size );

        D3D12_RESOURCE_BARRIER toUav{};
        toUav.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        toUav.Transition.pResource   = pDest;
        toUav.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        toUav.Transition.StateAfter  = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        toUav.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        pList->ResourceBarrier( 1, &toUav );

        submitUploadSlot( slotIndex );

        {
            std::scoped_lock<mutex> lock{ _pDevice->_resourceStateMutex };
            _pDevice->_mapStructuredBufferState[buffer] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        }
    }

    bool D3D12RHIResource::uploadTexture2D( RHITextureHandle texture, const RHITextureUploadDesc& desc )
    {
        ID3D12Resource* pTexture = _pDevice->resolveTexture( texture );
        if ( pTexture == nullptr || _pDevice->_device == nullptr || _pDevice->_commandQueue == nullptr )
            return false;

        const D3D12_RESOURCE_DESC resDesc = pTexture->GetDesc();
        RHITextureMipSpan         arrMip[constant::kMaxTextureMipCount]{};
        const uint32              mipCount = resolveTextureUploadMips( desc, fromDxgiFormat( resDesc.Format ), static_cast<uint32>( resDesc.Width ),
                                                                       resDesc.Height, resDesc.MipLevels, arrMip, constant::kMaxTextureMipCount );
        if ( mipCount == 0 )
        {
            SW_LOG_ERROR( "uploadTexture2D: unsupported format or not enough data (%# bytes for %#x%#, %# mips)",
                          desc._sizeBytes, static_cast<uint32>( resDesc.Width ), resDesc.Height, static_cast<uint32>( resDesc.MipLevels ) );
            return false;
        }

        // 텍스처 복사는 행 피치 256·서브리소스 512 정렬 풋프린트를 요구한다 — 빈틈없는 입력을 풋프린트대로 다시 깐다.
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT arrFootprint[constant::kMaxTextureMipCount]{};
        UINT                               arrRowCount[constant::kMaxTextureMipCount]{};
        UINT64                             arrRowSize[constant::kMaxTextureMipCount]{};
        UINT64                             totalBytes{ 0 };
        _pDevice->_device->GetCopyableFootprints( &resDesc, 0, mipCount, 0, arrFootprint, arrRowCount, arrRowSize, &totalBytes );

        uint32 slotIndex{ 0 };
        uint64 stagingOffset{ 0 };
        void*  pMapped{ nullptr };
        if ( acquireUploadStaging( totalBytes, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT, slotIndex, stagingOffset, pMapped ) == false )
            return false;
        // 스테이징 안의 실제 위치로 풋프린트를 다시 받는다(BaseOffset).
        _pDevice->_device->GetCopyableFootprints( &resDesc, 0, mipCount, stagingOffset, arrFootprint, arrRowCount, arrRowSize, &totalBytes );

        for ( uint32 mip = 0; mip < mipCount; ++mip )
        {
            const RHITextureMipSpan&                  span      = arrMip[mip];
            const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& footprint = arrFootprint[mip];
            uint8*                                    pDstBase  = static_cast<uint8*>( pMapped ) + footprint.Offset;
            const uint32                              rowBytes  = MathUtil::min( span._rowBytes, static_cast<uint32>( arrRowSize[mip] ) );
            for ( uint32 row = 0; row < arrRowCount[mip]; ++row )
                Memory::copy( pDstBase + static_cast<uint64>( row ) * footprint.Footprint.RowPitch, span._pData + static_cast<uint64>( row ) * span._rowBytes, rowBytes );
        }

        D3D12RHIDevice::StructuredUploadSlot& slot  = _pDevice->_arrStructuredUploadSlot[slotIndex];
        ID3D12GraphicsCommandList*            pList = slot._copyCommandList.Get();

        // 렌더 타깃이면 추적 중인 상태에서, 아니면 COMMON 에서 출발해 같은 상태로 돌아간다 — 그래야 기존 SRV
        // 바인딩 경로(COMMON 암묵 승격)가 그대로 맞는다.
        D3D12_RESOURCE_STATES stateBefore = D3D12_RESOURCE_STATE_COMMON;
        {
            std::scoped_lock<mutex> lock{ _pDevice->_resourceStateMutex };
            const auto              offscreenIt = _pDevice->_mapOffscreenTexture.find( texture );
            if ( offscreenIt != _pDevice->_mapOffscreenTexture.end() )
                stateBefore = offscreenIt->second._state;
        }

        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource   = pTexture;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        if ( stateBefore != D3D12_RESOURCE_STATE_COPY_DEST )
        {
            barrier.Transition.StateBefore = stateBefore;
            barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_DEST;
            pList->ResourceBarrier( 1, &barrier );
        }

        for ( uint32 mip = 0; mip < mipCount; ++mip )
        {
            D3D12_TEXTURE_COPY_LOCATION dst{};
            dst.pResource        = pTexture;
            dst.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            dst.SubresourceIndex = arrMip[mip]._mip;

            D3D12_TEXTURE_COPY_LOCATION src{};
            src.pResource       = slot._uploadHeap.Get();
            src.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            src.PlacedFootprint = arrFootprint[mip];
            pList->CopyTextureRegion( &dst, 0, 0, 0, &src, nullptr );
        }

        if ( stateBefore != D3D12_RESOURCE_STATE_COPY_DEST )
        {
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
            barrier.Transition.StateAfter  = stateBefore;
            pList->ResourceBarrier( 1, &barrier );
        }

        submitUploadSlot( slotIndex );
        return true;
    }

    RHIBufferHandle D3D12RHIResource::createVertexBuffer( const void* pData, uint32 sizeBytes )
    {
        if ( _pDevice->_device == nullptr || pData == nullptr || sizeBytes == 0 )
            return 0;

        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC resDesc{};
        resDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        resDesc.Width            = sizeBytes;
        resDesc.Height           = 1;
        resDesc.DepthOrArraySize = 1;
        resDesc.MipLevels        = 1;
        resDesc.Format           = DXGI_FORMAT_UNKNOWN;
        resDesc.SampleDesc.Count = 1;
        resDesc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
        if ( FAILED( _pDevice->_device->CreateCommittedResource( &heapProps, D3D12_HEAP_FLAG_NONE, &resDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS( buffer.GetAddressOf() ) ) ) )
            return 0;

        void* pMapped{ nullptr };
        if ( FAILED( buffer->Map( 0, nullptr, &pMapped ) ) || pMapped == nullptr )
            return 0;
        Memory::copy( pMapped, pData, sizeBytes );
        buffer->Unmap( 0, nullptr );

        return _pDevice->storeBuffer( buffer );
    }

    void D3D12RHIResource::destroyBuffer( RHIBufferHandle buffer )
    {
        if ( buffer == 0 )
            return;
        if ( buffer == _pDevice->_frameStreamState._boundMeshVb )
            _pDevice->_frameStreamState._boundMeshVb = 0;
        if ( buffer == _pDevice->_frameStreamState._boundIndexBuffer )
            _pDevice->_frameStreamState._boundIndexBuffer = 0;
        {
            std::scoped_lock<mutex> lock{ _pDevice->_resourceStateMutex };
            _pDevice->_mapStructuredBufferState.erase( buffer );
        }
        const auto mapIt = _pDevice->_mapCbMapped.find( buffer );
        if ( mapIt != _pDevice->_mapCbMapped.end() && mapIt->second != nullptr )
        {
            ID3D12Resource* pRes = _pDevice->resolveBuffer( buffer );
            if ( pRes != nullptr )
                pRes->Unmap( 0, nullptr );
            _pDevice->_mapCbMapped.erase( mapIt );
        }
        _pDevice->_mapCbAlignedSize.erase( buffer );
        Microsoft::WRL::ComPtr<ID3D12Resource> owned;
        if ( _pDevice->_gpuBuffers.take( buffer, owned ) == false )
            return;

        {
            std::unique_lock<std::shared_mutex> lock{ _pDevice->_bindlessMutex };
            for ( D3D12RHIDevice::BindlessResourceRecord& rec : _pDevice->_listRegisteredBindless )
            {
                if ( rec._buffer != buffer )
                    continue;
                rec._resource.Reset();
                rec._buffer = 0;
            }
            for ( D3D12RHIDevice::BindlessResourceRecord& rec : _pDevice->_listRegisteredUAV )
            {
                if ( rec._buffer != buffer )
                    continue;
                rec._resource.Reset();
                rec._buffer = 0;
            }
        }

        auto releaseCb = [owned]()
        { (void)owned.Get(); };
        _pDevice->_releaseQueue.enqueueGpuRelease( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, releaseCb ), _pDevice->_fenceValue );
    }

    RHITextureHandle D3D12RHIResource::createTexture2D( const RHITextureDesc& desc )
    {
        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        const bool        bDepth      = desc._bIsDepthStencil != 0;
        const DXGI_FORMAT typelessFmt = bDepth ? DXGI_FORMAT_R24G8_TYPELESS : toDxgiFormat( desc._format );
        const DXGI_FORMAT dsvFmt      = toDxgiFormat( constant::kDepthStencilFormat );
        const DXGI_FORMAT colorFmt    = toDxgiFormat( desc._format );

        D3D12_RESOURCE_DESC resDesc{};
        resDesc.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        resDesc.Alignment          = 0;
        resDesc.Width              = desc._width;
        resDesc.Height             = desc._height;
        resDesc.DepthOrArraySize   = 1;
        resDesc.MipLevels          = static_cast<UINT16>( desc._mipLevels );
        resDesc.Format             = typelessFmt;
        resDesc.SampleDesc.Count   = 1;
        resDesc.SampleDesc.Quality = 0;
        resDesc.Layout             = D3D12_TEXTURE_LAYOUT_UNKNOWN;

        D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;
        if ( desc._bIsRenderTarget )
            flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        if ( bDepth )
            flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        if ( desc._bIsUnorderedAccess )
            flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        // Depth+SRV: do not deny shader resource.
        resDesc.Flags = flags;

        D3D12_CLEAR_VALUE  clearValue{};
        D3D12_CLEAR_VALUE* pClearValue{ nullptr };
        if ( desc._bIsRenderTarget )
        {
            clearValue.Format   = colorFmt;
            clearValue.Color[0] = desc._clearColor._x;
            clearValue.Color[1] = desc._clearColor._y;
            clearValue.Color[2] = desc._clearColor._z;
            clearValue.Color[3] = desc._clearColor._w;
            pClearValue         = &clearValue;
        }
        else if ( bDepth )
        {
            clearValue.Format               = dsvFmt;
            clearValue.DepthStencil.Depth   = desc._clearDepth;
            clearValue.DepthStencil.Stencil = desc._clearStencil;
            pClearValue                     = &clearValue;
        }

        Microsoft::WRL::ComPtr<ID3D12Resource> texture;
        if ( FAILED( _pDevice->_device->CreateCommittedResource( &heapProps, D3D12_HEAP_FLAG_NONE, &resDesc,
                                                                 D3D12_RESOURCE_STATE_COMMON, pClearValue, IID_PPV_ARGS( texture.GetAddressOf() ) ) ) )
            return 0;

        const RHITextureHandle                 handle  = _pDevice->storeTexture( texture );
        ID3D12Resource*                        pNative = _pDevice->resolveTexture( handle );
        D3D12RHIDevice::OffscreenTextureRecord record{};
        record._state    = D3D12_RESOURCE_STATE_COMMON;
        record._format   = bDepth ? dsvFmt : colorFmt;
        record._width    = desc._width;
        record._height   = desc._height;
        record._bHasRtv  = 0;
        record._bHasDsv  = 0;
        record._reserved = 0;

        _pDevice->checkRegistryMutableNow( "createTexture2D" );

        // 오프스크린 레코드와 디스크립터 프리리스트는 `transitionTexture` 가 기록 중에 읽는 것과
        // 같은 자료다. 슬롯 배정부터 맵 삽입까지를 그 락 안에서 끝낸다 — 예전엔 읽는 쪽만 잠가서
        // 생성/파괴가 맵을 리해시하면 읽는 쪽이 무효한 참조를 잡을 수 있었다.
        std::scoped_lock<mutex> offscreenLock{ _pDevice->_resourceStateMutex };

        if ( pNative != nullptr && desc._bIsRenderTarget && _pDevice->_rtvHeap != nullptr )
        {
            uint32 rtvSlot{ 0 };
            if ( _pDevice->_listFreeOffscreenRtvIndex.empty() == false )
            {
                rtvSlot = _pDevice->_listFreeOffscreenRtvIndex.back();
                _pDevice->_listFreeOffscreenRtvIndex.pop_back();
            }
            else if ( _pDevice->_nextOffscreenRtvIndex < D3D12RHIDevice::kMaxOffscreenRtvs )
                rtvSlot = _pDevice->_nextOffscreenRtvIndex++;
            else
            {
                // 고갈되면 예전엔 조용히 넘어갔다 — 유효한 핸들이 돌아오는데 RTV 가 없어서,
                // 나중에 beginRenderPass 가 이유 없이 아무것도 안 그리는 것처럼 보였다.
                rtvSlot = D3D12RHIDevice::kMaxOffscreenRtvs;
                SW_LOG_ERROR( "오프스크린 RTV 디스크립터 고갈(최대 %#) — 이 텍스처는 렌더타깃으로 쓸 수 없습니다.",
                              static_cast<uint32>( D3D12RHIDevice::kMaxOffscreenRtvs ) );
            }
            if ( rtvSlot < D3D12RHIDevice::kMaxOffscreenRtvs )
            {
                record._rtvIndex  = _pDevice->_swapChain.getBufferCount() + rtvSlot;
                record._rtvHandle = _pDevice->_rtvHeap->GetCPUDescriptorHandleForHeapStart();
                record._rtvHandle.ptr += static_cast<SIZE_T>( record._rtvIndex ) * _pDevice->_rtvDescriptorSize;
                D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
                rtvDesc.Format               = colorFmt;
                rtvDesc.ViewDimension        = D3D12_RTV_DIMENSION_TEXTURE2D;
                rtvDesc.Texture2D.MipSlice   = 0;
                rtvDesc.Texture2D.PlaneSlice = 0;
                _pDevice->_device->CreateRenderTargetView( pNative, &rtvDesc, record._rtvHandle );
                record._bHasRtv = 1;
            }
        }

        if ( pNative != nullptr && bDepth && _pDevice->_dsvHeap != nullptr )
        {
            uint32 dsvSlot{ 0 };
            if ( _pDevice->_listFreeOffscreenDsvIndex.empty() == false )
            {
                dsvSlot = _pDevice->_listFreeOffscreenDsvIndex.back();
                _pDevice->_listFreeOffscreenDsvIndex.pop_back();
            }
            else if ( _pDevice->_nextOffscreenDsvIndex < D3D12RHIDevice::kMaxOffscreenDsvs )
                dsvSlot = _pDevice->_nextOffscreenDsvIndex++;
            else
            {
                dsvSlot = D3D12RHIDevice::kMaxOffscreenDsvs;
                SW_LOG_ERROR( "오프스크린 DSV 디스크립터 고갈(최대 %#) — 이 텍스처는 뎁스로 쓸 수 없습니다.",
                              static_cast<uint32>( D3D12RHIDevice::kMaxOffscreenDsvs ) );
            }
            if ( dsvSlot < D3D12RHIDevice::kMaxOffscreenDsvs )
            {
                record._dsvIndex  = dsvSlot;
                record._dsvHandle = _pDevice->_dsvHeap->GetCPUDescriptorHandleForHeapStart();
                record._dsvHandle.ptr += static_cast<SIZE_T>( record._dsvIndex ) * _pDevice->_device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_DSV );
                D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
                dsvDesc.Format             = dsvFmt;
                dsvDesc.ViewDimension      = D3D12_DSV_DIMENSION_TEXTURE2D;
                dsvDesc.Flags              = D3D12_DSV_FLAG_NONE;
                dsvDesc.Texture2D.MipSlice = 0;
                _pDevice->_device->CreateDepthStencilView( pNative, &dsvDesc, record._dsvHandle );
                record._bHasDsv = 1;
            }
        }

        _pDevice->_mapOffscreenTexture[handle] = record;
        return handle;
    }

    void D3D12RHIResource::destroyTexture( RHITextureHandle texture )
    {
        if ( texture == 0 )
            return;
        _pDevice->checkRegistryMutableNow( "destroyTexture" );
        {
            std::scoped_lock<mutex> offscreenLock{ _pDevice->_resourceStateMutex };

            auto it = _pDevice->_mapOffscreenTexture.find( texture );
            if ( it != _pDevice->_mapOffscreenTexture.end() )
            {
                const uint32 offscreenRtvBase = _pDevice->_swapChain.getBufferCount();
                if ( it->second._bHasRtv != 0 && it->second._rtvIndex >= offscreenRtvBase )
                    _pDevice->_listFreeOffscreenRtvIndex.push_back( it->second._rtvIndex - offscreenRtvBase );
                if ( it->second._bHasDsv != 0 )
                    _pDevice->_listFreeOffscreenDsvIndex.push_back( it->second._dsvIndex );
                _pDevice->_mapOffscreenTexture.erase( it );
            }
        }
        Microsoft::WRL::ComPtr<ID3D12Resource> owned;
        if ( _pDevice->_gpuTextures.take( texture, owned ) == false )
            return;

        {
            std::unique_lock<std::shared_mutex> lock{ _pDevice->_bindlessMutex };
            for ( D3D12RHIDevice::BindlessResourceRecord& rec : _pDevice->_listRegisteredBindless )
            {
                if ( rec._texture != texture )
                    continue;
                rec._resource.Reset();
                rec._texture = 0;
            }
        }

        auto releaseCb = [owned]()
        { (void)owned.Get(); };
        _pDevice->_releaseQueue.enqueueGpuRelease( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, releaseCb ), _pDevice->_fenceValue );
    }
} // namespace sw
#endif
