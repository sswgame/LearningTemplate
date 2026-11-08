#include "pch.h"

#include "Engine/Graphics/RHI/DX/DX12/D3D12RHIResourceFactory.h"

#include "Core/Concurrency/mutex.h"
#include "Core/Container/IndexFreeList.h"
#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Foundation/EnginePlatformHeaders.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/RHI/Backend/RHIBufferSizeUtil.h"
#include "Engine/Graphics/RHI/Backend/RHIShaderRequest.h"
#include "Engine/Graphics/RHI/DX/DX12/D3D12RHIDevice.h"
#include "Engine/Graphics/RHI/DX/DX12/D3D12RHIResourcePreset.h"
#include "Engine/Graphics/RHI/DX/DXGIFormat.h"
#include "Engine/Graphics/RHI/DX/RHIDxRenderState.h"
#include "Engine/Graphics/Shader/Compile/ShaderCache.h"
#include "Engine/Graphics/Shader/Compile/ShaderCompiler.h"

#if defined( SW_PLATFORM_WINDOWS )
namespace sw
{
    RHIBufferHandle D3D12RHIResourceFactory::createConstantBuffer( uint32 size )
    {
        const UINT                             alignedSize = MathUtil::align( size, constant::render::kConstantBufferAlignment );
        Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
        void*                                  pMapped{ nullptr };
        if ( _pDevice->createMappedUploadBuffer( static_cast<uint64>( alignedSize ) * constant::render::kMaxFrameCountInFlight, buffer, pMapped ) == false )
            return 0;

        const RHIBufferHandle handle = _pDevice->storeBuffer( buffer );
        {
            // 상수버퍼 맵은 렌더 스레드가 드로우마다 읽는다(`resolveBufferAddress`). 에디터가 없는 빌드는 게임 틱과 기록이 겹치므로, 게임 스레드의
            // 삽입(해시 표 재배치)이 그 읽기와 겹치지 않게 배타 락으로 넣는다.
            UniqueLock<shared_mutex> lock{ _pDevice->_bindlessMutex };
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
        ScopedSharedLock<shared_mutex> lock{ _pDevice->_bindlessMutex };
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
        if ( isHrSuccess( _pDevice->_device->CreateCommittedResource( &heapProps, D3D12_HEAP_FLAG_NONE, &resourceDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS( buffer.GetAddressOf() ) ), "CreateCommittedResource" ) == false )
            return 0;

        const RHIBufferHandle handle = _pDevice->storeBuffer( buffer );
        {
            ScopedLock<mutex> lock{ _pDevice->_resourceStateMutex };
            _pDevice->_mapStructuredBufferState[handle] = D3D12_RESOURCE_STATE_COMMON;
        }
        {
            // 등록(registerBindlessBufferSRV · registerBindlessBufferUAV)이 같은 배타 락 안에서 읽는다. 다른 스레드의 생성과 겹쳐 재해시되지 않게 한다.
            UniqueLock<shared_mutex> registryLock{ _pDevice->_bindlessMutex };
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
        bool                                  bNewFencePeriod = ( slot._resetFence != _pDevice->_fenceValue.load() );

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
            bNewFencePeriod = ( slot._resetFence != _pDevice->_fenceValue.load() );
        }

        if ( slot._copyAllocator == nullptr )
        {
            if ( isHrSuccess( _pDevice->_device->CreateCommandAllocator( D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS( slot._copyAllocator.GetAddressOf() ) ), "CreateCommandAllocator" ) == false )
            {
                SW_LOG_ERROR_ID( "D3D12RHIResourceFactory.OpenUploadSlotFailedCreateCopy", "openUploadSlot: failed to create copy command allocator" );
                return false;
            }
            utf16 arrName[constant::buffer::kMax64]{};
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
                SW_LOG_ERROR_ID( "D3D12RHIResourceFactory.OpenUploadSlotPreviousCopiesAllocator", "openUploadSlot: previous copies on allocator %# have not completed (fence %#)",
                                 slotIndex, slot._resetFence );
                return false;
            }
            if ( isHrSuccess( slot._copyAllocator->Reset(), "Reset" ) == false )
            {
                SW_LOG_ERROR_ID( "D3D12RHIResourceFactory.OpenUploadSlotCopyAllocatorReset", "openUploadSlot: copy allocator Reset failed" );
                return false;
            }
        }
        if ( bNewFencePeriod )
            slot._uploadOffset = 0;
        slot._resetFence = _pDevice->_fenceValue.load();

        if ( slot._copyCommandList == nullptr )
        {
            if ( isHrSuccess( _pDevice->_device->CreateCommandList( 0, D3D12_COMMAND_LIST_TYPE_DIRECT, slot._copyAllocator.Get(), nullptr, IID_PPV_ARGS( slot._copyCommandList.GetAddressOf() ) ), "CreateCommandList" ) == false )
            {
                SW_LOG_ERROR_ID( "D3D12RHIResourceFactory.OpenUploadSlotFailedCreateCopy2", "openUploadSlot: failed to create copy command list" );
                return false;
            }
        }
        else if ( isHrSuccess( slot._copyCommandList->Reset( slot._copyAllocator.Get(), nullptr ), "Reset" ) == false )
        {
            SW_LOG_ERROR_ID( "D3D12RHIResourceFactory.OpenUploadSlotCopyCommandList", "openUploadSlot: copy command list Reset failed" );
            return false;
        }
        slot._bListOpen = SW_TRUE;
        outSlotIndex    = slotIndex;
        return true;
    }

