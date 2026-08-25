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
				case RHIBufferState::Common:
				default:
					return D3D12_RESOURCE_STATE_COMMON;
			}
		}
	} // namespace

	void D3D12RHICommandContext::ensureRecording()
	{
		if ( _pDevice == nullptr || _pDevice->_bRecording != 0 )
			return;
		_pDevice->waitForRingSlot();
		ID3D12CommandAllocator* pAllocator = _pDevice->currentAllocator();
		if ( pAllocator == nullptr || _pDevice->_commandList == nullptr )
			return;
		pAllocator->Reset();
		_pDevice->_commandList->Reset( pAllocator, nullptr );
		_pDevice->_bRecording = 1;
	}

	void D3D12RHICommandContext::bindDescriptorHeaps()
	{
		if ( _pDevice->_commandList == nullptr || _pDevice->_cbvHeap == nullptr )
			return;
		ID3D12DescriptorHeap* heaps[] = { _pDevice->_cbvHeap.Get() };
		_pDevice->_commandList->SetDescriptorHeaps( 1, heaps );
	}

	void D3D12RHICommandContext::bindMaterialCbv( RHIDescriptorIndex materialDescriptorIndex )
	{
		if ( materialDescriptorIndex == kInvalidDescriptorIndex )
			return;
		if ( materialDescriptorIndex >= static_cast<RHIDescriptorIndex>( _pDevice->_listRegisteredBindless.size() ) )
			return;
		const D3D12RHIDevice::BindlessResourceRecord& rec = _pDevice->_listRegisteredBindless[materialDescriptorIndex];
		if ( rec._resource == nullptr )
			return;
		bindDescriptorHeaps();
		// Native bindless: shaders read ResourceDescriptorHeap[g_BindlessCbIndex] (b0, space1).
		if ( _pDevice->_bHeapDirectlyIndexed != 0 )
		{
			const uint32 index = static_cast<uint32>( materialDescriptorIndex );
			_pDevice->_commandList->SetGraphicsRoot32BitConstants( D3D12RHIDevice::kComputeRootConstantsParam, 1, &index, 0 );
		}
		_pDevice->_commandList->SetGraphicsRootDescriptorTable( 0, rec._gpuHandle );
	}

	void D3D12RHICommandContext::bindMeshVertexBuffer()
	{
		ID3D12Resource* pVb = _pDevice->resolveBuffer( _pDevice->_boundMeshVb );
		if ( pVb == nullptr )
			return;
		D3D12_VERTEX_BUFFER_VIEW vbv{};
		vbv.BufferLocation = pVb->GetGPUVirtualAddress() + _pDevice->_boundMeshOffset;
		vbv.SizeInBytes	   = static_cast<UINT>( pVb->GetDesc().Width > _pDevice->_boundMeshOffset
													? pVb->GetDesc().Width - _pDevice->_boundMeshOffset
													: 0 );
		vbv.StrideInBytes  = _pDevice->_boundMeshStride;
		_pDevice->_commandList->IASetVertexBuffers( 0, 1, &vbv );
	}

	void D3D12RHICommandContext::bindFullscreenVertexBuffer()
	{
		if ( _pDevice->_vertexBuffer == nullptr )
			return;
		D3D12_VERTEX_BUFFER_VIEW vbv{};
		vbv.BufferLocation = _pDevice->_vertexBuffer->GetGPUVirtualAddress();
		vbv.SizeInBytes	   = static_cast<UINT>( sizeof( RHIVertex ) * 3 );
		vbv.StrideInBytes  = static_cast<UINT>( sizeof( RHIVertex ) );
		_pDevice->_commandList->IASetVertexBuffers( 0, 1, &vbv );
	}

	void D3D12RHICommandContext::bindBoundIndexBuffer()
	{
		ID3D12Resource* pIb = _pDevice->resolveBuffer( _pDevice->_boundIndexBuffer );
		if ( pIb == nullptr )
			return;
		D3D12_INDEX_BUFFER_VIEW ibv{};
		ibv.BufferLocation = pIb->GetGPUVirtualAddress() + _pDevice->_boundIndexOffset;
		ibv.SizeInBytes	   = static_cast<UINT>( pIb->GetDesc().Width > _pDevice->_boundIndexOffset
													? pIb->GetDesc().Width - _pDevice->_boundIndexOffset
													: 0 );
		ibv.Format		   = ( _pDevice->_boundIndexStride == 2 ) ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
		_pDevice->_commandList->IASetIndexBuffer( &ibv );
	}

	void D3D12RHICommandContext::transitionTexture( RHITextureHandle texture, D3D12_RESOURCE_STATES newState )
	{
		auto it = _pDevice->_mapOffscreenTextures.find( texture );
		if ( it == _pDevice->_mapOffscreenTextures.end() )
			return;
		D3D12RHIDevice::OffscreenTextureRecord& record	  = it->second;
		ID3D12Resource*							pResource = _pDevice->resolveTexture( texture );
		if ( pResource == nullptr || record._state == newState )
			return;
		D3D12_RESOURCE_BARRIER barrier{};
		barrier.Type				   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource   = pResource;
		barrier.Transition.StateBefore = record._state;
		barrier.Transition.StateAfter  = newState;
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		_pDevice->_commandList->ResourceBarrier( 1, &barrier );
		record._state = newState;
	}

	void D3D12RHICommandContext::beginOffscreenPass( RHITextureHandle colorTarget, float32 clearColor[4] )
	{
		if ( colorTarget == 0 )
		{
			IRHISwapChain* pSwapChain = _pDevice->getSwapChain();
			if ( pSwapChain != nullptr )
				pSwapChain->beginFrame( clearColor );
			return;
		}

		ensureRecording();
		if ( _pDevice->_commandList == nullptr )
			return;

		auto it = _pDevice->_mapOffscreenTextures.find( colorTarget );
		if ( it == _pDevice->_mapOffscreenTextures.end() || it->second._bHasRtv == 0 )
			return;

		D3D12RHIDevice::OffscreenTextureRecord& record = it->second;
		transitionTexture( colorTarget, D3D12_RESOURCE_STATE_RENDER_TARGET );

		_pDevice->_commandList->OMSetRenderTargets( 1, &record._rtvHandle, FALSE, nullptr );
		_pDevice->_commandList->ClearRenderTargetView( record._rtvHandle, clearColor, 0, nullptr );

		_pDevice->_arrActiveColorTargets[0] = colorTarget;
		_pDevice->_activeColorTargetCount	= 1;
		_pDevice->_activeDepthTarget		= 0;
		_pDevice->_bActiveSwapchainRT		= 0;

		D3D12_VIEWPORT vp{};
		vp.Width	= static_cast<float32>( record._width );
		vp.Height	= static_cast<float32>( record._height );
		vp.MinDepth = 0.0f;
		vp.MaxDepth = 1.0f;
		_pDevice->_commandList->RSSetViewports( 1, &vp );

		D3D12_RECT scissor{ 0, 0, static_cast<LONG>( record._width ), static_cast<LONG>( record._height ) };
		_pDevice->_commandList->RSSetScissorRects( 1, &scissor );
	}

	void D3D12RHICommandContext::blitTexture( RHITextureHandle src, RHITextureHandle dst )
	{
		if ( _pDevice->_commandList == nullptr || src == 0 )
			return;

		ID3D12Resource* pSrcRes = _pDevice->resolveTexture( src );
		if ( pSrcRes == nullptr )
			return;

		auto srcIt = _pDevice->_mapOffscreenTextures.find( src );
		if ( srcIt == _pDevice->_mapOffscreenTextures.end() || srcIt->second._bHasDsv != 0 )
			return;

		transitionTexture( src, D3D12_RESOURCE_STATE_COPY_SOURCE );

		ID3D12Resource*		  pDstRes		 = nullptr;
		D3D12_RESOURCE_STATES dstStateBefore = D3D12_RESOURCE_STATE_COMMON;
		D3D12_RESOURCE_STATES dstStateAfter	 = D3D12_RESOURCE_STATE_RENDER_TARGET;
		RHITextureHandle	  dstHandle		 = dst;
		bool				  bSwapchainDst	 = false;

		if ( dst == 0 )
		{
			if ( _pDevice->_frameIndex >= _pDevice->_listRenderTargets.size() || _pDevice->_listRenderTargets[_pDevice->_frameIndex] == nullptr )
				return;
			pDstRes		   = _pDevice->_listRenderTargets[_pDevice->_frameIndex].Get();
			dstStateBefore = _pDevice->_swapchainState;
			dstStateAfter  = D3D12_RESOURCE_STATE_PRESENT;
			bSwapchainDst  = true;
		}
		else
		{
			pDstRes = _pDevice->resolveTexture( dst );
			if ( pDstRes == nullptr )
				return;
			auto dstIt = _pDevice->_mapOffscreenTextures.find( dst );
			if ( dstIt == _pDevice->_mapOffscreenTextures.end() || dstIt->second._bHasDsv != 0 )
				return;
			dstStateBefore = dstIt->second._state;
		}

		if ( dstStateBefore != D3D12_RESOURCE_STATE_COPY_DEST )
		{
			D3D12_RESOURCE_BARRIER barrier{};
			barrier.Type				   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barrier.Transition.pResource   = pDstRes;
			barrier.Transition.StateBefore = dstStateBefore;
			barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_DEST;
			barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			_pDevice->_commandList->ResourceBarrier( 1, &barrier );
			if ( bSwapchainDst )
				_pDevice->_swapchainState = D3D12_RESOURCE_STATE_COPY_DEST;
			else
			{
				auto dstIt = _pDevice->_mapOffscreenTextures.find( dstHandle );
				if ( dstIt != _pDevice->_mapOffscreenTextures.end() )
					dstIt->second._state = D3D12_RESOURCE_STATE_COPY_DEST;
			}
		}

		_pDevice->_commandList->CopyResource( pDstRes, pSrcRes );

		{
			D3D12_RESOURCE_BARRIER barrier{};
			barrier.Type				   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barrier.Transition.pResource   = pDstRes;
			barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
			barrier.Transition.StateAfter  = dstStateAfter;
			barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			_pDevice->_commandList->ResourceBarrier( 1, &barrier );
			if ( bSwapchainDst )
				_pDevice->_swapchainState = dstStateAfter;
			else
			{
				auto dstIt = _pDevice->_mapOffscreenTextures.find( dstHandle );
				if ( dstIt != _pDevice->_mapOffscreenTextures.end() )
					dstIt->second._state = dstStateAfter;
			}
		}
	}

	void D3D12RHICommandContext::bindShaderResource( RHIDescriptorIndex index, uint32 slot )
	{
		if ( _pDevice->_commandList == nullptr || _pDevice->_rootSignature == nullptr || slot >= 4 )
			return;
		if ( index >= static_cast<RHIDescriptorIndex>( _pDevice->_listRegisteredBindless.size() ) )
			return;
		const D3D12RHIDevice::BindlessResourceRecord& rec = _pDevice->_listRegisteredBindless[index];
		if ( rec._resource == nullptr )
			return;

		bindDescriptorHeaps();
		_pDevice->_commandList->SetGraphicsRootSignature( _pDevice->_rootSignature.Get() );
		_pDevice->_commandList->SetGraphicsRootDescriptorTable( D3D12RHIDevice::kGraphicsSrvRootParam0 + slot, rec._gpuHandle );
	}

	void D3D12RHICommandContext::prepareTextureForShaderRead( RHITextureHandle texture )
	{
		if ( _pDevice->_commandList == nullptr || texture == 0 )
			return;

		auto it = _pDevice->_mapOffscreenTextures.find( texture );
		if ( it == _pDevice->_mapOffscreenTextures.end() )
			return;
		if ( it->second._bHasRtv == 0 )
			return;

		transitionTexture( texture, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE );
	}

	void D3D12RHICommandContext::bindComputeUAV( RHIDescriptorIndex index, uint32 slot )
	{
		if ( _pDevice->_commandList == nullptr || slot >= 4 )
			return;
		if ( index >= static_cast<RHIDescriptorIndex>( _pDevice->_listRegisteredUAVs.size() ) )
			return;
		const D3D12RHIDevice::BindlessResourceRecord& rec = _pDevice->_listRegisteredUAVs[index];
		if ( rec._resource == nullptr )
			return;

		bindDescriptorHeaps();
		ID3D12RootSignature* pRootSig = _pDevice->_computeRootSignature.Get();
		if ( pRootSig == nullptr )
			pRootSig = _pDevice->_rootSignature.Get();
		if ( pRootSig != nullptr )
			_pDevice->_commandList->SetComputeRootSignature( pRootSig );
		_pDevice->_commandList->SetComputeRootDescriptorTable( 1 + slot, rec._gpuHandle );
	}

	void D3D12RHICommandContext::setVertexBuffer( uint32 slot, RHIBufferHandle buffer, uint32 stride, uint32 offset )
	{
		(void)slot;
		_pDevice->_boundMeshVb	   = buffer;
		_pDevice->_boundMeshStride = stride > 0 ? stride : static_cast<uint32>( sizeof( RHIVertex ) );
		_pDevice->_boundMeshOffset = offset;
	}

	void D3D12RHICommandContext::draw( uint32 vertexCount, uint32 startVertex, RHIDescriptorIndex materialDescriptorIndex )
	{
		if ( _pDevice->_commandList == nullptr || _pDevice->_rootSignature == nullptr || vertexCount == 0 )
			return;

		const D3D12RHIDevice::D3D12PipelineStateRecord* pPsoRec = _pDevice->_pipelineStates.get( _pDevice->_activeGraphicsPso );
		if ( pPsoRec == nullptr || pPsoRec->pso == nullptr )
			return;

		_pDevice->_commandList->SetGraphicsRootSignature( _pDevice->_rootSignature.Get() );
		_pDevice->_commandList->SetPipelineState( pPsoRec->pso.Get() );
		bindMaterialCbv( materialDescriptorIndex );
		_pDevice->_commandList->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
		if ( _pDevice->_boundMeshVb != 0 )
			bindMeshVertexBuffer();
		else
			bindFullscreenVertexBuffer();
		_pDevice->_commandList->DrawInstanced( vertexCount, 1, startVertex, 0 );
	}

	void D3D12RHICommandContext::dispatchCompute( uint32 threadGroupCountX, uint32 threadGroupCountY, uint32 threadGroupCountZ )
	{
		if ( _pDevice->_commandList == nullptr )
			return;
		_pDevice->_commandList->Dispatch( threadGroupCountX, threadGroupCountY, threadGroupCountZ );
	}

	void D3D12RHICommandContext::setViewport( const RHIViewport& viewport )
	{
		if ( _pDevice->_commandList == nullptr )
			return;

		D3D12_VIEWPORT vp{};
		vp.TopLeftX = viewport._x;
		vp.TopLeftY = viewport._y;
		vp.Width	= viewport._width;
		vp.Height	= viewport._height;
		vp.MinDepth = viewport._minDepth;
		vp.MaxDepth = viewport._maxDepth;
		_pDevice->_commandList->RSSetViewports( 1, &vp );

		D3D12_RECT scissor{
			static_cast<LONG>( viewport._x ),
			static_cast<LONG>( viewport._y ),
			static_cast<LONG>( viewport._x + viewport._width ),
			static_cast<LONG>( viewport._y + viewport._height ) };
		_pDevice->_commandList->RSSetScissorRects( 1, &scissor );
	}

	void D3D12RHICommandContext::drawIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset,
											   RHIDescriptorIndex materialDescriptorIndex )
	{
		if ( _pDevice->_commandList == nullptr || _pDevice->_drawCommandSignature == nullptr || argumentBuffer == 0 )
			return;

		ID3D12Resource* pArgs = _pDevice->resolveBuffer( argumentBuffer );
		if ( pArgs == nullptr )
			return;

		if ( _pDevice->_rootSignature != nullptr )
		{
			_pDevice->_commandList->SetGraphicsRootSignature( _pDevice->_rootSignature.Get() );
			bindMaterialCbv( materialDescriptorIndex );
		}
		bindFullscreenVertexBuffer();
		_pDevice->_commandList->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
		_pDevice->_commandList->ExecuteIndirect( _pDevice->_drawCommandSignature.Get(), 1, pArgs, argumentBufferOffset, nullptr, 0 );
	}

	void D3D12RHICommandContext::multiDrawIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset, uint32 maxCommandCount,
													RHIBufferHandle countBuffer, uint32 countBufferOffset )
	{
		if ( _pDevice->_commandList == nullptr || _pDevice->_drawCommandSignature == nullptr || argumentBuffer == 0 || maxCommandCount == 0 )
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

		_pDevice->_commandList->ExecuteIndirect( _pDevice->_drawCommandSignature.Get(), maxCommandCount, pArgs, argumentBufferOffset,
												 pCountRes, countBufferOffset );
	}

	void D3D12RHICommandContext::setComputeRootConstants( uint32 rootParameterIndex, uint32 num32BitValues, const void* pData,
														  uint32 destOffsetIn32BitValues )
	{
		if ( _pDevice->_commandList == nullptr || pData == nullptr || num32BitValues == 0 )
			return;
		if ( destOffsetIn32BitValues >= D3D12RHIDevice::kMaxComputeRootConstantDwords )
			return;

		const uint32 maxCount	= D3D12RHIDevice::kMaxComputeRootConstantDwords - destOffsetIn32BitValues;
		const uint32 count		= num32BitValues < maxCount ? num32BitValues : maxCount;
		uint32		 paramIndex = rootParameterIndex;
		if ( rootParameterIndex == 0 || rootParameterIndex == D3D12RHIDevice::kComputeRootConstantsParam )
			paramIndex = D3D12RHIDevice::kComputeRootConstantsParam;

		ID3D12RootSignature* pRootSig = _pDevice->_computeRootSignature.Get();
		if ( pRootSig == nullptr )
			pRootSig = _pDevice->_rootSignature.Get();
		if ( pRootSig != nullptr )
			_pDevice->_commandList->SetComputeRootSignature( pRootSig );

		_pDevice->_commandList->SetComputeRoot32BitConstants( paramIndex, count, pData, destOffsetIn32BitValues );
	}

	void D3D12RHICommandContext::drawIndexedIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset )
	{
		if ( _pDevice->_commandList == nullptr || _pDevice->_drawIndexedCommandSignature == nullptr || argumentBuffer == 0 )
			return;
		if ( _pDevice->_boundIndexBuffer == 0 )
			return;

		ID3D12Resource* pArgs = _pDevice->resolveBuffer( argumentBuffer );
		if ( pArgs == nullptr )
			return;

		bindMeshVertexBuffer();
		bindBoundIndexBuffer();
		_pDevice->_commandList->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
		_pDevice->_commandList->ExecuteIndirect( _pDevice->_drawIndexedCommandSignature.Get(), 1, pArgs, argumentBufferOffset, nullptr, 0 );
	}

	void D3D12RHICommandContext::dispatchIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset )
	{
		if ( _pDevice->_commandList == nullptr || _pDevice->_dispatchCommandSignature == nullptr || argumentBuffer == 0 )
			return;

		ID3D12Resource* pArgs = _pDevice->resolveBuffer( argumentBuffer );
		if ( pArgs == nullptr )
			return;

		_pDevice->_commandList->ExecuteIndirect( _pDevice->_dispatchCommandSignature.Get(), 1, pArgs, argumentBufferOffset, nullptr, 0 );
	}

	void D3D12RHICommandContext::beginEventMarker( const utf8* pName )
	{
		if ( _pDevice->_commandList == nullptr || pName == nullptr )
			return;
	#if defined( SW_HAS_PIX )
		PIXBeginEvent( _pDevice->_commandList.Get(), 0, "%s", pName );
	#else
		(void)pName;
	#endif
	}

	void D3D12RHICommandContext::endEventMarker()
	{
		if ( _pDevice->_commandList == nullptr )
			return;
	#if defined( SW_HAS_PIX )
		PIXEndEvent( _pDevice->_commandList.Get() );
	#endif
	}

	void D3D12RHICommandContext::setPipelineState( RHIPipelineStateHandle pso )
	{
		if ( _pDevice->_commandList == nullptr )
			return;

		_pDevice->_activeGraphicsPso							= pso;
		const D3D12RHIDevice::D3D12PipelineStateRecord* pRecord = _pDevice->_pipelineStates.get( pso );
		if ( pRecord == nullptr || pRecord->pso == nullptr )
			return;

		if ( _pDevice->_rootSignature != nullptr )
			_pDevice->_commandList->SetGraphicsRootSignature( _pDevice->_rootSignature.Get() );
		_pDevice->_commandList->SetPipelineState( pRecord->pso.Get() );
	}

	void D3D12RHICommandContext::setComputePipelineState( RHIPipelineStateHandle pso )
	{
		if ( _pDevice->_commandList == nullptr )
			return;

		const D3D12RHIDevice::D3D12PipelineStateRecord* pRecord = _pDevice->_pipelineStates.get( pso );
		if ( pRecord == nullptr || pRecord->pso == nullptr )
			return;

		ID3D12RootSignature* pRootSig = _pDevice->_computeRootSignature.Get();
		if ( pRootSig == nullptr )
			pRootSig = _pDevice->_rootSignature.Get();
		if ( pRootSig != nullptr )
			_pDevice->_commandList->SetComputeRootSignature( pRootSig );
		_pDevice->_commandList->SetPipelineState( pRecord->pso.Get() );
	}

	void D3D12RHICommandContext::beginRenderPass( const RHIRenderPassBeginInfo& beginInfo )
	{
		ensureRecording();
		if ( _pDevice->_commandList == nullptr )
			return;

		const bool bBindColor = beginInfo._bBindColor != 0;
		const bool bHasDepth  = beginInfo._depthTarget != 0;
		if ( bBindColor == false && bHasDepth == false )
			return;

		D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[kMaxColorAttachments]{};
		uint32						rtCount{ 0 };
		_pDevice->_activeColorTargetCount = 0;
		_pDevice->_bActiveSwapchainRT	  = 0;

		for ( uint32 attachmentIndex = 0; attachmentIndex < beginInfo._colorTargetCount; ++attachmentIndex )
		{
			const RHITextureHandle		colorHandle = ( beginInfo._colorTargetCount > 0 )
														? beginInfo._arrColorTargets[attachmentIndex]
														: beginInfo._colorTarget;
			D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
			bool						bValid = false;

			if ( colorHandle == 0 )
			{
				if ( attachmentIndex > 0 || _pDevice->_rtvHeap == nullptr || _pDevice->_frameIndex >= _pDevice->_listRenderTargets.size() )
					break;
				if ( _pDevice->_swapchainState != D3D12_RESOURCE_STATE_RENDER_TARGET )
				{
					D3D12_RESOURCE_BARRIER barrier{};
					barrier.Type				   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
					barrier.Transition.pResource   = _pDevice->_listRenderTargets[_pDevice->_frameIndex].Get();
					barrier.Transition.StateBefore = _pDevice->_swapchainState;
					barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
					barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
					_pDevice->_commandList->ResourceBarrier( 1, &barrier );
					_pDevice->_swapchainState = D3D12_RESOURCE_STATE_RENDER_TARGET;
				}
				rtv = _pDevice->_rtvHeap->GetCPUDescriptorHandleForHeapStart();
				rtv.ptr += _pDevice->_frameIndex * _pDevice->_rtvDescriptorSize;
				bValid									  = true;
				_pDevice->_bActiveSwapchainRT			  = 1;
				_pDevice->_arrActiveColorTargets[rtCount] = 0;
			}
			else
			{
				auto it = _pDevice->_mapOffscreenTextures.find( colorHandle );
				if ( it == _pDevice->_mapOffscreenTextures.end() || it->second._bHasRtv == 0 )
				{
					if ( attachmentIndex > 0 )
						break;
					return;
				}
				transitionTexture( colorHandle, D3D12_RESOURCE_STATE_RENDER_TARGET );
				rtv										  = it->second._rtvHandle;
				bValid									  = true;
				_pDevice->_arrActiveColorTargets[rtCount] = colorHandle;
			}

			if ( bValid == false )
				break;

			const RHIRenderPassLoadOp loadOp = ( beginInfo._colorTargetCount > 0 ) ? beginInfo._arrLoadOps[attachmentIndex] : beginInfo._loadOp;
			const float32*			  pClear = ( beginInfo._colorTargetCount > 0 ) ? beginInfo._arrClearColors[attachmentIndex] : beginInfo._arrClearColor;
			if ( loadOp == RHIRenderPassLoadOp::Clear )
				_pDevice->_commandList->ClearRenderTargetView( rtv, pClear, 0, nullptr );

			rtvHandles[rtCount++] = rtv;
		}

		D3D12_CPU_DESCRIPTOR_HANDLE* pDsv{ nullptr };
		D3D12_CPU_DESCRIPTOR_HANDLE	 dsvHandle{};
		_pDevice->_activeDepthTarget = 0;
		if ( bHasDepth )
		{
			auto depthIt = _pDevice->_mapOffscreenTextures.find( beginInfo._depthTarget );
			if ( depthIt != _pDevice->_mapOffscreenTextures.end() && depthIt->second._bHasDsv != 0 )
			{
				transitionTexture( beginInfo._depthTarget, D3D12_RESOURCE_STATE_DEPTH_WRITE );
				dsvHandle					 = depthIt->second._dsvHandle;
				pDsv						 = &dsvHandle;
				_pDevice->_activeDepthTarget = beginInfo._depthTarget;
				if ( beginInfo._depthLoadOp == RHIRenderPassLoadOp::Clear )
					_pDevice->_commandList->ClearDepthStencilView( dsvHandle, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
																   beginInfo._clearDepth, 0, 0, nullptr );
			}
		}

		_pDevice->_activeColorTargetCount = rtCount;
		if ( rtCount > 0 )
			_pDevice->_commandList->OMSetRenderTargets( rtCount, rtvHandles, FALSE, pDsv );
		else if ( pDsv != nullptr )
			_pDevice->_commandList->OMSetRenderTargets( 0, nullptr, FALSE, pDsv );

		const uint32   vpW = beginInfo._width > 0 ? beginInfo._width : _pDevice->_width;
		const uint32   vpH = beginInfo._height > 0 ? beginInfo._height : _pDevice->_height;
		D3D12_VIEWPORT vp{};
		vp.Width	= static_cast<float32>( vpW );
		vp.Height	= static_cast<float32>( vpH );
		vp.MinDepth = 0.0f;
		vp.MaxDepth = 1.0f;
		_pDevice->_commandList->RSSetViewports( 1, &vp );

		D3D12_RECT scissor{ 0, 0, static_cast<LONG>( vpW ), static_cast<LONG>( vpH ) };
		_pDevice->_commandList->RSSetScissorRects( 1, &scissor );
	}

	void D3D12RHICommandContext::endRenderPass()
	{
		_pDevice->_activeColorTargetCount = 0;
		_pDevice->_activeDepthTarget	  = 0;
		_pDevice->_bActiveSwapchainRT	  = 0;
	}

	void D3D12RHICommandContext::setIndexBuffer( RHIBufferHandle buffer, uint32 indexStride, uint32 offset )
	{
		_pDevice->_boundIndexBuffer = buffer;
		_pDevice->_boundIndexStride = ( indexStride == 2 ) ? 2u : 4u;
		_pDevice->_boundIndexOffset = offset;
		if ( _pDevice->_commandList == nullptr || buffer == 0 )
			return;
		bindBoundIndexBuffer();
	}

	void D3D12RHICommandContext::transitionBuffer( RHIBufferHandle buffer, RHIBufferState newState )
	{
		if ( _pDevice->_commandList == nullptr || buffer == 0 )
			return;

		ID3D12Resource* pResource = _pDevice->resolveBuffer( buffer );
		if ( pResource == nullptr )
			return;

		auto stateIt = _pDevice->_mapStructuredBufferStates.find( buffer );
		if ( stateIt == _pDevice->_mapStructuredBufferStates.end() )
			return;

		const D3D12_RESOURCE_STATES stateBefore = stateIt->second;
		const D3D12_RESOURCE_STATES stateAfter	= toD3D12BufferState( newState );
		if ( stateBefore == stateAfter )
			return;

		D3D12_RESOURCE_BARRIER barrier{};
		barrier.Type				   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource   = pResource;
		barrier.Transition.StateBefore = stateBefore;
		barrier.Transition.StateAfter  = stateAfter;
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		_pDevice->_commandList->ResourceBarrier( 1, &barrier );
		stateIt->second = stateAfter;
	}

	void D3D12RHICommandContext::endOffscreenPass( RHITextureHandle colorTarget )
	{
		if ( colorTarget == 0 || _pDevice->_commandList == nullptr )
			return;

		auto it = _pDevice->_mapOffscreenTextures.find( colorTarget );
		if ( it == _pDevice->_mapOffscreenTextures.end() )
			return;

		D3D12RHIDevice::OffscreenTextureRecord& record	  = it->second;
		ID3D12Resource*							pResource = _pDevice->resolveTexture( colorTarget );
		if ( pResource == nullptr )
			return;
		if ( record._state != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE )
		{
			D3D12_RESOURCE_BARRIER barrier{};
			barrier.Type				   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barrier.Transition.pResource   = pResource;
			barrier.Transition.StateBefore = record._state;
			barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
			barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			_pDevice->_commandList->ResourceBarrier( 1, &barrier );
			record._state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		}
	}
} // namespace sw
#endif
