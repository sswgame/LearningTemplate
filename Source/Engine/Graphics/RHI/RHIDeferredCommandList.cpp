#include "pch.h"

#include "Engine/Graphics/RHI/RHIDeferredCommandList.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"

namespace sw
{
	SW_LOG_CALLER( "RHI" );

	RHIDeferredCommandList::RHIDeferredCommandList( RHICommandListMode mode, IRHICommandContext* pContext )
		: _mode{ mode }
		, _pContext{ pContext }
		, _recordingThread{}
		, _listCmd{}
		, _bRecording{ false }
		, _bApplied{ false }
	{
		_listCmd.reserve( 256 );
	}

	void RHIDeferredCommandList::beginCommandList()
	{
		_listCmd.clear();
		if ( _listCmd.capacity() < 256 )
			_listCmd.reserve( 256 );
		_bApplied		 = false;
		_bRecording		 = true;
		_recordingThread = std::this_thread::get_id();
	}

	void RHIDeferredCommandList::endCommandList()
	{
		_bRecording = false;
		if ( _mode == RHICommandListMode::Immediate && _pContext != nullptr )
		{
			replay( _pContext );
			_listCmd.clear();
			_bApplied = true;
		}
	}

	void RHIDeferredCommandList::setViewport( const RHIViewport& viewport )
	{
		Cmd cmd{};
		cmd._op		  = Op::SetViewport;
		cmd._viewport = viewport;
		push( std::move( cmd ) );
	}

	void RHIDeferredCommandList::setPipelineState( RHIPipelineStateHandle pso )
	{
		Cmd cmd{};
		cmd._op	 = Op::SetPipelineState;
		cmd._pso = pso;
		push( std::move( cmd ) );
	}

	void RHIDeferredCommandList::beginRenderPass( const RHIRenderPassBeginInfo& beginInfo )
	{
		Cmd cmd{};
		cmd._op		   = Op::BeginRenderPass;
		cmd._beginInfo = beginInfo;
		push( std::move( cmd ) );
	}

	void RHIDeferredCommandList::endRenderPass()
	{
		Cmd cmd{};
		cmd._op = Op::EndRenderPass;
		push( std::move( cmd ) );
	}

	void RHIDeferredCommandList::setVertexBuffer( uint32 slot, RHIBufferHandle buffer, uint32 stride, uint32 offset )
	{
		Cmd cmd{};
		cmd._op		= Op::SetVertexBuffer;
		cmd._slot	= slot;
		cmd._buffer = buffer;
		cmd._stride = stride;
		cmd._offset = offset;
		push( std::move( cmd ) );
	}

	void RHIDeferredCommandList::draw( uint32 vertexCount, uint32 startVertex, RHIDescriptorIndex materialDescriptorIndex )
	{
		Cmd cmd{};
		cmd._op			   = Op::Draw;
		cmd._vertexCount   = vertexCount;
		cmd._startVertex   = startVertex;
		cmd._materialIndex = materialDescriptorIndex;
		push( std::move( cmd ) );
	}

	void RHIDeferredCommandList::setIndexBuffer( RHIBufferHandle buffer, uint32 indexStride, uint32 offset )
	{
		Cmd cmd{};
		cmd._op			 = Op::SetIndexBuffer;
		cmd._buffer		 = buffer;
		cmd._indexStride = indexStride;
		cmd._offset		 = offset;
		push( std::move( cmd ) );
	}

	void RHIDeferredCommandList::setComputePipelineState( RHIPipelineStateHandle pso )
	{
		Cmd cmd{};
		cmd._op	 = Op::SetComputePipelineState;
		cmd._pso = pso;
		push( std::move( cmd ) );
	}

	void RHIDeferredCommandList::dispatchCompute( uint32 threadGroupCountX, uint32 threadGroupCountY, uint32 threadGroupCountZ )
	{
		Cmd cmd{};
		cmd._op		   = Op::DispatchCompute;
		cmd._dispatchX = threadGroupCountX;
		cmd._dispatchY = threadGroupCountY;
		cmd._dispatchZ = threadGroupCountZ;
		push( std::move( cmd ) );
	}