    bool D3D12RHIResourceFactory::acquireUploadStaging( uint64 sizeBytes, uint64 alignment, uint32& outSlotIndex, uint64& outOffset, void*& pOutMapped )
    {
        const bool bCannotAcquireStaging = sizeBytes == 0 || _pDevice->_device == nullptr || _pDevice->_commandQueue == nullptr;
        if ( bCannotAcquireStaging )
            return false;

        // 프레임 링 슬롯 하나를 재사용한다(매 호출마다 업로드 힙 · 얼로케이터 · 리스트를 새로 만들지 않는다).
        // 이 슬롯을 다시 쓸 차례가 됐다는 것은 waitForRingSlot() 이 이미 constant::render::kMaxFrameCountInFlight 프레임 전 제출의
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
                Microsoft::WRL::ComPtr<ID3D12Resource> oldHeap  = slot._uploadHeap;
                RHIMemoryTracker*                      pTracker = &_pDevice->getMemoryTracker();
                _pDevice->_releaseQueue.enqueueGPURelease( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, [oldHeap, pTracker]()
                {
                    pTracker->recordFree( RHIMemoryKey::makeDeviceObject( oldHeap.Get() ) );
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
                SW_LOG_ERROR_ID( "D3D12RHIResourceFactory.AcquireUploadStagingFailedCreateMap", "acquireUploadStaging: failed to create or map staging upload buffer (%# bytes)", newCapacity );
                return false;
            }

            _pDevice->getMemoryTracker().recordAllocation( RHIMemoryKey::makeDeviceObject( newHeap.Get() ), RHIMemoryKind::Staging,
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
        _pDevice->requireNoOpenRenderPassForUpload( "updateStructuredBufferRegions" );
        const bool bInvalidBufferRegionUpload = buffer == 0 || pBaseSource == nullptr || pRegions == nullptr || regionCount == 0 ||
                                                _pDevice->_device == nullptr || _pDevice->_commandQueue == nullptr;
        if ( bInvalidBufferRegionUpload )
            return;

        ID3D12Resource* pDest = _pDevice->resolveBuffer( buffer );
        if ( pDest == nullptr )
            return;

        ScopedLock<mutex> uploadLock{ _pDevice->_uploadSlotMutex };

        // **조각을 모두 한 스테이징에 모아 한 번만 제출한다.** 조각마다 부르면 스테이징 확보와 큐
        // 제출이 그만큼 되풀이돼 비용이 구간 수에 선형으로 붙는다(호출당 ~3.3 us).
        constexpr uint32 kCopyAlignment = 4;
        uint32           totalSize      = 0;
        for ( uint32 regionIndex = 0; regionIndex < regionCount; ++regionIndex )
        {
            totalSize += MathUtil::align( pRegions[regionIndex]._sizeBytes, kCopyAlignment );
        }
        if ( totalSize == 0 )
            return;

        uint32 slotIndex{ 0 };
        uint64 stagingOffset{ 0 };
        void*  pMapped{ nullptr };
        if ( acquireUploadStaging( totalSize, constant::render::kConstantBufferAlignment, slotIndex, stagingOffset, pMapped ) == false )
            return;

        const uint8* pBase = static_cast<const uint8*>( pBaseSource );
        uint64       cursor{ stagingOffset };
        for ( uint32 regionIndex = 0; regionIndex < regionCount; ++regionIndex )
        {
            const RHIBufferCopyRegion& region = pRegions[regionIndex];
            if ( region._sizeBytes == 0 )
                continue;
            Memory::copy( static_cast<uint8*>( pMapped ) + cursor, pBase + region._srcOffset, region._sizeBytes );
            cursor += MathUtil::align( region._sizeBytes, kCopyAlignment );
        }

        D3D12RHIDevice::StructuredUploadSlot& slot  = _pDevice->_arrStructuredUploadSlot[slotIndex];
        ID3D12GraphicsCommandList*            pList = slot._copyCommandList.Get();

        D3D12_RESOURCE_STATES stateBefore = D3D12_RESOURCE_STATE_COMMON;
        {
            ScopedLock<mutex>                                                           lock{ _pDevice->_resourceStateMutex };
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
            if ( region._sizeBytes == 0 )
                continue;
            pList->CopyBufferRegion( pDest, region._dstOffset, slot._uploadHeap.Get(), cursor, region._sizeBytes );
            cursor += MathUtil::align( region._sizeBytes, kCopyAlignment );
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
            ScopedLock<mutex> lock{ _pDevice->_resourceStateMutex };
            _pDevice->_mapStructuredBufferState[buffer] = kShaderReadState;
        }
    }

    // 길이를 나누지 않는다: 서브리소스 · 풋프린트 · 스테이징 복사 · 상태 전이의 순서가 곧 뜻이다.
    RHITransferSubmit D3D12RHIResourceFactory::submitTextureUpload( RHITextureHandle texture, const RHITextureUploadDesc& desc )
    {
        ID3D12Resource* pTexture                  = _pDevice->resolveTexture( texture );
        const bool      bTextureUploadUnavailable = pTexture == nullptr || _pDevice->_device == nullptr || _pDevice->_commandQueue == nullptr || _pDevice->isDeviceLost();
        if ( bTextureUploadUnavailable )
        {
            // 장치를 잃었으면 진입에서 Rejected 다(계약 2) — Accepted 를 주고 폴링에서야 Failed 가 되면 호출자가 잃은 장치에 올린 줄 알고 기다린다.
            SW_LOG_ERROR_ID( "D3D12RHIResourceFactory.SubmitTextureUploadTextureDeviceNot", "submitTextureUpload: texture %# or the device is not usable (uninitialized or lost)", texture );
            return RHITransferSubmit::rejected();
        }

        // 프레임 제출 락(`_uploadSlotMutex`)은 잡지 않는다 — 풋프린트 계산, 소스 복사, 리스트 기록은 호출 스레드의 일이고 큐에 내는 순간만 전송 제출 락이 직렬화한다.
        const D3D12_RESOURCE_DESC resourceDesc = pTexture->GetDesc();
        if ( desc._arraySlice >= resourceDesc.DepthOrArraySize )
        {
            SW_LOG_ERROR_ID( "D3D12RHIResourceFactory.SubmitTextureUploadSliceOutRange", "submitTextureUpload: slice %# is out of range (%# slices)", desc._arraySlice, static_cast<uint32>( resourceDesc.DepthOrArraySize ) );
            return RHITransferSubmit::rejected();
        }
        // 서브리소스 번호 = 밉 + 면 × 밉 수(D3D12CalcSubresource 와 같은 순서).
        const UINT        firstSubresource = desc._arraySlice * resourceDesc.MipLevels;
        RHITextureMipSpan arrMip[constant::render::kMaxTextureMipCount]{};
        const uint32      mipCount = resolveTextureUploadMips( desc, fromDXGIFormat( resourceDesc.Format ), static_cast<uint32>( resourceDesc.Width ),
                                                               resourceDesc.Height, resourceDesc.MipLevels, arrMip, constant::render::kMaxTextureMipCount );
        if ( mipCount == 0 )
        {
            SW_LOG_ERROR_ID( "D3D12RHIResourceFactory.SubmitTextureUploadUnsupportedFormatNot", "submitTextureUpload: unsupported format or not enough data (%# bytes for %#×%#, %# mips)",
                             desc._sizeBytes, static_cast<uint32>( resourceDesc.Width ), resourceDesc.Height, static_cast<uint32>( resourceDesc.MipLevels ) );
            return RHITransferSubmit::rejected();
        }

        // 텍스처 복사는 행 피치 256 · 서브리소스 512 정렬 풋프린트를 요구한다. 빈틈없는 입력을 풋프린트대로 다시 깐다.
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT arrFootprint[constant::render::kMaxTextureMipCount]{};
        UINT                               arrRowCount[constant::render::kMaxTextureMipCount]{};
        UINT64                             arrRowSize[constant::render::kMaxTextureMipCount]{};
        UINT64                             totalBytes{ 0 };
        _pDevice->_device->GetCopyableFootprints( &resourceDesc, firstSubresource, mipCount, 0, arrFootprint, arrRowCount, arrRowSize, &totalBytes );

        D3D12RHIDevice::D3D12StagingRing& ring = _pDevice->_uploadRing;
        RHIStagingSpan                    span{};
        const RHISubmitResult             allocateResult = _pDevice->allocateStagingSpan( ring, totalBytes, "submitTextureUpload", span );
        if ( allocateResult != RHISubmitResult::Accepted )
            return RHITransferSubmit::fromUnaccepted( allocateResult );
        // 링 안의 실제 위치로 풋프린트를 다시 받는다(BaseOffset). 구간 시작이 배치 오프셋 정렬(512)의 배수라 서브리소스 오프셋도 규칙을 지킨다.
        _pDevice->_device->GetCopyableFootprints( &resourceDesc, firstSubresource, mipCount, span._offsetBytes, arrFootprint, arrRowCount, arrRowSize, &totalBytes );

        // 소스를 스테이징으로 복사한다(계약 1). 이 뒤로 호출자는 버퍼를 재사용해도 된다. 큐 락 밖의 일이다.
        uint8* const pRingBase = ring.getSpanMappedBase( span );
        for ( uint32 mip = 0; mip < mipCount; ++mip )
        {
            const RHITextureMipSpan&                  mipSpan   = arrMip[mip];
            const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& footprint = arrFootprint[mip];
            uint8*                                    pDstBase  = pRingBase + footprint.Offset;
            const uint32                              rowBytes  = MathUtil::min( mipSpan._rowBytes, static_cast<uint32>( arrRowSize[mip] ) );
            for ( uint32 row = 0; row < arrRowCount[mip]; ++row )
            {
                Memory::copy( pDstBase + static_cast<uint64>( row ) * footprint.Footprint.RowPitch, mipSpan._pData + static_cast<uint64>( row ) * mipSpan._rowBytes, rowBytes );
            }
        }

        D3D12RHIDevice::D3D12TransferJob job{};
        if ( _pDevice->beginTransferJob( job ) == false )
        {
            ring._allocator.free( span );
            return RHITransferSubmit::rejected();
        }
        ID3D12GraphicsCommandList* pList = job._list.Get();

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
            src.pResource       = ring.getSpanResource( span );
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
        // 기록이 끝났다. 즉시 큐에 내고 전송 펜스를 올린다(기다리지 않는다). 시리얼은 큐 제출과 같은 락 안에서 매겨진다.
        const uint64 serial = _pDevice->submitTransferJob( std::move( job ), "submitTextureUpload" );
        if ( serial == 0 )
        {
            ring._allocator.free( span );
            return RHITransferSubmit::rejected();
        }
        ring._allocator.markSubmitted( span, serial );
        _pDevice->recordTextureTransfer( texture, serial );
        return _pDevice->_transferSerials.makeAccepted( serial );
    }

    RHITransferSubmit D3D12RHIResourceFactory::submitTextureRegionUpload( RHITextureHandle texture, const RHITextureRegionUploadDesc& desc )
    {
        ID3D12Resource* pTexture                        = _pDevice->resolveTexture( texture );
        const bool      bTextureRegionUploadUnavailable = pTexture == nullptr || _pDevice->_device == nullptr || _pDevice->_commandQueue == nullptr || _pDevice->isDeviceLost();
        if ( bTextureRegionUploadUnavailable )
        {
            SW_LOG_ERROR_ID( "D3D12RHIResourceFactory.SubmitTextureRegionUploadTextureDeviceNot", "submitTextureRegionUpload: texture %# or the device is not usable (uninitialized or lost)", texture );
            return RHITransferSubmit::rejected();
        }

        const D3D12_RESOURCE_DESC resourceDesc = pTexture->GetDesc();
        uint32                    rowBytes{ 0 };
        if ( validateTextureRegionUpload( fromDXGIFormat( resourceDesc.Format ), static_cast<uint32>( resourceDesc.Width ), resourceDesc.Height,
                                          resourceDesc.MipLevels, resourceDesc.DepthOrArraySize, desc, rowBytes ) == false )
            return RHITransferSubmit::rejected();

        // 복사 원본은 행 피치 256 정렬 풋프린트여야 한다 — 구간 크기로 손수 짓는다(GetCopyableFootprints 는 서브리소스 전체를 준다).
        const uint32                      rowPitch = MathUtil::align( rowBytes, static_cast<uint32>( D3D12_TEXTURE_DATA_PITCH_ALIGNMENT ) );
        D3D12RHIDevice::D3D12StagingRing& ring     = _pDevice->_uploadRing;
        RHIStagingSpan                    span{};
        const RHISubmitResult             allocateResult = _pDevice->allocateStagingSpan( ring, static_cast<uint64>( rowPitch ) * desc._height, "submitTextureRegionUpload", span );
        if ( allocateResult != RHISubmitResult::Accepted )
            return RHITransferSubmit::fromUnaccepted( allocateResult );

        uint8*       pDstBase = ring.getSpanMappedBase( span ) + span._offsetBytes;
        const uint8* pSrcBase = static_cast<const uint8*>( desc._pData );
        for ( uint32 row = 0; row < desc._height; ++row )
        {
            Memory::copy( pDstBase + static_cast<uint64>( row ) * rowPitch, pSrcBase + static_cast<uint64>( row ) * rowBytes, rowBytes );
        }

        D3D12RHIDevice::D3D12TransferJob job{};
        if ( _pDevice->beginTransferJob( job ) == false )
        {
            ring._allocator.free( span );
            return RHITransferSubmit::rejected();
        }
        ID3D12GraphicsCommandList* pList = job._list.Get();

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
        src.pResource                          = ring.getSpanResource( span );
        src.Type                               = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint.Offset             = span._offsetBytes;
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
        const uint64 serial = _pDevice->submitTransferJob( std::move( job ), "submitTextureRegionUpload" );
        if ( serial == 0 )
        {
            ring._allocator.free( span );
            return RHITransferSubmit::rejected();
        }
        ring._allocator.markSubmitted( span, serial );
        _pDevice->recordTextureTransfer( texture, serial );
        return _pDevice->_transferSerials.makeAccepted( serial );
    }

    RHITransferStatus D3D12RHIResourceFactory::pollTransfer( RHITransferTicket ticket )
    {
        // 판정은 공용 부품이 한다. 아직이면 끝난 전송을 한 번 걷고 다시 본다 — 제출은 이미 큐에 났으므로 프레임을 돌리지 않아도 되풀이하면 끝에 이른다(계약 7).
        const RHITransferStatus status = _pDevice->_transferSerials.classify( ticket );
        if ( status != RHITransferStatus::Pending )
            return status;
        const bool bDeviceGone = _pDevice->_commandQueue == nullptr || _pDevice->_transferFence == nullptr || _pDevice->isDeviceRemoved();
        if ( bDeviceGone )
        {
            _pDevice->_transferSerials.markLost();
            return _pDevice->_transferSerials.classify( ticket );
        }
        _pDevice->retireCompletedTransfers();
        return _pDevice->_transferSerials.classify( ticket );
    }

    bool D3D12RHIResourceFactory::takeTextureReadback( RHITransferTicket ticket, vector<uint8>& outBytes )
    {
        if ( pollTransfer( ticket ) != RHITransferStatus::Complete )
        {
            SW_LOG_ERROR_ID( "D3D12RHIResourceFactory.TakeTextureReadbackTicketNotCompleted", "takeTextureReadback: ticket %# has not completed", ticket._serial );
            return false;
        }
        D3D12RHIDevice::TextureReadbackRecord record;
        if ( _pDevice->_readbackTable.take( ticket._serial, record ) == false )
        {
            SW_LOG_ERROR_ID( "D3D12RHIResourceFactory.TakeTextureReadbackTicketNotPending", "takeTextureReadback: ticket %# is not a pending texture readback (an upload ticket, or already taken or discarded)", ticket._serial );
            return false;
        }

        void* pMapped{ nullptr };
        if ( isHrSuccess( record._readback->Map( 0, nullptr, &pMapped ), "Map" ) == false || pMapped == nullptr )
        {
            SW_LOG_ERROR_ID( "D3D12RHIResourceFactory.TakeTextureReadbackFailedMapReadback", "takeTextureReadback: failed to map the readback buffer of ticket %#", ticket._serial );
            return false;
        }
        outBytes.assign( record._layout._sizeBytes, 0 );
        const uint32 copyRowBytes = MathUtil::min( record._layout._rowBytes, static_cast<uint32>( record._rowSize ) );
        for ( uint32 row = 0; row < record._rowCount; ++row )
        {
            Memory::copy( outBytes.data() + static_cast<uint64>( row ) * record._layout._rowBytes,
                          static_cast<const uint8*>( pMapped ) + record._footprint.Offset + static_cast<uint64>( row ) * record._footprint.Footprint.RowPitch, copyRowBytes );
        }
        D3D12_RANGE noWrite{ 0, 0 };
        record._readback->Unmap( 0, &noWrite );
        if ( record._bDepth != SW_FALSE )
            convertRHIDepth24ToFloat( outBytes.data(), record._layout._width * record._layout._height );
        return true;
    }

    void D3D12RHIResourceFactory::discardTextureReadback( RHITransferTicket ticket )
    {
        if ( ticket._deviceGeneration != _pDevice->_transferSerials.getGeneration() )
            return;
        D3D12RHIDevice::TextureReadbackRecord record;
        if ( _pDevice->_readbackTable.take( ticket._serial, record ) == false )
            return; // 업로드 티켓이거나 이미 가져감 · 버림이다 — 계약상 아무 일도 하지 않는다
        // 복사가 아직 GPU 에서 도는 중일 수 있어 읽기 버퍼를 지금 놓으면 안 된다. 전송이 끝나면(워터마크가 이 시리얼을 지나면) 놓는다.
        // 이미 끝났다면 바로 놓는다.
        if ( _pDevice->_transferSerials.isComplete( ticket._serial ) )
            return;
        _pDevice->_transferReleaseQueue.enqueueGPUKeepAlive( std::move( record._readback ), ticket._serial );
    }

    RHIFormat D3D12RHIResourceFactory::getTextureFormat( RHITextureHandle texture ) const
    {
        // 오프스크린 레코드는 요청 포맷을 그대로 들고 있다(깊이는 리소스가 typeless 라 GetDesc 로는 못 되돌린다).
        D3D12RHIDevice::OffscreenTargetView view{};
        if ( _pDevice->findOffscreenTargetView( texture, 0, view ) )
        {
            if ( view._bHasDSV != SW_FALSE )
                return RHIFormat::D24_UNORM_S8_UINT;
            return fromDXGIFormat( view._format );
        }
        ID3D12Resource* pTexture = _pDevice->resolveTexture( texture );
        if ( pTexture == nullptr )
            return RHIFormat::Unknown;
        return fromDXGIFormat( pTexture->GetDesc().Format );
    }

    RHITransferSubmit D3D12RHIResourceFactory::submitTextureReadback( RHITextureHandle texture, uint32 mip, uint32 arraySlice, RHITextureMipSpan& outLayout )
    {
        ID3D12Resource* pTexture                    = _pDevice->resolveTexture( texture );
        const bool      bTextureReadbackUnavailable = pTexture == nullptr || _pDevice->_device == nullptr || _pDevice->_commandQueue == nullptr || _pDevice->isDeviceLost();
        if ( bTextureReadbackUnavailable )
        {
            SW_LOG_ERROR_ID( "D3D12RHIResourceFactory.SubmitTextureReadbackTextureDeviceNot", "submitTextureReadback: texture %# or the device is not usable (uninitialized or lost)", texture );
            return RHITransferSubmit::rejected();
        }

        // 읽기 힙 버퍼 생성, 풋프린트 계산, 리스트 기록은 호출 스레드의 일이다 — 프레임 제출 락(`_uploadSlotMutex`)을 잡지 않는다.
        const D3D12_RESOURCE_DESC resourceDesc = pTexture->GetDesc();
        if ( mip >= resourceDesc.MipLevels || arraySlice >= resourceDesc.DepthOrArraySize )
        {
            SW_LOG_ERROR_ID( "D3D12RHIResourceFactory.SubmitTextureReadbackMipSliceOut", "submitTextureReadback: mip %# / slice %# is out of range (%# mips, %# slices)", mip, arraySlice, static_cast<uint32>( resourceDesc.MipLevels ),
                             static_cast<uint32>( resourceDesc.DepthOrArraySize ) );
            return RHITransferSubmit::rejected();
        }
        // 깊이는 typeless(R24G8)로 만들어져 있다. 깊이 평면(평면 0)만 복사하고 가져갈 때 0..1 float32 로 바꾼다(kRHIDepthReadbackFormat).
        const bool      bDepth       = resourceDesc.Format == DXGI_FORMAT_R24G8_TYPELESS || resourceDesc.Format == DXGI_FORMAT_D24_UNORM_S8_UINT;
        const RHIFormat layoutFormat = bDepth ? kRHIDepthReadbackFormat : fromDXGIFormat( resourceDesc.Format );
        if ( computeRHITextureMipLayout( layoutFormat, static_cast<uint32>( resourceDesc.Width ), resourceDesc.Height, mip, outLayout ) == false )
        {
            SW_LOG_ERROR_ID( "D3D12RHIResourceFactory.SubmitTextureReadbackUnsupportedFormatX", "submitTextureReadback: unsupported format for %#x%# mip %#", static_cast<uint32>( resourceDesc.Width ), resourceDesc.Height, mip );
            return RHITransferSubmit::rejected();
        }

        // 평면 0 의 서브리소스 번호는 평면이 하나인 텍스처와 같다.
        const UINT                         subresource = mip + arraySlice * resourceDesc.MipLevels;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
        UINT                               rowCount{ 0 };
        UINT64                             rowSize{ 0 };
        UINT64                             totalBytes{ 0 };
        _pDevice->_device->GetCopyableFootprints( &resourceDesc, subresource, 1, 0, &footprint, &rowCount, &rowSize, &totalBytes );

        const D3D12_HEAP_PROPERTIES            readbackHeap = D3D12RHIResourcePreset::heapProperties( D3D12_HEAP_TYPE_READBACK );
        const D3D12_RESOURCE_DESC              bufferDesc   = D3D12RHIResourcePreset::bufferDesc( totalBytes );
        Microsoft::WRL::ComPtr<ID3D12Resource> readback;
        if ( isHrSuccess( _pDevice->_device->CreateCommittedResource( &readbackHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
                                                                      D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS( readback.GetAddressOf() ) ),
                          "CreateCommittedResource" ) == false )
        {
            SW_LOG_ERROR_ID( "D3D12RHIResourceFactory.SubmitTextureReadbackFailedCreateReadback", "submitTextureReadback: failed to create the readback buffer (%# bytes)", totalBytes );
            return RHITransferSubmit::rejected();
        }

        D3D12RHIDevice::D3D12TransferJob job{};
        if ( _pDevice->beginTransferJob( job ) == false )
        {
            SW_LOG_ERROR_ID( "D3D12RHIResourceFactory.SubmitTextureReadbackFailedOpenCopy", "submitTextureReadback: failed to open the copy command list" );
            return RHITransferSubmit::rejected();
        }
        ID3D12GraphicsCommandList* pList = job._list.Get();

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
        // 기록이 끝났다. 즉시 큐에 내고 전송 펜스를 올린다(기다리지 않는다). 읽기 버퍼는 보관표가 쥐고 있다가 `takeTextureReadback` 이 Map 한다.
        // 읽기 버퍼는 읽기마다 만든다(되읽기 링이 없다) — 링이 아니므로 `Busy` 가 없다.
        const uint64 serial = _pDevice->submitTransferJob( std::move( job ), "submitTextureReadback" );
        if ( serial == 0 )
            return RHITransferSubmit::rejected();
        // 원본 텍스처도 복사가 끝나기 전에는 해제되지 않게 전송 시리얼에 매단다(destroyTexture 가 해제를 미룬다).
        _pDevice->recordTextureTransfer( texture, serial );

        D3D12RHIDevice::TextureReadbackRecord record;
        record._readback  = std::move( readback );
        record._footprint = footprint;
        record._layout    = outLayout;
        record._rowCount  = rowCount;
        record._rowSize   = rowSize;
        record._bDepth    = bDepth ? SW_TRUE : SW_FALSE;
        _pDevice->_readbackTable.add( serial, std::move( record ) );
        return _pDevice->_transferSerials.makeAccepted( serial );
    }

    RHIBufferHandle D3D12RHIResourceFactory::createVertexBuffer( const void* pData, uint32 sizeBytes )
    {
        return createUploadBuffer( pData, sizeBytes );
    }

    void D3D12RHIResourceFactory::updateGeometryBufferRegions( RHIBufferHandle buffer, const void* pBaseSource, const RHIBufferCopyRegion* pRegions, uint32 regionCount )
    {
        _pDevice->requireNoOpenRenderPassForUpload( "updateGeometryBufferRegions" );
        const bool bInvalidVertexRegionUpdate = buffer == 0 || pBaseSource == nullptr || pRegions == nullptr || regionCount == 0;
        if ( bInvalidVertexRegionUpdate )
            return;
        ID3D12Resource* pDest = _pDevice->resolveBuffer( buffer );
        if ( pDest == nullptr )
            return;

        // 정점 버퍼는 업로드 힙(createUploadBuffer)이라 복사의 목적지가 될 수 없다. 매핑해 CPU 가 직접 쓴다 — 앞 프레임이 읽는 조각이 아님은
        // 부르는 쪽이 지킨다(IRHIResourceFactory::updateGeometryBufferRegions).
        const uint64      bufferBytes = pDest->GetDesc().Width;
        const D3D12_RANGE noRead{ 0, 0 };
        void*             pMapped{ nullptr };
        if ( isHrSuccess( pDest->Map( 0, &noRead, &pMapped ), "Map" ) == false || pMapped == nullptr )
        {
            SW_LOG_ERROR_ID( "D3D12RHIResourceFactory.UpdateGeometryBufferRegionsFailedMapVertex", "updateGeometryBufferRegions: failed to map the vertex or index buffer" );
            return;
        }
        const uint8* pBase = static_cast<const uint8*>( pBaseSource );
        for ( uint32 regionIndex = 0; regionIndex < regionCount; ++regionIndex )
        {
            const RHIBufferCopyRegion& region = pRegions[regionIndex];
            if ( static_cast<uint64>( region._dstOffset ) + region._sizeBytes > bufferBytes )
            {
                SW_LOG_ERROR_ID( "D3D12RHIResourceFactory.UpdateGeometryBufferRegionsRegionOutsideByte", "updateGeometryBufferRegions: region [%#, +%#) is outside the %#-byte vertex or index buffer; skipped", region._dstOffset, region._sizeBytes, bufferBytes );
                continue;
            }
            Memory::copy( static_cast<uint8*>( pMapped ) + region._dstOffset, pBase + region._srcOffset, region._sizeBytes );
        }
        pDest->Unmap( 0, nullptr );
    }

    RHIBufferHandle D3D12RHIResourceFactory::createIndexBuffer( const void* pData, uint32 sizeBytes )
    {
        // 인덱스 크기는 걸 때(setIndexBuffer) 정한다. 인덱스 버퍼는 구조버퍼가 아니라 정점 버퍼처럼
        // 업로드 힙의 GENERIC_READ 로 둔다. 그 상태가 INDEX_BUFFER 읽기를 포함하므로 전이 없이 걸 수 있다.
        return createUploadBuffer( pData, sizeBytes );
    }

    RHIBufferHandle D3D12RHIResourceFactory::createUploadBuffer( const void* pData, uint32 sizeBytes )
    {
        const bool bInvalidUploadBufferRequest = _pDevice->_device == nullptr || pData == nullptr || sizeBytes == 0;
        if ( bInvalidUploadBufferRequest )
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
            ScopedLock<mutex> lock{ _pDevice->_resourceStateMutex };
            _pDevice->_mapStructuredBufferState.erase( buffer );
        }
        UniqueLock<shared_mutex> registryLock{ _pDevice->_bindlessMutex };
        const auto               mapIt = _pDevice->_mapCbMapped.find( buffer );
        if ( mapIt != _pDevice->_mapCbMapped.end() && mapIt->second != nullptr )
        {
            ID3D12Resource* pResource = _pDevice->resolveBuffer( buffer );
            if ( pResource != nullptr )
                pResource->Unmap( 0, nullptr );
            _pDevice->_mapCbMapped.erase( mapIt );
        }
        _pDevice->_mapCbAlignedSize.erase( buffer );
        _pDevice->_mapStructuredStride.erase( buffer );
        _pDevice->_constantBufferMirror.remove( buffer );
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

    RHIBufferHandle D3D12RHIResourceFactory::createReadbackBuffer( uint32 elementSize, uint32 elementCount )
    {
        uint32     totalBytes{ 0 };
        const bool bInvalidReadbackRequest = _pDevice->_device == nullptr || elementSize == 0 || elementCount == 0 || RHIBufferSizeUtil::computeStructuredBytes( elementSize, elementCount, totalBytes ) == false;
        if ( bInvalidReadbackRequest )
            return 0;
        const D3D12_HEAP_PROPERTIES            readbackHeap = D3D12RHIResourcePreset::heapProperties( D3D12_HEAP_TYPE_READBACK );
        const D3D12_RESOURCE_DESC              bufferDesc   = D3D12RHIResourcePreset::bufferDesc( totalBytes );
        Microsoft::WRL::ComPtr<ID3D12Resource> readback;
        if ( isHrSuccess( _pDevice->_device->CreateCommittedResource( &readbackHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                                      IID_PPV_ARGS( readback.GetAddressOf() ) ),
                          "CreateCommittedResource" ) == false )
            return 0;
        // 읽기 힙 리소스는 COPY_DEST 로 고정이다 — 상태 추적(`_mapStructuredBufferState`)에 올리지 않는다.
        return _pDevice->storeBuffer( readback );
    }

    bool D3D12RHIResourceFactory::readReadbackBuffer( RHIBufferHandle readback, uint32 sizeBytes, void* pDestination )
    {
        ID3D12Resource* pResource   = _pDevice->resolveBuffer( readback );
        const bool      bUnreadable = pResource == nullptr || pDestination == nullptr || sizeBytes == 0;
        if ( bUnreadable )
            return false;
        // 읽기 힙이 아닌 버퍼(업로드 힙 정점 버퍼 등)는 매핑되더라도 읽기 버퍼가 아니다. 네 백엔드가 같이 거절한다.
        D3D12_HEAP_PROPERTIES heapProperties{};
        D3D12_HEAP_FLAGS      heapFlags{};
        const bool            bNotReadback = FAILED( pResource->GetHeapProperties( &heapProperties, &heapFlags ) ) || heapProperties.Type != D3D12_HEAP_TYPE_READBACK;
        if ( bNotReadback )
        {
            SW_LOG_ERROR_ID( "D3D12RHIResourceFactory.ReadReadbackBufferBufferNotReadback", "readReadbackBuffer: buffer %# is not a readback buffer", readback );
            return false;
        }
        const bool bTooLarge = static_cast<uint64>( sizeBytes ) > pResource->GetDesc().Width;
        if ( bTooLarge )
        {
            SW_LOG_ERROR_ID( "D3D12RHIResourceFactory.ReadReadbackBufferBytesPastEnd", "readReadbackBuffer: %# bytes is past the end of the readback buffer", sizeBytes );
            return false;
        }
        void*             pMapped{ nullptr };
        const D3D12_RANGE readRange{ 0, sizeBytes };
        if ( isHrSuccess( pResource->Map( 0, &readRange, &pMapped ), "ID3D12Resource::Map (readback buffer)" ) == false || pMapped == nullptr )
            return false;
        Memory::copy( pDestination, pMapped, sizeBytes );
        const D3D12_RANGE noWrite{ 0, 0 };
        pResource->Unmap( 0, &noWrite );
        return true;
    }

    void D3D12RHIResourceFactory::setTextureDebugName( RHITextureHandle texture, const utf8* pName )
    {
        ID3D12Resource* const pResource = _pDevice->resolveTexture( texture );
        if ( pResource == nullptr || pName == nullptr )
            return;
        // PIX · DRED 페이지 폴트 출력이 이 이름을 보인다(디버그 빌드의 `SW Texture <핸들>` 이름을 첨부 이름으로 덮는다).
        const wstring wideName = StringUtil::utf8ToUTF16( pName );
        pResource->SetName( wideName.c_str() );
    }

    // 길이 유지(긴 함수 정리 기준): 서술 채우기 · 리소스 생성 · 뷰 · 초기 데이터 업로드의 순서가 곧 의미라 나누지 않는다.
    RHITextureHandle D3D12RHIResourceFactory::createTexture2D( const RHITextureDesc& desc )
    {
        if ( isRHITextureShapeValid( desc ) == false )
        {
            SW_LOG_ERROR_ID( "D3D12RHIResourceFactory.CreateTexture2DDimensionSlicesX", "createTexture2D: dimension %# with %# slices (%#x%#) is not a valid texture shape", static_cast<uint32>( desc._dimension ),
                             desc._arraySize, desc._width, desc._height );
            return 0;
        }
        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        const bool        bDepth      = desc._bIsDepthStencil != SW_FALSE;
        const DXGI_FORMAT typelessFmt = bDepth ? DXGI_FORMAT_R24G8_TYPELESS : toDXGIFormat( desc._format );
        const DXGI_FORMAT dsvFmt      = toDXGIFormat( constant::render::kDepthStencilFormat );
        const DXGI_FORMAT colorFmt    = toDXGIFormat( desc._format );

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
        if ( isHrSuccess( _pDevice->_device->CreateCommittedResource( &heapProps, D3D12_HEAP_FLAG_NONE, &resourceDesc,
                                                                      D3D12_RESOURCE_STATE_COMMON, pClearValue, IID_PPV_ARGS( texture.GetAddressOf() ) ),
                          "CreateCommittedResource" ) == false )
            return 0;

        const RHITextureHandle                 handle  = _pDevice->storeTexture( texture, RHIMemoryTracker::classifyTexture( desc ) );
        ID3D12Resource*                        pNative = _pDevice->resolveTexture( handle );
        D3D12RHIDevice::OffscreenTextureRecord record{};
        record._state      = D3D12_RESOURCE_STATE_COMMON;
        record._format     = bDepth ? dsvFmt : colorFmt;
        record._width      = desc._width;
        record._height     = desc._height;
        record._arraySize  = desc._arraySize;
        record._dimension  = desc._dimension;
        record._bHasRTV    = SW_FALSE;
        record._bHasDSV    = SW_FALSE;
        record._reserved   = 0;
        const bool bSliced = desc._dimension != RHITextureDimension::Texture2D;

        _pDevice->requireRegistryMutableNow( "createTexture2D" );

        // 오프스크린 레코드와 디스크립터 프리리스트는 `transitionTexture` 가 기록 중에 읽는 것과
        // 같은 자료다. 슬롯 배정부터 맵 삽입까지를 그 락 안에서 끝낸다. 읽는 쪽만 잠그면
        // 생성/파괴가 맵을 리해시할 때 읽는 쪽이 무효한 참조를 잡는다.
        ScopedLock<mutex> offscreenLock{ _pDevice->_resourceStateMutex };

        for ( uint32 slice = 0; pNative != nullptr && desc._bIsRenderTarget && _pDevice->_rtvHeap != nullptr && slice < desc._arraySize; ++slice )
        {
            uint32     rtvSlot{ 0 };
            const bool bReused = _pDevice->_freeOffscreenRTV.tryPop( rtvSlot );
            if ( bReused == false && _pDevice->_nextOffscreenRTVIndex < D3D12RHIDevice::kMaxOffscreenRTVs )
                rtvSlot = _pDevice->_nextOffscreenRTVIndex++;
            else if ( bReused == false )
            {
                // 고갈은 로그로 남긴다. 유효한 핸들이 돌아오는데 RTV 가 없으므로, 조용히 넘어가면
                // 나중에 beginRenderPass 가 이유 없이 아무것도 안 그리는 것처럼 보인다.
                rtvSlot = D3D12RHIDevice::kMaxOffscreenRTVs;
                SW_LOG_ERROR_ID( "D3D12RHIResourceFactory.OffscreenRTVDescriptorsExhausted", "Offscreen RTV descriptors exhausted (max %#) - this texture cannot be used as a render target.",
                                 static_cast<uint32>( D3D12RHIDevice::kMaxOffscreenRTVs ) );
            }
            if ( rtvSlot >= D3D12RHIDevice::kMaxOffscreenRTVs )
                break;
            const uint32                  rtvIndex  = _pDevice->_swapChain.getBufferCount() + rtvSlot;
            D3D12_CPU_DESCRIPTOR_HANDLE   rtvHandle = _pDevice->getOffscreenRTVHandle( rtvIndex );
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
                record._bHasRTV   = SW_TRUE;
            }
            else
                record._listExtraRTVIndex.push_back( rtvIndex );
        }

        for ( uint32 slice = 0; pNative != nullptr && bDepth && _pDevice->_dsvHeap != nullptr && slice < desc._arraySize; ++slice )
        {
            uint32     dsvSlot{ 0 };
            const bool bReused = _pDevice->_freeOffscreenDSV.tryPop( dsvSlot );
            if ( bReused == false && _pDevice->_nextOffscreenDSVIndex < D3D12RHIDevice::kMaxOffscreenDSVs )
                dsvSlot = _pDevice->_nextOffscreenDSVIndex++;
            else if ( bReused == false )
            {
                dsvSlot = D3D12RHIDevice::kMaxOffscreenDSVs;
                SW_LOG_ERROR_ID( "D3D12RHIResourceFactory.OffscreenDSVDescriptorsExhausted", "Offscreen DSV descriptors exhausted (max %#) - this texture cannot be used as depth.",
                                 static_cast<uint32>( D3D12RHIDevice::kMaxOffscreenDSVs ) );
            }
            if ( dsvSlot >= D3D12RHIDevice::kMaxOffscreenDSVs )
                break;
            const D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = _pDevice->getOffscreenDSVHandle( dsvSlot );
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
                record._bHasDSV   = SW_TRUE;
            }
            else
                record._listExtraDSVIndex.push_back( dsvSlot );
        }

        _pDevice->_mapOffscreenTexture[handle] = record;
        return handle;
    }

