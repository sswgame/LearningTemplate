#include "pch.h"

#include "Engine/Graphics/RHI/DX12/D3D12RHISwapChain.h"

#include "Engine/Graphics/RHI/DX/RHIDxResultCheck.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Engine/Graphics/RHI/DX/RHIDxgiFormat.h"
    #include "Engine/Graphics/RHI/DX/RHIDxgiTearing.h"

namespace sw
{
    SW_LOG_CALLER( "D3D12" );

    bool D3D12RHISwapChain::initialize( IDXGIFactory4* pFactory, ID3D12CommandQueue* pQueue, const RHISwapChainDesc& desc )
    {
        // 이 백엔드는 Win32 전용이다 — 핸들이 Win32 표면의 것이 아니면 HWND 로 읽지 않고(창 없음으로 대신하지도 않고) 오류로 멈춘다.
        void* pWindowHandle{ nullptr };
        if ( desc.tryGetWin32Handle( pWindowHandle ) == false )
        {
            SW_LOG_ERROR( "The D3D12 swap chain needs a Win32 surface" );
            return false;
        }
        _pHWnd       = static_cast<HWND>( pWindowHandle );
        _width       = desc._width;
        _height      = desc._height;
        _bufferCount = desc._bufferCount;
        _format      = desc._format;
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
        _bAllowTearing  = queryDxgiAllowTearing();
        _swapChainFlags = _bAllowTearing ? static_cast<uint32>( DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING ) : 0u;

        DXGI_SWAP_CHAIN_DESC1 scDesc{};
        scDesc.BufferCount      = _bufferCount;
        scDesc.Width            = _width;
        scDesc.Height           = _height;
        scDesc.Format           = toDxgiFormat( desc._format );
        scDesc.BufferUsage      = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        scDesc.SwapEffect       = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        scDesc.SampleDesc.Count = 1;
        scDesc.Flags            = _swapChainFlags;

        Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain1;
        if ( isHrSuccess( pFactory->CreateSwapChainForHwnd( pQueue, _pHWnd, &scDesc, nullptr, nullptr, swapChain1.GetAddressOf() ), "CreateSwapChainForHwnd" ) == false )
            return false;

        swapChain1.As( &_swapChain );
        acquireNextImage();
        return true;
    }

    void D3D12RHISwapChain::shutdown()
    {
        _renderThread.assumeHeld(); // 렌더 스레드의 프레임 진행(그 병렬 기록 태스크 포함)에서, 또는 렌더 스레드를 세운 뒤(초기화 · 종료 · resize) 불린다
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
        _renderThread.assumeHeld(); // 렌더 스레드의 프레임 진행(그 병렬 기록 태스크 포함)에서, 또는 렌더 스레드를 세운 뒤(초기화 · 종료 · resize) 불린다
        const bool bCannotCreateBackBuffers = _swapChain == nullptr || pDevice == nullptr || pRtvHeap == nullptr;
        if ( bCannotCreateBackBuffers )
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
        _renderThread.assumeHeld(); // 렌더 스레드의 프레임 진행(그 병렬 기록 태스크 포함)에서, 또는 렌더 스레드를 세운 뒤(초기화 · 종료 · resize) 불린다
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

        // 생성 때와 **같은 플래그**여야 한다. 티어링 스왑체인을 0 으로 리사이즈하면 그 뒤의
        // Present( 0, ALLOW_TEARING ) 이 INVALID_CALL 이 된다.
        const HRESULT resizeHr = _swapChain->ResizeBuffers( _bufferCount, width, height, DXGI_FORMAT_UNKNOWN, _swapChainFlags );
        if ( FAILED( resizeHr ) )
        {
            SW_LOG_ERROR( "ResizeBuffers failed hr=0x%#", static_cast<uint32>( resizeHr ) );
            return false;
        }
        return true;
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
        _renderThread.assumeHeld(); // 렌더 스레드의 프레임 진행(그 병렬 기록 태스크 포함)에서, 또는 렌더 스레드를 세운 뒤(초기화 · 종료 · resize) 불린다
        if ( pCmdList == nullptr || isBackBufferReady() == false )
            return;

        // 상태 확인과 배리어 기록이 한 덩어리여야 한다. 병렬 패스 기록에서 이 둘이 갈라지면 두
        // 스레드가 같은 "이전 상태"를 보고 각자 배리어를 기록하고, 두 번째가 before==after 가 된다.
        ScopedLock<mutex> lock{ _stateMutex };
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
        _renderThread.assumeHeld(); // 렌더 스레드의 프레임 진행(그 병렬 기록 태스크 포함)에서, 또는 렌더 스레드를 세운 뒤(초기화 · 종료 · resize) 불린다
        if ( isBackBufferReady() == false )
            return nullptr;
        return _listBackBuffer[_backBufferIndex].Get();
    }

    D3D12_CPU_DESCRIPTOR_HANDLE D3D12RHISwapChain::getCurrentRtv() const
    {
        _renderThread.assumeHeld(); // 렌더 스레드의 프레임 진행(그 병렬 기록 태스크 포함)에서, 또는 렌더 스레드를 세운 뒤(초기화 · 종료 · resize) 불린다
        if ( _backBufferIndex >= _listBackBufferRtv.size() )
            return D3D12_CPU_DESCRIPTOR_HANDLE{ 0 };
        return _listBackBufferRtv[_backBufferIndex];
    }
} // namespace sw
#endif