	void RHIDeferredCommandList::setComputeRootConstants( uint32 rootParameterIndex, uint32 num32BitValues, const void* pData, uint32 destOffsetIn32BitValues )
	{
		Cmd cmd{};
		cmd._op						 = Op::SetComputeRootConstants;
		cmd._rootParameterIndex		 = rootParameterIndex;
		cmd._destOffsetIn32BitValues = destOffsetIn32BitValues;
		if ( num32BitValues > 0 && pData != nullptr )
		{
			cmd._listRootConstantWord.resize( num32BitValues );
			Memory::copy( cmd._listRootConstantWord.data(), pData, static_cast<size_t>( num32BitValues ) * sizeof( uint32 ) );
		}
		push( std::move( cmd ) );
	}

	void RHIDeferredCommandList::bindComputeUAV( RHIDescriptorIndex index, uint32 slot )
	{
		Cmd cmd{};
		cmd._op				 = Op::BindComputeUAV;
		cmd._descriptorIndex = index;
		cmd._slot			 = slot;
		push( std::move( cmd ) );
	}

	void RHIDeferredCommandList::bindShaderResource( RHIDescriptorIndex index, uint32 slot )
	{
		Cmd cmd{};
		cmd._op				 = Op::BindShaderResource;
		cmd._descriptorIndex = index;
		cmd._slot			 = slot;
		push( std::move( cmd ) );
	}

	void RHIDeferredCommandList::prepareTextureForShaderRead( RHITextureHandle texture )
	{
		Cmd cmd{};
		cmd._op			= Op::PrepareTextureForShaderRead;
		cmd._srcTexture = texture;
		push( std::move( cmd ) );
	}

	void RHIDeferredCommandList::blitTexture( RHITextureHandle src, RHITextureHandle dst )
	{
		Cmd cmd{};
		cmd._op			= Op::BlitTexture;
		cmd._srcTexture = src;
		cmd._dstTexture = dst;
		push( std::move( cmd ) );
	}

