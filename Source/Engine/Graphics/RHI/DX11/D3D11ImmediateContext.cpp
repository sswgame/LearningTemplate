#include "pch.h"

#include "Engine/Graphics/RHI/DX11/D3D11ImmediateContext.h"

#if defined( SW_PLATFORM_WINDOWS )

namespace sw
{
    SW_LOG_CALLER( "D3D11" );

    D3D11ImmediateContext::ScopedLock::ScopedLock( ID3D11DeviceContext* pContext, ID3D11Multithread* pMultithread )
        : _pContext{ pContext }
        , _pMultithread{ pMultithread }
    {
        if ( _pMultithread != nullptr )
            _pMultithread->Enter();
    }

    D3D11ImmediateContext::ScopedLock::~ScopedLock()
    {
        if ( _pMultithread != nullptr )
            _pMultithread->Leave();
    }

    D3D11ImmediateContext::D3D11ImmediateContext()
        : _context{ nullptr }
        , _multithread{ nullptr }
    {
    }

    bool D3D11ImmediateContext::initialize( Microsoft::WRL::ComPtr<ID3D11DeviceContext> context )
    {
        if ( context == nullptr )
        {
            SW_LOG_ERROR( "D3D11ImmediateContext::initialize: the device returned no immediate context" );
            return false;
        }
        Microsoft::WRL::ComPtr<ID3D11Multithread> multithread;
        const HRESULT                             hr = context.As( &multithread );
        if ( FAILED( hr ) )
        {
            SW_LOG_ERROR( "D3D11ImmediateContext::initialize: ID3D11Multithread is not available hr=0x%#", static_cast<uint32>( hr ) );
            return false;
        }
        // 렌더 스레드의 프레임 스트림과 다른 스레드의 전송이 같은 즉시 컨텍스트를 쓴다. 런타임이 호출마다 잠그게 한다.
        (void)multithread->SetMultithreadProtected( TRUE ); // 돌려주는 값은 이전 상태다
        if ( multithread->GetMultithreadProtected() == FALSE )
        {
            SW_LOG_ERROR( "D3D11ImmediateContext::initialize: multithread protection could not be turned on" );
            return false;
        }
        _context     = std::move( context );
        _multithread = std::move( multithread );
        return true;
    }

    void D3D11ImmediateContext::shutdown()
    {
        _multithread.Reset();
        _context.Reset();
    }

    D3D11ImmediateContext::ScopedLock D3D11ImmediateContext::lock() const
    {
        SW_ASSERT( isValid() );
        return ScopedLock{ _context.Get(), _multithread.Get() };
    }
} // namespace sw

#endif
