#include "pch.h"

#include "Engine/Graphics/RHI/DX12/D3D12RHICommandList.h"

#include "Engine/Graphics/RHI/DX12/D3D12RHIDevice.h"

#if defined( SW_PLATFORM_WINDOWS )

namespace sw
{
    D3D12RHICommandList::D3D12RHICommandList( D3D12RHIDevice* pDevice )
        : _pDevice{ pDevice }
        , _entry{ pDevice != nullptr ? pDevice->acquireCommandListEntry() : D3D12CommandListEntry{} }
        , _state{}
        , _context{ pDevice, _entry._list.Get(), &_state }
    {
    }

    D3D12RHICommandList::~D3D12RHICommandList()
    {
        if ( _pDevice != nullptr )
            _pDevice->recycleCommandListEntryDeferred( std::move( _entry ) );
    }

    void D3D12RHICommandList::beginCommandList()
    {
        _state = D3D12RecordingState{};
        if ( _pDevice == nullptr || _entry._list == nullptr || _entry._allocator == nullptr )
            return;

        // 이 리스트 전용 얼로케이터라 다른 스레드가 동시에 기록/Reset 하지 않는다. 풀에서 빌려온
        // 시점에 이미 GPU 펜스를 통과했으므로 곧바로 Reset 해도 안전하다.
        if ( FAILED( _entry._allocator->Reset() ) )
            return;
        if ( FAILED( _entry._list->Reset( _entry._allocator.Get(), nullptr ) ) )
            return;

        _state._bRecording = 1;
        if ( _pDevice->_cbvHeap != nullptr )
        {
            ID3D12DescriptorHeap* heaps[] = { _pDevice->_cbvHeap.Get() };
            _entry._list->SetDescriptorHeaps( 1, heaps );
        }
    }

    void D3D12RHICommandList::endCommandList()
    {
        if ( _entry._list != nullptr )
            _entry._list->Close();
    }
} // namespace sw

#endif
