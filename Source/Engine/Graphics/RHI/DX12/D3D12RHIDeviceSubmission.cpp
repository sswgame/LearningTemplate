#include "pch.h"

#include "Engine/Graphics/RHI/DX12/D3D12RHICommandContext.h"
#include "Engine/Graphics/RHI/DX12/D3D12RHICommandList.h"
#include "Engine/Graphics/RHI/DX12/D3D12RHIDevice.h"
#include "Engine/Graphics/RHI/DX12/D3D12RHIResource.h"
#include "Engine/Graphics/RHI/DX12/D3D12RHISwapChain.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Engine/Common/EnginePlatformHeaders.h"
    #include "Engine/Config/EngineData.h"
    #include "Engine/Graphics/RHI/DX/RHIDxgiFormat.h"
    #include "Engine/Graphics/Shader/ShaderCache.h"

namespace sw
{
    SW_LOG_CALLER( "D3D12" );

    void D3D12RHIDevice::waitIdle()
    {
        waitForPreviousFrame();
        _releaseQueue.flushAll();
    }

    unique_ptr<IRHICommandList> D3D12RHIDevice::createCommandList()
    {
        unique_ptr<D3D12RHICommandList> list = make_unique<D3D12RHICommandList>( this );
        if ( list->isValid() == false )
        {
            if ( _bDeviceRemovedLogged == 0 )
                SW_LOG_ERROR( "D3D12RHIDevice::createCommandList: 네이티브 커맨드 리스트 생성 실패." );
            return nullptr;
        }
        return list;
    }

    ID3D12GraphicsCommandList* D3D12RHIDevice::beginNextFrameSegment()
    {
        D3D12CommandListEntry entry = acquireCommandListEntry();
        if ( entry._list == nullptr || entry._allocator == nullptr )
            return nullptr;

        // 풀에서 나온 쌍은 이미 펜스를 통과했으므로 곧바로 Reset 해도 된다.
        if ( FAILED( entry._allocator->Reset() ) || FAILED( entry._list->Reset( entry._allocator.Get(), nullptr ) ) )
            return nullptr;

        if ( _cbvHeap != nullptr )
        {
            ID3D12DescriptorHeap* heaps[] = { _cbvHeap.Get() };
            entry._list->SetDescriptorHeaps( 1, heaps );
        }

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

        ID3D12CommandList* arr[] = { pList };
        _commandQueue->ExecuteCommandLists( 1, arr );
    }

    void D3D12RHIDevice::executeCommandList( IRHICommandList* pCmdList )
    {
        auto* pNative = static_cast<D3D12RHICommandList*>( pCmdList );
        if ( pNative == nullptr || _commandQueue == nullptr || _activeFrameList == nullptr )
            return;

        ID3D12GraphicsCommandList* pList = pNative->getNativeCommandList();
        if ( pList == nullptr )
            return;

        // 예전엔 여기서 곧바로 ExecuteCommandLists 를 불렀다. 그러면 프레임 스트림(디바이스가
        // 소유한 리스트)은 endFrame 에서 한 번에 제출되므로, 그래프보다 **먼저** 기록한 것까지
        // 그래프 뒤에 실행됐다 — 오프스크린 경로의 게임 RT 클리어가 대표적이다.
        // Vulkan(S4)과 같이 스트림을 이 지점에서 자르고 순서대로 모아 endFrame 에서 한 번에
        // 제출한다. 같은 큐의 제출 순서가 곧 실행 순서다.
        _activeFrameList->Close();
        _listPendingSubmit.push_back( _activeFrameList );
        _listPendingSubmit.push_back( pList );

        // 즉시 모드에서도 잘라 담은 순서 그대로 내보내므로 실행 순서는 같다 — 제출 시점만 앞당긴다.
        if ( _bImmediateSubmit && _listPendingSubmit.empty() == false )
        {
            _commandQueue->ExecuteCommandLists( static_cast<UINT>( _listPendingSubmit.size() ), _listPendingSubmit.data() );
            _listPendingSubmit.clear();
        }

        ID3D12GraphicsCommandList* pNextSegment = beginNextFrameSegment();
        _activeFrameList                        = pNextSegment;
        if ( pNextSegment == nullptr )
        {
            _frameStreamState._bRecording = 0;
            return;
        }

        // 새 리스트라 바인딩 캐시가 무효다. 뷰포트/시저도 다시 깔아준다.
        _frameStreamState             = D3D12RecordingState{};
        _frameStreamState._bRecording = 1;
        _frameStreamContext->rebindCommandList( pNextSegment );

        D3D12_VIEWPORT vp{};
        vp.Width    = static_cast<float32>( _width );
        vp.Height   = static_cast<float32>( _height );
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        pNextSegment->RSSetViewports( 1, &vp );

        D3D12_RECT scissor{ 0, 0, static_cast<LONG>( _width ), static_cast<LONG>( _height ) };
        pNextSegment->RSSetScissorRects( 1, &scissor );
    }

