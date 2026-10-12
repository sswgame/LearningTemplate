#include "pch.h"

#include "Editor/Viewport/EditorProjectedLineRenderer.h"

#include "Editor/Common/GUI/EditorColorUtil.h"
#include "Editor/Viewport/EditorViewportProjection.h"

#include "Engine/Automation/AutomationProbe.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct EditorProjectedLineRendererInternal
        {
            /** @brief 디버그 선의 굵기(픽셀)입니다. 선택 외곽선(2.0)보다 가늘어 겹쳐도 구분된다. */
            static constexpr float32 kLineThickness = 1.5f;

            [[nodiscard]] static bool readPhysicsSegments( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                outValue = static_cast<float64>( EditorShapeVisualizerStats::get()._physicsSegmentCount );
                return true;
            }

            [[nodiscard]] static bool readNavMeshSegments( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                outValue = static_cast<float64>( EditorShapeVisualizerStats::get()._navMeshSegmentCount );
                return true;
            }
        };
    } // namespace

    EditorProjectedLineRenderer::EditorProjectedLineRenderer( ImDrawList* pDrawList, const float4x4& viewProjection, const float2& canvasPosition,
                                                              const float2& canvasSize )
        : _pDrawList{ pDrawList }
        , _viewProjection{ viewProjection }
        , _canvasPosition{ canvasPosition }
        , _canvasSize{ canvasSize }
        , _drawnSegmentCount{ 0 }
    {
    }

    void EditorProjectedLineRenderer::drawLine( const float3& from, const float3& to, const float4& color )
    {
        ImVec2 screenFrom;
        ImVec2 screenTo;
        if ( EditorViewportProjectionUtil::projectSegment( _viewProjection, from, to, _canvasPosition, _canvasSize, screenFrom, screenTo ) == false )
            return;
        _pDrawList->AddLine( screenFrom, screenTo, EditorColorUtil::toImU32( Color::fromFloat4( color ) ), EditorProjectedLineRendererInternal::kLineThickness );
        ++_drawnSegmentCount;
    }

    EditorShapeVisualizerStats& EditorShapeVisualizerStats::get()
    {
        static EditorShapeVisualizerStats s_stats;
        return s_stats;
    }
} // namespace sw::editor

namespace sw::editor
{
    SW_AUTOMATION_PROBE( editorPhysicsShapeSegments, "Editor.PhysicsShapeSegments", "Line segments the physics_shapes visualizer drew in the last frame",
                         &EditorProjectedLineRendererInternal::readPhysicsSegments );
    SW_AUTOMATION_PROBE( editorNavMeshSegments, "Editor.NavMeshSegments", "Line segments the navmesh visualizer drew in the last frame",
                         &EditorProjectedLineRendererInternal::readNavMeshSegments );
} // namespace sw::editor
