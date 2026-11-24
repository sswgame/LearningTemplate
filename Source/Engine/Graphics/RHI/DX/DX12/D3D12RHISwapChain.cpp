#include "pch.h"

#include "Engine/Graphics/RHI/DX/DX12/D3D12RHISwapChain.h"

#include "Core/Log/ScopedDeferredLog.h"

#include "Engine/Graphics/RHI/DX/RHIDxResultCheck.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Engine/Graphics/RHI/DX/DXGIFormat.h"
    #include "Engine/Graphics/RHI/DX/DXGITearing.h"

namespace sw
{
    D3D12RHISwapChain::D3D12RHISwapChain()
        : _swapChain{}
        , _renderThread{}
        , _listBackBuffer{}
        , _listBackBufferRTV{}
        , _ownedRTVHeap{}
        , _pBarrierWatcher{ nullptr }
        , _pHWnd{ nullptr }
        , _width{ 0 }
        , _height{ 0 }
        , _bufferCount{ 2 }
        , _backBufferIndex{ 0 }
        , _sdrFormat{ constant::render::kBackBufferFormat }
        , _colorSpace{ RHIColorSpace::SRGBNonLinear }
        , _displayOutputSupport{}
        , _displayMetadata{}
        , _swapChainFlags{ 0 }
        , _bAllowTearing{ SW_FALSE }
        , _stateMutex{}
        , _state{ D3D12_RESOURCE_STATE_PRESENT }
    {
    }

    bool D3D12RHISwapChain::initialize( IDXGIFactory4* pFactory, ID3D12CommandQueue* pQueue, const RHISwapChainDesc& desc )
    {
        // 이 백엔드는 Win32 전용이다 — 핸들이 Win32 표면의 것이 아니면 HWND 로 읽지 않고(창 없음으로 대신하지도 않고) 오류로 멈춘다.
        void* pWindowHandle{ nullptr };
        if ( desc.tryGetWin32Handle( pWindowHandle ) == false )
        {
            SW_LOG_ERROR_ID( "D3D12.D3D12SwapChainNeeds", "The D3D12 swap chain needs a Win32 surface" );
            return false;
        }
        _pHWnd       = static_cast<HWND>( pWindowHandle );
        _width       = desc._width;
        _height      = desc._height;
        _bufferCount = desc._bufferCount;
        _sdrFormat   = desc._sdrFormat;
        _colorSpace  = desc._colorSpace;
        _state       = D3D12_RESOURCE_STATE_PRESENT;

        // 창이 없으면 네이티브 스왑체인도 없다. 크기만 기억해 두면 오프스크린 경로는 그대로 동작한다.
        const bool bNoNativeSwapChain = _pHWnd == nullptr || _width == 0 || _height == 0;
        if ( bNoNativeSwapChain )
            return true;

        if ( pFactory == nullptr || pQueue == nullptr )
            return false;

        // VSync 를 끄려면 티어링 허용 스왑체인이어야 한다. 동기화 간격 0 만으로는 DWM 합성이
        // vblank 에 맞춰 넘겨 주므로 화면 주사율에 그대로 붙는다(RHIDxgiTearing.h). VSync 는 실행 중에도 바뀌므로(`IRHIDevice::setVSync`)
        // 지원하면 항상 티어링 허용으로 만든다. 켜진 VSync 는 Present( 1, 0 ) 이라 이 플래그와 상관없다.
        _bAllowTearing  = queryDXGIAllowTearing();
        _swapChainFlags = _bAllowTearing ? static_cast<uint32>( DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING ) : 0u;

        DXGI_SWAP_CHAIN_DESC1 scDesc{};
        scDesc.BufferCount      = _bufferCount;
        scDesc.Width            = _width;
        scDesc.Height           = _height;
        scDesc.Format           = toDXGIFormat( desc.getFormat() );
        scDesc.BufferUsage      = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        scDesc.SwapEffect       = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        scDesc.SampleDesc.Count = 1;
        scDesc.Flags            = _swapChainFlags;

        Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain1;
        if ( isHrSuccess( pFactory->CreateSwapChainForHwnd( pQueue, _pHWnd, &scDesc, nullptr, nullptr, swapChain1.GetAddressOf() ), "CreateSwapChainForHwnd" ) == false )
            return false;

        if ( disableDXGIWindowChanges( swapChain1.Get(), _pHWnd ) == false )
        {
            SW_LOG_ERROR_ID( "D3D12.WindowAssociationFailed", "IDXGIFactory::MakeWindowAssociation failed - DXGI would take Alt+Enter into exclusive fullscreen where tearing Present fails" );
            return false;
        }
        swapChain1.As( &_swapChain );
        _displayOutputSupport = queryDXGIDisplayOutputSupport( _swapChain.Get() );
        // SDR 은 DXGI 의 기본 색 공간이라 알릴 것이 없다. HDR 로 만드는 창은 만들 때 알린다(마스터링 값은 기본 — 주 창은 `setColorSpace` 가 실제 값으로 다시 알린다).
        if ( _colorSpace != RHIColorSpace::SRGBNonLinear && applyDXGIColorSpace( _swapChain.Get(), _colorSpace, RHIDisplayMetadata{} ) == false )
            return false;
        acquireNextImage();
        return true;
    }

