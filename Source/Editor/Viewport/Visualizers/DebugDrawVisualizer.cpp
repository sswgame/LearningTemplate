/**
 * @file DebugDrawVisualizer.cpp
 * @brief 게임 코드가 `DebugDrawQueue` 에 넣은 선 · 구 · 상자 · 화살표 · 글자를 뷰포트에 그립니다(2D · 3D 뷰 모두)
 */
#include "pch.h"

#include "Editor/Viewport/EditorViewportProjection.h"
#include "Editor/Viewport/EditorViewportVisualizer.h"
#include "Editor/Viewport/EditorVisualizerGeometry.h"

#include "Engine/Graphics/Renderer/Debug/DebugDrawQueue.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct DebugDrawVisualizerInternal
        {
            static ImU32 toColor( const float4& color ) { return ImGui::ColorConvertFloat4ToU32( ImVec4( color._x, color._y, color._z, color._w ) ); }

            /**
             * @brief 디버그 큐가 확정한 도형을 그립니다. 구는 대원 셋(2D 뷰는 XY 원 하나), 선분은 근평면에서 잘라 투영합니다.
             * @details 큐는 `EngineLoop::endFrame` 이 지난 프레임에 넣은 것을 확정합니다 — 게임 업데이트든 씬 틱이든 넣은 것이 모두 보인다.
             *          재사용 버퍼 하나로 충분합니다(에디터 UI 는 게임 스레드 하나에서 돈다).
             */
            static void draw( const EditorViewportVisualizerArgs& args )
            {
                EditorDebugDrawStats& stats = EditorDebugDrawStats::get();
                stats._segmentCount         = 0;
                stats._textCount            = 0;
                stats._frame                = ImGui::GetFrameCount();
                if ( args._pDebugDrawQueue == nullptr )
                    return;

                static vector<EditorWorldSegment> s_listSegment;
                s_listSegment.clear();
                EditorVisualizerGeometryUtil::appendDebugDrawSegments( *args._pDebugDrawQueue, args._bFlat2D, s_listSegment );
                for ( const EditorWorldSegment& segment : s_listSegment )
                {
                    ImVec2 screenFrom;
                    ImVec2 screenTo;
                    if ( EditorViewportProjectionUtil::projectSegment( *args._pViewProj, segment._from, segment._to, args._canvasPos, args._canvasSize, screenFrom,
                                                                       screenTo ) == false )
                        continue;
                    args._pDrawList->AddLine( screenFrom, screenTo, toColor( segment._color ), 1.5f );
                    ++stats._segmentCount;
                }

                for ( const DebugText& text : args._pDebugDrawQueue->getVisibleTexts() )
                {
                    ImVec2 screen;
                    if ( EditorViewportProjectionUtil::projectPoint( *args._pViewProj, text._position, args._canvasPos, args._canvasSize, screen ) == false )
                        continue;
                    // 글자 그림자를 먼저 깔아 밝은 배경에서도 읽히게 한다.
                    args._pDrawList->AddText( ImVec2( screen.x + 1.0f, screen.y + 1.0f ), IM_COL32( 0, 0, 0, 200 ), text._text.c_str() );
                    args._pDrawList->AddText( screen, toColor( text._color ), text._text.c_str() );
                    ++stats._textCount;
                }
            }
        };
    } // namespace

    SW_EDITOR_VISUALIZER( DebugDraw, "debug_draw", 300, "Dbg", "게임 코드가 DebugDrawQueue 에 넣은 선 · 구 · 상자 · 화살표 · 글자를 표시합니다", true,
                          &DebugDrawVisualizerInternal::draw );
} // namespace sw::editor