    void D3D12RHIResourceFactory::destroyTexture( RHITextureHandle texture )
    {
        if ( texture == 0 )
            return;
        _pDevice->requireRegistryMutableNow( "destroyTexture" );
        {
            ScopedLock<mutex> offscreenLock{ _pDevice->_resourceStateMutex };

            auto it = _pDevice->_mapOffscreenTexture.find( texture );
            if ( it != _pDevice->_mapOffscreenTexture.end() )
            {
                const uint32 offscreenRTVBase = _pDevice->_swapChain.getBufferCount();
                if ( it->second._bHasRTV != SW_FALSE && it->second._rtvIndex >= offscreenRTVBase )
                    _pDevice->_freeOffscreenRTV.push( it->second._rtvIndex - offscreenRTVBase );
                if ( it->second._bHasDSV != SW_FALSE )
                    _pDevice->_freeOffscreenDSV.push( it->second._dsvIndex );
                for ( const uint32 rtvIndex : it->second._listExtraRTVIndex )
                {
                    _pDevice->_freeOffscreenRTV.push( rtvIndex - offscreenRTVBase );
                }
                for ( const uint32 dsvIndex : it->second._listExtraDSVIndex )
                {
                    _pDevice->_freeOffscreenDSV.push( dsvIndex );
                }
                _pDevice->_mapOffscreenTexture.erase( it );
            }
        }
        Microsoft::WRL::ComPtr<ID3D12Resource> owned;
        if ( _pDevice->_gpuTextures.take( texture, owned ) == false )
            return;

        // 이 텍스처에 발급한 bindless 번호는 파괴가 모두 놓는다(DX11 · GL · Vulkan 과 같은 규칙 — IRHIResourceFactory::registerBindlessTexture). 남겨 두면 등록한 쪽이 해제를
        // 잊거나 늦게 한 번호가 새고, 늦은 해제가 이미 다른 리소스에 다시 발급된 번호를 놓는다.
        vector<RHIDescriptorIndex> listFreedIndex;
        {
            UniqueLock<shared_mutex> lock{ _pDevice->_bindlessMutex };
            for ( size_t index = 0; index < _pDevice->_listRegisteredBindless.size(); ++index )
            {
                D3D12RHIDevice::BindlessResourceRecord& record = _pDevice->_listRegisteredBindless[index];
                if ( record._texture != texture )
                    continue;
                record._resource.Reset();
                record._texture = 0;
                listFreedIndex.push_back( static_cast<RHIDescriptorIndex>( index ) );
            }
        }
        for ( const RHIDescriptorIndex index : listFreedIndex )
        {
            deferFreeBindlessIndex( index );
        }

        // 전송 중인 텍스처의 해제는 전송이 끝난 뒤로 미룬다(전송 계약 6). 같은 핸들이 다시 쓰이지 않도록 표의 칸도 지운다.
        const uint64 lastTransferSerial = _pDevice->findTextureTransferSerial( texture );
        _pDevice->eraseTextureTransfer( texture );
        const bool bUnfinishedTransfer = lastTransferSerial != 0 && _pDevice->_transferSerials.isComplete( lastTransferSerial ) == false;
        if ( bUnfinishedTransfer )
        {
            _pDevice->releaseTextureAfterTransfer( std::move( owned ), texture, lastTransferSerial );
            return;
        }
        _pDevice->releaseTrackedResourceDeferred( std::move( owned ), RHIMemoryKey::makeTexture( texture ) );
    }

