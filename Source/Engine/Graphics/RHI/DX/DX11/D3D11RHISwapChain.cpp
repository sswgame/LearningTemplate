#include "pch.h"

#include "Engine/Graphics/RHI/DX/DX11/D3D11RHISwapChain.h"

#include "Engine/Graphics/RHI/DX/DXGIFormat.h"
#include "Engine/Graphics/RHI/DX/DXGITearing.h"
#include "Engine/Graphics/RHI/DX/RHIDxResultCheck.h"

#if defined( SW_PLATFORM_WINDOWS )

namespace sw
{
    bool D3D11RHISwapChain::attach( IDXGISwapChain* pSwapChain, HWND hWnd, uint32 width, uint32 height, uint32 swapChainFlags, RHIFormat sdrFormat )
    {
        // 모든 DX11 스왑체인이 여기를 지나므로 창 연결 끄기도 여기 하나다(DXGITearing.h — Alt+Enter 배타 전체 화면 R-1).
        if ( disableDXGIWindowChanges( pSwapChain, hWnd ) == false )
        {
            SW_LOG_ERROR_ID( "D3D11.WindowAssociationFailed", "IDXGIFactory::MakeWindowAssociation failed - DXGI would take Alt+Enter into exclusive fullscreen where tearing Present fails" );
            return false;
        }
        _swapChain            = pSwapChain;
        _pHWnd                = hWnd;
        _width                = width;
        _height               = height;
        _swapChainFlags       = swapChainFlags;
        _sdrFormat            = sdrFormat;
        _colorSpace           = RHIColorSpace::SRGBNonLinear; // 넘겨받는 스왑체인은 늘 SDR 로 만든다(주 창 · 다른 창) — HDR 은 `setColorSpace` 가 켠다
        _displayOutputSupport = queryDXGIDisplayOutputSupport( pSwapChain );
        return true;
    }

    bool D3D11RHISwapChain::createLike( ID3D11Device* pDevice, const D3D11RHISwapChain& source, HWND hWnd, uint32 width, uint32 height )
    {
        DXGI_SWAP_CHAIN_DESC desc{};
        if ( pDevice == nullptr || source._swapChain == nullptr || isHrSuccess( source._swapChain->GetDesc( &desc ), "IDXGISwapChain::GetDesc" ) == false )
            return false;
        desc.OutputWindow      = hWnd;
        desc.BufferDesc.Width  = width;
        desc.BufferDesc.Height = height;
        desc.BufferDesc.Format = toDXGIFormat( source._sdrFormat ); // 주 창이 HDR 이어도 다른 창(에디터 도구 창)은 SDR 이다 — 그 창의 UI PSO 는 SDR 포맷 하나다

        // 디바이스를 만든 팩터리에 만든다 — 다른 팩터리의 스왑체인은 이 디바이스와 짝이 아니다.
        Microsoft::WRL::ComPtr<IDXGIDevice>    dxgiDevice;
        Microsoft::WRL::ComPtr<IDXGIAdapter>   adapter;
        Microsoft::WRL::ComPtr<IDXGIFactory>   factory;
        Microsoft::WRL::ComPtr<IDXGISwapChain> created;
        const bool                             bCreated = isHrSuccess( pDevice->QueryInterface( IID_PPV_ARGS( dxgiDevice.GetAddressOf() ) ), "QueryInterface(IDXGIDevice)" ) &&
                              isHrSuccess( dxgiDevice->GetAdapter( adapter.GetAddressOf() ), "IDXGIDevice::GetAdapter" ) &&
                              isHrSuccess( adapter->GetParent( IID_PPV_ARGS( factory.GetAddressOf() ) ), "IDXGIAdapter::GetParent" ) &&
                              isHrSuccess( factory->CreateSwapChain( pDevice, &desc, created.GetAddressOf() ), "IDXGIFactory::CreateSwapChain" );
        if ( bCreated == false )
            return false;
        return attach( created.Get(), hWnd, width, height, desc.Flags, source._sdrFormat );
    }

