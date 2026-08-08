/**
 * @file InstantDebugDraw.cpp
 */
#include "InstantDebugDraw.h"

namespace sw
{
	void InstantDebugDraw::clear()
	{
		_lines.clear();
		_spheres.clear();
	}

	void InstantDebugDraw::drawLine( const float3& from, const float3& to, const float4& color )
	{
		DebugLine line{};
		line._from	= from;
		line._to	= to;
		line._color = color;
		_lines.push_back( line );
	}

	void InstantDebugDraw::drawSphere( const float3& center, float32 radius, const float4& color )
	{
		DebugSphere sphere{};
		sphere._center = center;
		sphere._radius = radius;
		sphere._color  = color;
		_spheres.push_back( sphere );
	}
} // namespace sw
