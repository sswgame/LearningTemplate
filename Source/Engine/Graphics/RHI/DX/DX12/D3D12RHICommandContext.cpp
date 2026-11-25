#include "pch.h"

#include "Engine/Graphics/RHI/DX/DX12/D3D12RHICommandContext.h"

#include "Core/Concurrency/mutex.h"

#include "Engine/Graphics/Debug/GPUBreadcrumbs.h"
#include "Engine/Graphics/RHI/DX/DX12/D3D12RHIDevice.h"
#include "Engine/Graphics/RHI/DX/DX12/D3D12RHIResourceFactory.h"
#include "Engine/Graphics/RHI/DX/DXGIFormat.h"
#include "Engine/Graphics/RHI/Diagnostics/RHIDrawDiagnostics.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"

#if defined( SW_PLATFORM_WINDOWS )
    #if __has_include( <pix3.h> )
        #include <pix3.h>
        #define SW_HAS_PIX 1
    #endif

namespace sw
{
    IRHIDevice& D3D12RHICommandContext::getContextDevice() const
    {
        return *_pDevice;
    }

    namespace
    {
        struct D3D12RHICommandContextInternal
        {
            /** @brief 네이티브 렌더 패스 API(`ID3D12GraphicsCommandList4`)입니다. Windows 10 1809 런타임부터 늘 있다 — 없으면 기록할 수 없어 멈춘다(대신하는 길을 두지 않는다). */
            static Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> asRenderPassList( ID3D12GraphicsCommandList* pList )
            {
                Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> list4;
                requireHr( pList->QueryInterface( IID_PPV_ARGS( list4.GetAddressOf() ) ), "QueryInterface(ID3D12GraphicsCommandList4)" );
                return list4;
            }

            /** @brief 엔진 load op 을 네이티브 패스의 시작 접근으로 옮깁니다. */
            static D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE toBeginningAccess( RHIRenderPassLoadOp loadOp )
            {
                switch ( loadOp )
                {
                    case RHIRenderPassLoadOp::Clear:
                        return D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR;
                    case RHIRenderPassLoadOp::Load:
                        return D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_PRESERVE;
                    case RHIRenderPassLoadOp::DontCare:
                        return D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_DISCARD;
                }
                return D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_PRESERVE;
            }

            /** @brief 엔진 store op 을 네이티브 패스의 끝 접근으로 옮깁니다. */
            static D3D12_RENDER_PASS_ENDING_ACCESS_TYPE toEndingAccess( RHIRenderPassStoreOp storeOp )
            {
                return storeOp == RHIRenderPassStoreOp::DontCare ? D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_DISCARD : D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_PRESERVE;
            }
        };
    } // namespace

    namespace
    {
        D3D12_RESOURCE_STATES toD3D12BufferState( RHIBufferState state )
        {
            switch ( state )
            {
                case RHIBufferState::UnorderedAccess:
                    return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
                case RHIBufferState::ShaderResource:
                    return D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
                case RHIBufferState::IndirectArgument:
                    return D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
                case RHIBufferState::CopyDest:
                    return D3D12_RESOURCE_STATE_COPY_DEST;
                case RHIBufferState::VertexOrConstant:
                    // 정점 버퍼는 셰이더도 읽는다(모프 · 스킨 컴퓨트가 풀을 SRV 로) — 읽기 상태는 합칠 수 있다. GPU 전용 정점 버퍼(R-15)가 이 상태로 돌아온다.
                    return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
                case RHIBufferState::Index:
                    return D3D12_RESOURCE_STATE_INDEX_BUFFER;
                case RHIBufferState::CopySource:
                    return D3D12_RESOURCE_STATE_COPY_SOURCE;
                case RHIBufferState::Common:
                    return D3D12_RESOURCE_STATE_COMMON;
            }
        }
    } // namespace
} // namespace sw

namespace sw
{
    ID3D12GraphicsCommandList* D3D12RHICommandContext::commandListForRecord()
    {
        if ( _pState != nullptr )
            _pState->_bRecordedAny = SW_TRUE;
        return _pCmdList;
    }

    void D3D12RHICommandContext::ensureRecording()
    {
        if ( _pDevice == nullptr || _pState->_bRecording != SW_FALSE )
            return;
        _pDevice->waitForRingSlot();
        ID3D12CommandAllocator* pAllocator = _pDevice->currentAllocator();
        if ( pAllocator == nullptr || _pCmdList == nullptr )
            return;
        // 얼로케이터 · 리스트 리셋이 실패하면(GPU 가 아직 쓰는 얼로케이터, 장치 분실) 닫힌 리스트에 기록하게 된다 — 이어 갈 수 없어 멈춘다.
        requireHr( pAllocator->Reset(), "ID3D12CommandAllocator::Reset" );
        requireHr( commandListForRecord()->Reset( pAllocator, nullptr ), "ID3D12GraphicsCommandList::Reset" );
        _pState->_bRecording     = SW_TRUE;
        _pState->_boundNativePSO = 0; // 새 리스트에는 아직 아무 PSO 도 안 걸렸다. 캐시를 무효로 한다.
        for ( D3D12SlotTableState& slotState : _pState->_arrSlotState )
        {
            slotState = D3D12SlotTableState{}; // 새 리스트에는 슬롯 테이블도 없다. 첫 드로우가 다시 굳힌다.
        }
        beginRecording();
        // 힙 · 루트 시그니처 · 텍스처 배열 테이블은 리스트가 열릴 때 한 번 건다. 이후 bind*() 는 루트 CBV(상수버퍼)와 슬롯 테이블(t · u)만 바꾼다.
        _pDevice->bindBindlessRootState( _pCmdList );
    }

