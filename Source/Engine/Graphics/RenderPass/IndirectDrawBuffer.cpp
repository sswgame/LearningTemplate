#include "pch.h"

#include "Engine/Graphics/RenderPass/IndirectDrawBuffer.h"

#include "Core/Log/Logger.h"

namespace sw
{
	IndirectDrawBuffer::IndirectDrawBuffer()
		: _listCommand{}
		, _gpuBufferHandle{ 0 }
	{
	}

	void IndirectDrawBuffer::addDrawCommand( const DrawIndexedInstancedIndirectCommand& cmd )
	{
		_listCommand.push_back( cmd );
	}

	void IndirectDrawBuffer::addDrawCommand( uint32 indexCount, uint32 instanceCount, uint32 startIndex, int32 baseVertex, uint32 startInstance )
	{
		DrawIndexedInstancedIndirectCommand cmd{};
		cmd._indexCountPerInstance = indexCount;
		cmd._instanceCount		   = instanceCount;
		cmd._startIndexLocation	   = startIndex;
		cmd._baseVertexLocation	   = baseVertex;
		cmd._startInstanceLocation = startInstance;
		_listCommand.push_back( cmd );
	}

	void IndirectDrawBuffer::clear()
	{
		_listCommand.clear();
		_gpuBufferHandle = 0;
	}

	bool IndirectDrawBuffer::uploadToBuffer( IRHIResource* pResource )
	{
		if ( pResource == nullptr || _listCommand.empty() )
			return false;

		if ( _gpuBufferHandle != 0 )
		{
			pResource->destroyBuffer( _gpuBufferHandle );
			_gpuBufferHandle = 0;
		}

		RHIBufferDesc desc{};
		desc._sizeBytes	   = static_cast<uint32>( getBufferSizeInBytes() );
		desc._elementSize  = static_cast<uint32>( sizeof( DrawIndexedInstancedIndirectCommand ) );
		desc._elementCount = static_cast<uint32>( _listCommand.size() );
		desc._usage		   = RHIBufferUsage::IndirectArgs;
		desc._pInitialData = _listCommand.data();

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
		if ( pCmdList == nullptr || _gpuBufferHandle == 0 || _listCommand.empty() )
			return;

		for ( size_t cmdIndex = 0; cmdIndex < _listCommand.size(); ++cmdIndex )
		{
			const uint32 offsetInBytes = static_cast<uint32>( cmdIndex * sizeof( DrawIndexedInstancedIndirectCommand ) );
			pCmdList->drawIndexedIndirect( _gpuBufferHandle, offsetInBytes );
		}
	}
} // namespace sw
