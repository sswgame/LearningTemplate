/**
 * @file DataLayersPanel.h
 * @brief 데이터 레이어 창 — 활성 씬의 레이어를 보고 전환합니다(언리얼 Data Layers Outliner 자리). 값은 `DataLayersViewModel::gather` 가 모으고 명령도 그쪽이 받으며, 이 패널은 그리기만 합니다(MVVM 계약).
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Editor/Common/GUI/Panel/IEditorPanel.h"
#include "Editor/Panels/Scene/Model/DataLayersViewModel.h"

namespace sw::editor
{
    /** @brief 데이터 레이어 도구 창입니다. */
    class DataLayersPanel : public IEditorPanel
    {
    public:
        /** @brief 패널 id 입니다(`SW_EDITOR_PANEL`, `-gv_editorOpenPanel`). */
        static constexpr const utf8* kPanelID = "data_layers";

        /** @brief 창을 만듭니다(닫힌 채 시작한다). */
        DataLayersPanel();

        const utf8* getPanelTitle() const override { return "Data Layers"; }
        void        drawContent() override;

    private:
        DataLayersViewModel _viewModel;
        DataLayersViewState _viewState; ///< 프레임마다 다시 채운다(목록 버퍼를 다시 쓴다)
    };
} // namespace sw::editor
