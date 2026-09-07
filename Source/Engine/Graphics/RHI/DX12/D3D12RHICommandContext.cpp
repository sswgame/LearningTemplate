#include "pch.h"

#include "Engine/Graphics/RHI/DX12/D3D12RHICommandContext.h"

#include "Engine/Graphics/RHI/DX12/D3D12RHIDevice.h"
#include "Engine/Graphics/Shader/ShaderBindingSlots.h"

#if defined( SW_PLATFORM_WINDOWS )
    #if __has_include( <pix3.h> )
        #include <pix3.h>
        #define SW_HAS_PIX 1
    #endif

namespace sw
{
    namespace
    {
        struct D3D12RHICommandContextInternal
        {
            static D3D12_RESOURCE_STATES toD3D12BufferState( RHIBufferState state )
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
                        return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
                    case RHIBufferState::Index:
                        return D3D12_RESOURCE_STATE_INDEX_BUFFER;
                    case RHIBufferState::Common:
                    default:
                        return D3D12_RESOURCE_STATE_COMMON;
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    void D3D12RHICommandContext::ensureRecording()
    {
        if ( _pDevice == nullptr || _pState->_bRecording != 0 )
            return;
        _pDevice->waitForRingSlot();
        ID3D12CommandAllocator* pAllocator = _pDevice->currentAllocator();
        if ( pAllocator == nullptr || _pCmdList == nullptr )
            return;
        pAllocator->Reset();
        _pCmdList->Reset( pAllocator, nullptr );
        _pState->_bRecording             = 1;
        _pState->_boundNativeGraphicsPso = 0;                     // 새 리스트엔 아직 아무 PSO도 안 걸림 — 캐시 무효화.
        _pState->_arrSlotState[0]        = D3D12SlotTableState{}; // 새 리스트엔 슬롯 테이블도 없다 — 첫 드로우가 다시 굳힌다.
        _pState->_arrSlotState[1]        = D3D12SlotTableState{};
        // 힙·루트 시그니처·텍스처 배열 테이블은 리스트가 열릴 때 한 번 — 이후 bind*() 는 루트 디스크립터(GPU 주소)만 쓴다.
        _pDevice->bindBindlessRootState( _pCmdList );
    }

