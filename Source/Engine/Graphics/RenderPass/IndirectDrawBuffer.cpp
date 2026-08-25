#include "pch.h"

#include "Engine/Graphics/RenderPass/IndirectDrawBuffer.h"

#include "Core/Log/Logger.h"

namespace sw
{
	IndirectDrawBuffer::IndirectDrawBuffer()
		: _listCommands{}
		, _gpuBufferHandle{ 0 }
	{
	}

	void IndirectDrawBuffer::addDrawCommand( const DrawIndexedInstancedIndirectCommand& cmd )
	{
		_listCommands.push_back( cmd );
	}

	void IndirectDrawBuffer::addDrawCommand( uint32 indexCount, uint32 instanceCount, uint32 startIndex, int32 baseVertex, uint32 startInstance )
	{
		DrawIndexedInstancedIndirectCommand cmd{};
		cmd._indexCountPerInstance = indexCount;
		cmd._instanceCount		   = instanceCount;
		cmd._startIndexLocation	   = startIndex;
		cmd._baseVertexLocation	   = baseVertex;
		cmd._startInstanceLocation = startInstance;
		_listCommands.push_back( cmd );
	}

	void IndirectDrawBuffer::clear()
	{
		_listCommands.clear();
		_gpuBufferHandle = 0;
	}

	bool IndirectDrawBuffer::uploadToBuffer( IRHIResource* pResource )
	{
		if ( pResource == nullptr || _listCommands.empty() )
			return false;

		if ( _gpuBufferHandle != 0 )
		{
			pResource->destroyBuffer( _gpuBufferHandle );
			_gpuBufferHandle = 0;
		}

		RHIBufferDesc desc{};
		desc._sizeBytes	   = static_cast<uint32>( getBufferSizeInBytes() );
		desc._elementSize  = static_cast<uint32>( sizeof( DrawIndexedInstancedIndirectCommand ) );
		desc._elementCount = static_cast<uint32>( _listCommands.size() );
		desc._usage		   = RHIBufferUsage::IndirectArgs;
		desc._pInitialData = _listCommands.data();

		_gpuBufferHandle = pResource->createBuffer( desc );
		return _gpuBufferHandle != 0;
	}

	void IndirectDrawBuffer::releaseGpu( IRHIResource* pResource )
	{
		if ( pResource != nullptr && _gpuBufferHandle != 0 )
		{
			pResource->destroyBuffer( _gpuBufferHandle );
			_gpuBufferHandle = 0;
		}
	}

	void IndirectDrawBuffer::drawAllIndirect( IRHICommandList* pCmdList ) const
	{
		if ( pCmdList == nullptr || _gpuBufferHandle == 0 || _listCommands.empty() )
			return;

		for ( size_t cmdIndex = 0; cmdIndex < _listCommands.size(); ++cmdIndex )
		{
			const uint32 offsetInBytes = static_cast<uint32>( cmdIndex * sizeof( DrawIndexedInstancedIndirectCommand ) );
			pCmdList->drawIndexedIndirect( _gpuBufferHandle, offsetInBytes );
		}
	}
} // namespace sw
