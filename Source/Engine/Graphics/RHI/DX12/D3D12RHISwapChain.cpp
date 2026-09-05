#include "pch.h"

#include "Engine/Graphics/RHI/DX12/D3D12RHISwapChain.h"

#include "Engine/Graphics/RHI/DX12/D3D12RHIDevice.h"

#if defined( SW_PLATFORM_WINDOWS )
namespace sw
{
    SW_LOG_CALLER( "D3D12" );

    void* D3D12RHISwapChain::getNativeSwapChain() const
    {
        return _pDevice->getNativeSwapChain();
    }

    void D3D12RHISwapChain::resize( uint32 width, uint32 height )
    {
        if ( _pDevice->_swapChain == nullptr || ( width == 0 && height == 0 ) )
            return;
        _pDevice->_width  = width;
        _pDevice->_height = height;

        if ( _pDevice->_frameStreamState._bRecording != 0 && _pDevice->_commandList != nullptr )
        {
            _pDevice->_commandList->Close();
            ID3D12CommandList* arrCommandList[] = { _pDevice->_commandList.Get() };
            if ( _pDevice->_commandQueue != nullptr )
                _pDevice->_commandQueue->ExecuteCommandLists( 1, arrCommandList );
            _pDevice->_frameStreamState._bRecording = 0;
        }

        _pDevice->waitForPreviousFrame();
        _pDevice->cleanupRenderTargets();
        const HRESULT resizeHr = _pDevice->_swapChain->ResizeBuffers( _pDevice->_bufferCount, width, height, DXGI_FORMAT_UNKNOWN, 0 );
        if ( FAILED( resizeHr ) )
        {
            SW_LOG_ERROR( "ResizeBuffers failed hr=0x%#", static_cast<uint32>( resizeHr ) );
            return;
        }
        _pDevice->createRenderTargets();
        _pDevice->_frameIndex = _pDevice->_swapChain->GetCurrentBackBufferIndex();
    }

    void D3D12RHISwapChain::beginFrame( const float4& clearColor )
    {
        if ( _pDevice->_frameStreamState._bRecording == 0 )
        {
            _pDevice->waitForRingSlot();
            ID3D12CommandAllocator* pAllocator = _pDevice->currentAllocator();
            if ( pAllocator == nullptr || _pDevice->_commandList == nullptr )
                return;
            // 디바이스가 제거된 상태에서는 Reset()이 실패해 커맨드 리스트가 여전히 closed로 남는다.
            // 그걸 무시하고 _bRecording=1로 넘어가면 이후의 모든 커맨드리스트 호출이 "closed command
            // list" 에러를 매번 뱉으며 프레임마다 반복 폭주하게 된다 — 실패 시 이번 프레임을 스킵한다.
            if ( FAILED( pAllocator->Reset() ) || FAILED( _pDevice->_commandList->Reset( pAllocator, nullptr ) ) )
                return;
            _pDevice->_frameStreamState._bRecording = 1;
            if ( _pDevice->_cbvHeap != nullptr )
            {
                ID3D12DescriptorHeap* heaps[] = { _pDevice->_cbvHeap.Get() };
                _pDevice->_commandList->SetDescriptorHeaps( 1, heaps );
            }
        }
        _pDevice->_frameIndex = _pDevice->_swapChain->GetCurrentBackBufferIndex();

        // resize()가 ResizeBuffers 실패로 조기 반환하면 cleanupRenderTargets()만 실행되고
        // createRenderTargets()는 못 돌아 _listRenderTarget이 비어있는 채로 남는다 — 그 상태에서
        // 아래 인덱싱을 그대로 하면 범위 밖 접근(SW_ASSERT 트리거, 디버거 없으면 크래시)이 된다.
        // 디바이스가 이미 맛이 간 프레임이므로 이번 프레임은 조용히 건너뛴다.
        if ( _pDevice->_frameIndex >= _pDevice->_listRenderTarget.size() )
            return;

        // 백버퍼 바인딩(RENDER_TARGET 배리어 + OMSetRenderTargets + Clear)은 여기서 하지 않는다 —
        // beginFrame 은 프레임 수명주기 전용이고, 백버퍼 타깃팅은 beginRenderPass(핸들 0) 가 배리어까지
        // 포함해 명시적으로 한다 (docs/05_RHI_FrameContract.md S2). 뷰포트/시저는 기본값으로 남긴다.
        (void)clearColor;

        constexpr float32 kDefaultViewportX        = 0.0f;
        constexpr float32 kDefaultViewportY        = 0.0f;
        constexpr float32 kDefaultViewportMinDepth = 0.0f;
        constexpr float32 kDefaultViewportMaxDepth = 1.0f;

        D3D12_VIEWPORT vp{};
        vp.Width    = static_cast<float32>( _pDevice->_width );
        vp.Height   = static_cast<float32>( _pDevice->_height );
        vp.MinDepth = kDefaultViewportMinDepth;
        vp.MaxDepth = kDefaultViewportMaxDepth;
        vp.TopLeftX = kDefaultViewportX;
        vp.TopLeftY = kDefaultViewportY;
        _pDevice->_commandList->RSSetViewports( 1, &vp );

        D3D12_RECT scissorRect{ 0, 0, static_cast<LONG>( _pDevice->_width ), static_cast<LONG>( _pDevice->_height ) };
        _pDevice->_commandList->RSSetScissorRects( 1, &scissorRect );
    }

