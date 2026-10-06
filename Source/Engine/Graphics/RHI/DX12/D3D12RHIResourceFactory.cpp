#include "pch.h"

#include "Engine/Graphics/RHI/DX12/D3D12RHIResourceFactory.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Common/EnginePlatformHeaders.h"
#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/RHI/DX/RHIDxgiFormat.h"
#include "Engine/Graphics/RHI/DX12/D3D12RHIDevice.h"
#include "Engine/Graphics/RHI/DX12/D3D12RHIResourcePreset.h"
#include "Engine/Graphics/Shader/Compile/ShaderCache.h"

#if defined( SW_PLATFORM_WINDOWS )
namespace sw
{
    SW_LOG_CALLER( "D3D12RHIResourceFactory" );

    RHIBufferHandle D3D12RHIResourceFactory::createConstantBuffer( uint32 size )
    {
        const UINT                             alignedSize = MathUtil::align( size, constant::kConstantBufferAlignment );
        Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
        void*                                  pMapped{ nullptr };
        if ( _pDevice->createMappedUploadBuffer( static_cast<uint64>( alignedSize ) * constant::kMaxFrameCountInFlight, buffer, pMapped ) == false )
            return 0;

        const RHIBufferHandle handle = _pDevice->storeBuffer( buffer );
        {
            // 상수버퍼 맵은 렌더 스레드가 드로우마다 읽는다(`resolveBufferAddress`). 에디터가 없는 빌드는 게임 틱과 기록이 겹치므로, 게임 스레드의
            // 삽입(해시 표 재배치)이 그 읽기와 겹치지 않게 배타 락으로 넣는다.
            std::unique_lock<std::shared_mutex> lock{ _pDevice->_bindlessMutex };
            _pDevice->_mapCbAlignedSize[handle] = alignedSize;
            _pDevice->_mapCbMapped[handle]      = pMapped;
        }
        return handle;
    }

    void D3D12RHIResourceFactory::updateConstantBuffer( RHIBufferHandle buffer, const void* pData, uint32 size )
    {
        if ( buffer == 0 || pData == nullptr )
            return;

        // 이번 프레임 칸에 쓰고, 나머지 칸은 링이 그 칸으로 돌아올 때 채운다(`RHIConstantBufferMirror` — 값이 바뀔 때만 쓰는 머티리얼 버퍼가
        // 세 프레임 중 두 프레임을 옛 값으로 그리지 않게). 조회와 복사는 읽기 락 안에서 한다 — 다른 스레드의 `destroyBuffer` 가 Unmap 하는
        // 도중에 쓰면 안 된다.
        std::shared_lock<std::shared_mutex> lock{ _pDevice->_bindlessMutex };
        if ( _pDevice->_mapCbMapped.contains( buffer ) == false )
            return;
        _pDevice->_constantBufferMirror.write( buffer, _pDevice->_frameRing.currentIndex(), pData, size,
                                               [this]( RHIBufferHandle target, uint32 slot, const void* pBytes, uint32 byteCount )
        { _pDevice->writeConstantBufferSlot( target, slot, pBytes, byteCount ); } );

        // 힙의 CBV 는 여기서 갱신하지 않는다. 주의: 드로우마다 **레지스트리 전체를 훑어** CreateConstantBufferView 를
        // 다시 부르면 등록 수 N(수백) · 드로우 D 에 O(N·D) 라 드로우 경로의 지배적 비용이 된다.
        // CBV 주소는 **프레임 링 슬롯**에만 의존하므로 프레임당 한 번이면 충분하다.
        // D3D12RHIDevice::refreshConstantBufferViews 가 beginFrame 에서 한 번에 한다.
    }

    RHIBufferHandle D3D12RHIResourceFactory::createStructuredBuffer( uint32 elementSize, uint32 elementCount )
    {
        // 64비트로 곱한다. `UINT` 로 곱해 `Width`(UINT64)에 넣으면 넘칠 때 조용히 작은 버퍼가 된다.
        const uint64                totalBytes   = static_cast<uint64>( elementSize ) * static_cast<uint64>( elementCount );
        const D3D12_HEAP_PROPERTIES heapProps    = D3D12RHIResourcePreset::heapProperties( D3D12_HEAP_TYPE_DEFAULT );
        const D3D12_RESOURCE_DESC   resourceDesc = D3D12RHIResourcePreset::bufferDesc( totalBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS );

        Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
        if ( FAILED( _pDevice->_device->CreateCommittedResource( &heapProps, D3D12_HEAP_FLAG_NONE, &resourceDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS( buffer.GetAddressOf() ) ) ) )
            return 0;

        const RHIBufferHandle handle = _pDevice->storeBuffer( buffer );
        {
            std::scoped_lock<mutex> lock{ _pDevice->_resourceStateMutex };
            _pDevice->_mapStructuredBufferState[handle] = D3D12_RESOURCE_STATE_COMMON;
        }
        {
            // 등록(registerBindlessResource · registerBindlessUav)이 같은 배타 락 안에서 읽는다. 다른 스레드의 생성과 겹쳐 재해시되지 않게 한다.
            std::unique_lock<std::shared_mutex> registryLock{ _pDevice->_bindlessMutex };
            _pDevice->_mapStructuredStride[handle] = elementSize > 0 ? elementSize : 4u;
        }
        return handle;
    }

