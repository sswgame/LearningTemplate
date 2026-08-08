#pragma once
/**
 * @file InstantDebugDraw.h
 * @brief CPU-side immediate debug primitives (lines / spheres), cleared each frame.
 */
#include "Core/Common/CommonHeaders.h"
#include "Core/Common/CommonMacros.h"
#include "Core/Common/Types.h"
#include "Core/Utility/Math/VectorMath.h"

namespace sw
{
	struct DebugLine
	{
		float3 _from{};
		float3 _to{};
		float4 _color{ 1.0f, 1.0f, 1.0f, 1.0f };
	};

	struct DebugSphere
	{
		float3	_center{};
		float32 _radius = 1.0f;
		float4	_color{ 1.0f, 1.0f, 1.0f, 1.0f };
	};

	/**
	 * @class InstantDebugDraw
	 * @brief Queues debug geometry for a future debug render pass.
	 */
	class SW_API InstantDebugDraw
	{
	public:
		InstantDebugDraw() = default;

		void clear();

		void drawLine( const float3& from, const float3& to, const float4& color );
		void drawSphere( const float3& center, float32 radius, const float4& color );

		const std::vector<DebugLine>&	getLines() const { return _lines; }
		const std::vector<DebugSphere>& getSpheres() const { return _spheres; }
		uint32							getLineCount() const { return static_cast<uint32>( _lines.size() ); }
		uint32							getSphereCount() const { return static_cast<uint32>( _spheres.size() ); }

	private:
		std::vector<DebugLine>	 _lines;
		std::vector<DebugSphere> _spheres;
	};
} // namespace sw