    D3D12_GPU_VIRTUAL_ADDRESS D3D12RHICommandContext::resolveBufferAddress( RHIDescriptorIndex index, bool bUav, bool bConstantBuffer ) const
    {
        if ( index == kInvalidDescriptorIndex )
            return 0;

        // 락이 없다. 레지스트리는 기록 중에 **바뀌지 않는다** — 등록/해제는 전부 그래프 셋업에서
        // 끝내고, 그 규칙은 checkRegistryMutableNow 가 디버그에서 감시한다
        // (IRHIDevice::setParallelRecording 참고). 드로우마다 도는 경로라 락을 거는 대신 애초에
        // 공유하지 않는 쪽을 택했다. const 참조로 받는 것도 중요하다 — 비-const 접근은 "쓰기" 로 취급된다.
        const vector<D3D12RHIDevice::BindlessResourceRecord>& listRegistry =
            bUav ? _pDevice->_listRegisteredUAV : _pDevice->_listRegisteredBindless;
        if ( index >= static_cast<RHIDescriptorIndex>( listRegistry.size() ) )
            return 0;
        const D3D12RHIDevice::BindlessResourceRecord& rec = listRegistry[index];
        if ( rec._resource == nullptr )
            return 0;

        D3D12_GPU_VIRTUAL_ADDRESS address = rec._resource->GetGPUVirtualAddress();
        if ( bConstantBuffer )
        {
            // 링 상수버퍼(createConstantBuffer)는 프레임 슬롯마다 정렬 크기만큼 떨어진 자리에 쓴다 —
            // updateConstantBuffer 가 이번 프레임 슬롯에 썼으므로 같은 슬롯 주소를 건다.
            const auto sizeIt = _pDevice->_mapCbAlignedSize.find( rec._buffer );
            if ( sizeIt != _pDevice->_mapCbAlignedSize.end() )
                address += static_cast<D3D12_GPU_VIRTUAL_ADDRESS>( _pDevice->_frameRing.currentIndex() ) * sizeIt->second;
        }
        return address;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE D3D12RHICommandContext::resolveOfflineView( RHIDescriptorIndex index, bool bUav ) const
    {
        D3D12_CPU_DESCRIPTOR_HANDLE none{};
        if ( index == kInvalidDescriptorIndex )
            return none;
        // resolveBufferAddress 와 같은 이유로 락이 없다 — 레지스트리는 기록 중 불변이다.
        const vector<D3D12RHIDevice::BindlessResourceRecord>& listRegistry =
            bUav ? _pDevice->_listRegisteredUAV : _pDevice->_listRegisteredBindless;
        if ( index >= static_cast<RHIDescriptorIndex>( listRegistry.size() ) )
            return none;
        const D3D12RHIDevice::BindlessResourceRecord& rec = listRegistry[index];
        if ( rec._resource == nullptr )
            return none;
        return rec._offlineCpuHandle;
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
        if ( count == 0 || count > D3D12RHIDevice::kMaxSlotTableSize || nullView.ptr == 0 )
            return false;
        uint32 base{ 0 };
        if ( allocateOnlineDescriptors( count, base ) == false )
            return false;

        // 원본은 슬롯마다 흩어져 있고(오프라인 힙 여기저기) 목적지는 연속 구간 하나다 — CopyDescriptors 의 N:1 형태.
        D3D12_CPU_DESCRIPTOR_HANDLE arrSrc[D3D12RHIDevice::kMaxSlotTableSize]{};
        UINT                        arrSrcSize[D3D12RHIDevice::kMaxSlotTableSize]{};
        for ( uint32 slot = 0; slot < count; ++slot )
        {
            arrSrc[slot]     = ( pSlots[slot].ptr != 0 ) ? pSlots[slot] : nullView;
            arrSrcSize[slot] = 1;
        }
        const D3D12_CPU_DESCRIPTOR_HANDLE dst     = _pDevice->shaderVisibleCpuAt( base );
        const UINT                        dstSize = count;
        _pDevice->_device->CopyDescriptors( 1, &dst, &dstSize, count, arrSrc, arrSrcSize, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
        outTable = _pDevice->shaderVisibleGpuAt( base );
        return true;
    }

    void D3D12RHICommandContext::flushSlotTables( bool bCompute )
    {
        if ( _pCmdList == nullptr || _pDevice->_rootSignature == nullptr )
            return;
        D3D12SlotTableState& state = _pState->_arrSlotState[bCompute ? 1 : 0];

        if ( state._bSrvDirty != 0 )
        {
            D3D12_GPU_DESCRIPTOR_HANDLE table{};
            if ( writeSlotTable( state._arrSrv, shaderslot::kSrvSlotCount, _pDevice->offlineDescriptorAt( D3D12RHIDevice::kOfflineNullSrvIndex ), table ) )
            {
                if ( bCompute )
                    _pCmdList->SetComputeRootDescriptorTable( D3D12RHIDevice::kSrvTableParam, table );
                else
                    _pCmdList->SetGraphicsRootDescriptorTable( D3D12RHIDevice::kSrvTableParam, table );
                state._bSrvDirty = 0;
            }
        }
        // u 테이블은 컴퓨트만 쓴다 — 그래픽스 스테이지엔 UAV 선언이 없다(binding.hlsli 가 RW 텍스처를 컴퓨트에서만 선언한다).
        if ( bCompute && state._bUavDirty != 0 )
        {
            D3D12_GPU_DESCRIPTOR_HANDLE table{};
            if ( writeSlotTable( state._arrUav, shaderslot::kComputeUavSlotCount, _pDevice->offlineDescriptorAt( D3D12RHIDevice::kOfflineNullUavIndex ), table ) )
            {
                _pCmdList->SetComputeRootDescriptorTable( D3D12RHIDevice::kUavTableParam, table );
                state._bUavDirty = 0;
            }
        }
    }

    void D3D12RHICommandContext::bindMeshVertexBuffer()
    {
        ID3D12Resource* pVb = _pDevice->resolveBuffer( _pState->_boundMeshVb );
        if ( pVb == nullptr )
            return;
        D3D12_VERTEX_BUFFER_VIEW vbv{};
        vbv.BufferLocation = pVb->GetGPUVirtualAddress() + _pState->_boundMeshOffset;
        vbv.SizeInBytes    = static_cast<UINT>( pVb->GetDesc().Width > _pState->_boundMeshOffset
                                                    ? pVb->GetDesc().Width - _pState->_boundMeshOffset
                                                    : 0 );
        vbv.StrideInBytes  = _pState->_boundMeshStride;
        _pCmdList->IASetVertexBuffers( 0, 1, &vbv );
    }

    void D3D12RHICommandContext::bindMeshVertexBufferOrFallback()
    {
        if ( _pState->_boundMeshVb != 0 )
            bindMeshVertexBuffer();
        else
            bindFullscreenVertexBuffer();
    }

    void D3D12RHICommandContext::bindFullscreenVertexBuffer()
    {
        if ( _pDevice->_vertexBuffer == nullptr )
            return;
        D3D12_VERTEX_BUFFER_VIEW vbv{};
        vbv.BufferLocation = _pDevice->_vertexBuffer->GetGPUVirtualAddress();
        vbv.SizeInBytes    = static_cast<UINT>( sizeof( RHIVertex ) * 3 );
        vbv.StrideInBytes  = static_cast<UINT>( sizeof( RHIVertex ) );
        _pCmdList->IASetVertexBuffers( 0, 1, &vbv );
    }

    void D3D12RHICommandContext::bindBoundIndexBuffer()
    {
        ID3D12Resource* pIb = _pDevice->resolveBuffer( _pState->_boundIndexBuffer );
        if ( pIb == nullptr )
            return;
        D3D12_INDEX_BUFFER_VIEW ibv{};
        ibv.BufferLocation = pIb->GetGPUVirtualAddress() + _pState->_boundIndexOffset;
        ibv.SizeInBytes    = static_cast<UINT>( pIb->GetDesc().Width > _pState->_boundIndexOffset
                                                    ? pIb->GetDesc().Width - _pState->_boundIndexOffset
                                                    : 0 );
        ibv.Format         = ( _pState->_boundIndexStride == 2 ) ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
        _pCmdList->IASetIndexBuffer( &ibv );
    }

    void D3D12RHICommandContext::transitionTexture( RHITextureHandle texture, D3D12_RESOURCE_STATES newState )
    {
        ID3D12Resource* pResource = _pDevice->resolveTexture( texture );
        if ( pResource == nullptr )
            return;

        // 상태 확인과 배리어 기록이 한 덩어리여야 한다 — RenderGraph::executeParallel 이 같은 웨이브의
        // 패스 콜백을 여러 스레드에서 돌리는데, 둘이 같은 텍스처를 전이하면 둘 다 같은 "이전 상태" 를
        // 보고 각자 배리어를 쏴서 두 번째가 before==after 가 된다(검증 오류 → 디바이스 제거).
        std::scoped_lock<mutex> lock{ _pDevice->_resourceStateMutex };

        auto it = _pDevice->_mapOffscreenTexture.find( texture );
        if ( it == _pDevice->_mapOffscreenTexture.end() )
            return;
        D3D12RHIDevice::OffscreenTextureRecord& record = it->second;
        if ( record._state == newState )
            return;

        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource   = pResource;
        barrier.Transition.StateBefore = record._state;
        barrier.Transition.StateAfter  = newState;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        _pCmdList->ResourceBarrier( 1, &barrier );
        record._state = newState;
    }

    void D3D12RHICommandContext::blitTexture( RHITextureHandle src, RHITextureHandle dst )
    {
        if ( _pCmdList == nullptr || src == 0 )
            return;

        ID3D12Resource* pSrcRes = _pDevice->resolveTexture( src );
        if ( pSrcRes == nullptr )
            return;

        auto srcIt = _pDevice->_mapOffscreenTexture.find( src );
        if ( srcIt == _pDevice->_mapOffscreenTexture.end() || srcIt->second._bHasDsv != 0 )
            return;

        _pDevice->noteBarrierDuringRecording( "blitTexture(src)" );
        transitionTexture( src, D3D12_RESOURCE_STATE_COPY_SOURCE );

        ID3D12Resource*       pDstRes        = nullptr;
        D3D12_RESOURCE_STATES dstStateBefore = D3D12_RESOURCE_STATE_COMMON;
        D3D12_RESOURCE_STATES dstStateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
        RHITextureHandle      dstHandle      = dst;
        bool                  bSwapchainDst  = false;

        if ( dst == 0 )
        {
            pDstRes = _pDevice->_swapChain.getCurrentBackBuffer();
            if ( pDstRes == nullptr )
                return;
            dstStateBefore = _pDevice->_swapChain.getState();
            dstStateAfter  = D3D12_RESOURCE_STATE_PRESENT;
            bSwapchainDst  = true;
        }
        else
        {
            pDstRes = _pDevice->resolveTexture( dst );
            if ( pDstRes == nullptr )
                return;
            auto dstIt = _pDevice->_mapOffscreenTexture.find( dst );
            if ( dstIt == _pDevice->_mapOffscreenTexture.end() || dstIt->second._bHasDsv != 0 )
                return;
            dstStateBefore = dstIt->second._state;
        }

        // CopyResource 는 포맷과 크기가 완전히 같아야 한다. 예전엔 검증 없이 발행해서, 포맷이나
        // 해상도가 다른 조합(예: R16G16B16A16_FLOAT 트랜지언트 → R8G8B8A8 백버퍼, 1280 → 320)에서
        // 그대로 정의되지 않은 동작이 됐다 — 검증 레이어는 오류를 내고 드라이버는
        // DXGI_ERROR_DRIVER_INTERNAL_ERROR 로 디바이스를 날린다.
        {
            const D3D12_RESOURCE_DESC srcDesc = pSrcRes->GetDesc();
            const D3D12_RESOURCE_DESC dstDesc = pDstRes->GetDesc();
            if ( srcDesc.Format != dstDesc.Format || srcDesc.Width != dstDesc.Width ||
                 srcDesc.Height != dstDesc.Height || srcDesc.DepthOrArraySize != dstDesc.DepthOrArraySize ||
                 srcDesc.MipLevels != dstDesc.MipLevels )
            {
                if ( _pDevice->_bBlitMismatchLogged == 0 )
                {
                    _pDevice->_bBlitMismatchLogged = 1;
                    SW_LOG_ERROR( "blitTexture: CopyResource 불가 — src(fmt=%# %#x%#) dst(fmt=%# %#x%#). 복사를 건너뜁니다.",
                                  static_cast<uint32>( srcDesc.Format ), static_cast<uint32>( srcDesc.Width ), static_cast<uint32>( srcDesc.Height ),
                                  static_cast<uint32>( dstDesc.Format ), static_cast<uint32>( dstDesc.Width ), static_cast<uint32>( dstDesc.Height ) );
                }
                return;
            }
        }

        // 스왑체인 백버퍼의 상태는 스왑체인 객체만 바꾼다 — 여기서 배리어를 따로 쏘고 상태를 직접
        // 대입하면 그 두 벌이 어긋날 수 있다. 오프스크린만 자기 레코드를 갱신한다.
        auto transitionDst = [&]( D3D12_RESOURCE_STATES stateBefore, D3D12_RESOURCE_STATES stateAfter )
        {
            if ( bSwapchainDst )
            {
                _pDevice->_swapChain.transitionTo( _pCmdList, stateAfter );
                return;
            }

            std::scoped_lock<mutex> lock{ _pDevice->_resourceStateMutex };

            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource   = pDstRes;
            barrier.Transition.StateBefore = stateBefore;
            barrier.Transition.StateAfter  = stateAfter;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            _pCmdList->ResourceBarrier( 1, &barrier );

            auto dstIt = _pDevice->_mapOffscreenTexture.find( dstHandle );
            if ( dstIt != _pDevice->_mapOffscreenTexture.end() )
                dstIt->second._state = stateAfter;
        };

        if ( dstStateBefore != D3D12_RESOURCE_STATE_COPY_DEST )
            transitionDst( dstStateBefore, D3D12_RESOURCE_STATE_COPY_DEST );

        _pCmdList->CopyResource( pDstRes, pSrcRes );

        transitionDst( D3D12_RESOURCE_STATE_COPY_DEST, dstStateAfter );
    }

    void D3D12RHICommandContext::bindShaderResource( RHIDescriptorIndex index, uint32 slot )
    {
        // 그래픽스 t# → 슬롯 테이블 상태에 오프라인 뷰를 적는다. 테이블은 드로우 직전 flushSlotTables 가 굳힌다.
        // 뷰라서 버퍼든 텍스처든 같은 경로다(예전 루트 SRV 는 raw/구조 버퍼만 받았다).
        if ( _pCmdList == nullptr || _pDevice->_rootSignature == nullptr || slot >= shaderslot::kSrvSlotCount )
            return;
        const D3D12_CPU_DESCRIPTOR_HANDLE view = resolveOfflineView( index, false );
        if ( view.ptr == 0 )
            return;
        // **값이 실제로 달라질 때만** 더럽힘 표시를 한다. 바인더는 드로우마다 같은 버퍼를 다시 걸므로
        // (인스턴스 버퍼·머티리얼 버퍼는 배치가 바뀌어도 대개 그대로다) 무조건 표시하면 드로우마다
        // 디스크립터 10 개를 온라인 힙에 복사하고 테이블을 다시 걸게 된다.
        // 언리얼 FD3D12DescriptorCache 도 테이블 내용이 그대로면 이미 건 테이블을 그대로 쓴다.
        D3D12SlotTableState& state = _pState->_arrSlotState[0];
        if ( state._arrSrv[slot].ptr == view.ptr )
            return;
        state._arrSrv[slot] = view;
        state._bSrvDirty    = 1;
    }

    void D3D12RHICommandContext::prepareTextureForShaderRead( RHITextureHandle texture )
    {
        if ( _pCmdList == nullptr || texture == 0 )
            return;

        auto it = _pDevice->_mapOffscreenTexture.find( texture );
        if ( it == _pDevice->_mapOffscreenTexture.end() )
            return;
        if ( it->second._bHasRtv == 0 && it->second._bHasDsv == 0 )
            return;

        _pDevice->noteBarrierDuringRecording( "prepareTextureForShaderRead" );
        transitionTexture( texture, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE );
    }

    void D3D12RHICommandContext::prepareTextureForUnorderedAccess( RHITextureHandle texture )
    {
        if ( _pCmdList == nullptr || texture == 0 )
            return;
        // COMMON 은 SRV 로만 암묵 승격된다 — UAV 는 명시 전이가 필요하다 (readback 이 레코드 상태로 되돌린다).
        _pDevice->noteBarrierDuringRecording( "prepareTextureForUnorderedAccess" );
        transitionTexture( texture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS );
    }

    void D3D12RHICommandContext::prepareTextureForRenderTarget( RHITextureHandle texture )
    {
        if ( _pCmdList == nullptr )
            return;

        if ( texture == 0 )
        {
            // 백버퍼. 뎁스로 쓰이는 일은 없다.
            _pDevice->_swapChain.transitionTo( _pCmdList, D3D12_RESOURCE_STATE_RENDER_TARGET );
            return;
        }

        bool bDepth = false;
        {
            std::scoped_lock<mutex> lock{ _pDevice->_resourceStateMutex };
            const auto              it = _pDevice->_mapOffscreenTexture.find( texture );
            if ( it == _pDevice->_mapOffscreenTexture.end() )
                return;
            bDepth = it->second._bHasDsv != 0;
        }
        transitionTexture( texture, bDepth ? D3D12_RESOURCE_STATE_DEPTH_WRITE : D3D12_RESOURCE_STATE_RENDER_TARGET );
    }

    void D3D12RHICommandContext::bindComputeUAV( RHIDescriptorIndex index, uint32 slot )
    {
        // 컴퓨트 u# → 컴퓨트 슬롯 테이블 상태. 인덱스는 UAV 레지스트리(registerBindlessUAV)의 것. 디스패치 직전 굳힌다.
        if ( _pCmdList == nullptr || _pDevice->_rootSignature == nullptr || slot >= shaderslot::kComputeUavSlotCount )
            return;
        const D3D12_CPU_DESCRIPTOR_HANDLE view = resolveOfflineView( index, true );
        if ( view.ptr == 0 )
            return;
        D3D12SlotTableState& state = _pState->_arrSlotState[1];
        if ( state._arrUav[slot].ptr == view.ptr )
            return;
        state._arrUav[slot] = view;
        state._bUavDirty    = 1;
    }

    void D3D12RHICommandContext::bindComputeConstantBuffer( RHIDescriptorIndex index, uint32 slot )
    {
        if ( _pCmdList == nullptr || _pDevice->_rootSignature == nullptr || slot >= shaderslot::kConstantBufferSlotCount )
            return;
        const D3D12_GPU_VIRTUAL_ADDRESS address = resolveBufferAddress( index, false, true );
        if ( address == 0 )
            return;
        _pCmdList->SetComputeRootConstantBufferView( D3D12RHIDevice::kCbvRootParam0 + slot, address );
    }

    void D3D12RHICommandContext::bindComputeShaderResource( RHIDescriptorIndex index, uint32 slot )
    {
        if ( _pCmdList == nullptr || _pDevice->_rootSignature == nullptr || slot >= shaderslot::kSrvSlotCount )
            return;
        const D3D12_CPU_DESCRIPTOR_HANDLE view = resolveOfflineView( index, false );
        if ( view.ptr == 0 )
            return;
        D3D12SlotTableState& state = _pState->_arrSlotState[1];
        if ( state._arrSrv[slot].ptr == view.ptr )
            return;
        state._arrSrv[slot] = view;
        state._bSrvDirty    = 1;
    }

    void D3D12RHICommandContext::setVertexBuffer( uint32 slot, RHIBufferHandle buffer, uint32 stride, uint32 offset )
    {
        (void)slot;
        _pState->_boundMeshVb     = buffer;
        _pState->_boundMeshStride = stride > 0 ? stride : static_cast<uint32>( sizeof( RHIVertex ) );
        _pState->_boundMeshOffset = offset;
    }

    void D3D12RHICommandContext::draw( uint32 vertexCount, uint32 startVertex )
    {
        if ( _pCmdList == nullptr || _pDevice->_rootSignature == nullptr || vertexCount == 0 )
            return;

        const D3D12RHIDevice::D3D12PipelineStateRecord* pPsoRec = _pDevice->_pipelineStates.get( _pState->_activeGraphicsPso );
        if ( pPsoRec == nullptr || pPsoRec->_pso == nullptr )
            return;

        if ( _pState->_boundNativeGraphicsPso != _pState->_activeGraphicsPso )
        {
            _pCmdList->SetPipelineState( pPsoRec->_pso.Get() );
            _pState->_boundNativeGraphicsPso = _pState->_activeGraphicsPso;
        }
        // b0/b1 은 호출자가 bindConstantBuffer( index, shaderslot::k*ConstantBuffer ) 로 건다 (루트 CBV). t 슬롯은 여기서 테이블로 굳힌다.
        flushSlotTables( false );
        _pCmdList->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
        bindMeshVertexBufferOrFallback();
        _pCmdList->DrawInstanced( vertexCount, 1, startVertex, 0 );
    }

    void D3D12RHICommandContext::drawInstanced( uint32 vertexCount, uint32 instanceCount, uint32 startVertex, uint32 startInstance )
    {
        if ( _pCmdList == nullptr || _pDevice->_rootSignature == nullptr || vertexCount == 0 || instanceCount == 0 )
            return;

        const D3D12RHIDevice::D3D12PipelineStateRecord* pPsoRec = _pDevice->_pipelineStates.get( _pState->_activeGraphicsPso );
        if ( pPsoRec == nullptr || pPsoRec->_pso == nullptr )
            return;

        if ( _pState->_boundNativeGraphicsPso != _pState->_activeGraphicsPso )
        {
            _pCmdList->SetPipelineState( pPsoRec->_pso.Get() );
            _pState->_boundNativeGraphicsPso = _pState->_activeGraphicsPso;
        }
        flushSlotTables( false );
        _pCmdList->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
        bindMeshVertexBufferOrFallback();
        _pCmdList->DrawInstanced( vertexCount, instanceCount, startVertex, startInstance );
    }

    void D3D12RHICommandContext::bindConstantBuffer( RHIDescriptorIndex cb, uint32 slot )
    {
        // 상수버퍼는 디스크립터 테이블이 아니라 루트 CBV(GPU 주소)다 — 힙에 쓸 일이 없고 슬롯 b# 이 곧 루트 파라미터다.
        if ( slot >= shaderslot::kConstantBufferSlotCount )
        {
            SW_LOG_TRACE( "bindConstantBuffer: 슬롯 b%# 는 루트 시그니처의 CBV 수(%#)를 넘습니다.", slot, shaderslot::kConstantBufferSlotCount );
            return;
        }
        if ( _pCmdList == nullptr || _pDevice->_rootSignature == nullptr )
            return;
        const D3D12_GPU_VIRTUAL_ADDRESS address = resolveBufferAddress( cb, false, true );
        if ( address == 0 )
            return;
        _pCmdList->SetGraphicsRootConstantBufferView( D3D12RHIDevice::kCbvRootParam0 + slot, address );
    }

    void D3D12RHICommandContext::bindStructuredBuffer( RHIDescriptorIndex index, uint32 slot )
    {
        // 그래픽스 구조버퍼(인스턴스 t4, 머티리얼 데이터 t9 …) — 리플렉션이 준 슬롯의 루트 SRV 에 GPU 주소를 건다.
        bindShaderResource( index, slot );
    }

    void D3D12RHICommandContext::dispatchCompute( uint32 threadGroupCountX, uint32 threadGroupCountY, uint32 threadGroupCountZ )
    {
        if ( _pCmdList == nullptr )
            return;
        flushSlotTables( true );
        _pCmdList->Dispatch( threadGroupCountX, threadGroupCountY, threadGroupCountZ );
    }

    void D3D12RHICommandContext::setViewport( const RHIViewport& viewport )
    {
        if ( _pCmdList == nullptr )
            return;

        D3D12_VIEWPORT vp{};
        vp.TopLeftX = viewport._x;
        vp.TopLeftY = viewport._y;
        vp.Width    = viewport._width;
        vp.Height   = viewport._height;
        vp.MinDepth = viewport._minDepth;
        vp.MaxDepth = viewport._maxDepth;
        _pCmdList->RSSetViewports( 1, &vp );

        D3D12_RECT scissor{
            static_cast<LONG>( viewport._x ),
            static_cast<LONG>( viewport._y ),
            static_cast<LONG>( viewport._x + viewport._width ),
            static_cast<LONG>( viewport._y + viewport._height ) };
        _pCmdList->RSSetScissorRects( 1, &scissor );
    }

    void D3D12RHICommandContext::drawIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset )
    {
        if ( _pCmdList == nullptr || _pDevice->_drawCommandSignature == nullptr || argumentBuffer == 0 )
            return;

        ID3D12Resource* pArgs = _pDevice->resolveBuffer( argumentBuffer );
        if ( pArgs == nullptr )
            return;

        // 여기서 풀스크린 정점버퍼를 무조건 걸던 것이 GPU 드리븐 메시 드로우를 통째로 깨뜨렸다.
        // setVertexBuffer 가 걸어 둔 배치 메시 VB 를 덮어써서, ExecuteIndirect 가 36 정점을 3 정점짜리
        // 버퍼에서 읽어 화면에 찢어진 삼각형이 나왔다(범위 밖은 0 이라 죽지는 않아 더 늦게 드러났다).
        // 다른 세 백엔드는 원래 메시 VB 를 우선한다.
        flushSlotTables( false );
        bindMeshVertexBufferOrFallback();
        _pCmdList->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
        _pCmdList->ExecuteIndirect( _pDevice->_drawCommandSignature.Get(), 1, pArgs, argumentBufferOffset, nullptr, 0 );
    }

    void D3D12RHICommandContext::multiDrawIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset, uint32 maxCommandCount,
                                                    RHIBufferHandle countBuffer, uint32 countBufferOffset )
    {
        if ( _pCmdList == nullptr || _pDevice->_drawCommandSignature == nullptr || argumentBuffer == 0 || maxCommandCount == 0 )
            return;

        ID3D12Resource* pArgs = _pDevice->resolveBuffer( argumentBuffer );
        if ( pArgs == nullptr )
            return;

        flushSlotTables( false );
        bindMeshVertexBufferOrFallback();
        _pCmdList->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST );

