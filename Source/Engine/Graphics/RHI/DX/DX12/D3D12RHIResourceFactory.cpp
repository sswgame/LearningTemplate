#include "pch.h"

#include "Engine/Graphics/RHI/DX/DX12/D3D12RHIResourceFactory.h"

#include "Core/Concurrency/mutex.h"
#include "Core/Container/IndexFreeList.h"
#include "Core/Log/LogOnce.h"
#include "Core/Log/ScopedDeferredLog.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Foundation/EnginePlatformHeaders.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/RHI/Backend/RHIBufferSizeUtil.h"
#include "Engine/Graphics/RHI/DX/DX12/D3D12RHICommandContext.h"
#include "Engine/Graphics/RHI/DX/DX12/D3D12RHIDevice.h"
#include "Engine/Graphics/RHI/DX/DX12/D3D12RHIResourcePreset.h"
#include "Engine/Graphics/RHI/DX/DXGIFormat.h"
#include "Engine/Graphics/RHI/DX/RHIDxRenderState.h"
#include "Engine/Graphics/RHI/RHICompiledShaderStages.h"
#include "Engine/Graphics/RHI/RHIFormatInfo.h"
#include "Engine/Graphics/RHI/RHIMeshVertexLayout.h"
#include "Engine/Graphics/RHI/RHIPipelineStateDesc.h"
#include "Engine/Graphics/RHI/RHITextureMipLayout.h"
#include "Engine/Graphics/RHI/Transfer/RHIStagingPlacement.h"
#include "Engine/Graphics/RHI/Transfer/RHITextureTransferDriver.h"
#include "Engine/Graphics/Shader/Compile/ShaderCache.h"
#include "Engine/Graphics/Shader/Compile/ShaderCompiler.h"

#if defined( SW_PLATFORM_WINDOWS )
namespace sw
{
    namespace
    {
        struct D3D12TextureInternal
        {
            /** @brief @p desc 텍스처의 D3D12 리소스 서술입니다(커밋 · placed · 크기 묻기가 같은 서술을 쓴다). */
            static D3D12_RESOURCE_DESC makeResourceDesc( const RHITextureDesc& desc )
            {
                const bool          bDepth = desc._bIsDepthStencil != SW_FALSE;
                D3D12_RESOURCE_DESC resourceDesc{};
                resourceDesc.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
                resourceDesc.Alignment          = 0;
                resourceDesc.Width              = desc._width;
                resourceDesc.Height             = desc._height;
                resourceDesc.DepthOrArraySize   = static_cast<UINT16>( desc._arraySize );
                resourceDesc.MipLevels          = static_cast<UINT16>( desc._mipLevels );
                resourceDesc.Format             = bDepth ? DXGI_FORMAT_R24G8_TYPELESS : toDXGIFormat( desc._format );
                resourceDesc.SampleDesc.Count   = 1;
                resourceDesc.SampleDesc.Quality = 0;
                resourceDesc.Layout             = D3D12_TEXTURE_LAYOUT_UNKNOWN;
                D3D12_RESOURCE_FLAGS flags      = D3D12_RESOURCE_FLAG_NONE;
                if ( desc._bIsRenderTarget )
                    flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
                if ( bDepth )
                    flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
                if ( desc._bIsUnorderedAccess )
                    flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
                // 깊이 + SRV: 셰이더 리소스 접근을 막지 않는다(DENY_SHADER_RESOURCE 를 붙이지 않는다).
                resourceDesc.Flags = flags;
                return resourceDesc;
            }
        };
    } // namespace

    RHIBufferHandle D3D12RHIResourceFactory::createConstantBuffer( uint32 size )
    {
        const UINT                             alignedSize = MathUtil::align( size, constant::render::kConstantBufferAlignment );
        Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
        void*                                  pMapped{ nullptr };
        if ( _pDevice->createMappedUploadBuffer( static_cast<uint64>( alignedSize ) * constant::render::kMaxFrameCountInFlight, L"ConstantBuffer", buffer, pMapped ) == false )
            return 0;

        const RHIBufferHandle handle = _pDevice->storeBuffer( buffer );
        // 핸들을 돌려주기 전이라 다른 스레드는 이 칸을 아직 모른다 — 매핑 주소와 칸 크기를 락 없이 적는다(그 뒤로 바뀌지 않는다).
        if ( D3D12RHIDevice::BufferRecord* const pRecord = _pDevice->findBufferRecord( handle ) )
        {
            pRecord->_pConstantMapped     = pMapped;
            pRecord->_constantAlignedSize = alignedSize;
        }
        return handle;
    }

    void D3D12RHIResourceFactory::writeConstantBuffer( RHIBufferHandle buffer, const void* pData, uint32 size )
    {
        if ( buffer == 0 || pData == nullptr )
            return;

        // 이번 프레임 칸에 쓰고, 나머지 칸은 링이 그 칸으로 돌아올 때 채운다(`RHIConstantBufferMirror` — 값이 바뀔 때만 쓰는 머티리얼 버퍼가
        // 세 프레임 중 두 프레임을 옛 값으로 그리지 않게). 레코드는 등록표 잠금 없이 찾고, 부수기와는 미러의 칸 잠금이 가른다(`destroyBufferNative`).
        const D3D12RHIDevice::BufferRecord* const pRecord = _pDevice->findBufferRecord( buffer );
        if ( pRecord == nullptr || pRecord->_pConstantMapped == nullptr )
            return;
        SW_PROFILE_COUNT( "RT.Bind.constantBufferWrites", 1 );
        _pDevice->_constantBufferMirror.write( buffer, 0, _pDevice->_frameRing.currentIndex(), pData, size,
                                               [this]( RHIBufferHandle target, uint32 slot, uint32 offset, const void* pBytes, uint32 byteCount )
        { _pDevice->writeConstantBufferSlot( target, slot, offset, pBytes, byteCount ); } );
    }

    RHIBufferHandle D3D12RHIResourceFactory::createConstantBufferPage( uint32 size )
    {
        // 업로드 버퍼 하나(프레임 칸 × 페이지 크기)를 구간이 나눠 쓴다 — 상수 버퍼마다 버퍼 하나이던 것을 페이지당 하나로 줄인다(감사 R-4).
        return createConstantBuffer( size );
    }

    void D3D12RHIResourceFactory::writeConstantBufferSlice( const RHIBufferSlice& slice, const void* pData, uint32 size )
    {
        if ( slice._buffer == 0 || pData == nullptr )
            return;
        // 버퍼 갱신과 같은 길이되 이번 프레임 칸의 구간 오프셋에 쓴다(나머지 칸은 미러가 같은 오프셋에 채운다).
        const D3D12RHIDevice::BufferRecord* const pRecord = _pDevice->findBufferRecord( slice._buffer );
        if ( pRecord == nullptr || pRecord->_pConstantMapped == nullptr )
            return;
        SW_PROFILE_COUNT( "RT.Bind.constantBufferWrites", 1 );
        _pDevice->_constantBufferMirror.write( slice._buffer, slice._offset, _pDevice->_frameRing.currentIndex(), pData, MathUtil::min( size, slice._size ),
                                               [this]( RHIBufferHandle target, uint32 slot, uint32 offset, const void* pBytes, uint32 byteCount )
        { _pDevice->writeConstantBufferSlot( target, slot, offset, pBytes, byteCount ); } );
    }

    RHIBufferHandle D3D12RHIResourceFactory::createStructuredBuffer( uint32 elementSize, uint32 elementCount )
    {
        // 64비트로 곱한다. `UINT` 로 곱해 `Width`(UINT64)에 넣으면 넘칠 때 조용히 작은 버퍼가 된다.
        const uint64              totalBytes   = static_cast<uint64>( elementSize ) * static_cast<uint64>( elementCount );
        const D3D12_RESOURCE_DESC resourceDesc = D3D12RHIResourcePreset::bufferDesc( totalBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS );

        Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
        if ( _pDevice->createResource( D3D12_HEAP_TYPE_DEFAULT, resourceDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, L"StructuredBuffer", buffer ) == false )
            return 0;

        const RHIBufferHandle handle = _pDevice->storeBuffer( buffer );
        // 핸들을 돌려주기 전이라 다른 스레드는 이 칸을 아직 모른다 — 추적 표시를 락 없이 켠다(상태는 COMMON 으로 시작한다).
        if ( D3D12RHIDevice::BufferRecord* const pRecord = _pDevice->findBufferRecord( handle ) )
            pRecord->_bTrackedState = SW_TRUE;
        {
            // 등록(registerBindlessBufferSRV · registerBindlessBufferUAV)이 같은 배타 락 안에서 읽는다. 다른 스레드의 생성과 겹쳐 재해시되지 않게 한다.
            UniqueLock<shared_mutex> registryLock{ _pDevice->_bindlessMutex };
            _pDevice->_mapStructuredStride[handle] = elementSize > 0 ? elementSize : 4u;
        }
        return handle;
    }