    D3D12RHIResourceFactory::BindlessHandleSet D3D12RHIResourceFactory::bindlessHandlesAt( RHIDescriptorIndex index ) const
    {
        BindlessHandleSet handle{};
        handle._cpu = _pDevice->_cbvHeap->GetCPUDescriptorHandleForHeapStart();
        handle._cpu.ptr += static_cast<SIZE_T>( index ) * _pDevice->_cbvDescriptorSize;
        handle._offline = _pDevice->offlineDescriptorAt( index );
        return handle;
    }

    RHIDescriptorIndex D3D12RHIResourceFactory::acquireBindlessIndex( UniqueLock<shared_mutex>& lock )
    {
        (void)lock;
        // 잠금은 부르는 쪽의 것이다(선언의 SW_REQUIRES_LOCK). 인덱스를 집는 것과 그 자리에 뷰를 만드는 것이 한 임계 구역이어야 한다.

        // 반납된 슬롯이 있으면 그것부터 쓴다(펜스 뒤에 회수된 것들이다). 새 인덱스는 레지스트리 크기가 아니라 힙 카운터다 —
        // SRV/CBV 와 UAV 가 레지스트리 둘에 나뉘어도 힙 인덱스 공간은 하나라서다.
        RHIDescriptorIndex reused{ kInvalidDescriptorIndex };
        if ( _pDevice->_freeBindless.tryPop( reused ) )
            return reused;

        if ( _pDevice->_allocatedDescriptorsCount >= D3D12RHIDevice::kBindlessDescriptorCapacity )
        {
            SW_LOG_ERROR_ID( "D3D12RHIResourceFactory.ShaderVisibleDescriptorHeap", "Shader visible descriptor heap overflow! Max: %#", D3D12RHIDevice::kBindlessDescriptorCapacity );
            return kInvalidDescriptorIndex;
        }
        return _pDevice->_allocatedDescriptorsCount++;
    }