    bool D3D12RHISwapChain::createOwnedRTVHeap( ID3D12Device* pDevice )
    {
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
        heapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        heapDesc.NumDescriptors = _bufferCount;
        return pDevice != nullptr && isHrSuccess( pDevice->CreateDescriptorHeap( &heapDesc, IID_PPV_ARGS( _ownedRTVHeap.GetAddressOf() ) ), "CreateDescriptorHeap (window RTV)" );
    }

    void D3D12RHISwapChain::shutdown()
    {
        _renderThread.assumeHeld(); // 렌더 스레드의 프레임 진행(그 병렬 기록 태스크 포함)에서, 또는 렌더 스레드를 세운 뒤(초기화 · 종료 · resize) 불린다
        releaseBackBuffers();
        _swapChain.Reset();
        _ownedRTVHeap.Reset();
        _pHWnd           = nullptr;
        _width           = 0;
        _height          = 0;
        _backBufferIndex = 0;
        _state           = D3D12_RESOURCE_STATE_PRESENT;
    }

    void D3D12RHISwapChain::createBackBuffers( ID3D12Device* pDevice, ID3D12DescriptorHeap* pRTVHeap, uint32 rtvDescriptorSize )
    {
        _renderThread.assumeHeld(); // 렌더 스레드의 프레임 진행(그 병렬 기록 태스크 포함)에서, 또는 렌더 스레드를 세운 뒤(초기화 · 종료 · resize) 불린다
        const bool bCannotCreateBackBuffers = _swapChain == nullptr || pDevice == nullptr || pRTVHeap == nullptr;
        if ( bCannotCreateBackBuffers )
            return;

        _listBackBuffer.resize( _bufferCount );
        _listBackBufferRTV.resize( _bufferCount );
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle( pRTVHeap->GetCPUDescriptorHandleForHeapStart() );

        for ( uint32 bufferIndex = 0; bufferIndex < _bufferCount; ++bufferIndex )
        {
            const HRESULT getHr = _swapChain->GetBuffer( bufferIndex, IID_PPV_ARGS( _listBackBuffer[bufferIndex].GetAddressOf() ) );
            if ( FAILED( getHr ) )
            {
                SW_LOG_ERROR_ID( "D3D12.GetBufferFailedHrX", "GetBuffer(%#) failed hr=0x%#", bufferIndex, static_cast<uint32>( getHr ) );
                releaseBackBuffers();
                return;
            }
            pDevice->CreateRenderTargetView( _listBackBuffer[bufferIndex].Get(), nullptr, rtvHandle );
            _listBackBufferRTV[bufferIndex] = rtvHandle;
            rtvHandle.ptr += rtvDescriptorSize;
        }
    }

    void D3D12RHISwapChain::releaseBackBuffers()
    {
        _renderThread.assumeHeld(); // 렌더 스레드의 프레임 진행(그 병렬 기록 태스크 포함)에서, 또는 렌더 스레드를 세운 뒤(초기화 · 종료 · resize) 불린다
        for ( Microsoft::WRL::ComPtr<ID3D12Resource>& backBuffer : _listBackBuffer )
        {
            backBuffer.Reset();
        }
        _listBackBuffer.clear();
        _listBackBufferRTV.clear();
    }

    bool D3D12RHISwapChain::resize( uint32 width, uint32 height )
    {
        if ( _swapChain == nullptr )
            return false;

        _width  = width;
        _height = height;

        // 생성 때와 **같은 플래그**여야 한다. 티어링 스왑체인을 0 으로 리사이즈하면 그 뒤의
        // Present( 0, ALLOW_TEARING ) 이 INVALID_CALL 이 된다.
        const HRESULT resizeHr = _swapChain->ResizeBuffers( _bufferCount, width, height, DXGI_FORMAT_UNKNOWN, _swapChainFlags );
        if ( FAILED( resizeHr ) )
        {
            SW_LOG_ERROR_ID( "D3D12.ResizeBuffersFailedHrX", "ResizeBuffers failed hr=0x%#", static_cast<uint32>( resizeHr ) );
            return false;
        }
        _displayOutputSupport = queryDXGIDisplayOutputSupport( _swapChain.Get() );                     // 창을 다른 모니터로 끌어 크기가 바뀌었을 수 있다
        _colorSpace           = reapplyDXGIColorSpace( makeBuffers(), _colorSpace, _displayMetadata ); // ResizeBuffers 가 HDR 메타데이터를 잃을 수 있다
        // 새 백버퍼는 PRESENT(COMMON) 상태로 생긴다 — 추적 상태도 그 값으로 맞춘다. present 하지 않고 끝난 프레임(무인 시나리오 · bPresent=false)의 RENDER_TARGET 을
        // 들고 가면 다음 프레임이 배리어 없이 Clear 해 디버그 레이어가 막았다(id 538).
        markPresented();
        return true;
    }

