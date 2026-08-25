/**
 * @file DebugDrawQueue.h
 * @brief CPU 디버그 프리미티브 큐 (선/구체). GPU 즉시 드로우가 아닙니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/FrameStlAllocator.h"

namespace sw
{
	/// @brief 한 프레임 디버그 선 (시작/끝/색)
	struct DebugLine
	{
		float3 _from{};
		float3 _to{};
		float4 _color{ 1.0f, 1.0f, 1.0f, 1.0f };
	};

	/// @brief 한 프레임 디버그 구체 (중심/반지름/색)
	struct DebugSphere
	{
		float3	_center{};
		float32 _radius{ 1.0f };
		float4	_color{ 1.0f, 1.0f, 1.0f, 1.0f };
	};

	/**
	 * @class DebugDrawQueue
	 * @brief 한 프레임 디버그 지오메트리 큐. Editor Game View 등이 ImGui로 소비합니다.
	 */
	class SW_API DebugDrawQueue
	{
	public:
		/** @brief 빈 디버그 드로우 큐. */
		DebugDrawQueue() = default;

		/** @brief 큐를 비웁니다. */
		void clear();

		/** @brief 선을 예약합니다. */
		void drawLine( const float3& from, const float3& to, const float4& color );
		/** @brief 구체를 예약합니다. */
		void drawSphere( const float3& center, float32 radius, const float4& color );

		/** @brief 예약된 선 목록을 반환합니다. */
		const sw::vector<DebugLine, FrameStlAllocator<DebugLine>>& getLines() const { return _listLines; }
		/** @brief 예약된 구체 목록을 반환합니다. */
		const sw::vector<DebugSphere, FrameStlAllocator<DebugSphere>>& getSpheres() const { return _listSpheres; }
		/** @brief 선 개수를 반환합니다. */
		uint32 getLineCount() const { return static_cast<uint32>( _listLines.size() ); }
		/** @brief 구체 개수를 반환합니다. */
		uint32 getSphereCount() const { return static_cast<uint32>( _listSpheres.size() ); }

	private:
		sw::vector<DebugLine, FrameStlAllocator<DebugLine>>		_listLines;
		sw::vector<DebugSphere, FrameStlAllocator<DebugSphere>> _listSpheres;
	};
} // namespace sw
