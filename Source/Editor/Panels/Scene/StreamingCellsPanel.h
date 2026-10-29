/**
 * @file StreamingCellsPanel.h
 * @brief 월드 셀 창 — 스트리밍하는 씬을 쿠커처럼 셀로 나눠 셀마다 실릴 엔티티와 게임 카메라 자리에서의 올림 · 내림을 미리 보고, 쿠킹이 실패할 셀을 넘는 참조를 짚습니다.
 * @details 언리얼 World Partition 창의 셀 보기 · 맵 검사 자리입니다(D1-7). 나누기는 `SceneCooker::previewStreamingCells` 한 길이라
 *          쿠커와 같은 결과다. 씬을 한 번 더 짓는 무거운 일이라 단추로만 다시 계산하고, 활성 씬이 바뀌면 결과를 버린다.
 *          셀의 상태(올림 · 유지 · 내림)는 게임 카메라에서 셀까지의 거리를 격자의 로딩 반경과 반경 + 여유(`SceneSettings::_unloadMargin`)로 비교한 것으로,
 *          `SceneStreamingSubsystem` 의 갱신 규칙과 같다. 쿠킹본으로 지은 씬(스트리밍 중)은 문서로 쓰지 않으므로 나눠 보지 않고 올라 있는 셀 수만 보인다.
 *          격자 선은 `gv_worldCellDebugDraw` 가 씬 뷰에 그린다. 상태와 명령은 `StreamingCellsModel`(Model/, ImGui 없음)이 들고, 이 패널은 그리기만 한다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Editor/Common/GUI/Panel/IEditorPanel.h"
#include "Editor/Panels/Scene/Model/StreamingCellsModel.h"

namespace sw
{
    class Scene;
} // namespace sw

namespace sw::editor
{
    /** @brief 월드 셀 미리보기 도구 창입니다. */
    class StreamingCellsPanel : public IEditorPanel
    {
    public:
        /** @brief 패널 id 입니다(`SW_EDITOR_PANEL`, `-gv_editorOpenPanel`). */
        static constexpr const utf8* kPanelID = "world_cells";

        /** @brief 월드 셀 창을 만듭니다(닫힌 채 시작한다). */
        StreamingCellsPanel();

        const utf8* getPanelTitle() const override { return "Streaming Cells"; }
        void        drawContent() override;
        bool        isToolPanel() const override { return true; }

        /** @brief 미리보기 상태와 명령입니다. */
        StreamingCellsModel& getModel() { return _model; }

    private:
        /** @brief 셀을 넘는 참조 표 — 줄을 누르면 가리키는 쪽 오브젝트를 고른다. */
        void drawViolations();
        /** @brief 셀 표 — 좌표 · 엔티티 수 · 상태. 줄을 누르면 그 셀의 엔티티를 고른다. */
        void drawCells( Scene& scene );

        StreamingCellsModel _model;
    };
} // namespace sw::editor
