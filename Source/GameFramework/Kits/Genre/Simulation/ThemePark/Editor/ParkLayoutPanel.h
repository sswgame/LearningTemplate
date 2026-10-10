/**
 * @file ParkLayoutPanel.h
 * @brief Park Layout 패널 — 배치 파일의 놀이기구 목록(비용 · 정원 · 탑승 시간), 더블클릭하면 에디터 카메라를 그 놀이기구로 옮깁니다.
 */
#pragma once
#include "Editor/Common/GUI/IEditorPanel.h"

namespace sw::editor
{
    /**
     * @class ParkLayoutPanel
     * @brief ThemePark 키트의 에디터 확장 패널입니다. 키트 데이터를 읽기만 하므로 어느 게임으로 띄워도 돕니다.
     */
    class ParkLayoutPanel final : public IEditorPanel
    {
    public:
        ParkLayoutPanel();

        const utf8* getPanelTitle() const override { return "Park Layout"; }
        bool        isToolPanel() const override { return true; }
        void        drawContent() override;
    };
} // namespace sw::editor
