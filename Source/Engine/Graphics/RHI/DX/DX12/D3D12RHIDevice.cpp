#include "pch.h"

#include "Engine/Graphics/RHI/DX/DX12/D3D12RHIDevice.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Diagnostics/CrashHandler.h"
#include "Core/Foundation/BuildInfo.h"
#include "Core/Log/LogOnce.h"

#include "Engine/Graphics/Debug/GPUBreadcrumbs.h"
#include "Engine/Graphics/RHI/DX/DX12/D3D12RHICommandContext.h"
#include "Engine/Graphics/RHI/DX/DX12/D3D12RHICommandList.h"
#include "Engine/Graphics/RHI/DX/DX12/D3D12RHIResourceFactory.h"
#include "Engine/Graphics/RHI/DX/DX12/D3D12RHIResourcePreset.h"
#include "Engine/Graphics/RHI/Diagnostics/RHIMemoryTracker.h"
#include "Engine/Graphics/RHI/Diagnostics/RHITimestampFrame.h"
#include "Engine/Graphics/RHI/Transfer/RHIStagingRingUtil.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Engine/Graphics/RHI/Backend/RHILiveCommandListUtil.h"
    #include "Engine/Foundation/EngineServices.h"
    #include "Engine/Profiling/FrameProfiler.h"
    #include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
    #include "Engine/Foundation/EnginePlatformHeaders.h"
    #include "Engine/Config/EngineDefaultAssets.h"
    #include "Engine/Graphics/RHI/DX/DXGIAdapter.h"
    #include "Engine/Graphics/RHI/DX/DXGIFormat.h"
    #include "Engine/Graphics/RHI/DX/DXGIMemoryBudget.h"
    #include "Engine/Graphics/Shader/Compile/ShaderCache.h"

namespace sw
{
    D3D12RHIDevice::D3D12RHIDevice( uint32 deviceGeneration )
        : _device{ nullptr }
        , _memoryAdapter{ nullptr }
        , _commandQueue{ nullptr }
        , _computeQueue{ nullptr }
        , _queueSyncFence{ nullptr }
        , _computeFence{ nullptr }
        , _queueSyncValue{ 0 }
        , _computeFenceValue{ 0 }
        , _computeWaitedValue{ 0 }
        , _rtvHeap{ nullptr }
        , _dsvHeap{ nullptr }
        , _cbvHeap{ nullptr }
        , _offlineViewHeap{ nullptr }
        , _rootSignature{ nullptr }
        , _drawCommandSignature{ nullptr }
        , _drawIndexedCommandSignature{ nullptr }
        , _dispatchCommandSignature{ nullptr }
        , _arrCommandAllocator{}
        , _commandList{ nullptr }
        , _pActiveFrameList{ nullptr }
        , _cmdListPoolMutex{}
        , _listFreeCmdListEntry{}
        , _listFreeComputeCmdListEntry{}
        , _onlineBlockMutex{}
        , _freeOnlineBlock{}
        , _frameRing{}
        , _timestampFrequency{ 0 }
        , _timestampWrittenMask{}
        , _arrTimestampMask{}
        , _gpuBuffers{}
        , _gpuTextures{}
        , _resourceStateMutex{}
        , _mapStructuredBufferState{}
        , _mapOffscreenTexture{}
        , _nextOffscreenRTVIndex{ 0 }
        , _nextOffscreenDSVIndex{ 0 }
        , _freeOffscreenRTV{}
        , _freeOffscreenDSV{}
        , _mapCbAlignedSize{}
        , _mapCbMapped{}
        , _constantBufferMirror{}
        , _pipelineStates{}
        , _renderPassRegistry{}
        , _swapChain{}
        , _bBindlessRootSignature{ SW_FALSE }
        , _reservedPassFlags{ 0 }
        , _bDeviceRemovedLogged{ SW_FALSE }
        , _arrBreadcrumb{}
        , _bBlitMismatchLogged{ SW_FALSE }
        , _bOnlineHeapExhaustedLogged{ SW_FALSE }
        , _bTimestampEnabled{ SW_FALSE }
        , _validationMessageCount{ 0 }
        , _validationCallbackCookie{ 0 }
        , _frameStreamState{}
        , _listRegisteredBindless{}
        , _freeBindless{}
        , _listRegisteredUAV{}
        , _rtvDescriptorSize{ 0 }
        , _cbvDescriptorSize{ 0 }
        , _allocatedDescriptorsCount{ 0 }
        , _cmdListEntryCreated{ 0 }
        , _fence{ nullptr }
        , _fenceValue{ 0 }
        , _transferSerials{ deviceGeneration }
        , _transferFence{ nullptr }
        , _transferFenceValue{ 0 }
        , _transferSubmitMutex{}
        , _transferFenceQueue{ _transferSerials }
        , _uploadRing{}
        , _transferJobMutex{}
        , _listFreeTransferJob{}
        , _textureTransferMutex{}
        , _mapTextureTransferMarks{}
        , _readbackTable{}
        , _releaseQueue{ constant::render::kGPUReleaseFrameLatency }
        , _transferReleaseQueue{}
        , _frameStreamContext{ nullptr }
        , _resourceImpl{ nullptr }
        , _timestampProvider{ *this }
    {
        _resourceImpl       = sw::make_unique<D3D12RHIResourceFactory>( this );
        _pTimestampProvider = &_timestampProvider;
    }

    D3D12RHIDevice::~D3D12RHIDevice()
    {
        shutdown();
    }

    void* D3D12RHIDevice::getNativeTexturePointer( RHITextureHandle texture ) const
    {
        return resolveTexture( texture );
    }

    // ------------------------------------------------------------------------------
    // 리소스 조회 · 디스크립터 힙 · 온라인 블록 (D3D12RHIResourceFactory 와 컨텍스트가 쓴다)
    // ------------------------------------------------------------------------------

    ID3D12Resource* D3D12RHIDevice::resolveBuffer( RHIBufferHandle handle ) const
    {
        const Microsoft::WRL::ComPtr<ID3D12Resource>* slot = _gpuBuffers.get( handle );
        return slot != nullptr ? slot->Get() : nullptr;
    }

    ID3D12Resource* D3D12RHIDevice::resolveTexture( RHITextureHandle handle ) const
    {
        const Microsoft::WRL::ComPtr<ID3D12Resource>* slot = _gpuTextures.get( handle );
        return slot != nullptr ? slot->Get() : nullptr;
    }

    D3D12_RESOURCE_STATES D3D12RHIDevice::getTrackedTextureState( RHITextureHandle texture )
    {
        ScopedLock<mutex> lock{ _resourceStateMutex };
        const auto        offscreenIt = _mapOffscreenTexture.find( texture );
        if ( offscreenIt == _mapOffscreenTexture.end() )
            return D3D12_RESOURCE_STATE_COMMON;
        return offscreenIt->second._state;
    }