        ID3D12Resource* pCountRes = nullptr;
        if ( countBuffer != 0 )
        {
            pCountRes = _pDevice->resolveBuffer( countBuffer );
            if ( pCountRes == nullptr )
            {
                for ( uint32 commandIndex = 0; commandIndex < maxCommandCount; ++commandIndex )
                {
                    const uint32 offset =
                        argumentBufferOffset + commandIndex * static_cast<uint32>( sizeof( RHIDrawIndirectCommand ) );
                    drawIndirect( argumentBuffer, offset );
                }
                return;
            }
        }

        _pCmdList->ExecuteIndirect( _pDevice->_drawCommandSignature.Get(), maxCommandCount, pArgs, argumentBufferOffset,
                                    pCountRes, countBufferOffset );
    }

    void D3D12RHICommandContext::setComputeRootConstants( uint32 rootParameterIndex, uint32 num32BitValues, const void* pData,
                                                          uint32 destOffsetIn32BitValues )
    {
        (void)rootParameterIndex;
        if ( _pCmdList == nullptr || _pDevice->_rootSignature == nullptr || pData == nullptr || num32BitValues == 0 )
            return;
        if ( destOffsetIn32BitValues >= D3D12RHIDevice::kMaxComputeRootConstantDwords )
            return;

        const uint32 maxCount = D3D12RHIDevice::kMaxComputeRootConstantDwords - destOffsetIn32BitValues;
        const uint32 count    = num32BitValues < maxCount ? num32BitValues : maxCount;
        _pCmdList->SetComputeRoot32BitConstants( D3D12RHIDevice::kRootConstantsParam, count, pData, destOffsetIn32BitValues );
    }