    bool D3D12RHIResourceFactory::isTextureReadyToSample( RHITextureHandle texture )
    {
        return _pDevice->resolveTexture( texture ) != nullptr && _pDevice->isTextureFirstUploadComplete( texture );
    }

    RHIDescriptorIndex D3D12RHIResourceFactory::registerBindlessTexture( RHITextureHandle texture )
    {
        _pDevice->requireRegistryMutableNow( "registerBindlessTexture" );
        if ( texture == 0 || _pDevice->_cbvHeap == nullptr )
            return kInvalidDescriptorIndex;

        auto* pResource = _pDevice->resolveTexture( texture );
        if ( pResource == nullptr )
            return kInvalidDescriptorIndex;
        // 끝나지 않은 업로드가 남은 텍스처는 등록하지 않는다(소비자는 pollTransfer 가 Complete 를 준 뒤에 등록한다 — IRHIResourceFactory 전송 계약 머리말).
        if ( _pDevice->isTextureReadyToRegister( texture ) == false )
            return kInvalidDescriptorIndex;
        // 셰이더의 bindless 텍스처 배열은 Texture2D[] 다 — 배열 · 큐브 뷰를 넣으면 셰이더가 읽지 못한다(RHITextureDimension 참고).
        {
            ScopedLock<mutex> stateLock{ _pDevice->_resourceStateMutex };
            const auto        it = _pDevice->_mapOffscreenTexture.find( texture );
            if ( it != _pDevice->_mapOffscreenTexture.end() && it->second._dimension != RHITextureDimension::Texture2D )
            {
                SW_LOG_ERROR_ID( "D3D12RHIResourceFactory.RegisterBindlessTextureTextureTextureOnly", "registerBindlessTexture: texture %# is a %# texture — only Texture2D can be bound to the bindless texture table yet",
                                 texture, static_cast<uint32>( it->second._dimension ) );
                return kInvalidDescriptorIndex;
            }
        }

        UniqueLock<shared_mutex> lock{ _pDevice->_bindlessMutex };
        const RHIDescriptorIndex index = acquireBindlessIndex( lock );
        if ( index == kInvalidDescriptorIndex )
            return kInvalidDescriptorIndex;

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.ViewDimension                 = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping       = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MostDetailedMip     = 0;
        srvDesc.Texture2D.MipLevels           = pResource->GetDesc().MipLevels;
        srvDesc.Texture2D.PlaneSlice          = 0;
        srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

        const DXGI_FORMAT resourceFormat = pResource->GetDesc().Format;
        if ( resourceFormat == DXGI_FORMAT_R24G8_TYPELESS || resourceFormat == DXGI_FORMAT_D24_UNORM_S8_UINT )
            srvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        else
            srvDesc.Format = resourceFormat;

        const BindlessHandleSet handle = bindlessHandlesAt( index );

        _pDevice->_device->CreateShaderResourceView( pResource, &srvDesc, handle._cpu );
        _pDevice->_device->CreateShaderResourceView( pResource, &srvDesc, handle._offline );

        writeBindlessRecord( BindlessRegistry::ShaderResource, index, pResource, handle, 0, texture );
        return index;
    }

