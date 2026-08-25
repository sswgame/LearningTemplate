#include "pch.h"

#include "Engine/Graphics/RenderPass/InstanceBuffer.h"

namespace sw
{
	void InstanceBuffer::clear()
	{
		_listMatrices.clear();
	}

	void InstanceBuffer::reserve( uint32 count )
	{
		_listMatrices.reserve( count );
	}

	void InstanceBuffer::push( const float4x4& matrix )
	{
		_listMatrices.push_back( matrix );
	}

	void InstanceBuffer::pushFloats( const float32* pValues, uint32 floatCount )
	{
		if ( pValues == nullptr || floatCount < 16 )
			return;
		const uint32 instanceCount = floatCount / 16u;
		for ( uint32 instanceIndex = 0; instanceIndex < instanceCount; ++instanceIndex )
		{
			_listMatrices.emplace_back( pValues + ( instanceIndex * 16u ) );
		}
	}
} // namespace sw