    bool D3D12RHIResourceFactory::beginUploadPeriod( uint32 slotIndex )
    {
        if ( _pDevice->_device == nullptr || _pDevice->_commandQueue == nullptr )
            return false;
        D3D12RHIDevice::StructuredUploadSlot& slot = _pDevice->_arrStructuredUploadSlot[slotIndex];
        // 프레임 스트림에 기록한 복사가 아직 나가지 않았으면 그 구간이 이어진다 — 프레임 도중의 장치 대기가 펜스 값을 올려도 스테이징을 되감지 않는다.
        if ( slot._bFrameCopies != SW_FALSE || slot._resetFence == _pDevice->_fenceValue.load() )
            return true;
        // 구간이 바뀌었는데 이 슬롯의 리스트가 열려 있다면 프레임 제출과 Signal 사이에 연 복사다. 지금 내보낸다. 내보내면 `_resetFence` 가 **지금** 구간이 되므로
        // (그 복사는 이번 구간의 Signal 에서야 끝난다) 얼로케이터를 Reset 하지 않고 이어 쓴다.
        if ( _pDevice->_openUploadSlotIndex == slotIndex )
        {
            _pDevice->flushPendingUploads( true );
            return true;
        }
        if ( slot._resetFence != UINT64_MAX )
        {
            // 펜스 값이 바뀌었다는 것은 앞 구간 뒤에 Signal 이 **큐에 들어갔다**는 뜻이지 GPU 가 그 구간의 복사를
            // 끝냈다는 뜻이 아니다. signalCurrentFrame 은 올리기만 하고 기다리지 않는다. 프레임 끝 Signal 직후,
            // 링이 아직 앞 슬롯을 가리키는 동안 업로드가 오면 여기서 아직 실행 중인 얼로케이터를 Reset 하게 된다
            // ("is being reset before previous executions ... have completed" → DEVICE_HUNG, GPU 가 붐빌 때만).
            // 링 슬롯 대기가 가려 줄 것이라 기대하지 않고 이 슬롯의 펜스(_resetFence: 그 구간의 제출 뒤에
            // Signal 된 값)를 직접 기다린다. 보통은 이미 지나 있어 비용이 없다.
            if ( _pDevice->waitForFenceValue( slot._resetFence ) == false )
            {
                SW_LOG_ERROR_ID( "D3D12RHIResourceFactory.OpenUploadSlotPreviousCopiesAllocator", "beginUploadPeriod: previous copies on allocator %# have not completed (fence %#)",
                                 slotIndex, slot._resetFence );
                return false;
            }
            if ( slot._copyAllocator != nullptr && isHrSuccess( slot._copyAllocator->Reset(), "Reset" ) == false )
            {
                SW_LOG_ERROR_ID( "D3D12RHIResourceFactory.OpenUploadSlotCopyAllocatorReset", "beginUploadPeriod: copy allocator Reset failed" );
                return false;
            }
        }
        slot._uploadOffset = 0;
        slot._resetFence   = _pDevice->_fenceValue.load();
        return true;
    }

