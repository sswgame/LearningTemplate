#include "pch.h"

#include "Engine/Graphics/RHI/DX11/D3D11RHICommandList.h"

#include "Engine/Graphics/RHI/DX11/D3D11RHIDevice.h"

#if defined( SW_PLATFORM_WINDOWS )

namespace sw
{
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> D3D11RHICommandList::createNativeContext( D3D11RHIDevice* pDevice )
    {
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
        if ( pDevice == nullptr || pDevice->_device == nullptr )
        {
            SW_LOG_ERROR( "D3D11RHICommandList::createNativeContext: precondition failed (device=%#)",
                          pDevice != nullptr && pDevice->_device != nullptr );
            return context;
        }

        // 드라이버가 커맨드 리스트를 네이티브로 지원하지 않아도 D3D11 런타임이 소프트웨어로
        // 에뮬레이션하므로 이 호출은 항상 성공한다(느려질 뿐).
        const HRESULT hr = pDevice->_device->CreateDeferredContext( 0, context.GetAddressOf() );
        if ( FAILED( hr ) )
        {
            SW_LOG_ERROR( "D3D11RHICommandList::createNativeContext: CreateDeferredContext failed hr=0x%#", static_cast<uint32>( hr ) );
            context.Reset();
        }
        return context;
    }

    D3D11RHICommandList::D3D11RHICommandList( D3D11RHIDevice* pDevice )
        : _pDevice{ pDevice }
        , _pNativeContext{ createNativeContext( pDevice ) }
        , _pFinishedList{ nullptr }
        , _context{ pDevice, _pNativeContext.Get() }
    {
        _pContext = &_context;
        if ( _pDevice != nullptr )
            _pDevice->registerCommandList( this );
    }

    D3D11RHICommandList::~D3D11RHICommandList()
    {
        if ( _pDevice != nullptr )
            _pDevice->unregisterCommandList( this );
    }

    void D3D11RHICommandList::detachFromDevice()
    {
        releaseRecordedState();
        _pDevice = nullptr;
    }

    void D3D11RHICommandList::releaseRecordedState()
    {
        _pFinishedList.Reset();
        if ( _pNativeContext != nullptr )
            _pNativeContext->ClearState();
    }

    void D3D11RHICommandList::beginCommandList()
    {
        // FinishCommandList(FALSE, ...) 가 이전 실행 직후 Deferred Context 를 이미 기본 상태로
        // 되돌려놨다 — 여기서는 이전에 제출한 네이티브 리스트 참조만 정리한다.
        _pFinishedList.Reset();
    }

    void D3D11RHICommandList::endCommandList()
    {
        if ( _pNativeContext == nullptr )
            return;
        _pNativeContext->FinishCommandList( FALSE, _pFinishedList.ReleaseAndGetAddressOf() );
    }
} // namespace sw

#endif