    void D3D12RHICommandContext::setGraphicsRootConstants( uint32 rootParameterIndex, uint32 num32BitValues, const void* pData,
                                                           uint32 destOffsetIn32BitValues )
    {
        (void)rootParameterIndex; // 루트 인자 번호는 루트 시그니처가 정한다 (kRootConstantsParam).
        if ( _pCmdList == nullptr || _pDevice->_rootSignature == nullptr || pData == nullptr || num32BitValues == 0 )
            return;
        if ( destOffsetIn32BitValues >= D3D12RHIDevice::kMaxComputeRootConstantDwords )
            return;

        const uint32 maxCount = D3D12RHIDevice::kMaxComputeRootConstantDwords - destOffsetIn32BitValues;
        const uint32 count    = num32BitValues < maxCount ? num32BitValues : maxCount;
        _pCmdList->SetGraphicsRoot32BitConstants( D3D12RHIDevice::kRootConstantsParam, count, pData, destOffsetIn32BitValues );
    }

    void D3D12RHICommandContext::drawIndexedIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset )
    {
        if ( _pCmdList == nullptr || _pDevice->_drawIndexedCommandSignature == nullptr || argumentBuffer == 0 )
            return;
        if ( _pState->_boundIndexBuffer == 0 )
            return;

        ID3D12Resource* pArgs = _pDevice->resolveBuffer( argumentBuffer );
        if ( pArgs == nullptr )
            return;

        flushSlotTables( false );
        bindMeshVertexBuffer();
        bindBoundIndexBuffer();
        _pCmdList->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
        _pCmdList->ExecuteIndirect( _pDevice->_drawIndexedCommandSignature.Get(), 1, pArgs, argumentBufferOffset, nullptr, 0 );
    }

