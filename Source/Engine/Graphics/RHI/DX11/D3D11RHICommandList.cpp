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
        , _recordingSlot{ D3D11RHIDevice::kNoRecordingSlot }
        , _recordingState{}
        , _context{ pDevice, _pNativeContext.Get(), &_recordingState }
    {
        _pContext = &_context;
        if ( _pDevice != nullptr )
        {
            _pDevice->registerCommandList( this );
            _recordingSlot = _pDevice->acquireRecordingSlot( _pNativeContext.Get() );
        }
    }

    D3D11RHICommandList::~D3D11RHICommandList()
    {
        if ( _pDevice != nullptr )
        {
            // 슬롯을 돌려주면 세대가 바뀐다. 어느 스레드가 이 리스트의 토큰을 들고 있든 그 순간 무효다.
            _pDevice->releaseRecordingSlot( _recordingSlot );
            _pDevice->unregisterCommandList( this );
        }
    }

    void D3D11RHICommandList::detachFromDevice()
    {
        if ( _pDevice != nullptr )
            _pDevice->releaseRecordingSlot( _recordingSlot );
        _recordingSlot = D3D11RHIDevice::kNoRecordingSlot;
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
        // 되돌려 놨다. 이전에 제출한 네이티브 리스트 참조를 정리하고, 그 기본 상태에 없는 정적 샘플러 세트를 다시 건다.
        _pFinishedList.Reset();
        if ( _pDevice != nullptr )
            _pDevice->bindStaticSamplers( _pNativeContext.Get() );
        // 이 스레드의 리소스 갱신이 즉시 컨텍스트가 아니라 **이 Deferred Context** 로 가게 한다. 새 기록 세대를 열고 토큰을 묶는다.
        if ( _pDevice != nullptr )
            _pDevice->beginRecording( _recordingSlot );
    }

    void D3D11RHICommandList::endCommandList()
    {
        // 세대를 닫는다. 이 리스트를 연 스레드가 누구든 그쪽 토큰도 여기서 무효가 된다.
        if ( _pDevice != nullptr )
            _pDevice->endRecording( _recordingSlot );
        if ( _pNativeContext == nullptr )
            return;
        _pNativeContext->FinishCommandList( FALSE, _pFinishedList.ReleaseAndGetAddressOf() );
    }
} // namespace sw

#endif
