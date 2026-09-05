#include "pch.h"

#include "Engine/Graphics/RHI/DX12/D3D12RHICommandList.h"

#include "Engine/Graphics/RHI/DX12/D3D12RHIDevice.h"

#if defined( SW_PLATFORM_WINDOWS )

namespace sw
{
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> D3D12RHICommandList::createNativeList( D3D12RHIDevice* pDevice )
    {
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> list;
        if ( pDevice == nullptr || pDevice->_device == nullptr || pDevice->_arrFrameCmdAllocator[0] == nullptr )
        {
            SW_LOG_ERROR( "D3D12RHICommandList::createNativeList: precondition failed (device=%# alloc0=%#)",
                          pDevice != nullptr && pDevice->_device != nullptr,
                          pDevice != nullptr && pDevice->_arrFrameCmdAllocator[0] != nullptr );
            return list;
        }

        // 생성 시점의 얼로케이터는 더미다 — beginCommandList() 가 실제 프레임 링 얼로케이터로 Reset 한다.
        const HRESULT hr = pDevice->_device->CreateCommandList( 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                                pDevice->_arrFrameCmdAllocator[0].Get(), nullptr,
                                                                IID_PPV_ARGS( list.GetAddressOf() ) );
        if ( SUCCEEDED( hr ) )
            list->Close();
        else
        {
            SW_LOG_ERROR( "D3D12RHICommandList::createNativeList: CreateCommandList failed hr=0x%#", static_cast<uint32>( hr ) );
            list.Reset();
        }
        return list;
    }

    D3D12RHICommandList::D3D12RHICommandList( D3D12RHIDevice* pDevice )
        : _pDevice{ pDevice }
        , _cmdList{ createNativeList( pDevice ) }
        , _state{}
        , _context{ pDevice, _cmdList.Get(), &_state }
    {
    }

    void D3D12RHICommandList::beginCommandList()
    {
        _state = D3D12RecordingState{};
        if ( _pDevice == nullptr || _cmdList == nullptr )
            return;

        // 스왑체인 begin/end(레거시 리스트)와 별개인 전용 얼로케이터를 쓴다 — 링 인덱스 자체는
        // 이번 프레임에 이미 waitForRingSlot() 이 정한 것을 그대로 따른다(다시 대기/전진하지 않음).
        ID3D12CommandAllocator* pAllocator = _pDevice->currentFrameCmdAllocator();
        if ( pAllocator == nullptr )
            return;
        pAllocator->Reset();
        _cmdList->Reset( pAllocator, nullptr );
        _state._bRecording = 1;
        if ( _pDevice->_cbvHeap != nullptr )
        {
            ID3D12DescriptorHeap* heaps[] = { _pDevice->_cbvHeap.Get() };
            _cmdList->SetDescriptorHeaps( 1, heaps );
        }
    }

    void D3D12RHICommandList::endCommandList()
    {
        if ( _cmdList != nullptr )
            _cmdList->Close();
    }
} // namespace sw

#endif