    void D3D12RHIDevice::waitForPreviousFrame()
    {
        if ( _commandQueue == nullptr || _fence == nullptr )
            return;

        if ( _device != nullptr && FAILED( _device->GetDeviceRemovedReason() ) )
            return;

        const UINT64  fenceToWait = _fenceValue;
        const HRESULT signalHr    = _commandQueue->Signal( _fence.Get(), fenceToWait );
        _fenceValue++;

        if ( FAILED( signalHr ) )
        {
            SW_LOG_ERROR( "Fence Signal failed hr=0x%# (DeviceRemoved=0x%#)",
                          static_cast<uint32>( signalHr ),
                          static_cast<uint32>( _device ? _device->GetDeviceRemovedReason() : S_OK ) );
            return;
        }

        if ( _fence->GetCompletedValue() < fenceToWait )
        {
            if ( _fenceEvent == nullptr )
                return;

            _fence->SetEventOnCompletion( fenceToWait, _fenceEvent );
            const DWORD waitResult = WaitForSingleObject( _fenceEvent, 2000 );
            if ( waitResult != WAIT_OBJECT_0 )
                SW_LOG_ERROR( "Fence wait timed out (result=%#, fence=%#)", static_cast<uint32>( waitResult ), static_cast<uint32>( fenceToWait ) );
        }

        if ( _swapChain != nullptr && ( _device == nullptr || SUCCEEDED( _device->GetDeviceRemovedReason() ) ) )
            _frameIndex = _swapChain->GetCurrentBackBufferIndex();
    }

    void D3D12RHIDevice::waitForRingSlot()
    {
        if ( _fence == nullptr )
            return;

        if ( _device != nullptr && FAILED( _device->GetDeviceRemovedReason() ) )
            return;

        const uint64 completed = _fence->GetCompletedValue();
        if ( _frameRing.beginFrame( completed ) )
            return;

        const uint32 nextIndex = ( _frameRing.currentIndex() + 1 ) % constant::kMaxFrameCountInFlight;
        const uint64 waitValue = _frameRing.getFenceValue( nextIndex );
        if ( _fence->GetCompletedValue() < waitValue && _fenceEvent != nullptr )
        {
            _fence->SetEventOnCompletion( waitValue, _fenceEvent );
            WaitForSingleObject( _fenceEvent, 2000 );
        }
        _frameRing.beginFrame( _fence->GetCompletedValue() );
    }

    void D3D12RHIDevice::signalCurrentFrame()
    {
        if ( _commandQueue == nullptr || _fence == nullptr )
            return;

        if ( _device != nullptr && FAILED( _device->GetDeviceRemovedReason() ) )
            return;

        const UINT64  fenceToSignal = _fenceValue;
        const HRESULT signalHr      = _commandQueue->Signal( _fence.Get(), fenceToSignal );
        _fenceValue++;
        if ( FAILED( signalHr ) )
        {
            SW_LOG_ERROR( "Fence Signal failed hr=0x%#", static_cast<uint32>( signalHr ) );
            return;
        }
        _frameRing.setFenceValue( _frameRing.currentIndex(), fenceToSignal );
        _releaseQueue.tickCompleted( _fence->GetCompletedValue() );
    }

