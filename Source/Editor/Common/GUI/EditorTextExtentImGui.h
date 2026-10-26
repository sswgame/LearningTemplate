/**
 * @file EditorTextExtentImGui.h
 * @brief `EditorTextExtent` 를 ImGui 크기 인자(`BeginChild` · `BeginTable`)로 바꾸는 입구입니다.
 */
#pragma once
#include "Editor/Common/GUI/EditorTextExtent.h"

#include <imgui.h>

namespace sw::editor
{
    /**
     * @brief 글자 칸 · 줄 수를 픽셀 `ImVec2` 로 바꿉니다. 0 인 축은 0(남은 영역을 채움)이고, 창 크기와 달리 뷰포트 비율로 자르지 않는다(부모 영역 안에 놓이는 크기).
     * @details child · table 크기를 픽셀 리터럴로 적지 않게 하는 입구다. 음수 칸은 ImGui 규칙대로 오른쪽(아래) 여백이다.
     */
    [[nodiscard]] inline ImVec2 measureExtent( const EditorTextExtent& extent )
    {
        const float32 width  = extent._columns != 0.0f ? extent._columns * ImGui::CalcTextSize( "0" ).x : 0.0f;
        const float32 height = extent._lines != 0.0f ? extent._lines * ImGui::GetTextLineHeightWithSpacing() : 0.0f;
        return ImVec2{ width, height };
    }
} // namespace sw::editor
