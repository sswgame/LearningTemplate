/**
 * @file ParkLayoutVisualizer.cpp
 * @brief 뷰포트 시각화 themepark.layout(툴바 "Park")을 구현합니다. 배치 파일의 놀이기구 영역, 입구, 코스터 트랙을 씬 뷰 도형 값으로 모읍니다(그리기는 `EditorViewportVisualizer::drawAll`).
 */
#include "pch.h"

#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Viewport/EditorViewportVisualizer.h"
#include "Editor/Viewport/Model/EditorVisualizerDrawList.h"
#include "Editor/Viewport/Model/EditorVisualizerGeometry.h"

#include "Engine/Localization/LocText.h"

#include "GameFramework/Kits/Genre/Simulation/ThemePark/Editor/ParkLayoutPreview.h"

namespace sw::editor
{
    namespace
    {
        struct ParkLayoutVisualizerInternal
        {
            /** @brief 놀이기구 이름을 영역 위로 띄우는 높이(m)입니다. */
            static constexpr float32 kLabelLift = 0.5f;

            /** @brief 배치 파일의 선분(놀이기구 영역 · 입구 · 트랙)과 놀이기구 이름을 모읍니다. 이름 색은 테마 토큰 `Viewport.Label` 입니다. */
            static void gather( const EditorViewportVisualizerArgs& /*args*/, EditorVisualizerDrawList& outDrawList )
            {
                ParkLayoutPreview* pPreview = findSubsystem<ParkLayoutPreview>();
                if ( pPreview == nullptr )
                    return;
                (void)pPreview->refresh(); // 바뀌었을 때만 다시 읽는다. 읽기 실패는 refresh 가 알린다
                if ( pPreview->isLoaded() == false )
                    return;
                vector<EditorWorldSegment> listSegment; // 프레임마다 지역으로 채운다(재사용 버퍼를 함수 정적으로 두면 모듈 언로드 뒤까지 남는다)
                pPreview->appendSegments( listSegment );
                for ( const EditorWorldSegment& segment : listSegment )
                {
                    EditorVisualizerLine line{};
                    line._from  = segment._from;
                    line._to    = segment._to;
                    line._color = segment._color;
                    outDrawList.addLine( line );
                }
                for ( const ParkRidePreview& ride : pPreview->getRides() )
                {
                    EditorVisualizerText label{};
                    label._position  = ride._position + float3{ 0.0f, ride._size._y + kLabelLift, 0.0f };
                    label._text      = ride._name;
                    label._colorRole = EditorVisualizerColorRole::ThemeToken;
                    label._pThemeKey = "Viewport.Label";
                    label._bShadow   = false;
                    outDrawList._listText.push_back( label );
                }
            }
        };
    } // namespace

    SW_EDITOR_VISUALIZER_GATHER( ParkLayout, "themepark.layout", 900, SW_EDITOR_ICON_NAME( "Map" ), SW_LOCKEYREF( "EdPluginVisualizer", "ParkLayoutLabel" ),
                                 SW_LOCKEYREF( "EdPluginVisualizer", "ParkLayoutTip" ), false, nullptr, &ParkLayoutVisualizerInternal::gather );
} // namespace sw::editor
