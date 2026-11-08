#include "pch.h"

#include "Engine/Graphics/RHI/DX/DX12/D3D12RHICommandContext.h"

#include "Core/Concurrency/mutex.h"

#include "Engine/Graphics/Debug/GPUBreadcrumbs.h"
#include "Engine/Graphics/RHI/DX/DX12/D3D12RHIDevice.h"
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
                    return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
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
        _pState->_bRecording             = SW_TRUE;
        _pState->_boundNativeGraphicsPSO = 0;                     // 새 리스트에는 아직 아무 PSO 도 안 걸렸다. 캐시를 무효로 한다.
        _pState->_arrSlotState[0]        = D3D12SlotTableState{}; // 새 리스트에는 슬롯 테이블도 없다. 첫 드로우가 다시 굳힌다.
        _pState->_arrSlotState[1]        = D3D12SlotTableState{};
        // 힙 · 루트 시그니처 · 텍스처 배열 테이블은 리스트가 열릴 때 한 번 건다. 이후 bind*() 는 루트 CBV(상수버퍼)와 슬롯 테이블(t · u)만 바꾼다.
        _pDevice->bindBindlessRootState( _pCmdList );
    }

    D3D12_GPU_VIRTUAL_ADDRESS D3D12RHICommandContext::resolveBufferAddress( RHIDescriptorIndex index, bool bUAV, bool bConstantBuffer ) const
    {
        if ( index == kInvalidDescriptorIndex )
            return 0;

        // 렌더 스레드 안에서 레지스트리는 기록 중에 바뀌지 않는다(등록/해제는 그래프 셋업에서 끝나고 requireRegistryMutableNow 가 감시한다). 그러나
        // **게임 스레드**는 렌더 스레드의 기록과 겹쳐 버퍼를 만들고 부순다(에디터가 없는 빌드 — 오브젝트가 사라지며 머티리얼 인스턴스가 풀린다).
        // 그 쓰기와 겹치지 않게 짧은 읽기 락을 잡는다(경합이 없으면 원자 연산 하나). const 참조로 받는 것도 중요하다.
        ScopedSharedLock<shared_mutex>                        registryLock{ _pDevice->_bindlessMutex };
        const vector<D3D12RHIDevice::BindlessResourceRecord>& listRegistry =
            bUAV ? _pDevice->_listRegisteredUAV : _pDevice->_listRegisteredBindless;
        if ( index >= static_cast<RHIDescriptorIndex>( listRegistry.size() ) )
            return 0;
        const D3D12RHIDevice::BindlessResourceRecord& record = listRegistry[index];
        if ( record._resource == nullptr )
            return 0;

        D3D12_GPU_VIRTUAL_ADDRESS address = record._resource->GetGPUVirtualAddress();
        if ( bConstantBuffer )
        {
            // 링 상수버퍼(createConstantBuffer)는 프레임 슬롯마다 정렬 크기만큼 떨어진 자리에 쓴다.
            // updateConstantBuffer 가 이번 프레임 슬롯에 썼으므로 같은 슬롯 주소를 건다.
            const auto sizeIt = _pDevice->_mapCbAlignedSize.find( record._buffer );
            if ( sizeIt != _pDevice->_mapCbAlignedSize.end() )
                address += static_cast<D3D12_GPU_VIRTUAL_ADDRESS>( _pDevice->_frameRing.currentIndex() ) * sizeIt->second;
        }
        return address;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE D3D12RHICommandContext::resolveOfflineView( RHIDescriptorIndex index, bool bUAV ) const
    {
        if ( index == kInvalidDescriptorIndex )
            return D3D12_CPU_DESCRIPTOR_HANDLE{};
        // resolveBufferAddress 와 같은 이유로 짧은 읽기 락을 잡는다.
        ScopedSharedLock<shared_mutex>                        registryLock{ _pDevice->_bindlessMutex };
        const vector<D3D12RHIDevice::BindlessResourceRecord>& listRegistry =
            bUAV ? _pDevice->_listRegisteredUAV : _pDevice->_listRegisteredBindless;
        if ( index >= static_cast<RHIDescriptorIndex>( listRegistry.size() ) )
            return D3D12_CPU_DESCRIPTOR_HANDLE{};
        const D3D12RHIDevice::BindlessResourceRecord& record = listRegistry[index];
        if ( record._resource == nullptr )
            return D3D12_CPU_DESCRIPTOR_HANDLE{};
        return record._offlineCPUHandle;
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

    bool D3D12RHICommandContext::flushSlotTables( bool bCompute )
    {
        if ( _pCmdList == nullptr || _pDevice->_rootSignature == nullptr )
            return false;
        D3D12SlotTableState& state = _pState->_arrSlotState[bCompute ? 1 : 0];

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
        // u 테이블 — 컴퓨트는 셰이더 소유 버퍼 · 메시지 링, 그래픽스는 셰이더 메시지 링(u4, bindGraphicsUAV)만 건다. 그래픽스 상태는 걸린 것이 없으면 더럽지 않아 테이블을 세우지 않는다.
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
        return true;
    }

    bool D3D12RHICommandContext::bindMeshVertexBufferForDraw()
    {
        // PSO 가 읽는 슬롯에만 버퍼를 건다(정점 입력이 없는 셰이더는 레이아웃이 비어 아무 슬롯도 읽지 않는다). 건 뒤에 부서진 버퍼(세대가 달라 풀리지 않는다)면 드로우를 버린다.
        const D3D12RHIDevice::D3D12PipelineStateRecord* pPSORecord = _pDevice->_pipelineStates.get( _pState->_activeGraphicsPSO );
        const RHIVertexLayout                           input      = pPSORecord != nullptr ? pPSORecord->_vertexLayout : RHIVertexLayout{}; // 없는 PSO 는 bindActiveGraphicsPSO 가 멈췄다
        return _pState->_geometry.bindVertexBuffersForDraw( _pDevice->getDrawDiagnostics(), input, [this]( uint32 slot, const RHIBufferBinding& binding )
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
        return _pState->_geometry.bindIndexBufferForDraw( _pDevice->getDrawDiagnostics(), [this]( const RHIBufferBinding& binding )
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

    void D3D12RHICommandContext::transitionTexture( RHITextureHandle texture, D3D12_RESOURCE_STATES requestedState )
    {
        const D3D12_RESOURCE_STATES newState  = toQueueState( requestedState );
        ID3D12Resource*             pResource = _pDevice->resolveTexture( texture );
        if ( pResource == nullptr )
            return;

        // 상태 확인과 배리어 기록이 한 덩어리여야 한다. RenderGraph::executeParallel 이 같은 레벨의
        // 패스 콜백을 여러 스레드에서 돌리는데, 둘이 같은 텍스처를 전이하면 둘 다 같은 "이전 상태" 를
        // 보고 각자 배리어를 쏴서 두 번째가 before==after 가 된다(검증 오류 → 디바이스 제거).
        ScopedLock<mutex> lock{ _pDevice->_resourceStateMutex };

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
        commandListForRecord()->ResourceBarrier( 1, &barrier );
        record._state = newState;
    }

    // 길이 유지(긴 함수 정리 기준): 같은 포맷 복사와 셰이더 블릿의 상태 전이 순서가 곧 의미라 나누지 않는다.
    void D3D12RHICommandContext::blitTextureNative( RHITextureHandle src, RHITextureHandle dst )
    {
        if ( _pCmdList == nullptr || ( src == 0 && dst == 0 ) )
            return;

        if ( src == 0 )
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
        if ( _pDevice->findOffscreenTargetView( src, 0, srcView ) == false || srcView._bHasDSV != SW_FALSE )
            return;

        _pDevice->reportBarrierDuringRecording( "blitTexture(src)" );
        transitionTexture( src, D3D12_RESOURCE_STATE_COPY_SOURCE );

        ID3D12Resource*       pDstResource   = nullptr;
        D3D12_RESOURCE_STATES dstStateBefore = D3D12_RESOURCE_STATE_COMMON;
        D3D12_RESOURCE_STATES dstStateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;

        if ( dst == 0 )
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
            if ( _pDevice->findOffscreenTargetView( dst, 0, dstView ) == false || dstView._bHasDSV != SW_FALSE )
                return;
            dstStateBefore = dstView._state;
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

        if ( dstStateBefore != D3D12_RESOURCE_STATE_COPY_DEST )
            transitionBlitDst( dst, pDstResource, dstStateBefore, D3D12_RESOURCE_STATE_COPY_DEST );

        commandListForRecord()->CopyResource( pDstResource, pSrcResource );

        transitionBlitDst( dst, pDstResource, D3D12_RESOURCE_STATE_COPY_DEST, dstStateAfter );
    }

    void D3D12RHICommandContext::transitionBlitDst( RHITextureHandle dst, ID3D12Resource* pDstResource, D3D12_RESOURCE_STATES stateBefore,
                                                    D3D12_RESOURCE_STATES stateAfter )
    {
        if ( dst == 0 )
        {
            _pDevice->_swapChain.transitionTo( commandListForRecord(), stateAfter );
            return;
        }

        ScopedLock<mutex> lock{ _pDevice->_resourceStateMutex };

        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource   = pDstResource;
        barrier.Transition.StateBefore = stateBefore;
        barrier.Transition.StateAfter  = stateAfter;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        commandListForRecord()->ResourceBarrier( 1, &barrier );

        auto dstIt = _pDevice->_mapOffscreenTexture.find( dst );
        if ( dstIt != _pDevice->_mapOffscreenTexture.end() )
            dstIt->second._state = stateAfter;
    }

    void D3D12RHICommandContext::bindShaderResource( RHIDescriptorIndex index, uint32 slot )
    {
        // 그래픽스 t# → 슬롯 테이블 상태에 오프라인 뷰를 적는다. 테이블은 드로우 직전 flushSlotTables 가 굳힌다.
        // 뷰라서 버퍼든 텍스처든 같은 경로다(루트 SRV 는 raw/구조 버퍼만 받는다).
        const bool bCannotBindShaderResource = _pCmdList == nullptr || _pDevice->_rootSignature == nullptr || slot >= shaderslot::kSRVSlotCount;
        if ( bCannotBindShaderResource )
            return;
        const D3D12_CPU_DESCRIPTOR_HANDLE view = resolveOfflineView( index, false );
        if ( view.ptr == 0 )
            return;
        // **값이 실제로 달라질 때만** 더럽힘 표시를 한다. 바인더는 드로우마다 같은 버퍼를 다시 걸므로
        // (인스턴스 버퍼·머티리얼 버퍼는 배치가 바뀌어도 대개 그대로다) 무조건 표시하면 드로우마다
        // 디스크립터 10 개를 온라인 힙에 복사하고 테이블을 다시 걸게 된다.
        // 언리얼 FD3D12DescriptorCache 도 테이블 내용이 그대로면 이미 건 테이블을 그대로 쓴다.
        D3D12SlotTableState& state = _pState->_arrSlotState[0];
        if ( state._arrSRV[slot].ptr == view.ptr )
            return;
        state._arrSRV[slot] = view;
        state._bSRVDirty    = SW_TRUE;
    }

    void D3D12RHICommandContext::prepareTextureForShaderReadNative( RHITextureHandle texture )
    {
        if ( _pCmdList == nullptr || texture == 0 )
            return;

        D3D12RHIDevice::OffscreenTargetView view{};
        if ( _pDevice->findOffscreenTargetView( texture, 0, view ) == false )
            return;
        if ( view._bHasRTV == SW_FALSE && view._bHasDSV == SW_FALSE )
            return;

        _pDevice->reportBarrierDuringRecording( "prepareTextureForShaderRead" );
        transitionTexture( texture, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE );
    }

    void D3D12RHICommandContext::prepareTextureForUnorderedAccessNative( RHITextureHandle texture )
    {
        if ( _pCmdList == nullptr || texture == 0 )
            return;
        // COMMON 은 SRV 로만 암묵 승격된다. UAV 는 명시 전이가 필요하다(readback 이 레코드 상태로 되돌린다).
        _pDevice->reportBarrierDuringRecording( "prepareTextureForUnorderedAccess" );
        transitionTexture( texture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS );
    }

    void D3D12RHICommandContext::prepareTextureForRenderTargetNative( RHITextureHandle texture )
    {
        if ( _pCmdList == nullptr )
            return;

        if ( texture == 0 )
        {
            // 백버퍼. 뎁스로 쓰이는 일은 없다.
            _pDevice->_swapChain.transitionTo( commandListForRecord(), D3D12_RESOURCE_STATE_RENDER_TARGET );
            return;
        }

        bool bDepth = false;
        {
            ScopedLock<mutex> lock{ _pDevice->_resourceStateMutex };
            const auto        it = _pDevice->_mapOffscreenTexture.find( texture );
            if ( it == _pDevice->_mapOffscreenTexture.end() )
                return;
            bDepth = it->second._bHasDSV != SW_FALSE;
        }
        transitionTexture( texture, bDepth ? D3D12_RESOURCE_STATE_DEPTH_WRITE : D3D12_RESOURCE_STATE_RENDER_TARGET );
    }

    void D3D12RHICommandContext::bindComputeUAV( RHIDescriptorIndex index, uint32 slot )
    {
        // 컴퓨트 u# → 컴퓨트 슬롯 테이블 상태. 인덱스는 UAV 레지스트리(registerBindlessBufferUAV)의 것. 디스패치 직전 굳힌다.
        const bool bCannotBindUnorderedAccess = _pCmdList == nullptr || _pDevice->_rootSignature == nullptr || slot >= shaderslot::kComputeUAVSlotCount;
        if ( bCannotBindUnorderedAccess )
            return;
        const D3D12_CPU_DESCRIPTOR_HANDLE view = resolveOfflineView( index, true );
        if ( view.ptr == 0 )
            return;
        D3D12SlotTableState& state = _pState->_arrSlotState[1];
        if ( state._arrUAV[slot].ptr == view.ptr )
            return;
        state._arrUAV[slot] = view;
        state._bUAVDirty    = SW_TRUE;
    }

    void D3D12RHICommandContext::bindGraphicsUAV( RHIDescriptorIndex index, uint32 slot )
    {
        // 그래픽스 u# → 그래픽스 슬롯 테이블 상태. 루트 시그니처의 u 테이블은 모든 단계에 보인다(SHADER_VISIBILITY_ALL). 드로우 직전 flushSlotTables( false ) 가 굳힌다.
        const bool bCannotBindUnorderedAccess = _pCmdList == nullptr || _pDevice->_rootSignature == nullptr || slot >= shaderslot::kComputeUAVSlotCount;
        if ( bCannotBindUnorderedAccess )
            return;
        const D3D12_CPU_DESCRIPTOR_HANDLE view = resolveOfflineView( index, true );
        if ( view.ptr == 0 )
            return;
        D3D12SlotTableState& state = _pState->_arrSlotState[0];
        if ( state._arrUAV[slot].ptr == view.ptr )
            return;
        state._arrUAV[slot] = view;
        state._bUAVDirty    = SW_TRUE;
    }

    void D3D12RHICommandContext::bindComputeConstantBuffer( RHIDescriptorIndex constantBufferIndex, uint32 slot )
    {
        const bool bCannotBindComputeConstantBuffer = _pCmdList == nullptr || _pDevice->_rootSignature == nullptr || slot >= shaderslot::kConstantBufferSlotCount;
        if ( bCannotBindComputeConstantBuffer )
            return;
        const D3D12_GPU_VIRTUAL_ADDRESS address = resolveBufferAddress( constantBufferIndex, false, true );
        if ( address == 0 )
            return;
        commandListForRecord()->SetComputeRootConstantBufferView( D3D12RHIDevice::kCBVRootParam0 + slot, address );
    }

    void D3D12RHICommandContext::bindComputeShaderResource( RHIDescriptorIndex index, uint32 slot )
    {
        const bool bCannotBindComputeShaderResource = _pCmdList == nullptr || _pDevice->_rootSignature == nullptr || slot >= shaderslot::kSRVSlotCount;
        if ( bCannotBindComputeShaderResource )
            return;
        const D3D12_CPU_DESCRIPTOR_HANDLE view = resolveOfflineView( index, false );
        if ( view.ptr == 0 )
            return;
        D3D12SlotTableState& state = _pState->_arrSlotState[1];
        if ( state._arrSRV[slot].ptr == view.ptr )
            return;
        state._arrSRV[slot] = view;
        state._bSRVDirty    = SW_TRUE;
    }

    void D3D12RHICommandContext::setVertexBuffer( uint32 slot, RHIBufferHandle buffer, uint32 stride, uint32 offset )
    {
        _pState->_geometry.setVertexBuffer( slot, buffer, stride, offset );
    }

    bool D3D12RHICommandContext::bindActiveGraphicsPSO()
    {
        // 백엔드 계약(네 백엔드 같음): 등록된 그래픽스 PSO 없이 그리면 오류로 멈춘다. 조용히 건너뛰거나 앞 PSO 로 그리지 않는다 — PSO 가 0 이면
        // 부르는 쪽이 드로우를 건너뛰는 것이 렌더러의 규칙이라, 여기 닿은 것은 부르는 쪽의 결함이다(지운 핸들 · PSO 를 걸지 않은 드로우).
        const D3D12RHIDevice::D3D12PipelineStateRecord* pPSORecord = _pDevice->_pipelineStates.get( _pState->_activeGraphicsPSO );
        if ( pPSORecord == nullptr || pPSORecord->_pso == nullptr )
            SW_FATAL( "D3D12 draw without a registered graphics PSO (handle %#)", _pState->_activeGraphicsPSO );

        if ( _pState->_boundNativeGraphicsPSO != _pState->_activeGraphicsPSO )
        {
            commandListForRecord()->SetPipelineState( pPSORecord->_pso.Get() );
            commandListForRecord()->OMSetStencilRef( pPSORecord->_stencilRef );
            _pState->_boundNativeGraphicsPSO = _pState->_activeGraphicsPSO;
        }
        return true;
    }

    void D3D12RHICommandContext::drawNative( uint32 vertexCount, uint32 startVertex )
    {
        const bool bNothingToDraw = _pCmdList == nullptr || _pDevice->_rootSignature == nullptr || vertexCount == 0;
        if ( bNothingToDraw )
            return;

        if ( bindActiveGraphicsPSO() == false )
            return;
        // b0/b1 은 부르는 쪽이 bindConstantBuffer( index, shaderslot::k*ConstantBuffer ) 로 건다(루트 CBV). t 슬롯은 여기서 테이블로 굳힌다.
        if ( flushSlotTables( false ) == false )
            return;
        commandListForRecord()->IASetPrimitiveTopology( _pState->_activeTopology );
        if ( bindMeshVertexBufferForDraw() == false )
            return;
        commandListForRecord()->DrawInstanced( vertexCount, 1, startVertex, 0 );
    }

    void D3D12RHICommandContext::drawInstancedNative( uint32 vertexCount, uint32 instanceCount, uint32 startVertex, uint32 startInstance )
    {
        const bool bNothingToDrawInstanced = _pCmdList == nullptr || _pDevice->_rootSignature == nullptr || vertexCount == 0 || instanceCount == 0;
        if ( bNothingToDrawInstanced )
            return;

        if ( bindActiveGraphicsPSO() == false )
            return;
        if ( flushSlotTables( false ) == false )
            return;
        commandListForRecord()->IASetPrimitiveTopology( _pState->_activeTopology );
        if ( bindMeshVertexBufferForDraw() == false )
            return;
        commandListForRecord()->DrawInstanced( vertexCount, instanceCount, startVertex, startInstance );
    }

    void D3D12RHICommandContext::updateConstantBuffer( RHIBufferHandle buffer, const void* pData, uint32 size )
    {
        if ( _pDevice != nullptr )
            _pDevice->getResourceFactory()->updateConstantBuffer( buffer, pData, size );
    }

    void D3D12RHICommandContext::bindConstantBuffer( RHIDescriptorIndex constantBufferIndex, uint32 slot )
    {
        // 상수버퍼는 디스크립터 테이블이 아니라 루트 CBV(GPU 주소)다. 힙에 쓸 일이 없고 슬롯 b# 이 곧 루트 파라미터다.
        if ( slot >= shaderslot::kConstantBufferSlotCount )
        {
            SW_LOG_TRACE_ID( "D3D12RHICommandContext.BindConstantBufferSlotBExceeds", "bindConstantBuffer: slot b%# exceeds the root signature CBV count (%#).", slot, shaderslot::kConstantBufferSlotCount );
            return;
        }
        if ( _pCmdList == nullptr || _pDevice->_rootSignature == nullptr )
            return;
        const D3D12_GPU_VIRTUAL_ADDRESS address = resolveBufferAddress( constantBufferIndex, false, true );
        if ( address == 0 )
            return;
        commandListForRecord()->SetGraphicsRootConstantBufferView( D3D12RHIDevice::kCBVRootParam0 + slot, address );
    }

    void D3D12RHICommandContext::bindStructuredBuffer( RHIDescriptorIndex index, uint32 slot )
    {
        // 그래픽스 구조버퍼(인스턴스 t4, 머티리얼 데이터 t5 …)도 텍스처와 같은 슬롯 테이블 경로다(bindShaderResource).
        bindShaderResource( index, slot );
    }

    void D3D12RHICommandContext::dispatchComputeNative( uint32 threadGroupCountX, uint32 threadGroupCountY, uint32 threadGroupCountZ )
    {
        if ( _pCmdList == nullptr )
            return;
        requireComputePipelineForDispatch();
        if ( flushSlotTables( true ) == false )
            return;
        commandListForRecord()->Dispatch( threadGroupCountX, threadGroupCountY, threadGroupCountZ );
    }

    void D3D12RHICommandContext::setViewport( const RHIViewport& viewport )
    {
        if ( _pCmdList == nullptr )
            return;

        D3D12_VIEWPORT d3dViewport{};
        d3dViewport.TopLeftX = viewport._x;
        d3dViewport.TopLeftY = viewport._y;
        d3dViewport.Width    = viewport._width;
        d3dViewport.Height   = viewport._height;
        d3dViewport.MinDepth = viewport._minDepth;
        d3dViewport.MaxDepth = viewport._maxDepth;
        commandListForRecord()->RSSetViewports( 1, &d3dViewport );

        D3D12_RECT scissor{
            static_cast<LONG>( viewport._x ),
            static_cast<LONG>( viewport._y ),
            static_cast<LONG>( viewport._x + viewport._width ),
            static_cast<LONG>( viewport._y + viewport._height ) };
        commandListForRecord()->RSSetScissorRects( 1, &scissor );
    }

    void D3D12RHICommandContext::setScissorRect( const RHIScissorRect& rect )
    {
        if ( _pCmdList == nullptr )
            return;
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

        // 여기서 풀스크린 정점버퍼를 무조건 걸던 것이 GPU 드리븐 메시 드로우를 통째로 깨뜨렸다.
        // setVertexBuffer 가 걸어 둔 배치 메시 VB 를 덮어써서, ExecuteIndirect 가 36 정점을 3 정점짜리
        // 버퍼에서 읽어 화면에 찢어진 삼각형이 나왔다(범위 밖은 0 이라 죽지는 않아 더 늦게 드러났다).
        // 다른 세 백엔드는 원래 메시 VB 를 우선한다.
        if ( flushSlotTables( false ) == false )
            return;
        if ( bindMeshVertexBufferForDraw() == false )
            return;
        commandListForRecord()->IASetPrimitiveTopology( _pState->_activeTopology );

        // countBuffer 가 있으면 GPU 가 적어 둔 개수를 쓴다(drawCount 는 상한). ExecuteIndirect 는 둘을 같이 받는다.
        ID3D12Resource* pCountResource = ( countBuffer != 0 ) ? _pDevice->resolveBuffer( countBuffer ) : nullptr;
        commandListForRecord()->ExecuteIndirect( _pDevice->_drawCommandSignature.Get(), drawCount, pArgs, argumentBufferOffset, pCountResource,
                                                 ( pCountResource != nullptr ) ? countBufferOffset : 0 );
    }

    void D3D12RHICommandContext::setComputeRootConstants( uint32 rootParameterIndex, uint32 num32BitValues, const void* pData,
                                                          uint32 destOffsetIn32BitValues )
    {
        (void)rootParameterIndex;
        const bool bInvalidRootConstants = _pCmdList == nullptr || _pDevice->_rootSignature == nullptr || pData == nullptr || num32BitValues == 0;
        if ( bInvalidRootConstants )
            return;
        if ( destOffsetIn32BitValues >= shaderslot::kRootConstantDwords )
            return;

        const uint32 maxCount = shaderslot::kRootConstantDwords - destOffsetIn32BitValues;
        const uint32 count    = num32BitValues < maxCount ? num32BitValues : maxCount;
        commandListForRecord()->SetComputeRoot32BitConstants( D3D12RHIDevice::kRootConstantsParam, count, pData, destOffsetIn32BitValues );
    }

    void D3D12RHICommandContext::setGraphicsRootConstants( uint32 rootParameterIndex, uint32 num32BitValues, const void* pData,
                                                           uint32 destOffsetIn32BitValues )
    {
        (void)rootParameterIndex; // 루트 인자 번호는 루트 시그니처가 정한다 (kRootConstantsParam).
        const bool bInvalidComputeRootConstants = _pCmdList == nullptr || _pDevice->_rootSignature == nullptr || pData == nullptr || num32BitValues == 0;
        if ( bInvalidComputeRootConstants )
            return;
        if ( destOffsetIn32BitValues >= shaderslot::kRootConstantDwords )
            return;

        const uint32 maxCount = shaderslot::kRootConstantDwords - destOffsetIn32BitValues;
        const uint32 count    = num32BitValues < maxCount ? num32BitValues : maxCount;
        commandListForRecord()->SetGraphicsRoot32BitConstants( D3D12RHIDevice::kRootConstantsParam, count, pData, destOffsetIn32BitValues );
    }

    void D3D12RHICommandContext::drawIndexedInstancedNative( uint32 indexCount, uint32 instanceCount, uint32 startIndex, int32 baseVertex, uint32 startInstance )
    {
        const bool bNothingToDraw = _pCmdList == nullptr || indexCount == 0 || instanceCount == 0;
        if ( bNothingToDraw )
            return;
        if ( flushSlotTables( false ) == false )
            return;
        if ( bindMeshVertexBufferForDraw() == false )
            return;
        if ( bindIndexBufferForDraw() == false )
            return;
        commandListForRecord()->IASetPrimitiveTopology( _pState->_activeTopology );
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

        if ( flushSlotTables( false ) == false )
            return;
        // 슬롯 0(메시 정점)과 1(인스턴스 슬롯 스트림)을 다른 드로우와 같은 도우미로 함께 건다. 주의: 슬롯 0 만 걸면 이 리스트에서
        // 슬롯 1 이 한 번도 안 걸렸을 때 인스턴스 자리를 0 으로 읽는다.
        if ( bindMeshVertexBufferForDraw() == false )
            return;
        if ( bindIndexBufferForDraw() == false )
            return;
        commandListForRecord()->IASetPrimitiveTopology( _pState->_activeTopology );
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

        requireComputePipelineForDispatch();
        ID3D12Resource* pArgs = _pDevice->resolveBuffer( argumentBuffer );
        if ( pArgs == nullptr )
            return;

        if ( flushSlotTables( true ) == false )
            return;
        commandListForRecord()->ExecuteIndirect( _pDevice->_dispatchCommandSignature.Get(), 1, pArgs, argumentBufferOffset, nullptr, 0 );
    }

    void D3D12RHICommandContext::writeBreadcrumb( uint32 markerID )
    {
        if ( _pCmdList == nullptr || _pDevice->_breadcrumbAddress == 0 )
            return;
        // WriteBufferImmediate 는 ID3D12GraphicsCommandList2 다 - 없는 런타임은 표지를 쓰지 않는다.
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> list2;
        if ( FAILED( commandListForRecord()->QueryInterface( IID_PPV_ARGS( list2.GetAddressOf() ) ) ) )
            return;
        // 시작 표지는 [0] 칸에 MARKER_IN(앞선 명령이 모두 **시작**한 뒤), 끝 표지(GPUBreadcrumbs::kEndMarkerBit)는 [1] 칸에 MARKER_OUT(앞선 명령이 모두 **끝난** 뒤)으로 쓴다.
        // 소실 뒤 [0] 은 GPU 가 시작한 마지막 패스, [1] 은 끝낸 마지막 패스다.
        // https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_writebufferimmediate_mode
        const bool                                 bEnd = ( markerID & GPUBreadcrumbs::kEndMarkerBit ) != 0;
        const D3D12_WRITEBUFFERIMMEDIATE_PARAMETER param{ _pDevice->_breadcrumbAddress + ( bEnd ? sizeof( uint32 ) : 0u ), markerID };
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

    void D3D12RHICommandContext::setPipelineState( RHIPipelineStateHandle pso )
    {
        if ( _pCmdList == nullptr )
            return;

        _pState->_activeGraphicsPSO = pso;
        // PSO 가 바뀌면 그래픽스 슬롯 상태와 정점 버퍼를 비운다. 이전 패스의 t 슬롯 · 스트림이 다음 드로우로 새지 않게 한다(Vulkan setPipelineState 와 같다).
        _pState->_arrSlotState[0] = D3D12SlotTableState{};
        _pState->_geometry.clearVertexBuffers();
        const D3D12RHIDevice::D3D12PipelineStateRecord* pRecord = _pDevice->_pipelineStates.get( pso );
        if ( pRecord == nullptr || pRecord->_pso == nullptr )
            return;
        _pState->_activeTopology = pRecord->_topology; // 드로우가 IASetPrimitiveTopology 로 건다

        // 루트 시그니처는 리스트가 열릴 때 이미 걸렸다(bindBindlessRootState). PSO 만 바꾼다. 루트 CBV 인자는 유지된다.
        commandListForRecord()->SetPipelineState( pRecord->_pso.Get() );
        commandListForRecord()->OMSetStencilRef( pRecord->_stencilRef );
        // 드로우가 같은 PSO 로 다시 SetPipelineState 하지 않도록 이미 바인딩된 것으로 표시한다.
        _pState->_boundNativeGraphicsPSO = pso;
    }

    void D3D12RHICommandContext::setComputePipelineStateNative( RHIPipelineStateHandle pso )
    {
        if ( _pCmdList == nullptr )
            return;

        _pState->_arrSlotState[1]                               = D3D12SlotTableState{}; // 컴퓨트 슬롯 상태도 PSO 단위
        _pState->_activeComputePSO                              = pso;                   // 레코드가 없어도 기록한다 — 디스패치가 앞 PSO 로 대신 돌지 않고 멈추게 한다
        const D3D12RHIDevice::D3D12PipelineStateRecord* pRecord = _pDevice->_pipelineStates.get( pso );
        if ( pRecord == nullptr || pRecord->_pso == nullptr )
            return;

        commandListForRecord()->SetPipelineState( pRecord->_pso.Get() );
        // 리스트의 PSO 가 컴퓨트로 바뀌었다. 그래픽스 캐시를 비워 다음 드로우가 같은 핸들이어도 다시 건다(안 그러면 컴퓨트 PSO 로 그린다).
        _pState->_boundNativeGraphicsPSO = 0;
    }

    void D3D12RHICommandContext::requireComputePipelineForDispatch() const
    {
        // 백엔드 계약(네 백엔드 같음): 등록된 컴퓨트 PSO 없이 디스패치하면 오류로 멈춘다. 앞 디스패치의 PSO 로 대신 돌지 않고 조용히 건너뛰지도 않는다 — PSO 가 0 이면
        // 부르는 쪽이 디스패치를 건너뛰는 것이 렌더러의 규칙이라, 여기 닿은 것은 부르는 쪽의 결함이다(지운 핸들 · 컴퓨트 PSO 를 걸지 않은 디스패치).
        const D3D12RHIDevice::D3D12PipelineStateRecord* pRecord = _pDevice->_pipelineStates.get( _pState->_activeComputePSO );
        if ( pRecord == nullptr || pRecord->_pso == nullptr )
            SW_FATAL( "D3D12 dispatch without a registered compute PSO (handle %#)", _pState->_activeComputePSO );
    }

    // 길이 유지(긴 함수 정리 기준): 상태 전이 · 시작 접근 채우기 · 패스 시작 · 뷰포트의 순서가 곧 의미라 나누지 않는다.
    RHIRenderPassBeginResult D3D12RHICommandContext::beginRenderPassNative( const RHIRenderPassBeginInfo& beginInfo )
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

        const bool         bBackBuffer    = bBindColor && beginInfo._arrColorTarget[0] == 0;
        D3D12RHISwapChain& swapChain      = bBackBuffer ? _pDevice->useSwapChain( beginInfo._pSwapChain ) : _pDevice->_swapChain;
        const uint32       colorBindCount = ( beginInfo._colorTargetCount > 0 ) ? beginInfo._colorTargetCount : ( bBindColor ? 1u : 0u );
        for ( uint32 attachmentIndex = 0; attachmentIndex < colorBindCount && attachmentIndex < kMaxColorAttachments; ++attachmentIndex )
        {
            const RHITextureHandle      colorHandle = beginInfo._arrColorTarget[attachmentIndex];
            D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
            DXGI_FORMAT                 format{ DXGI_FORMAT_UNKNOWN };
            if ( colorHandle == 0 )
            {
                if ( attachmentIndex > 0 )
                    break;
                if ( swapChain.isBackBufferReady() == false )
                    return RHIRenderPassBeginResult::NoBackBuffer; // 그릴 백버퍼가 없다(최소화 · 스왑체인 재생성 중) — 이 패스의 드로우는 알리지 않고 버린다
                swapChain.transitionTo( commandListForRecord(), D3D12_RESOURCE_STATE_RENDER_TARGET );
                rtv                                     = swapChain.getCurrentRTV();
                format                                  = toDXGIFormat( swapChain.getFormat() );
                _pState->_bActiveSwapchainRT            = SW_TRUE;
                _pState->_arrActiveColorTarget[rtCount] = 0;
            }
            else
            {
                const uint32                        slice = beginInfo._arrColorTargetSlice[attachmentIndex];
                D3D12RHIDevice::OffscreenTargetView view{};
                const bool                          bViewMissing = _pDevice->findOffscreenTargetView( colorHandle, slice, view ) == false || view._bHasRTV == SW_FALSE;
                if ( bViewMissing )
                {
                    if ( attachmentIndex > 0 )
                        break;
                    return RHIRenderPassBeginResult::Failed;
                }
                _pDevice->reportBarrierDuringRecording( "beginRenderPass(color)" );
                transitionTexture( colorHandle, D3D12_RESOURCE_STATE_RENDER_TARGET );
                if ( view._bSliceInRange == SW_FALSE )
                {
                    if ( attachmentIndex > 0 )
                        break;
                    return RHIRenderPassBeginResult::Failed;
                }
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
            const bool                          bDepthFound          = _pDevice->findOffscreenTargetView( beginInfo._depthTarget, beginInfo._depthTargetSlice, depthView );
            const bool                          bDepthTargetWritable = bDepthFound && depthView._bHasDSV != SW_FALSE && depthView._bSliceInRange != SW_FALSE;
            if ( bDepthTargetWritable )
            {
                _pDevice->reportBarrierDuringRecording( "beginRenderPass(depth)" );
                transitionTexture( beginInfo._depthTarget, D3D12_RESOURCE_STATE_DEPTH_WRITE );
                _pState->_activeDepthTarget         = beginInfo._depthTarget;
                bDepthBound                         = true;
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

        const uint32   vpW = beginInfo._width > 0 ? beginInfo._width : swapChain.getWidth();
        const uint32   vpH = beginInfo._height > 0 ? beginInfo._height : swapChain.getHeight();
        D3D12_VIEWPORT viewport{};
        viewport.Width    = static_cast<float32>( vpW );
        viewport.Height   = static_cast<float32>( vpH );
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;
        commandListForRecord()->RSSetViewports( 1, &viewport );

        D3D12_RECT scissor{ 0, 0, static_cast<LONG>( vpW ), static_cast<LONG>( vpH ) };
        commandListForRecord()->RSSetScissorRects( 1, &scissor );
        return RHIRenderPassBeginResult::Begun;
    }

    void D3D12RHICommandContext::endRenderPassNative()
    {
        D3D12RHICommandContextInternal::asRenderPassList( commandListForRecord() )->EndRenderPass();
        _pState->_activeDepthTarget  = 0;
        _pState->_bActiveSwapchainRT = SW_FALSE;
    }

    void D3D12RHICommandContext::setIndexBuffer( RHIBufferHandle buffer, uint32 indexStride, uint32 offset )
    {
        _pState->_geometry.setIndexBuffer( buffer, indexStride, offset ); // 인덱스 드로우가 건다(bindIndexBufferForDraw)
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

    void D3D12RHICommandContext::transitionBufferNative( RHIBufferHandle buffer, RHIBufferState newState )
    {
        if ( _pCmdList == nullptr || buffer == 0 )
            return;

        ID3D12Resource* pResource = _pDevice->resolveBuffer( buffer );
        if ( pResource == nullptr )
            return;

        const D3D12_RESOURCE_STATES stateAfter = toQueueState( toD3D12BufferState( newState ) );
        D3D12_RESOURCE_STATES       stateBefore;
        {
            ScopedLock<mutex> lock{ _pDevice->_resourceStateMutex };
            auto              stateIt = _pDevice->_mapStructuredBufferState.find( buffer );
            if ( stateIt == _pDevice->_mapStructuredBufferState.end() )
                return;
            if ( stateIt->second == stateAfter )
                return;
            stateBefore     = stateIt->second;
            stateIt->second = stateAfter;
        }
        // 컴퓨트 큐 리스트는 따로 낸 ExecuteCommandLists 라 버퍼가 COMMON 으로 감쇠해 들어온다. 기록한 상태에 그래픽스 전용 비트가 있으면 COMMON 을 앞 상태로 쓴다
        // (COMMON 또는 승격 가능한 상태를 앞 상태로 쓰는 것이 문서의 허용 범위다 — 그래픽스 전용 비트는 COMPUTE 리스트의 배리어에 적을 수 없다).
        if ( _bComputeQueueList != SW_FALSE && toQueueState( stateBefore ) != stateBefore )
            stateBefore = D3D12_RESOURCE_STATE_COMMON;
        if ( stateBefore == stateAfter )
            return;

        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource   = pResource;
        barrier.Transition.StateBefore = stateBefore;
        barrier.Transition.StateAfter  = stateAfter;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        commandListForRecord()->ResourceBarrier( 1, &barrier );
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

} // namespace sw
#endif
