#include "pch.h"

#include "Engine/Graphics/RHI/DX11/D3D11RHISwapChain.h"

#if defined( SW_PLATFORM_WINDOWS )

namespace sw
{
    SW_LOG_CALLER( "D3D11" );

    void D3D11RHISwapChain::attach( IDXGISwapChain* pSwapChain, HWND hWnd, uint32 width, uint32 height )
    {
        _swapChain = pSwapChain;
        _pHWnd     = hWnd;
        _width     = width;
        _height    = height;
    }

    void D3D11RHISwapChain::shutdown()
    {
        releaseBackBufferRtv();
        _swapChain.Reset();
        _pHWnd  = nullptr;
        _width  = 0;
        _height = 0;
    }

    bool D3D11RHISwapChain::resize( uint32 width, uint32 height )
    {
        if ( _swapChain == nullptr )
            return false;

        _width  = width;
        _height = height;

        // 0 = 기존 버퍼 개수 유지.
        const HRESULT resizeHr = _swapChain->ResizeBuffers( 0, width, height, DXGI_FORMAT_UNKNOWN, 0 );
        if ( FAILED( resizeHr ) )
        {
            SW_LOG_ERROR( "ResizeBuffers failed hr=0x%#", static_cast<uint32>( resizeHr ) );
            return false;
        }
        return true;
    }

    void D3D11RHISwapChain::acquireNextImage( ID3D11Device* pDevice )
    {
        releaseBackBufferRtv();
        if ( _swapChain == nullptr || pDevice == nullptr )
            return;

        Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer = getBackBufferTexture();
        if ( backBuffer == nullptr )
            return;

        pDevice->CreateRenderTargetView( backBuffer.Get(), nullptr, _backBufferRtv.GetAddressOf() );
    }

    void D3D11RHISwapChain::releaseBackBufferRtv()
    {
        _backBufferRtv.Reset();
    }

    HRESULT D3D11RHISwapChain::present( bool vsync )
    {
        if ( _swapChain == nullptr )
            return S_OK;
        return _swapChain->Present( vsync ? 1 : 0, 0 );
    }

    Microsoft::WRL::ComPtr<ID3D11Texture2D> D3D11RHISwapChain::getBackBufferTexture() const
    {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
        if ( _swapChain == nullptr )
            return backBuffer;

        const HRESULT hr = _swapChain->GetBuffer( 0, IID_PPV_ARGS( backBuffer.GetAddressOf() ) );
        if ( FAILED( hr ) )
        {
            SW_LOG_ERROR( "GetBuffer failed hr=0x%#", static_cast<uint32>( hr ) );
            backBuffer.Reset();
        }
        return backBuffer;
    }
} // namespace sw
#endif