    bool D3D12RHIDevice::findOffscreenTargetView( RHITextureHandle texture, uint32 slice, OffscreenTargetView& outView )
    {
        ScopedLock<mutex> lock{ _resourceStateMutex };
        const auto        it = _mapOffscreenTexture.find( texture );
        if ( it == _mapOffscreenTexture.end() )
            return false;

        const OffscreenTextureRecord& record = it->second;
        outView                              = OffscreenTargetView{};
        outView._state                       = record._state;
        outView._format                      = record._format;
        outView._bHasRTV                     = record._bHasRTV;
        outView._bHasDSV                     = record._bHasDSV;
        if ( slice >= record._arraySize )
            return true;

        // 면 0 은 레코드의 첫 뷰, 면 1.. 은 추가 인덱스다. 디스크립터가 바닥나 만들다 멈춘 면은 목록에 없다.
        const size_t extraIndex = static_cast<size_t>( slice ) - 1u;
        if ( record._bHasRTV != SW_FALSE )
        {
            if ( slice == 0 )
                outView._rtvHandle = record._rtvHandle;
            else if ( extraIndex < record._listExtraRTVIndex.size() )
                outView._rtvHandle = getOffscreenRTVHandle( record._listExtraRTVIndex[extraIndex] );
        }
        if ( record._bHasDSV != SW_FALSE )
        {
            if ( slice == 0 )
                outView._dsvHandle = record._dsvHandle;
            else if ( extraIndex < record._listExtraDSVIndex.size() )
                outView._dsvHandle = getOffscreenDSVHandle( record._listExtraDSVIndex[extraIndex] );
        }
        outView._bSliceInRange = ( outView._rtvHandle.ptr != 0 || outView._dsvHandle.ptr != 0 ) ? SW_TRUE : SW_FALSE;
        return true;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE D3D12RHIDevice::getOffscreenRTVHandle( uint32 rtvIndex ) const
    {
        D3D12_CPU_DESCRIPTOR_HANDLE handle = _rtvHeap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>( rtvIndex ) * _rtvDescriptorSize;
        return handle;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE D3D12RHIDevice::getOffscreenDSVHandle( uint32 dsvIndex ) const
    {
        D3D12_CPU_DESCRIPTOR_HANDLE handle = _dsvHeap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>( dsvIndex ) * _device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_DSV );
        return handle;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE D3D12RHIDevice::offlineDescriptorAt( uint32 index ) const
    {
        D3D12_CPU_DESCRIPTOR_HANDLE handle{};
        if ( _offlineViewHeap == nullptr || index >= kOfflineDescriptorCount )
            return handle;
        handle = _offlineViewHeap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>( index ) * _cbvDescriptorSize;
        return handle;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE D3D12RHIDevice::shaderVisibleCPUAt( uint32 index ) const
    {
        D3D12_CPU_DESCRIPTOR_HANDLE handle{};
        if ( _cbvHeap == nullptr || index >= kMaxShaderVisibleDescriptors )
            return handle;
        handle = _cbvHeap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>( index ) * _cbvDescriptorSize;
        return handle;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE D3D12RHIDevice::shaderVisibleGPUAt( uint32 index ) const
    {
        D3D12_GPU_DESCRIPTOR_HANDLE handle{};
        if ( _cbvHeap == nullptr || index >= kMaxShaderVisibleDescriptors )
            return handle;
        handle = _cbvHeap->GetGPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<UINT64>( index ) * _cbvDescriptorSize;
        return handle;
    }

    uint32 D3D12RHIDevice::acquireOnlineBlock()
    {
        ScopedLock<mutex> lock{ _onlineBlockMutex };
        // 프레임 끝까지 기다리지 않고 펜스가 지난 묶음을 먼저 거둔다 — 한 프레임에 리스트가 많으면(미리보기 씬 · 컴퓨트) 블록 수보다 리스트가 많다.
        if ( _freeOnlineBlock.empty() )
            recycleCompletedOnlineBlocksLocked( _fence->GetCompletedValue() );
        if ( _freeOnlineBlock.empty() )
        {
            if ( _bOnlineHeapExhaustedLogged == SW_FALSE )
            {
                _bOnlineHeapExhaustedLogged = SW_TRUE;
                // 실제 동작은 건너뛰기다 — flushSlotTables 가 false 를 받아 드로우 · 디스패치를 내지 않는다(앞 테이블로 그리지 않는다).
                SW_LOG_ERROR_ID( "D3D12.OutOnlineDescriptorBlocks", "Out of online descriptor blocks (%#x%#) - draws and dispatches that need a new slot table are skipped.",
                                 kOnlineBlockCount, kOnlineBlockDescriptorCount );
            }
            return UINT32_MAX;
        }
        uint32 block{ 0 };
        (void)_freeOnlineBlock.tryPop( block ); // 위에서 비지 않았음을 봤다
        return block;
    }

    void D3D12RHIDevice::releaseOnlineBlocksDeferred( D3D12RecordingState& state )
    {
        state._onlineCursor = 0;
        state._onlineEnd    = 0;
        if ( state._listOnlineBlock.empty() )
            return;

        // GPU 가 이 리스트의 테이블을 아직 읽는 중이다. 현재 펜스가 지난 뒤에야 블록을 다시 내준다.
        // 벡터를 복사하지 않고 통째로 묶음에 옮긴다. 상태는 풀에서 온 빈 벡터를 받아 다음 기록에 그대로 쓴다.
        ScopedLock<mutex>       lock{ _onlineBlockMutex };
        OnlineBlockRecycleBatch batch{};
        batch._fence = _fenceValue.load();
        if ( _listOnlineRecyclePool.empty() == false )
        {
            batch._listBlock.swap( _listOnlineRecyclePool.back() );
            _listOnlineRecyclePool.pop_back();
        }
        batch._listBlock.swap( state._listOnlineBlock );
        _listPendingOnlineRecycle.push_back( std::move( batch ) );
    }

    void D3D12RHIDevice::recycleCompletedOnlineBlocks( uint64 completedFence )
    {
        ScopedLock<mutex> lock{ _onlineBlockMutex };
        recycleCompletedOnlineBlocksLocked( completedFence );
    }

    void D3D12RHIDevice::recycleCompletedOnlineBlocksLocked( uint64 completedFence )
    {
        for ( size_t index = 0; index < _listPendingOnlineRecycle.size(); )
        {
            OnlineBlockRecycleBatch& batch = _listPendingOnlineRecycle[index];
            if ( batch._fence > completedFence )
            {
                ++index;
                continue;
            }
            for ( const uint32 block : batch._listBlock )
            {
                _freeOnlineBlock.push( block );
            }
            batch._listBlock.clear();
            _listOnlineRecyclePool.push_back( std::move( batch._listBlock ) );
            VectorUtil::removeAtSwap( _listPendingOnlineRecycle, index );
        }
    }

    RHIBufferHandle D3D12RHIDevice::storeBuffer( Microsoft::WRL::ComPtr<ID3D12Resource> buffer )
    {
        if ( buffer == nullptr )
            return 0;
        const uint64 bytes = computeAllocationBytes( buffer.Get() );
    #if defined( SW_DEBUG )
        ID3D12Resource* pNamed = buffer.Get();
    #endif
        const RHIBufferHandle handle = _gpuBuffers.insert( std::move( buffer ) );
        getMemoryTracker().recordAllocation( RHIMemoryKey::makeBuffer( handle ), RHIMemoryKind::Buffer, bytes );
    #if defined( SW_DEBUG )
        nameResourceForDiagnostics( pNamed, L"Buffer", handle );
    #endif
        return handle;
    }

    uint64 D3D12RHIDevice::computeAllocationBytes( ID3D12Resource* pResource ) const
    {
        if ( pResource == nullptr || _device == nullptr )
            return kRHIMemoryUnknownBytes;
        const D3D12_RESOURCE_DESC            desc = pResource->GetDesc();
        const D3D12_RESOURCE_ALLOCATION_INFO info = _device->GetResourceAllocationInfo( 0, 1, &desc );
        // 서술이 잘못됐으면 SizeInBytes 가 UINT64_MAX 다. 지어내지 않고 "크기 모름" 으로 센다.
        return info.SizeInBytes == UINT64_MAX ? kRHIMemoryUnknownBytes : static_cast<uint64>( info.SizeInBytes );
    }

    void D3D12RHIDevice::releaseTrackedResourceDeferred( Microsoft::WRL::ComPtr<ID3D12Resource> owned, const RHIMemoryKey& key )
    {
        const uint64 releaseFence = _fenceValue.load();
        // 키 공간은 템플릿 인자로 갈라 람다를 델리게이트의 인라인 칸(24 바이트)에 맞춘다.
        if ( key._space == RHIMemoryKeySpace::Texture )
            _releaseQueue.enqueueGPUTrackedRelease<RHIMemoryKeySpace::Texture>( owned, getMemoryTracker(), key._id, releaseFence );
        else
            _releaseQueue.enqueueGPUTrackedRelease<RHIMemoryKeySpace::Buffer>( owned, getMemoryTracker(), key._id, releaseFence );
    }

    bool D3D12RHIDevice::createMappedUploadBuffer( uint64 sizeBytes, Microsoft::WRL::ComPtr<ID3D12Resource>& outBuffer, void*& pOutMapped )
    {
        pOutMapped = nullptr;
        outBuffer.Reset();

        const D3D12_HEAP_PROPERTIES heapProps    = D3D12RHIResourcePreset::heapProperties( D3D12_HEAP_TYPE_UPLOAD );
        const D3D12_RESOURCE_DESC   resourceDesc = D3D12RHIResourcePreset::bufferDesc( sizeBytes );
        if ( isHrSuccess( _device->CreateCommittedResource( &heapProps, D3D12_HEAP_FLAG_NONE, &resourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                            IID_PPV_ARGS( outBuffer.GetAddressOf() ) ),
                          "CreateCommittedResource (mapped upload buffer)" ) == false )
            return false;

        if ( isHrSuccess( outBuffer->Map( 0, nullptr, &pOutMapped ), "ID3D12Resource::Map (mapped upload buffer)" ) == false || pOutMapped == nullptr )
        {
            pOutMapped = nullptr;
            outBuffer.Reset();
            return false;
        }
        return true;
    }

    RHITextureHandle D3D12RHIDevice::storeTexture( Microsoft::WRL::ComPtr<ID3D12Resource> texture, RHIMemoryKind kind )
    {
        if ( texture == nullptr )
            return 0;
        const uint64 bytes = computeAllocationBytes( texture.Get() );
    #if defined( SW_DEBUG )
        ID3D12Resource* pNamed = texture.Get();
    #endif
        const RHITextureHandle handle = _gpuTextures.insert( std::move( texture ) );
        getMemoryTracker().recordAllocation( RHIMemoryKey::makeTexture( handle ), kind, bytes );
    #if defined( SW_DEBUG )
        nameResourceForDiagnostics( pNamed, L"Texture", handle );
    #endif
        return handle;
    }

    #if defined( SW_DEBUG )
    void D3D12RHIDevice::nameResourceForDiagnostics( ID3D12Resource* pResource, const utf16* pKind, uint64 handle )
    {
        if ( pResource == nullptr || pKind == nullptr )
            return;
        // DRED 페이지 폴트 출력(flushDebugMessages)은 폴트 주소를 덮는 할당을 이 이름으로 보여 준다. 이름이 없으면 "unnamed" 뿐이다.
        // 핸들 번호를 넣어 RHI 로그(핸들로 적는다)와 맞춰 볼 수 있게 한다. 크기 · 포맷은 같은 종류의 여러 할당을 가린다.
        const D3D12_RESOURCE_DESC desc = pResource->GetDesc();
        utf16                     arrName[constant::buffer::kMax128]{};
        swprintf_s( arrName, L"SW %ls %llu (%llux%u fmt %u)", pKind, static_cast<uint64>( handle ), static_cast<uint64>( desc.Width ),
                    static_cast<uint32>( desc.Height ), static_cast<uint32>( desc.Format ) );
        pResource->SetName( arrName );
    }
    #endif

    void CALLBACK D3D12RHIDevice::onValidationMessage( D3D12_MESSAGE_CATEGORY category, D3D12_MESSAGE_SEVERITY severity, D3D12_MESSAGE_ID id,
                                                       LPCSTR pDescription, void* pContext )
    {
        (void)category;
        D3D12RHIDevice* pDevice = static_cast<D3D12RHIDevice*>( pContext );
        if ( pDevice == nullptr || severity > D3D12_MESSAGE_SEVERITY_WARNING )
            return;
        // 같은 결함은 드로우마다 되풀이된다. 앞의 몇 개만 적고 나머지는 센다(테스트는 수로 본다).
        constexpr uint32 kMaxLoggedMessages = 16;
        const uint32     count              = pDevice->_validationMessageCount.fetch_add( 1, std::memory_order_relaxed ) + 1;
        if ( count <= kMaxLoggedMessages )
            SW_LOG_WARNING_ID( "D3D12.D3D12DebugLayerSeverity", "D3D12 debug layer (severity %#, id %#): %#", static_cast<int32>( severity ), static_cast<int32>( id ),
                               pDescription != nullptr ? pDescription : "" );
    }

    void D3D12RHIDevice::flushDebugMessages( [[maybe_unused]] const utf8* pStage )
    {
        if ( build::kShipping )
            return; // 배포본 — 디버그 레이어 메시지도 DRED 도 켜지 않았다
        // 디바이스가 이미 제거된 상태로 한 번 로그를 남겼으면, 프레임마다 똑같은 검증 메시지
        // 수십 줄 + DRED 덤프를 무한 반복하지 않는다. 자동 복구가 없어서 그 이후 매 프레임
        // 여기로 다시 들어오는데, 정보량 없이 로그만 무한히 쌓인다.
        const bool bAlreadyDeviceRemoved = _bDeviceRemovedLogged.load( std::memory_order_relaxed ) != SW_FALSE;

    #if defined( SW_DEBUG )
        Microsoft::WRL::ComPtr<ID3D12InfoQueue> infoQueue;
        if ( SUCCEEDED( _device.As( &infoQueue ) ) && infoQueue != nullptr )
        {
            const uint64 messageCount = infoQueue->GetNumStoredMessages();
            for ( uint64 messageIndex = 0; messageIndex < messageCount; ++messageIndex )
            {
                SIZE_T messageLength{ 0 };
                infoQueue->GetMessage( messageIndex, nullptr, &messageLength );
                vector<uint8>  bytes( messageLength );
                D3D12_MESSAGE* pMessage = reinterpret_cast<D3D12_MESSAGE*>( bytes.data() );
                if ( FAILED( infoQueue->GetMessage( messageIndex, pMessage, &messageLength ) ) || bAlreadyDeviceRemoved )
                    continue;
                // 메시지의 심각도대로 남긴다 — 정보성 메시지(객체 생성 · 파괴 기록)까지 오류로 남기면 실패 하나에 수백 줄이 쌓여 원인 줄이 묻힌다.
                switch ( pMessage->Severity )
                {
                    case D3D12_MESSAGE_SEVERITY_CORRUPTION:
                    case D3D12_MESSAGE_SEVERITY_ERROR:
                        SW_LOG_ERROR_ID( "D3D12.DebugLayer", "[%#] %#", pStage, pMessage->pDescription );
                        break;
                    case D3D12_MESSAGE_SEVERITY_WARNING:
                        SW_LOG_WARNING_ID( "D3D12.DebugLayerWarning", "[%#] %#", pStage, pMessage->pDescription );
                        break;
                    case D3D12_MESSAGE_SEVERITY_INFO:
                    case D3D12_MESSAGE_SEVERITY_MESSAGE:
                        SW_LOG_TRACE_ID( "D3D12.DebugLayerInfo", "[%#] %#", pStage, pMessage->pDescription );
                        break;
                }
            }
            infoQueue->ClearStoredMessages();
        }
    #endif

        if ( bAlreadyDeviceRemoved )
            return;

        if ( isDeviceRemoved() )
        {
            _bDeviceRemovedLogged.store( SW_TRUE, std::memory_order_relaxed );
            // 마지막으로 GPU 가 닿은 패스 - 우리 표지(WriteBufferImmediate)를 이름으로 풀어 로그 · 크래시 문맥 · 빵부스러기에 싣는다.
            uint32       arrBreadcrumb[GPUBreadcrumbs::kMaxReported]{};
            const uint32 breadcrumbCount = readLastBreadcrumbs( arrBreadcrumb, GPUBreadcrumbs::kMaxReported );
            GPUBreadcrumbs::reportDeviceLost( "Direct3D 12", arrBreadcrumb, breadcrumbCount );
            Microsoft::WRL::ComPtr<ID3D12DeviceRemovedExtendedData> dred;
            if ( SUCCEEDED( _device.As( &dred ) ) && dred != nullptr )
            {
                D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT autoBreadcrumbsOutput{};
                if ( SUCCEEDED( dred->GetAutoBreadcrumbsOutput( &autoBreadcrumbsOutput ) ) )
                {
                    const D3D12_AUTO_BREADCRUMB_NODE* pNode = autoBreadcrumbsOutput.pHeadAutoBreadcrumbNode;
                    while ( pNode != nullptr )
                    {
                        const uint32 executed = ( pNode->pCommandHistory != nullptr && pNode->pLastBreadcrumbValue != nullptr )
                                                  ? *pNode->pLastBreadcrumbValue
                                                  : 0;
                        SW_LOG_ERROR_ID( "D3D12.CommandListTotalExecuted", "CommandList='%#', Total=%#, Executed=%#",
                                         pNode->pCommandListDebugNameA ? pNode->pCommandListDebugNameA : "unnamed",
                                         pNode->BreadcrumbCount, executed );
                        const bool bHasCompletedBreadcrumb = pNode->pCommandHistory != nullptr && 0 < executed && executed <= pNode->BreadcrumbCount;
                        if ( bHasCompletedBreadcrumb )
                        {
                            SW_LOG_ERROR_ID( "D3D12.LastCompletedOpIndex", "Last completed Op index=%#, OpType=%#",
                                             executed - 1, static_cast<uint32>( pNode->pCommandHistory[executed - 1] ) );
                            if ( executed < pNode->BreadcrumbCount )
                            {
                                SW_LOG_ERROR_ID( "D3D12.FailedFlightOpIndex", "Failed/In-Flight Op index=%#, OpType=%#",
                                                 executed, static_cast<uint32>( pNode->pCommandHistory[executed] ) );
                            }
                        }
                        pNode = pNode->pNext;
                    }
                }

                D3D12_DRED_PAGE_FAULT_OUTPUT pageFaultOutput{};
                if ( SUCCEEDED( dred->GetPageFaultAllocationOutput( &pageFaultOutput ) ) )
                {
                    SW_LOG_ERROR_ID( "D3D12.PageFaultVAX", "PageFault VA=0x%#", Fmt( static_cast<uint64>( pageFaultOutput.PageFaultVA ), Format( 16, Format::Padding::Zero ).hexUpper() ) );
                    // 폴트 주소를 덮는 할당을 이름으로 되짚는다 — 살아 있는 것과 최근에 해제된 것(해제 뒤 사용이면 여기 나온다).
                    // 이름은 storeBuffer · storeTexture 가 붙인다(nameResourceForDiagnostics, `SW Buffer <핸들>`).
                    for ( const D3D12_DRED_ALLOCATION_NODE* pAllocation = pageFaultOutput.pHeadExistingAllocationNode; pAllocation != nullptr;
                          pAllocation                                   = pAllocation->pNext )
                    {
                        SW_LOG_ERROR_ID( "D3D12.PageFaultVAInsideLive", "  PageFault VA is inside live allocation '%#' (type %#)",
                                         pAllocation->ObjectNameA != nullptr ? pAllocation->ObjectNameA : "unnamed", static_cast<uint32>( pAllocation->AllocationType ) );
                    }
                    for ( const D3D12_DRED_ALLOCATION_NODE* pAllocation = pageFaultOutput.pHeadRecentFreedAllocationNode; pAllocation != nullptr;
                          pAllocation                                   = pAllocation->pNext )
                    {
                        SW_LOG_ERROR_ID( "D3D12.PageFaultVAInsideRecently", "  PageFault VA is inside recently freed allocation '%#' (type %#) — use after free",
                                         pAllocation->ObjectNameA != nullptr ? pAllocation->ObjectNameA : "unnamed", static_cast<uint32>( pAllocation->AllocationType ) );
                    }
                }
            }
        }
    }

    bool D3D12RHIDevice::queryGPUMemoryBudgetInternal( RHIMemoryBudget& outBudget )
    {
        return queryDXGIMemoryBudget( _memoryAdapter.Get(), outBudget );
    }

    IRHIResourceFactory* D3D12RHIDevice::getResourceFactory()
    {
        return _resourceImpl.get();
    }
    IRHICommandContext* D3D12RHIDevice::getFrameStreamContext()
    {
        return _frameStreamContext.get();
    }

    // 길이 유지(긴 함수 정리 기준): 디바이스 · 큐 · 힙 · 전역 리소스 생성의 순서가 곧 의미라 나누지 않는다.
    bool D3D12RHIDevice::initializeInternal( const RHISwapChainDesc& desc )
    {
    #if defined( SW_DEBUG )
        {
            Microsoft::WRL::ComPtr<ID3D12Debug> debugController;
            if ( SUCCEEDED( D3D12GetDebugInterface( IID_PPV_ARGS( debugController.GetAddressOf() ) ) ) )
            {
                debugController->EnableDebugLayer();
                SW_LOG_INFO_ID( "D3D12.DebugLayerEnabled", "Debug layer enabled." );
            }
        }
    #endif
        // DRED(Device Removed Extended Data)의 자동 브레드크럼 · 페이지 폴트 - 디바이스를 만들기 **전에** 켜야 한다. 디버그 레이어가 없는 개발 빌드(Release)에서도 켠다
        // (`-gv_gpuBreadcrumbs` 가 기본 켬). 소실 뒤 마지막 패스 이름은 우리 표지(`createBreadcrumbBuffer`)가 싣고, DRED 는 명령 종류별 진행과 폴트 주소를 싣는다.
        if ( GPUBreadcrumbs::isEnabled() )
        {
            Microsoft::WRL::ComPtr<ID3D12DeviceRemovedExtendedDataSettings> dredSettings;
            if ( SUCCEEDED( D3D12GetDebugInterface( IID_PPV_ARGS( dredSettings.GetAddressOf() ) ) ) )
            {
                dredSettings->SetAutoBreadcrumbsEnablement( D3D12_DRED_ENABLEMENT_FORCED_ON );
                dredSettings->SetPageFaultEnablement( D3D12_DRED_ENABLEMENT_FORCED_ON );
                SW_LOG_INFO_ID( "D3D12.DREDDeviceRemovedExtended", "DRED (Device Removed Extended Data) diagnostic enabled." );
            }
        }

        Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
        if ( FAILED( CreateDXGIFactory1( IID_PPV_ARGS( factory.GetAddressOf() ) ) ) )
            return false;

        // 소프트웨어 어댑터(WARP) — CI 러너와 같은 래스터라이저(gv_rhiSoftwareAdapter).
        Microsoft::WRL::ComPtr<IDXGIAdapter1> softwareAdapter;
        if ( desc._bSoftwareAdapter && FAILED( factory->EnumWarpAdapter( IID_PPV_ARGS( softwareAdapter.GetAddressOf() ) ) ) )
        {
            SW_LOG_ERROR_ID( "D3D12.WARPAdapterNotAvailable", "WARP adapter is not available (gv_rhiSoftwareAdapter)" );
            return false;
        }

        // 하드웨어는 고성능 순서의 첫 어댑터다 — nullptr(기본 어댑터)은 하이브리드 노트북에서 내장 GPU 다(RHIDxgiAdapter.h).
        const Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter = softwareAdapter != nullptr ? softwareAdapter : findDXGIPreferredAdapter( factory.Get(), desc._bPreferMinimumPower );
        if ( adapter == nullptr )
        {
            SW_LOG_ERROR_ID( "D3D12.NoHardwareAdapter", "No hardware DXGI adapter (IDXGIFactory6 high-performance enumeration found none)" );
            return false;
        }
        // 크래시 리포트 · 로그에 실제로 쓰는 어댑터를 남긴다.
        DXGI_ADAPTER_DESC1 adapterDesc{};
        if ( SUCCEEDED( adapter->GetDesc1( &adapterDesc ) ) )
        {
            utf8         arrName[constant::buffer::kMax256]{};
            const string name = StringUtil::utf16ToUTF8( adapterDesc.Description );
            formatstring( arrName, constant::buffer::kMax256, "%# (vendor %#, device %#, VRAM %# MB)", name.c_str(), adapterDesc.VendorId, adapterDesc.DeviceId,
                          static_cast<uint32>( adapterDesc.DedicatedVideoMemory / constant::memory::kBytesPerMegabyte ) );
            engine::getSubsystem<CrashHandler>().setContextValue( "GPU", arrName );
            SW_LOG_INFO_ID( "D3D12.AdapterSelected", "Direct3D 12 adapter: %#", arrName );
        }
        markInitPhase( "adapter" );

        if ( FAILED( D3D12CreateDevice( adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS( _device.GetAddressOf() ) ) ) )
            return false;
        createBreadcrumbBuffer();
        // 메모리 질의는 디바이스가 실제로 쓰는 어댑터에 한다(기본 어댑터를 가정하지 않는다). 못 찾으면 드라이버 값은 "모름" 이다.
        _memoryAdapter = findDXGIAdapterByLuid( _device->GetAdapterLuid() );
        // 실제로 선 어댑터가 소프트웨어인지 — 요청이 아니라 결과를 적는다.
        if ( softwareAdapter != nullptr )
        {
            DXGI_ADAPTER_DESC1 softwareDesc{};
            const LUID         deviceLuid = _device->GetAdapterLuid();
            _bSoftwareAdapter             = SUCCEEDED( softwareAdapter->GetDesc1( &softwareDesc ) ) && softwareDesc.AdapterLuid.LowPart == deviceLuid.LowPart &&
                                softwareDesc.AdapterLuid.HighPart == deviceLuid.HighPart;
        }

        D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
        if ( SUCCEEDED( _device->CheckFeatureSupport( D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof( options ) ) ) )
        {
            if ( options.ResourceBindingTier >= D3D12_RESOURCE_BINDING_TIER_3 )
                SW_LOG_TRACE_ID( "D3D12.DeviceSupportsResourceBinding", "Device supports Resource Binding Tier 3 (Bindless)." );
            else
                SW_LOG_WARNING_ID( "D3D12.DeviceDoesNOTSupport", "Device does NOT support Resource Binding Tier 3. Fallback may be required." );
        }

    #if defined( SW_DEBUG )
        {
            Microsoft::WRL::ComPtr<ID3D12InfoQueue> infoQueue;
            if ( SUCCEEDED( _device.As( &infoQueue ) ) )
            {
                infoQueue->SetBreakOnSeverity( D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE );
                infoQueue->SetBreakOnSeverity( D3D12_MESSAGE_SEVERITY_ERROR, FALSE );
                // 생성 때 준 최적 클리어 값과 다른 색으로 지우면 "느릴 수 있다" 는 성능 안내뿐이다(지우기는 맞게 된다). 렌더 타깃을 여러 클리어 색으로 다시 쓰는
                // 그래프 · 에디터 뷰에서 늘 나므로 언리얼 D3D12RHI 처럼 거른다.
                D3D12_MESSAGE_ID        arrDeniedID[] = { D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE, D3D12_MESSAGE_ID_CLEARDEPTHSTENCILVIEW_MISMATCHINGCLEARVALUE };
                D3D12_INFO_QUEUE_FILTER filter{};
                filter.DenyList.NumIDs  = static_cast<UINT>( std::size( arrDeniedID ) );
                filter.DenyList.pIDList = arrDeniedID;
                if ( FAILED( infoQueue->PushStorageFilter( &filter ) ) )
                    SW_LOG_INFO_ID( "D3D12.D3D12DebugLayerStorage", "D3D12 debug layer storage filter was not applied" );
            }
            // 큐는 장치 실패 때만 비운다(flushDebugMessages). 경고가 나는 그 자리에서 보이게 콜백을 단다(Windows 11 의 디버그 레이어부터 있다).
            Microsoft::WRL::ComPtr<ID3D12InfoQueue1> infoQueue1;
            const bool                               bCallbackRegistered = SUCCEEDED( _device.As( &infoQueue1 ) ) &&
                                             SUCCEEDED( infoQueue1->RegisterMessageCallback( &D3D12RHIDevice::onValidationMessage, D3D12_MESSAGE_CALLBACK_FLAG_NONE,
                                                                                             this, &_validationCallbackCookie ) );
            if ( bCallbackRegistered == false )
            {
                _validationCallbackCookie = 0;
                SW_LOG_INFO_ID( "D3D12.D3D12DebugLayerNo", "D3D12 debug layer has no message callback (ID3D12InfoQueue1) - validation warnings show only when the device fails" );
            }
        }
    #endif

        markInitPhase( "device" );
        D3D12_COMMAND_QUEUE_DESC queueDesc{};
        queueDesc.Type  = D3D12_COMMAND_LIST_TYPE_DIRECT;
        queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        if ( isHrSuccess( _device->CreateCommandQueue( &queueDesc, IID_PPV_ARGS( _commandQueue.GetAddressOf() ) ), "CreateCommandQueue" ) == false )
            return false;
        // 비동기 컴퓨트 큐와 두 큐를 잇는 펜스 둘이다. 셋 중 하나라도 없으면 능력이 거짓이다 — 렌더 그래프가 AsyncCompute 패스를 그래픽스 큐에서 돌린다(장치를 실패로 보지 않는다).
        D3D12_COMMAND_QUEUE_DESC computeQueueDesc{};
        computeQueueDesc.Type         = D3D12_COMMAND_LIST_TYPE_COMPUTE;
        computeQueueDesc.Flags        = D3D12_COMMAND_QUEUE_FLAG_NONE;
        const bool bAsyncComputeReady = isHrSuccess( _device->CreateCommandQueue( &computeQueueDesc, IID_PPV_ARGS( _computeQueue.GetAddressOf() ) ), "CreateCommandQueue (compute)" ) &&
                                        isHrSuccess( _device->CreateFence( 0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS( _queueSyncFence.GetAddressOf() ) ), "CreateFence (queue sync)" ) &&
                                        isHrSuccess( _device->CreateFence( 0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS( _computeFence.GetAddressOf() ) ), "CreateFence (compute)" );
        if ( bAsyncComputeReady == false )
        {
            _computeQueue.Reset();
            _queueSyncFence.Reset();
            _computeFence.Reset();
            SW_LOG_WARNING_ID( "D3D12.AsyncComputeQueueUnavailable", "D3D12 async compute queue could not be created - AsyncCompute passes run on the graphics queue" );
        }
        _queueSyncValue     = 0;
        _computeFenceValue  = 0;
        _computeWaitedValue = 0;

        _swapChain.setBarrierWatcher( this );
        if ( _swapChain.initialize( factory.Get(), _commandQueue.Get(), desc ) == false )
            return false;
        markInitPhase( "swapchain" );

        _bBindlessRootSignature = SW_FALSE;
        _frameStreamState       = D3D12RecordingState{};

        // 백버퍼와 오프스크린 렌더 타깃이 **같은 RTV 힙**을 나눠 쓴다. 앞쪽 bufferCount 칸이 백버퍼,
        // 그 뒤가 오프스크린이다. 그래서 이 힙은 스왑체인이 아니라 디바이스가 소유한다.
        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
        rtvHeapDesc.NumDescriptors = _swapChain.getBufferCount() + kMaxOffscreenRTVs;
        rtvHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if ( isHrSuccess( _device->CreateDescriptorHeap( &rtvHeapDesc, IID_PPV_ARGS( _rtvHeap.GetAddressOf() ) ), "CreateDescriptorHeap" ) == false )
            return false;

        _rtvDescriptorSize = _device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_RTV );

        D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc{};
        dsvHeapDesc.NumDescriptors = kMaxOffscreenDSVs;
        dsvHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        dsvHeapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if ( isHrSuccess( _device->CreateDescriptorHeap( &dsvHeapDesc, IID_PPV_ARGS( _dsvHeap.GetAddressOf() ) ), "CreateDescriptorHeap" ) == false )
            return false;

        D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc{};
        cbvHeapDesc.NumDescriptors = kMaxShaderVisibleDescriptors;
        cbvHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        cbvHeapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if ( isHrSuccess( _device->CreateDescriptorHeap( &cbvHeapDesc, IID_PPV_ARGS( _cbvHeap.GetAddressOf() ) ), "CreateDescriptorHeap" ) == false )
            return false;

        _cbvDescriptorSize = _device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
        // GPU 메모리를 차지하는 것은 셰이더 가시 힙뿐이다(RTV · DSV · 오프라인 힙은 CPU 메모리). 크기는 칸 수 × 칸 크기다.
        getMemoryTracker().recordAllocation( RHIMemoryKey::makeDeviceObject( _cbvHeap.Get() ), RHIMemoryKind::Descriptor,
                                             static_cast<uint64>( cbvHeapDesc.NumDescriptors ) * _cbvDescriptorSize );

        // 오프라인(CPU 전용) 뷰 힙. 슬롯 테이블은 CopyDescriptors 로 굳히는데 셰이더 가시 힙은 복사 원본이 될 수 없다.
        // 등록은 뷰를 두 힙에 같은 인덱스로 만들고, 마지막 두 칸은 안 걸린 슬롯을 채우는 null 뷰다.
        D3D12_DESCRIPTOR_HEAP_DESC offlineHeapDesc{};
        offlineHeapDesc.NumDescriptors = kOfflineDescriptorCount;
        offlineHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        offlineHeapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if ( isHrSuccess( _device->CreateDescriptorHeap( &offlineHeapDesc, IID_PPV_ARGS( _offlineViewHeap.GetAddressOf() ) ), "CreateDescriptorHeap" ) == false )
            return false;
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC nullSRV{};
            nullSRV.ViewDimension           = D3D12_SRV_DIMENSION_BUFFER;
            nullSRV.Format                  = DXGI_FORMAT_R32_TYPELESS;
            nullSRV.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            nullSRV.Buffer.NumElements      = 1;
            nullSRV.Buffer.Flags            = D3D12_BUFFER_SRV_FLAG_RAW;
            _device->CreateShaderResourceView( nullptr, &nullSRV, offlineDescriptorAt( kOfflineNullSRVIndex ) );

            D3D12_UNORDERED_ACCESS_VIEW_DESC nullUAV{};
            nullUAV.ViewDimension      = D3D12_UAV_DIMENSION_BUFFER;
            nullUAV.Format             = DXGI_FORMAT_R32_TYPELESS;
            nullUAV.Buffer.NumElements = 1;
            nullUAV.Buffer.Flags       = D3D12_BUFFER_UAV_FLAG_RAW;
            _device->CreateUnorderedAccessView( nullptr, nullptr, &nullUAV, offlineDescriptorAt( kOfflineNullUAVIndex ) );

            // 레지스트리 0 번 = null Texture2D SRV(kNullTextureBindlessIndex). 등록은 그 다음 칸부터 받는다 — 0 으로 남은 텍스처 인덱스가
            // 상수버퍼 · 구조버퍼 뷰를 Texture2D 로 읽지 않고 0 을 읽는다.
            D3D12_SHADER_RESOURCE_VIEW_DESC nullTexture{};
            nullTexture.ViewDimension              = D3D12_SRV_DIMENSION_TEXTURE2D;
            nullTexture.Format                     = DXGI_FORMAT_R8G8B8A8_UNORM;
            nullTexture.Shader4ComponentMapping    = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            nullTexture.Texture2D.MipLevels        = 1;
            D3D12_CPU_DESCRIPTOR_HANDLE onlineNull = _cbvHeap->GetCPUDescriptorHandleForHeapStart();
            onlineNull.ptr += static_cast<SIZE_T>( kNullTextureBindlessIndex ) * _cbvDescriptorSize;
            _device->CreateShaderResourceView( nullptr, &nullTexture, onlineNull );
            _device->CreateShaderResourceView( nullptr, &nullTexture, offlineDescriptorAt( kNullTextureBindlessIndex ) );
            _allocatedDescriptorsCount = kNullTextureBindlessIndex + 1;
        }
        {
            ScopedLock<mutex> lock{ _onlineBlockMutex };
            _freeOnlineBlock.clear();
            for ( uint32 block = kOnlineBlockCount; block > 0; --block )
            {
                _freeOnlineBlock.push( block - 1 );
            }
            _bOnlineHeapExhaustedLogged = SW_FALSE;
        }

        for ( uint32 frameIndex = 0; frameIndex < constant::render::kMaxFrameCountInFlight; ++frameIndex )
        {
            if ( isHrSuccess( _device->CreateCommandAllocator( D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS( _arrCommandAllocator[frameIndex].GetAddressOf() ) ), "CreateCommandAllocator" ) == false )
                return false;
            // 디버그 레이어의 "allocator is being reset [in use]" 메시지는 객체 이름을 찍는다. 이름이 없으면
            // 어느 얼로케이터가 문제인지 주소만 남아 추적이 안 된다.
            utf16 arrName[constant::buffer::kMax64]{};
            swprintf_s( arrName, L"FrameStreamAllocator%u", frameIndex );
            _arrCommandAllocator[frameIndex]->SetName( arrName );
        }

        if ( isHrSuccess( _device->CreateCommandList( 0, D3D12_COMMAND_LIST_TYPE_DIRECT, _arrCommandAllocator[0].Get(), nullptr, IID_PPV_ARGS( _commandList.GetAddressOf() ) ), "CreateCommandList" ) == false )
            return false;

        // 갓 만든 리스트는 열린 채라 닫아 둔다(첫 프레임이 Reset 으로 연다). 빈 리스트 닫기가 실패하면 장치가 쓸 수 없는 상태다.
        if ( isHrSuccess( _commandList->Close(), "ID3D12GraphicsCommandList::Close (initial)" ) == false )
            return false;
        _frameStreamState._bRecording = SW_FALSE;
        _frameRing.reset();

        _swapChain.createBackBuffers( _device.Get(), _rtvHeap.Get(), _rtvDescriptorSize );

        if ( isHrSuccess( _device->CreateFence( 0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS( _fence.GetAddressOf() ) ), "CreateFence" ) == false )
            return false;
        _fenceValue = 1;
        // 전송 전용 펜스: 전송 작업(복사 리스트 하나)을 낼 때마다 한 값을 올린다. 프레임 펜스와 따로라 전송 완료를 프레임 없이도 알 수 있다(RHIAsyncUploads).
        if ( isHrSuccess( _device->CreateFence( 0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS( _transferFence.GetAddressOf() ) ), "CreateFence" ) == false )
            return false;
        _transferFenceValue = 0;
        if ( initializeTransfers( desc ) == false )
            return false;

        if ( createGlobalResources() == false )
            return false;

        _frameStreamContext = sw::make_unique<D3D12RHICommandContext>( this, _commandList.Get(), &_frameStreamState );

        return true;
    }

    void D3D12RHIDevice::detachCommandRecordingInternal()
    {
        _frameStreamContext.reset();
        RHILiveCommandListUtil::detachAll( _liveCmdListMutex, _listLiveCmd );
    }

    void D3D12RHIDevice::createBreadcrumbBuffer()
    {
        if ( GPUBreadcrumbs::isEnabled() == false || _device == nullptr )
            return;
        // 큐마다 읽기 힙(CPU 가 읽는 시스템 메모리) uint32 둘 — [0] 시작 표지 · [1] 끝 표지. GPU 가 소실 중에 쓴 값이 소실 뒤에도 CPU 에서 읽힌다. 읽기 힙 버퍼의 상태는 COPY_DEST 로 고정이다.
        const D3D12_HEAP_PROPERTIES heapProperties = D3D12RHIResourcePreset::heapProperties( D3D12_HEAP_TYPE_READBACK );
        const D3D12_RESOURCE_DESC   bufferDesc     = D3D12RHIResourcePreset::bufferDesc( sizeof( uint32 ) * kBreadcrumbSlotCount );
        for ( BreadcrumbTrack& track : _arrBreadcrumb )
        {
            if ( isHrSuccess( _device->CreateCommittedResource( &heapProperties, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                                IID_PPV_ARGS( track._buffer.GetAddressOf() ) ),
                              "CreateCommittedResource(breadcrumb)" ) == false )
            {
                SW_LOG_WARNING_ID( "D3D12.D3D12GPUBreadcrumbBuffer", "The D3D12 GPU breadcrumb buffer could not be created - device lost reports only the CPU-recorded last pass" );
                return;
            }
            void*       pMapped = nullptr;
            D3D12_RANGE noRead{ 0, 0 };
            if ( isHrSuccess( track._buffer->Map( 0, &noRead, &pMapped ), "Map(breadcrumb)" ) == false || pMapped == nullptr )
            {
                track._buffer.Reset();
                return;
            }
            static_cast<uint32*>( pMapped )[0] = 0;
            static_cast<uint32*>( pMapped )[1] = 0;
            track._pMapped                     = static_cast<const volatile uint32*>( pMapped );
            track._address                     = track._buffer->GetGPUVirtualAddress();
        }
        SW_LOG_INFO_ID( "D3D12.D3D12GPUBreadcrumbsEnabled", "D3D12 GPU breadcrumbs enabled (WriteBufferImmediate markers, one buffer per queue)" );
    }

    D3D12_GPU_VIRTUAL_ADDRESS D3D12RHIDevice::getBreadcrumbAddress( bool bComputeQueue ) const
    {
        return _arrBreadcrumb[bComputeQueue ? 1 : 0]._address;
    }

    uint32 D3D12RHIDevice::readLastBreadcrumbs( uint32* pOutID, uint32 capacity )
    {
        if ( pOutID == nullptr || capacity == 0 )
            return 0;
        // 큐마다 [0] 시작 표지 · [1] 끝 표지 — 그래픽스 다음 컴퓨트. 0 인 칸(아직 쓰지 않음)은 싣지 않는다. 푸는 일은 GPUBreadcrumbs::parse 가 한다.
        uint32 filled = 0;
        for ( const BreadcrumbTrack& track : _arrBreadcrumb )
        {
            if ( track._pMapped == nullptr )
                continue;
            for ( uint32 slot = 0; slot < kBreadcrumbSlotCount && filled < capacity; ++slot )
            {
                const uint32 marker = track._pMapped[slot];
                if ( marker != 0 )
                    pOutID[filled++] = marker;
            }
        }
        return filled;
    }

    void D3D12RHIDevice::shutdownInternal()
    {
        for ( BreadcrumbTrack& track : _arrBreadcrumb )
        {
            if ( track._buffer != nullptr && track._pMapped != nullptr )
                track._buffer->Unmap( 0, nullptr );
            track = BreadcrumbTrack{};
        }
        _mapOffscreenTexture.clear();
        _pipelineStates.clear();
        _renderPassRegistry.clear();
        _listRegisteredBindless.clear();
        _freeBindless.clear();
        _listRegisteredUAV.clear();
        _mapStructuredBufferState.clear();
        _gpuBuffers.clear();
        _gpuTextures.clear();
        _frameStreamState          = D3D12RecordingState{};
        _nextOffscreenRTVIndex     = 0;
        _allocatedDescriptorsCount = 0;

        _listFrameSwapChain.clear();
        for ( const unique_ptr<D3D12RHISwapChain>& pWindow : _listWindowSwapChain )
        {
            ScopedThreadRoleBorrow borrow{ pWindow->getRenderThreadRole() };
            pWindow->shutdown();
        }
        _listWindowSwapChain.clear();
        _swapChain.shutdown();
        _rootSignature.Reset();
        _drawCommandSignature.Reset();
        _drawIndexedCommandSignature.Reset();
        _dispatchCommandSignature.Reset();
        getMemoryTracker().recordFree( RHIMemoryKey::makeDeviceObject( _cbvHeap.Get() ) );
        _cbvHeap.Reset();
        _offlineViewHeap.Reset();
        {
            ScopedLock<mutex> lock{ _onlineBlockMutex };
            _freeOnlineBlock.clear();
        }
        _dsvHeap.Reset();
        _rtvHeap.Reset();
        _nextOffscreenDSVIndex = 0;
        _commandList.Reset();
        for ( Microsoft::WRL::ComPtr<ID3D12CommandAllocator>& allocator : _arrCommandAllocator )
        {
            allocator.Reset();
        }
        {
            ScopedLock<mutex> lock{ _cmdListPoolMutex };
            _listFreeCmdListEntry.clear();
            _listFreeComputeCmdListEntry.clear();
        }
        _frameStreamState._bRecording = SW_FALSE;
        _bBindlessRootSignature       = SW_FALSE;
        // 전송 작업 · 스테이징 링 · 전송을 기다리던 해제는 큐와 펜스를 놓기 전에 정리한다(GPU 는 종료 2단계에서 쉬었다).
        shutdownTransfers();
        _fence.Reset();
        _transferFence.Reset();
        _queueSyncFence.Reset();
        _computeFence.Reset();
        _computeQueue.Reset();
        _commandQueue.Reset();
        _memoryAdapter.Reset();
        Microsoft::WRL::ComPtr<ID3D12InfoQueue1> infoQueue1;
        const bool                               bHasValidationCallback = _validationCallbackCookie != 0 && _device != nullptr && SUCCEEDED( _device.As( &infoQueue1 ) );
        if ( bHasValidationCallback )
            infoQueue1->UnregisterMessageCallback( _validationCallbackCookie );
        _validationCallbackCookie = 0;
        infoQueue1.Reset();
        _device.Reset();

        _fenceValue        = 0;
        _rtvDescriptorSize = 0;
        _cbvDescriptorSize = 0;
    }

    void D3D12RHIDevice::resizeInternal( uint32 width, uint32 height )
    {
        if ( _swapChain.isValid() == false || ( width == 0 && height == 0 ) )
            return;
        resizeSwapChainBuffers( _swapChain, width, height );
    }

    IRHISwapChain* D3D12RHIDevice::createSwapChainInternal( const RHISwapChainDesc& desc )
    {
        // 메인 창과 같은 버퍼 수 · 포맷이어야 같은 PSO · 프레임 링 규칙을 쓴다. 팩터리는 메인 창과 같은 호출로 만든다(스왑체인은 큐에 붙어 어댑터가 정해진다).
        RHISwapChainDesc windowDesc = desc;
        windowDesc._bufferCount     = _swapChain.getBufferCount();
        windowDesc._format          = _swapChain.getFormat();
        Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
        if ( isHrSuccess( CreateDXGIFactory1( IID_PPV_ARGS( factory.GetAddressOf() ) ), "CreateDXGIFactory1 (window)" ) == false )
            return nullptr;

        unique_ptr<D3D12RHISwapChain> pSwapChain = make_unique<D3D12RHISwapChain>();
        pSwapChain->bindRenderThread( _renderThreadID.load( std::memory_order_acquire ) );
        ScopedThreadRoleBorrow borrow{ pSwapChain->getRenderThreadRole() };
        pSwapChain->setBarrierWatcher( this );
        const bool bCreated = pSwapChain->initialize( factory.Get(), _commandQueue.Get(), windowDesc ) && pSwapChain->isValid() && pSwapChain->createOwnedRTVHeap( _device.Get() );
        if ( bCreated == false )
            return nullptr;
        pSwapChain->createBackBuffers( _device.Get(), pSwapChain->getOwnedRTVHeap(), _rtvDescriptorSize );
        if ( pSwapChain->isBackBufferReady() == false )
            return nullptr;
        _listWindowSwapChain.push_back( std::move( pSwapChain ) );
        return _listWindowSwapChain.back().get();
    }

    void D3D12RHIDevice::resizeSwapChainInternal( IRHISwapChain& swapChain, uint32 width, uint32 height )
    {
        resizeSwapChainBuffers( static_cast<D3D12RHISwapChain&>( swapChain ), width, height ); // 이 디바이스가 만든 것만 들어온다(`createSwapChain`)
    }

    void D3D12RHIDevice::destroySwapChainInternal( IRHISwapChain& swapChain )
    {
        D3D12RHISwapChain& window = static_cast<D3D12RHISwapChain&>( swapChain );
        {
            ScopedThreadRoleBorrow borrow{ window.getRenderThreadRole() };
            window.shutdown();
        }
        (void)VectorUtil::removeIf( _listWindowSwapChain, [&window]( const unique_ptr<D3D12RHISwapChain>& pEntry )
        { return pEntry.get() == &window; } );
    }

    D3D12RHISwapChain& D3D12RHIDevice::useSwapChain( IRHISwapChain* pSwapChain )
    {
        if ( pSwapChain == nullptr || pSwapChain == &_swapChain )
            return _swapChain;
        D3D12RHISwapChain& swapChain = static_cast<D3D12RHISwapChain&>( *pSwapChain ); // 이 디바이스가 만든 것만 들어온다(`createSwapChain`)
        if ( std::find( _listFrameSwapChain.begin(), _listFrameSwapChain.end(), &swapChain ) == _listFrameSwapChain.end() )
        {
            swapChain.acquireNextImage();
            _listFrameSwapChain.push_back( &swapChain );
        }
        return swapChain;
    }

    void D3D12RHIDevice::resizeSwapChainBuffers( D3D12RHISwapChain& swapChain, uint32 width, uint32 height )
    {
        // resize 는 렌더 스레드를 세운 뒤(`Engine::onMainWindowResize` · `IRHIDevice::resizeSwapChain` 의 `waitIdle`) 부른다 — 그동안 프레임 진행 상태(백 버퍼 · 프레임 펜스)는
        // 이 스레드가 렌더 스레드 역할을 빌려 만진다. 프레임 펜스 대기(`waitForPreviousFrame`)는 메인 창의 백 버퍼를 다시 고르므로 다른 창을 바꿀 때도 메인 창의 역할을 빌린다.
        ScopedThreadRoleBorrow mainBorrow{ _swapChain.getRenderThreadRole() };
        ScopedThreadRoleBorrow borrow{ swapChain.getRenderThreadRole() };

        if ( _frameStreamState._bRecording != SW_FALSE && _commandList != nullptr )
        {
            {
                ScopedLock<mutex> uploadLock{ _uploadSlotMutex };
                flushPendingUploads( true );
            }
            _frameStreamContext->endRenderPass();
            requireHr( _commandList->Close(), "ID3D12GraphicsCommandList::Close (resize)" );
            ID3D12CommandList* arrCommandList[] = { _commandList.Get() };
            if ( _commandQueue != nullptr )
                _commandQueue->ExecuteCommandLists( 1, arrCommandList );
            _frameStreamState._bRecording = SW_FALSE;
            releaseOnlineBlocksDeferred( _frameStreamState );
        }

        waitForPreviousFrame();
        swapChain.releaseBackBuffers();
        if ( swapChain.resize( width, height ) == false )
            return;
        ID3D12DescriptorHeap* const pRTVHeap = &swapChain == &_swapChain ? _rtvHeap.Get() : swapChain.getOwnedRTVHeap();
        swapChain.createBackBuffers( _device.Get(), pRTVHeap, _rtvDescriptorSize );
        swapChain.acquireNextImage();
    }

    void D3D12RHIDevice::onRenderThreadRoleChanged( std::thread::id owner )
    {
        _swapChain.bindRenderThread( owner );
        for ( const unique_ptr<D3D12RHISwapChain>& pWindow : _listWindowSwapChain )
        {
            pWindow->bindRenderThread( owner );
        }
    }

    void D3D12RHIDevice::waitIdleInternal()
    {
        // 다른 스레드가 렌더 스레드를 `waitIdle` 로 세운 채 부른다(`Engine::onMainWindowResize`) — 그동안 프레임 진행 상태는 이 스레드가 빌려 만진다.
        ScopedThreadRoleBorrow borrow{ _swapChain.getRenderThreadRole() };
        waitForPreviousFrame();
        // 프레임 안에서 낸 비동기 컴퓨트 일은 프레임 펜스가 덮지만, 프레임 도중에 세웠으면 아직 돌 수 있다 — 이벤트 없이(null) 끝날 때까지 기다린다.
        if ( _computeFence != nullptr && _computeFence->GetCompletedValue() < _computeFenceValue )
            requireHr( _computeFence->SetEventOnCompletion( _computeFenceValue, nullptr ), "ID3D12Fence::SetEventOnCompletion (compute idle)" );
        // 전송은 같은 큐에서 프레임 펜스보다 먼저 끝났다 — 워터마크를 올려야 전송을 기다리던 텍스처 해제가 `_releaseQueue` 로 넘어와 아래 비우기에 든다.
        retireCompletedTransfers();
        _releaseQueue.flushAll();
    }

    void D3D12RHIDevice::enqueueGPURelease( const RHIResourceReleaseDelegate& releaseDelegate )
    {
        _releaseQueue.enqueueGPURelease( releaseDelegate, _fenceValue );
    }

    unique_ptr<IRHICommandList> D3D12RHIDevice::createCommandList()
    {
        unique_ptr<D3D12RHICommandList> list = make_unique<D3D12RHICommandList>( this );
        if ( list->isValid() == false )
        {
            if ( _bDeviceRemovedLogged.load( std::memory_order_relaxed ) == SW_FALSE )
                SW_LOG_ERROR_ONCE_ID( "D3D12.D3D12RHIDeviceCreateCommandListFailedCreate", "D3D12RHIDevice::createCommandList: failed to create the native command list." );
            return nullptr;
        }
        return list;
    }

    void D3D12RHIDevice::registerCommandList( D3D12RHICommandList* pCmdList )
    {
        ScopedLock<mutex> lock{ _liveCmdListMutex };
        _listLiveCmd.push_back( pCmdList );
    }

    void D3D12RHIDevice::unregisterCommandList( D3D12RHICommandList* pCmdList )
    {
        ScopedLock<mutex> lock{ _liveCmdListMutex };
        (void)VectorUtil::removeSingleSwap( _listLiveCmd, pCmdList ); // 등록되지 않은 리스트면 뺄 것이 없다
    }

    ID3D12GraphicsCommandList* D3D12RHIDevice::beginNextFrameSegment()
    {
        D3D12CommandListEntry entry = acquireCommandListEntry();
        if ( entry._list == nullptr || entry._allocator == nullptr )
            return nullptr;

        // 풀에서 나온 쌍은 이미 펜스를 통과했으므로 곧바로 Reset 해도 된다.
        if ( isHrSuccess( entry._allocator->Reset(), "Reset" ) == false || isHrSuccess( entry._list->Reset( entry._allocator.Get(), nullptr ), "Reset" ) == false )
            return nullptr;

        bindBindlessRootState( entry._list.Get() );

        ID3D12GraphicsCommandList* pList = entry._list.Get();
        _listFrameSegment.push_back( std::move( entry ) );
        return pList;
    }

    void D3D12RHIDevice::executeCommandListImmediate( IRHICommandList* pCmdList )
    {
        auto* pNative = static_cast<D3D12RHICommandList*>( pCmdList );
        if ( pNative == nullptr || _commandQueue == nullptr )
            return;

        ID3D12GraphicsCommandList* pList = pNative->getNativeCommandList();
        if ( pList == nullptr )
            return;

        // 이 리스트가 방금 올린 버퍼를 읽을 수 있다. 열어 둔 업로드 복사를 먼저 내보낸다.
        {
            ScopedLock<mutex> uploadLock{ _uploadSlotMutex };
            flushPendingUploads( true );
        }
        ID3D12CommandList* arr[] = { pList };
        _commandQueue->ExecuteCommandLists( 1, arr );
    }

    void D3D12RHIDevice::executeCommandList( IRHICommandList* pCmdList )
    {
        auto*      pNative                  = static_cast<D3D12RHICommandList*>( pCmdList );
        const bool bCannotSubmitCommandList = pNative == nullptr || _commandQueue == nullptr || _pActiveFrameList == nullptr;
        if ( bCannotSubmitCommandList )
            return;

        ID3D12GraphicsCommandList* pList = pNative->getNativeCommandList();
        if ( pList == nullptr )
            return;

        // 프레임 스트림을 이 지점에서 자르고 [지금까지의 조각][이 리스트] 순서로 모아 endFrame 에서 한 번에
        // 제출한다(Vulkan 과 같다). 같은 큐의 제출 순서가 곧 실행 순서다. 주의: 여기서 곧바로 ExecuteCommandLists 를
        // 부르면 프레임 스트림은 endFrame 에 제출되므로, 그래프보다 **먼저** 기록한 것(오프스크린 게임 RT 클리어 등)이
        // 그래프 뒤에 실행된다.
        //
        // **빈 조각은 자르지 않는다.** 이 조각에 아무것도 기록되지 않았으면(레벨 배리어는 패스 리스트에 들어가므로
        // 레벨 사이가 그렇다) 패스 리스트만 넣고 조각은 열어 둔 채 다음 기록을 받는다. 그 조각은 뒤에 기록될
        // 것만 담으므로 뒤에 제출돼도 순서가 맞다. 잘라 내보내면 큐에 빈 리스트가 나가고 제출이 리스트당 ~7 us 다.
        const bool bSegmentEmpty = ( _frameStreamState._bRecordedAny == SW_FALSE );
        if ( bSegmentEmpty )
        {
            _listPendingSubmit.push_back( pList );
            submitPendingIfImmediate();
            return;
        }

        // Close 실패는 기록 중 잘못된 명령이 있었다는 뜻이다 — 제출하면 장치가 제거되므로 이유와 함께 멈춘다(아래 Close 도 같다). 네이티브 패스는 리스트를 넘지 못해 먼저 닫는다.
        _frameStreamContext->endRenderPass();
        requireHr( _pActiveFrameList->Close(), "ID3D12GraphicsCommandList::Close (frame segment)" );
        releaseOnlineBlocksDeferred( _frameStreamState );
        _listPendingSubmit.push_back( _pActiveFrameList );
        _listPendingSubmit.push_back( pList );
        submitPendingIfImmediate();
        openNextFrameSegment();
    }

    void D3D12RHIDevice::openNextFrameSegment()
    {
        ID3D12GraphicsCommandList* pNextSegment = beginNextFrameSegment();
        _pActiveFrameList                       = pNextSegment;
        if ( pNextSegment == nullptr )
        {
            _frameStreamState._bRecording = SW_FALSE;
            return;
        }

        // 새 리스트라 바인딩 캐시가 무효다. 뷰포트/시저도 다시 깔아준다.
        _frameStreamState.resetForRecording();
        _frameStreamState._bRecording = SW_TRUE;
        _frameStreamContext->rebindCommandList( pNextSegment );

        D3D12_VIEWPORT viewport{};
        viewport.Width    = static_cast<float32>( _swapChain.getWidth() );
        viewport.Height   = static_cast<float32>( _swapChain.getHeight() );
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;
        pNextSegment->RSSetViewports( 1, &viewport );

        D3D12_RECT scissor{ 0, 0, static_cast<LONG>( _swapChain.getWidth() ), static_cast<LONG>( _swapChain.getHeight() ) };
        pNextSegment->RSSetScissorRects( 1, &scissor );
    }

    void D3D12RHIDevice::submitPendingIfImmediate()
    {
        if ( _bImmediateSubmit == false || _listPendingSubmit.empty() )
            return;

        // 여기까지 기록된 구조 버퍼 갱신 복사가 이 리스트들보다 먼저 가야 한다. 앞에 끼운다. **실행까지 업로드 락을 쥔다** — 닫아서 대기열에 넣은 복사
        // 리스트를 실행 전에 다른 스레드의 갱신이 `Reset` 하면 그 복사가 사라진다.
        ScopedLock<mutex> uploadLock{ _uploadSlotMutex };
        flushPendingUploads( false );
        _commandQueue->ExecuteCommandLists( static_cast<UINT>( _listPendingSubmit.size() ), _listPendingSubmit.data() );
        _listPendingSubmit.clear();
    }

    void D3D12RHIDevice::submitFrameWorkNow()
    {
        if ( _commandQueue == nullptr || _pActiveFrameList == nullptr )
            return;
        // 조각에 기록한 것이 있으면 닫아 대기열 끝에 넣고 새 조각을 연다(빈 조각은 그대로 둔다 — `executeCommandList` 와 같은 규칙).
        const bool bSegmentHasWork = _frameStreamState._bRecordedAny != SW_FALSE;
        if ( bSegmentHasWork )
        {
            _frameStreamContext->endRenderPass();
            requireHr( _pActiveFrameList->Close(), "ID3D12GraphicsCommandList::Close (frame segment, queue sync)" );
            releaseOnlineBlocksDeferred( _frameStreamState );
            _listPendingSubmit.push_back( _pActiveFrameList );
        }
        // 구조 버퍼 복사가 앞에 끼고, 실행까지 업로드 락을 쥔다(submitPendingIfImmediate 와 같은 이유).
        {
            ScopedLock<mutex> uploadLock{ _uploadSlotMutex };
            flushPendingUploads( false );
            if ( _listPendingSubmit.empty() == false )
                _commandQueue->ExecuteCommandLists( static_cast<UINT>( _listPendingSubmit.size() ), _listPendingSubmit.data() );
            _listPendingSubmit.clear();
        }
        if ( bSegmentHasWork )
            openNextFrameSegment();
    }

    unique_ptr<IRHICommandList> D3D12RHIDevice::createAsyncComputeCommandList()
    {
        if ( _computeQueue == nullptr )
            return IRHIDevice::createAsyncComputeCommandList(); // 능력이 거짓인 길 — 오류를 남기고 nullptr
        unique_ptr<D3D12RHICommandList> list = make_unique<D3D12RHICommandList>( this, D3D12_COMMAND_LIST_TYPE_COMPUTE );
        if ( list->isValid() == false )
        {
            SW_LOG_ERROR_ONCE_ID( "D3D12.CreateAsyncComputeListFailed", "D3D12RHIDevice::createAsyncComputeCommandList: failed to create the native compute command list." );
            return nullptr;
        }
        return list;
    }

    RHIQueueSyncPoint D3D12RHIDevice::submitAsyncCompute( IRHICommandList* pCmdList )
    {
        auto* pNative = static_cast<D3D12RHICommandList*>( pCmdList );
        if ( _computeQueue == nullptr )
            return IRHIDevice::submitAsyncCompute( pCmdList );
        if ( pNative == nullptr || pNative->getNativeCommandList() == nullptr )
            return RHIQueueSyncPoint{};
        // 포크: 지금까지 넘긴 그래픽스 일(프레임 스트림 · 리스트 · 구조 버퍼 복사)을 큐에 내고 그 끝을 신호한다. 컴퓨트 큐는 그 신호를 기다린 뒤 이 리스트를 실행한다.
        // 버퍼는 ExecuteCommandLists 경계에서 COMMON 으로 돌아가므로(상태 감쇠) 큐 사이 소유권 이전이 따로 없다 — 텍스처는 생산자가 컴퓨트에서 쓸 상태로 넘겨 둔다.
        submitFrameWorkNow();
        ++_queueSyncValue;
        requireHr( _commandQueue->Signal( _queueSyncFence.Get(), _queueSyncValue ), "ID3D12CommandQueue::Signal (fork)" );
        requireHr( _computeQueue->Wait( _queueSyncFence.Get(), _queueSyncValue ), "ID3D12CommandQueue::Wait (fork)" );
        ID3D12CommandList* arrList[] = { pNative->getNativeCommandList() };
        _computeQueue->ExecuteCommandLists( 1, arrList );
        ++_computeFenceValue;
        requireHr( _computeQueue->Signal( _computeFence.Get(), _computeFenceValue ), "ID3D12CommandQueue::Signal (compute)" );
        return RHIQueueSyncPoint{ _computeFenceValue };
    }

    void D3D12RHIDevice::waitForAsyncCompute( RHIQueueSyncPoint point )
    {
        if ( _computeQueue == nullptr )
        {
            IRHIDevice::waitForAsyncCompute( point );
            return;
        }
        if ( point.isValid() == false || point._value <= _computeWaitedValue )
            return;
        // 조인: 그 전 그래픽스 일은 기다리지 않게 먼저 큐에 낸다 — 포크와 조인 사이의 그래픽스 일이 컴퓨트와 겹친다.
        submitFrameWorkNow();
        requireHr( _commandQueue->Wait( _computeFence.Get(), point._value ), "ID3D12CommandQueue::Wait (join)" );
        _computeWaitedValue = point._value;
    }

    void D3D12RHIDevice::flushPendingUploads( bool bExecuteNow )
    {
        if ( _commandQueue == nullptr )
            return;
        SW_PROFILE_SCOPE( "RT.Upload.flush" ); // Vulkan 의 `RT.Upload.waitIdle` 과 견주는 자리(여기는 기다리지 않고 제출만 한다)
        for ( StructuredUploadSlot& slot : _arrStructuredUploadSlot )
        {
            if ( slot._bListOpen == SW_FALSE || slot._copyCommandList == nullptr )
                continue;
            requireHr( slot._copyCommandList->Close(), "ID3D12GraphicsCommandList::Close (structured upload)" );
            slot._bListOpen = SW_FALSE;
            // 이 얼로케이터의 기록이 모두 끝났다고 믿어도 되는 펜스는 **이 제출 뒤 첫 Signal** 이다. 주의: 리스트를 **열 때** 적으면, 프레임 제출 뒤 ·
            // Signal 전에 연 복사(다음 프레임에 제출된다)가 한 프레임 이른 펜스를 달고, 세 프레임 뒤 그 펜스만 기다리고 얼로케이터를 Reset 한다 —
            // `_resetFence` 가 막으려는 "실행 중 Reset" 그대로다. 부르는 쪽은 업로드 락을 쥐고 있고, 실행은 다음 Signal 보다 먼저다.
            slot._resetFence = _fenceValue.load();
            if ( bExecuteNow )
            {
                ID3D12CommandList* arrList[] = { slot._copyCommandList.Get() };
                _commandQueue->ExecuteCommandLists( 1, arrList );
            }
            else
            {
                // 프레임 리스트 앞에 넣는다. 큐 순서가 실행 순서다.
                _listPendingSubmit.insert( _listPendingSubmit.begin(), slot._copyCommandList.Get() );
            }
        }
    }

    bool D3D12RHIDevice::debugRemoveDevice()
    {
        Microsoft::WRL::ComPtr<ID3D12Device5> device5;
        if ( _device == nullptr || FAILED( _device.As( &device5 ) ) )
            return false;
        SW_LOG_WARNING_ID( "D3D12.RemovingD3D12DeviceRequest", "Removing the D3D12 device on request (rhi.removeDevice) - every later GPU call fails as after a TDR" );
        device5->RemoveDevice();
        _transferSerials.markLost(); // 제거 직후부터 제출 진입이 Rejected 를 준다(다음 질의를 기다리지 않는다)
        return true;
    }

    bool D3D12RHIDevice::isDeviceRemoved()
    {
        if ( _device == nullptr || SUCCEEDED( _device->GetDeviceRemovedReason() ) )
            return false;
        _transferSerials.markLost();
        return true;
    }

    bool D3D12RHIDevice::isDeviceLost()
    {
        return _transferSerials.isLost() || isDeviceRemoved();
    }

    void D3D12RHIDevice::waitForPreviousFrame()
    {
        if ( _commandQueue == nullptr || _fence == nullptr )
            return;

        if ( isDeviceRemoved() )
            return;

        // 업로드 내보내기와 Signal · 올리기를 한 락 안에서 한다(`_fenceValue` 의 설명). 기다리는 것은 락 밖에서다.
        UINT64  fenceToWait{ 0 };
        HRESULT signalHr{ S_OK };
        {
            ScopedLock<mutex> uploadLock{ _uploadSlotMutex };
            flushPendingUploads( true );
            fenceToWait = _fenceValue.load();
            signalHr    = _commandQueue->Signal( _fence.Get(), fenceToWait );
            _fenceValue.store( fenceToWait + 1 );
        }

        if ( FAILED( signalHr ) )
        {
            SW_LOG_ERROR_ID( "D3D12.FenceSignalFailedHr", "Fence Signal failed hr=0x%# (DeviceRemoved=0x%#)",
                             static_cast<uint32>( signalHr ),
                             static_cast<uint32>( _device ? _device->GetDeviceRemovedReason() : S_OK ) );
            return;
        }

        if ( waitForFenceValue( fenceToWait ) == false )
            return;

        if ( isDeviceRemoved() == false )
            _swapChain.acquireNextImage();
    }

    void D3D12RHIDevice::waitForRingSlot()
    {
        if ( _fence == nullptr )
            return;

        if ( isDeviceRemoved() )
            return;

        const uint64 completed = _fence->GetCompletedValue();
        bool         bAdvanced = _frameRing.beginFrame( completed );
        if ( bAdvanced == false )
        {
            const uint32 nextIndex = ( _frameRing.currentIndex() + 1 ) % constant::render::kMaxFrameCountInFlight;
            const uint64 waitValue = _frameRing.getFenceValue( nextIndex );
            // 2 초가 지나도 **끝날 때까지** 기다린다(`waitForFenceValue`). 주의: 시간 초과를 무시하고 링을 넘기지 못한 채 이어가면, 방금 제출한
            // 프레임의 얼로케이터를 Reset 하고 GPU 가 읽는 상수버퍼 링 칸을 덮어쓴다. 장치가 제거됐으면(Windows 는 멈춘 GPU 를 TDR 로 제거한다)
            // 기다리지 않는다.
            waitForFenceValue( waitValue );
            bAdvanced = _frameRing.beginFrame( _fence->GetCompletedValue() );
        }
        // 새 칸의 GPU 사용이 끝났다. 값이 바뀔 때만 쓰는 상수버퍼 가운데 이 칸이 옛 값인 것을 채운다. 넘어가지 못했으면(장치 제거) 지금 칸은
        // GPU 가 아직 읽는 중일 수 있으니 건드리지 않는다.
        if ( bAdvanced )
            fillConstantBufferSlot();
    }

    bool D3D12RHIDevice::waitForFenceValue( uint64 fenceValue )
    {
        if ( _fence == nullptr )
            return false;
        if ( _fence->GetCompletedValue() >= fenceValue )
            return true;
        // 이벤트는 기다리는 호출마다 만든다. 렌더 스레드(링 슬롯 · 이전 프레임)와 다른 스레드(업로드 슬롯의 얼로케이터)가 동시에 기다리는데, 장치의
        // 자동 리셋 이벤트 하나를 나눠 쓰면 먼저 끝난 쪽의 신호를 다른 쪽이 가져가 아직 오지 않은 펜스를 지났다고 믿고 쓰는 중인 얼로케이터를 Reset 한다.
        const HANDLE fenceEvent = CreateEventW( nullptr, FALSE, FALSE, nullptr );
        if ( fenceEvent == nullptr )
        {
            SW_LOG_ERROR_ID( "D3D12.FenceWaitFailedCreateEventW", "Fence %# wait failed: CreateEventW returned null (GetLastError=%#)", fenceValue, static_cast<uint32>( GetLastError() ) );
            return false;
        }
        // 끝날 때까지 기다린다. 시간 초과로 실패를 돌려주면 부르는 쪽(링 슬롯 · 리드백 · 크기 변경 · 해제 큐 비우기)이 GPU 가 아직 쓰는 자원을
        // 풀거나 재사용한다. 2 초마다 한 번 알리고, 장치가 제거되면(TDR) 멈춘다 — 그때는 기다려도 끝나지 않는다.
        bool   bReached = SUCCEEDED( _fence->SetEventOnCompletion( fenceValue, fenceEvent ) );
        uint32 waitedSeconds{ 0 };
        while ( bReached && WaitForSingleObject( fenceEvent, 2000 ) != WAIT_OBJECT_0 )
        {
            waitedSeconds += 2;
            if ( isDeviceRemoved() )
            {
                SW_LOG_ERROR_ONCE_ID( "D3D12.FenceNeverCompletedDevice", "Fence %# never completed — the device was removed (hr=0x%#)", fenceValue,
                                      static_cast<uint32>( _device->GetDeviceRemovedReason() ) );
                bReached = false;
                break;
            }
            if ( _fence->GetCompletedValue() >= fenceValue )
                break;
            SW_LOG_WARNING_ID( "D3D12.GPUNotReachedFence", "GPU has not reached fence %# after %# s — still waiting", fenceValue, waitedSeconds );
        }
        CloseHandle( fenceEvent );
        return bReached;
    }

    void D3D12RHIDevice::signalCurrentFrame()
    {
        if ( _commandQueue == nullptr || _fence == nullptr )
            return;

        if ( isDeviceRemoved() )
            return;

        // Signal 과 올리기를 업로드 락 안에서 한 덩어리로 한다. 다른 스레드의 업로드가 그 사이에 `_fenceValue` 를 읽고 복사를 실행하면
        // 복사는 Signal 뒤에 나가는데 앞 값을 받아, 그 펜스를 기다린 얼로케이터 Reset 이 아직 도는 복사를 지운다(`_fenceValue` 의 설명).
        UINT64  fenceToSignal{ 0 };
        HRESULT signalHr{ S_OK };
        {
            ScopedLock<mutex> uploadLock{ _uploadSlotMutex };
            fenceToSignal = _fenceValue.load();
            signalHr      = _commandQueue->Signal( _fence.Get(), fenceToSignal );
            _fenceValue.store( fenceToSignal + 1 );
        }
        if ( FAILED( signalHr ) )
        {
            SW_LOG_ERROR_ONCE_ID( "D3D12.FenceSignalFailedHr2", "Fence Signal failed hr=0x%#", static_cast<uint32>( signalHr ) );
            return;
        }
        _frameRing.setFenceValue( _frameRing.currentIndex(), fenceToSignal );
        const uint64 completedFence = _fence->GetCompletedValue();
        _releaseQueue.tickCompleted( completedFence );
        recycleCompletedOnlineBlocks( completedFence );
        // 프레임을 돌리는 동안 폴링하는 쪽이 없어도 끝난 전송의 작업 · 스테이징 구간 · 전송을 기다리던 해제가 돌아오게 한다(기다리지 않는다).
        retireCompletedTransfers();
    }

    ID3D12CommandAllocator* D3D12RHIDevice::currentAllocator()
    {
        return _arrCommandAllocator[_frameRing.currentIndex()].Get();
    }

    D3D12CommandListEntry D3D12RHIDevice::acquireCommandListEntry( D3D12_COMMAND_LIST_TYPE type )
    {
        // 반환 대상은 이 하나다. 이름 있는 반환 객체가 여럿이면 NRVO 가 걸리지 않아
        // 반환할 때마다 엔트리(ComPtr 두 개)가 복사된다.
        D3D12CommandListEntry entry;
        entry._type = type;
        {
            ScopedLock<mutex>              lock{ _cmdListPoolMutex };
            vector<D3D12CommandListEntry>& listFree = ( type == D3D12_COMMAND_LIST_TYPE_COMPUTE ) ? _listFreeComputeCmdListEntry : _listFreeCmdListEntry;
            if ( listFree.empty() == false )
            {
                entry = std::move( listFree.back() );
                listFree.pop_back();
                return entry;
            }
        }

        // 풀이 비었으면 새로 만든다. 생성 직후의 리스트는 열린 상태라 바로 Close 해 둔다
        // (beginCommandList 가 Reset 으로 다시 연다).
        if ( _device == nullptr )
            return entry;

        // HRESULT 를 여기서 남긴다. 부르는 쪽은 "생성 실패" 한 줄만 남기므로 원인(메모리 부족인지 디바이스 제거인지)은 여기서만 보인다.
        const HRESULT allocHr = _device->CreateCommandAllocator( type, IID_PPV_ARGS( entry._allocator.GetAddressOf() ) );
        if ( FAILED( allocHr ) )
        {
            if ( _bDeviceRemovedLogged.load( std::memory_order_relaxed ) == SW_FALSE )
                SW_LOG_ERROR_ONCE_ID( "D3D12.AcquireCommandListEntryCreateCommandAllocatorFailedHr", "acquireCommandListEntry: CreateCommandAllocator failed hr=0x%# removed=0x%# pooled=%#",
                                      static_cast<uint32>( allocHr ),
                                      static_cast<uint32>( _device->GetDeviceRemovedReason() ),
                                      static_cast<uint32>( _cmdListEntryCreated ) );
            flushDebugMessages( "acquireCommandListEntry" );
            entry = D3D12CommandListEntry{};
            return entry;
        }
        const HRESULT listHr = _device->CreateCommandList( 0, type, entry._allocator.Get(), nullptr,
                                                           IID_PPV_ARGS( entry._list.GetAddressOf() ) );
        if ( FAILED( listHr ) )
        {
            if ( _bDeviceRemovedLogged.load( std::memory_order_relaxed ) == SW_FALSE )
                SW_LOG_ERROR_ONCE_ID( "D3D12.AcquireCommandListEntryCreateCommandListFailedHr", "acquireCommandListEntry: CreateCommandList failed hr=0x%# removed=0x%# pooled=%#",
                                      static_cast<uint32>( listHr ),
                                      static_cast<uint32>( _device->GetDeviceRemovedReason() ),
                                      static_cast<uint32>( _cmdListEntryCreated ) );
            entry = D3D12CommandListEntry{};
            return entry;
        }
        const uint32 entryIndex = _cmdListEntryCreated++;
        utf16        arrName[constant::buffer::kMax64]{};
        swprintf_s( arrName, L"PoolAllocator%u", entryIndex );
        entry._allocator->SetName( arrName );
        swprintf_s( arrName, L"PoolList%u", entryIndex );
        entry._list->SetName( arrName );
        // 빈 리스트 닫기가 실패하면 장치가 쓸 수 없는 상태다 — 빈 쌍을 돌려줘 부르는 쪽이 생성 실패로 다룬다.
        if ( isHrSuccess( entry._list->Close(), "ID3D12GraphicsCommandList::Close (pool entry)" ) == false )
            entry = D3D12CommandListEntry{};
        return entry;
    }

    void D3D12RHIDevice::recycleCommandListEntryDeferred( const D3D12CommandListEntry& entry )
    {
        if ( entry._list == nullptr || entry._allocator == nullptr )
            return;

        // 제출 직후 리스트 객체가 사라져도 GPU 는 아직 이 얼로케이터의 커맨드 메모리를 읽고 있다.
        // 해제 큐에 실어 현재 펜스가 통과한 뒤에야 재사용 풀로 돌려보낸다. 그래야 다음 사용자가
        // Reset 해도 안전하다.
        // 컴퓨트 큐 쌍도 같은 프레임 펜스를 기다린다 — 프레임 끝이 컴퓨트 큐의 일을 모두 기다린 뒤 프레임 펜스를 신호한다(`endFrameInternal`).
        auto recycleCb = [this, entry]()
        {
            ScopedLock<mutex> lock{ _cmdListPoolMutex };
            if ( entry._type == D3D12_COMMAND_LIST_TYPE_COMPUTE )
                _listFreeComputeCmdListEntry.push_back( entry );
            else
                _listFreeCmdListEntry.push_back( entry );
        };
        _releaseQueue.enqueueGPURelease( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, recycleCb ), _fenceValue );
    }

    uint32 D3D12RHIDevice::getTimestampSlotCount() const
    {
        return ( _bTimestampEnabled.load( std::memory_order_relaxed ) != SW_FALSE && _timestampHeap != nullptr && _timestampFrequency != 0 )
                 ? constant::render::kMaxGPUTimestampSlot
                 : 0u;
    }

    bool D3D12RHIDevice::readTimestamps( RHITimestampFrame& outFrame )
    {
        outFrame = _timestampFrame;
        return outFrame._listMicro.empty() == false;
    }

    bool D3D12RHIDevice::readGPUClockNanos( int64& outGPUNanos )
    {
        outGPUNanos = 0;
        if ( _commandQueue == nullptr )
            return false;
        uint64 frequency = _timestampFrequency;
        if ( frequency == 0 && FAILED( _commandQueue->GetTimestampFrequency( &frequency ) ) )
            return false;
        if ( frequency == 0 )
            return false;
        // 큐의 GPU 시계와 CPU 시계를 한 순간에 읽는다(기다리지 않는다). GPU 값만 쓴다 — CPU 쪽은 Tracy 가 자기 시계로 찍는다.
        uint64 gpuTick{ 0 };
        uint64 cpuTick{ 0 };
        if ( FAILED( _commandQueue->GetClockCalibration( &gpuTick, &cpuTick ) ) )
            return false;
        outGPUNanos = RHITimestampFrame::convertTickToNanos( gpuTick, constant::time::kNanosecondsPerSecond / static_cast<float64>( frequency ) );
        return true;
    }

    void D3D12RHIDevice::ensureTimestampResources()
    {
        const bool bTimestampResourcesNotNeeded = _bTimestampEnabled.load( std::memory_order_relaxed ) == SW_FALSE || _timestampHeap != nullptr || _device == nullptr ||
                                                  _commandQueue == nullptr;
        if ( bTimestampResourcesNotNeeded )
            return;

        UINT64 frequency{ 0 };
        if ( FAILED( _commandQueue->GetTimestampFrequency( &frequency ) ) || frequency == 0 )
            return;

        D3D12_QUERY_HEAP_DESC heapDesc{};
        heapDesc.Type  = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        heapDesc.Count = constant::render::kMaxGPUTimestampSlot * constant::render::kMaxFrameCountInFlight;
        if ( isHrSuccess( _device->CreateQueryHeap( &heapDesc, IID_PPV_ARGS( &_timestampHeap ) ), "CreateQueryHeap" ) == false )
            return;

        // 읽기 전용 버퍼 하나에 링 전체 분량을 담는다. 슬롯마다 구간이 겹치지 않는다.
        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC bufferDesc{};
        bufferDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufferDesc.Width            = sizeof( uint64 ) * heapDesc.Count;
        bufferDesc.Height           = 1;
        bufferDesc.DepthOrArraySize = 1;
        bufferDesc.MipLevels        = 1;
        bufferDesc.SampleDesc.Count = 1;
        bufferDesc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if ( isHrSuccess( _device->CreateCommittedResource( &heapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc,
                                                            D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                            IID_PPV_ARGS( &_timestampReadback ) ),
                          "CreateCommittedResource" ) == false )
        {
            _timestampHeap.Reset();
            return;
        }
        _timestampFrequency = frequency;
    }

    void D3D12RHIDevice::collectTimestampsForSlot()
    {
        // 이 슬롯은 방금 펜스를 통과했다. 지난번 이 슬롯에 적은 값이 GPU 에서 이미 끝나 있다.
        _timestampFrame._listMicro.clear();
        const GPUTimestampSlotMask writtenMask         = _arrTimestampMask[_frameRing.currentIndex()];
        const bool                 bNoTimestampsToRead = _timestampReadback == nullptr || _timestampFrequency == 0 || writtenMask.any() == false;
        if ( bNoTimestampsToRead )
            return;

        const uint32 base       = getTimestampBase();
        const size_t byteOffset = sizeof( uint64 ) * base;
        D3D12_RANGE  range{ byteOffset, byteOffset + sizeof( uint64 ) * constant::render::kMaxGPUTimestampSlot };

        void* pMapped{ nullptr };
        if ( isHrSuccess( _timestampReadback->Map( 0, &range, &pMapped ), "ID3D12Resource::Map (timestamp readback)" ) == false || pMapped == nullptr )
            return;

        // DX12 는 쿼리 힙을 리셋하지 않는다. 안 적은 칸에는 **지난 사이클의 값**이 그대로 남는다.
        // 그래서 어느 칸이 이번 것인지 비트로 가려야 한다. 안 그러면 건너뛴 패스가 0us 로 보고된다.
        const uint64* pTicks = reinterpret_cast<const uint64*>( static_cast<const uint8*>( pMapped ) + byteOffset );
        // 그래서 writtenMask 가 곧 준비 비트다. 이 슬롯은 방금 펜스를 통과했다.
        RHITimestampFrame::resolve( pTicks, writtenMask, constant::time::kNanosecondsPerSecond / static_cast<float64>( _timestampFrequency ), _timestampFrame );

        const D3D12_RANGE emptyRange{ 0, 0 };
        _timestampReadback->Unmap( 0, &emptyRange );
    }

    void D3D12RHIDevice::beginFrameInternal()
    {
        if ( _frameStreamState._bRecording == SW_FALSE )
        {
            waitForRingSlot();
            // **여기가 타임스탬프를 읽는 유일한 안전한 자리다.** 이 슬롯의 펜스를 방금 통과했으므로
            // 지난번 이 슬롯에 적은 값이 GPU 에서 끝나 있다. 기다리지 않으니 파이프라인이 안 멈춘다.
            ensureTimestampResources();
            collectTimestampsForSlot();
            _timestampWrittenMask.clear();
            // 링 슬롯이 정해졌다. 상수버퍼 CBV 를 그 슬롯으로 맞춘다(드로우 경로에서 하던 일).
            refreshConstantBufferViews();
            // 프레임 스트림은 세그먼트로 나뉜다. 첫 세그먼트는 디바이스 소유 리스트를 그대로 쓰고,
            // 커맨드 리스트가 제출될 때마다 executeCommandList 가 잘라 새 세그먼트를 연다.
            _listPendingSubmit.clear();
            ID3D12CommandAllocator* pAllocator = currentAllocator();
            if ( pAllocator == nullptr || _commandList == nullptr )
                return;
            // 디바이스가 제거된 상태에서는 Reset() 이 실패해 커맨드 리스트가 여전히 closed 로 남는다.
            // 그것을 무시하고 _bRecording=1 로 넘어가면 이후의 모든 커맨드 리스트 호출이 "closed command
            // list" 에러를 매번 뱉으며 프레임마다 반복 폭주하게 된다. 실패하면 이번 프레임을 건너뛴다.
            if ( isHrSuccess( pAllocator->Reset(), "Reset" ) == false || isHrSuccess( _commandList->Reset( pAllocator, nullptr ), "Reset" ) == false )
                return;
            _frameStreamState._bRecording      = SW_TRUE;
            _frameStreamState._arrSlotState[0] = D3D12SlotTableState{}; // 새 리스트라 슬롯 테이블은 첫 드로우가 다시 굳힌다
            _frameStreamState._arrSlotState[1] = D3D12SlotTableState{};
            _pActiveFrameList                  = _commandList.Get();
            _frameStreamContext->rebindCommandList( _pActiveFrameList );
            bindBindlessRootState( _pActiveFrameList );
        }
        _swapChain.acquireNextImage();

        // resize() 가 ResizeBuffers 실패로 일찍 반환하면 백버퍼가 비워진 채로 남는다. 그 상태로
        // 계속 진행하면 null 리소스가 배리어에 들어간다. 디바이스가 이미 맛이 간 프레임이므로
        // 이번 프레임은 조용히 건너뛴다.
        if ( _swapChain.isBackBufferReady() == false )
            return;

        // 백버퍼 바인딩(RENDER_TARGET 배리어 + OMSetRenderTargets + Clear)은 여기서 하지 않는다.
        // beginFrame 은 프레임 수명주기 전용이고, 백버퍼 타깃팅은 RenderThread 가 여는 beginRenderPass(핸들 0) 가
        // 배리어까지 포함해 명시적으로 한다. 뷰포트/시저는 기본값으로 남긴다.

        constexpr float32 kDefaultViewportX        = 0.0f;
        constexpr float32 kDefaultViewportY        = 0.0f;
        constexpr float32 kDefaultViewportMinDepth = 0.0f;
        constexpr float32 kDefaultViewportMaxDepth = 1.0f;

        D3D12_VIEWPORT viewport{};
        viewport.Width    = static_cast<float32>( _swapChain.getWidth() );
        viewport.Height   = static_cast<float32>( _swapChain.getHeight() );
        viewport.MinDepth = kDefaultViewportMinDepth;
        viewport.MaxDepth = kDefaultViewportMaxDepth;
        viewport.TopLeftX = kDefaultViewportX;
        viewport.TopLeftY = kDefaultViewportY;
        _pActiveFrameList->RSSetViewports( 1, &viewport );

        D3D12_RECT scissorRect{ 0, 0, static_cast<LONG>( _swapChain.getWidth() ), static_cast<LONG>( _swapChain.getHeight() ) };
        _pActiveFrameList->RSSetScissorRects( 1, &scissorRect );
    }

    // 길이를 나누지 않는다: 닫기 · 타임스탬프 해석 · 제출 · 펜스 · Present 의 프레임 끝 순서가 곧 뜻이고,
    // 업로드 리스트를 프레임 리스트 앞에 끼우는 규칙이 한곳에 있어야 한다.
    void D3D12RHIDevice::endFrameInternal( bool vsync, bool bPresent )
    {
        // 열린 렌더 패스는 공용 `IRHIDevice::endFrame` 이 이미 닫았다(아래 Present 전이 · 타임스탬프 Resolve 는 패스 밖의 일이다).
        if ( bPresent )
        {
            _swapChain.transitionTo( _pActiveFrameList, D3D12_RESOURCE_STATE_PRESENT );
            for ( D3D12RHISwapChain* pWindow : _listFrameSwapChain )
            {
                pWindow->transitionTo( _pActiveFrameList, D3D12_RESOURCE_STATE_PRESENT );
            }
        }

        // Present 의 300 us 가 어디로 가는지 측정하려고 닫기 · 제출 / DXGI Present / 펜스 신호 · 다음 이미지로 나눠 측정한다.
        {
            SW_PROFILE_SCOPE( "RT.Present.submit" );
            static const uint32 s_slotSubmitListCount = engine::getSubsystem<FrameProfiler>().registerScope( "RT.Present.submit.listCount" );
            if ( _frameStreamState._bRecording != SW_FALSE && _pActiveFrameList != nullptr )
            {
                SW_PROFILE_SCOPE( "RT.Present.submit.close" );
                // 구간 전체를 읽기 버퍼로 옮긴다. 32 칸이면 256 바이트라 옮기는 값이 사실상 공짜고,
                // 어느 칸이 이번 것인지는 비트로 따로 굳혀 둔다(안 적은 칸에는 지난 사이클 값이 남아 있다).
                // 슬롯 번호는 **패스 인덱스로 고정**이라 병렬 기록에도 경쟁이 없다.
                const GPUTimestampSlotMask writtenMask       = _timestampWrittenMask.load();
                _arrTimestampMask[_frameRing.currentIndex()] = writtenMask;
                const bool bShouldResolveTimestamps          = _bTimestampEnabled.load( std::memory_order_relaxed ) != SW_FALSE && _timestampHeap != nullptr && _timestampReadback != nullptr &&
                                                      writtenMask.any();
                if ( bShouldResolveTimestamps )
                {
                    // 적힌 칸의 연속 구간만 옮긴다. 한 번도 적지 않은 칸을 Resolve 하면 디버그 레이어 오류(1319 "Cannot Resolve query that has never been performed")다.
                    const uint32 base = getTimestampBase();
                    uint32       slot = 0;
                    while ( slot < constant::render::kMaxGPUTimestampSlot )
                    {
                        if ( writtenMask.test( slot ) == false )
                        {
                            ++slot;
                            continue;
                        }
                        const uint32 runStart = slot;
                        while ( slot < constant::render::kMaxGPUTimestampSlot && writtenMask.test( slot ) )
                        {
                            ++slot;
                        }
                        _pActiveFrameList->ResolveQueryData( _timestampHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, base + runStart, slot - runStart,
                                                             _timestampReadback.Get(), sizeof( uint64 ) * ( base + runStart ) );
                    }
                }
                requireHr( _pActiveFrameList->Close(), "ID3D12GraphicsCommandList::Close (end frame)" );
                _listPendingSubmit.push_back( _pActiveFrameList );
                _frameStreamState._bRecording = SW_FALSE;
                releaseOnlineBlocksDeferred( _frameStreamState );
            }

            // 열어 둔 구조 버퍼 갱신 복사 리스트를 프레임 리스트 앞에 끼우고 한 번에 제출한다. **실행까지 업로드 락을 쥔다** — 닫아서 대기열에 넣은 복사
            // 리스트를 실행 전에 다른 스레드의 갱신이 `Reset` 하면 GPUScene 복사가 사라진다(submitPendingIfImmediate 와 같은 이유).
            // 텍스처 전송은 제출 때 이미 큐에 나갔다 — 이 프레임 리스트들보다 먼저 실행된다(계약 3).
            // 아직 기다리지 않은 비동기 컴퓨트 일이 있으면 마지막 그래픽스 일 앞에서 기다린다 — 뒤의 프레임 펜스 하나가 두 큐의 끝을 덮어야
            // 컴퓨트 리스트 얼로케이터와 지연 해제가 프레임 펜스로 충분하다.
            if ( _computeQueue != nullptr && _computeFenceValue > _computeWaitedValue )
            {
                requireHr( _commandQueue->Wait( _computeFence.Get(), _computeFenceValue ), "ID3D12CommandQueue::Wait (end of frame)" );
                _computeWaitedValue = _computeFenceValue;
            }
            {
                ScopedLock<mutex> uploadLock{ _uploadSlotMutex };
                flushPendingUploads( false );

                // 프레임 세그먼트와 패스 리스트를 기록 순서 그대로 한 번에 제출한다.
                if ( _listPendingSubmit.empty() == false && _commandQueue != nullptr )
                {
                    SW_PROFILE_SCOPE( "RT.Present.submit.execute" );
                    engine::getSubsystem<FrameProfiler>().addCount( s_slotSubmitListCount, _listPendingSubmit.size() );
                    _commandQueue->ExecuteCommandLists( static_cast<UINT>( _listPendingSubmit.size() ),
                                                        _listPendingSubmit.data() );
                }
                _listPendingSubmit.clear();
            }
            _pActiveFrameList = nullptr;
        }

        // 빌려 쓴 추가 세그먼트는 이번 프레임 펜스를 통과한 뒤 풀로 돌아간다.
        for ( D3D12CommandListEntry& segment : _listFrameSegment )
        {
            recycleCommandListEntryDeferred( segment );
        }
        _listFrameSegment.clear();

        if ( bPresent )
        {
            HRESULT presentHr = S_OK;
            {
                SW_PROFILE_SCOPE( "RT.Present.present" );
                presentHr = _swapChain.present( vsync );
                // 이 프레임에 그린 다른 창도 같은 제출 뒤에 표시한다(Godot 의 execute_and_present 가 여러 스왑체인을 함께 내는 자리).
                for ( D3D12RHISwapChain* pWindow : _listFrameSwapChain )
                {
                    const HRESULT windowHr = pWindow->present( vsync );
                    if ( FAILED( windowHr ) )
                        SW_LOG_ERROR_ONCE_ID( "D3D12.WindowPresentFailedHr", "Present of a window swap chain failed hr=%#", static_cast<uint32>( windowHr ) );
                    pWindow->markPresented();
                }
            }
            if ( FAILED( presentHr ) )
            {
                [[maybe_unused]] const HRESULT removed = _device->GetDeviceRemovedReason();
                // Present 가 장치 제거 · 리셋으로 실패하면(DXGI_ERROR_DEVICE_REMOVED / DEVICE_RESET) 전송 손실로 표시한다. 디버그 전용인 flushDebugMessages 에 맡기지 않는다.
                const bool bPresentLostDevice = presentHr == DXGI_ERROR_DEVICE_REMOVED || presentHr == DXGI_ERROR_DEVICE_RESET || FAILED( removed );
                if ( bPresentLostDevice )
                    _transferSerials.markLost();
                // 디바이스 제거는 자동 복구가 없어서 한 번 일어나면 이후 매 프레임 여기로 들어온다.
                // 첫 발견 때만 로그를 남기고 그 뒤로는 조용히 건너뛰어 로그 폭주를 막는다.
                if ( _bDeviceRemovedLogged.load( std::memory_order_relaxed ) == SW_FALSE )
                {
                    // %# 는 10진 출력이다. DXGI_ERROR_DEVICE_HUNG == 0x887A0005 == 2289696773
                    SW_LOG_ERROR_ONCE_ID( "D3D12.PresentFailedHrX887A0005", "Present failed hr=%# (0x887A0005=DEVICE_HUNG), DeviceRemovedReason=%#",
                                          static_cast<uint32>( presentHr ), static_cast<uint32>( removed ) );
                }
                flushDebugMessages( "after Present" );
            }
        }
        {
            SW_PROFILE_SCOPE( "RT.Present.signalAcquire" );
            signalCurrentFrame();
            _swapChain.acquireNextImage();
        }
        if ( bPresent )
            _swapChain.markPresented();
        _listFrameSwapChain.clear();
    }

    void D3D12RHIDevice::bindBindlessRootState( ID3D12GraphicsCommandList* pList )
    {
        if ( pList == nullptr || _cbvHeap == nullptr )
            return;
        ID3D12DescriptorHeap* heaps[] = { _cbvHeap.Get() };
        pList->SetDescriptorHeaps( 1, heaps );
        if ( _rootSignature == nullptr )
            return;
        // 유일한 테이블(텍스처 배열)이 힙 시작을 가리킨다. 리스트가 사는 동안 바뀌지 않는다. 같은 루트 시그니처를
        // 그래픽스/컴퓨트 두 바인드 포인트에 건다(루트 인자는 바인드 포인트별로 따로 산다). 상수버퍼는 이후 루트 CBV 로, t/u 는 슬롯 테이블로 건다.
        const D3D12_GPU_DESCRIPTOR_HANDLE heapStart = _cbvHeap->GetGPUDescriptorHandleForHeapStart();
        pList->SetGraphicsRootSignature( _rootSignature.Get() );
        pList->SetGraphicsRootDescriptorTable( kBindlessTextureTableParam, heapStart );
        pList->SetComputeRootSignature( _rootSignature.Get() );
        pList->SetComputeRootDescriptorTable( kBindlessTextureTableParam, heapStart );
    }

    void D3D12RHIDevice::bindBindlessComputeRootState( ID3D12GraphicsCommandList* pList )
    {
        if ( pList == nullptr || _cbvHeap == nullptr )
            return;
        ID3D12DescriptorHeap* heaps[] = { _cbvHeap.Get() };
        pList->SetDescriptorHeaps( 1, heaps );
        if ( _rootSignature == nullptr )
            return;
        pList->SetComputeRootSignature( _rootSignature.Get() );
        pList->SetComputeRootDescriptorTable( kBindlessTextureTableParam, _cbvHeap->GetGPUDescriptorHandleForHeapStart() );
    }

    void D3D12RHIDevice::refreshConstantBufferViews()
    {
        if ( _device == nullptr || _cbvHeap == nullptr )
            return;

        // 링 상수버퍼의 CBV 는 이번 프레임 슬롯을 가리켜야 한다. 슬롯은 프레임당 한 번 바뀌므로 여기서 한 번에 맞춘다.
        // 렌더 스레드의 기록 시작 전이지만 게임 스레드는 그와 겹쳐 버퍼를 만들고 부수므로(레지스트리 · 상수버퍼 맵에 쓴다)
        // 읽기 락을 잡는다. 주의: "단일 스레드 구간" 이 아니다.
        ScopedSharedLock<shared_mutex> registryLock{ _bindlessMutex };
        const uint32                   slot = _frameRing.currentIndex();
        for ( BindlessResourceRecord& record : _listRegisteredBindless )
        {
            if ( record._resource == nullptr || record._buffer == 0 )
                continue;
            const auto sizeIt = _mapCbAlignedSize.find( record._buffer );
            if ( sizeIt == _mapCbAlignedSize.end() )
                continue; // 링 상수버퍼가 아니다 (구조버퍼 SRV 등은 주소가 안 바뀐다).

            D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc{};
            cbvDesc.BufferLocation = record._resource->GetGPUVirtualAddress() + static_cast<UINT64>( slot ) * sizeIt->second;
            cbvDesc.SizeInBytes    = sizeIt->second;
            _device->CreateConstantBufferView( &cbvDesc, record._cpuHandle );
        }
    }

    void D3D12RHIDevice::writeConstantBufferSlot( RHIBufferHandle buffer, uint32 slot, const void* pData, uint32 size )
    {
        const auto sizeIt                      = _mapCbAlignedSize.find( buffer );
        const auto mapIt                       = _mapCbMapped.find( buffer );
        const bool bCannotUpdateConstantBuffer = sizeIt == _mapCbAlignedSize.end() || mapIt == _mapCbMapped.end() || mapIt->second == nullptr || pData == nullptr;
        if ( bCannotUpdateConstantBuffer )
            return;
        const uint32 alignedSize = sizeIt->second;
        const uint32 copyBytes   = MathUtil::min( size, alignedSize );
        Memory::copy( static_cast<uint8*>( mapIt->second ) + static_cast<size_t>( slot ) * alignedSize, pData, copyBytes );
    }

    void D3D12RHIDevice::fillConstantBufferSlot()
    {
        // 맵 조회가 게임 스레드의 만들기 · 부수기와 겹치지 않게 읽기 락을 쥔다(락 순서: 레지스트리 → 그림자).
        ScopedSharedLock<shared_mutex> registryLock{ _bindlessMutex };
        _constantBufferMirror.fillSlot( _frameRing.currentIndex(), [this]( RHIBufferHandle buffer, uint32 slot, const void* pData, uint32 size )
        { writeConstantBufferSlot( buffer, slot, pData, size ); } );
    }

    // 길이 유지(긴 함수 정리 기준): 루트 시그니처 · 힙 · 전역 리소스 생성의 순서가 곧 의미라 나누지 않는다.
    bool D3D12RHIDevice::createGlobalResources()
    {
        // 루트 시그니처(binding_slots.hlsli). 언리얼 FD3D12RootSignature 와 같은 배치: CB 는 루트 CBV, t/u 슬롯은 테이블, 텍스처는 배열 테이블.
        //  [0..2]  루트 CBV  b0..b2 (PassCB / MaterialCB · 컴퓨트 CB / 예비)
        //  [3]     테이블: t0..t11 space0 (컴퓨트 읽기 t0..t3, 인스턴스 t4, 머티리얼 데이터 t5. 오프라인 뷰를 드로우 직전 온라인 블록에 복사)
        //  [4]     테이블: u0..u4 space0 (컴퓨트 쓰기 — u4 는 셰이더 assert 링)
        //  [5]     테이블: t0 space1 무제한 텍스처 배열 + u0 space1 무제한 RW 텍스처 배열 (둘 다 힙 시작). SM6.6 ResourceDescriptorHeap 은 쓰지 않는다.
        //  [6]     32비트 루트 상수 b0 space2 (set*RootConstants)
        //  정적 샘플러 s0..s7 (binding_slots.hlsli 4 의 세트), space0.
        // 비용: shaderslot::dx12::kRootSignatureDwords = 3*2 + 3*1 + 16 = 25 dword (한계 64). t/u 를 루트 디스크립터로 두면 51 이 된다.
        {
            D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
            if ( SUCCEEDED( _device->CheckFeatureSupport( D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof( options ) ) ) &&
                 options.ResourceBindingTier < D3D12_RESOURCE_BINDING_TIER_2 )
            {
                SW_LOG_ERROR_ID( "D3D12.ResourceBindingTierUnbounded", "Resource Binding Tier %# - unbounded texture arrays need Tier 2 or higher.",
                                 static_cast<uint32>( options.ResourceBindingTier ) );
            }
        }

        D3D12_ROOT_PARAMETER arrParam[kRootParameterCount]{};
        auto                 setRootDescriptor = [&]( uint32 paramIndex, D3D12_ROOT_PARAMETER_TYPE type, uint32 shaderRegister )
        {
            arrParam[paramIndex].ParameterType             = type;
            arrParam[paramIndex].Descriptor.ShaderRegister = shaderRegister;
            arrParam[paramIndex].Descriptor.RegisterSpace  = 0;
            arrParam[paramIndex].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
        };
        for ( uint32 slot = 0; slot < shaderslot::kConstantBufferSlotCount; ++slot )
        {
            setRootDescriptor( kCBVRootParam0 + slot, D3D12_ROOT_PARAMETER_TYPE_CBV, slot );
        }

        // t/u 슬롯 테이블. 범위 하나씩. 테이블 시작은 드로우 · 디스패치 직전 flushSlotTables 가 온라인 블록에 굳혀 건다.
        D3D12_DESCRIPTOR_RANGE srvSlotRange{};
        srvSlotRange.RangeType                                       = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvSlotRange.NumDescriptors                                  = shaderslot::kSRVSlotCount;
        srvSlotRange.BaseShaderRegister                              = 0;
        srvSlotRange.RegisterSpace                                   = 0;
        srvSlotRange.OffsetInDescriptorsFromTableStart               = 0;
        arrParam[kSRVTableParam].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        arrParam[kSRVTableParam].DescriptorTable.NumDescriptorRanges = 1;
        arrParam[kSRVTableParam].DescriptorTable.pDescriptorRanges   = &srvSlotRange;
        arrParam[kSRVTableParam].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_DESCRIPTOR_RANGE uavSlotRange{};
        uavSlotRange.RangeType                                       = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        uavSlotRange.NumDescriptors                                  = shaderslot::kComputeUAVSlotCount;
        uavSlotRange.BaseShaderRegister                              = 0;
        uavSlotRange.RegisterSpace                                   = 0;
        uavSlotRange.OffsetInDescriptorsFromTableStart               = 0;
        arrParam[kUAVTableParam].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        arrParam[kUAVTableParam].DescriptorTable.NumDescriptorRanges = 1;
        arrParam[kUAVTableParam].DescriptorTable.pDescriptorRanges   = &uavSlotRange;
        arrParam[kUAVTableParam].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;

        // 텍스처 테이블. 범위 둘이 같은 힙 시작을 가리킨다: t0 space1 = Texture2D g_SwBindlessTex2D[], u0 space1 = RWTexture2D
        // g_SwBindlessRWTex2D[] (컴퓨트). 인덱스는 등록이 준 힙 슬롯이라 둘 다 offset 0 이다.
        D3D12_DESCRIPTOR_RANGE arrTextureRange[2]{};
        arrTextureRange[0].RangeType                                             = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        arrTextureRange[0].NumDescriptors                                        = UINT_MAX; // 무제한
        arrTextureRange[0].BaseShaderRegister                                    = 0;
        arrTextureRange[0].RegisterSpace                                         = shaderslot::bindless::kTextureSpace;
        arrTextureRange[0].OffsetInDescriptorsFromTableStart                     = 0;
        arrTextureRange[1].RangeType                                             = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        arrTextureRange[1].NumDescriptors                                        = UINT_MAX;
        arrTextureRange[1].BaseShaderRegister                                    = 0;
        arrTextureRange[1].RegisterSpace                                         = shaderslot::bindless::kTextureSpace;
        arrTextureRange[1].OffsetInDescriptorsFromTableStart                     = 0;
        arrParam[kBindlessTextureTableParam].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        arrParam[kBindlessTextureTableParam].DescriptorTable.NumDescriptorRanges = 2;
        arrParam[kBindlessTextureTableParam].DescriptorTable.pDescriptorRanges   = arrTextureRange;
        arrParam[kBindlessTextureTableParam].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;

        arrParam[kRootConstantsParam].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        arrParam[kRootConstantsParam].Constants.ShaderRegister = shaderslot::kRootConstantRegister;
        arrParam[kRootConstantsParam].Constants.RegisterSpace  = shaderslot::kRootConstantSpace;
        arrParam[kRootConstantsParam].Constants.Num32BitValues = shaderslot::kRootConstantDwords;
        arrParam[kRootConstantsParam].ShaderVisibility         = D3D12_SHADER_VISIBILITY_ALL;

        // 정적 샘플러 세트 s0..s7 (binding_slots.hlsli 4, 언리얼의 정적 샘플러와 같은 자리). 셰이더는 g_SwSamplers[SW_SAMPLER_*].
        D3D12_STATIC_SAMPLER_DESC staticSamplers[shaderslot::kStaticSamplerCount]{};
        struct StaticSamplerSpec
        {
            D3D12_FILTER               _filter;
            D3D12_TEXTURE_ADDRESS_MODE _address;
            D3D12_COMPARISON_FUNC      _comparison;
            uint32                     _anisotropy;
        };
        const StaticSamplerSpec arrSpec[shaderslot::kStaticSamplerCount] = {
            {                 D3D12_FILTER_MIN_MAG_MIP_LINEAR,   D3D12_TEXTURE_ADDRESS_MODE_WRAP,     D3D12_COMPARISON_FUNC_ALWAYS, 1}, // LINEAR_WRAP
            {                 D3D12_FILTER_MIN_MAG_MIP_LINEAR,  D3D12_TEXTURE_ADDRESS_MODE_CLAMP,     D3D12_COMPARISON_FUNC_ALWAYS, 1}, // LINEAR_CLAMP
            {                  D3D12_FILTER_MIN_MAG_MIP_POINT,   D3D12_TEXTURE_ADDRESS_MODE_WRAP,     D3D12_COMPARISON_FUNC_ALWAYS, 1}, // POINT_WRAP
            {                  D3D12_FILTER_MIN_MAG_MIP_POINT,  D3D12_TEXTURE_ADDRESS_MODE_CLAMP,     D3D12_COMPARISON_FUNC_ALWAYS, 1}, // POINT_CLAMP
            {                 D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_MIRROR,     D3D12_COMPARISON_FUNC_ALWAYS, 1}, // LINEAR_MIRROR
            {                        D3D12_FILTER_ANISOTROPIC,   D3D12_TEXTURE_ADDRESS_MODE_WRAP,     D3D12_COMPARISON_FUNC_ALWAYS, 8}, // ANISO_WRAP
            {                  D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_BORDER,     D3D12_COMPARISON_FUNC_ALWAYS, 1}, // POINT_BORDER
            {D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT,  D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_COMPARISON_FUNC_LESS_EQUAL, 1}, // SHADOW_CMP
        };
        for ( uint32 samplerIndex = 0; samplerIndex < shaderslot::kStaticSamplerCount; ++samplerIndex )
        {
            D3D12_STATIC_SAMPLER_DESC& sampler = staticSamplers[samplerIndex];
            sampler.Filter                     = arrSpec[samplerIndex]._filter;
            sampler.AddressU                   = arrSpec[samplerIndex]._address;
            sampler.AddressV                   = arrSpec[samplerIndex]._address;
            sampler.AddressW                   = arrSpec[samplerIndex]._address;
            sampler.MipLODBias                 = 0.0f;
            sampler.MaxAnisotropy              = arrSpec[samplerIndex]._anisotropy;
            sampler.ComparisonFunc             = arrSpec[samplerIndex]._comparison;
            sampler.BorderColor                = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
            sampler.MinLOD                     = 0.0f;
            sampler.MaxLOD                     = D3D12_FLOAT32_MAX;
            sampler.ShaderRegister             = samplerIndex;
            sampler.RegisterSpace              = 0;
            sampler.ShaderVisibility           = D3D12_SHADER_VISIBILITY_ALL;
        }

        D3D12_ROOT_SIGNATURE_DESC rootSigDesc{};
        rootSigDesc.NumParameters     = kRootParameterCount;
        rootSigDesc.pParameters       = arrParam;
        rootSigDesc.NumStaticSamplers = _countof( staticSamplers );
        rootSigDesc.pStaticSamplers   = staticSamplers;
        rootSigDesc.Flags             = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob;
        Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
        _bBindlessRootSignature = SW_FALSE;
        if ( FAILED( D3D12SerializeRootSignature( &rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1_0, &signatureBlob, &errorBlob ) ) )
        {
            if ( errorBlob )
                SW_LOG_ERROR_ID( "D3D12.RootSignatureSerializeError", "Root Signature Serialize Error: %s", static_cast<const utf8*>( errorBlob->GetBufferPointer() ) );
            return false;
        }
        if ( isHrSuccess( _device->CreateRootSignature( 0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS( _rootSignature.GetAddressOf() ) ), "CreateRootSignature" ) == false )
        {
            SW_LOG_ERROR_ID( "D3D12.CreateRootSignatureFailed", "CreateRootSignature failed." );
            return false;
        }
        _bBindlessRootSignature = SW_TRUE;

        D3D12_INDIRECT_ARGUMENT_DESC drawArg{};
        drawArg.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;

        D3D12_COMMAND_SIGNATURE_DESC drawCmdSigDesc{};
        drawCmdSigDesc.ByteStride       = sizeof( D3D12_DRAW_ARGUMENTS );
        drawCmdSigDesc.NumArgumentDescs = 1;
        drawCmdSigDesc.pArgumentDescs   = &drawArg;
        // 간접 드로우 · 디스패치 서명은 장치 초기화의 필수 조건이다. 못 만들면 간접 호출이 조용히 빠지지 않게 초기화를 실패시킨다.
        if ( isHrSuccess( _device->CreateCommandSignature( &drawCmdSigDesc, nullptr, IID_PPV_ARGS( _drawCommandSignature.GetAddressOf() ) ), "CreateCommandSignature (draw)" ) == false )
            return false;

        D3D12_INDIRECT_ARGUMENT_DESC drawIndexedArg{};
        drawIndexedArg.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;

        D3D12_COMMAND_SIGNATURE_DESC drawIndexedCmdSigDesc{};
        drawIndexedCmdSigDesc.ByteStride       = sizeof( D3D12_DRAW_INDEXED_ARGUMENTS );
        drawIndexedCmdSigDesc.NumArgumentDescs = 1;
        drawIndexedCmdSigDesc.pArgumentDescs   = &drawIndexedArg;
        if ( isHrSuccess( _device->CreateCommandSignature( &drawIndexedCmdSigDesc, nullptr, IID_PPV_ARGS( _drawIndexedCommandSignature.GetAddressOf() ) ), "CreateCommandSignature (drawIndexed)" ) == false )
            return false;

        D3D12_INDIRECT_ARGUMENT_DESC dispatchArg{};
        dispatchArg.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;

        D3D12_COMMAND_SIGNATURE_DESC dispatchCmdSigDesc{};
        dispatchCmdSigDesc.ByteStride       = sizeof( D3D12_DISPATCH_ARGUMENTS );
        dispatchCmdSigDesc.NumArgumentDescs = 1;
        dispatchCmdSigDesc.pArgumentDescs   = &dispatchArg;
        if ( isHrSuccess( _device->CreateCommandSignature( &dispatchCmdSigDesc, nullptr, IID_PPV_ARGS( _dispatchCommandSignature.GetAddressOf() ) ), "CreateCommandSignature (dispatch)" ) == false )
            return false;

        return true;
    }

    bool D3D12RHIDevice::isBindlessIndexLive( RHIBindlessIndexSpace space, RHIDescriptorIndex index ) const
    {
        ScopedSharedLock<shared_mutex> lock{ _bindlessMutex };
        if ( index >= _listRegisteredBindless.size() )
            return false;
        // 힙 하나를 나눠 쓰므로 종류까지 본다 — 텍스처 자리에 버퍼 뷰가 있으면 Texture2D 로 읽는 셰이더가 잘못된 디스크립터를 읽는다.
        const BindlessResourceRecord& record = _listRegisteredBindless[index];
        if ( record._resource == nullptr )
            return false;
        return ( space == RHIBindlessIndexSpace::Texture ) ? record._texture != 0 : record._buffer != 0;
    }

    bool D3D12RHIDevice::initializeTransfers( const RHISwapChainDesc& desc )
    {
        if ( desc._stagingRingBytes == 0 )
        {
            SW_LOG_ERROR_ID( "D3D12.D3D12BackendNeedsStaging", "The D3D12 backend needs a staging ring capacity (gv_rhiStagingRingMegabytes must be positive)" );
            return false;
        }
        return createStagingRing( _uploadRing, desc._stagingRingBytes, desc._stagingSoloMaxBytes );
    }

    void D3D12RHIDevice::shutdownTransfers()
    {
        // GPU 는 종료 2단계(waitIdle)에서 쉬었다. 잃은 장치면 끝나지 않은 작업이 남아 있을 수 있지만 더 기다리지 않는다(isDeviceLost 설명).
        _transferFenceQueue.retireAll( [this]( D3D12TransferJob& job )
        { recycleTransferJob( std::move( job ) ); } );
        // 전송을 기다리던 해제를 프레임 해제 큐로 넘기고 바로 비운다 — 네이티브 디바이스가 사라진 뒤의 소멸자에 맡기지 않는다(종료 3 · 4단계가 넣은 해제는 부를 단계가 없다).
        _transferReleaseQueue.flushAll();
        _releaseQueue.flushAll();
        {
            ScopedLock<mutex> jobLock{ _transferJobMutex };
            _listFreeTransferJob.clear();
        }
        {
            ScopedLock<mutex> lock{ _textureTransferMutex };
            _mapTextureTransferMarks.clear();
        }
        // 가져가지 않은 읽기는 읽기 버퍼와 함께 사라진다. 그 뒤의 가져가기 · 버리기는 업로드 티켓처럼 아무 일도 하지 않는다.
        _readbackTable.clear();
        destroyStagingRing( _uploadRing );
    }

    bool D3D12RHIDevice::createStagingRing( D3D12StagingRing& ring, uint64 capacityBytes, uint64 soloMaxBytes )
    {
        if ( createMappedStagingBuffer( capacityBytes, ring._buffer, ring._pMapped ) == false )
            return false;
        // 구간 시작 정렬은 텍스처 복사의 배치 오프셋 규칙(512)이다 — 구간 안에서 다시 맞추지 않아도 된다.
        ring._allocator.initialize( capacityBytes, static_cast<uint64>( D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT ), soloMaxBytes );
        return true;
    }

    void D3D12RHIDevice::destroyStagingRing( D3D12StagingRing& ring )
    {
        {
            ScopedLock<mutex> soloLock{ ring._soloMutex };
            destroyMappedStagingBuffer( ring._soloBuffer, ring._pSoloMapped );
        }
        destroyMappedStagingBuffer( ring._buffer, ring._pMapped );
        ring._allocator.shutdown();
    }

    bool D3D12RHIDevice::createMappedStagingBuffer( uint64 sizeBytes, Microsoft::WRL::ComPtr<ID3D12Resource>& outBuffer, uint8*& pOutMapped )
    {
        pOutMapped = nullptr;
        void* pMapped{ nullptr };
        if ( createMappedUploadBuffer( sizeBytes, outBuffer, pMapped ) == false )
        {
            SW_LOG_ERROR_ID( "D3D12.FailedCreateMapD3D12", "Failed to create or map the D3D12 staging upload buffer (%# bytes, gv_rhiStagingRingMegabytes · gv_rhiStagingSingleMaxMegabytes)", sizeBytes );
            return false;
        }
        pOutMapped = static_cast<uint8*>( pMapped );
        getMemoryTracker().recordAllocation( RHIMemoryKey::makeDeviceObject( outBuffer.Get() ), RHIMemoryKind::Staging, computeAllocationBytes( outBuffer.Get() ) );
        return true;
    }

    void D3D12RHIDevice::destroyMappedStagingBuffer( Microsoft::WRL::ComPtr<ID3D12Resource>& rBuffer, uint8*& pMapped )
    {
        // 자원을 놓으면 매핑도 풀린다. 추적기에는 매핑까지 끝난 버퍼만 올렸다.
        if ( rBuffer != nullptr )
            getMemoryTracker().recordFree( RHIMemoryKey::makeDeviceObject( rBuffer.Get() ) );
        rBuffer.Reset();
        pMapped = nullptr;
    }

    RHISubmitResult D3D12RHIDevice::allocateStagingSpan( D3D12StagingRing& ring, uint64 sizeBytes, const utf8* pCaller, RHIStagingSpan& outSpan )
    {
        RHIStagingAllocateResult result = ring._allocator.allocate( sizeBytes, _transferSerials.getWatermark(), outSpan );
        if ( result == RHIStagingAllocateResult::Full )
        {
            retireCompletedTransfers();
            result = ring._allocator.allocate( sizeBytes, _transferSerials.getWatermark(), outSpan );
        }
        // 결과 옮기기 · 못 받은 구간 놓기 · 이유 로그는 세 백엔드가 같이 쓰는 한 곳이다(잃은 장치면 Full 도 Rejected, 계약 2).
        const RHISubmitResult submitResult = RHIStagingRingUtil::settleAllocation( ring._allocator, _transferSerials, result, sizeBytes, "D3D12", pCaller, outSpan );
        if ( submitResult != RHISubmitResult::Accepted || outSpan._bSolo == SW_FALSE )
            return submitResult;
        // 할당기가 단독 구간을 내줬다면 앞선 단독 구간은 이미 돌아왔다 — 남은 버퍼(아직 회수가 지우지 않은 것)를 GPU 가 더 쓰지 않는다.
        ScopedLock<mutex> soloLock{ ring._soloMutex };
        destroyMappedStagingBuffer( ring._soloBuffer, ring._pSoloMapped );
        if ( createMappedStagingBuffer( outSpan._sizeBytes, ring._soloBuffer, ring._pSoloMapped ) )
            return RHISubmitResult::Accepted;
        return RHIStagingRingUtil::rejectSoloBufferFailure( ring._allocator, outSpan, "D3D12", pCaller );
    }

    void D3D12RHIDevice::destroyIdleSoloBuffer( D3D12StagingRing& ring )
    {
        ScopedLock<mutex> soloLock{ ring._soloMutex };
        if ( ring._soloBuffer == nullptr )
            return;
        // 판정은 락 안에서 한다 — 밖에서 보면 그 사이 다른 스레드가 새 단독 구간을 받아 버퍼를 만들 수 있다.
        ring._allocator.freeCompleted( _transferSerials.getWatermark() );
        if ( ring._allocator.hasLiveSolo() )
            return;
        destroyMappedStagingBuffer( ring._soloBuffer, ring._pSoloMapped );
    }

    bool D3D12RHIDevice::beginTransferJob( D3D12TransferJob& outJob )
    {
        outJob = D3D12TransferJob{};
        {
            ScopedLock<mutex> jobLock{ _transferJobMutex };
            if ( _listFreeTransferJob.empty() == false )
            {
                outJob = std::move( _listFreeTransferJob.back() );
                _listFreeTransferJob.pop_back();
            }
        }

        if ( outJob._list == nullptr )
        {
            // 새 리스트는 열린 채로 만들어진다 — 바로 기록한다.
            const HRESULT allocatorHr = _device->CreateCommandAllocator( D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS( outJob._allocator.GetAddressOf() ) );
            const HRESULT listHr      = SUCCEEDED( allocatorHr ) ? _device->CreateCommandList( 0, D3D12_COMMAND_LIST_TYPE_DIRECT, outJob._allocator.Get(), nullptr,
                                                                                               IID_PPV_ARGS( outJob._list.GetAddressOf() ) )
                                                                 : allocatorHr;
            if ( FAILED( listHr ) )
            {
                SW_LOG_ERROR_ID( "D3D12.FailedCreateD3D12Transfer", "Failed to create a D3D12 transfer command allocator or list hr=0x%# (DeviceRemoved=0x%#)", static_cast<uint32>( listHr ),
                                 static_cast<uint32>( _device->GetDeviceRemovedReason() ) );
                outJob = D3D12TransferJob{};
                return false;
            }
            outJob._allocator->SetName( L"TransferAllocator" );
            return true;
        }

        // 놀고 있던 작업은 전송 펜스를 지난 것만 돌아온다 — 얼로케이터를 곧바로 Reset 해도 GPU 가 읽지 않는다.
        const HRESULT resetHr = isHrSuccess( outJob._allocator->Reset(), "Reset" ) == false ? E_FAIL : outJob._list->Reset( outJob._allocator.Get(), nullptr );
        if ( FAILED( resetHr ) )
        {
            SW_LOG_ERROR_ID( "D3D12.FailedResetPooledD3D12", "Failed to reset a pooled D3D12 transfer command allocator or list (DeviceRemoved=0x%#)", static_cast<uint32>( _device->GetDeviceRemovedReason() ) );
            outJob = D3D12TransferJob{};
            return false;
        }
        return true;
    }

    uint64 D3D12RHIDevice::submitTransferJob( D3D12TransferJob job, const utf8* pWhere )
    {
        const HRESULT closeHr = job._list->Close();
        if ( FAILED( closeHr ) )
        {
            SW_LOG_ERROR_ID( "D3D12.FailedCloseTransferCommand", "%#: failed to close the transfer command list hr=0x%#", pWhere, static_cast<uint32>( closeHr ) );
            recycleTransferJob( std::move( job ) );
            return 0;
        }

        ScopedLock<mutex> submitLock{ _transferSubmitMutex };
        // 기록하는 사이에 장치를 잃었으면(다른 스레드의 손실 · rhi.removeDevice) 내지 않는다.
        if ( isDeviceLost() )
        {
            SW_LOG_ERROR_ID( "D3D12.D3D12DeviceLostWhile", "%#: the D3D12 device was lost while the transfer was recorded; the transfer is rejected", pWhere );
            recycleTransferJob( std::move( job ) );
            return 0;
        }
        ID3D12CommandList* arrList[] = { job._list.Get() };
        _commandQueue->ExecuteCommandLists( 1, arrList );
        const UINT64  fenceToSignal = _transferFenceValue + 1;
        const HRESULT signalHr      = _commandQueue->Signal( _transferFence.Get(), fenceToSignal );
        if ( FAILED( signalHr ) )
        {
            // 리스트는 이미 큐에 났지만 끝났는지 알 방법이 없다. 작업은 돌려놓지 않고 버린다(재사용하면 실행 중인 얼로케이터를 Reset 할 수 있다).
            if ( _transferSerials.markLost() )
                SW_LOG_ERROR_ID( "D3D12.TransferFenceSignalFailed", "%#: transfer fence Signal failed hr=0x%# (DeviceRemoved=0x%#)", pWhere, static_cast<uint32>( signalHr ),
                                 static_cast<uint32>( _device->GetDeviceRemovedReason() ) );
            return 0;
        }
        _transferFenceValue = fenceToSignal;
        job._fenceValue     = fenceToSignal;
        // 시리얼은 큐에 낸 직후 같은 락 안에서 매긴다 — 락 밖에서 매기면 시리얼 순서가 큐 순서와 어긋나 워터마크가 끝나지 않은 제출을 덮는다(계약 5).
        return _transferFenceQueue.enqueue( std::move( job ) );
    }

    void D3D12RHIDevice::recycleTransferJob( D3D12TransferJob&& job )
    {
        job._fenceValue = 0;
        ScopedLock<mutex> jobLock{ _transferJobMutex };
        _listFreeTransferJob.push_back( std::move( job ) );
    }

    void D3D12RHIDevice::retireCompletedTransfers()
    {
        if ( _transferFence == nullptr )
            return;
        // 잃은 장치의 펜스는 묻지 않는다: 제거된 장치의 펜스는 끝난 값(UINT64_MAX)을 돌려줘 끝나지 않은 전송이 Complete 로 보인다. 워터마크가 멈추고 남은 티켓은 Failed 다.
        if ( _device != nullptr && isDeviceLost() == false )
        {
            const UINT64 completedFence = _transferFence->GetCompletedValue();
            _transferFenceQueue.retireDone(
                [completedFence]( const D3D12TransferJob& job )
            { return job._fenceValue <= completedFence; },
                [this]( D3D12TransferJob& job )
            { recycleTransferJob( std::move( job ) ); } );
        }
        // 워터마크가 지난 전송을 기다리던 해제를 프레임 해제 큐로 넘긴다(그 프레임 펜스 뒤에 놓인다).
        _transferReleaseQueue.tickCompleted( _transferSerials.getWatermark() );
        // 할당기가 단독 구간을 돌려받았으면 그 일회성 버퍼를 지운다(업로드 힙 메모리를 링 하나 크기 넘게 쥐고 있지 않는다).
        destroyIdleSoloBuffer( _uploadRing );
    }

    void D3D12RHIDevice::releaseTextureAfterTransfer( Microsoft::WRL::ComPtr<ID3D12Resource> owned, RHITextureHandle texture, uint64 lastSerial )
    {
        // 전송이 끝난 뒤의 프레임 펜스까지 한 번 더 미뤄 이 텍스처를 그린 프레임도 지나게 한다. 전송과 프레임이 같은 큐여도 해제가 그 공유에 기대지 않게 전송 완료를 직접 본다.
        _transferReleaseQueue.enqueueGPURelease( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, [this, owned, texture]()
        { releaseTrackedResourceDeferred( owned, RHIMemoryKey::makeTexture( texture ) ); } ),
                                                 lastSerial );
    }