    void D3D12RHICommandContext::dispatchIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset )
    {
        if ( _pCmdList == nullptr || _pDevice->_dispatchCommandSignature == nullptr || argumentBuffer == 0 )
            return;

        ID3D12Resource* pArgs = _pDevice->resolveBuffer( argumentBuffer );
        if ( pArgs == nullptr )
            return;

        flushSlotTables( true );
        _pCmdList->ExecuteIndirect( _pDevice->_dispatchCommandSignature.Get(), 1, pArgs, argumentBufferOffset, nullptr, 0 );
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

    void D3D12RHICommandContext::setPipelineState( RHIPipelineStateHandle pso )
    {
        if ( _pCmdList == nullptr )
            return;

        _pState->_activeGraphicsPso = pso;
        // PSO 가 바뀌면 그래픽스 슬롯 상태를 비운다 — 이전 패스의 t 슬롯이 다음 테이블로 새지 않게(Vulkan setPipelineState 와 같다).
        _pState->_arrSlotState[0]                               = D3D12SlotTableState{};
        const D3D12RHIDevice::D3D12PipelineStateRecord* pRecord = _pDevice->_pipelineStates.get( pso );
        if ( pRecord == nullptr || pRecord->_pso == nullptr )
            return;

        // 루트 시그니처는 리스트가 열릴 때 이미 걸렸다(bindBindlessRootState) — PSO 만 바꾼다. 루트 CBV 인자는 유지된다.
        _pCmdList->SetPipelineState( pRecord->_pso.Get() );
        // draw()/drawInstanced()가 같은 PSO로 다시 SetPipelineState 하지 않도록 이미 바인딩된 것으로 표시.
        _pState->_boundNativeGraphicsPso = pso;
    }

    void D3D12RHICommandContext::setComputePipelineState( RHIPipelineStateHandle pso )
    {
        if ( _pCmdList == nullptr )
            return;

        _pState->_arrSlotState[1]                               = D3D12SlotTableState{}; // 컴퓨트 슬롯 상태도 PSO 단위
        const D3D12RHIDevice::D3D12PipelineStateRecord* pRecord = _pDevice->_pipelineStates.get( pso );
        if ( pRecord == nullptr || pRecord->_pso == nullptr )
            return;

        _pCmdList->SetPipelineState( pRecord->_pso.Get() );
    }

    void D3D12RHICommandContext::beginRenderPass( const RHIRenderPassBeginInfo& beginInfo )
    {
        ensureRecording();
        if ( _pCmdList == nullptr )
            return;

        const bool bBindColor = beginInfo._bBindColor != 0;
        const bool bHasDepth  = beginInfo._depthTarget != 0;
        if ( bBindColor == false && bHasDepth == false )
            return;

        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[kMaxColorAttachments]{};
        uint32                      rtCount{ 0 };
        _pState->_activeColorTargetCount = 0;
        _pState->_bActiveSwapchainRT     = 0;

        const uint32 wantCount = ( beginInfo._colorTargetCount > 0 ) ? beginInfo._colorTargetCount : ( bBindColor ? 1u : 0u );
        for ( uint32 attachmentIndex = 0; attachmentIndex < wantCount && attachmentIndex < kMaxColorAttachments; ++attachmentIndex )
        {
            const RHITextureHandle      colorHandle = beginInfo._arrColorTarget[attachmentIndex];
            D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
            bool                        bValid = false;

            if ( colorHandle == 0 )
            {
                if ( attachmentIndex > 0 || _pDevice->_swapChain.isBackBufferReady() == false )
                    break;
                _pDevice->_swapChain.transitionTo( _pCmdList, D3D12_RESOURCE_STATE_RENDER_TARGET );
                rtv                                     = _pDevice->_swapChain.getCurrentRtv();
                bValid                                  = true;
                _pState->_bActiveSwapchainRT            = 1;
                _pState->_arrActiveColorTarget[rtCount] = 0;
            }
            else
            {
                auto it = _pDevice->_mapOffscreenTexture.find( colorHandle );
                if ( it == _pDevice->_mapOffscreenTexture.end() || it->second._bHasRtv == 0 )
                {
                    if ( attachmentIndex > 0 )
                        break;
                    return;
                }
                _pDevice->noteBarrierDuringRecording( "beginRenderPass(color)" );
                transitionTexture( colorHandle, D3D12_RESOURCE_STATE_RENDER_TARGET );
                rtv                                     = it->second._rtvHandle;
                bValid                                  = true;
                _pState->_arrActiveColorTarget[rtCount] = colorHandle;
            }

            if ( bValid == false )
                break;

            const RHIRenderPassLoadOp loadOp = beginInfo._arrLoadOp[attachmentIndex];
            const float32*            pClear = &beginInfo._arrClearColor[attachmentIndex]._x;
            if ( loadOp == RHIRenderPassLoadOp::Clear )
                _pCmdList->ClearRenderTargetView( rtv, pClear, 0, nullptr );

            rtvHandles[rtCount++] = rtv;
        }

        D3D12_CPU_DESCRIPTOR_HANDLE* pDsv{ nullptr };
        D3D12_CPU_DESCRIPTOR_HANDLE  dsvHandle{};
        _pState->_activeDepthTarget = 0;
        if ( bHasDepth )
        {
            auto depthIt = _pDevice->_mapOffscreenTexture.find( beginInfo._depthTarget );
            if ( depthIt != _pDevice->_mapOffscreenTexture.end() && depthIt->second._bHasDsv != 0 )
            {
                _pDevice->noteBarrierDuringRecording( "beginRenderPass(depth)" );
                transitionTexture( beginInfo._depthTarget, D3D12_RESOURCE_STATE_DEPTH_WRITE );
                dsvHandle                   = depthIt->second._dsvHandle;
                pDsv                        = &dsvHandle;
                _pState->_activeDepthTarget = beginInfo._depthTarget;
                if ( beginInfo._depthLoadOp == RHIRenderPassLoadOp::Clear )
                    _pCmdList->ClearDepthStencilView( dsvHandle, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
                                                      beginInfo._clearDepth, 0, 0, nullptr );
            }
        }

        _pState->_activeColorTargetCount = rtCount;
        if ( rtCount > 0 )
            _pCmdList->OMSetRenderTargets( rtCount, rtvHandles, FALSE, pDsv );
        else if ( pDsv != nullptr )
            _pCmdList->OMSetRenderTargets( 0, nullptr, FALSE, pDsv );

        const uint32   vpW = beginInfo._width > 0 ? beginInfo._width : _pDevice->_swapChain.getWidth();
        const uint32   vpH = beginInfo._height > 0 ? beginInfo._height : _pDevice->_swapChain.getHeight();
        D3D12_VIEWPORT vp{};
        vp.Width    = static_cast<float32>( vpW );
        vp.Height   = static_cast<float32>( vpH );
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        _pCmdList->RSSetViewports( 1, &vp );

        D3D12_RECT scissor{ 0, 0, static_cast<LONG>( vpW ), static_cast<LONG>( vpH ) };
        _pCmdList->RSSetScissorRects( 1, &scissor );
    }

    void D3D12RHICommandContext::endRenderPass()
    {
        _pState->_activeColorTargetCount = 0;
        _pState->_activeDepthTarget      = 0;
        _pState->_bActiveSwapchainRT     = 0;
    }

    void D3D12RHICommandContext::setIndexBuffer( RHIBufferHandle buffer, uint32 indexStride, uint32 offset )
    {
        _pState->_boundIndexBuffer = buffer;
        _pState->_boundIndexStride = ( indexStride == 2 ) ? 2u : 4u;
        _pState->_boundIndexOffset = offset;
        if ( _pCmdList == nullptr || buffer == 0 )
            return;
        bindBoundIndexBuffer();
    }

    void D3D12RHICommandContext::uavBarrier( RHIBufferHandle buffer )
    {
        if ( _pCmdList == nullptr || buffer == 0 )
            return;
        ID3D12Resource* pResource = _pDevice->resolveBuffer( buffer );
        if ( pResource == nullptr )
            return;

        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type          = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        barrier.UAV.pResource = pResource;
        _pCmdList->ResourceBarrier( 1, &barrier );
    }

    void D3D12RHICommandContext::transitionBuffer( RHIBufferHandle buffer, RHIBufferState newState )
    {
        if ( _pCmdList == nullptr || buffer == 0 )
            return;

        ID3D12Resource* pResource = _pDevice->resolveBuffer( buffer );
        if ( pResource == nullptr )
            return;

        const D3D12_RESOURCE_STATES stateAfter = D3D12RHICommandContextInternal::toD3D12BufferState( newState );
        D3D12_RESOURCE_STATES       stateBefore;
        {
            std::scoped_lock<mutex> lock{ _pDevice->_resourceStateMutex };
            auto                    stateIt = _pDevice->_mapStructuredBufferState.find( buffer );
            if ( stateIt == _pDevice->_mapStructuredBufferState.end() )
                return;
            if ( stateIt->second == stateAfter )
                return;
            stateBefore     = stateIt->second;
            stateIt->second = stateAfter;
        }

        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource   = pResource;
        barrier.Transition.StateBefore = stateBefore;
        barrier.Transition.StateAfter  = stateAfter;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        _pCmdList->ResourceBarrier( 1, &barrier );
    }

} // namespace sw
#endif
