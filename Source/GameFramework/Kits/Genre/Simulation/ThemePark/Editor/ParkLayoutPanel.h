/**
 * @file ParkLayoutPanel.h
 * @brief Park Layout 패널을 구현합니다. 배치 파일의 놀이기구 표(비용, 정원, 시간, 탑승료, 배치 문제)를 거르고 정렬해 보여 주며, 더블클릭하면 에디터 카메라를 그 놀이기구로 옮깁니다.
 */
#pragma once
#include "Core/Foundation/Constants.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/GUI/Panel/IEditorPanel.h"

namespace sw::editor
{
    /**
     * @class ParkLayoutPanel
     * @brief ThemePark 키트의 에디터 확장 패널입니다. 키트 데이터를 읽기만 하므로 어느 게임으로 띄워도 동작합니다. 판단은 모델(`ParkLayoutModel`)이고 이 패널은 그리기만 합니다.
     */
    // [doc:panel-class]
    class ParkLayoutPanel final : public IEditorPanel
    {
    public:
        /** @brief 패널 id 입니다. 등록(`SW_EDITOR_PANEL`)과 열기(`EditorPanelManager::setPanelOpen<…>`)가 모두 이 상수에서 온다. */
        static constexpr const utf8* kPanelID = "themepark.layout";
        /** @brief 창 제목입니다(ImGui 창 이름 — 자체 테스트가 창을 찾을 때 이 상수를 쓴다). */
        static constexpr const utf8* kPanelTitle = "Park Layout";

        ParkLayoutPanel();

        const utf8* getPanelTitle() const override { return kPanelTitle; }
        void        drawContent() override;

    private:
        fixed_string<constant::buffer::kMax64> _filter; ///< 놀이기구 검색어
    };
    // [/doc:panel-class]
} // namespace sw::editor
