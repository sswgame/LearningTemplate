#pragma once
/**
 * @file InstanceBuffer.h
 * @brief CPU-side instance transform staging buffer.
 */
#include "Core/Common/CommonHeaders.h"
#include "Core/Common/CommonMacros.h"
#include "Core/Common/Types.h"
#include "Core/Utility/Math/MatrixMath.h"

namespace sw
{
	/**
	 * @class InstanceBuffer
	 * @brief Collects per-instance float4x4 (or raw floats) for a draw call upload.
	 */
	class SW_API InstanceBuffer
	{
	public:
		InstanceBuffer() = default;

		void clear();
		void reserve( uint32 count );

		void push( const float4x4& matrix );
		void pushFloats( const float32* values, uint32 floatCount );

		const float4x4* data() const { return _matrices.empty() ? nullptr : _matrices.data(); }
		float4x4*		data() { return _matrices.empty() ? nullptr : _matrices.data(); }
		uint32			count() const { return static_cast<uint32>( _matrices.size() ); }
		bool			empty() const { return _matrices.empty(); }

		const float32* floats() const
		{
			return _matrices.empty() ? nullptr : reinterpret_cast<const float32*>( _matrices.data() );
		}
		uint32 floatCount() const { return count() * 16u; }

	private:
		std::vector<float4x4> _matrices;
	};
} // namespace sw
