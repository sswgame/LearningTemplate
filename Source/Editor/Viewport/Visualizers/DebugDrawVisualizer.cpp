/**
 * @file DebugDrawVisualizer.cpp
 * @brief 게임 코드가 `DebugDrawQueue` 에 넣은 이번 프레임의 선 · 구를 뷰포트에 그립니다
 */
#include "pch.h"

#include "Editor/Viewport/EditorViewportProjection.h"
#include "Editor/Viewport/EditorViewportVisualizer.h"
#include "Editor/Viewport/EditorVisualizerGeometry.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct DebugDrawVisualizerInternal
        {
            /**
             * @brief 디버그 큐의 선 · 구를 그립니다(구는 대원 셋). 근평면에서 자른 선분으로 투영합니다.
             * @details 큐는 `EngineLoop::endFrame` 이 비웁니다 — 에디터 UI(`updateEditorUi`)보다 먼저 채운 것(게임 업데이트)이 보입니다.
             *          스레드 · 저장이 없는 한 프레임 큐라 재사용 버퍼 하나로 충분합니다(에디터 UI 는 게임 스레드 하나에서 돈다).
             */
            static void draw( const EditorViewportVisualizerArgs& args )
            {
                if ( args._pDebugDrawQueue == nullptr )
                    return;
                static vector<EditorWorldSegment> s_listSegment;
                s_listSegment.clear();
                EditorVisualizerGeometryUtil::appendDebugDrawSegments( *args._pDebugDrawQueue, s_listSegment );
                for ( const EditorWorldSegment& segment : s_listSegment )
                {
                    ImVec2 screenFrom;
                    ImVec2 screenTo;
                    if ( EditorViewportProjectionUtil::projectSegment( *args._pViewProj, segment._from, segment._to, args._canvasPos, args._canvasSize, screenFrom,
                                                                       screenTo ) == false )
                        continue;
                    const ImU32 color = ImGui::ColorConvertFloat4ToU32( ImVec4( segment._color._x, segment._color._y, segment._color._z, segment._color._w ) );
                    args._pDrawList->AddLine( screenFrom, screenTo, color, 1.5f );
                }
            }
        };
    } // namespace

    SW_EDITOR_VISUALIZER( DebugDraw, "debug_draw", 300, "Dbg", "게임 코드가 DebugDrawQueue 에 넣은 선 · 구를 표시합니다", true,
                          &DebugDrawVisualizerInternal::draw );
} // namespace sw::editor
