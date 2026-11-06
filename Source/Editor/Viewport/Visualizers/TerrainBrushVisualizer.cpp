/**
 * @file TerrainBrushVisualizer.cpp
 * @brief 지형 칠하기 도구 모드("Terrain") 토글입니다. 켜면 `TerrainBrushTool` 이 브러시 원 · 설정 창을 그리고 드래그로 높이 · 레이어 가중치를 고칩니다.
 */
#include "pch.h"

#include "Editor/Common/Config/EditorIconGlyphTable.h"
#include "Editor/Viewport/EditorViewportVisualizer.h"
#include "Editor/Viewport/TerrainBrushTool.h"

#include "Engine/Localization/LocText.h"

namespace sw::editor
{
    SW_EDITOR_VISUALIZER( TerrainBrush, "terrainbrush", 140, SW_EDITOR_ICON_NAME( "Heightfield" ), SW_LOCKEYREF( "EdVisualizer", "TerrainBrushLabel" ),
                          SW_LOCKEYREF( "EdVisualizer", "TerrainBrushTip" ), false,
                          &TerrainBrushTool::process );
} // namespace sw::editor
