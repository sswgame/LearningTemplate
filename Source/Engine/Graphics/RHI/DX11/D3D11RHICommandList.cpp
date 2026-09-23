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
        , _recordingState{}
        , _context{ pDevice, _pNativeContext.Get(), &_recordingState }
    {
        _pContext = &_context;
        if ( _pDevice != nullptr )
            _pDevice->registerCommandList( this );
    }

    D3D11RHICommandList::~D3D11RHICommandList()
    {
        // 이 스레드가 이 리스트를 열어 두고(배리어 기록) 워커가 닫았으면 묶임이 여기 남아 있다 — 리스트와 함께 푼다.
        D3D11RHIDevice::unbindRecordingContextIf( _pNativeContext.Get() );
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
        D3D11RHIDevice::unbindRecordingContextIf( _pNativeContext.Get() );
        _pFinishedList.Reset();
        if ( _pNativeContext != nullptr )
            _pNativeContext->ClearState();
    }

    void D3D11RHICommandList::beginCommandList()
    {
        // FinishCommandList(FALSE, ...) 가 이전 실행 직후 Deferred Context 를 이미 기본 상태로
        // 되돌려놨다 — 이전에 제출한 네이티브 리스트 참조를 정리하고, 그 기본 상태에 없는 정적 샘플러 세트를 다시 건다.
        _pFinishedList.Reset();
        if ( _pDevice != nullptr )
            _pDevice->bindStaticSamplers( _pNativeContext.Get() );
        // 이 스레드의 리소스 갱신이 즉시 컨텍스트가 아니라 **이 Deferred Context** 로 가게 한다.
        D3D11RHIDevice::bindRecordingContext( _pNativeContext.Get() );
    }

    void D3D11RHICommandList::endCommandList()
    {
        D3D11RHIDevice::unbindRecordingContext();
        if ( _pNativeContext == nullptr )
            return;
        _pNativeContext->FinishCommandList( FALSE, _pFinishedList.ReleaseAndGetAddressOf() );
    }
} // namespace sw

#endif
