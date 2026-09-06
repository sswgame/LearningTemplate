#include "pch.h"

#include "Engine/Graphics/RHI/DX12/D3D12RHISwapChain.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Engine/Graphics/RHI/DX/RHIDxgiFormat.h"

namespace sw
{
    SW_LOG_CALLER( "D3D12" );

    bool D3D12RHISwapChain::initialize( IDXGIFactory4* pFactory, ID3D12CommandQueue* pQueue, const RHISwapChainDesc& desc )
    {
        _pHWnd       = static_cast<HWND>( desc._pWindowHandle );
        _width       = desc._width;
        _height      = desc._height;
        _bufferCount = desc._bufferCount;
        _state       = D3D12_RESOURCE_STATE_PRESENT;

        // 창이 없으면 네이티브 스왑체인도 없다. 크기만 기억해 두면 오프스크린 경로는 그대로 돈다.
        if ( _pHWnd == nullptr || _width == 0 || _height == 0 )
            return true;

        if ( pFactory == nullptr || pQueue == nullptr )
            return false;

        DXGI_SWAP_CHAIN_DESC1 scDesc{};
        scDesc.BufferCount      = _bufferCount;
        scDesc.Width            = _width;
        scDesc.Height           = _height;
        scDesc.Format           = toDxgiFormat( desc._format );
        scDesc.BufferUsage      = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        scDesc.SwapEffect       = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        scDesc.SampleDesc.Count = 1;

        Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain1;
        if ( FAILED( pFactory->CreateSwapChainForHwnd( pQueue, _pHWnd, &scDesc, nullptr, nullptr, swapChain1.GetAddressOf() ) ) )
            return false;

        swapChain1.As( &_swapChain );
        acquireNextImage();
        return true;
    }

    void D3D12RHISwapChain::shutdown()
    {
        releaseBackBuffers();
        _swapChain.Reset();
        _pHWnd           = nullptr;
        _width           = 0;
        _height          = 0;
        _backBufferIndex = 0;
        _state           = D3D12_RESOURCE_STATE_PRESENT;
    }

    void D3D12RHISwapChain::createBackBuffers( ID3D12Device* pDevice, ID3D12DescriptorHeap* pRtvHeap, uint32 rtvDescriptorSize )
    {
        if ( _swapChain == nullptr || pDevice == nullptr || pRtvHeap == nullptr )
            return;

        _listBackBuffer.resize( _bufferCount );
        _listBackBufferRtv.resize( _bufferCount );
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle( pRtvHeap->GetCPUDescriptorHandleForHeapStart() );

        for ( uint32 bufferIndex = 0; bufferIndex < _bufferCount; ++bufferIndex )
        {
            const HRESULT getHr = _swapChain->GetBuffer( bufferIndex, IID_PPV_ARGS( _listBackBuffer[bufferIndex].GetAddressOf() ) );
            if ( FAILED( getHr ) )
            {
                SW_LOG_ERROR( "GetBuffer(%#) failed hr=0x%#", bufferIndex, static_cast<uint32>( getHr ) );
                releaseBackBuffers();
                return;
            }
            pDevice->CreateRenderTargetView( _listBackBuffer[bufferIndex].Get(), nullptr, rtvHandle );
            _listBackBufferRtv[bufferIndex] = rtvHandle;
            rtvHandle.ptr += rtvDescriptorSize;
        }
    }

    void D3D12RHISwapChain::releaseBackBuffers()
    {
        for ( Microsoft::WRL::ComPtr<ID3D12Resource>& backBuffer : _listBackBuffer )
        {
            backBuffer.Reset();
        }
        _listBackBuffer.clear();
        _listBackBufferRtv.clear();
    }

    bool D3D12RHISwapChain::resize( uint32 width, uint32 height )
    {
        if ( _swapChain == nullptr )
            return false;

        _width  = width;
        _height = height;

        const HRESULT resizeHr = _swapChain->ResizeBuffers( _bufferCount, width, height, DXGI_FORMAT_UNKNOWN, 0 );
        if ( FAILED( resizeHr ) )
        {
            SW_LOG_ERROR( "ResizeBuffers failed hr=0x%#", static_cast<uint32>( resizeHr ) );
            return false;
        }
        return true;
    }

    void D3D12RHISwapChain::acquireNextImage()
    {
        if ( _swapChain == nullptr )
            return;
        _backBufferIndex = _swapChain->GetCurrentBackBufferIndex();
    }

    HRESULT D3D12RHISwapChain::present( bool vsync )
    {
        if ( _swapChain == nullptr )
            return S_OK;
        return _swapChain->Present( vsync ? 1 : 0, 0 );
    }

    void D3D12RHISwapChain::transitionTo( ID3D12GraphicsCommandList* pCmdList, D3D12_RESOURCE_STATES stateAfter )
    {
        if ( pCmdList == nullptr || _state == stateAfter || isBackBufferReady() == false )
            return;

        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource   = _listBackBuffer[_backBufferIndex].Get();
        barrier.Transition.StateBefore = _state;
        barrier.Transition.StateAfter  = stateAfter;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        pCmdList->ResourceBarrier( 1, &barrier );
        _state = stateAfter;
    }

    ID3D12Resource* D3D12RHISwapChain::getCurrentBackBuffer() const
    {
        if ( isBackBufferReady() == false )
            return nullptr;
        return _listBackBuffer[_backBufferIndex].Get();
    }

    D3D12_CPU_DESCRIPTOR_HANDLE D3D12RHISwapChain::getCurrentRtv() const
    {
        if ( _backBufferIndex >= _listBackBufferRtv.size() )
            return D3D12_CPU_DESCRIPTOR_HANDLE{ 0 };
        return _listBackBufferRtv[_backBufferIndex];
    }
} // namespace sw
#endif
