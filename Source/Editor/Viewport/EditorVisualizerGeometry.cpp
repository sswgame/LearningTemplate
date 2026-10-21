#include "pch.h"

#include "Editor/Viewport/EditorVisualizerGeometry.h"

#include "Core/Math/AABB.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"

#include "Engine/Graphics/Debug/DebugDrawQueue.h"
#include "Engine/Object/Component/2D/BoxCollider2DComponent.h"

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
        // 펼치는 규칙(구는 축마다 대원 하나)은 큐 한 곳에 있다 — 게임 뷰 겹치기(DebugDrawCanvasUtil)와 같은 선분을 그린다.
        static vector<DebugLine> s_listLine;
        s_listLine.clear();
        queue.appendVisibleSegments( bFlat2D, s_listLine );
        for ( const DebugLine& line : s_listLine )
        {
            outListSegment.push_back( EditorWorldSegment{ line._from, line._to, line._color } );
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
