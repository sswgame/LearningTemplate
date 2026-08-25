/**
 * @file InstanceBuffer.h
 * @brief 드로우 업로드용 CPU 인스턴스 변환 스테이징
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/MatrixMath.h"

namespace sw
{
	/**
	 * @class InstanceBuffer
	 * @brief 드로우 업로드용 인스턴스 float4x4 (또는 raw float32)를 모읍니다.
	 */
	class SW_API InstanceBuffer
	{
	public:
		/** @brief 빈 인스턴스 행렬 스테이징. */
		InstanceBuffer() = default;

		/** @brief 비웁니다. */
		void clear();
		/** @brief 용량을 예약합니다. */
		void reserve( uint32 count );

		/** @brief 행렬을 넣습니다. */
		void push( const float4x4& matrix );
		/** @brief float32 배열을 행렬로 넣습니다. */
		void pushFloats( const float32* pValues, uint32 floatCount );

		/** @brief 행렬 배열 포인터. 비었으면 nullptr. */
		const float4x4* data() const { return _listMatrices.empty() ? nullptr : _listMatrices.data(); }
		/** @brief 행렬 배열 포인터. 비었으면 nullptr. */
		float4x4* data() { return _listMatrices.empty() ? nullptr : _listMatrices.data(); }
		/** @brief 크기를 반환합니다. */
		uint32 count() const { return static_cast<uint32>( _listMatrices.size() ); }
		/** @brief 비었는지 반환합니다. */
		bool empty() const { return _listMatrices.empty(); }

		/** @brief 행렬을 float32 배열로 봅니다 (인스턴스당 16개). */
		const float32* floats() const { return _listMatrices.empty() ? nullptr : reinterpret_cast<const float32*>( _listMatrices.data() ); }
		/** @brief 개수를 반환합니다. */
		uint32 floatCount() const { return count() * 16u; }

	private:
		vector<float4x4> _listMatrices;
	};
} // namespace sw
