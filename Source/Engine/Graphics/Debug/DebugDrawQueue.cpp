#include "pch.h"

#include "Engine/Graphics/Debug/DebugDrawQueue.h"

namespace sw
{
    void DebugDrawQueue::clear()
    {
        _listLine.clear();
        _listSphere.clear();
    }

    void DebugDrawQueue::drawLine( const float3& from, const float3& to, const float4& color )
    {
        DebugLine line{};
        line._from  = from;
        line._to    = to;
        line._color = color;
        _listLine.push_back( line );
    }

    void DebugDrawQueue::drawSphere( const float3& center, float32 radius, const float4& color )
    {
        DebugSphere sphere{};
        sphere._center = center;
        sphere._radius = radius;
        sphere._color  = color;
        _listSphere.push_back( sphere );
    }
} // namespace sw