    void D3D12RHIDevice::recordTextureTransfer( RHITextureHandle texture, uint64 serial )
    {
        ScopedLock<mutex> lock{ _textureTransferMutex };
        _mapTextureTransferMarks[texture].recordWrite( serial );
    }

    uint64 D3D12RHIDevice::findTextureTransferSerial( RHITextureHandle texture )
    {
        ScopedLock<mutex> lock{ _textureTransferMutex };
        const auto        foundIt = _mapTextureTransferMarks.find( texture );
        return foundIt != _mapTextureTransferMarks.end() ? foundIt->second._lastSerial.load() : 0;
    }

    bool D3D12RHIDevice::isTextureReadyToRegister( RHITextureHandle texture )
    {
        ScopedLock<mutex> lock{ _textureTransferMutex };
        const auto        foundIt = _mapTextureTransferMarks.find( texture );
        return foundIt == _mapTextureTransferMarks.end() || _transferSerials.isTextureReadyToRegister( foundIt->second, texture );
    }

    bool D3D12RHIDevice::isTextureFirstUploadComplete( RHITextureHandle texture )
    {
        ScopedLock<mutex> lock{ _textureTransferMutex };
        const auto        foundIt = _mapTextureTransferMarks.find( texture );
        return foundIt == _mapTextureTransferMarks.end() || _transferSerials.isFirstUploadComplete( foundIt->second );
    }

    void D3D12RHIDevice::eraseTextureTransfer( RHITextureHandle texture )
    {
        ScopedLock<mutex> lock{ _textureTransferMutex };
        _mapTextureTransferMarks.erase( texture );
    }
} // namespace sw
#endif
