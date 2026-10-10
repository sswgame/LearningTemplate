/**
 * @file ParkLayoutVisualizer.cpp
 * @brief 뷰포트 시각화 themepark.layout(툴바 "Park") — 배치 파일의 놀이기구 발자국 · 입구 · 코스터 트랙을 씬 뷰에 겹쳐 그립니다.
 */
#include "pch.h"

#include "Editor/Viewport/EditorViewportProjection.h"
#include "Editor/Viewport/EditorViewportVisualizer.h"

#include "GameFramework/Kits/Genre/Simulation/ThemePark/Editor/ParkLayoutPreview.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct ParkLayoutVisualizerInternal
        {
            static void draw( const EditorViewportVisualizerArgs& args )
            {
                ParkLayoutPreview& preview = ParkLayoutPreview::get();
                (void)preview.refresh(); // 바뀌었을 때만 다시 읽는다 — 읽기 실패는 refresh 가 알린다
                if ( preview.isLoaded() == false )
                    return;
                static vector<EditorWorldSegment> s_listSegment; // 프레임마다 다시 채우는 재사용 버퍼
                s_listSegment.clear();
                preview.appendSegments( s_listSegment );
                for ( const EditorWorldSegment& segment : s_listSegment )
                {
                    ImVec2 screenFrom;
                    ImVec2 screenTo;
                    if ( EditorViewportProjectionUtil::projectSegment( *args._pViewProj, segment._from, segment._to, args._canvasPos, args._canvasSize, screenFrom,
                                                                       screenTo ) == false )
                        continue;
                    const ImU32 color = ImGui::ColorConvertFloat4ToU32( ImVec4{ segment._color._x, segment._color._y, segment._color._z, segment._color._w } );
                    args._pDrawList->AddLine( screenFrom, screenTo, color, 1.5f );
                }
                for ( const ParkRidePreview& ride : preview.getRides() )
                {
                    ImVec2       labelPos;
                    const float3 top = ride._position + float3{ 0.0f, ride._size._y + 0.5f, 0.0f };
                    if ( EditorViewportProjectionUtil::projectPoint( *args._pViewProj, top, args._canvasPos, args._canvasSize, labelPos ) )
                        args._pDrawList->AddText( labelPos, IM_COL32( 255, 230, 120, 255 ), ride._name.c_str() );
                }
            }
        };
    } // namespace

    SW_EDITOR_VISUALIZER( ParkLayout, "themepark.layout", 900, "Park", "ThemePark 배치 파일(rides.xml)의 놀이기구 발자국, 입구, 코스터 트랙", false,
                          &ParkLayoutVisualizerInternal::draw );
} // namespace sw::editor
