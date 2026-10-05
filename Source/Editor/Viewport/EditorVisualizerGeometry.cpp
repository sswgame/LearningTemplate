#include "pch.h"

#include "Editor/Viewport/EditorVisualizerGeometry.h"

#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"

#include "Engine/Graphics/Debug/DebugDrawQueue.h"
#include "Engine/Object/Component/2D/BoxCollider2DComponent.h"
#include "Engine/Physics/AABB.h"

namespace sw::editor
{
    EditorDebugDrawStats& EditorDebugDrawStats::get()
    {
        static EditorDebugDrawStats s_stats;
        return s_stats;
    }

    void EditorVisualizerGeometryUtil::computeColliderCorners( const BoxCollider2DComponent& collider, float3 ( &outArrCorner )[4] )
    {
        AABB box{};
        (void)collider.getWorldBox( box ); // 콜라이더는 늘 상자를 낸다
        const float32 centerX = ( box._min._x + box._max._x ) * 0.5f;
        const float32 centerY = ( box._min._y + box._max._y ) * 0.5f;
        const float32 halfX   = MathUtil::max( ( box._max._x - box._min._x ) * 0.5f, kMinColliderHalfExtent );
        const float32 halfY   = MathUtil::max( ( box._max._y - box._min._y ) * 0.5f, kMinColliderHalfExtent );
        const float32 worldZ  = box._min._z;

        outArrCorner[0] = float3{ centerX - halfX, centerY - halfY, worldZ };
        outArrCorner[1] = float3{ centerX + halfX, centerY - halfY, worldZ };
        outArrCorner[2] = float3{ centerX + halfX, centerY + halfY, worldZ };
        outArrCorner[3] = float3{ centerX - halfX, centerY + halfY, worldZ };
    }

    void EditorVisualizerGeometryUtil::appendDebugDrawSegments( const DebugDrawQueue& queue, bool bFlat2D, vector<EditorWorldSegment>& outListSegment )
    {
        for ( const DebugLine& line : queue.getVisibleLines() )
            outListSegment.push_back( EditorWorldSegment{ line._from, line._to, line._color } );

        // 구는 축마다 대원 하나씩 — 어느 방향에서 봐도 윤곽이 보인다. 2D 뷰는 XY 원 하나다.
        constexpr float32 kStep       = ( MathUtil::Pi * 2.0f ) / static_cast<float32>( kSphereCircleSegmentCount );
        const uint32      circleCount = bFlat2D ? 1u : 3u;
        for ( const DebugSphere& sphere : queue.getVisibleSpheres() )
        {
            for ( uint32 axisIndex = 0; axisIndex < circleCount; ++axisIndex )
            {
                for ( uint32 segmentIndex = 0; segmentIndex < kSphereCircleSegmentCount; ++segmentIndex )
                {
                    const float32 angleA = kStep * static_cast<float32>( segmentIndex );
                    const float32 angleB = kStep * static_cast<float32>( segmentIndex + 1 );
                    const float32 cosA   = MathUtil::cos( angleA ) * sphere._radius;
                    const float32 sinA   = MathUtil::sin( angleA ) * sphere._radius;
                    const float32 cosB   = MathUtil::cos( angleB ) * sphere._radius;
                    const float32 sinB   = MathUtil::sin( angleB ) * sphere._radius;
                    float3        offsetA{};
                    float3        offsetB{};
                    if ( axisIndex == 0 )
                    {
                        offsetA = float3{ cosA, sinA, 0.0f };
                        offsetB = float3{ cosB, sinB, 0.0f };
                    }
                    else if ( axisIndex == 1 )
                    {
                        offsetA = float3{ cosA, 0.0f, sinA };
                        offsetB = float3{ cosB, 0.0f, sinB };
                    }
                    else
                    {
                        offsetA = float3{ 0.0f, cosA, sinA };
                        offsetB = float3{ 0.0f, cosB, sinB };
                    }
                    outListSegment.push_back( EditorWorldSegment{ sphere._center + offsetA, sphere._center + offsetB, sphere._color } );
                }
            }
        }
    }

    bool EditorVisualizerGeometryUtil::isFlat2DView( bool bOrthographic, const float4x4& view )
    {
        if ( bOrthographic == false )
            return false;
        // 월드 Z 축이 뷰 공간에서 시선(뷰 Z)과 나란하면 XY 평면을 정면으로 보는 2D 뷰다.
        constexpr float32 kAlignedCosine = 0.999f;
        const float3      viewAxisZ      = float3::transformVector( float3{ 0.0f, 0.0f, 1.0f }, view ).normalize();
        return MathUtil::abs( viewAxisZ._z ) >= kAlignedCosine;
    }
} // namespace sw::editor
