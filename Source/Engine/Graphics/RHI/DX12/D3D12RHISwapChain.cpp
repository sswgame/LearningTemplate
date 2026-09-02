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

        if ( _pDevice->_bRecording != 0 && _pDevice->_commandList != nullptr )
        {
            _pDevice->_commandList->Close();
            ID3D12CommandList* arrCommandList[] = { _pDevice->_commandList.Get() };
            if ( _pDevice->_commandQueue != nullptr )
                _pDevice->_commandQueue->ExecuteCommandLists( 1, arrCommandList );
            _pDevice->_bRecording = 0;
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
        if ( _pDevice->_bRecording == 0 )
        {
            _pDevice->waitForRingSlot();
            ID3D12CommandAllocator* pAllocator = _pDevice->currentAllocator();
            if ( pAllocator == nullptr || _pDevice->_commandList == nullptr )
                return;
            pAllocator->Reset();
            _pDevice->_commandList->Reset( pAllocator, nullptr );
            _pDevice->_bRecording = 1;
            if ( _pDevice->_cbvHeap != nullptr )
            {
                ID3D12DescriptorHeap* heaps[] = { _pDevice->_cbvHeap.Get() };
                _pDevice->_commandList->SetDescriptorHeaps( 1, heaps );
            }
        }
        _pDevice->_frameIndex = _pDevice->_swapChain->GetCurrentBackBufferIndex();

        if ( _pDevice->_swapchainState != D3D12_RESOURCE_STATE_RENDER_TARGET )
        {
            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource   = _pDevice->_listRenderTarget[_pDevice->_frameIndex].Get();
            barrier.Transition.StateBefore = _pDevice->_swapchainState;
            barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            _pDevice->_commandList->ResourceBarrier( 1, &barrier );
            _pDevice->_swapchainState = D3D12_RESOURCE_STATE_RENDER_TARGET;
        }

        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle( _pDevice->_rtvHeap->GetCPUDescriptorHandleForHeapStart() );
        rtvHandle.ptr += ( _pDevice->_frameIndex * _pDevice->_rtvDescriptorSize );

        _pDevice->_commandList->OMSetRenderTargets( 1, &rtvHandle, FALSE, nullptr );
        _pDevice->_commandList->ClearRenderTargetView( rtvHandle, &clearColor._x, 0, nullptr );

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

        if ( _pDevice->_bRecording != 0 && _pDevice->_commandList != nullptr )
        {
            _pDevice->_commandList->Close();
            ID3D12CommandList* ppCommandLists[] = { _pDevice->_commandList.Get() };
            _pDevice->_commandQueue->ExecuteCommandLists( 1, ppCommandLists );
            _pDevice->_bRecording = 0;
        }

        if ( bPresent && _pDevice->_swapChain != nullptr )
        {
            const HRESULT presentHr = _pDevice->_swapChain->Present( vsync ? 1 : 0, 0 );
            if ( FAILED( presentHr ) )
            {
                [[maybe_unused]] const HRESULT removed = _pDevice->_device->GetDeviceRemovedReason();
                // %# 는 10진 출력 — DXGI_ERROR_DEVICE_HUNG == 0x887A0005 == 2289696773
                SW_LOG_ERROR( "Present failed hr=%# (0x887A0005=DEVICE_HUNG), DeviceRemovedReason=%#",
                              static_cast<uint32>( presentHr ), static_cast<uint32>( removed ) );
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
