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

        std::shared_lock<std::shared_mutex> lock{ _pDevice->_bindlessMutex };

        // 공유 락이라 여러 스레드가 동시에 여기 들어온다 — 비-const 순회는 "쓰기" 로 취급되어
        // 읽기만 하는데도 레이스로 잡힌다. 실제로 읽기만 하므로 const 로 받는다.
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

    void D3D12RHIResource::updateStructuredBuffer( RHIBufferHandle buffer, const void* pData, uint32 size )
    {
        if ( buffer == 0 || pData == nullptr || size == 0 || _pDevice->_device == nullptr || _pDevice->_commandQueue == nullptr )
            return;

        ID3D12Resource* pDest = _pDevice->resolveBuffer( buffer );
        if ( pDest == nullptr )
            return;

        // 프레임 링 슬롯 하나를 재사용한다(매 호출마다 업로드 힙/얼로케이터/리스트를 새로 만들지 않음).
        // 이 슬롯을 다시 쓸 차례가 됐다는 건 waitForRingSlot()이 이미 constant::kMaxFrameCountInFlight 프레임 전 제출의
        // GPU 완료를 보장했다는 뜻이라 별도 대기(waitForPreviousFrame) 없이 안전하다.
        D3D12RHIDevice::StructuredUploadSlot& slot = _pDevice->_arrStructuredUploadSlot[_pDevice->_frameRing.currentIndex()];

        if ( slot._uploadHeap == nullptr || slot._capacity < size )
        {
            const uint64 newCapacity = MathUtil::align( static_cast<uint64>( size ) * 2, 65536ull );

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
                SW_LOG_ERROR( "updateStructuredBuffer: failed to (re)create staging upload buffer (%# bytes)", newCapacity );
                return;
            }

            void* pMapped{ nullptr };
            if ( FAILED( newHeap->Map( 0, nullptr, &pMapped ) ) || pMapped == nullptr )
            {
                SW_LOG_ERROR( "updateStructuredBuffer: Map failed on staging buffer" );
                return;
            }

            slot._uploadHeap = newHeap;
            slot._pMapped    = pMapped;
            slot._capacity   = newCapacity;
        }

        Memory::copy( slot._pMapped, pData, size );

        if ( slot._copyAllocator == nullptr )
        {
            if ( FAILED( _pDevice->_device->CreateCommandAllocator( D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS( slot._copyAllocator.GetAddressOf() ) ) ) )
            {
                SW_LOG_ERROR( "updateStructuredBuffer: failed to create copy command allocator" );
                return;
            }
        }
        else if ( FAILED( slot._copyAllocator->Reset() ) )
        {
            SW_LOG_ERROR( "updateStructuredBuffer: copy allocator Reset failed" );
            return;
        }

        if ( slot._copyCommandList == nullptr )
        {
            if ( FAILED( _pDevice->_device->CreateCommandList( 0, D3D12_COMMAND_LIST_TYPE_DIRECT, slot._copyAllocator.Get(), nullptr, IID_PPV_ARGS( slot._copyCommandList.GetAddressOf() ) ) ) )
            {
                SW_LOG_ERROR( "updateStructuredBuffer: failed to create copy command list" );
                return;
            }
        }
        else if ( FAILED( slot._copyCommandList->Reset( slot._copyAllocator.Get(), nullptr ) ) )
        {
            SW_LOG_ERROR( "updateStructuredBuffer: copy command list Reset failed" );
            return;
        }

        ID3D12GraphicsCommandList* pList = slot._copyCommandList.Get();

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

        pList->CopyBufferRegion( pDest, 0, slot._uploadHeap.Get(), 0, size );

        D3D12_RESOURCE_BARRIER toUav{};
        toUav.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        toUav.Transition.pResource   = pDest;
        toUav.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        toUav.Transition.StateAfter  = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        toUav.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        pList->ResourceBarrier( 1, &toUav );

        pList->Close();
        ID3D12CommandList* lists[] = { pList };
        _pDevice->_commandQueue->ExecuteCommandLists( 1, lists );

        {
            std::scoped_lock<mutex> lock{ _pDevice->_resourceStateMutex };
            _pDevice->_mapStructuredBufferState[buffer] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        }
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
                rtvSlot = D3D12RHIDevice::kMaxOffscreenRtvs;
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
                dsvSlot = D3D12RHIDevice::kMaxOffscreenDsvs;
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