    void D3D12RHISwapChain::endFrame( bool vsync, bool bPresent )
    {
        if ( bPresent && _pDevice->_swapchainState != D3D12_RESOURCE_STATE_PRESENT && _pDevice->_listRenderTarget.empty() == false )
        {
            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource   = _pDevice->_listRenderTarget[_pDevice->_frameIndex].Get();
            barrier.Transition.StateBefore = _pDevice->_swapchainState;
            barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            _pDevice->_commandList->ResourceBarrier( 1, &barrier );
            _pDevice->_swapchainState = D3D12_RESOURCE_STATE_PRESENT;
        }

        if ( _pDevice->_frameStreamState._bRecording != 0 && _pDevice->_commandList != nullptr )
        {
            _pDevice->_commandList->Close();
            ID3D12CommandList* ppCommandLists[] = { _pDevice->_commandList.Get() };
            _pDevice->_commandQueue->ExecuteCommandLists( 1, ppCommandLists );
            _pDevice->_frameStreamState._bRecording = 0;
        }

        if ( bPresent && _pDevice->_swapChain != nullptr )
        {
            const HRESULT presentHr = _pDevice->_swapChain->Present( vsync ? 1 : 0, 0 );
            if ( FAILED( presentHr ) )
            {
                [[maybe_unused]] const HRESULT removed = _pDevice->_device->GetDeviceRemovedReason();
                // 디바이스 제거는 자동 복구가 없어서 한 번 일어나면 이후 매 프레임 여기로 들어온다 —
                // 첫 발견 때만 로그를 남기고 그 뒤로는 조용히 스킵해 로그 폭주를 막는다.
                if ( _pDevice->_bDeviceRemovedLogged == 0 )
                {
                    // %# 는 10진 출력 — DXGI_ERROR_DEVICE_HUNG == 0x887A0005 == 2289696773
                    SW_LOG_ERROR( "Present failed hr=%# (0x887A0005=DEVICE_HUNG), DeviceRemovedReason=%#",
                                  static_cast<uint32>( presentHr ), static_cast<uint32>( removed ) );
                }
                _pDevice->flushDebugMessages( "after Present" );
            }
        }
        _pDevice->signalCurrentFrame();
        if ( _pDevice->_swapChain != nullptr )
            _pDevice->_frameIndex = _pDevice->_swapChain->GetCurrentBackBufferIndex();
        if ( bPresent )
            _pDevice->_swapchainState = D3D12_RESOURCE_STATE_PRESENT;
    }

} // namespace sw
#endif
