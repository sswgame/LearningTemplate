/**
 * @file TerrainBrushVisualizer.cpp
 * @brief 지형 칠하기 도구 모드("Terrain") 토글입니다. 켜면 `TerrainBrushTool` 이 브러시 원 · 설정 창을 그리고 드래그로 높이 · 레이어 가중치를 고칩니다.
 */
#include "pch.h"

#include "Editor/Common/GUI/EditorIconGlyphs.h"
#include "Editor/Viewport/EditorViewportVisualizer.h"
#include "Editor/Viewport/TerrainBrushTool.h"

namespace sw::editor
{
    SW_EDITOR_VISUALIZER( TerrainBrush, "terrainbrush", 140, editoricon::kHeightfield, "Terrain",
                          "Terrain tool: drag on the terrain to sculpt or paint layers (brush settings window, Ctrl+Z undoes one drag)", false,
                          &TerrainBrushTool::process );
} // namespace sw::editor