	void RHIDeferredCommandList::drawIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset,
											   RHIDescriptorIndex materialDescriptorIndex )
	{
		Cmd cmd{};
		cmd._op				= Op::DrawIndirect;
		cmd._argumentBuffer = argumentBuffer;
		cmd._argumentOffset = argumentBufferOffset;
		cmd._materialIndex	= materialDescriptorIndex;
		push( std::move( cmd ) );
	}

	void RHIDeferredCommandList::dispatchIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset )
	{
		Cmd cmd{};
		cmd._op				= Op::DispatchIndirect;
		cmd._argumentBuffer = argumentBuffer;
		cmd._argumentOffset = argumentBufferOffset;
		push( std::move( cmd ) );
	}

	void RHIDeferredCommandList::transitionBuffer( RHIBufferHandle buffer, RHIBufferState newState )
	{
		Cmd cmd{};
		cmd._op				= Op::TransitionBuffer;
		cmd._argumentBuffer = buffer;
		cmd._bufferState	= newState;
		push( std::move( cmd ) );
	}

	void RHIDeferredCommandList::drawIndexedIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset )
	{
		Cmd cmd{};
		cmd._op				= Op::DrawIndexedIndirect;
		cmd._argumentBuffer = argumentBuffer;
		cmd._argumentOffset = argumentBufferOffset;
		push( std::move( cmd ) );
	}

	void RHIDeferredCommandList::multiDrawIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset,
													uint32 maxCommandCount, RHIBufferHandle countBuffer, uint32 countBufferOffset )
	{
		Cmd cmd{};
		cmd._op				 = Op::MultiDrawIndirect;
		cmd._argumentBuffer	 = argumentBuffer;
		cmd._argumentOffset	 = argumentBufferOffset;
		cmd._maxCommandCount = maxCommandCount;
		cmd._countBuffer	 = countBuffer;
		cmd._countOffset	 = countBufferOffset;
		push( std::move( cmd ) );
	}

	void RHIDeferredCommandList::beginEventMarker( const utf8* pName )
	{
		Cmd cmd{};
		cmd._op		   = Op::BeginEventMarker;
		cmd._eventName = pName != nullptr ? pName : "";
		push( std::move( cmd ) );
	}

	void RHIDeferredCommandList::endEventMarker()
	{
		Cmd cmd{};
		cmd._op = Op::EndEventMarker;
		push( std::move( cmd ) );
	}

	void RHIDeferredCommandList::replay( IRHICommandContext* pContext ) const
	{
		if ( pContext == nullptr )
			return;

		for ( size_t idx = 0; idx < _listCmd.size(); ++idx )
		{
			const Cmd& cmd = _listCmd[idx];
			switch ( cmd._op )
			{
				case Op::SetViewport:
					pContext->setViewport( cmd._viewport );
					break;
				case Op::SetPipelineState:
					pContext->setPipelineState( cmd._pso );
					break;
				case Op::BeginRenderPass:
					pContext->beginRenderPass( cmd._beginInfo );
					break;
				case Op::EndRenderPass:
					pContext->endRenderPass();
					break;
				case Op::SetVertexBuffer:
					pContext->setVertexBuffer( cmd._slot, cmd._buffer, cmd._stride, cmd._offset );
					break;
				case Op::Draw:
					pContext->draw( cmd._vertexCount, cmd._startVertex, cmd._materialIndex );
					break;
				case Op::SetIndexBuffer:
					pContext->setIndexBuffer( cmd._buffer, cmd._indexStride, cmd._offset );
					break;
				case Op::SetComputePipelineState:
					pContext->setComputePipelineState( cmd._pso );
					break;
				case Op::DispatchCompute:
					pContext->dispatchCompute( cmd._dispatchX, cmd._dispatchY, cmd._dispatchZ );
					break;
				case Op::SetComputeRootConstants:
					pContext->setComputeRootConstants( cmd._rootParameterIndex,
													   static_cast<uint32>( cmd._listRootConstantWord.size() ),
													   cmd._listRootConstantWord.empty() ? nullptr : cmd._listRootConstantWord.data(),
													   cmd._destOffsetIn32BitValues );
					break;
				case Op::BindComputeUAV:
					pContext->bindComputeUAV( cmd._descriptorIndex, cmd._slot );
					break;
				case Op::BindShaderResource:
					pContext->bindShaderResource( cmd._descriptorIndex, cmd._slot );
					break;
				case Op::PrepareTextureForShaderRead:
					pContext->prepareTextureForShaderRead( cmd._srcTexture );
					break;
				case Op::BlitTexture:
					pContext->blitTexture( cmd._srcTexture, cmd._dstTexture );
					break;
				case Op::DrawIndirect:
					pContext->drawIndirect( cmd._argumentBuffer, cmd._argumentOffset, cmd._materialIndex );
					break;
				case Op::DispatchIndirect:
					pContext->dispatchIndirect( cmd._argumentBuffer, cmd._argumentOffset );
					break;
				case Op::TransitionBuffer:
					pContext->transitionBuffer( cmd._argumentBuffer, cmd._bufferState );
					break;
				case Op::DrawIndexedIndirect:
					pContext->drawIndexedIndirect( cmd._argumentBuffer, cmd._argumentOffset );
					break;
				case Op::MultiDrawIndirect:
					pContext->multiDrawIndirect( cmd._argumentBuffer, cmd._argumentOffset, cmd._maxCommandCount, cmd._countBuffer,
												 cmd._countOffset );
					break;
				case Op::BeginEventMarker:
					pContext->beginEventMarker( cmd._eventName.empty() ? nullptr : cmd._eventName.data() );
					break;
				case Op::EndEventMarker:
					pContext->endEventMarker();
					break;
			}
		}
	}

	void RHIDeferredCommandList::push( Cmd cmd )
	{
		if ( _bRecording == false )
			return;
		assertRecordingThread();
		_listCmd.push_back( std::move( cmd ) );
	}

	void RHIDeferredCommandList::assertRecordingThread() const
	{
		SW_ASSERT( std::this_thread::get_id() == _recordingThread );
	}

	void RHIDeferredCommandList::execute( IRHIDevice* pDevice, IRHICommandList* pCmdList )
	{
		if ( pDevice == nullptr || pCmdList == nullptr )
			return;
		RHIDeferredCommandList* pDeferred = pCmdList->asDeferred();
		if ( pDeferred == nullptr || pDeferred->isApplied() )
			return;
		IRHICommandContext* pImm = pDevice->getImmediateContext();
		if ( pImm == nullptr )
		{
			SW_LOG_WARNING( "RHIDeferredCommandList::execute: Immediate Context is null (device not initialized?); not marking applied" );
			return;
		}
		pDeferred->replay( pImm );
		pDeferred->markApplied();
	}

} // namespace sw
