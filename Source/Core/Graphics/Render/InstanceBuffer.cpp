/**
 * @file InstanceBuffer.cpp
 */
#include "InstanceBuffer.h"

namespace sw
{
	void InstanceBuffer::clear()
	{
		_matrices.clear();
	}

	void InstanceBuffer::reserve( uint32 count )
	{
		_matrices.reserve( count );
	}

	void InstanceBuffer::push( const float4x4& matrix )
	{
		_matrices.push_back( matrix );
	}

	void InstanceBuffer::pushFloats( const float32* values, uint32 floatCount )
	{
		if ( values == nullptr || floatCount < 16 )
			return;
		const uint32 instanceCount = floatCount / 16u;
		for ( uint32 i = 0; i < instanceCount; ++i )
			_matrices.emplace_back( values + ( i * 16u ) );
	}
} // namespace sw