    bool D3D12RHIResourceFactory::openUploadSlot( uint32& outSlotIndex )
    {
        if ( _pDevice->_device == nullptr || _pDevice->_commandQueue == nullptr )
            return false;
        const uint32                          slotIndex       = _pDevice->_frameRing.currentIndex();
        D3D12RHIDevice::StructuredUploadSlot& slot            = _pDevice->_arrStructuredUploadSlot[slotIndex];
        bool                                  bNewFencePeriod = ( slot._resetFence != _pDevice->_fenceValue );

        // 같은 펜스 구간에 이미 열려 있으면 이어서 기록한다. 리스트를 닫고 다시 여는 것도, 제출도 프레임에 한 번이다.
        if ( slot._bListOpen != SW_FALSE && bNewFencePeriod == false )
        {
            outSlotIndex = slotIndex;
            return true;
        }
        // 구간이 바뀌었는데 열려 있다면 프레임 제출과 Signal 사이에 연 복사다. 지금 내보낸다. 내보내면 `_resetFence` 가 **지금** 구간이 되므로
        // (그 복사는 이번 구간의 Signal 에서야 끝난다) 얼로케이터를 Reset 하지 않고 이어 쓴다 — 다시 판정한다.
        if ( slot._bListOpen != SW_FALSE )
        {
            _pDevice->flushPendingUploads( true );
            bNewFencePeriod = ( slot._resetFence != _pDevice->_fenceValue );
        }

        if ( slot._copyAllocator == nullptr )
        {
            if ( FAILED( _pDevice->_device->CreateCommandAllocator( D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS( slot._copyAllocator.GetAddressOf() ) ) ) )
            {
                SW_LOG_ERROR( "openUploadSlot: failed to create copy command allocator" );
                return false;
            }
            utf16 arrName[constant::kMaxBuffer64]{};
            swprintf_s( arrName, L"StructuredUploadAllocator%u", slotIndex );
            slot._copyAllocator->SetName( arrName );
        }
        else if ( bNewFencePeriod )
        {
            // 펜스 값이 바뀌었다는 것은 앞 구간 뒤에 Signal 이 **큐에 들어갔다**는 뜻이지 GPU 가 그 구간의 복사를
            // 끝냈다는 뜻이 아니다. signalCurrentFrame 은 올리기만 하고 기다리지 않는다. 프레임 끝 Signal 직후,
            // 링이 아직 앞 슬롯을 가리키는 동안 업로드가 오면 여기서 아직 실행 중인 얼로케이터를 Reset 하게 된다
            // ("is being reset before previous executions ... have completed" → DEVICE_HUNG, GPU 가 붐빌 때만).
            // 링 슬롯 대기가 가려 줄 것이라 기대하지 않고 이 얼로케이터의 펜스(_resetFence: 그 구간의 제출 뒤에
            // Signal 된 값)를 직접 기다린다. 보통은 이미 지나 있어 비용이 없다.
            if ( _pDevice->waitForFenceValue( slot._resetFence ) == false )
            {
                SW_LOG_ERROR( "openUploadSlot: previous copies on allocator %# have not completed (fence %#)",
                              slotIndex, slot._resetFence );
                return false;
            }
            if ( FAILED( slot._copyAllocator->Reset() ) )
            {
                SW_LOG_ERROR( "openUploadSlot: copy allocator Reset failed" );
                return false;
            }
        }
        if ( bNewFencePeriod )
            slot._uploadOffset = 0;
        slot._resetFence = _pDevice->_fenceValue;

        if ( slot._copyCommandList == nullptr )
        {
            if ( FAILED( _pDevice->_device->CreateCommandList( 0, D3D12_COMMAND_LIST_TYPE_DIRECT, slot._copyAllocator.Get(), nullptr, IID_PPV_ARGS( slot._copyCommandList.GetAddressOf() ) ) ) )
            {
                SW_LOG_ERROR( "openUploadSlot: failed to create copy command list" );
                return false;
            }
        }
        else if ( FAILED( slot._copyCommandList->Reset( slot._copyAllocator.Get(), nullptr ) ) )
        {
            SW_LOG_ERROR( "openUploadSlot: copy command list Reset failed" );
            return false;
        }
        slot._bListOpen = SW_TRUE;
        outSlotIndex    = slotIndex;
        return true;
    }

    bool D3D12RHIResourceFactory::waitForQueueDrain()
    {
        if ( _pDevice->_commandQueue == nullptr || _pDevice->_fence == nullptr || _pDevice->_fenceEvent == nullptr )
            return false;
        if ( _pDevice->_device != nullptr && FAILED( _pDevice->_device->GetDeviceRemovedReason() ) )
            return false;

        // 열어 둔 복사가 있으면 먼저 내보낸다. 아래 펜스가 그것까지 덮어야 한다.
        _pDevice->flushPendingUploads( true );

        // waitForPreviousFrame 은 스왑체인 acquire 까지 하므로 프레임 중간에 부를 수 없다. 펜스만 올리고 기다린다.
        // _fenceValue 가 올라가므로 다음 openUploadSlot 은 새 구간으로 보고 얼로케이터를 Reset 한다. 방금
        // 기다린 작업이 그 얼로케이터의 마지막 사용이니 안전하다.
        const UINT64 fenceToWait = _pDevice->_fenceValue;
        if ( FAILED( _pDevice->_commandQueue->Signal( _pDevice->_fence.Get(), fenceToWait ) ) )
            return false;
        _pDevice->_fenceValue++;
        if ( _pDevice->waitForFenceValue( fenceToWait ) == false )
            return false;
        const uint64 completedFence = _pDevice->_fence->GetCompletedValue();
        _pDevice->_releaseQueue.tickCompleted( completedFence );
        _pDevice->recycleCompletedOnlineBlocks( completedFence );
        return true;
    }

