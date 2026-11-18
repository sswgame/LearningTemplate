/**
 * @file TerrainBrushTool.h
 * @brief 지형 칠하기 도구 모드("Terrain" 시각화 토글)입니다. 브러시 원을 그리고, 왼쪽 단추 드래그로 높이 · 레이어 가중치 · 구멍을 고치고, 드래그 하나를 되돌리기 하나로 남깁니다.
 *        설정 창의 해상도 줄(`terrain.resize`)은 지형을 다시 표본화하고 전후 전체를 되돌리기 하나(`Resize Terrain`)로 남깁니다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Editor/Common/EditorExports.h"
#include "Editor/Viewport/Model/TerrainBrush.h"

namespace sw::editor
{
    struct EditorViewportVisualizerArgs;

    /**
     * @struct TerrainBrushTool
     * @brief 지형 칠하기 도구 View 입니다 — 브러시 원 · 설정 창을 그리고 마우스를 명령으로 냅니다. 레이 맞추기 · 붓질 · 되돌리기 · 저장은 `TerrainBrushEdit`(Model), 계산은 `TerrainBrush`.
     * @details 마우스 레이가 지형에 닿는 동안 씬 뷰 마우스를 가져가(`EditorViewportMouseCapture`) 클릭 선택 · 기즈모가 물러난다. 드래그 하나가 되돌리기 하나로 남는다(언리얼 Landscape Mode 의 Sculpt/Paint).
     *          플레이 중에는 편집하지 않는다.
     */
    struct SW_EDITOR_API TerrainBrushTool
    {
        /** @brief 브러시 원 · 설정 창을 그리고 드래그를 처리합니다. 시각화 토글이 켜져 있으면 프레임마다 부릅니다. */
        static void process( const EditorViewportVisualizerArgs& args );
    };
} // namespace sw::editor