    RHIDescriptorIndex D3D12RHIResourceFactory::registerBindlessBufferSRV( RHIBufferHandle buffer )
    {
        _pDevice->requireRegistryMutableNow( "registerBindlessBufferSRV" );
        if ( buffer == 0 || _pDevice->_cbvHeap == nullptr )
            return kInvalidDescriptorIndex;

        auto* pResource = _pDevice->resolveBuffer( buffer );
        if ( pResource == nullptr )
            return kInvalidDescriptorIndex;

        UniqueLock<shared_mutex> lock{ _pDevice->_bindlessMutex };
        const RHIDescriptorIndex index = acquireBindlessIndex( lock );
        if ( index == kInvalidDescriptorIndex )
            return kInvalidDescriptorIndex;

        const BindlessHandleSet handle = bindlessHandlesAt( index );

        // 구조버퍼면 StructuredBuffer SRV 다(셰이더의 StructuredBuffer<T> name[] 이 이 힙 인덱스로 읽는다).
        // 그 밖(상수 버퍼 링)이면 CBV 다.
        const auto strideIt = _pDevice->_mapStructuredStride.find( buffer );
        if ( strideIt != _pDevice->_mapStructuredStride.end() && strideIt->second > 0 )
        {
            const UINT                      stride = strideIt->second;
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.ViewDimension              = D3D12_SRV_DIMENSION_BUFFER;
            srvDesc.Shader4ComponentMapping    = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Format                     = DXGI_FORMAT_UNKNOWN;
            srvDesc.Buffer.FirstElement        = 0;
            srvDesc.Buffer.NumElements         = static_cast<UINT>( pResource->GetDesc().Width ) / stride;
            srvDesc.Buffer.StructureByteStride = stride;
            srvDesc.Buffer.Flags               = D3D12_BUFFER_SRV_FLAG_NONE;
            // 온라인 힙과 오프라인 힙에 각각 한 번씩.
            _pDevice->_device->CreateShaderResourceView( pResource, &srvDesc, handle._cpu );
            _pDevice->_device->CreateShaderResourceView( pResource, &srvDesc, handle._offline );

            writeBindlessRecord( BindlessRegistry::ShaderResource, index, pResource, handle, buffer, 0 );
            return index;
        }

        D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc{};
        cbvDesc.BufferLocation = pResource->GetGPUVirtualAddress();

        // 상수버퍼 맵 조회는 위에서 쥔 배타 락(`lock`) 안이다. 그 맵은 같은 `_bindlessMutex` 가 지킨다.
        const auto sizeIt = _pDevice->_mapCbAlignedSize.find( buffer );
        if ( sizeIt != _pDevice->_mapCbAlignedSize.end() )
        {
            cbvDesc.BufferLocation += static_cast<UINT64>( _pDevice->_frameRing.currentIndex() ) * sizeIt->second;
            cbvDesc.SizeInBytes = sizeIt->second;
        }
        else
        {
            // 링이 아닌 버퍼: CBV 크기는 256 바이트로 정렬돼야 하고 리소스 폭을 넘으면 안 된다.
            const UINT width = static_cast<UINT>( pResource->GetDesc().Width );
            // 256 은 CBV 크기 정렬(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT)이다. 텍스처 행 정렬
            // (D3D12_TEXTURE_DATA_PITCH_ALIGNMENT)도 256 이지만 이름만 같은 별개 값이라 그 상수를 쓰지 않는다.
            const UINT aligned  = MathUtil::align( width, 256u );
            cbvDesc.SizeInBytes = ( aligned <= width ) ? aligned : ( width & ~255u );
            if ( cbvDesc.SizeInBytes == 0 )
            {
                // 집은 인덱스를 돌려준다. 그냥 반환하면 256 바이트보다 좁은 버퍼를 등록할 때마다 힙 슬롯이 하나씩 샌다.
                // 아직 뷰를 만들지 않았으므로 펜스를 기다릴 것 없이 바로 프리리스트로 간다(잠금은 쥐고 있다).
                _pDevice->_freeBindless.push( index );
                return kInvalidDescriptorIndex;
            }
        }

        _pDevice->_device->CreateConstantBufferView( &cbvDesc, handle._cpu );
        _pDevice->_device->CreateConstantBufferView( &cbvDesc, handle._offline );

        writeBindlessRecord( BindlessRegistry::ShaderResource, index, pResource, handle, buffer, 0 );
        return index;
    }

    void D3D12RHIResourceFactory::unregisterBindlessResource( RHIDescriptorIndex index )
    {
        _pDevice->requireRegistryMutableNow( "unregisterBindlessResource" );
        releaseBindlessSlot( index );
    }

    void D3D12RHIResourceFactory::unregisterBindlessTexture( RHIDescriptorIndex index )
    {
        // DX12 는 텍스처와 버퍼가 같은 셰이더 가시 힙을 나눠 쓰므로 인덱스 공간이 하나다.
        _pDevice->requireRegistryMutableNow( "unregisterBindlessTexture" );
        releaseBindlessSlot( index );
    }

    void D3D12RHIResourceFactory::releaseBindlessRecord( BindlessRegistry registry, RHIDescriptorIndex index )
    {
        const bool bUAV = ( registry == BindlessRegistry::UnorderedAccess );

        UniqueLock<shared_mutex>                        lock{ _pDevice->_bindlessMutex };
        vector<D3D12RHIDevice::BindlessResourceRecord>& listRegistry = bUAV ? _pDevice->_listRegisteredUAV : _pDevice->_listRegisteredBindless;
        if ( index >= listRegistry.size() )
            return;

        D3D12RHIDevice::BindlessResourceRecord& record = listRegistry[index];
        // 이미 빈 슬롯을 다시 프리리스트에 넣으면 같은 인덱스가 두 리소스에 발급된다.
        const bool bSlotAlreadyFree = record._resource == nullptr && record._buffer == 0 && record._texture == 0;
        if ( bSlotAlreadyFree )
        {
            SW_LOG_ERROR_ID( "D3D12RHIResourceFactory.IndexAlreadyFreeIgnoring", "%# index %# is already free; ignoring the duplicate release.",
                             bUAV ? "Bindless UAV" : "Bindless", index );
            return;
        }

        record._resource = nullptr;
        record._buffer   = 0;
        record._texture  = 0;
        deferFreeBindlessIndex( index ); // 힙 인덱스 공간이 하나라 프리리스트도 하나다
    }

    void D3D12RHIResourceFactory::writeBindlessRecord( BindlessRegistry registry, RHIDescriptorIndex index, ID3D12Resource* pResource,
                                                       const BindlessHandleSet& handle, RHIBufferHandle buffer, RHITextureHandle texture )
    {
        vector<D3D12RHIDevice::BindlessResourceRecord>& listRegistry =
            ( registry == BindlessRegistry::UnorderedAccess ) ? _pDevice->_listRegisteredUAV : _pDevice->_listRegisteredBindless;
        if ( index >= listRegistry.size() )
            listRegistry.resize( index + 1 );

        D3D12RHIDevice::BindlessResourceRecord& record = listRegistry[index];
        record                                         = { pResource, handle._cpu, handle._offline };
        record._buffer                                 = buffer;
        record._texture                                = texture;
    }

    void D3D12RHIResourceFactory::releaseBindlessSlot( RHIDescriptorIndex index )
    {
        releaseBindlessRecord( BindlessRegistry::ShaderResource, index );
    }