    bool D3D12RHISwapChain::setColorSpace( RHIColorSpace colorSpace, const RHIDisplayMetadata& metadata )
    {
        if ( _swapChain == nullptr )
            return false;
        // 포맷은 색 공간에서 정한다 — 버퍼 수 · 크기 · 플래그는 그대로 두고 포맷만 바꿔 다시 만든다(언리얼 `FD3D12Viewport::Resize` 의 픽셀 포맷 바꿈과 같은 길).
        // 실패하면 옛 포맷 · 색 공간(못 하면 SDR)으로 되돌린 값이 온다 — 버퍼 포맷과 기록한 색 공간이 늘 짝이다.
        const RHIColorSpace result = changeDXGIColorSpace( makeBuffers(), _colorSpace, _displayMetadata, colorSpace, metadata );
        markPresented();
        const bool bChanged = result == colorSpace;
        if ( bChanged )
            _displayMetadata = metadata;
        _colorSpace           = result;
        _displayOutputSupport = queryDXGIDisplayOutputSupport( _swapChain.Get() );
        return bChanged;
    }

    bool D3D12RHISwapChain::setDisplayMetadata( const RHIDisplayMetadata& metadata )
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

    void D3D12RHISwapChain::acquireNextImage()
    {
        _renderThread.assumeHeld(); // 렌더 스레드의 프레임 진행(그 병렬 기록 태스크 포함)에서, 또는 렌더 스레드를 세운 뒤(초기화 · 종료 · resize) 불린다
        if ( _swapChain == nullptr )
            return;
        _backBufferIndex = _swapChain->GetCurrentBackBufferIndex();
    }

    HRESULT D3D12RHISwapChain::present( bool vsync )
    {
        if ( _swapChain == nullptr )
            return S_OK;
        const UINT presentFlags = ( vsync == false && _bAllowTearing ) ? DXGI_PRESENT_ALLOW_TEARING : 0u;
        return _swapChain->Present( vsync ? 1 : 0, presentFlags );
    }

    void D3D12RHISwapChain::transitionTo( ID3D12GraphicsCommandList* pCmdList, D3D12_RESOURCE_STATES stateAfter )
    {
        _renderThread.assumeHeldByJoinedTask(); // 병렬 패스 기록 태스크가 부른다(렌더 스레드가 합류를 기다린다 — 인덱스는 기록 밖에서만 바뀐다)
        if ( pCmdList == nullptr || isBackBufferReady() == false )
            return;

        // 상태 확인과 배리어 기록이 한 덩어리여야 한다. 병렬 패스 기록에서 이 둘이 갈라지면 두
        // 스레드가 같은 "이전 상태"를 보고 각자 배리어를 기록하고, 두 번째가 before==after 가 된다.
        const ScopedDeferredLog deferredLog;
        ScopedLock<mutex>       lock{ _stateMutex };
        if ( _state == stateAfter )
            return;

        if ( _pBarrierWatcher != nullptr )
            _pBarrierWatcher->reportBarrierDuringRecording( "swapChain.transitionTo" );

        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource   = _listBackBuffer[_backBufferIndex].Get();
        barrier.Transition.StateBefore = _state;
        barrier.Transition.StateAfter  = stateAfter;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        pCmdList->ResourceBarrier( 1, &barrier );
        _state = stateAfter;
    }

    void D3D12RHISwapChain::markPresented()
    {
        ScopedLock<mutex> lock{ _stateMutex };
        _state = D3D12_RESOURCE_STATE_PRESENT;
    }

    D3D12_RESOURCE_STATES D3D12RHISwapChain::getState() const
    {
        ScopedLock<mutex> lock{ _stateMutex };
        return _state;
    }

    ID3D12Resource* D3D12RHISwapChain::getCurrentBackBuffer() const
    {
        _renderThread.assumeHeldByJoinedTask(); // 병렬 패스 기록 태스크가 부른다(렌더 스레드가 합류를 기다린다 — 인덱스는 기록 밖에서만 바뀐다)
        if ( isBackBufferReady() == false )
            return nullptr;
        return _listBackBuffer[_backBufferIndex].Get();
    }

    D3D12_CPU_DESCRIPTOR_HANDLE D3D12RHISwapChain::getCurrentRTV() const
    {
        _renderThread.assumeHeldByJoinedTask(); // 병렬 패스 기록 태스크가 부른다(렌더 스레드가 합류를 기다린다 — 인덱스는 기록 밖에서만 바뀐다)
        if ( _backBufferIndex >= _listBackBufferRTV.size() )
            return D3D12_CPU_DESCRIPTOR_HANDLE{ 0 };
        return _listBackBufferRTV[_backBufferIndex];
    }
} // namespace sw
#endif