    D3D12_GPU_VIRTUAL_ADDRESS D3D12RHICommandContext::resolveBufferAddress( RHIDescriptorIndex index, bool bUAV, bool bConstantBuffer ) const
    {
        const D3D12RHIDevice::BindlessReadSlot* const pSlot = _pDevice->findBindlessReadSlot( index, bUAV );
        if ( pSlot == nullptr )
            return 0;
        D3D12_GPU_VIRTUAL_ADDRESS address = pSlot->getGPUAddress();
        if ( bConstantBuffer && address != 0 )
        {
            // 링 상수버퍼(createConstantBuffer)는 프레임 슬롯마다 정렬 크기만큼 떨어진 자리에 쓴다(링이 아니면 크기가 0 이다).
            // updateConstantBuffer 가 이번 프레임 슬롯에 썼으므로 같은 슬롯 주소를 건다.
            const uint32 alignedSize = pSlot->getCBAlignedSize();
            address += static_cast<D3D12_GPU_VIRTUAL_ADDRESS>( _pDevice->_frameRing.currentIndex() ) * alignedSize;
        }
        return address;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE D3D12RHICommandContext::resolveOfflineView( RHIDescriptorIndex index, bool bUAV ) const
    {
        const D3D12RHIDevice::BindlessReadSlot* const pSlot = _pDevice->findBindlessReadSlot( index, bUAV );
        if ( pSlot == nullptr )
            return D3D12_CPU_DESCRIPTOR_HANDLE{};
        return D3D12_CPU_DESCRIPTOR_HANDLE{ static_cast<SIZE_T>( pSlot->getOfflineView() ) };
    }

    bool D3D12RHICommandContext::allocateOnlineDescriptors( uint32 count, uint32& outBase )
    {
        if ( _pState->_onlineCursor + count > _pState->_onlineEnd )
        {
            const uint32 block = _pDevice->acquireOnlineBlock();
            if ( block == UINT32_MAX )
                return false;
            _pState->_listOnlineBlock.push_back( block );
            _pState->_onlineCursor = D3D12RHIDevice::kBindlessDescriptorCapacity + block * D3D12RHIDevice::kOnlineBlockDescriptorCount;
            _pState->_onlineEnd    = _pState->_onlineCursor + D3D12RHIDevice::kOnlineBlockDescriptorCount;
        }
        outBase = _pState->_onlineCursor;
        _pState->_onlineCursor += count;
        return true;
    }

    bool D3D12RHICommandContext::writeSlotTable( const D3D12_CPU_DESCRIPTOR_HANDLE* pSlots, uint32 count, D3D12_CPU_DESCRIPTOR_HANDLE nullView,
                                                 D3D12_GPU_DESCRIPTOR_HANDLE& outTable )
    {
        const bool bInvalidSlotTableRequest = count == 0 || count > D3D12RHIDevice::kMaxSlotTableSize || nullView.ptr == 0;
        if ( bInvalidSlotTableRequest )
            return false;
        uint32 base{ 0 };
        if ( allocateOnlineDescriptors( count, base ) == false )
            return false;

        // 원본은 슬롯마다 흩어져 있고(오프라인 힙 여기저기) 목적지는 연속 구간 하나다. CopyDescriptors 의 N:1 형태다.
        D3D12_CPU_DESCRIPTOR_HANDLE arrSrc[D3D12RHIDevice::kMaxSlotTableSize]{};
        UINT                        arrSrcSize[D3D12RHIDevice::kMaxSlotTableSize]{};
        for ( uint32 slot = 0; slot < count; ++slot )
        {
            arrSrc[slot]     = ( pSlots[slot].ptr != 0 ) ? pSlots[slot] : nullView;
            arrSrcSize[slot] = 1;
        }
        const D3D12_CPU_DESCRIPTOR_HANDLE dst     = _pDevice->shaderVisibleCPUAt( base );
        const UINT                        dstSize = count;
        _pDevice->_device->CopyDescriptors( 1, &dst, &dstSize, count, arrSrc, arrSrcSize, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
        outTable = _pDevice->shaderVisibleGPUAt( base );
        return true;
    }

    bool D3D12RHICommandContext::flushBindings( RHIBindPoint point )
    {
        if ( _pCmdList == nullptr || _pDevice->_rootSignature == nullptr )
            return false;
        const bool                bCompute    = point == RHIBindPoint::Compute;
        RHIShaderSlots&           slots       = _bindings.getSlots( point );
        D3D12SlotTableState&      state       = _pState->_arrSlotState[static_cast<uint32>( point )];
        const RHIDrawDiagnostics& diagnostics = _pDevice->getDrawDiagnostics();

        // 상수버퍼는 디스크립터 테이블이 아니라 루트 CBV(GPU 주소)다. 힙에 쓸 일이 없고 슬롯 b# 이 곧 루트 파라미터다. 뗀 칸은 0 으로 채운 버퍼다(루트 CBV 는 null 을 읽을 수 없다).
        if ( slots.isDirty( RHIShaderSlotKind::ConstantBuffer ) )
        {
            slots.flush( RHIShaderSlotKind::ConstantBuffer, [&]( uint32 slot, RHIDescriptorIndex index )
            {
                D3D12_GPU_VIRTUAL_ADDRESS address = index != kInvalidDescriptorIndex ? resolveBufferAddress( index, false, true ) : 0;
                if ( address == 0 )
                {
                    if ( index != kInvalidDescriptorIndex )
                        diagnostics.reportDescriptorNotRegistered( "b", slot, index );
                    address = _pDevice->getNullConstantBufferAddress();
                }
                if ( bCompute )
                    commandListForRecord()->SetComputeRootConstantBufferView( D3D12RHIDevice::kCBVRootParam0 + slot, address );
                else
                    commandListForRecord()->SetGraphicsRootConstantBufferView( D3D12RHIDevice::kCBVRootParam0 + slot, address );
            } );
        }

        // t# · u# → 오프라인 뷰 표. **뷰가 실제로 달라질 때만** 테이블을 더럽힌다 — 바인더는 드로우마다 같은 버퍼를 다시 걸므로(PSO 가 바뀌어도 인스턴스 버퍼 · 머티리얼 버퍼는
        // 대개 그대로다) 무조건 표시하면 드로우마다 디스크립터를 온라인 힙에 복사한다. 언리얼 FD3D12DescriptorCache 도 테이블 내용이 그대로면 이미 건 테이블을 그대로 쓴다.
        const auto resolveView = [&]( const utf8* pKind, uint32 slot, RHIDescriptorIndex index, bool bUAV )
        {
            if ( index == kInvalidDescriptorIndex )
                return D3D12_CPU_DESCRIPTOR_HANDLE{};
            const D3D12_CPU_DESCRIPTOR_HANDLE view = resolveOfflineView( index, bUAV );
            if ( view.ptr == 0 )
                diagnostics.reportDescriptorNotRegistered( pKind, slot, index );
            return view;
        };
        if ( slots.isDirty( RHIShaderSlotKind::ShaderResource ) )
        {
            slots.flush( RHIShaderSlotKind::ShaderResource, [&]( uint32 slot, RHIDescriptorIndex index )
            {
                // 텍스처 칸(t16..)은 에뮬 백엔드의 것이다 — 여기서는 셰이더가 힙을 번호로 읽는다.
                if ( slot >= shaderslot::kSRVSlotCount )
                    return;
                const D3D12_CPU_DESCRIPTOR_HANDLE view = resolveView( "t", slot, index, false );
                if ( state._arrSRV[slot].ptr == view.ptr )
                    return;
                state._arrSRV[slot] = view;
                state._bSRVDirty    = SW_TRUE;
            } );
        }
        if ( slots.isDirty( RHIShaderSlotKind::UnorderedAccess ) )
        {
            slots.flush( RHIShaderSlotKind::UnorderedAccess, [&]( uint32 slot, RHIDescriptorIndex index )
            {
                // RW 텍스처 칸(u5..)도 에뮬 백엔드의 것이다.
                if ( slot >= shaderslot::kComputeUAVSlotCount )
                    return;
                const D3D12_CPU_DESCRIPTOR_HANDLE view = resolveView( "u", slot, index, true );
                if ( state._arrUAV[slot].ptr == view.ptr )
                    return;
                state._arrUAV[slot] = view;
                state._bUAVDirty    = SW_TRUE;
            } );
        }

        if ( state._bSRVDirty != SW_FALSE )
        {
            D3D12_GPU_DESCRIPTOR_HANDLE table{};
            if ( writeSlotTable( state._arrSRV, shaderslot::kSRVSlotCount, _pDevice->offlineDescriptorAt( D3D12RHIDevice::kOfflineNullSRVIndex ), table ) == false )
                return false;
            if ( bCompute )
                commandListForRecord()->SetComputeRootDescriptorTable( D3D12RHIDevice::kSRVTableParam, table );
            else
                commandListForRecord()->SetGraphicsRootDescriptorTable( D3D12RHIDevice::kSRVTableParam, table );
            state._bSRVDirty = SW_FALSE;
        }
        // u 테이블 — 컴퓨트는 셰이더 소유 버퍼 · 메시지 링, 그래픽스는 셰이더 메시지 링(u4, bindGraphicsUAV)만 건다.
        if ( state._bUAVDirty != SW_FALSE )
        {
            D3D12_GPU_DESCRIPTOR_HANDLE table{};
            if ( writeSlotTable( state._arrUAV, shaderslot::kComputeUAVSlotCount, _pDevice->offlineDescriptorAt( D3D12RHIDevice::kOfflineNullUAVIndex ), table ) == false )
                return false;
            if ( bCompute )
                commandListForRecord()->SetComputeRootDescriptorTable( D3D12RHIDevice::kUAVTableParam, table );
            else
                commandListForRecord()->SetGraphicsRootDescriptorTable( D3D12RHIDevice::kUAVTableParam, table );
            state._bUAVDirty = SW_FALSE;
        }

        RHIRootConstants& constants = _bindings.getRootConstants( point );
        if ( constants.isDirty() )
        {
            const uint32 first = constants._dirtyBegin;
            const uint32 count = constants._dirtyEnd - constants._dirtyBegin;
            if ( bCompute )
                commandListForRecord()->SetComputeRoot32BitConstants( D3D12RHIDevice::kRootConstantsParam, count, constants._arrDword + first, first );
            else
                commandListForRecord()->SetGraphicsRoot32BitConstants( D3D12RHIDevice::kRootConstantsParam, count, constants._arrDword + first, first );
            constants.markFlushed();
        }
        return true;
    }

    bool D3D12RHICommandContext::prepareDraw( bool bIndexed )
    {
        // 백엔드 계약(네 백엔드 같음): 등록된 그래픽스 PSO 없이 그리면 오류로 멈춘다. 조용히 건너뛰거나 앞 PSO 로 그리지 않는다 — PSO 가 0 이면
        // 부르는 쪽이 드로우를 건너뛰는 것이 렌더러의 규칙이라, 여기 닿은 것은 부르는 쪽의 결함이다(지운 핸들 · PSO 를 걸지 않은 드로우).
        const D3D12RHIDevice::D3D12PipelineStateRecord* pRecord = _pDevice->_pipelineStates.get( _bindings._graphicsPSO );
        if ( pRecord == nullptr || pRecord->_pso == nullptr )
            reportFatalError( "D3D12 draw without a registered graphics PSO (handle %#)", _bindings._graphicsPSO.getPacked() );
        // 루트 시그니처는 리스트가 열릴 때 이미 걸렸다(bindBindlessRootState). 루트 인자는 PSO 를 바꿔도 남는다.
        if ( _pState->_boundNativePSO != _bindings._graphicsPSO )
        {
            commandListForRecord()->SetPipelineState( pRecord->_pso.Get() );
            commandListForRecord()->OMSetStencilRef( pRecord->_stencilRef );
            commandListForRecord()->IASetPrimitiveTopology( pRecord->_topology );
            _pState->_boundNativePSO = _bindings._graphicsPSO;
        }
        if ( flushBindings( RHIBindPoint::Graphics ) == false )
            return false;
        if ( bindMeshVertexBufferForDraw( pRecord->_vertexLayout ) == false )
            return false;
        return bIndexed == false || bindIndexBufferForDraw();
    }

    bool D3D12RHICommandContext::prepareDispatch()
    {
        // 백엔드 계약(네 백엔드 같음): 등록된 컴퓨트 PSO 없이 디스패치하면 오류로 멈춘다. 앞 디스패치의 PSO 로 대신 돌지 않고 조용히 건너뛰지도 않는다 — PSO 가 0 이면
        // 부르는 쪽이 디스패치를 건너뛰는 것이 렌더러의 규칙이라, 여기 닿은 것은 부르는 쪽의 결함이다(지운 핸들 · 컴퓨트 PSO 를 걸지 않은 디스패치).
        const D3D12RHIDevice::D3D12PipelineStateRecord* pRecord = _pDevice->_pipelineStates.get( _bindings._computePSO );
        if ( pRecord == nullptr || pRecord->_pso == nullptr )
            reportFatalError( "D3D12 dispatch without a registered compute PSO (handle %#)", _bindings._computePSO.getPacked() );
        // 리스트의 PSO 는 하나다 — 컴퓨트를 걸면 다음 드로우가 그래픽스 PSO 를 다시 건다.
        if ( _pState->_boundNativePSO != _bindings._computePSO )
        {
            commandListForRecord()->SetPipelineState( pRecord->_pso.Get() );
            _pState->_boundNativePSO = _bindings._computePSO;
        }
        return flushBindings( RHIBindPoint::Compute );
    }

    bool D3D12RHICommandContext::bindMeshVertexBufferForDraw( const RHIVertexLayout& layout )
    {
        // PSO 가 읽는 슬롯에만 버퍼를 건다(정점 입력이 없는 셰이더는 레이아웃이 비어 아무 슬롯도 읽지 않는다). 건 뒤에 부서진 버퍼(세대가 달라 풀리지 않는다)면 드로우를 버린다.
        return _bindings._geometry.bindVertexBuffersForDraw( _pDevice->getDrawDiagnostics(), layout, [this]( uint32 slot, const RHIBufferBinding& binding )
        {
            ID3D12Resource* const pVb = _pDevice->resolveBuffer( binding._buffer );
            if ( pVb == nullptr )
                return false;
            D3D12_VERTEX_BUFFER_VIEW vbv{};
            vbv.BufferLocation = pVb->GetGPUVirtualAddress() + binding._offset;
            vbv.SizeInBytes    = static_cast<UINT>( pVb->GetDesc().Width > binding._offset ? pVb->GetDesc().Width - binding._offset : 0 );
            vbv.StrideInBytes  = binding._stride;
            commandListForRecord()->IASetVertexBuffers( slot, 1, &vbv );
            return true;
        } );
    }

    bool D3D12RHICommandContext::bindIndexBufferForDraw()
    {
        return _bindings._geometry.bindIndexBufferForDraw( _pDevice->getDrawDiagnostics(), [this]( const RHIBufferBinding& binding )
        {
            ID3D12Resource* const pIb = _pDevice->resolveBuffer( binding._buffer );
            if ( pIb == nullptr )
                return false;
            const UINT64            width = pIb->GetDesc().Width;
            D3D12_INDEX_BUFFER_VIEW ibv{};
            ibv.BufferLocation = pIb->GetGPUVirtualAddress() + binding._offset;
            ibv.SizeInBytes    = static_cast<UINT>( width > binding._offset ? width - binding._offset : 0 );
            ibv.Format         = ( binding._stride == 2 ) ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
            commandListForRecord()->IASetIndexBuffer( &ibv );
            return true;
        } );
    }

    D3D12_RESOURCE_STATES D3D12RHICommandContext::toQueueState( D3D12_RESOURCE_STATES state ) const
    {
        if ( _bComputeQueueList == SW_FALSE || ( state & D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE ) == 0 )
            return state;
        return static_cast<D3D12_RESOURCE_STATES>( ( state & ~D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE ) | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE );
    }

    uint32 D3D12RHICommandContext::appendTextureBarriers( RHITextureHandle texture, D3D12_RESOURCE_STATES requestedState, const RHISubresourceRange& range )
    {
        const D3D12_RESOURCE_STATES          newState = toQueueState( requestedState );
        D3D12RHIDevice::TextureRecord* const pRecord  = _pDevice->findTextureRecord( texture );
        if ( pRecord == nullptr || pRecord->_resource == nullptr || pRecord->_bOffscreen == SW_FALSE )
            return 0;
        // 상태 확인과 상태 바꾸기가 한 덩어리여야 한다. RenderGraph::executeParallel 이 같은 레벨의 패스 콜백을 여러 스레드에서 돌리는데,
        // 둘이 같은 텍스처를 전이하면 둘 다 같은 "이전 상태" 를 보고 각자 배리어를 쏴서 두 번째가 before==after 가 된다(검증 오류 → 디바이스 제거).
        // 추적 상태가 한 번에 정해 이긴 쪽 하나만 배리어를 낸다(`RHITrackedSubresourceState`). 일부 밉 · 면만 옮기면 서브리소스마다 낸다(R-10).
        ID3D12Resource* const pResource = pRecord->_resource.Get();
        return pRecord->_state.transitionTo( range, newState, [this, pResource, newState]( uint32 subresource, D3D12_RESOURCE_STATES stateBefore )
        {
            D3D12_RESOURCE_BARRIER& barrier = _listBarrierScratch.emplace_back();
            barrier                         = D3D12_RESOURCE_BARRIER{};
            barrier.Type                    = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource    = pResource;
            barrier.Transition.StateBefore  = stateBefore;
            barrier.Transition.StateAfter   = newState;
            barrier.Transition.Subresource  = subresource; // kWholeResource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES
        } );
    }

    void D3D12RHICommandContext::flushBarrierScratch()
    {
        if ( _listBarrierScratch.empty() )
            return;
        commandListForRecord()->ResourceBarrier( static_cast<UINT>( _listBarrierScratch.size() ), _listBarrierScratch.data() );
        _listBarrierScratch.clear();
    }

    void D3D12RHICommandContext::transitionTexture( RHITextureHandle texture, D3D12_RESOURCE_STATES requestedState, const RHISubresourceRange& range )
    {
        (void)appendTextureBarriers( texture, requestedState, range );
        flushBarrierScratch();
    }

    bool D3D12RHICommandContext::findTextureTransitionState( const RHITextureTransition& transition, D3D12_RESOURCE_STATES& outState ) const
    {
        const D3D12RHIDevice::TextureRecord* const pRecord = _pDevice->findTextureRecord( transition._texture );
        if ( pRecord == nullptr || pRecord->_bOffscreen == SW_FALSE )
            return false;
        const bool bDepth = pRecord->_offscreen._bHasDSV != SW_FALSE;
        switch ( transition._state )
        {
            case RHITextureState::ShaderResource:
                // 첨부가 없는 텍스처는 COMMON 에서 SRV 로 암묵 승격된다 — 옮길 것이 없다.
                if ( pRecord->_offscreen._bHasRTV == SW_FALSE && bDepth == false )
                    return false;
                // 모든 셰이더 단계의 읽기다(R-9 — 픽셀 단계만이면 컴퓨트 · 정점 셰이더의 SRV 읽기가 틀린 상태다). 컴퓨트 큐 리스트는 아래 상태 거르기가 PIXEL 을 뺀다.
                outState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
                return true;
            case RHITextureState::RenderTarget:
                outState = bDepth ? D3D12_RESOURCE_STATE_DEPTH_WRITE : D3D12_RESOURCE_STATE_RENDER_TARGET;
                return true;
            case RHITextureState::UnorderedAccess:
                // COMMON 은 SRV 로만 암묵 승격된다. UAV 는 명시 전이가 필요하다(readback 이 레코드 상태로 되돌린다).
                outState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
                return true;
        }
        return false;
    }

    void D3D12RHICommandContext::transitionTexturesNative( const RHITextureTransition* pTransition, uint32 count )
    {
        if ( _pCmdList == nullptr )
            return;
        // 배열 하나로 낸다 — ResourceBarrier 는 배열을 원소마다 부른 것처럼 차례대로 처리하고, 묶어 내는 것이 문서의 권장이다
        // (https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-resourcebarrier "Transitions should be batched together").
        bool bReported = false;
        for ( uint32 index = 0; index < count; ++index )
        {
            const RHITextureTransition& transition = pTransition[index];
            if ( transition._texture == kRHIBackBufferTexture )
            {
                // 백버퍼. 뎁스로 쓰이는 일은 없다. 스왑체인은 제 상태를 들고 따로 낸다.
                if ( transition._state == RHITextureState::RenderTarget )
                    _pDevice->_swapChain.transitionTo( commandListForRecord(), D3D12_RESOURCE_STATE_RENDER_TARGET );
                continue;
            }
            D3D12_RESOURCE_STATES state{};
            if ( findTextureTransitionState( transition, state ) == false || appendTextureBarriers( transition._texture, state, transition._range ) == 0 )
                continue;
            if ( bReported == false )
            {
                _pDevice->reportBarrierDuringRecording( "transitionTextures" );
                bReported = true;
            }
        }
        flushBarrierScratch();
    }

    // 길이 유지(긴 함수 정리 기준): 같은 포맷 복사와 셰이더 블릿의 상태 전이 순서가 곧 의미라 나누지 않는다.
    void D3D12RHICommandContext::blitTextureNative( RHITextureHandle src, RHITextureHandle dst )
    {
        if ( _pCmdList == nullptr || ( src == kRHIBackBufferTexture && dst == kRHIBackBufferTexture ) )
            return;

        if ( src == kRHIBackBufferTexture )
        {
            // 백버퍼 → 텍스처(창에 나갈 그림을 읽는다). 스왑체인 상태는 스왑체인 객체만 바꾼다.
            ID3D12Resource* pBack = _pDevice->_swapChain.getCurrentBackBuffer();
            ID3D12Resource* pDst  = _pDevice->resolveTexture( dst );
            if ( pBack == nullptr || pDst == nullptr )
                return;
            const D3D12_RESOURCE_DESC backDesc   = pBack->GetDesc();
            const D3D12_RESOURCE_DESC dstDesc    = pDst->GetDesc();
            const bool                bSameShape = backDesc.Format == dstDesc.Format && backDesc.Width == dstDesc.Width && backDesc.Height == dstDesc.Height;
            if ( bSameShape == false )
            {
                SW_LOG_ERROR_ID( "D3D12RHICommandContext.BlitTextureBackbufferTargetMust", "blitTexture(backbuffer): the target must match the back buffer (fmt %# %#x%# vs %# %#x%#)", static_cast<uint32>( backDesc.Format ),
                                 static_cast<uint32>( backDesc.Width ), backDesc.Height, static_cast<uint32>( dstDesc.Format ), static_cast<uint32>( dstDesc.Width ),
                                 dstDesc.Height );
                return;
            }
            _pDevice->reportBarrierDuringRecording( "blitTexture(backbuffer)" );
            const D3D12_RESOURCE_STATES backStateBefore = _pDevice->_swapChain.getState();
            _pDevice->_swapChain.transitionTo( commandListForRecord(), D3D12_RESOURCE_STATE_COPY_SOURCE );
            transitionTexture( dst, D3D12_RESOURCE_STATE_COPY_DEST );
            commandListForRecord()->CopyResource( pDst, pBack );
            _pDevice->_swapChain.transitionTo( commandListForRecord(), backStateBefore );
            return;
        }

        ID3D12Resource* pSrcResource = _pDevice->resolveTexture( src );
        if ( pSrcResource == nullptr )
            return;

        D3D12RHIDevice::OffscreenTargetView srcView{};
        if ( _pDevice->findOffscreenTargetView( src, 0, 0, srcView ) == false || srcView._bHasDSV != SW_FALSE )
            return;

        _pDevice->reportBarrierDuringRecording( "blitTexture(src)" );
        transitionTexture( src, D3D12_RESOURCE_STATE_COPY_SOURCE );

        ID3D12Resource*       pDstResource   = nullptr;
        D3D12_RESOURCE_STATES dstStateBefore = D3D12_RESOURCE_STATE_COMMON;
        D3D12_RESOURCE_STATES dstStateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;

        if ( dst == kRHIBackBufferTexture )
        {
            pDstResource = _pDevice->_swapChain.getCurrentBackBuffer();
            if ( pDstResource == nullptr )
                return;
            dstStateBefore = _pDevice->_swapChain.getState();
            dstStateAfter  = D3D12_RESOURCE_STATE_PRESENT;
        }
        else
        {
            pDstResource = _pDevice->resolveTexture( dst );
            if ( pDstResource == nullptr )
                return;
            D3D12RHIDevice::OffscreenTargetView dstView{};
            if ( _pDevice->findOffscreenTargetView( dst, 0, 0, dstView ) == false || dstView._bHasDSV != SW_FALSE )
                return;
        }

        // CopyResource 는 포맷과 크기가 완전히 같아야 한다. 검증 없이 발행하면 포맷이나 해상도가 다른 조합
        // (예: R16G16B16A16_FLOAT 트랜지언트 → R8G8B8A8 백버퍼, 1280 → 320)이 정의되지 않은 동작이 된다.
        // 검증 레이어는 오류를 내고 드라이버는 DXGI_ERROR_DRIVER_INTERNAL_ERROR 로 디바이스를 날린다.
        {
            const D3D12_RESOURCE_DESC srcDesc             = pSrcResource->GetDesc();
            const D3D12_RESOURCE_DESC dstDesc             = pDstResource->GetDesc();
            const bool                bBlitFormatMismatch = srcDesc.Format != dstDesc.Format || srcDesc.Width != dstDesc.Width ||
                                             srcDesc.Height != dstDesc.Height || srcDesc.DepthOrArraySize != dstDesc.DepthOrArraySize ||
                                             srcDesc.MipLevels != dstDesc.MipLevels;
            if ( bBlitFormatMismatch )
            {
                // 병렬 기록 워커가 함께 오므로 확인과 표시를 한 번의 교환으로 한다.
                if ( _pDevice->_bBlitMismatchLogged.exchange( SW_TRUE, std::memory_order_relaxed ) == SW_FALSE )
                {
                    SW_LOG_ERROR_ID( "D3D12RHICommandContext.BlitTextureCopyResourceNotPossible", "blitTexture: CopyResource not possible - src(fmt=%# %#x%#) dst(fmt=%# %#x%#). Skipping the copy.",
                                     static_cast<uint32>( srcDesc.Format ), static_cast<uint32>( srcDesc.Width ), static_cast<uint32>( srcDesc.Height ),
                                     static_cast<uint32>( dstDesc.Format ), static_cast<uint32>( dstDesc.Width ), static_cast<uint32>( dstDesc.Height ) );
                }
                return;
            }
        }

        // 스왑체인 백버퍼(`dst == kRHIBackBufferTexture`)의 상태는 스왑체인 객체만 바꾼다. 오프스크린은 추적 상태가 서브리소스마다 옮긴다(R-10 — 예전엔 한 값을 읽고 덮어썼다).
        if ( dst == kRHIBackBufferTexture )
        {
            if ( dstStateBefore != D3D12_RESOURCE_STATE_COPY_DEST )
                _pDevice->_swapChain.transitionTo( commandListForRecord(), D3D12_RESOURCE_STATE_COPY_DEST );
        }
        else
            transitionTexture( dst, D3D12_RESOURCE_STATE_COPY_DEST );

        commandListForRecord()->CopyResource( pDstResource, pSrcResource );
        transitionTexture( src, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE ); // 계약: 원본은 셰이더 읽기로 남는다(`IRHIPassCommandList::blitTexture`)

        if ( dst == kRHIBackBufferTexture )
            _pDevice->_swapChain.transitionTo( commandListForRecord(), dstStateAfter );
        else
            transitionTexture( dst, dstStateAfter );
    }

    // 단일 전이도 묶음과 같은 길(원소 하나)을 지난다 — 상태 표 · 건너뛰기 규칙이 한 벌이다(버퍼의 `transitionBufferNative` 와 같다).
    void D3D12RHICommandContext::prepareTextureForShaderReadNative( RHITextureHandle texture )
    {
        const RHITextureTransition transition{ texture, RHITextureState::ShaderResource };
        transitionTexturesNative( &transition, 1 );
    }

    void D3D12RHICommandContext::prepareTextureForUnorderedAccessNative( RHITextureHandle texture )
    {
        const RHITextureTransition transition{ texture, RHITextureState::UnorderedAccess };
        transitionTexturesNative( &transition, 1 );
    }

    void D3D12RHICommandContext::prepareTextureForRenderTargetNative( RHITextureHandle texture )
    {
        const RHITextureTransition transition{ texture, RHITextureState::RenderTarget };
        transitionTexturesNative( &transition, 1 );
    }

    void D3D12RHICommandContext::drawNative( uint32 vertexCount, uint32 startVertex )
    {
        drawInstancedNative( vertexCount, 1, startVertex, 0 );
    }

    void D3D12RHICommandContext::drawInstancedNative( uint32 vertexCount, uint32 instanceCount, uint32 startVertex, uint32 startInstance )
    {
        const bool bNothingToDraw = _pCmdList == nullptr || vertexCount == 0 || instanceCount == 0;
        if ( bNothingToDraw || prepareDraw( false ) == false )
            return;
        commandListForRecord()->DrawInstanced( vertexCount, instanceCount, startVertex, startInstance );
    }

    void D3D12RHICommandContext::updateConstantBufferNative( RHIBufferHandle buffer, const void* pData, uint32 size )
    {
        if ( _pDevice != nullptr )
            _pDevice->_resourceImpl->writeConstantBuffer( buffer, pData, size );
    }

    void D3D12RHICommandContext::dispatchComputeNative( uint32 threadGroupCountX, uint32 threadGroupCountY, uint32 threadGroupCountZ )
    {
        if ( _pCmdList == nullptr )
            return;
        if ( prepareDispatch() == false )
            return;
        commandListForRecord()->Dispatch( threadGroupCountX, threadGroupCountY, threadGroupCountZ );
    }

    void D3D12RHICommandContext::setViewportNative( const RHIViewport& viewport )
    {
        D3D12_VIEWPORT d3dViewport{};
        d3dViewport.TopLeftX = viewport._x;
        d3dViewport.TopLeftY = viewport._y;
        d3dViewport.Width    = viewport._width;
        d3dViewport.Height   = viewport._height;
        d3dViewport.MinDepth = viewport._minDepth;
        d3dViewport.MaxDepth = viewport._maxDepth;
        commandListForRecord()->RSSetViewports( 1, &d3dViewport );
    }

    void D3D12RHICommandContext::setScissorRectNative( const RHIScissorRect& rect )
    {
        const D3D12_RECT scissor{ static_cast<LONG>( rect._x ), static_cast<LONG>( rect._y ), static_cast<LONG>( rect._x + rect._width ),
                                  static_cast<LONG>( rect._y + rect._height ) };
        commandListForRecord()->RSSetScissorRects( 1, &scissor );
    }

    void D3D12RHICommandContext::drawIndirectNative( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset, uint32 drawCount,
                                                     RHIBufferHandle countBuffer, uint32 countBufferOffset )
    {
        const bool bNothingToDrawIndirect = _pCmdList == nullptr || _pDevice->_drawCommandSignature == nullptr || argumentBuffer == 0 || drawCount == 0;
        if ( bNothingToDrawIndirect )
            return;

        ID3D12Resource* pArgs = _pDevice->resolveBuffer( argumentBuffer );
        if ( pArgs == nullptr )
            return;

        if ( prepareDraw( false ) == false )
            return;

        // countBuffer 가 있으면 GPU 가 적어 둔 개수를 쓴다(drawCount 는 상한). ExecuteIndirect 는 둘을 같이 받는다.
        ID3D12Resource* pCountResource = ( countBuffer != 0 ) ? _pDevice->resolveBuffer( countBuffer ) : nullptr;
        commandListForRecord()->ExecuteIndirect( _pDevice->_drawCommandSignature.Get(), drawCount, pArgs, argumentBufferOffset, pCountResource,
                                                 ( pCountResource != nullptr ) ? countBufferOffset : 0 );
    }

    void D3D12RHICommandContext::drawIndexedInstancedNative( uint32 indexCount, uint32 instanceCount, uint32 startIndex, int32 baseVertex, uint32 startInstance )
    {
        const bool bNothingToDraw = _pCmdList == nullptr || indexCount == 0 || instanceCount == 0;
        if ( bNothingToDraw || prepareDraw( true ) == false )
            return;
        commandListForRecord()->DrawIndexedInstanced( indexCount, instanceCount, startIndex, baseVertex, startInstance );
    }

    void D3D12RHICommandContext::drawIndexedIndirectNative( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset, uint32 drawCount, RHIBufferHandle countBuffer,
                                                            uint32 countBufferOffset )
    {
        const bool bCannotDrawIndexedIndirect = _pCmdList == nullptr || _pDevice->_drawIndexedCommandSignature == nullptr || argumentBuffer == 0 || drawCount == 0;
        if ( bCannotDrawIndexedIndirect )
            return;

        ID3D12Resource* pArgs = _pDevice->resolveBuffer( argumentBuffer );
        if ( pArgs == nullptr )
            return;

        if ( prepareDraw( true ) == false )
            return;
        // 개수 버퍼가 있으면 GPU 가 적어 둔 개수를 쓴다(drawCount 는 상한) — `drawIndirect` 와 같다.
        ID3D12Resource* pCountResource = ( countBuffer != 0 ) ? _pDevice->resolveBuffer( countBuffer ) : nullptr;
        commandListForRecord()->ExecuteIndirect( _pDevice->_drawIndexedCommandSignature.Get(), drawCount, pArgs, argumentBufferOffset, pCountResource,
                                                 ( pCountResource != nullptr ) ? countBufferOffset : 0 );
    }

    void D3D12RHICommandContext::dispatchIndirectNative( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset )
    {
        const bool bCannotDispatchIndirect = _pCmdList == nullptr || _pDevice->_dispatchCommandSignature == nullptr || argumentBuffer == 0;
        if ( bCannotDispatchIndirect )
            return;

        ID3D12Resource* pArgs = _pDevice->resolveBuffer( argumentBuffer );
        if ( pArgs == nullptr || prepareDispatch() == false )
            return;
        commandListForRecord()->ExecuteIndirect( _pDevice->_dispatchCommandSignature.Get(), 1, pArgs, argumentBufferOffset, nullptr, 0 );
    }

    void D3D12RHICommandContext::writeBreadcrumb( uint32 markerID )
    {
        const D3D12_GPU_VIRTUAL_ADDRESS address = _pDevice->getBreadcrumbAddress( _bComputeQueueList != SW_FALSE ); // 큐마다 제 버퍼 — 큐 사이 경합이 없다
        if ( _pCmdList == nullptr || address == 0 )
            return;
        // WriteBufferImmediate 는 ID3D12GraphicsCommandList2 다 - 없는 런타임은 표지를 쓰지 않는다.
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> list2;
        if ( FAILED( commandListForRecord()->QueryInterface( IID_PPV_ARGS( list2.GetAddressOf() ) ) ) )
            return;
        // 시작 표지는 [0] 칸에 MARKER_IN(앞선 명령이 모두 **시작**한 뒤), 끝 표지(GPUBreadcrumbs::kEndMarkerBit)는 [1] 칸에 MARKER_OUT(앞선 명령이 모두 **끝난** 뒤)으로 쓴다.
        // 소실 뒤 [0] 은 GPU 가 시작한 마지막 패스, [1] 은 끝낸 마지막 패스다.
        // https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_writebufferimmediate_mode
        const bool                                 bEnd = ( markerID & GPUBreadcrumbs::kEndMarkerBit ) != 0;
        const D3D12_WRITEBUFFERIMMEDIATE_PARAMETER param{ address + ( bEnd ? sizeof( uint32 ) : 0u ), markerID };
        const D3D12_WRITEBUFFERIMMEDIATE_MODE      mode = bEnd ? D3D12_WRITEBUFFERIMMEDIATE_MODE_MARKER_OUT : D3D12_WRITEBUFFERIMMEDIATE_MODE_MARKER_IN;
        list2->WriteBufferImmediate( 1, &param, &mode );
    }

    void D3D12RHICommandContext::beginEventMarker( const utf8* pName )
    {
        if ( _pCmdList == nullptr || pName == nullptr )
            return;
    #if defined( SW_HAS_PIX )
        PIXBeginEvent( _pCmdList, 0, "%s", pName );
    #else
        (void)pName;
    #endif
    }

    void D3D12RHICommandContext::endEventMarker()
    {
        if ( _pCmdList == nullptr )
            return;
    #if defined( SW_HAS_PIX )
        PIXEndEvent( _pCmdList );
    #endif
    }

    // 길이 유지(긴 함수 정리 기준): 상태 전이 · 시작 접근 채우기 · 패스 시작 · 뷰포트의 순서가 곧 의미라 나누지 않는다.
    RHIRenderPassBeginResult D3D12RHICommandContext::beginRenderPassNative( const RHIRenderPassBeginInfo& beginInfo, RHIRenderPassExtent& outTargetExtent )
    {
        // 네이티브 렌더 패스다(`ID3D12GraphicsCommandList4::BeginRenderPass` — Vulkan · 언리얼 D3D12RHI 와 같은 모양). 상태 전이는 패스를 열기 전에 끝내고, 지우기는
        // 시작 접근(CLEAR)으로 넘긴다 — 패스 안에서는 배리어 · 복사 · Clear 호출을 할 수 없다(디버그 레이어가 잡는다). 예전에는 OMSetRenderTargets + Clear*View 로
        // 흉내 내 패스 안의 배리어 · 업로드가 DX12 에서만 그냥 지나갔고, load op 의 Load · DontCare 는 뜻이 사라졌다.
        ensureRecording();
        if ( _pCmdList == nullptr )
            return RHIRenderPassBeginResult::Failed;

        const bool bBindColor = beginInfo._bBindColor != SW_FALSE;
        const bool bHasDepth  = beginInfo._depthTarget != 0;
        if ( bBindColor == false && bHasDepth == false )
            return RHIRenderPassBeginResult::Failed;

        D3D12_RENDER_PASS_RENDER_TARGET_DESC arrColorDesc[kMaxColorAttachments]{};
        uint32                               rtCount{ 0 };
        _pState->_bActiveSwapchainRT = SW_FALSE;

        const bool         bBackBuffer = bBindColor && beginInfo._arrColorTarget[0] == kRHIBackBufferTexture;
        D3D12RHISwapChain& swapChain   = bBackBuffer ? _pDevice->useSwapChain( beginInfo._pSwapChain ) : _pDevice->_swapChain;
        outTargetExtent                = RHIRenderPassExtent{ swapChain.getWidth(), swapChain.getHeight() };
        const uint32 colorBindCount    = ( beginInfo._colorTargetCount > 0 ) ? beginInfo._colorTargetCount : ( bBindColor ? 1u : 0u );
        for ( uint32 attachmentIndex = 0; attachmentIndex < colorBindCount && attachmentIndex < kMaxColorAttachments; ++attachmentIndex )
        {
            const RHITextureHandle      colorHandle = beginInfo._arrColorTarget[attachmentIndex];
            D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
            DXGI_FORMAT                 format{ DXGI_FORMAT_UNKNOWN };
            if ( colorHandle == kRHIBackBufferTexture )
            {
                if ( attachmentIndex > 0 )
                    break;
                if ( swapChain.isBackBufferReady() == false )
                    return RHIRenderPassBeginResult::NoBackBuffer; // 그릴 백버퍼가 없다(최소화 · 스왑체인 재생성 중) — 이 패스의 드로우는 알리지 않고 버린다
                swapChain.transitionTo( commandListForRecord(), D3D12_RESOURCE_STATE_RENDER_TARGET );
                rtv                                     = swapChain.getCurrentRTV();
                format                                  = toDXGIFormat( swapChain.getFormat() );
                _pState->_bActiveSwapchainRT            = SW_TRUE;
                _pState->_arrActiveColorTarget[rtCount] = kRHIBackBufferTexture;
            }
            else
            {
                const uint32                        slice = beginInfo._arrColorTargetSlice[attachmentIndex];
                const uint32                        mip   = beginInfo._arrColorTargetMip[attachmentIndex];
                D3D12RHIDevice::OffscreenTargetView view{};
                const bool                          bViewMissing = _pDevice->findOffscreenTargetView( colorHandle, slice, mip, view ) == false || view._bHasRTV == SW_FALSE;
                if ( bViewMissing )
                {
                    if ( attachmentIndex > 0 )
                        break;
                    return RHIRenderPassBeginResult::Failed;
                }
                _pDevice->reportBarrierDuringRecording( "beginRenderPass(color)" );
                // 그리는 밉만 옮긴다(면은 전체 — 예전과 같다). 같은 텍스처의 다른 밉을 읽는 다운샘플 체인이 그 밉의 상태를 지킨다(R-10).
                transitionTexture( colorHandle, D3D12_RESOURCE_STATE_RENDER_TARGET, RHISubresourceRange{ static_cast<uint16>( mip ), 1u, 0u, RHISubresourceRange::kAll } );
                if ( view._bSliceInRange == SW_FALSE )
                {
                    if ( attachmentIndex > 0 )
                        break;
                    return RHIRenderPassBeginResult::Failed;
                }
                if ( attachmentIndex == 0 )
                    outTargetExtent = view._extent;
                rtv                                     = view._rtvHandle;
                format                                  = view._format;
                _pState->_arrActiveColorTarget[rtCount] = colorHandle;
            }

            D3D12_RENDER_PASS_RENDER_TARGET_DESC& colorDesc = arrColorDesc[rtCount++];
            colorDesc.cpuDescriptor                         = rtv;
            colorDesc.BeginningAccess.Type                  = D3D12RHICommandContextInternal::toBeginningAccess( beginInfo._arrLoadOp[attachmentIndex] );
            if ( colorDesc.BeginningAccess.Type == D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR )
            {
                colorDesc.BeginningAccess.Clear.ClearValue.Format = format;
                Memory::copy( colorDesc.BeginningAccess.Clear.ClearValue.Color, &beginInfo._arrClearColor[attachmentIndex]._x, sizeof( float32 ) * 4u );
            }
            colorDesc.EndingAccess.Type = D3D12RHICommandContextInternal::toEndingAccess( beginInfo._arrStoreOp[attachmentIndex] );
        }

        D3D12_RENDER_PASS_DEPTH_STENCIL_DESC depthDesc{};
        bool                                 bDepthBound{ false };
        _pState->_activeDepthTarget = 0;
        if ( bHasDepth )
        {
            D3D12RHIDevice::OffscreenTargetView depthView{};
            const bool                          bDepthFound          = _pDevice->findOffscreenTargetView( beginInfo._depthTarget, beginInfo._depthTargetSlice, beginInfo._depthTargetMip, depthView );
            const bool                          bDepthTargetWritable = bDepthFound && depthView._bHasDSV != SW_FALSE && depthView._bSliceInRange != SW_FALSE;
            if ( bDepthTargetWritable )
            {
                _pDevice->reportBarrierDuringRecording( "beginRenderPass(depth)" );
                transitionTexture( beginInfo._depthTarget, D3D12_RESOURCE_STATE_DEPTH_WRITE, RHISubresourceRange{ beginInfo._depthTargetMip, 1u, 0u, RHISubresourceRange::kAll } );
                _pState->_activeDepthTarget = beginInfo._depthTarget;
                bDepthBound                 = true;
                if ( bBindColor == false )
                    outTargetExtent = depthView._extent;
                depthDesc.cpuDescriptor             = depthView._dsvHandle;
                depthDesc.DepthBeginningAccess.Type = D3D12RHICommandContextInternal::toBeginningAccess( beginInfo._depthLoadOp );
                depthDesc.DepthEndingAccess.Type    = D3D12RHICommandContextInternal::toEndingAccess( beginInfo._depthStoreOp );
                if ( depthDesc.DepthBeginningAccess.Type == D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR )
                {
                    depthDesc.DepthBeginningAccess.Clear.ClearValue.Format               = depthView._format;
                    depthDesc.DepthBeginningAccess.Clear.ClearValue.DepthStencil.Depth   = beginInfo._clearDepth;
                    depthDesc.DepthBeginningAccess.Clear.ClearValue.DepthStencil.Stencil = beginInfo._clearStencil;
                }
                // 스텐실이 있는 포맷이면 깊이와 같이 다룬다(예전 ClearDepthStencilView 가 둘 다 지웠다). 없는 포맷은 접근하지 않는다.
                const bool bStencil = depthView._format == DXGI_FORMAT_D24_UNORM_S8_UINT || depthView._format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
                if ( bStencil )
                {
                    depthDesc.StencilBeginningAccess = depthDesc.DepthBeginningAccess;
                    depthDesc.StencilEndingAccess    = depthDesc.DepthEndingAccess;
                }
                else
                {
                    depthDesc.StencilBeginningAccess.Type = D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_NO_ACCESS;
                    depthDesc.StencilEndingAccess.Type    = D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_NO_ACCESS;
                }
            }
        }
        if ( rtCount == 0 && bDepthBound == false )
            return RHIRenderPassBeginResult::Failed;

        D3D12RHICommandContextInternal::asRenderPassList( commandListForRecord() )
            ->BeginRenderPass( rtCount, rtCount > 0 ? arrColorDesc : nullptr, bDepthBound ? &depthDesc : nullptr, D3D12_RENDER_PASS_FLAG_NONE );

        return RHIRenderPassBeginResult::Begun;
    }

    void D3D12RHICommandContext::endRenderPassNative()
    {
        D3D12RHICommandContextInternal::asRenderPassList( commandListForRecord() )->EndRenderPass();
        _pState->_activeDepthTarget  = 0;
        _pState->_bActiveSwapchainRT = SW_FALSE;
    }

    void D3D12RHICommandContext::beginTextureAliasNative( RHITextureHandle texture )
    {
        if ( _pCmdList == nullptr )
            return;
        ID3D12Resource* pResource = _pDevice->resolveTexture( texture );
        if ( pResource == nullptr )
            return;
        // 앞 리소스를 특정하지 않는 앨리어싱 배리어 — 같은 힙 바이트를 쓴 어떤 placed 리소스의 일도 이 리소스의 쓰기 앞에 끝난다(D3D12 앨리어싱 규칙).
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type                     = D3D12_RESOURCE_BARRIER_TYPE_ALIASING;
        barrier.Aliasing.pResourceBefore = nullptr;
        barrier.Aliasing.pResourceAfter  = pResource;
        commandListForRecord()->ResourceBarrier( 1, &barrier );
    }

    void D3D12RHICommandContext::uavBarrierNative( RHIBufferHandle buffer )
    {
        if ( _pCmdList == nullptr || buffer == 0 )
            return;
        ID3D12Resource* pResource = _pDevice->resolveBuffer( buffer );
        if ( pResource == nullptr )
            return;

        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type          = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        barrier.UAV.pResource = pResource;
        commandListForRecord()->ResourceBarrier( 1, &barrier );
    }

    void D3D12RHICommandContext::uavBarrierTextureNative( RHITextureHandle texture )
    {
        if ( _pCmdList == nullptr )
            return;
        ID3D12Resource* pResource = _pDevice->resolveTexture( texture );
        if ( pResource == nullptr )
            return;
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type          = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        barrier.UAV.pResource = pResource;
        commandListForRecord()->ResourceBarrier( 1, &barrier );
    }

    void D3D12RHICommandContext::transitionBufferNative( RHIBufferHandle buffer, RHIBufferState newState )
    {
        const RHIBufferTransition transition{ buffer, newState };
        transitionBuffersNative( &transition, 1 );
    }

    void D3D12RHICommandContext::transitionBuffersNative( const RHIBufferTransition* pTransition, uint32 count )
    {
        if ( _pCmdList == nullptr )
            return;
        // 모은 전이를 ResourceBarrier 한 번에 낸다 — 배리어마다 부르면 드라이버가 그때마다 캐시 플러시 · 대기를 따로 푼다.
        // https://learn.microsoft.com/en-us/windows/win32/direct3d12/using-resource-barriers-to-synchronize-resource-states-in-direct3d-12 (배리어는 묶어 내라)
        constexpr uint32       kMaxBarrierPerCall = 16;
        D3D12_RESOURCE_BARRIER arrBarrier[kMaxBarrierPerCall]{};
        uint32                 barrierCount = 0;
        for ( uint32 index = 0; index < count; ++index )
        {
            const RHIBufferHandle               buffer  = pTransition[index]._buffer;
            D3D12RHIDevice::BufferRecord* const pRecord = buffer != 0 ? _pDevice->findBufferRecord( buffer ) : nullptr;
            if ( pRecord == nullptr || pRecord->_resource == nullptr || pRecord->_bTrackedState == SW_FALSE )
                continue;
            ID3D12Resource* const       pResource  = pRecord->_resource.Get();
            const D3D12_RESOURCE_STATES stateAfter = toQueueState( toD3D12BufferState( pTransition[index]._state ) );
            D3D12_RESOURCE_STATES       stateBefore{};
            if ( pRecord->_state.transitionTo( stateAfter, stateBefore ) == false )
                continue;
            // 컴퓨트 큐 리스트는 따로 낸 ExecuteCommandLists 라 버퍼가 COMMON 으로 감쇠해 들어온다. 기록한 상태에 그래픽스 전용 비트가 있으면 COMMON 을 앞 상태로 쓴다
            // (COMMON 또는 승격 가능한 상태를 앞 상태로 쓰는 것이 문서의 허용 범위다 — 그래픽스 전용 비트는 COMPUTE 리스트의 배리어에 적을 수 없다).
            if ( _bComputeQueueList != SW_FALSE && toQueueState( stateBefore ) != stateBefore )
                stateBefore = D3D12_RESOURCE_STATE_COMMON;
            if ( stateBefore == stateAfter )
                continue;
            D3D12_RESOURCE_BARRIER& barrier = arrBarrier[barrierCount++];
            barrier.Type                    = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Flags                   = D3D12_RESOURCE_BARRIER_FLAG_NONE;
            barrier.Transition.pResource    = pResource;
            barrier.Transition.StateBefore  = stateBefore;
            barrier.Transition.StateAfter   = stateAfter;
            barrier.Transition.Subresource  = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            if ( barrierCount == kMaxBarrierPerCall )
            {
                commandListForRecord()->ResourceBarrier( barrierCount, arrBarrier );
                barrierCount = 0;
            }
        }
        if ( barrierCount > 0 )
            commandListForRecord()->ResourceBarrier( barrierCount, arrBarrier );
    }

    void D3D12RHICommandContext::copyBufferToReadbackNative( RHIBufferHandle source, RHIBufferHandle readback, uint32 sizeBytes )
    {
        const bool bNothingToCopy = _pCmdList == nullptr || source == 0 || readback == 0 || sizeBytes == 0;
        if ( bNothingToCopy )
            return;
        ID3D12Resource* pSource = _pDevice->resolveBuffer( source );
        ID3D12Resource* pTarget = _pDevice->resolveBuffer( readback );
        if ( pSource == nullptr || pTarget == nullptr )
            return;
        // 읽기 힙 버퍼는 만들 때부터 COPY_DEST 로 둔다(만든 곳 `createReadbackBuffer`) — 배리어가 없다. 원본은 부르는 쪽이 COPY_SOURCE 로 옮겨 두었다.
        commandListForRecord()->CopyBufferRegion( pTarget, 0, pSource, 0, sizeBytes );
    }

    uint64 D3D12RHICommandContext::findBufferBytes( RHIBufferHandle buffer ) const
    {
        ID3D12Resource* const pBuffer = _pDevice->resolveBuffer( buffer );
        return pBuffer != nullptr ? pBuffer->GetDesc().Width : 0;
    }

    void D3D12RHICommandContext::copyBufferRegionNative( RHIBufferHandle destination, uint64 destinationOffset, RHIBufferHandle source, uint64 sourceOffset, uint64 sizeBytes )
    {
        if ( _pCmdList == nullptr )
            return;
        ID3D12Resource* pSource = _pDevice->resolveBuffer( source );
        ID3D12Resource* pTarget = _pDevice->resolveBuffer( destination );
        if ( pSource == nullptr || pTarget == nullptr )
            return;
        // 원본은 COPY_SOURCE, 도착은 COPY_DEST 로 부르는 쪽이 옮겨 두었다(`transitionBuffers`). 범위 밖 · 같은 버퍼는 공용 문맥이 걸렀다.
        commandListForRecord()->CopyBufferRegion( pTarget, destinationOffset, pSource, sourceOffset, sizeBytes );
    }

} // namespace sw
#endif
