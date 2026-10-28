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
     * @brief 브러시 계산은 `TerrainBrush`(ImGui 없음)이고, 이 도구는 레이 맞추기 · 그리기 · 마우스 · 되돌리기 · 저장만 합니다(언리얼 Landscape Mode 의 Sculpt/Paint).
     * @details 마우스 레이가 지형에 닿는 동안 씬 뷰 마우스를 가져가(`EditorViewportMouseClaim`) 클릭 선택 · 기즈모가 물러난다. 드래그를 시작하면 그 지형의 높이(또는 스플랫 가중치) 전부를
     *          한 번 복사해 두고, 프레임마다 사본에 브러시를 적용해 그 프레임에 바뀐 사각형만 `TerrainComponent::writeHeights` · `writeLayerWeights` 로 넘긴다(프레임에 한 번).
     *          단추를 놓으면 드래그 전체가 닿은 사각형의 편집 전 · 후 값만 담아 되돌리기 하나(`Sculpt Terrain` · `Paint Terrain`)를 남긴다 — 되돌리기는 같은 사각형을 편집 전 값으로 다시 쓴다.
     *          쓴 값은 에셋 파일에 들어가지 않는다. 설정 창의 저장 단추가 `saveHeightfield` · `saveSplat` 로 원래 경로에 쓴다. 플레이 중에는 편집하지 않는다.
     */
    struct SW_EDITOR_API TerrainBrushTool
    {
        /** @brief 브러시 원 · 설정 창을 그리고 드래그를 처리합니다. 시각화 토글이 켜져 있으면 프레임마다 부릅니다. */
        static void process( const EditorViewportVisualizerArgs& args );
        /** @brief 브러시 설정입니다(설정 창과 개발 명령 `terrain.brush` 가 고칩니다). */
        static TerrainBrushSettings& getSettings();
    };
} // namespace sw::editor