    void D3D11RHISwapChain::shutdown()
    {
        releaseBackBufferRTV();
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

        // 0 은 기존 버퍼 개수 유지를 뜻한다. 플래그는 생성 때와 같아야 한다. 티어링 스왑체인을 0 으로
        // 리사이즈하면 그 뒤의 Present( 0, ALLOW_TEARING ) 이 INVALID_CALL 이 된다.
        const HRESULT resizeHr = _swapChain->ResizeBuffers( 0, width, height, DXGI_FORMAT_UNKNOWN, _swapChainFlags );
        if ( FAILED( resizeHr ) )
        {
            SW_LOG_ERROR_ID( "D3D11.ResizeBuffersFailedHrX", "ResizeBuffers failed hr=0x%#", static_cast<uint32>( resizeHr ) );
            return false;
        }
        _displayOutputSupport = queryDXGIDisplayOutputSupport( _swapChain.Get() );                     // 창을 다른 모니터로 끌어 크기가 바뀌었을 수 있다
        _colorSpace           = reapplyDXGIColorSpace( makeBuffers(), _colorSpace, _displayMetadata ); // ResizeBuffers 가 HDR 메타데이터를 잃을 수 있다
        return true;
    }

    bool D3D11RHISwapChain::setColorSpace( RHIColorSpace colorSpace, const RHIDisplayMetadata& metadata )
    {
        if ( _swapChain == nullptr )
            return false;
        // 포맷만 바꿔 다시 만든다. 실패하면 옛 포맷 · 색 공간(못 하면 SDR)으로 되돌린 값이 온다 — 버퍼 포맷과 기록한 색 공간이 늘 짝이다.
        const RHIColorSpace result   = changeDXGIColorSpace( makeBuffers(), _colorSpace, _displayMetadata, colorSpace, metadata );
        const bool          bChanged = result == colorSpace;
        if ( bChanged )
            _displayMetadata = metadata;
        _colorSpace           = result;
        _displayOutputSupport = queryDXGIDisplayOutputSupport( _swapChain.Get() );
        return bChanged;
    }

    bool D3D11RHISwapChain::setDisplayMetadata( const RHIDisplayMetadata& metadata )
    {
        if ( _colorSpace != RHIColorSpace::HDR10ST2084 )
        {
            _displayMetadata = metadata;
            return true;
        }
        if ( applyDXGIColorSpace( _swapChain.Get(), _colorSpace, metadata ) )
        {
            _displayMetadata = metadata;
            return true;
        }
        (void)applyDXGIColorSpace( _swapChain.Get(), _colorSpace, _displayMetadata ); // 실패는 색 공간을 SDR 로 내렸다 — 버퍼는 HDR10 이라 옛 값으로 다시 알린다
        return false;
    }

    void D3D11RHISwapChain::acquireNextImage( ID3D11Device* pDevice )
    {
        releaseBackBufferRTV();
        if ( _swapChain == nullptr || pDevice == nullptr )
            return;

        Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer = getBackBufferTexture();
        if ( backBuffer == nullptr )
            return;

        // 뷰를 못 만들면 _backBufferRTV 가 비어 있고 백버퍼 패스가 대상 없이 열린다 — 오류로 남긴다(장치 분실이면 다음 Present 가 분실로 처리한다).
        if ( isHrSuccess( pDevice->CreateRenderTargetView( backBuffer.Get(), nullptr, _backBufferRTV.GetAddressOf() ), "CreateRenderTargetView (back buffer)" ) == false )
            _backBufferRTV.Reset();
    }

    void D3D11RHISwapChain::releaseBackBufferRTV()
    {
        _backBufferRTV.Reset();
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
            SW_LOG_ERROR_ID( "D3D11.GetBufferFailedHrX", "GetBuffer failed hr=0x%#", static_cast<uint32>( hr ) );
            backBuffer.Reset();
        }
        return backBuffer;
    }
} // namespace sw
#endif
