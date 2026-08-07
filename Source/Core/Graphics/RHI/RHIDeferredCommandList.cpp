/**
 * @file RHIDeferredCommandList.cpp
 * @brief Deferred IRHICommandList record / replay
 */
#include "RHIDeferredCommandList.h"

namespace sw
{
	void RHIDeferredCommandList::beginCommandList()
	{
		_cmds.clear();
		_bRecording = true;
	}

	void RHIDeferredCommandList::endCommandList()
	{
		_bRecording = false;
	}

	void RHIDeferredCommandList::push( Cmd cmd )
	{
		if ( _bRecording == false )
			return;
		_cmds.push_back( std::move( cmd ) );
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

	void RHIDeferredCommandList::drawTriangle( RHIDescriptorIndex materialDescriptorIndex )
	{
		Cmd cmd{};
		cmd.op			  = Op::DrawTriangle;
		cmd.materialIndex = materialDescriptorIndex;
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

	void RHIDeferredCommandList::setComputeRootConstants( uint32 rootParameterIndex, uint32 num32BitValues, const void* data, uint32 destOffsetIn32BitValues )
	{
		Cmd cmd{};
		cmd.op						 = Op::SetComputeRootConstants;
		cmd.rootParameterIndex		 = rootParameterIndex;
		cmd.destOffsetIn32BitValues = destOffsetIn32BitValues;
		if ( num32BitValues > 0 && data != nullptr )
		{
			cmd.rootConstantWords.resize( num32BitValues );
			std::memcpy( cmd.rootConstantWords.data(), data, static_cast<size_t>( num32BitValues ) * sizeof( uint32 ) );
		}
		push( std::move( cmd ) );
	}

	void RHIDeferredCommandList::drawIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset )
	{
		Cmd cmd{};
		cmd.op			   = Op::DrawIndirect;
		cmd.argumentBuffer = argumentBuffer;
		cmd.argumentOffset = argumentBufferOffset;
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

	void RHIDeferredCommandList::beginEventMarker( const utf8* name )
	{
		Cmd cmd{};
		cmd.op		  = Op::BeginEventMarker;
		cmd.eventName = name != nullptr ? name : "";
		push( std::move( cmd ) );
	}

	void RHIDeferredCommandList::endEventMarker()
	{
		Cmd cmd{};
		cmd.op = Op::EndEventMarker;
		push( std::move( cmd ) );
	}

	void RHIDeferredCommandList::replay( IRHIDevice* device ) const
	{
		if ( device == nullptr )
			return;

		for ( const Cmd& cmd : _cmds )
		{
			switch ( cmd.op )
			{
				case Op::SetViewport:
					device->setViewport( cmd.viewport );
					break;
				case Op::SetPipelineState:
					device->setPipelineState( cmd.pso );
					break;
				case Op::BeginRenderPass:
					device->beginRenderPass( cmd.beginInfo );
					break;
				case Op::EndRenderPass:
					device->endRenderPass();
					break;
				case Op::DrawTriangle:
					device->drawTriangle( cmd.materialIndex );
					break;
				case Op::DispatchCompute:
					device->dispatchCompute( cmd.dispatchX, cmd.dispatchY, cmd.dispatchZ );
					break;
				case Op::SetComputeRootConstants:
					device->setComputeRootConstants( cmd.rootParameterIndex,
													 static_cast<uint32>( cmd.rootConstantWords.size() ),
													 cmd.rootConstantWords.empty() ? nullptr : cmd.rootConstantWords.data(),
													 cmd.destOffsetIn32BitValues );
					break;
				case Op::DrawIndirect:
					device->drawIndirect( cmd.argumentBuffer, cmd.argumentOffset );
					break;
				case Op::DispatchIndirect:
					device->dispatchIndirect( cmd.argumentBuffer, cmd.argumentOffset );
					break;
				case Op::BeginEventMarker:
					device->beginEventMarker( cmd.eventName.c_str() );
					break;
				case Op::EndEventMarker:
					device->endEventMarker();
					break;
			}
		}
	}
} // namespace sw
