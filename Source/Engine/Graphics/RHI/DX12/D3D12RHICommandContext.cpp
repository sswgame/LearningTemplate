#include "pch.h"

#include "Engine/Graphics/RHI/DX12/D3D12RHICommandContext.h"

#include "Engine/Graphics/RHI/DX12/D3D12RHIDevice.h"
#include "Engine/Graphics/RHI/IRHISwapChain.h"

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
        _pState->_boundNativeGraphicsPso = 0; // 새 리스트엔 아직 아무 PSO도 안 걸림 — 캐시 무효화.
        bindDescriptorHeaps();
    }

    void D3D12RHICommandContext::bindDescriptorHeaps()
    {
        if ( _pCmdList == nullptr || _pDevice->_cbvHeap == nullptr )
            return;
        ID3D12DescriptorHeap* heaps[] = { _pDevice->_cbvHeap.Get() };
        _pCmdList->SetDescriptorHeaps( 1, heaps );
    }

    void D3D12RHICommandContext::bindPassAndMaterialCbv( RHIDescriptorIndex passCbDescriptorIndex,
                                                         RHIDescriptorIndex materialCbDescriptorIndex )
    {
        auto resolve = [this]( RHIDescriptorIndex index ) -> const D3D12RHIDevice::BindlessResourceRecord*
        {
            if ( index == kInvalidDescriptorIndex )
                return nullptr;
            if ( index >= static_cast<RHIDescriptorIndex>( _pDevice->_listRegisteredBindless.size() ) )
                return nullptr;
            const D3D12RHIDevice::BindlessResourceRecord& rec = _pDevice->_listRegisteredBindless[index];
            return rec._resource != nullptr ? &rec : nullptr;
        };

        const D3D12RHIDevice::BindlessResourceRecord* pPassRec = resolve( passCbDescriptorIndex );
        const D3D12RHIDevice::BindlessResourceRecord* pMatRec  = resolve( materialCbDescriptorIndex );
        if ( pPassRec == nullptr && pMatRec == nullptr )
            return;

        // 디스크립터 힙은 ensureRecording()이 Reset() 직후 한 번만 SetDescriptorHeaps 하면 그 커맨드
        // 리스트가 Close될 때까지 유지된다 — 여기서 다시 부를 필요 없음(예전엔 매 바인딩마다 재호출).

        // Native bindless: 셰이더가 ResourceDescriptorHeap[g_BindlessCbIndex] 로 PassCB 를 읽는다.
        if ( pPassRec != nullptr )
        {
            if ( _pDevice->_bHeapDirectlyIndexed != 0 )
            {
                const uint32 index = static_cast<uint32>( passCbDescriptorIndex );
                _pCmdList->SetGraphicsRoot32BitConstants( D3D12RHIDevice::kComputeRootConstantsParam, 1, &index, 0 );
            }
            _pCmdList->SetGraphicsRootDescriptorTable( 0, pPassRec->_gpuHandle );
        }

        if ( pMatRec != nullptr )
            _pCmdList->SetGraphicsRootDescriptorTable( D3D12RHIDevice::kMaterialCbvParam, pMatRec->_gpuHandle );
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
        auto it = _pDevice->_mapOffscreenTexture.find( texture );
        if ( it == _pDevice->_mapOffscreenTexture.end() )
            return;
        D3D12RHIDevice::OffscreenTextureRecord& record    = it->second;
        ID3D12Resource*                         pResource = _pDevice->resolveTexture( texture );
        if ( pResource == nullptr || record._state == newState )
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

        transitionTexture( src, D3D12_RESOURCE_STATE_COPY_SOURCE );

        ID3D12Resource*       pDstRes        = nullptr;
        D3D12_RESOURCE_STATES dstStateBefore = D3D12_RESOURCE_STATE_COMMON;
        D3D12_RESOURCE_STATES dstStateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
        RHITextureHandle      dstHandle      = dst;
        bool                  bSwapchainDst  = false;

        if ( dst == 0 )
        {
            if ( _pDevice->_frameIndex >= _pDevice->_listRenderTarget.size() || _pDevice->_listRenderTarget[_pDevice->_frameIndex] == nullptr )
                return;
            pDstRes        = _pDevice->_listRenderTarget[_pDevice->_frameIndex].Get();
            dstStateBefore = _pDevice->_swapchainState;
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

        if ( dstStateBefore != D3D12_RESOURCE_STATE_COPY_DEST )
        {
            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource   = pDstRes;
            barrier.Transition.StateBefore = dstStateBefore;
            barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_DEST;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            _pCmdList->ResourceBarrier( 1, &barrier );
            if ( bSwapchainDst )
                _pDevice->_swapchainState = D3D12_RESOURCE_STATE_COPY_DEST;
            else
            {
                auto dstIt = _pDevice->_mapOffscreenTexture.find( dstHandle );
                if ( dstIt != _pDevice->_mapOffscreenTexture.end() )
                    dstIt->second._state = D3D12_RESOURCE_STATE_COPY_DEST;
            }
        }

        _pCmdList->CopyResource( pDstRes, pSrcRes );

        {
            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource   = pDstRes;
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
            barrier.Transition.StateAfter  = dstStateAfter;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            _pCmdList->ResourceBarrier( 1, &barrier );
            if ( bSwapchainDst )
                _pDevice->_swapchainState = dstStateAfter;
            else
            {
                auto dstIt = _pDevice->_mapOffscreenTexture.find( dstHandle );
                if ( dstIt != _pDevice->_mapOffscreenTexture.end() )
                    dstIt->second._state = dstStateAfter;
            }
        }
    }

    void D3D12RHICommandContext::bindShaderResource( RHIDescriptorIndex index, uint32 slot )
    {
        if ( _pCmdList == nullptr || _pDevice->_rootSignature == nullptr || slot >= 4 )
            return;
        if ( index >= static_cast<RHIDescriptorIndex>( _pDevice->_listRegisteredBindless.size() ) )
            return;
        const D3D12RHIDevice::BindlessResourceRecord& rec = _pDevice->_listRegisteredBindless[index];
        if ( rec._resource == nullptr )
            return;

        _pCmdList->SetGraphicsRootSignature( _pDevice->_rootSignature.Get() );
        _pCmdList->SetGraphicsRootDescriptorTable( D3D12RHIDevice::kGraphicsSrvRootParam0 + slot, rec._gpuHandle );
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

        transitionTexture( texture, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE );
    }

    void D3D12RHICommandContext::bindComputeUAV( RHIDescriptorIndex index, uint32 slot )
    {
        if ( _pCmdList == nullptr || slot >= 4 )
            return;
        if ( index >= static_cast<RHIDescriptorIndex>( _pDevice->_listRegisteredUAV.size() ) )
            return;
        const D3D12RHIDevice::BindlessResourceRecord& rec = _pDevice->_listRegisteredUAV[index];
        if ( rec._resource == nullptr )
            return;

        ID3D12RootSignature* pRootSig = _pDevice->_computeRootSignature.Get();
        if ( pRootSig == nullptr )
            pRootSig = _pDevice->_rootSignature.Get();
        if ( pRootSig != nullptr )
            _pCmdList->SetComputeRootSignature( pRootSig );
        _pCmdList->SetComputeRootDescriptorTable( 1 + slot, rec._gpuHandle );
    }

    void D3D12RHICommandContext::bindComputeConstantBuffer( RHIDescriptorIndex index, uint32 slot )
    {
        // 루트파라미터 0 은 b0/space0 CBV 테이블 하나만 담당한다 (gpucull 의 CullParams).
        if ( _pCmdList == nullptr || slot != 0 )
            return;
        if ( index >= static_cast<RHIDescriptorIndex>( _pDevice->_listRegisteredBindless.size() ) )
            return;
        const D3D12RHIDevice::BindlessResourceRecord& rec = _pDevice->_listRegisteredBindless[index];
        if ( rec._resource == nullptr )
            return;

        ID3D12RootSignature* pRootSig = _pDevice->_computeRootSignature.Get();
        if ( pRootSig == nullptr )
            pRootSig = _pDevice->_rootSignature.Get();
        if ( pRootSig != nullptr )
            _pCmdList->SetComputeRootSignature( pRootSig );
        _pCmdList->SetComputeRootDescriptorTable( 0, rec._gpuHandle );
    }

    void D3D12RHICommandContext::bindComputeShaderResource( RHIDescriptorIndex index, uint32 slot )
    {
        if ( _pCmdList == nullptr || slot >= 4 )
            return;
        if ( index >= static_cast<RHIDescriptorIndex>( _pDevice->_listRegisteredBindless.size() ) )
            return;
        const D3D12RHIDevice::BindlessResourceRecord& rec = _pDevice->_listRegisteredBindless[index];
        if ( rec._resource == nullptr )
            return;

        ID3D12RootSignature* pRootSig = _pDevice->_computeRootSignature.Get();
        if ( pRootSig == nullptr )
            pRootSig = _pDevice->_rootSignature.Get();
        if ( pRootSig != nullptr )
            _pCmdList->SetComputeRootSignature( pRootSig );
        _pCmdList->SetComputeRootDescriptorTable( D3D12RHIDevice::kGraphicsSrvRootParam0 + slot, rec._gpuHandle );
    }

    void D3D12RHICommandContext::setVertexBuffer( uint32 slot, RHIBufferHandle buffer, uint32 stride, uint32 offset )
    {
        (void)slot;
        _pState->_boundMeshVb     = buffer;
        _pState->_boundMeshStride = stride > 0 ? stride : static_cast<uint32>( sizeof( RHIVertex ) );
        _pState->_boundMeshOffset = offset;
    }

    void D3D12RHICommandContext::draw( uint32 vertexCount, uint32 startVertex,
                                       RHIDescriptorIndex passCbDescriptorIndex, RHIDescriptorIndex materialCbDescriptorIndex )
    {
        if ( _pCmdList == nullptr || _pDevice->_rootSignature == nullptr || vertexCount == 0 )
            return;

        const D3D12RHIDevice::D3D12PipelineStateRecord* pPsoRec = _pDevice->_pipelineStates.get( _pState->_activeGraphicsPso );
        if ( pPsoRec == nullptr || pPsoRec->_pso == nullptr )
            return;

        if ( _pState->_boundNativeGraphicsPso != _pState->_activeGraphicsPso )
        {
            _pCmdList->SetGraphicsRootSignature( _pDevice->_rootSignature.Get() );
            _pCmdList->SetPipelineState( pPsoRec->_pso.Get() );
            _pState->_boundNativeGraphicsPso = _pState->_activeGraphicsPso;
        }
        bindPassAndMaterialCbv( passCbDescriptorIndex, materialCbDescriptorIndex );
        _pCmdList->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
        if ( _pState->_boundMeshVb != 0 )
            bindMeshVertexBuffer();
        else
            bindFullscreenVertexBuffer();
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
            _pCmdList->SetGraphicsRootSignature( _pDevice->_rootSignature.Get() );
            _pCmdList->SetPipelineState( pPsoRec->_pso.Get() );
            _pState->_boundNativeGraphicsPso = _pState->_activeGraphicsPso;
        }
        _pCmdList->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
        if ( _pState->_boundMeshVb != 0 )
            bindMeshVertexBuffer();
        else
            bindFullscreenVertexBuffer();
        _pCmdList->DrawInstanced( vertexCount, instanceCount, startVertex, startInstance );
    }

    void D3D12RHICommandContext::bindConstantBuffer( RHIDescriptorIndex cb, uint32 slot )
    {
        if ( slot == 0 )
            bindPassAndMaterialCbv( cb, kInvalidDescriptorIndex );
        else if ( slot == 1 )
            bindPassAndMaterialCbv( kInvalidDescriptorIndex, cb );
        else
            SW_LOG_TRACE( "bindConstantBuffer: 슬롯 b%# 는 현재 루트시그니처에서 지원하지 않습니다.", slot );
    }

    void D3D12RHICommandContext::bindStructuredBuffer( RHIDescriptorIndex index, uint32 slot )
    {
        // 네이티브 bindless(힙 직접 인덱싱) 는 인덱스가 CB 에 있으므로 no-op. 에뮬은 SRV 테이블(t#)로.
        if ( _pDevice->_bHeapDirectlyIndexed != 0 )
            return;
        bindShaderResource( index, slot );
    }

    void D3D12RHICommandContext::dispatchCompute( uint32 threadGroupCountX, uint32 threadGroupCountY, uint32 threadGroupCountZ )
    {
        if ( _pCmdList == nullptr )
            return;
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

    void D3D12RHICommandContext::drawIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset,
                                               RHIDescriptorIndex passCbDescriptorIndex, RHIDescriptorIndex materialCbDescriptorIndex )
    {
        if ( _pCmdList == nullptr || _pDevice->_drawCommandSignature == nullptr || argumentBuffer == 0 )
            return;

        ID3D12Resource* pArgs = _pDevice->resolveBuffer( argumentBuffer );
        if ( pArgs == nullptr )
            return;

        if ( _pDevice->_rootSignature != nullptr )
        {
            _pCmdList->SetGraphicsRootSignature( _pDevice->_rootSignature.Get() );
            bindPassAndMaterialCbv( passCbDescriptorIndex, materialCbDescriptorIndex );
        }
        bindFullscreenVertexBuffer();
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
        if ( _pCmdList == nullptr || pData == nullptr || num32BitValues == 0 )
            return;
        if ( destOffsetIn32BitValues >= D3D12RHIDevice::kMaxComputeRootConstantDwords )
            return;

        const uint32 maxCount   = D3D12RHIDevice::kMaxComputeRootConstantDwords - destOffsetIn32BitValues;
        const uint32 count      = num32BitValues < maxCount ? num32BitValues : maxCount;
        uint32       paramIndex = rootParameterIndex;
        if ( rootParameterIndex == 0 || rootParameterIndex == D3D12RHIDevice::kComputeRootConstantsParam )
            paramIndex = D3D12RHIDevice::kComputeRootConstantsParam;

        ID3D12RootSignature* pRootSig = _pDevice->_computeRootSignature.Get();
        if ( pRootSig == nullptr )
            pRootSig = _pDevice->_rootSignature.Get();
        if ( pRootSig != nullptr )
        {
            _pCmdList->SetComputeRootSignature( pRootSig );
        }

        _pCmdList->SetComputeRoot32BitConstants( paramIndex, count, pData, destOffsetIn32BitValues );
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

        _pState->_activeGraphicsPso                             = pso;
        const D3D12RHIDevice::D3D12PipelineStateRecord* pRecord = _pDevice->_pipelineStates.get( pso );
        if ( pRecord == nullptr || pRecord->_pso == nullptr )
            return;

        if ( _pDevice->_rootSignature != nullptr )
        {
            _pCmdList->SetGraphicsRootSignature( _pDevice->_rootSignature.Get() );
        }
        _pCmdList->SetPipelineState( pRecord->_pso.Get() );
        // draw()/drawInstanced()가 같은 PSO로 다시 SetPipelineState 하지 않도록 이미 바인딩된 것으로 표시.
        _pState->_boundNativeGraphicsPso = pso;
    }

    void D3D12RHICommandContext::setComputePipelineState( RHIPipelineStateHandle pso )
    {
        if ( _pCmdList == nullptr )
            return;

        const D3D12RHIDevice::D3D12PipelineStateRecord* pRecord = _pDevice->_pipelineStates.get( pso );
        if ( pRecord == nullptr || pRecord->_pso == nullptr )
            return;

        ID3D12RootSignature* pRootSig = _pDevice->_computeRootSignature.Get();
        if ( pRootSig == nullptr )
            pRootSig = _pDevice->_rootSignature.Get();
        if ( pRootSig != nullptr )
        {
            _pCmdList->SetComputeRootSignature( pRootSig );
        }
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
                if ( attachmentIndex > 0 || _pDevice->_rtvHeap == nullptr || _pDevice->_frameIndex >= _pDevice->_listRenderTarget.size() )
                    break;
                if ( _pDevice->_swapchainState != D3D12_RESOURCE_STATE_RENDER_TARGET )
                {
                    D3D12_RESOURCE_BARRIER barrier{};
                    barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                    barrier.Transition.pResource   = _pDevice->_listRenderTarget[_pDevice->_frameIndex].Get();
                    barrier.Transition.StateBefore = _pDevice->_swapchainState;
                    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
                    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                    _pCmdList->ResourceBarrier( 1, &barrier );
                    _pDevice->_swapchainState = D3D12_RESOURCE_STATE_RENDER_TARGET;
                }
                rtv = _pDevice->_rtvHeap->GetCPUDescriptorHandleForHeapStart();
                rtv.ptr += _pDevice->_frameIndex * _pDevice->_rtvDescriptorSize;
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

        const uint32   vpW = beginInfo._width > 0 ? beginInfo._width : _pDevice->_width;
        const uint32   vpH = beginInfo._height > 0 ? beginInfo._height : _pDevice->_height;
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