    bool D3D12RHIResourceFactory::acquireUploadStaging( uint64 sizeBytes, uint64 alignment, uint32& outSlotIndex, uint64& outOffset, void*& pOutMapped )
    {
        if ( sizeBytes == 0 || _pDevice->_device == nullptr || _pDevice->_commandQueue == nullptr )
            return false;

        // 프레임 링 슬롯 하나를 재사용한다(매 호출마다 업로드 힙 · 얼로케이터 · 리스트를 새로 만들지 않는다).
        // 이 슬롯을 다시 쓸 차례가 됐다는 것은 waitForRingSlot() 이 이미 constant::kMaxFrameCountInFlight 프레임 전 제출의
        // GPU 완료를 보장했다는 뜻이라 별도 대기(waitForPreviousFrame) 없이 안전하다. **프레임 사이에는**.
        // 같은 프레임 안의 두 번째 호출은 첫 번째 복사가 GPU 에서 아직 도는 중일 수 있으므로, 펜스 구간이
        // 바뀌었을 때만 얼로케이터를 Reset 하고 스테이징은 오프셋을 이어 쓴다.
        // 얼로케이터 · 리스트를 먼저 연다. 펜스 구간이 바뀌었으면 여기서 오프셋도 0 으로 되감긴다.
        uint32 slotIndex{ 0 };
        if ( openUploadSlot( slotIndex ) == false )
            return false;
        D3D12RHIDevice::StructuredUploadSlot& slot = _pDevice->_arrStructuredUploadSlot[slotIndex];

        uint64 stagingOffset = MathUtil::align( slot._uploadOffset, alignment );
        if ( slot._uploadHeap == nullptr || slot._capacity < stagingOffset + sizeBytes )
        {
            const uint64 newCapacity = MathUtil::align( ( stagingOffset + sizeBytes ) * 2, 65536ull );

            // 옛 힙은 이번 구간의 앞선 복사가 아직 읽고 있을 수 있다. 펜스 뒤에 놓아 준다.
            if ( slot._uploadHeap != nullptr )
            {
                Microsoft::WRL::ComPtr<ID3D12Resource> oldHeap = slot._uploadHeap;
                RHIMemoryLedger*                       pLedger = &_pDevice->getMemoryLedger();
                _pDevice->_releaseQueue.enqueueGpuRelease( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, [oldHeap, pLedger]()
                {
                    pLedger->recordFree( RHIMemoryKey::makeDeviceObject( oldHeap.Get() ) );
                } ),
                                                           _pDevice->_fenceValue );
                slot._uploadHeap   = nullptr;
                slot._pMapped      = nullptr;
                slot._uploadOffset = 0;
                stagingOffset      = 0;
            }

            Microsoft::WRL::ComPtr<ID3D12Resource> newHeap;
            void*                                  pMapped{ nullptr };
            if ( _pDevice->createMappedUploadBuffer( newCapacity, newHeap, pMapped ) == false )
            {
                SW_LOG_ERROR( "acquireUploadStaging: failed to create or map staging upload buffer (%# bytes)", newCapacity );
                return false;
            }

            _pDevice->getMemoryLedger().recordAllocation( RHIMemoryKey::makeDeviceObject( newHeap.Get() ), RHIMemoryKind::Staging,
                                                          _pDevice->computeAllocationBytes( newHeap.Get() ) );
            slot._uploadHeap = newHeap;
            slot._pMapped    = pMapped;
            slot._capacity   = newCapacity;
        }