    void D3D12RHIResourceFactory::deferFreeBindlessIndex( RHIDescriptorIndex index )
    {
        // 인덱스는 GPU 가 이 프레임까지의 커맨드를 다 읽은 뒤에야 재사용한다(언리얼의 지연 디스크립터 해제와 같다).
        // 즉시 프리리스트에 넣으면 같은 프레임에 등록된 새 리소스가 그 자리를 받아, 아직 실행 중인 리스트가 새 리소스를 읽는다.
        D3D12RHIDevice* pDevice = _pDevice;
        _pDevice->_releaseQueue.enqueueGPURelease( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, [pDevice, index]()
        {
            UniqueLock<shared_mutex> lock{ pDevice->_bindlessMutex };
            pDevice->_freeBindless.push( index );
        } ),
                                                   _pDevice->_fenceValue );
    }

    RHIDescriptorIndex D3D12RHIResourceFactory::registerBindlessBufferUAV( RHIBufferHandle buffer )
    {
        _pDevice->requireRegistryMutableNow( "registerBindlessBufferUAV" );
        if ( buffer == 0 || _pDevice->_cbvHeap == nullptr )
            return kInvalidDescriptorIndex;

        ID3D12Resource* pResource = _pDevice->resolveBuffer( buffer );
        if ( pResource == nullptr || pResource->GetDesc().Width < 4 )
            return kInvalidDescriptorIndex;

        // UAV 도 SRV/CBV 와 **같은 힙 인덱스 공간**을 쓴다. 셰이더가 RWStructuredBuffer<T> name[] 을 이 인덱스로
        // 고르고, 루트 시그니처의 UAV 무제한 범위가 힙 시작(offset 0)을 가리키므로 인덱스 = 힙 슬롯이어야 한다
        // (UAV 목록 안의 순번을 돌려주면 안 된다).
        UniqueLock<shared_mutex> lock{ _pDevice->_bindlessMutex };
        const RHIDescriptorIndex index = acquireBindlessIndex( lock );
        if ( index == kInvalidDescriptorIndex )
            return kInvalidDescriptorIndex;

        const BindlessHandleSet handle = bindlessHandlesAt( index );

        // 구조버퍼면 StructuredBuffer UAV, 아니면 RAW UAV(RWByteAddressBuffer: R32_TYPELESS + RAW, stride 0)다.
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
        uavDesc.ViewDimension       = D3D12_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.FirstElement = 0;
        const auto strideIt         = _pDevice->_mapStructuredStride.find( buffer );
        if ( strideIt != _pDevice->_mapStructuredStride.end() && strideIt->second > 0 )
        {
            uavDesc.Format                     = DXGI_FORMAT_UNKNOWN;
            uavDesc.Buffer.NumElements         = static_cast<UINT>( pResource->GetDesc().Width ) / strideIt->second;
            uavDesc.Buffer.StructureByteStride = strideIt->second;
            uavDesc.Buffer.Flags               = D3D12_BUFFER_UAV_FLAG_NONE;
        }
        else
        {
            uavDesc.Format                     = DXGI_FORMAT_R32_TYPELESS;
            uavDesc.Buffer.NumElements         = static_cast<UINT>( pResource->GetDesc().Width / 4 );
            uavDesc.Buffer.StructureByteStride = 0;
            uavDesc.Buffer.Flags               = D3D12_BUFFER_UAV_FLAG_RAW;
        }
        _pDevice->_device->CreateUnorderedAccessView( pResource, nullptr, &uavDesc, handle._cpu );
        _pDevice->_device->CreateUnorderedAccessView( pResource, nullptr, &uavDesc, handle._offline );

        writeBindlessRecord( BindlessRegistry::UnorderedAccess, index, pResource, handle, buffer, 0 );
        return index;
    }

    RHIDescriptorIndex D3D12RHIResourceFactory::registerBindlessTextureUAV( RHITextureHandle texture )
    {
        _pDevice->requireRegistryMutableNow( "registerBindlessTextureUAV" );
        if ( texture == 0 || _pDevice->_cbvHeap == nullptr )
            return kInvalidDescriptorIndex;
        ID3D12Resource* pResource = _pDevice->resolveTexture( texture );
        if ( pResource == nullptr || ( pResource->GetDesc().Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS ) == 0 )
            return kInvalidDescriptorIndex;

        // 텍스처 UAV 도 같은 힙 인덱스 공간이다. 셰이더가 RWTexture2D g_SwBindlessRWTex2D[] (u0 space1) 을 이 인덱스로 고른다.
        UniqueLock<shared_mutex> lock{ _pDevice->_bindlessMutex };
        const RHIDescriptorIndex index = acquireBindlessIndex( lock );
        if ( index == kInvalidDescriptorIndex )
            return kInvalidDescriptorIndex;

        const BindlessHandleSet handle = bindlessHandlesAt( index );

        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
        uavDesc.Format               = pResource->GetDesc().Format;
        uavDesc.ViewDimension        = D3D12_UAV_DIMENSION_TEXTURE2D;
        uavDesc.Texture2D.MipSlice   = 0;
        uavDesc.Texture2D.PlaneSlice = 0;
        _pDevice->_device->CreateUnorderedAccessView( pResource, nullptr, &uavDesc, handle._cpu );
        _pDevice->_device->CreateUnorderedAccessView( pResource, nullptr, &uavDesc, handle._offline );

        writeBindlessRecord( BindlessRegistry::UnorderedAccess, index, pResource, handle, 0, texture );
        return index;
    }

    void D3D12RHIResourceFactory::unregisterBindlessUAV( RHIDescriptorIndex index )
    {
        _pDevice->requireRegistryMutableNow( "unregisterBindlessUAV" );
        releaseBindlessRecord( BindlessRegistry::UnorderedAccess, index );
    }

    RHIPipelineStateHandle D3D12RHIResourceFactory::createPipelineState( const RHIPipelineStateDesc& desc )
    {
        Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
        uint32                                      vertexSlotMask{ 0 };

        // 디스크립터 해석(진입점 기본값, define, 뎁스 전용 판정, RT 수)은 RHIShaderRequest 하나가 한다. 백엔드는 받기만 한다.
        const RHIGraphicsShaderRequest request         = RHIShaderRequest::resolveGraphics( desc, ShaderTargetFormat::DXIL_D3D12 );
        const bool                     bHasPixelShader = request._bHasPixelShader != SW_FALSE;
        ShaderCompileResult            vsResult{};
        ShaderCompileResult            psResult{};
        if ( RHIShaderRequest::compileGraphics( request, vsResult, psResult ) )
        {
            // 입력 레이아웃은 메시 정점 형식의 공용 테이블에서 셰이더가 읽는 속성만 골라 만든다(`fillDXGIInputElements` — DX11 과 같은 함수, Vulkan · GL 도 같은 규칙).
            // 정점 입력이 없는 셰이더(SV_VertexID 로 정점을 만든다)는 빈 레이아웃이라 정점 버퍼 없이 그린다.
            const uint32             consumedMask = RHIShaderRequest::computeConsumedVertexAttributeMask( vsResult._bytecode, ShaderTargetFormat::DXIL_D3D12 );
            D3D12_INPUT_ELEMENT_DESC arrInputElement[constant::render::kVertexAttributeCount]{};
            const uint32             elementCount = fillDXGIInputElements<D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA>( desc._meshVertexFormat, consumedMask, arrInputElement );
            vertexSlotMask                        = RHIShaderRequest::toVertexSlotMask( consumedMask );

            D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
            psoDesc.InputLayout    = { elementCount > 0 ? arrInputElement : nullptr, elementCount };
            psoDesc.pRootSignature = _pDevice->_rootSignature.Get();
            psoDesc.VS             = { vsResult._bytecode.data(), vsResult._bytecode.size() };
            ShaderCompileResult gsResult{};
            ShaderCompileResult hsResult{};
            ShaderCompileResult dsResult{};
            if ( RHIShaderRequest::compileOptionalStages( request, gsResult, hsResult, dsResult ) == false )
            {
                SW_LOG_ERROR_ID( "D3D12RHIResourceFactory.CreatePipelineStateOptionalStageFailed", "createPipelineState: optional stage compile failed (%#)", desc._vertexShaderPath.c_str() );
                return 0;
            }
            if ( request._bHasGeometryShader != SW_FALSE )
                psoDesc.GS = { gsResult._bytecode.data(), gsResult._bytecode.size() };
            if ( request._bHasTessellation != SW_FALSE )
            {
                psoDesc.HS = { hsResult._bytecode.data(), hsResult._bytecode.size() };
                psoDesc.DS = { dsResult._bytecode.data(), dsResult._bytecode.size() };
            }
            if ( bHasPixelShader )
                psoDesc.PS = { psResult._bytecode.data(), psResult._bytecode.size() };
            const RHIRenderState& renderState = desc._renderState;
            // 다른 백엔드와 같이 채우기 모드를 읽는다. SOLID 로 고정하면 Wireframe 요청이 DX12 에서만 조용히 솔리드가 된다.
            psoDesc.RasterizerState.FillMode              = ( renderState._fillMode == RHIFillMode::Wireframe )
                                                              ? D3D12_FILL_MODE_WIREFRAME
                                                              : D3D12_FILL_MODE_SOLID;
            psoDesc.RasterizerState.CullMode              = ( renderState._cullMode == RHICullMode::Front )
                                                              ? D3D12_CULL_MODE_FRONT
                                                              : ( ( renderState._cullMode == RHICullMode::Back ) ? D3D12_CULL_MODE_BACK : D3D12_CULL_MODE_NONE );
            psoDesc.RasterizerState.FrontCounterClockwise = renderState._frontFace == RHIFrontFace::CounterClockwise ? TRUE : FALSE;
            // 기본은 가까운 · 먼 평면에서 자른다 — DX11(DepthClipEnable) · Vulkan(depthClampEnable 끔) · GL(GL_DEPTH_CLAMP 끔)과 같다. 0 으로 두면 DX12 만 평면 밖 기하를 깊이 1 로 눌러 그린다
            // (psoDesc{} 의 기본값이 FALSE 다). 시야 거리 설정(gv_viewDistanceScale)이 먼 평면을 당겨도 DX12 에서만 먼 기하가 남았다(useroptions distance 0.307, 나머지 0.665).
            psoDesc.RasterizerState.DepthClipEnable = renderState._bEnableDepthClip != SW_FALSE ? TRUE : FALSE;
            // 깊이 바이어스 — 다른 백엔드와 같은 상수 · 기울기 두 칸이다(RHIFixedRenderState::_depthBias).
            psoDesc.RasterizerState.DepthBias            = renderState._depthBias;
            psoDesc.RasterizerState.SlopeScaledDepthBias = renderState._slopeScaledDepthBias;
            psoDesc.SampleMask                           = MathUtil::kMaxUInt32;
            psoDesc.PrimitiveTopologyType                = request._bHasTessellation != SW_FALSE ? D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH : D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            // 뎁스 전용은 RT 가 0 개다. 1 로 두고 R8G8B8A8 을 선언하면 실제로는 DSV 만 바인딩되는 것과 어긋난다.
            psoDesc.NumRenderTargets = request._numRenderTargets;
            if ( psoDesc.NumRenderTargets > 8 )
                psoDesc.NumRenderTargets = 8;
            psoDesc.BlendState.IndependentBlendEnable = TRUE; // 꺼 두면 0 번 값이 모든 타깃에 간다
            // 쓰지 않는 칸도 유효한 계수여야 한다(0 은 D3D12_BLEND 값이 아니다). 쓰기 마스크는 0 이다 — 타깃이 없다.
            for ( D3D12_RENDER_TARGET_BLEND_DESC& unusedTarget : psoDesc.BlendState.RenderTarget )
            {
                unusedTarget.SrcBlend       = D3D12_BLEND_ONE;
                unusedTarget.DestBlend      = D3D12_BLEND_ZERO;
                unusedTarget.BlendOp        = D3D12_BLEND_OP_ADD;
                unusedTarget.SrcBlendAlpha  = D3D12_BLEND_ONE;
                unusedTarget.DestBlendAlpha = D3D12_BLEND_ZERO;
                unusedTarget.BlendOpAlpha   = D3D12_BLEND_OP_ADD;
                unusedTarget.LogicOp        = D3D12_LOGIC_OP_NOOP;
            }
            for ( UINT rtvIndex = 0; rtvIndex < psoDesc.NumRenderTargets; ++rtvIndex )
            {
                auto&                      rtBlend = psoDesc.BlendState.RenderTarget[rtvIndex];
                const RHIBlendTargetState& source  = renderState._arrBlendTarget[rtvIndex < kMaxColorAttachments ? rtvIndex : 0u];
                rtBlend.RenderTargetWriteMask      = static_cast<UINT8>( source._colorWriteMask ); // 비트가 같다(RHIColorWriteMask)
                psoDesc.RTVFormats[rtvIndex]       = toDXGIFormat( desc._arrRTVFormat[rtvIndex] );
                rtBlend.BlendEnable                = source._bEnableBlend != SW_FALSE ? TRUE : FALSE;
                rtBlend.SrcBlend                   = RHIDxRenderState::toBlend<D3D12_BLEND>( source._srcColor );
                rtBlend.DestBlend                  = RHIDxRenderState::toBlend<D3D12_BLEND>( source._dstColor );
                rtBlend.BlendOp                    = RHIDxRenderState::toBlendOp<D3D12_BLEND_OP>( source._colorOp );
                rtBlend.SrcBlendAlpha              = RHIDxRenderState::toBlend<D3D12_BLEND>( source._srcAlpha );
                rtBlend.DestBlendAlpha             = RHIDxRenderState::toBlend<D3D12_BLEND>( source._dstAlpha );
                rtBlend.BlendOpAlpha               = RHIDxRenderState::toBlendOp<D3D12_BLEND_OP>( source._alphaOp );
            }
            if ( renderState._bEnableDepthTest != SW_FALSE )
            {
                psoDesc.DepthStencilState.DepthEnable    = TRUE;
                psoDesc.DepthStencilState.DepthWriteMask = ( renderState._bEnableDepthWrite != SW_FALSE )
                                                             ? D3D12_DEPTH_WRITE_MASK_ALL
                                                             : D3D12_DEPTH_WRITE_MASK_ZERO;
                psoDesc.DepthStencilState.DepthFunc      = RHIDxRenderState::toCompare<D3D12_COMPARISON_FUNC>( renderState._depthCompare );
                psoDesc.DSVFormat                        = toDXGIFormat( desc._depthStencilFormat );
                // 스텐실 참조 값은 PSO 밖의 상태다 — 레코드에 두고 PSO 를 걸 때 함께 건다.
                psoDesc.DepthStencilState.StencilEnable                = renderState._bEnableStencil != SW_FALSE ? TRUE : FALSE;
                psoDesc.DepthStencilState.StencilReadMask              = renderState._stencilReadMask;
                psoDesc.DepthStencilState.StencilWriteMask             = renderState._stencilWriteMask;
                psoDesc.DepthStencilState.FrontFace.StencilFailOp      = RHIDxRenderState::toStencilOp<D3D12_STENCIL_OP>( renderState._stencilFrontFail );
                psoDesc.DepthStencilState.FrontFace.StencilDepthFailOp = RHIDxRenderState::toStencilOp<D3D12_STENCIL_OP>( renderState._stencilFrontDepthFail );
                psoDesc.DepthStencilState.FrontFace.StencilPassOp      = RHIDxRenderState::toStencilOp<D3D12_STENCIL_OP>( renderState._stencilFrontPass );
                psoDesc.DepthStencilState.FrontFace.StencilFunc        = RHIDxRenderState::toCompare<D3D12_COMPARISON_FUNC>( renderState._stencilFrontCompare );
                psoDesc.DepthStencilState.BackFace.StencilFailOp       = RHIDxRenderState::toStencilOp<D3D12_STENCIL_OP>( renderState._stencilBackFail );
                psoDesc.DepthStencilState.BackFace.StencilDepthFailOp  = RHIDxRenderState::toStencilOp<D3D12_STENCIL_OP>( renderState._stencilBackDepthFail );
                psoDesc.DepthStencilState.BackFace.StencilPassOp       = RHIDxRenderState::toStencilOp<D3D12_STENCIL_OP>( renderState._stencilBackPass );
                psoDesc.DepthStencilState.BackFace.StencilFunc         = RHIDxRenderState::toCompare<D3D12_COMPARISON_FUNC>( renderState._stencilBackCompare );
            }
            else
            {
                psoDesc.DepthStencilState.DepthEnable    = FALSE;
                psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
                psoDesc.DSVFormat                        = DXGI_FORMAT_UNKNOWN;
            }
            psoDesc.SampleDesc.Count = 1;

            // 실패를 조용히 삼키지 않는다. PSO 가 null 이면 드로우가 아무 흔적 없이 사라진다. 루트 시그니처와 셰이더가 맞지 않으면
            // 그렇게 숨는다. 디버그 레이어 메시지를 바로 비워 원인이 같은 줄에 나오게 한다.
            const HRESULT hrPSO = _pDevice->_device->CreateGraphicsPipelineState( &psoDesc, IID_PPV_ARGS( pso.GetAddressOf() ) );
            if ( FAILED( hrPSO ) )
            {
                SW_LOG_ERROR_ID( "D3D12RHIResourceFactory.CreateGraphicsPipelineStateFailedHrVS", "CreateGraphicsPipelineState failed (hr=%#): VS '%#' PS '%#'", static_cast<uint32>( hrPSO ), desc._vertexShaderPath.c_str(), desc._pixelShaderPath.c_str() );
                _pDevice->flushDebugMessages( "CreateGraphicsPipelineState" );
            }
        }

        // 컴파일 · 생성에 실패하면 PSO 를 만들지 않는다(핸들 0, 다른 세 백엔드와 같은 규칙). 빈 레코드를 등록하면 부르는 쪽은 PSO 가
        // 있는 줄 알고 그리다가 드로우 계약(PSO 없는 드로우는 멈춤)에 걸린다 — 0 이면 렌더러가 그 패스를 건너뛴다.
        if ( pso == nullptr )
            return 0;
        return _pDevice->_pipelineStates.insert( {
            pso, RHIVertexLayout{ vertexSlotMask, desc._meshVertexFormat },
            request._bHasTessellation != SW_FALSE ? D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST : D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
            desc._renderState._stencilRef
        } );
    }

    RHIPipelineStateHandle D3D12RHIResourceFactory::createComputePipelineState( string_view shaderPath, string_view entryPoint, const vector<string>& listDefine )
    {
        Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
        if ( shaderPath.empty() == false )
        {
            const ShaderCompileDesc csDesc = RHIShaderRequest::resolveCompute( shaderPath, entryPoint, listDefine, ShaderTargetFormat::DXIL_D3D12 );
            ShaderCompileResult     res    = RHIShaderRequest::compile( csDesc );
            if ( res._bSuccess )
            {
                D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
                psoDesc.pRootSignature = _pDevice->_rootSignature.Get();
                psoDesc.CS             = { res._bytecode.data(), res._bytecode.size() };

                const HRESULT hr = _pDevice->_device->CreateComputePipelineState( &psoDesc, IID_PPV_ARGS( pso.GetAddressOf() ) );
                if ( FAILED( hr ) )
                {
                    SW_LOG_ERROR_ID( "D3D12RHIResourceFactory.CreateComputePipelineStateFailedHrX", "CreateComputePipelineState failed hr=0x%#", static_cast<uint32>( hr ) );
                    _pDevice->flushDebugMessages( "CreateComputePipelineState" );
                    return 0;
                }
            }
            else
                return 0;
        }
        if ( pso == nullptr ) // 셰이더 경로가 비었다 — 빈 레코드를 등록하지 않는다
            return 0;
        return _pDevice->_pipelineStates.insert( { pso, RHIVertexLayout{} } ); // 컴퓨트는 정점 입력이 없다
    }

    void D3D12RHIResourceFactory::destroyPipelineState( RHIPipelineStateHandle pso )
    {
        if ( pso == 0 )
            return;
        D3D12RHIDevice::D3D12PipelineStateRecord record{};
        if ( _pDevice->_pipelineStates.take( pso, record ) == false )
            return;
        if ( _pDevice->_frameStreamState._activeGraphicsPSO == pso )
            _pDevice->_frameStreamState._activeGraphicsPSO = 0;
        _pDevice->_releaseQueue.enqueueGPUKeepAlive( record._pso, _pDevice->_fenceValue );
    }

    RHIRenderPassHandle D3D12RHIResourceFactory::createRenderPass( const RHIRenderPassDesc& desc )
    {
        return _pDevice->_renderPassRegistry.add( desc );
    }

    void D3D12RHIResourceFactory::destroyRenderPass( RHIRenderPassHandle pass )
    {
        _pDevice->_renderPassRegistry.retire( pass );
    }
} // namespace sw
#endif