    ID3D12CommandAllocator* D3D12RHIDevice::currentAllocator()
    {
        return _arrCommandAllocator[_frameRing.currentIndex()].Get();
    }

    D3D12CommandListEntry D3D12RHIDevice::acquireCommandListEntry()
    {
        {
            std::scoped_lock<mutex> lock{ _cmdListPoolMutex };
            if ( _listFreeCmdListEntry.empty() == false )
            {
                D3D12CommandListEntry entry = std::move( _listFreeCmdListEntry.back() );
                _listFreeCmdListEntry.pop_back();
                return entry;
            }
        }

        // 풀이 비었으면 새로 만든다. 생성 직후의 리스트는 열린 상태라 바로 Close 해 둔다
        // (beginCommandList 가 Reset 으로 다시 연다).
        D3D12CommandListEntry entry;
        if ( _device == nullptr )
            return entry;

        // 예전엔 HRESULT 를 버리고 빈 엔트리만 돌려줬다. 호출부는 "생성 실패" 한 줄만 남기므로
        // 원인(메모리 부족인지 디바이스 제거인지)을 알 방법이 없었다.
        const HRESULT allocHr = _device->CreateCommandAllocator( D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS( entry._allocator.GetAddressOf() ) );
        if ( FAILED( allocHr ) )
        {
            if ( _bDeviceRemovedLogged == 0 )
                SW_LOG_ERROR( "acquireCommandListEntry: CreateCommandAllocator failed hr=0x%# removed=0x%# pooled=%#",
                              static_cast<uint32>( allocHr ),
                              static_cast<uint32>( _device->GetDeviceRemovedReason() ),
                              static_cast<uint32>( _cmdListEntryCreated ) );
            flushDebugMessages( "acquireCommandListEntry" );
            return D3D12CommandListEntry{};
        }
        const HRESULT listHr = _device->CreateCommandList( 0, D3D12_COMMAND_LIST_TYPE_DIRECT, entry._allocator.Get(), nullptr,
                                                           IID_PPV_ARGS( entry._list.GetAddressOf() ) );
        if ( FAILED( listHr ) )
        {
            if ( _bDeviceRemovedLogged == 0 )
                SW_LOG_ERROR( "acquireCommandListEntry: CreateCommandList failed hr=0x%# removed=0x%# pooled=%#",
                              static_cast<uint32>( listHr ),
                              static_cast<uint32>( _device->GetDeviceRemovedReason() ),
                              static_cast<uint32>( _cmdListEntryCreated ) );
            return D3D12CommandListEntry{};
        }
        ++_cmdListEntryCreated;
        entry._list->Close();
        return entry;
    }

    void D3D12RHIDevice::recycleCommandListEntryDeferred( D3D12CommandListEntry entry )
    {
        if ( entry._list == nullptr || entry._allocator == nullptr )
            return;

        // 제출 직후 리스트 객체가 사라져도 GPU 는 아직 이 얼로케이터의 커맨드 메모리를 읽고 있다.
        // 해제 큐에 실어 현재 펜스가 통과한 뒤에야 재사용 풀로 돌려보낸다 — 그래야 다음 사용자가
        // Reset 해도 안전하다.
        auto recycleCb = [this, entry]()
        {
            std::scoped_lock<mutex> lock{ _cmdListPoolMutex };
            _listFreeCmdListEntry.push_back( entry );
        };
        _releaseQueue.enqueueGpuRelease( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, recycleCb ), _fenceValue );
    }

    ID3D12CommandAllocator* D3D12RHIDevice::currentFrameCmdAllocator()
    {
        return _arrFrameCmdAllocator[_frameRing.currentIndex()].Get();
    }

} // namespace sw
#endif