        slot._uploadOffset = stagingOffset + sizeBytes;
        outSlotIndex       = slotIndex;
        outOffset          = stagingOffset;
        pOutMapped         = slot._pMapped;
        return true;
    }

    void D3D12RHIResourceFactory::updateStructuredBufferRegions( RHIBufferHandle buffer, const void* pBaseSource,
                                                                 const RHIBufferCopyRegion* pRegions, uint32 regionCount )
    {
        if ( buffer == 0 || pBaseSource == nullptr || pRegions == nullptr || regionCount == 0 ||
             _pDevice->_device == nullptr || _pDevice->_commandQueue == nullptr )
            return;

        ID3D12Resource* pDest = _pDevice->resolveBuffer( buffer );
        if ( pDest == nullptr )
            return;

        std::scoped_lock<mutex> uploadLock{ _pDevice->_uploadSlotMutex };

        // **조각을 모두 한 스테이징에 모아 한 번만 제출한다.** 조각마다 부르면 스테이징 확보와 큐
        // 제출이 그만큼 되풀이돼 비용이 구간 수에 선형으로 붙는다(호출당 ~3.3 us).
        constexpr uint32 kCopyAlignment = 4;
        uint32           totalSize      = 0;
        for ( uint32 regionIndex = 0; regionIndex < regionCount; ++regionIndex )
            totalSize += MathUtil::align( pRegions[regionIndex]._size, kCopyAlignment );
        if ( totalSize == 0 )
            return;

        uint32 slotIndex{ 0 };
        uint64 stagingOffset{ 0 };
        void*  pMapped{ nullptr };
        if ( acquireUploadStaging( totalSize, constant::kConstantBufferAlignment, slotIndex, stagingOffset, pMapped ) == false )
            return;

        const uint8* pBase = static_cast<const uint8*>( pBaseSource );
        uint64       cursor{ stagingOffset };
        for ( uint32 regionIndex = 0; regionIndex < regionCount; ++regionIndex )
        {
            const RHIBufferCopyRegion& region = pRegions[regionIndex];
            if ( region._size == 0 )
                continue;
            Memory::copy( static_cast<uint8*>( pMapped ) + cursor, pBase + region._srcOffset, region._size );
            cursor += MathUtil::align( region._size, kCopyAlignment );
        }

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

        cursor = stagingOffset;
        for ( uint32 regionIndex = 0; regionIndex < regionCount; ++regionIndex )
        {
            const RHIBufferCopyRegion& region = pRegions[regionIndex];
            if ( region._size == 0 )
                continue;
            pList->CopyBufferRegion( pDest, region._dstOffset, slot._uploadHeap.Get(), cursor, region._size );
            cursor += MathUtil::align( region._size, kCopyAlignment );
        }

        // 올린 뒤에는 **셰이더 읽기** 상태로 둔다. 주의: UAV 로 끝내면 그래픽스가 SRV 로만 읽는 버퍼(배치 정보 · 머티리얼 데이터 · 라이트, 회전
        // 인스턴스가 없을 때의 인스턴스 버퍼)가 UAV 상태로 읽힌다(GPU 검증이 짚고, 드라이버 관용에 기대 동작한다). 컴퓨트로 쓰는 버퍼는 쓰기 전에
        // `transitionBuffer( UnorderedAccess )` 가 기록된 상태에서 옮긴다.
        constexpr D3D12_RESOURCE_STATES kShaderReadState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        D3D12_RESOURCE_BARRIER          toShaderRead{};
        toShaderRead.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        toShaderRead.Transition.pResource   = pDest;
        toShaderRead.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        toShaderRead.Transition.StateAfter  = kShaderReadState;
        toShaderRead.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        pList->ResourceBarrier( 1, &toShaderRead );
        // 제출은 프레임 끝(또는 큐 대기 직전)에 한 번이다. D3D12RHIDevice::flushPendingUploads.

        {
            std::scoped_lock<mutex> lock{ _pDevice->_resourceStateMutex };
            _pDevice->_mapStructuredBufferState[buffer] = kShaderReadState;
        }
    }

    bool D3D12RHIResourceFactory::uploadTexture2D( RHITextureHandle texture, const RHITextureUploadDesc& desc )
    {
        ID3D12Resource* pTexture = _pDevice->resolveTexture( texture );
        if ( pTexture == nullptr || _pDevice->_device == nullptr || _pDevice->_commandQueue == nullptr )
            return false;

        std::scoped_lock<mutex> uploadLock{ _pDevice->_uploadSlotMutex };

        const D3D12_RESOURCE_DESC resourceDesc = pTexture->GetDesc();
        if ( desc._arraySlice >= resourceDesc.DepthOrArraySize )
        {
            SW_LOG_ERROR( "uploadTexture2D: slice %# is out of range (%# slices)", desc._arraySlice, static_cast<uint32>( resourceDesc.DepthOrArraySize ) );
            return false;
        }
        // 서브리소스 번호 = 밉 + 면 × 밉 수(D3D12CalcSubresource 와 같은 순서).
        const UINT        firstSubresource = desc._arraySlice * resourceDesc.MipLevels;
        RHITextureMipSpan arrMip[constant::kMaxTextureMipCount]{};
        const uint32      mipCount = resolveTextureUploadMips( desc, fromDxgiFormat( resourceDesc.Format ), static_cast<uint32>( resourceDesc.Width ),
                                                               resourceDesc.Height, resourceDesc.MipLevels, arrMip, constant::kMaxTextureMipCount );
        if ( mipCount == 0 )
        {
            SW_LOG_ERROR( "uploadTexture2D: unsupported format or not enough data (%# bytes for %#×%#, %# mips)",
                          desc._sizeBytes, static_cast<uint32>( resourceDesc.Width ), resourceDesc.Height, static_cast<uint32>( resourceDesc.MipLevels ) );
            return false;
        }

        // 텍스처 복사는 행 피치 256 · 서브리소스 512 정렬 풋프린트를 요구한다. 빈틈없는 입력을 풋프린트대로 다시 깐다.
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT arrFootprint[constant::kMaxTextureMipCount]{};
        UINT                               arrRowCount[constant::kMaxTextureMipCount]{};
        UINT64                             arrRowSize[constant::kMaxTextureMipCount]{};
        UINT64                             totalBytes{ 0 };
        _pDevice->_device->GetCopyableFootprints( &resourceDesc, firstSubresource, mipCount, 0, arrFootprint, arrRowCount, arrRowSize, &totalBytes );

        uint32 slotIndex{ 0 };
        uint64 stagingOffset{ 0 };
        void*  pMapped{ nullptr };
        if ( acquireUploadStaging( totalBytes, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT, slotIndex, stagingOffset, pMapped ) == false )
            return false;
        // 스테이징 안의 실제 위치로 풋프린트를 다시 받는다(BaseOffset).
        _pDevice->_device->GetCopyableFootprints( &resourceDesc, firstSubresource, mipCount, stagingOffset, arrFootprint, arrRowCount, arrRowSize, &totalBytes );

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

        // 추적 상태에서 출발해 같은 상태로 돌아간다(getTrackedTextureState).
        const D3D12_RESOURCE_STATES stateBefore = _pDevice->getTrackedTextureState( texture );

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
            dst.SubresourceIndex = firstSubresource + arrMip[mip]._mip;

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
        // 제출은 프레임 끝(또는 큐 대기 직전)에 한 번이다. D3D12RHIDevice::flushPendingUploads.
        return true;
    }

    bool D3D12RHIResourceFactory::uploadTexture2DRegion( RHITextureHandle texture, const RHITextureRegionUploadDesc& desc )
    {
        ID3D12Resource* pTexture = _pDevice->resolveTexture( texture );
        if ( pTexture == nullptr || _pDevice->_device == nullptr || _pDevice->_commandQueue == nullptr )
            return false;

        // 슬롯 · 얼로케이터 규칙은 전체 업로드와 같은 것 하나다(acquireUploadStaging) — 같은 프레임에 업로드가 여럿이어도 슬롯을 두 번 Reset 하지 않는다.
        std::scoped_lock<mutex> uploadLock{ _pDevice->_uploadSlotMutex };

        const D3D12_RESOURCE_DESC resourceDesc = pTexture->GetDesc();
        uint32                    rowBytes{ 0 };
        if ( validateTextureRegionUpload( fromDxgiFormat( resourceDesc.Format ), static_cast<uint32>( resourceDesc.Width ), resourceDesc.Height,
                                          resourceDesc.MipLevels, resourceDesc.DepthOrArraySize, desc, rowBytes ) == false )
            return false;

        // 복사 원본은 행 피치 256 정렬 풋프린트여야 한다 — 구간 크기로 손수 짓는다(GetCopyableFootprints 는 서브리소스 전체를 준다).
        const uint32 rowPitch = MathUtil::align( rowBytes, static_cast<uint32>( D3D12_TEXTURE_DATA_PITCH_ALIGNMENT ) );
        uint32       slotIndex{ 0 };
        uint64       stagingOffset{ 0 };
        void*        pMapped{ nullptr };
        if ( acquireUploadStaging( static_cast<uint64>( rowPitch ) * desc._height, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT, slotIndex, stagingOffset, pMapped ) == false )
            return false;

        uint8*       pDstBase = static_cast<uint8*>( pMapped ) + stagingOffset;
        const uint8* pSrcBase = static_cast<const uint8*>( desc._pData );
        for ( uint32 row = 0; row < desc._height; ++row )
            Memory::copy( pDstBase + static_cast<uint64>( row ) * rowPitch, pSrcBase + static_cast<uint64>( row ) * rowBytes, rowBytes );

        D3D12RHIDevice::StructuredUploadSlot& slot  = _pDevice->_arrStructuredUploadSlot[slotIndex];
        ID3D12GraphicsCommandList*            pList = slot._copyCommandList.Get();

        // 추적 상태에서 출발해 같은 상태로 돌아간다(전체 업로드와 같다).
        const D3D12_RESOURCE_STATES stateBefore = _pDevice->getTrackedTextureState( texture );
        D3D12_RESOURCE_BARRIER      barrier{};
        barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource   = pTexture;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        if ( stateBefore != D3D12_RESOURCE_STATE_COPY_DEST )
        {
            barrier.Transition.StateBefore = stateBefore;
            barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_DEST;
            pList->ResourceBarrier( 1, &barrier );
        }

        D3D12_TEXTURE_COPY_LOCATION dst{};
        dst.pResource        = pTexture;
        dst.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.SubresourceIndex = desc._mip + desc._arraySlice * resourceDesc.MipLevels; // D3D12CalcSubresource 와 같은 순서

        D3D12_TEXTURE_COPY_LOCATION src{};
        src.pResource                          = slot._uploadHeap.Get();
        src.Type                               = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint.Offset             = stagingOffset;
        src.PlacedFootprint.Footprint.Format   = resourceDesc.Format;
        src.PlacedFootprint.Footprint.Width    = desc._width;
        src.PlacedFootprint.Footprint.Height   = desc._height;
        src.PlacedFootprint.Footprint.Depth    = 1;
        src.PlacedFootprint.Footprint.RowPitch = rowPitch;
        pList->CopyTextureRegion( &dst, desc._x, desc._y, 0, &src, nullptr );

        if ( stateBefore != D3D12_RESOURCE_STATE_COPY_DEST )
        {
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
            barrier.Transition.StateAfter  = stateBefore;
            pList->ResourceBarrier( 1, &barrier );
        }
        return true;
    }

    RHIFormat D3D12RHIResourceFactory::getTextureFormat( RHITextureHandle texture ) const
    {
        // 오프스크린 레코드는 요청 포맷을 그대로 들고 있다(깊이는 리소스가 typeless 라 GetDesc 로는 못 되돌린다).
        D3D12RHIDevice::OffscreenTargetView view{};
        if ( _pDevice->findOffscreenTargetView( texture, 0, view ) )
        {
            if ( view._bHasDsv != SW_FALSE )
                return RHIFormat::D24_UNORM_S8_UINT;
            return fromDxgiFormat( view._format );
        }
        ID3D12Resource* pTexture = _pDevice->resolveTexture( texture );
        if ( pTexture == nullptr )
            return RHIFormat::Unknown;
        return fromDxgiFormat( pTexture->GetDesc().Format );
    }

    bool D3D12RHIResourceFactory::readbackTexture2D( RHITextureHandle texture, uint32 mip, uint32 arraySlice, vector<uint8>& outBytes, RHITextureMipSpan& outLayout )
    {
        ID3D12Resource* pTexture = _pDevice->resolveTexture( texture );
        if ( pTexture == nullptr || _pDevice->_device == nullptr || _pDevice->_commandQueue == nullptr )
            return false;

        std::scoped_lock<mutex> uploadLock{ _pDevice->_uploadSlotMutex };

        const D3D12_RESOURCE_DESC resourceDesc = pTexture->GetDesc();
        if ( mip >= resourceDesc.MipLevels || arraySlice >= resourceDesc.DepthOrArraySize )
            return false;
        if ( computeRhiTextureMipLayout( fromDxgiFormat( resourceDesc.Format ), static_cast<uint32>( resourceDesc.Width ), resourceDesc.Height, mip, outLayout ) == false )
            return false;

        const UINT                         subresource = mip + arraySlice * resourceDesc.MipLevels;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
        UINT                               rowCount{ 0 };
        UINT64                             rowSize{ 0 };
        UINT64                             totalBytes{ 0 };
        _pDevice->_device->GetCopyableFootprints( &resourceDesc, subresource, 1, 0, &footprint, &rowCount, &rowSize, &totalBytes );

        const D3D12_HEAP_PROPERTIES            readbackHeap = D3D12RHIResourcePreset::heapProperties( D3D12_HEAP_TYPE_READBACK );
        const D3D12_RESOURCE_DESC              bufferDesc   = D3D12RHIResourcePreset::bufferDesc( totalBytes );
        Microsoft::WRL::ComPtr<ID3D12Resource> readback;
        if ( FAILED( _pDevice->_device->CreateCommittedResource( &readbackHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
                                                                 D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS( readback.GetAddressOf() ) ) ) )
            return false;

        uint32 slotIndex{ 0 };
        if ( openUploadSlot( slotIndex ) == false )
            return false;
        ID3D12GraphicsCommandList* pList = _pDevice->_arrStructuredUploadSlot[slotIndex]._copyCommandList.Get();

        const D3D12_RESOURCE_STATES stateBefore = _pDevice->getTrackedTextureState( texture );

        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource   = pTexture;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        if ( stateBefore != D3D12_RESOURCE_STATE_COPY_SOURCE )
        {
            barrier.Transition.StateBefore = stateBefore;
            barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_SOURCE;
            pList->ResourceBarrier( 1, &barrier );
        }

        D3D12_TEXTURE_COPY_LOCATION src{};
        src.pResource        = pTexture;
        src.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src.SubresourceIndex = subresource;
        D3D12_TEXTURE_COPY_LOCATION dst{};
        dst.pResource       = readback.Get();
        dst.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint = footprint;
        pList->CopyTextureRegion( &dst, 0, 0, 0, &src, nullptr );

        if ( stateBefore != D3D12_RESOURCE_STATE_COPY_SOURCE )
        {
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
            barrier.Transition.StateAfter  = stateBefore;
            pList->ResourceBarrier( 1, &barrier );
        }
        // 큐 대기가 열어 둔 복사를 먼저 내보낸다(flushPendingUploads). 이 readback 도 그 안에 있다.
        if ( waitForQueueDrain() == false )
            return false;

        void* pMapped{ nullptr };
        if ( FAILED( readback->Map( 0, nullptr, &pMapped ) ) || pMapped == nullptr )
            return false;
        outBytes.assign( outLayout._sizeBytes, 0 );
        const uint32 copyRowBytes = MathUtil::min( outLayout._rowBytes, static_cast<uint32>( rowSize ) );
        for ( uint32 row = 0; row < rowCount; ++row )
            Memory::copy( outBytes.data() + static_cast<uint64>( row ) * outLayout._rowBytes,
                          static_cast<const uint8*>( pMapped ) + footprint.Offset + static_cast<uint64>( row ) * footprint.Footprint.RowPitch, copyRowBytes );
        D3D12_RANGE noWrite{ 0, 0 };
        readback->Unmap( 0, &noWrite );
        return true;
    }

    RHIBufferHandle D3D12RHIResourceFactory::createVertexBuffer( const void* pData, uint32 sizeBytes )
    {
        return createUploadBuffer( pData, sizeBytes );
    }

    RHIBufferHandle D3D12RHIResourceFactory::createIndexBuffer( const void* pData, uint32 sizeBytes, uint32 indexStride )
    {
        // 인덱스 크기는 걸 때(setIndexBuffer) 정한다. 인덱스 버퍼는 구조버퍼가 아니라 정점 버퍼처럼
        // 업로드 힙의 GENERIC_READ 로 둔다. 그 상태가 INDEX_BUFFER 읽기를 포함하므로 전이 없이 걸 수 있다.
        (void)indexStride;
        return createUploadBuffer( pData, sizeBytes );
    }

    RHIBufferHandle D3D12RHIResourceFactory::createUploadBuffer( const void* pData, uint32 sizeBytes )
    {
        if ( _pDevice->_device == nullptr || pData == nullptr || sizeBytes == 0 )
            return 0;

        Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
        void*                                  pMapped{ nullptr };
        if ( _pDevice->createMappedUploadBuffer( sizeBytes, buffer, pMapped ) == false )
            return 0;
        Memory::copy( pMapped, pData, sizeBytes );
        buffer->Unmap( 0, nullptr );

        return _pDevice->storeBuffer( buffer );
    }

    void D3D12RHIResourceFactory::destroyBuffer( RHIBufferHandle buffer )
    {
        if ( buffer == 0 )
            return;
        // 렌더 스레드의 기록 상태(`_frameStreamState` 의 묶인 정점 · 인덱스 버퍼)는 여기서 지우지 않는다. 이 함수는 게임 스레드에서도 불리는데 그 값은
        // 렌더 스레드만 쓴다(여기서 쓰면 경쟁이다). 핸들은 세대가 있어 다시 쓰이지 않으므로, 지운 핸들은 드로우의 `resolveBuffer` 가 null 로
        // 풀어 건너뛴다.
        {
            std::scoped_lock<mutex> lock{ _pDevice->_resourceStateMutex };
            _pDevice->_mapStructuredBufferState.erase( buffer );
        }
        std::unique_lock<std::shared_mutex> registryLock{ _pDevice->_bindlessMutex };
        const auto                          mapIt = _pDevice->_mapCbMapped.find( buffer );
        if ( mapIt != _pDevice->_mapCbMapped.end() && mapIt->second != nullptr )
        {
            ID3D12Resource* pResource = _pDevice->resolveBuffer( buffer );
            if ( pResource != nullptr )
                pResource->Unmap( 0, nullptr );
            _pDevice->_mapCbMapped.erase( mapIt );
        }
        _pDevice->_mapCbAlignedSize.erase( buffer );
        _pDevice->_mapStructuredStride.erase( buffer );
        _pDevice->_constantBufferMirror.forget( buffer );
        Microsoft::WRL::ComPtr<ID3D12Resource> owned;
        if ( _pDevice->_gpuBuffers.take( buffer, owned ) == false )
            return;

        {
            for ( D3D12RHIDevice::BindlessResourceRecord& record : _pDevice->_listRegisteredBindless )
            {
                if ( record._buffer != buffer )
                    continue;
                record._resource.Reset();
                record._buffer = 0;
            }
            for ( D3D12RHIDevice::BindlessResourceRecord& record : _pDevice->_listRegisteredUAV )
            {
                if ( record._buffer != buffer )
                    continue;
                record._resource.Reset();
                record._buffer = 0;
            }
        }

        _pDevice->releaseTrackedResourceDeferred( std::move( owned ), RHIMemoryKey::makeBuffer( buffer ) );
    }

    RHITextureHandle D3D12RHIResourceFactory::createTexture2D( const RHITextureDesc& desc )
    {
        if ( isRhiTextureShapeValid( desc ) == false )
        {
            SW_LOG_ERROR( "createTexture2D: dimension %# with %# slices (%#x%#) is not a valid texture shape", static_cast<uint32>( desc._dimension ),
                          desc._arraySize, desc._width, desc._height );
            return 0;
        }
        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        const bool        bDepth      = desc._bIsDepthStencil != SW_FALSE;
        const DXGI_FORMAT typelessFmt = bDepth ? DXGI_FORMAT_R24G8_TYPELESS : toDxgiFormat( desc._format );
        const DXGI_FORMAT dsvFmt      = toDxgiFormat( constant::kDepthStencilFormat );
        const DXGI_FORMAT colorFmt    = toDxgiFormat( desc._format );

        D3D12_RESOURCE_DESC resourceDesc{};
        resourceDesc.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        resourceDesc.Alignment          = 0;
        resourceDesc.Width              = desc._width;
        resourceDesc.Height             = desc._height;
        resourceDesc.DepthOrArraySize   = static_cast<UINT16>( desc._arraySize );
        resourceDesc.MipLevels          = static_cast<UINT16>( desc._mipLevels );
        resourceDesc.Format             = typelessFmt;
        resourceDesc.SampleDesc.Count   = 1;
        resourceDesc.SampleDesc.Quality = 0;
        resourceDesc.Layout             = D3D12_TEXTURE_LAYOUT_UNKNOWN;

        D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;
        if ( desc._bIsRenderTarget )
            flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        if ( bDepth )
            flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        if ( desc._bIsUnorderedAccess )
            flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        // 깊이 + SRV: 셰이더 리소스 접근을 막지 않는다(DENY_SHADER_RESOURCE 를 붙이지 않는다).
        resourceDesc.Flags = flags;

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
        if ( FAILED( _pDevice->_device->CreateCommittedResource( &heapProps, D3D12_HEAP_FLAG_NONE, &resourceDesc,
                                                                 D3D12_RESOURCE_STATE_COMMON, pClearValue, IID_PPV_ARGS( texture.GetAddressOf() ) ) ) )
            return 0;

        const RHITextureHandle                 handle  = _pDevice->storeTexture( texture, RHIMemoryLedger::classifyTexture( desc ) );
        ID3D12Resource*                        pNative = _pDevice->resolveTexture( handle );
        D3D12RHIDevice::OffscreenTextureRecord record{};
        record._state      = D3D12_RESOURCE_STATE_COMMON;
        record._format     = bDepth ? dsvFmt : colorFmt;
        record._width      = desc._width;
        record._height     = desc._height;
        record._arraySize  = desc._arraySize;
        record._dimension  = desc._dimension;
        record._bHasRtv    = SW_FALSE;
        record._bHasDsv    = SW_FALSE;
        record._reserved   = 0;
        const bool bSliced = desc._dimension != RHITextureDimension::Texture2D;

        _pDevice->assertRegistryMutableNow( "createTexture2D" );

        // 오프스크린 레코드와 디스크립터 프리리스트는 `transitionTexture` 가 기록 중에 읽는 것과
        // 같은 자료다. 슬롯 배정부터 맵 삽입까지를 그 락 안에서 끝낸다. 읽는 쪽만 잠그면
        // 생성/파괴가 맵을 리해시할 때 읽는 쪽이 무효한 참조를 잡는다.
        std::scoped_lock<mutex> offscreenLock{ _pDevice->_resourceStateMutex };

        for ( uint32 slice = 0; pNative != nullptr && desc._bIsRenderTarget && _pDevice->_rtvHeap != nullptr && slice < desc._arraySize; ++slice )
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
                // 고갈은 로그로 남긴다. 유효한 핸들이 돌아오는데 RTV 가 없으므로, 조용히 넘어가면
                // 나중에 beginRenderPass 가 이유 없이 아무것도 안 그리는 것처럼 보인다.
                rtvSlot = D3D12RHIDevice::kMaxOffscreenRtvs;
                SW_LOG_ERROR( "오프스크린 RTV 디스크립터 고갈(최대 %#) — 이 텍스처는 렌더타깃으로 쓸 수 없습니다.",
                              static_cast<uint32>( D3D12RHIDevice::kMaxOffscreenRtvs ) );
            }
            if ( rtvSlot >= D3D12RHIDevice::kMaxOffscreenRtvs )
                break;
            const uint32                  rtvIndex  = _pDevice->_swapChain.getBufferCount() + rtvSlot;
            D3D12_CPU_DESCRIPTOR_HANDLE   rtvHandle = _pDevice->getOffscreenRtvHandle( rtvIndex );
            D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
            rtvDesc.Format = colorFmt;
            if ( bSliced )
            {
                rtvDesc.ViewDimension                  = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
                rtvDesc.Texture2DArray.MipSlice        = 0;
                rtvDesc.Texture2DArray.FirstArraySlice = slice;
                rtvDesc.Texture2DArray.ArraySize       = 1;
                rtvDesc.Texture2DArray.PlaneSlice      = 0;
            }
            else
            {
                rtvDesc.ViewDimension        = D3D12_RTV_DIMENSION_TEXTURE2D;
                rtvDesc.Texture2D.MipSlice   = 0;
                rtvDesc.Texture2D.PlaneSlice = 0;
            }
            _pDevice->_device->CreateRenderTargetView( pNative, &rtvDesc, rtvHandle );
            if ( slice == 0 )
            {
                record._rtvIndex  = rtvIndex;
                record._rtvHandle = rtvHandle;
                record._bHasRtv   = SW_TRUE;
            }
            else
                record._listExtraRtvIndex.push_back( rtvIndex );
        }

        for ( uint32 slice = 0; pNative != nullptr && bDepth && _pDevice->_dsvHeap != nullptr && slice < desc._arraySize; ++slice )
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
            if ( dsvSlot >= D3D12RHIDevice::kMaxOffscreenDsvs )
                break;
            const D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = _pDevice->getOffscreenDsvHandle( dsvSlot );
            D3D12_DEPTH_STENCIL_VIEW_DESC     dsvDesc{};
            dsvDesc.Format = dsvFmt;
            dsvDesc.Flags  = D3D12_DSV_FLAG_NONE;
            if ( bSliced )
            {
                dsvDesc.ViewDimension                  = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
                dsvDesc.Texture2DArray.MipSlice        = 0;
                dsvDesc.Texture2DArray.FirstArraySlice = slice;
                dsvDesc.Texture2DArray.ArraySize       = 1;
            }
            else
            {
                dsvDesc.ViewDimension      = D3D12_DSV_DIMENSION_TEXTURE2D;
                dsvDesc.Texture2D.MipSlice = 0;
            }
            _pDevice->_device->CreateDepthStencilView( pNative, &dsvDesc, dsvHandle );
            if ( slice == 0 )
            {
                record._dsvIndex  = dsvSlot;
                record._dsvHandle = dsvHandle;
                record._bHasDsv   = SW_TRUE;
            }
            else
                record._listExtraDsvIndex.push_back( dsvSlot );
        }

        _pDevice->_mapOffscreenTexture[handle] = record;
        return handle;
    }

    void D3D12RHIResourceFactory::destroyTexture( RHITextureHandle texture )
    {
        if ( texture == 0 )
            return;
        _pDevice->assertRegistryMutableNow( "destroyTexture" );
        {
            std::scoped_lock<mutex> offscreenLock{ _pDevice->_resourceStateMutex };

            auto it = _pDevice->_mapOffscreenTexture.find( texture );
            if ( it != _pDevice->_mapOffscreenTexture.end() )
            {
                const uint32 offscreenRtvBase = _pDevice->_swapChain.getBufferCount();
                if ( it->second._bHasRtv != SW_FALSE && it->second._rtvIndex >= offscreenRtvBase )
                    _pDevice->_listFreeOffscreenRtvIndex.push_back( it->second._rtvIndex - offscreenRtvBase );
                if ( it->second._bHasDsv != SW_FALSE )
                    _pDevice->_listFreeOffscreenDsvIndex.push_back( it->second._dsvIndex );
                for ( const uint32 rtvIndex : it->second._listExtraRtvIndex )
                    _pDevice->_listFreeOffscreenRtvIndex.push_back( rtvIndex - offscreenRtvBase );
                for ( const uint32 dsvIndex : it->second._listExtraDsvIndex )
                    _pDevice->_listFreeOffscreenDsvIndex.push_back( dsvIndex );
                _pDevice->_mapOffscreenTexture.erase( it );
            }
        }
        Microsoft::WRL::ComPtr<ID3D12Resource> owned;
        if ( _pDevice->_gpuTextures.take( texture, owned ) == false )
            return;

        {
            std::unique_lock<std::shared_mutex> lock{ _pDevice->_bindlessMutex };
            for ( D3D12RHIDevice::BindlessResourceRecord& record : _pDevice->_listRegisteredBindless )
            {
                if ( record._texture != texture )
                    continue;
                record._resource.Reset();
                record._texture = 0;
            }
        }

        _pDevice->releaseTrackedResourceDeferred( std::move( owned ), RHIMemoryKey::makeTexture( texture ) );
    }
} // namespace sw
#endif