    bool D3D12RHIResourceFactory::openUploadList( uint32 slotIndex )
    {
        // 이미 열려 있으면 이어서 기록한다. 리스트를 닫고 다시 여는 것도, 제출도 프레임에 한 번이다.
        if ( _pDevice->_openUploadSlotIndex == slotIndex )
            return true;
        // 다른 칸이 열려 있으면(링이 넘어가기 전에 연 프레임 밖 업로드) 지금 내보낸다 — 열린 리스트는 하나라 기록 순서가 큐 순서다(`_openUploadSlotIndex`).
        _pDevice->flushPendingUploads( true );
        D3D12RHIDevice::StructuredUploadSlot& slot = _pDevice->_arrStructuredUploadSlot[slotIndex];
        if ( slot._copyAllocator == nullptr )
        {
            if ( isHrSuccess( _pDevice->_device->CreateCommandAllocator( D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS( slot._copyAllocator.GetAddressOf() ) ), "CreateCommandAllocator" ) == false )
            {
                SW_LOG_ERROR_ID( "D3D12RHIResourceFactory.OpenUploadSlotFailedCreateCopy", "openUploadList: failed to create copy command allocator" );
                return false;
            }
            utf16 arrName[constant::buffer::kMax64]{};
            swprintf_s( arrName, L"StructuredUploadAllocator%u", slotIndex );
            slot._copyAllocator->SetName( arrName );
        }
        if ( slot._copyCommandList == nullptr )
        {
            if ( isHrSuccess( _pDevice->_device->CreateCommandList( 0, D3D12_COMMAND_LIST_TYPE_DIRECT, slot._copyAllocator.Get(), nullptr, IID_PPV_ARGS( slot._copyCommandList.GetAddressOf() ) ), "CreateCommandList" ) == false )
            {
                SW_LOG_ERROR_ID( "D3D12RHIResourceFactory.OpenUploadSlotFailedCreateCopy2", "openUploadList: failed to create copy command list" );
                return false;
            }
            utf16 arrListName[constant::buffer::kMax64]{};
            swprintf_s( arrListName, L"StructuredUploadList%u", slotIndex );
            slot._copyCommandList->SetName( arrListName ); // 디버그 레이어 글이 이 이름으로 업로드 리스트를 가리킨다
        }
        else if ( isHrSuccess( slot._copyCommandList->Reset( slot._copyAllocator.Get(), nullptr ), "Reset" ) == false )
        {
            SW_LOG_ERROR_ID( "D3D12RHIResourceFactory.OpenUploadSlotCopyCommandList", "openUploadList: copy command list Reset failed" );
            return false;
        }
        _pDevice->_openUploadSlotIndex = slotIndex;
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
        // 펜스 구간이 바뀌었으면 여기서 오프셋이 0 으로 되감긴다.
        const uint32 slotIndex = _pDevice->_frameRing.currentIndex();
        if ( beginUploadPeriod( slotIndex ) == false )
            return false;
        D3D12RHIDevice::StructuredUploadSlot& slot = _pDevice->_arrStructuredUploadSlot[slotIndex];

        const RHIStagingPlacement placement     = RHIStagingPlacement::place( slot._uploadOffset, alignment, sizeBytes, slot._capacity, slot._uploadHeap != nullptr );
        uint64                    stagingOffset = placement._offset;
        if ( placement._newCapacity != 0 )
        {
            const uint64 newCapacity = placement._newCapacity;

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
            if ( _pDevice->createMappedUploadBuffer( newCapacity, L"StructuredUploadStaging", newHeap, pMapped ) == false )
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

    void D3D12RHIResourceFactory::updateStructuredBufferRegionsNative( RHIBufferHandle buffer, const void* pBaseSource,
                                                                       const RHIBufferCopyRegion* pRegions, uint32 regionCount )
    {
        recordBufferCopies( buffer, pBaseSource, pRegions, regionCount, _pDevice->isRecordingFrameOnThisThread() ? UploadOrder::FrameStream : UploadOrder::BeforeFrame );
    }

    void D3D12RHIResourceFactory::recordBufferCopies( RHIBufferHandle buffer, const void* pBaseSource, const RHIBufferCopyRegion* pRegions, uint32 regionCount, UploadOrder order )
    {
        const bool bInvalidBufferRegionUpload = buffer == 0 || pBaseSource == nullptr || pRegions == nullptr || regionCount == 0 ||
                                                _pDevice->_device == nullptr || _pDevice->_commandQueue == nullptr;
        if ( bInvalidBufferRegionUpload )
            return;

        ID3D12Resource* pDest = _pDevice->resolveBuffer( buffer );
        if ( pDest == nullptr )
            return;
        // 프레임 스트림의 복사는 패스 밖이다(위 판정이 렌더 스레드의 위반을 알리고 닫았다 — 렌더 스레드가 없는 실행은 여기서 닫는다).
        if ( order == UploadOrder::FrameStream )
            _pDevice->_frameStreamContext->endRenderPass();

        const ScopedDeferredLog deferredLog;
        ScopedLock<mutex>       uploadLock{ _pDevice->_uploadSlotMutex };

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
        ID3D12GraphicsCommandList*            pList = _pDevice->_pActiveFrameList;
        if ( order == UploadOrder::FrameStream )
        {
            // 프레임 스트림의 지금 자리에 기록한다 — 추적 상태는 기록 시점의 값이고 실행 순서도 기록 순서다. 업로드 리스트(제출 맨 앞)로 보내면 이 프레임이 이미 낸 전이와 어긋나고
            // 앞서 기록한 읽기가 새 내용을 본다(Vulkan 도 렌더 스레드의 갱신은 프레임 커맨드버퍼에 기록한다).
            _pDevice->_frameStreamState._bRecordedAny = SW_TRUE;
            slot._bFrameCopies                        = SW_TRUE;
        }
        else
        {
            if ( openUploadList( slotIndex ) == false )
                return;
            pList = slot._copyCommandList.Get();
        }

        D3D12RHIDevice::BufferRecord* const pBufferRecord = _pDevice->findBufferRecord( buffer );
        const bool                          bTracked      = pBufferRecord != nullptr && pBufferRecord->_bTrackedState != SW_FALSE;
        const D3D12_RESOURCE_STATES         stateBefore   = bTracked ? pBufferRecord->_state.load() : D3D12_RESOURCE_STATE_COMMON;
        // 끝낸 뒤 둘 읽기 상태는 버퍼 종류가 정한다(정점 · 인덱스 · 구조 — `BufferRecord::_uploadReadState`). 추적하지 않는 버퍼는 셰이더 읽기다.
        constexpr D3D12_RESOURCE_STATES kShaderReadState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        const D3D12_RESOURCE_STATES     readState        = bTracked ? pBufferRecord->_uploadReadState : kShaderReadState;
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
        D3D12_RESOURCE_BARRIER toShaderRead{};
        toShaderRead.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        toShaderRead.Transition.pResource   = pDest;
        toShaderRead.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        toShaderRead.Transition.StateAfter  = readState;
        toShaderRead.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        pList->ResourceBarrier( 1, &toShaderRead );
        // 업로드 리스트의 제출은 프레임 끝(또는 큐 대기 직전)에 한 번이다. D3D12RHIDevice::flushPendingUploads.

        if ( bTracked )
            pBufferRecord->_state.store( readState );
    }

    // 길이를 나누지 않는다: 서브리소스 · 풋프린트 · 스테이징 복사 · 상태 전이의 순서가 곧 뜻이다.
    RHITransferSubmit D3D12RHIResourceFactory::submitTextureUpload( RHITextureHandle texture, const RHITextureUploadDesc& desc )
    {
        ID3D12Resource* pTexture                  = _pDevice->resolveTexture( texture );
        const bool      bTextureUploadUnavailable = pTexture == nullptr || _pDevice->_device == nullptr || _pDevice->_commandQueue == nullptr || _pDevice->pollDeviceLost();
        if ( bTextureUploadUnavailable )
        {
            // 장치를 잃었으면 진입에서 Rejected 다(계약 2) — Accepted 를 주고 폴링에서야 Failed 가 되면 호출자가 잃은 장치에 올린 줄 알고 기다린다.
            SW_LOG_ERROR_ID( "D3D12RHIResourceFactory.SubmitTextureUploadTextureDeviceNot", "submitTextureUpload: texture %# or the device is not usable (uninitialized or lost)", texture.getPacked() );
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
        _pDevice->_device->GetCopyableFootprints( &resourceDesc, firstSubresource, mipCount, span.getOffsetBytes(), arrFootprint, arrRowCount, arrRowSize, &totalBytes );

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

        return RHITextureTransferDriver::runJob<D3D12RHIDevice::D3D12TransferJob>(
            ring, span, _pDevice->_transferSerials, [this]( D3D12RHIDevice::D3D12TransferJob& job )
        { return _pDevice->beginTransferJob( job ); },
            [&]( D3D12RHIDevice::D3D12TransferJob& job )
        {
            ID3D12GraphicsCommandList* pList = job._list.Get();

            // 추적 상태에서 출발해 같은 상태로 돌아간다(서브리소스마다 — recordTextureStateDetour).
            _pDevice->recordTextureStateDetour( pList, texture, pTexture, D3D12_RESOURCE_STATE_COPY_DEST, true );

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

            _pDevice->recordTextureStateDetour( pList, texture, pTexture, D3D12_RESOURCE_STATE_COPY_DEST, false );
            // 기록이 끝났다. 즉시 큐에 내고 전송 펜스를 올린다(기다리지 않는다). 시리얼은 큐 제출과 같은 락 안에서 매겨진다.
        },
            [this]( D3D12RHIDevice::D3D12TransferJob& job )
        { return _pDevice->submitTransferJob( std::move( job ), "submitTextureUpload" ); },
            [&]( uint64 serial )
        {
            _pDevice->recordTextureTransfer( texture, serial );
        } );
    }

    RHITransferSubmit D3D12RHIResourceFactory::submitTextureRegionUpload( RHITextureHandle texture, const RHITextureRegionUploadDesc& desc )
    {
        ID3D12Resource* pTexture                        = _pDevice->resolveTexture( texture );
        const bool      bTextureRegionUploadUnavailable = pTexture == nullptr || _pDevice->_device == nullptr || _pDevice->_commandQueue == nullptr || _pDevice->pollDeviceLost();
        if ( bTextureRegionUploadUnavailable )
        {
            SW_LOG_ERROR_ID( "D3D12RHIResourceFactory.SubmitTextureRegionUploadTextureDeviceNot", "submitTextureRegionUpload: texture %# or the device is not usable (uninitialized or lost)", texture.getPacked() );
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

        uint8*       pDstBase = ring.getSpanMappedBase( span ) + span.getOffsetBytes();
        const uint8* pSrcBase = static_cast<const uint8*>( desc._pData );
        for ( uint32 row = 0; row < desc._height; ++row )
        {
            Memory::copy( pDstBase + static_cast<uint64>( row ) * rowPitch, pSrcBase + static_cast<uint64>( row ) * rowBytes, rowBytes );
        }

        return RHITextureTransferDriver::runJob<D3D12RHIDevice::D3D12TransferJob>(
            ring, span, _pDevice->_transferSerials, [this]( D3D12RHIDevice::D3D12TransferJob& job )
        { return _pDevice->beginTransferJob( job ); },
            [&]( D3D12RHIDevice::D3D12TransferJob& job )
        {
            ID3D12GraphicsCommandList* pList = job._list.Get();

            // 추적 상태에서 출발해 같은 상태로 돌아간다(전체 업로드와 같다).
            _pDevice->recordTextureStateDetour( pList, texture, pTexture, D3D12_RESOURCE_STATE_COPY_DEST, true );

            D3D12_TEXTURE_COPY_LOCATION dst{};
            dst.pResource        = pTexture;
            dst.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            dst.SubresourceIndex = desc._mip + desc._arraySlice * resourceDesc.MipLevels; // D3D12CalcSubresource 와 같은 순서

            D3D12_TEXTURE_COPY_LOCATION src{};
            src.pResource                          = ring.getSpanResource( span );
            src.Type                               = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            src.PlacedFootprint.Offset             = span.getOffsetBytes();
            src.PlacedFootprint.Footprint.Format   = resourceDesc.Format;
            src.PlacedFootprint.Footprint.Width    = desc._width;
            src.PlacedFootprint.Footprint.Height   = desc._height;
            src.PlacedFootprint.Footprint.Depth    = 1;
            src.PlacedFootprint.Footprint.RowPitch = rowPitch;
            pList->CopyTextureRegion( &dst, desc._x, desc._y, 0, &src, nullptr );

            _pDevice->recordTextureStateDetour( pList, texture, pTexture, D3D12_RESOURCE_STATE_COPY_DEST, false );
        },
            [this]( D3D12RHIDevice::D3D12TransferJob& job )
        { return _pDevice->submitTransferJob( std::move( job ), "submitTextureRegionUpload" ); },
            [&]( uint64 serial )
        {
            _pDevice->recordTextureTransfer( texture, serial );
        } );
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
            _pDevice->reportDeviceLost( "pollTransfer: no queue, fence or device" );
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

        // 링은 영속 매핑이다. 읽을 범위로 한 번 더 Map 해 CPU 캐시를 무효화하고(중첩 Map 은 참조를 센다) 그 주소에서 읽는다.
        D3D12RHIDevice::D3D12StagingRing& ring    = _pDevice->_readbackRing;
        ID3D12Resource* const             pBuffer = ring.getSpanResource( record._span );
        const D3D12_RANGE                 readRange{ static_cast<SIZE_T>( record._footprint.Offset ),
                                     static_cast<SIZE_T>( record._footprint.Offset + static_cast<uint64>( record._footprint.Footprint.RowPitch ) * record._rowCount ) };
        void*                             pMapped{ nullptr };
        if ( pBuffer == nullptr || isHrSuccess( pBuffer->Map( 0, &readRange, &pMapped ), "Map" ) == false || pMapped == nullptr )
        {
            SW_LOG_ERROR_ID( "D3D12RHIResourceFactory.TakeTextureReadbackFailedMapReadback", "takeTextureReadback: failed to map the readback buffer of ticket %#", ticket._serial );
            RHITextureTransferDriver::releaseTakenReadback( ring._allocator, std::move( record._span ) );
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
        pBuffer->Unmap( 0, &noWrite );
        RHITextureTransferDriver::releaseTakenReadback( ring._allocator, std::move( record._span ) );
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
        // 복사가 아직 GPU 에서 도는 중이면 구간에 시리얼을 붙여 워터마크가 지난 뒤에 돌아오게 한다(Vulkan 과 같은 꼬리).
        RHITextureTransferDriver::releaseDiscardedReadback( _pDevice->_readbackRing._allocator, std::move( record._span ), _pDevice->_transferSerials, ticket._serial );
    }

    RHIFormat D3D12RHIResourceFactory::getTextureFormat( RHITextureHandle texture ) const
    {
        // 오프스크린 레코드는 요청 포맷을 그대로 들고 있다(깊이는 리소스가 typeless 라 GetDesc 로는 못 되돌린다).
        D3D12RHIDevice::OffscreenTargetView view{};
        if ( _pDevice->findOffscreenTargetView( texture, 0, 0, view ) )
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
        const bool      bTextureReadbackUnavailable = pTexture == nullptr || _pDevice->_device == nullptr || _pDevice->_commandQueue == nullptr || _pDevice->pollDeviceLost();
        if ( bTextureReadbackUnavailable )
        {
            SW_LOG_ERROR_ID( "D3D12RHIResourceFactory.SubmitTextureReadbackTextureDeviceNot", "submitTextureReadback: texture %# or the device is not usable (uninitialized or lost)", texture.getPacked() );
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

        // 되읽기 링의 구간에 받는다(Vulkan 과 같은 두 링). 구간 시작이 배치 오프셋 정렬(512)의 배수라 풋프린트 규칙을 그대로 지킨다.
        D3D12RHIDevice::D3D12StagingRing& ring = _pDevice->_readbackRing;
        RHIStagingSpan                    span{};
        const RHISubmitResult             allocateResult = _pDevice->allocateStagingSpan( ring, totalBytes, "submitTextureReadback", span );
        if ( allocateResult != RHISubmitResult::Accepted )
            return RHITransferSubmit::fromUnaccepted( allocateResult );
        // 링 안의 실제 위치로 풋프린트를 다시 받는다(BaseOffset).
        _pDevice->_device->GetCopyableFootprints( &resourceDesc, subresource, 1, span.getOffsetBytes(), &footprint, &rowCount, &rowSize, &totalBytes );

        // 구간에는 시리얼을 붙이지 않는다 — 가져가거나 버릴 때까지 결과가 링에 남아야 한다(`runReadbackJob`).
        return RHITextureTransferDriver::runReadbackJob<D3D12RHIDevice::D3D12TransferJob>(
            ring, span, _pDevice->_transferSerials, [this]( D3D12RHIDevice::D3D12TransferJob& job )
        {
            if ( _pDevice->beginTransferJob( job ) )
                return true;
            SW_LOG_ERROR_ID( "D3D12RHIResourceFactory.SubmitTextureReadbackFailedOpenCopy", "submitTextureReadback: failed to open the copy command list" );
            return false;
        }, [&]( D3D12RHIDevice::D3D12TransferJob& job )
        {
            ID3D12GraphicsCommandList* pList = job._list.Get();

            _pDevice->recordTextureStateDetour( pList, texture, pTexture, D3D12_RESOURCE_STATE_COPY_SOURCE, true );

            D3D12_TEXTURE_COPY_LOCATION src{};
            src.pResource        = pTexture;
            src.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            src.SubresourceIndex = subresource;
            D3D12_TEXTURE_COPY_LOCATION dst{};
            dst.pResource       = ring.getSpanResource( span );
            dst.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            dst.PlacedFootprint = footprint;
            pList->CopyTextureRegion( &dst, 0, 0, 0, &src, nullptr );

            _pDevice->recordTextureStateDetour( pList, texture, pTexture, D3D12_RESOURCE_STATE_COPY_SOURCE, false );
        }, [this]( D3D12RHIDevice::D3D12TransferJob& job )
        { return _pDevice->submitTransferJob( std::move( job ), "submitTextureReadback" ); }, [&]( uint64 serial )
        {
            // 원본 텍스처도 복사가 끝나기 전에는 해제되지 않게 전송 시리얼에 매단다(destroyTexture 가 해제를 미룬다).
            _pDevice->recordTextureTransfer( texture, serial );

            D3D12RHIDevice::TextureReadbackRecord record;
            record._span      = std::move( span );
            record._footprint = footprint;
            record._layout    = outLayout;
            record._rowCount  = rowCount;
            record._rowSize   = rowSize;
            record._bDepth    = bDepth ? SW_TRUE : SW_FALSE;
            _pDevice->_readbackTable.add( serial, std::move( record ) );
        } );
    }

    RHIBufferHandle D3D12RHIResourceFactory::createVertexBuffer( const void* pData, uint32 sizeBytes )
    {
        // 정적 정점은 default 힙에 둔다 — 업로드 힙(시스템 메모리)에서 그리면 디스크리트 GPU 가 그릴 때마다 PCIe 로 읽는다(언리얼 · 유니티의 정적 버퍼도 default 힙 + 복사).
        // 내용은 스테이징 복사로 올린다(`recordBufferCopies` — 이번 프레임 리스트보다 먼저 실행된다).
        if ( pData == nullptr || sizeBytes == 0 )
            return 0;
        constexpr D3D12_RESOURCE_STATES kVertexRead =
            D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        return createDefaultBufferWithData( pData, sizeBytes, kVertexRead );
    }

    RHIBufferHandle D3D12RHIResourceFactory::createDefaultBufferWithData( const void* pData, uint32 sizeBytes, D3D12_RESOURCE_STATES readState )
    {
        const RHIBufferHandle handle = createGPUVertexBuffer( sizeBytes );
        if ( handle == 0 )
            return 0;
        if ( D3D12RHIDevice::BufferRecord* const pRecord = _pDevice->findBufferRecord( handle ) )
            pRecord->_uploadReadState = readState; // 핸들을 돌려주기 전이라 락 없이 쓴다
        // 새 버퍼의 첫 내용은 제출 맨 앞에 실행된다 — 어느 스레드 · 어느 자리에서 만들어도 이 버퍼를 쓰는 기록보다 먼저다(Vulkan `uploadDeviceLocal` 과 같다).
        const RHIBufferCopyRegion region{ 0, 0, sizeBytes };
        recordBufferCopies( handle, pData, &region, 1, UploadOrder::BeforeFrame );
        return handle;
    }

    RHIBufferHandle D3D12RHIResourceFactory::createGPUVertexBuffer( uint32 sizeBytes )
    {
        if ( _pDevice->_device == nullptr || sizeBytes == 0 )
            return 0;
        // default 힙 — 복사의 도착이 된다(업로드 힙 `createVertexBuffer` 는 못 된다). 구조 버퍼처럼 상태를 추적한다(COMMON 으로 시작, 그리기 전 VertexOrConstant 로 옮긴다).
        Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
        if ( _pDevice->createResource( D3D12_HEAP_TYPE_DEFAULT, D3D12RHIResourcePreset::bufferDesc( sizeBytes ), D3D12_RESOURCE_STATE_COMMON, nullptr, L"GeometryBuffer", buffer ) == false )
            return 0;
        const RHIBufferHandle handle = _pDevice->storeBuffer( buffer );
        if ( D3D12RHIDevice::BufferRecord* const pRecord = _pDevice->findBufferRecord( handle ) )
        {
            pRecord->_bTrackedState   = SW_TRUE; // 핸들을 돌려주기 전이라 락 없이 켠다(`createStructuredBuffer` 와 같다)
            pRecord->_uploadReadState = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        }
        return handle;
    }

    void D3D12RHIResourceFactory::updateGeometryBufferRegionsNative( RHIBufferHandle buffer, const void* pBaseSource, const RHIBufferCopyRegion* pRegions, uint32 regionCount )
    {
        const bool bInvalidVertexRegionUpdate = buffer == 0 || pBaseSource == nullptr || pRegions == nullptr || regionCount == 0;
        if ( bInvalidVertexRegionUpdate )
            return;
        // 정점 · 인덱스 버퍼는 default 힙이다(정적 자원을 시스템 메모리에서 그리지 않는다) — 스테이징 복사로 쓴다. 복사는 기록한 자리에서 실행되고
        // 앞 프레임이 읽는 조각이 아님은 부르는 쪽이 지킨다(IRHIResourceFactory::updateGeometryBufferRegions). 범위는 여기서 본다.
        ID3D12Resource* pDest = _pDevice->resolveBuffer( buffer );
        if ( pDest == nullptr )
            return;
        const uint64 bufferBytes = pDest->GetDesc().Width;
        for ( uint32 regionIndex = 0; regionIndex < regionCount; ++regionIndex )
        {
            const RHIBufferCopyRegion& region = pRegions[regionIndex];
            if ( static_cast<uint64>( region._dstOffset ) + region._sizeBytes > bufferBytes )
            {
                SW_LOG_ERROR_ID( "D3D12RHIResourceFactory.UpdateGeometryBufferRegionsRegionOutsideByte", "updateGeometryBufferRegions: region [%#, +%#) is outside the %#-byte vertex or index buffer; skipped", region._dstOffset, region._sizeBytes, bufferBytes );
                continue;
            }
            updateStructuredBufferRegionsNative( buffer, pBaseSource, &region, 1 );
        }
    }

    RHIBufferHandle D3D12RHIResourceFactory::createIndexBuffer( const void* pData, uint32 sizeBytes )
    {
        // 정적 인덱스도 default 힙 + 스테이징 복사다(`createVertexBuffer` 와 같은 까닭).
        if ( pData == nullptr || sizeBytes == 0 )
            return 0;
        return createDefaultBufferWithData( pData, sizeBytes, D3D12_RESOURCE_STATE_INDEX_BUFFER );
    }

    bool D3D12RHIResourceFactory::destroyBufferNative( RHIBufferHandle buffer )
    {
        if ( buffer == 0 )
            return false;
        // 렌더 스레드의 기록 상태(`_frameStreamState` 의 묶인 정점 · 인덱스 버퍼)는 여기서 지우지 않는다. 이 함수는 게임 스레드에서도 불리는데 그 값은
        // 렌더 스레드만 쓴다(여기서 쓰면 경쟁이다). 핸들은 세대가 있어 다시 쓰이지 않으므로, 지운 핸들은 드로우의 `resolveBuffer` 가 null 로
        // 풀어 건너뛴다.
        UniqueLock<shared_mutex> registryLock{ _pDevice->_bindlessMutex };
        _pDevice->_mapStructuredStride.erase( buffer );
        // 상수 버퍼는 매핑한 채 놓는다(D3D12 는 매핑한 자원을 그대로 놓아도 된다 — 자원은 GPU 펜스 뒤에 사라진다). 레코드는 미러의 칸 잠금 안에서 거둔다: 진행 중인 쓰기가 그 잠금 안에서 레코드를 읽는다.
        D3D12RHIDevice::BufferRecord taken{};
        bool                         bTaken{ false };
        _pDevice->_constantBufferMirror.remove( buffer, [&]()
        { bTaken = _pDevice->_gpuBuffers.take( buffer, taken ); } );
        if ( bTaken == false )
            return false;
        Microsoft::WRL::ComPtr<ID3D12Resource> owned = std::move( taken._resource );

        // 이 버퍼의 번호를 모두 놓는다(다섯 백엔드 같은 계약 — 위 `registryLock` 아래). 예전에는 값만 지우고 번호를 돌려받지 않아 힙 칸이 샜다.
        const auto deferFree = [this]( RHIDescriptorIndex index )
        {
            _pDevice->retireBindlessReadSlot( index );
            deferFreeBindlessIndex( index );
        };
        _pDevice->_srvRegistry.releaseBuffer( RHIBindlessReuse::AfterFence, buffer, deferFree );
        _pDevice->_uavRegistry.releaseBuffer( RHIBindlessReuse::AfterFence, buffer, deferFree );

        _pDevice->releaseTrackedResourceDeferred( std::move( owned ), RHIMemoryKey::makeBuffer( buffer ) );
        return true;
    }

    RHIBufferHandle D3D12RHIResourceFactory::createReadbackBuffer( uint32 elementSize, uint32 elementCount )
    {
        uint32     totalBytes{ 0 };
        const bool bInvalidReadbackRequest = _pDevice->_device == nullptr || elementSize == 0 || elementCount == 0 || RHIBufferSizeUtil::computeStructuredBytes( elementSize, elementCount, totalBytes ) == false;
        if ( bInvalidReadbackRequest )
            return 0;
        Microsoft::WRL::ComPtr<ID3D12Resource> readback;
        if ( _pDevice->createResource( D3D12_HEAP_TYPE_READBACK, D3D12RHIResourcePreset::bufferDesc( totalBytes ), D3D12_RESOURCE_STATE_COPY_DEST, nullptr, L"ReadbackBuffer", readback ) == false )
            return 0;
        // 읽기 힙 리소스는 COPY_DEST 로 고정이다 — 상태 추적(`BufferRecord::_bTrackedState`)에 올리지 않는다.
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
            SW_LOG_ERROR_ID( "D3D12RHIResourceFactory.ReadReadbackBufferBufferNotReadback", "readReadbackBuffer: buffer %# is not a readback buffer", readback.getPacked() );
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
        _pDevice->getMemoryTracker().setLabel( RHIMemoryKey::makeTexture( texture ), pName ); // 메모리 스냅숏 · RT 목록이 같은 이름을 보인다
        // PIX · DRED 페이지 폴트 출력이 이 이름을 보인다(디버그 빌드의 `SW Texture <핸들>` 이름을 첨부 이름으로 덮는다).
        const wstring wideName = StringUtil::utf8ToUTF16( pName );
        D3D12RHIDevice::setResourceName( pResource, wideName.c_str() );
    }

    // 길이 유지(긴 함수 정리 기준): 서술 채우기 · 리소스 생성 · 뷰 · 초기 데이터 업로드의 순서가 곧 의미라 나누지 않는다.
    RHITextureHandle D3D12RHIResourceFactory::createTexture2D( const RHITextureDesc& desc )
    {
        return createTextureResource( desc, nullptr, 0 );
    }

    RHITextureAllocationInfo D3D12RHIResourceFactory::getTextureAllocationInfo( const RHITextureDesc& desc )
    {
        if ( isRHITextureShapeValid( desc ) == false )
            return {};
        const D3D12_RESOURCE_DESC            resourceDesc = D3D12TextureInternal::makeResourceDesc( desc );
        const D3D12_RESOURCE_ALLOCATION_INFO info         = _pDevice->_device->GetResourceAllocationInfo( 0, 1, &resourceDesc );
        if ( info.SizeInBytes == UINT64_MAX )
            return {}; // 서술이 잘못됐다 — 부르는 쪽은 그 첨부를 커밋 리소스로 만든다(createTexture2D 가 이유를 남긴다)
        return RHITextureAllocationInfo{ info.SizeInBytes, info.Alignment };
    }

    RHITransientHeapHandle D3D12RHIResourceFactory::createTransientHeap( uint64 sizeBytes )
    {
        if ( sizeBytes == 0 || _pDevice->_allocator == nullptr )
            return 0;
        D3D12MA::ALLOCATION_DESC allocationDesc{};
        allocationDesc.Flags          = static_cast<D3D12MA::ALLOCATION_FLAGS>( D3D12MA::ALLOCATION_FLAG_COMMITTED | D3D12MA::ALLOCATION_FLAG_CAN_ALIAS ); // 자기 힙 하나(오프셋 0) — 풀이 크기가 바뀔 때 통째로 다시 만든다
        allocationDesc.HeapType       = D3D12_HEAP_TYPE_DEFAULT;
        allocationDesc.ExtraHeapFlags = D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES; // 리소스 힙 등급 1 도 받는 범주(렌더 타깃 · 깊이 텍스처만)
        D3D12_RESOURCE_ALLOCATION_INFO allocationInfo{};
        allocationInfo.SizeInBytes = MathUtil::align( sizeBytes, static_cast<uint64>( D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT ) ); // AllocateMemory 는 64KB 의 배수를 받는다
        allocationInfo.Alignment   = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
        Microsoft::WRL::ComPtr<D3D12MA::Allocation> heap;
        if ( isHrSuccess( _pDevice->_allocator->AllocateMemory( &allocationDesc, &allocationInfo, heap.GetAddressOf() ), "D3D12MA::Allocator::AllocateMemory (transient heap)" ) == false )
            return 0;
        heap->SetName( L"TransientHeap" );
        const RHITransientHeapHandle handle{ _nextTransientHeap++ };
        _mapTransientHeap.emplace( handle, std::move( heap ) );
        return handle;
    }

    bool D3D12RHIResourceFactory::destroyTransientHeapNative( RHITransientHeapHandle heap )
    {
        // 그 위의 placed 텍스처가 각자 힙을 붙든다 — 핸들 표에서만 지운다. 마지막 텍스처가 (해제 큐가 GPU 를 기다린 뒤) 놓일 때 힙이 사라진다.
        return _mapTransientHeap.erase( heap ) != 0;
    }

    RHITextureHandle D3D12RHIResourceFactory::createPlacedTexture2D( const RHITextureDesc& desc, RHITransientHeapHandle heap, uint64 offsetBytes )
    {
        const auto it = _mapTransientHeap.find( heap );
        if ( it == _mapTransientHeap.end() )
        {
            SW_LOG_ERROR_ID( "D3D12RHIResourceFactory.PlacedTextureNoHeap", "createPlacedTexture2D: transient heap %# does not exist", heap.getPacked() );
            return 0;
        }
        return createTextureResource( desc, it->second.Get(), offsetBytes );
    }

    RHITextureHandle D3D12RHIResourceFactory::createTextureResource( const RHITextureDesc& desc, D3D12MA::Allocation* pHeap, uint64 offsetBytes )
    {
        if ( isRHITextureShapeValid( desc ) == false )
        {
            SW_LOG_ERROR_ID( "D3D12RHIResourceFactory.CreateTexture2DDimensionSlicesX", "createTexture2D: dimension %# with %# slices (%#x%#) is not a valid texture shape", static_cast<uint32>( desc._dimension ),
                             desc._arraySize, desc._width, desc._height );
            return 0;
        }
        // 이 장치가 샘플링하지 못하는 압축 형식(데스크톱의 ASTC · ETC2)은 드라이버에 넘기지 않는다 — 넘기면 D3D12 는 실패, Vulkan 드라이버는 0 나누기로 죽었다.
        if ( RHIFormatUtil::isBlockCompressed( desc._format ) && _pDevice->supportsTextureFormat( desc._format ) == false )
        {
            SW_LOG_ERROR_ID( "D3D12.CreateTexture2DFormatUnsupported", "createTexture2D: this device cannot sample format %# (%#x%#) - the texture is not created", static_cast<uint32>( desc._format ), desc._width, desc._height );
            return 0;
        }
        const bool                bDepth       = desc._bIsDepthStencil != SW_FALSE;
        const DXGI_FORMAT         dsvFmt       = toDXGIFormat( constant::render::kDepthStencilFormat );
        const DXGI_FORMAT         colorFmt     = toDXGIFormat( desc._format );
        const D3D12_RESOURCE_DESC resourceDesc = D3D12TextureInternal::makeResourceDesc( desc );

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
        if ( pHeap != nullptr )
        {
            // placed — 같은 힙 바이트를 다른 텍스처와 나눈다. 첫 쓰기 앞에 앨리어싱 배리어(beginTextureAlias)와 지우기가 온다(트랜지언트 풀의 규칙).
            if ( _pDevice->createAliasingResource( pHeap, offsetBytes, resourceDesc, pClearValue, texture ) == false )
                return 0;
        }
        else if ( _pDevice->createResource( D3D12_HEAP_TYPE_DEFAULT, resourceDesc, D3D12_RESOURCE_STATE_COMMON, pClearValue, L"Texture", texture ) == false )
        {
            return 0;
        }

        const RHITextureHandle                 handle  = _pDevice->storeTexture( texture, desc );
        ID3D12Resource*                        pNative = _pDevice->resolveTexture( handle );
        D3D12RHIDevice::OffscreenTextureRecord record{};
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

        // RTV · DSV 칸 발급기만 잠근다. 오프스크린 레코드는 핸들 테이블 칸에 들어가고, 핸들을 돌려주기 전이라 기록 워커가 그 칸을 아직 모른다.
        // 칸은 쪽 목록이라 상한이 없다. 쪽을 만들지 못하면(힙 메모리 부족) 뷰 없는 텍스처를 유효 핸들로 내보내지 않고 생성 실패다(R-11 — 예전엔 128 · 64 칸을 넘으면 로그 뒤 뷰 없이 나갔다).
        const uint32 mipLevels = desc._mipLevels > 0 ? desc._mipLevels : 1u;
        record._mipLevels      = mipLevels;
        bool bViewsCreated     = pNative != nullptr;
        {
            const ScopedDeferredLog deferredLog;
            ScopedLock<mutex>       offscreenLock{ _pDevice->_offscreenViewMutex };
            for ( uint32 viewIndex = 0; bViewsCreated && desc._bIsRenderTarget && viewIndex < mipLevels * desc._arraySize; ++viewIndex )
            {
                const uint32                mip   = viewIndex / desc._arraySize;
                const uint32                slice = viewIndex % desc._arraySize;
                uint32                      rtvSlot{ 0 };
                D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle{};
                if ( _pDevice->acquireOffscreenView( false, rtvSlot, rtvHandle ) == false )
                {
                    bViewsCreated = false;
                    break;
                }
                D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
                rtvDesc.Format = colorFmt;
                if ( bSliced )
                {
                    rtvDesc.ViewDimension                  = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
                    rtvDesc.Texture2DArray.MipSlice        = mip;
                    rtvDesc.Texture2DArray.FirstArraySlice = slice;
                    rtvDesc.Texture2DArray.ArraySize       = 1;
                    rtvDesc.Texture2DArray.PlaneSlice      = 0;
                }
                else
                {
                    rtvDesc.ViewDimension        = D3D12_RTV_DIMENSION_TEXTURE2D;
                    rtvDesc.Texture2D.MipSlice   = mip;
                    rtvDesc.Texture2D.PlaneSlice = 0;
                }
                _pDevice->_device->CreateRenderTargetView( pNative, &rtvDesc, rtvHandle );
                if ( viewIndex == 0 )
                {
                    record._rtvIndex  = rtvSlot;
                    record._rtvHandle = rtvHandle;
                    record._bHasRTV   = SW_TRUE;
                }
                else
                {
                    record._listExtraRTVIndex.push_back( rtvSlot );
                    record._listExtraRTVHandle.push_back( rtvHandle );
                }
            }

            for ( uint32 viewIndex = 0; bViewsCreated && bDepth && viewIndex < mipLevels * desc._arraySize; ++viewIndex )
            {
                const uint32                mip   = viewIndex / desc._arraySize;
                const uint32                slice = viewIndex % desc._arraySize;
                uint32                      dsvSlot{ 0 };
                D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle{};
                if ( _pDevice->acquireOffscreenView( true, dsvSlot, dsvHandle ) == false )
                {
                    bViewsCreated = false;
                    break;
                }
                D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
                dsvDesc.Format = dsvFmt;
                dsvDesc.Flags  = D3D12_DSV_FLAG_NONE;
                if ( bSliced )
                {
                    dsvDesc.ViewDimension                  = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
                    dsvDesc.Texture2DArray.MipSlice        = mip;
                    dsvDesc.Texture2DArray.FirstArraySlice = slice;
                    dsvDesc.Texture2DArray.ArraySize       = 1;
                }
                else
                {
                    dsvDesc.ViewDimension      = D3D12_DSV_DIMENSION_TEXTURE2D;
                    dsvDesc.Texture2D.MipSlice = mip;
                }
                _pDevice->_device->CreateDepthStencilView( pNative, &dsvDesc, dsvHandle );
                if ( viewIndex == 0 )
                {
                    record._dsvIndex  = dsvSlot;
                    record._dsvHandle = dsvHandle;
                    record._bHasDSV   = SW_TRUE;
                }
                else
                {
                    record._listExtraDSVIndex.push_back( dsvSlot );
                    record._listExtraDSVHandle.push_back( dsvHandle );
                }
            }

            if ( bViewsCreated == false )
                _pDevice->releaseOffscreenViews( record );
        }
        if ( bViewsCreated == false )
        {
            SW_LOG_ERROR_ID( "D3D12RHIResourceFactory.OffscreenViewPageFailed", "Could not create the RTV/DSV views of a %#x%# texture (%# slices) - texture creation fails", desc._width, desc._height,
                             desc._arraySize );
            (void)destroyTextureNative( handle ); // 레코드를 붙이기 전이라 칸은 위에서 이미 돌려줬다
            return 0;
        }

        D3D12RHIDevice::TextureRecord* const pTextureRecord = _pDevice->findTextureRecord( handle );
        if ( pTextureRecord == nullptr )
            reportFatalError( "D3D12: texture handle %# vanished while it was being created", handle.getPacked() );
        pTextureRecord->_offscreen  = std::move( record );
        pTextureRecord->_bOffscreen = SW_TRUE;
        pTextureRecord->_state.initialize( static_cast<uint16>( mipLevels ), static_cast<uint16>( desc._arraySize ), D3D12_RESOURCE_STATE_COMMON );
        return handle;
    }

    bool D3D12RHIResourceFactory::destroyTextureNative( RHITextureHandle texture )
    {
        if ( texture == 0 )
            return false;
        _pDevice->requireRegistryMutableNow( "destroyTexture" );
        D3D12RHIDevice::TextureRecord taken{};
        if ( _pDevice->_gpuTextures.take( texture, taken ) == false )
            return false;
        if ( taken._bOffscreen != SW_FALSE )
        {
            ScopedLock<mutex> offscreenLock{ _pDevice->_offscreenViewMutex };
            _pDevice->releaseOffscreenViews( taken._offscreen );
        }
        Microsoft::WRL::ComPtr<ID3D12Resource> owned = std::move( taken._resource );

        // 이 텍스처에 발급한 bindless 번호는 파괴가 모두 놓는다(DX11 · GL · Vulkan 과 같은 규칙 — IRHIResourceFactory::registerBindlessTexture). 남겨 두면 등록한 쪽이 해제를
        // 잊거나 늦게 한 번호가 새고, 늦은 해제가 이미 다른 리소스에 다시 발급된 번호를 놓는다.
        vector<RHIDescriptorIndex> listFreedIndex;
        {
            UniqueLock<shared_mutex> lock{ _pDevice->_bindlessMutex };
            const auto               collect = [this, &listFreedIndex]( RHIDescriptorIndex index )
            {
                _pDevice->retireBindlessReadSlot( index );
                listFreedIndex.push_back( index );
            };
            _pDevice->_srvRegistry.releaseTexture( RHIBindlessReuse::AfterFence, texture, collect );
            _pDevice->_uavRegistry.releaseTexture( RHIBindlessReuse::AfterFence, texture, collect );
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
            return true;
        }
        _pDevice->releaseTrackedResourceDeferred( std::move( owned ), RHIMemoryKey::makeTexture( texture ) );
        return true;
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

        // 반납된 슬롯이 있으면 그것부터 쓴다(펜스 뒤에 회수된 것들이다). SRV/CBV 와 UAV 가 등록표 둘에 나뉘어도 힙 인덱스 공간은 하나라 발급기도 하나다.
        uint32 index{ 0 };
        if ( _pDevice->_bindlessIndexAllocator.tryAllocate( index ) == false )
        {
            SW_LOG_ERROR_ID( "D3D12RHIResourceFactory.ShaderVisibleDescriptorHeap", "Shader visible descriptor heap overflow! Max: %#", D3D12RHIDevice::kBindlessDescriptorCapacity );
            return kInvalidDescriptorIndex;
        }
        return static_cast<RHIDescriptorIndex>( index );
    }

    bool D3D12RHIResourceFactory::isTextureReadyToSample( RHITextureHandle texture )
    {
        return _pDevice->resolveTexture( texture ) != nullptr && _pDevice->isTextureFirstUploadComplete( texture );
    }

    RHITextureSRVIndex D3D12RHIResourceFactory::registerBindlessTexture( RHITextureHandle texture )
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
            const D3D12RHIDevice::TextureRecord* const pTextureRecord = _pDevice->findTextureRecord( texture );
            if ( pTextureRecord != nullptr && pTextureRecord->_bOffscreen != SW_FALSE && pTextureRecord->_offscreen._dimension != RHITextureDimension::Texture2D )
            {
                SW_LOG_ERROR_ID( "D3D12RHIResourceFactory.RegisterBindlessTextureTextureTextureOnly", "registerBindlessTexture: texture %# is a %# texture — only Texture2D can be bound to the bindless texture table yet",
                                 texture.getPacked(), static_cast<uint32>( pTextureRecord->_offscreen._dimension ) );
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

        writeBindlessRecord( BindlessRegistry::ShaderResource, index, pResource, handle, 0, texture, RHIBindlessLink::Owned );
        return RHITextureSRVIndex{ index };
    }

    RHIBufferSRVIndex D3D12RHIResourceFactory::registerBindlessBufferSRV( RHIBufferHandle buffer )
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

            writeBindlessRecord( BindlessRegistry::ShaderResource, index, pResource, handle, buffer, 0, RHIBindlessLink::Owned );
            return RHIBufferSRVIndex{ index };
        }

        // 상수 버퍼는 루트 CBV 로만 건다(resolveBufferAddress 가 이번 링 슬롯 주소를 낸다). 힙에는 뷰를 만들지 않는다 — 셰이더는 힙에서 CBV 를 읽지 않고
        // (b# 는 루트 서술자, 힙 테이블은 t# · u# 범위다), 예전엔 등록 때 두 개를 만들고 프레임마다 링 슬롯으로 다시 썼다(R-7). 번호는 바인딩 열쇠로만 쓴다.
        const D3D12RHIDevice::BufferRecord* const pRecord = _pDevice->findBufferRecord( buffer );
        if ( pRecord == nullptr || pRecord->_constantAlignedSize == 0 )
        {
            // 링이 아닌 버퍼: 루트 CBV 가 읽는 256 바이트보다 좁으면 상수 버퍼가 될 수 없다.
            // 집은 인덱스를 돌려준다. 그냥 반환하면 좁은 버퍼를 등록할 때마다 힙 슬롯이 하나씩 샌다(잠금은 쥐고 있다).
            if ( pResource->GetDesc().Width < D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT )
            {
                _pDevice->_bindlessIndexAllocator.free( index );
                return kInvalidDescriptorIndex;
            }
        }

        writeBindlessRecord( BindlessRegistry::ShaderResource, index, pResource, handle, buffer, 0, RHIBindlessLink::Owned );
        return RHIBufferSRVIndex{ index };
    }

    RHIBufferSRVIndex D3D12RHIResourceFactory::registerBindlessBufferSRV( const RHIBufferSlice& slice )
    {
        _pDevice->requireRegistryMutableNow( "registerBindlessBufferSRV(slice)" );
        if ( slice._buffer == 0 || _pDevice->_cbvHeap == nullptr )
            return kInvalidDescriptorIndex;
        auto* pResource = _pDevice->resolveBuffer( slice._buffer );
        if ( pResource == nullptr )
            return kInvalidDescriptorIndex;

        UniqueLock<shared_mutex> lock{ _pDevice->_bindlessMutex };
        // 루트 CBV 는 링 칸 주소 + 구간 오프셋이다(`resolveBufferAddress` 가 프레임 칸을 더한다). 링 페이지가 아니거나 칸을 넘는 구간은 부르는 쪽의 결함이다.
        const D3D12RHIDevice::BufferRecord* const pRecord           = _pDevice->findBufferRecord( slice._buffer );
        const bool                                bSliceOutsidePage = pRecord == nullptr || static_cast<uint64>( slice._offset ) + slice._size > pRecord->_constantAlignedSize;
        const bool                                bSliceMisaligned  = slice._offset % D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT != 0 || slice._size == 0;
        if ( bSliceOutsidePage || bSliceMisaligned )
        {
            lock.unlock(); // 잠금은 잎 잠금이다 — 로그는 놓은 뒤에 남긴다
            SW_LOG_ERROR_ID( "D3D12RHIResourceFactory.BadConstantBufferSlice", "registerBindlessBufferSRV: slice (offset %#, size %#) is not a 256-aligned slice of a constant buffer page",
                             slice._offset, slice._size );
            return kInvalidDescriptorIndex;
        }
        const RHIDescriptorIndex index = acquireBindlessIndex( lock );
        if ( index == kInvalidDescriptorIndex )
            return kInvalidDescriptorIndex;
        writeBindlessRecord( BindlessRegistry::ShaderResource, index, pResource, bindlessHandlesAt( index ), slice._buffer, 0, RHIBindlessLink::Owned, slice._offset );
        return RHIBufferSRVIndex{ index };
    }

    void D3D12RHIResourceFactory::unregisterBindlessResource( RHIBufferSRVIndex index )
    {
        _pDevice->requireRegistryMutableNow( "unregisterBindlessResource" );
        releaseBindlessSlot( index );
    }

    bool D3D12RHIResourceFactory::rebindBindlessTexture( RHITextureSRVIndex index, RHITextureHandle texture )
    {
        _pDevice->requireRegistryMutableNow( "rebindBindlessTexture" );
        ID3D12Resource* const pResource = texture != 0 ? _pDevice->resolveTexture( texture ) : nullptr;
        if ( pResource == nullptr || _pDevice->isTextureReadyToRegister( texture ) == false )
            return false;
        const D3D12RHIDevice::TextureRecord* const pTextureRecord = _pDevice->findTextureRecord( texture );
        if ( pTextureRecord != nullptr && pTextureRecord->_bOffscreen != SW_FALSE && pTextureRecord->_offscreen._dimension != RHITextureDimension::Texture2D )
            return false;

        UniqueLock<shared_mutex>                      lock{ _pDevice->_bindlessMutex };
        const D3D12RHIDevice::BindlessResourceRecord* pPrevious = _pDevice->_srvRegistry.find( index );
        if ( pPrevious == nullptr || pPrevious->_texture == 0 )
            return false; // 산 텍스처 번호가 아니다(버퍼 번호를 텍스처로 다시 묶지 않는다)

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.ViewDimension                 = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping       = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels           = pResource->GetDesc().MipLevels;
        const DXGI_FORMAT resourceFormat      = pResource->GetDesc().Format;
        srvDesc.Format                        = ( resourceFormat == DXGI_FORMAT_R24G8_TYPELESS || resourceFormat == DXGI_FORMAT_D24_UNORM_S8_UINT ) ? DXGI_FORMAT_R24_UNORM_X8_TYPELESS : resourceFormat;
        srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

        // 오프라인 뷰는 지금 덮는다 — 슬롯 테이블은 기록 때 복사해 가고 병렬 기록 중에는 여기 오지 않는다. 셰이더 가시 사본은 진행 중 프레임이 읽으므로
        // 덮지 않고 링 슬롯마다 그 차례에 쓴다(`_bindlessRingMirror` → `flushBindlessRingSlot`). 늦어도 다음 프레임부터 새 텍스처다.
        const BindlessHandleSet handle = bindlessHandlesAt( index );
        _pDevice->_device->CreateShaderResourceView( pResource, &srvDesc, handle._offline );
        D3D12RHIDevice::BindlessResourceRecord record{ pResource, handle._cpu, handle._offline };
        record._texture = texture;
        if ( _pDevice->_srvRegistry.rebind( index, record ) == false )
            return false;
        _pDevice->publishBindlessReadSlot( index, D3D12RHIDevice::BindlessReadRegistry::ShaderResource, record );
        _pDevice->_bindlessRingMirror.recordRewrite( index );
        return true;
    }

    RHITextureSRVIndex D3D12RHIResourceFactory::reserveBindlessTextureIndex()
    {
        // 힙 번호 발급기만 건드린다 — 뷰 · 읽기 칸은 그대로라 기록 경로가 읽는 상태를 바꾸지 않는다(어느 스레드든).
        UniqueLock<shared_mutex> lock{ _pDevice->_bindlessMutex };
        return RHITextureSRVIndex{ acquireBindlessIndex( lock ) };
    }

    bool D3D12RHIResourceFactory::activateReservedBindlessTexture( RHITextureSRVIndex index, RHITextureHandle texture )
    {
        _pDevice->requireRegistryMutableNow( "activateReservedBindlessTexture" );
        ID3D12Resource* const pResource = texture != 0 && _pDevice->_cbvHeap != nullptr ? _pDevice->resolveTexture( texture ) : nullptr;
        if ( pResource == nullptr || _pDevice->isTextureReadyToRegister( texture ) == false )
            return false;
        const D3D12RHIDevice::TextureRecord* const pTextureRecord = _pDevice->findTextureRecord( texture );
        if ( pTextureRecord != nullptr && pTextureRecord->_bOffscreen != SW_FALSE && pTextureRecord->_offscreen._dimension != RHITextureDimension::Texture2D )
            return false;

        UniqueLock<shared_mutex> lock{ _pDevice->_bindlessMutex };
        if ( _pDevice->_srvRegistry.isLive( index ) || _pDevice->_uavRegistry.isLive( index ) )
            return false;
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.ViewDimension                 = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping       = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels           = pResource->GetDesc().MipLevels;
        const DXGI_FORMAT resourceFormat      = pResource->GetDesc().Format;
        srvDesc.Format                        = ( resourceFormat == DXGI_FORMAT_R24G8_TYPELESS || resourceFormat == DXGI_FORMAT_D24_UNORM_S8_UINT ) ? DXGI_FORMAT_R24_UNORM_X8_TYPELESS : resourceFormat;
        srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
        // 새 번호다(예약은 펜스 뒤에야 다시 나간 번호를 준다) — 등록과 같이 힙 · 오프라인 뷰를 쓰고 모든 링 사본에 복사한다(`writeBindlessRecord`).
        const BindlessHandleSet handle = bindlessHandlesAt( index );
        _pDevice->_device->CreateShaderResourceView( pResource, &srvDesc, handle._cpu );
        _pDevice->_device->CreateShaderResourceView( pResource, &srvDesc, handle._offline );
        writeBindlessRecord( BindlessRegistry::ShaderResource, index, pResource, handle, 0, texture, RHIBindlessLink::Borrowed ); // 예약한 번호는 받은 쪽의 것이다
        return true;
    }

    void D3D12RHIResourceFactory::unregisterBindlessTexture( RHITextureSRVIndex index )
    {
        // DX12 는 텍스처와 버퍼가 같은 셰이더 가시 힙을 나눠 쓰므로 인덱스 공간이 하나다.
        _pDevice->requireRegistryMutableNow( "unregisterBindlessTexture" );
        releaseBindlessSlot( index );
    }

    void D3D12RHIResourceFactory::releaseBindlessRecord( BindlessRegistry registry, RHIDescriptorIndex index )
    {
        const bool bUAV = ( registry == BindlessRegistry::UnorderedAccess );

        const ScopedDeferredLog                                      deferredLog;
        UniqueLock<shared_mutex>                                     lock{ _pDevice->_bindlessMutex };
        RHIBindlessRegistry<D3D12RHIDevice::BindlessResourceRecord>& target = bUAV ? _pDevice->_uavRegistry : _pDevice->_srvRegistry;
        // 이미 빈 슬롯을 다시 프리리스트에 넣으면 같은 인덱스가 두 리소스에 발급된다 — 등록표가 거절한다.
        if ( target.release( index, RHIBindlessReuse::AfterFence ) == false )
        {
            SW_LOG_ERROR_ID( "D3D12RHIResourceFactory.IndexAlreadyFreeIgnoring", "%# index %# is already free; ignoring the duplicate release.",
                             bUAV ? "Bindless UAV" : "Bindless", index.getValue() );
            return;
        }
        _pDevice->retireBindlessReadSlot( index );
        deferFreeBindlessIndex( index ); // 힙 인덱스 공간이 하나라 발급기도 하나다
    }

    void D3D12RHIResourceFactory::writeBindlessRecord( BindlessRegistry registry, RHIDescriptorIndex index, ID3D12Resource* pResource,
                                                       const BindlessHandleSet& handle, RHIBufferHandle buffer, RHITextureHandle texture, RHIBindlessLink link, uint32 sliceOffset )
    {
        RHIBindlessRegistry<D3D12RHIDevice::BindlessResourceRecord>& target =
            ( registry == BindlessRegistry::UnorderedAccess ) ? _pDevice->_uavRegistry : _pDevice->_srvRegistry;
        D3D12RHIDevice::BindlessResourceRecord record{ pResource, handle._cpu, handle._offline };
        record._buffer      = buffer;
        record._texture     = texture;
        record._sliceOffset = sliceOffset;
        // 번호는 `acquireBindlessIndex` 가 발급기에서 받았다 — 산 칸이면 발급기를 거치지 않은 번호라 멈춘다.
        if ( target.assign( index, record, link ) == false )
            reportFatalError( "D3D12: bindless index %# was written twice without a release", index.getValue() );
        _pDevice->publishBindlessReadSlot( index, registry == BindlessRegistry::UnorderedAccess ? D3D12RHIDevice::BindlessReadRegistry::UnorderedAccess : D3D12RHIDevice::BindlessReadRegistry::ShaderResource,
                                           record );
        // 새 번호는 모든 링 사본에 바로 쓴다 — 펜스 뒤에야 다시 나간 번호라 진행 중 프레임이 읽지 않는다(R-6).
        _pDevice->copyBindlessDescriptorToAllRingSlots( index );
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
            pDevice->_bindlessIndexAllocator.free( index );
        } ),
                                                   _pDevice->_fenceValue );
    }

    RHIUnorderedAccessIndex D3D12RHIResourceFactory::registerBindlessBufferUAV( RHIBufferHandle buffer )
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

        writeBindlessRecord( BindlessRegistry::UnorderedAccess, index, pResource, handle, buffer, 0, RHIBindlessLink::Owned );
        return RHIUnorderedAccessIndex{ index };
    }

