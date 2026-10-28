/**
 * @file ParkLayoutVisualizer.cpp
 * @brief 뷰포트 시각화 themepark.layout(툴바 "Park")을 구현합니다. 배치 파일의 놀이기구 영역, 입구, 코스터 트랙을 씬 뷰에 겹쳐 그립니다.
 */
#include "pch.h"

#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorIconGlyphs.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Viewport/EditorViewportProjection.h"
#include "Editor/Viewport/EditorViewportVisualizer.h"

#include "GameFramework/Plugin/Editor/ThemePark/ParkLayoutPreview.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        /** @brief 배치 파일의 선분(놀이기구 영역 · 입구 · 트랙)과 놀이기구 이름을 그립니다. 이름 색은 테마 토큰 `_viewportLabel` 입니다. */
        void drawParkLayout( const EditorViewportVisualizerArgs& args )
        {
            ParkLayoutPreview* pPreview = findSubsystem<ParkLayoutPreview>();
            if ( pPreview == nullptr )
                return;
            ParkLayoutPreview& preview = *pPreview;
            (void)preview.refresh(); // 바뀌었을 때만 다시 읽는다. 읽기 실패는 refresh 가 알린다
            if ( preview.isLoaded() == false )
                return;
            vector<EditorWorldSegment> listSegment; // 프레임마다 지역으로 채운다(재사용 버퍼를 함수 정적으로 두면 모듈 언로드 뒤까지 남는다)
            preview.appendSegments( listSegment );
            for ( const EditorWorldSegment& segment : listSegment )
            {
                ImVec2 screenFrom;
                ImVec2 screenTo;
                if ( EditorViewportProjectionUtil::projectSegment( *args._pViewProj, segment._from, segment._to, args._canvasPos, args._canvasSize, screenFrom,
                                                                   screenTo ) == false )
                    continue;
                const ImU32 color = ImGui::ColorConvertFloat4ToU32( ImVec4{ segment._color._x, segment._color._y, segment._color._z, segment._color._w } );
                args._pDrawList->AddLine( screenFrom, screenTo, color, 1.5f );
            }
            const ImU32 colorLabel = EditorColorUtil::toImU32( EditorThemeUtil::getViewportLabelColor() );
            for ( const ParkRidePreview& ride : preview.getRides() )
            {
                ImVec2       labelPos;
                const float3 top = ride._position + float3{ 0.0f, ride._size._y + 0.5f, 0.0f };
                if ( EditorViewportProjectionUtil::projectPoint( *args._pViewProj, top, args._canvasPos, args._canvasSize, labelPos ) )
                    args._pDrawList->AddText( labelPos, colorLabel, ride._name.c_str() );
            }
        }
    } // namespace

    SW_EDITOR_VISUALIZER( ParkLayout, "themepark.layout", 900, editoricon::kMap, "Park", "ThemePark 배치 파일(rides.xml)의 놀이기구 발자국, 입구, 코스터 트랙", false,
                          &drawParkLayout );
} // namespace sw::editor
