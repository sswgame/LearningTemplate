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
        _pContext = &_context;
    }

    D3D12RHICommandList::~D3D12RHICommandList()
    {
        if ( _pDevice != nullptr )
            _pDevice->recycleCommandListEntryDeferred( std::move( _entry ) );
    }

    void D3D12RHICommandList::beginCommandList()
    {
        _state = D3D12RecordingState{};
        if ( _pDevice == nullptr )
            return;

        // 리스트 객체는 프레임을 넘어 재사용된다(FrameRenderer::_frameCmd 가 대표적이다). 그런데 그
        // 재사용은 얼로케이터 재사용이기도 해서, 직전 프레임에 제출한 커맨드를 GPU 가 아직 읽는 중에
        // Reset 을 때리는 계약 위반이 된다("command allocator is being reset before previous executions
        // have completed" → 커맨드 메모리 덮어쓰기 → DEVICE_HUNG). 에디터 경로는 프레임이 느려 GPU 가
        // 늘 따라잡아서 가려져 있었고, 에디터 없이 띄우면 곧바로 터졌다.
        // 그래서 두 번째 기록부터는 쌍을 통째로 갈아 낀다 — 쓰던 쌍은 펜스 통과 후 반납하고(대기 없음),
        // 새 쌍은 이미 펜스를 통과한 것만 들어있는 풀에서 빌린다. 풀은 in-flight 깊이만큼만 늘어난다.
        if ( _bEntryDirty != 0 )
        {
            _pDevice->recycleCommandListEntryDeferred( std::move( _entry ) );
            _entry = _pDevice->acquireCommandListEntry();
            _context.rebindCommandList( _entry._list.Get() );
            _bEntryDirty = 0;
        }

        if ( _entry._list == nullptr || _entry._allocator == nullptr )
            return;

        // 여기까지 왔으면 이 쌍은 이 리스트 전용이고 GPU 펜스도 통과한 상태다 — 바로 Reset 해도 된다.
        if ( FAILED( _entry._allocator->Reset() ) )
            return;
        if ( FAILED( _entry._list->Reset( _entry._allocator.Get(), nullptr ) ) )
            return;

        _bEntryDirty       = 1;
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
