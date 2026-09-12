#include "pch.h"

#include "Engine/Graphics/RHI/DX11/D3D11RHISwapChain.h"

#if defined( SW_PLATFORM_WINDOWS )

namespace sw
{
    SW_LOG_CALLER( "D3D11" );

    void D3D11RHISwapChain::attach( IDXGISwapChain* pSwapChain, HWND hWnd, uint32 width, uint32 height, uint32 swapChainFlags )
    {
        _swapChain      = pSwapChain;
        _pHWnd          = hWnd;
        _width          = width;
        _height         = height;
        _swapChainFlags = swapChainFlags;
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

        // 0 = 기존 버퍼 개수 유지. 플래그는 생성 때와 **같아야** 한다 — 티어링 스왑체인을 0 으로
        // 리사이즈하면 그 뒤의 Present( 0, ALLOW_TEARING ) 이 INVALID_CALL 이 된다.
        const HRESULT resizeHr = _swapChain->ResizeBuffers( 0, width, height, DXGI_FORMAT_UNKNOWN, _swapChainFlags );
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
        const bool bAllowTearing = ( _swapChainFlags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING ) != 0;
        const UINT presentFlags  = ( vsync == false && bAllowTearing ) ? DXGI_PRESENT_ALLOW_TEARING : 0u;
        return _swapChain->Present( vsync ? 1 : 0, presentFlags );
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
