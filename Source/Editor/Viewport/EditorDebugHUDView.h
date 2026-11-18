/**
 * @file EditorDebugHUDView.h
 * @brief 씬 뷰 오른쪽 위에 디버그 HUD 섹션을 그립니다 — 섹션 줄은 Model(`EditorDebugHUDViewModel`)이 적고, 이 View 는 그 값만 ImGui 로 그립니다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

struct ImDrawList;

namespace sw::editor
{
    class EditorDebugHUDViewModel;

    /**
     * @class EditorDebugHUDView
     * @brief 씬 뷰의 HUD 상자입니다. 섹션 줄 · 그래프는 `EditorDebugHUDViewModel` 이 4 Hz 로 적어 두고, 이 상자는 그 값을 그린다. 상자를 켜고 끄는 것은 뷰마다다(툴바 `HUD`).
     */
    class EditorDebugHUDView
    {
    public:
        /** @brief 캔버스 오른쪽 위(@p topRightInset 만큼 안 — 붙은 오버레이 바 아래)에 @p model 의 섹션을 그립니다. 상자 아래 끝(캔버스 위 기준 픽셀)을 돌려줍니다. */
        float32 draw( ImDrawList* pDrawList, const float2& canvasPos, const float2& canvasSize, const float2& topRightInset, const EditorDebugHUDViewModel& model );
        /** @brief 지난 그리기에 담은 섹션 수입니다(감시 값 `Editor.SceneViewHUDSections`). */
        uint32 getDrawnSectionCount() const { return _drawnSectionCount; }

    private:
        uint32 _drawnSectionCount{ 0 };
    };
} // namespace sw::editor