    RHIUnorderedAccessIndex D3D12RHIResourceFactory::registerBindlessTextureUAV( RHITextureHandle texture )
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

        writeBindlessRecord( BindlessRegistry::UnorderedAccess, index, pResource, handle, 0, texture, RHIBindlessLink::Owned );
        return RHIUnorderedAccessIndex{ index };
    }

    void D3D12RHIResourceFactory::unregisterBindlessUAV( RHIUnorderedAccessIndex index )
    {
        _pDevice->requireRegistryMutableNow( "unregisterBindlessUAV" );
        releaseBindlessRecord( BindlessRegistry::UnorderedAccess, index );
    }

    RHIPipelineStateHandle D3D12RHIResourceFactory::createPipelineStateNative( const RHIPipelineStateDesc& desc, const RHICompiledGraphicsStages& stages )
    {
        Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
        const uint32                                vertexSlotMask = stages._vertexSlotMask;

        // 셰이더 컴파일 · 정점 리플렉션은 앞단(`IRHIResourceFactory::createPipelineState`)이 끝냈다. 여기서는 바이트코드로 PSO 서술만 채운다.
        const bool bHasPixelShader = stages._bHasPixelShader != SW_FALSE;
        if ( stages._bHasVertexShader != SW_FALSE )
        {
            // 입력 레이아웃은 메시 정점 형식의 공용 테이블에서 셰이더가 읽는 속성만 골라 만든다(`fillDXGIInputElements` — DX11 과 같은 함수, Vulkan · GL 도 같은 규칙).
            // 정점 입력이 없는 셰이더(SV_VertexID 로 정점을 만든다)는 빈 레이아웃이라 정점 버퍼 없이 그린다.
            D3D12_INPUT_ELEMENT_DESC arrInputElement[constant::render::kVertexAttributeCount]{};
            const uint32             elementCount = fillDXGIInputElements<D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA>( desc._meshVertexFormat, stages._consumedAttributeMask, arrInputElement );

            D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
            psoDesc.InputLayout    = { elementCount > 0 ? arrInputElement : nullptr, elementCount };
            psoDesc.pRootSignature = _pDevice->_rootSignature.Get();
            psoDesc.VS             = { stages._vertex._bytecode.data(), stages._vertex._bytecode.size() };
            if ( stages._bHasGeometryShader != SW_FALSE )
                psoDesc.GS = { stages._geometry._bytecode.data(), stages._geometry._bytecode.size() };
            if ( stages._bHasTessellation != SW_FALSE )
            {
                psoDesc.HS = { stages._hull._bytecode.data(), stages._hull._bytecode.size() };
                psoDesc.DS = { stages._domain._bytecode.data(), stages._domain._bytecode.size() };
            }
            if ( bHasPixelShader )
                psoDesc.PS = { stages._pixel._bytecode.data(), stages._pixel._bytecode.size() };
            const RenderState& renderState = desc._renderState;
            // 다른 백엔드와 같이 채우기 모드를 읽는다. SOLID 로 고정하면 Wireframe 요청이 DX12 에서만 조용히 솔리드가 된다.
            psoDesc.RasterizerState.FillMode              = ( renderState._fillMode == FillMode::Wireframe )
                                                              ? D3D12_FILL_MODE_WIREFRAME
                                                              : D3D12_FILL_MODE_SOLID;
            psoDesc.RasterizerState.CullMode              = ( renderState._cullMode == CullMode::Front )
                                                              ? D3D12_CULL_MODE_FRONT
                                                              : ( ( renderState._cullMode == CullMode::Back ) ? D3D12_CULL_MODE_BACK : D3D12_CULL_MODE_NONE );
            psoDesc.RasterizerState.FrontCounterClockwise = renderState._frontFace == FrontFace::CounterClockwise ? TRUE : FALSE;
            // 기본은 가까운 · 먼 평면에서 자른다 — DX11(DepthClipEnable) · Vulkan(depthClampEnable 끔) · GL(GL_DEPTH_CLAMP 끔)과 같다. 0 으로 두면 DX12 만 평면 밖 기하를 깊이 1 로 눌러 그린다
            // (psoDesc{} 의 기본값이 FALSE 다). 시야 거리 설정(gv_viewDistanceScale)이 먼 평면을 당겨도 DX12 에서만 먼 기하가 남았다(useroptions distance 0.307, 나머지 0.665).
            psoDesc.RasterizerState.DepthClipEnable = renderState._bEnableDepthClip != SW_FALSE ? TRUE : FALSE;
            // 깊이 바이어스 — 다른 백엔드와 같은 상수 · 기울기 두 칸이다(FixedRenderState::_depthBias).
            psoDesc.RasterizerState.DepthBias            = renderState._depthBias;
            psoDesc.RasterizerState.SlopeScaledDepthBias = renderState._slopeScaledDepthBias;
            psoDesc.SampleMask                           = MathUtil::kMaxUInt32;
            psoDesc.PrimitiveTopologyType                = stages._bHasTessellation != SW_FALSE ? D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH : D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            // 뎁스 전용은 RT 가 0 개다. 1 로 두고 R8G8B8A8 을 선언하면 실제로는 DSV 만 바인딩되는 것과 어긋난다.
            psoDesc.NumRenderTargets = stages._numRenderTargets;
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
                auto&                   rtBlend = psoDesc.BlendState.RenderTarget[rtvIndex];
                const BlendTargetState& source  = renderState._arrBlendTarget[rtvIndex < kMaxColorAttachments ? rtvIndex : 0u];
                rtBlend.RenderTargetWriteMask   = static_cast<UINT8>( source._colorWriteMask ); // 비트가 같다(ColorWriteMask)
                psoDesc.RTVFormats[rtvIndex]    = toDXGIFormat( desc._arrRTVFormat[rtvIndex] );
                rtBlend.BlendEnable             = source._bEnableBlend != SW_FALSE ? TRUE : FALSE;
                rtBlend.SrcBlend                = RHIDxRenderState::toBlend<D3D12_BLEND>( source._srcColor );
                rtBlend.DestBlend               = RHIDxRenderState::toBlend<D3D12_BLEND>( source._dstColor );
                rtBlend.BlendOp                 = RHIDxRenderState::toBlendOp<D3D12_BLEND_OP>( source._colorOp );
                rtBlend.SrcBlendAlpha           = RHIDxRenderState::toBlend<D3D12_BLEND>( source._srcAlpha );
                rtBlend.DestBlendAlpha          = RHIDxRenderState::toBlend<D3D12_BLEND>( source._dstAlpha );
                rtBlend.BlendOpAlpha            = RHIDxRenderState::toBlendOp<D3D12_BLEND_OP>( source._alphaOp );
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
            const HRESULT hrPSO = _pDevice->createGraphicsPipeline( psoDesc, pso );
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
            stages._bHasTessellation != SW_FALSE ? D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST : D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
            desc._renderState._stencilRef
        } );
    }

    RHIPipelineStateHandle D3D12RHIResourceFactory::createComputePipelineStateNative( const RHICompiledShaderStage& computeStage, string_view label )
    {
        Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
        {
            D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
            psoDesc.pRootSignature = _pDevice->_rootSignature.Get();
            psoDesc.CS             = { computeStage._bytecode.data(), computeStage._bytecode.size() };

            const HRESULT hr = _pDevice->createComputePipeline( psoDesc, pso );
            if ( FAILED( hr ) )
            {
                SW_LOG_ERROR_ID( "D3D12RHIResourceFactory.CreateComputePipelineStateFailedHrX", "CreateComputePipelineState failed hr=0x%# '%#'", static_cast<uint32>( hr ), label );
                _pDevice->flushDebugMessages( "CreateComputePipelineState" );
                return 0;
            }
        }
        if ( pso == nullptr )
            return 0;
        return _pDevice->_pipelineStates.insert( { pso, RHIVertexLayout{} } ); // 컴퓨트는 정점 입력이 없다
    }

    bool D3D12RHIResourceFactory::destroyPipelineStateNative( RHIPipelineStateHandle pso )
    {
        if ( pso == 0 )
            return false;
        D3D12RHIDevice::D3D12PipelineStateRecord record{};
        if ( _pDevice->_pipelineStates.take( pso, record ) == false )
            return false;
        if ( _pDevice->_frameStreamState._activeGraphicsPSO == pso )
            _pDevice->_frameStreamState._activeGraphicsPSO = 0;
        _pDevice->_releaseQueue.enqueueGPUKeepAlive( record._pso, _pDevice->_fenceValue );
        return true;
    }
} // namespace sw
#endif
