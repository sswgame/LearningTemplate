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
		cmd.op		 = Op::SetViewport;
		cmd.viewport = viewport;
		push( std::move( cmd ) );
	}

	void RHIDeferredCommandList::setPipelineState( RHIPipelineStateHandle pso )
	{
		Cmd cmd{};
		cmd.op	= Op::SetPipelineState;
		cmd.pso = pso;
		push( std::move( cmd ) );
	}

	void RHIDeferredCommandList::beginRenderPass( const RHIRenderPassBeginInfo& beginInfo )
	{
		Cmd cmd{};
		cmd.op		  = Op::BeginRenderPass;
		cmd.beginInfo = beginInfo;
		push( std::move( cmd ) );
	}

	void RHIDeferredCommandList::endRenderPass()
	{
		Cmd cmd{};
		cmd.op = Op::EndRenderPass;
		push( std::move( cmd ) );
	}

	void RHIDeferredCommandList::setVertexBuffer( uint32 slot, RHIBufferHandle buffer, uint32 stride, uint32 offset )
	{
		Cmd cmd{};
		cmd.op	   = Op::SetVertexBuffer;
		cmd.slot   = slot;
		cmd.buffer = buffer;
		cmd.stride = stride;
		cmd.offset = offset;
		push( std::move( cmd ) );
	}

	void RHIDeferredCommandList::draw( uint32 vertexCount, uint32 startVertex, RHIDescriptorIndex materialDescriptorIndex )
	{
		Cmd cmd{};
		cmd.op			  = Op::Draw;
		cmd.vertexCount	  = vertexCount;
		cmd.startVertex	  = startVertex;
		cmd.materialIndex = materialDescriptorIndex;
		push( std::move( cmd ) );
	}

	void RHIDeferredCommandList::setIndexBuffer( RHIBufferHandle buffer, uint32 indexStride, uint32 offset )
	{
		Cmd cmd{};
		cmd.op			= Op::SetIndexBuffer;
		cmd.buffer		= buffer;
		cmd.indexStride = indexStride;
		cmd.offset		= offset;
		push( std::move( cmd ) );
	}

	void RHIDeferredCommandList::setComputePipelineState( RHIPipelineStateHandle pso )
	{
		Cmd cmd{};
		cmd.op	= Op::SetComputePipelineState;
		cmd.pso = pso;
		push( std::move( cmd ) );
	}

	void RHIDeferredCommandList::dispatchCompute( uint32 threadGroupCountX, uint32 threadGroupCountY, uint32 threadGroupCountZ )
	{
		Cmd cmd{};
		cmd.op		  = Op::DispatchCompute;
		cmd.dispatchX = threadGroupCountX;
		cmd.dispatchY = threadGroupCountY;
		cmd.dispatchZ = threadGroupCountZ;
		push( std::move( cmd ) );
	}

	void RHIDeferredCommandList::setComputeRootConstants( uint32 rootParameterIndex, uint32 num32BitValues, const void* pData, uint32 destOffsetIn32BitValues )
	{
		Cmd cmd{};
		cmd.op						= Op::SetComputeRootConstants;
		cmd.rootParameterIndex		= rootParameterIndex;
		cmd.destOffsetIn32BitValues = destOffsetIn32BitValues;
		if ( num32BitValues > 0 && pData != nullptr )
		{
			cmd.listRootConstantWord.resize( num32BitValues );
			Memory::copy( cmd.listRootConstantWord.data(), pData, static_cast<size_t>( num32BitValues ) * sizeof( uint32 ) );
		}
		push( std::move( cmd ) );
	}

	void RHIDeferredCommandList::bindComputeUAV( RHIDescriptorIndex index, uint32 slot )
	{
		Cmd cmd{};
		cmd.op				= Op::BindComputeUAV;
		cmd.descriptorIndex = index;
		cmd.slot			= slot;
		push( std::move( cmd ) );
	}

	void RHIDeferredCommandList::bindShaderResource( RHIDescriptorIndex index, uint32 slot )
	{
		Cmd cmd{};
		cmd.op				= Op::BindShaderResource;
		cmd.descriptorIndex = index;
		cmd.slot			= slot;
		push( std::move( cmd ) );
	}

	void RHIDeferredCommandList::prepareTextureForShaderRead( RHITextureHandle texture )
	{
		Cmd cmd{};
		cmd.op		   = Op::PrepareTextureForShaderRead;
		cmd.srcTexture = texture;
		push( std::move( cmd ) );
	}

	void RHIDeferredCommandList::blitTexture( RHITextureHandle src, RHITextureHandle dst )
	{
		Cmd cmd{};
		cmd.op		   = Op::BlitTexture;
		cmd.srcTexture = src;
		cmd.dstTexture = dst;
		push( std::move( cmd ) );
	}

	void RHIDeferredCommandList::drawIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset,
											   RHIDescriptorIndex materialDescriptorIndex )
	{
		Cmd cmd{};
		cmd.op			   = Op::DrawIndirect;
		cmd.argumentBuffer = argumentBuffer;
		cmd.argumentOffset = argumentBufferOffset;
		cmd.materialIndex  = materialDescriptorIndex;
		push( std::move( cmd ) );
	}

	void RHIDeferredCommandList::dispatchIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset )
	{
		Cmd cmd{};
		cmd.op			   = Op::DispatchIndirect;
		cmd.argumentBuffer = argumentBuffer;
		cmd.argumentOffset = argumentBufferOffset;
		push( std::move( cmd ) );
	}

	void RHIDeferredCommandList::transitionBuffer( RHIBufferHandle buffer, RHIBufferState newState )
	{
		Cmd cmd{};
		cmd.op			   = Op::TransitionBuffer;
		cmd.argumentBuffer = buffer;
		cmd.bufferState	   = newState;
		push( std::move( cmd ) );
	}

	void RHIDeferredCommandList::drawIndexedIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset )
	{
		Cmd cmd{};
		cmd.op			   = Op::DrawIndexedIndirect;
		cmd.argumentBuffer = argumentBuffer;
		cmd.argumentOffset = argumentBufferOffset;
		push( std::move( cmd ) );
	}

	void RHIDeferredCommandList::multiDrawIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset,
													uint32 maxCommandCount, RHIBufferHandle countBuffer, uint32 countBufferOffset )
	{
		Cmd cmd{};
		cmd.op				= Op::MultiDrawIndirect;
		cmd.argumentBuffer	= argumentBuffer;
		cmd.argumentOffset	= argumentBufferOffset;
		cmd.maxCommandCount = maxCommandCount;
		cmd.countBuffer		= countBuffer;
		cmd.countOffset		= countBufferOffset;
		push( std::move( cmd ) );
	}

	void RHIDeferredCommandList::beginEventMarker( const utf8* pName )
	{
		Cmd cmd{};
		cmd.op		  = Op::BeginEventMarker;
		cmd.eventName = pName != nullptr ? pName : "";
		push( std::move( cmd ) );
	}

	void RHIDeferredCommandList::endEventMarker()
	{
		Cmd cmd{};
		cmd.op = Op::EndEventMarker;
		push( std::move( cmd ) );
	}

	void RHIDeferredCommandList::replay( IRHICommandContext* pContext ) const
	{
		if ( pContext == nullptr )
			return;

		for ( size_t idx = 0; idx < _listCmd.size(); ++idx )
		{
			const Cmd& cmd = _listCmd[idx];
			switch ( cmd.op )
			{
				case Op::SetViewport:
					pContext->setViewport( cmd.viewport );
					break;
				case Op::SetPipelineState:
					pContext->setPipelineState( cmd.pso );
					break;
				case Op::BeginRenderPass:
					pContext->beginRenderPass( cmd.beginInfo );
					break;
				case Op::EndRenderPass:
					pContext->endRenderPass();
					break;
				case Op::SetVertexBuffer:
					pContext->setVertexBuffer( cmd.slot, cmd.buffer, cmd.stride, cmd.offset );
					break;
				case Op::Draw:
					pContext->draw( cmd.vertexCount, cmd.startVertex, cmd.materialIndex );
					break;
				case Op::SetIndexBuffer:
					pContext->setIndexBuffer( cmd.buffer, cmd.indexStride, cmd.offset );
					break;
				case Op::SetComputePipelineState:
					pContext->setComputePipelineState( cmd.pso );
					break;
				case Op::DispatchCompute:
					pContext->dispatchCompute( cmd.dispatchX, cmd.dispatchY, cmd.dispatchZ );
					break;
				case Op::SetComputeRootConstants:
					pContext->setComputeRootConstants( cmd.rootParameterIndex,
													   static_cast<uint32>( cmd.listRootConstantWord.size() ),
													   cmd.listRootConstantWord.empty() ? nullptr : cmd.listRootConstantWord.data(),
													   cmd.destOffsetIn32BitValues );
					break;
				case Op::BindComputeUAV:
					pContext->bindComputeUAV( cmd.descriptorIndex, cmd.slot );
					break;
				case Op::BindShaderResource:
					pContext->bindShaderResource( cmd.descriptorIndex, cmd.slot );
					break;
				case Op::PrepareTextureForShaderRead:
					pContext->prepareTextureForShaderRead( cmd.srcTexture );
					break;
				case Op::BlitTexture:
					pContext->blitTexture( cmd.srcTexture, cmd.dstTexture );
					break;
				case Op::DrawIndirect:
					pContext->drawIndirect( cmd.argumentBuffer, cmd.argumentOffset, cmd.materialIndex );
					break;
				case Op::DispatchIndirect:
					pContext->dispatchIndirect( cmd.argumentBuffer, cmd.argumentOffset );
					break;
				case Op::TransitionBuffer:
					pContext->transitionBuffer( cmd.argumentBuffer, cmd.bufferState );
					break;
				case Op::DrawIndexedIndirect:
					pContext->drawIndexedIndirect( cmd.argumentBuffer, cmd.argumentOffset );
					break;
				case Op::MultiDrawIndirect:
					pContext->multiDrawIndirect( cmd.argumentBuffer, cmd.argumentOffset, cmd.maxCommandCount, cmd.countBuffer,
												 cmd.countOffset );
					break;
				case Op::BeginEventMarker:
					pContext->beginEventMarker( cmd.eventName.empty() ? nullptr : cmd.eventName.data() );
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

	SW_API void executeDeferredCommandList( IRHIDevice* pDevice, IRHICommandList* pCmdList )
	{
		if ( pDevice == nullptr || pCmdList == nullptr )
			return;
		RHIDeferredCommandList* pDeferred = pCmdList->asDeferred();
		if ( pDeferred == nullptr || pDeferred->isApplied() )
			return;
		IRHICommandContext* pImm = pDevice->getImmediateContext();
		if ( pImm == nullptr )
		{
			SW_LOG_WARNING( "executeDeferredCommandList: Immediate Context is null (device not initialized?); not marking applied" );
			return;
		}
		pDeferred->replay( pImm );
		pDeferred->markApplied();
	}

} // namespace sw
